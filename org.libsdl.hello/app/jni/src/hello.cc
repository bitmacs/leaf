/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include "semaphores.h"
#include "vk.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_FRAMES_IN_FLIGHT 2

struct AppState {
    uint32_t width;
    uint32_t height;
};
static SDL_Window *window = NULL;

static VkContext vk_context = {};
static Uint64 last_frame_time = 0;
static Uint32 frame_index = 0;

static std::vector<VkFence> fences = {}; // each in-flight frame has a fence
static std::vector<VkCommandBuffer> command_buffers = {}; // each in-flight frame has one command buffer
static std::vector<VkDescriptorPool> descriptor_pools = {}; // each in-flight frame has one descriptor pool
static std::vector<VkDescriptorSet> descriptor_sets = {}; // each in-flight frame has one descriptor set
static std::vector<VkSemaphore> image_acquired_semaphores = {}; // each swapchain image has one image acquired semaphore
static std::vector<VkSemaphore> render_complete_semaphores = {}; // each swapchain image has one render complete semaphore
static SemaphorePool semaphore_pool = {}; // currently used for image acquired semaphores and render complete semaphores of each swapchain image, each in-flight frame has one semaphore pool

// 离屏渲染资源（每个 in-flight 帧一份）
std::vector<VkImage> depth_images;
std::vector<VkImage> color_images;
std::vector<VkDeviceMemory> depth_image_memories;
std::vector<VkDeviceMemory> color_image_memories;
std::vector<VkImageView> depth_image_views;
std::vector<VkImageView> color_image_views;
std::vector<VkFramebuffer> framebuffers;

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **pp_app_state, int argc, char *argv[])
{
    bool init_succeed = SDL_Init(SDL_INIT_VIDEO);
    assert(init_succeed);

    AppState *app_state = new AppState;
    app_state->width = 800;
    app_state->height = 600;
    *pp_app_state = app_state;

    window = SDL_CreateWindow("gfx demo", app_state->width, app_state->height, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    assert(window);

    create_vulkan_instance(&vk_context);
    create_vulkan_surface(&vk_context, window);
    choose_physical_device(&vk_context);
    create_device(&vk_context);
    create_swapchain(&vk_context, app_state->width, app_state->height);
    vk_context.depth_image_format = VK_FORMAT_D16_UNORM;
    create_command_pool(&vk_context);
    create_render_pass(&vk_context);

    image_acquired_semaphores.resize(vk_context.swapchain_images.size(), VK_NULL_HANDLE);
    render_complete_semaphores.resize(vk_context.swapchain_images.size(), VK_NULL_HANDLE);

    fences.resize(MAX_FRAMES_IN_FLIGHT);
    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkResult result = vkCreateFence(vk_context.device, &fence_create_info, nullptr, &fences[i]);
        assert(result == VK_SUCCESS);
    }

    command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    allocate_command_buffers(&vk_context, MAX_FRAMES_IN_FLIGHT, command_buffers.data());

    color_images.resize(MAX_FRAMES_IN_FLIGHT);
    depth_images.resize(MAX_FRAMES_IN_FLIGHT);
    color_image_memories.resize(MAX_FRAMES_IN_FLIGHT);
    depth_image_memories.resize(MAX_FRAMES_IN_FLIGHT);
    color_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    depth_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    framebuffers.resize(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_image(&vk_context, vk_context.surface_format, app_state->width, app_state->height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &color_images[i], &color_image_memories[i]);
        create_image_view(&vk_context, color_images[i], vk_context.surface_format, VK_IMAGE_ASPECT_COLOR_BIT, &color_image_views[i]);

        create_image(&vk_context, vk_context.depth_image_format, app_state->width, app_state->height, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &depth_images[i], &depth_image_memories[i]);
        create_image_view(&vk_context, depth_images[i], vk_context.depth_image_format, VK_IMAGE_ASPECT_DEPTH_BIT, &depth_image_views[i]);

        VkImageView attachments[] = {color_image_views[i], depth_image_views[i]};
        create_framebuffer(&vk_context, vk_context.render_pass, std::size(attachments), attachments, app_state->width, app_state->height, &framebuffers[i]);
    }

    last_frame_time = SDL_GetTicksNS(); // 初始化第一帧的时间
    return SDL_APP_CONTINUE;
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *p_app_state, SDL_Event *event)
{
    if (event->type == SDL_EVENT_KEY_DOWN ||
        event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    return SDL_APP_CONTINUE;
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *p_app_state)
{
    AppState *app_state = (AppState *) p_app_state;

    // 计算 delta time（以秒为单位）
    Uint64 current_time = SDL_GetTicksNS();
    Uint64 delta_time_ns = current_time - last_frame_time;
    double delta_time = (double) delta_time_ns / 1e9;  // 转换为秒
    last_frame_time = current_time;

    // SDL_Log("Delta time: %.6f ms (%.2f FPS)", delta_time * 1000, 1.0 / delta_time);

    // wait for the fence
    VkResult result = vkWaitForFences(vk_context.device, 1, &fences[frame_index], VK_TRUE, UINT64_MAX);
    assert(result == VK_SUCCESS);
    result = vkResetFences(vk_context.device, 1, &fences[frame_index]);
    assert(result == VK_SUCCESS);

    // acquire the next image
    uint32_t image_index;
    VkSemaphore image_acquired_semaphore = semaphore_pool.acquire_semaphore(&vk_context);
    result = vkAcquireNextImageKHR(vk_context.device, vk_context.swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &image_index);
    assert(result == VK_SUCCESS);
    if (image_acquired_semaphores[image_index] != VK_NULL_HANDLE) { semaphore_pool.return_semaphore(image_acquired_semaphores[image_index]); }
    image_acquired_semaphores[image_index] = image_acquired_semaphore;

    // record the command buffer
    VkCommandBuffer command_buffer = command_buffers[frame_index];
    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    assert(result == VK_SUCCESS);

    VkClearValue clear_values[2] = {};
    // clear_values[0].color = {.float32 = {0.5f, 0.8f, 1.0f, 1.0f}}; // 轻松活泼的天空蓝色 (RGB: 128, 204, 255)
    clear_values[0].color = {.float32 = {0.5f, 1.0f, 0.8f}}; // 薄荷绿
    clear_values[1].depthStencil = {.depth = 1.0f, .stencil = 0};
    begin_render_pass(&vk_context, command_buffer, vk_context.render_pass, framebuffers[frame_index], app_state->width, app_state->height, 2, clear_values);
    end_render_pass(&vk_context, command_buffer);

    {
        // 转换 color image layout 为 TRANSFER_SRC_OPTIMAL
        record_pipeline_image_barrier(command_buffer, color_images[frame_index],
                                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      VK_ACCESS_TRANSFER_READ_BIT,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // 转换 swapchain image layout 为 TRANSFER_DST_OPTIMAL
        record_pipeline_image_barrier(command_buffer, vk_context.swapchain_images[image_index],
                                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      0,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      VK_IMAGE_LAYOUT_UNDEFINED, // acquire 后通常是 UNDEFINED
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        blit_image(command_buffer, color_images[frame_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_context.swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, app_state->width, app_state->height);

        // 转换 swapchain image layout 为 PRESENT_SRC
        record_pipeline_image_barrier(command_buffer, vk_context.swapchain_images[image_index],
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      0, // present 操作不需要特定的 access mask
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    vkEndCommandBuffer(command_buffer);

    // submit the command buffer
    VkSemaphore render_complete_semaphore = semaphore_pool.acquire_semaphore(&vk_context);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    // vkAcquireNextImageKHR 的 semaphore 在图像可用时被 signal，通常发生在 COLOR_ATTACHMENT_OUTPUT_BIT 阶段
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[frame_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;
    result = vkQueueSubmit(vk_context.queue, 1, &submit_info, fences[frame_index]);
    assert(result == VK_SUCCESS);
    if (render_complete_semaphores[image_index] != VK_NULL_HANDLE) { semaphore_pool.return_semaphore(render_complete_semaphores[image_index]); }
    render_complete_semaphores[image_index] = render_complete_semaphore;

    // present the image
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &vk_context.swapchain;
    present_info.pImageIndices = &image_index;
    result = vkQueuePresentKHR(vk_context.queue, &present_info);
    assert(result == VK_SUCCESS);

    // const char *message = "Hello World!";
    // int w = 0, h = 0;
    // float x, y;
    // const float scale = 4.0f;
    //
    // /* Center the message and scale it up */
    // SDL_GetRenderOutputSize(renderer, &w, &h);
    // SDL_SetRenderScale(renderer, scale, scale);
    // x = ((w / scale) - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * SDL_strlen(message)) / 2;
    // y = ((h / scale) - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2;
    //
    // /* Draw the message */
    // SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    // SDL_RenderClear(renderer);
    // SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    // SDL_RenderDebugText(renderer, x, y, message);
    // SDL_RenderPresent(renderer);

    frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;

    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *p_app_state, SDL_AppResult result)
{
    vkDeviceWaitIdle(vk_context.device);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyFramebuffer(vk_context.device, framebuffers[i], nullptr);
        vkDestroyImageView(vk_context.device, color_image_views[i], nullptr);
        vkDestroyImageView(vk_context.device, depth_image_views[i], nullptr);
        vkDestroyImage(vk_context.device, color_images[i], nullptr);
        vkDestroyImage(vk_context.device, depth_images[i], nullptr);
        vkFreeMemory(vk_context.device, color_image_memories[i], nullptr);
        vkFreeMemory(vk_context.device, depth_image_memories[i], nullptr);
    }
    framebuffers.clear();
    color_image_views.clear();
    depth_image_views.clear();
    color_images.clear();
    depth_images.clear();
    color_image_memories.clear();
    depth_image_memories.clear();
    vkFreeCommandBuffers(vk_context.device, vk_context.command_pool, MAX_FRAMES_IN_FLIGHT, command_buffers.data());
    command_buffers.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyFence(vk_context.device, fences[i], nullptr);
    }
    fences.clear();
    for (size_t i = 0; i < vk_context.swapchain_images.size(); ++i) {
        if (image_acquired_semaphores[i] != VK_NULL_HANDLE) { semaphore_pool.return_semaphore(image_acquired_semaphores[i]); }
    }
    image_acquired_semaphores.clear();
    for (size_t i = 0; i < vk_context.swapchain_images.size(); ++i) {
        if (render_complete_semaphores[i] != VK_NULL_HANDLE) { semaphore_pool.return_semaphore(render_complete_semaphores[i]); }
    }
    render_complete_semaphores.clear();
    semaphore_pool.cleanup(&vk_context);
    cleanup_vulkan(&vk_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_Log("see you");
}


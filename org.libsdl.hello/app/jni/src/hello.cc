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
#include "camera.h"
#include "files.h"
#include "geometries.h"
#include "semaphores.h"
#include "tasks.h"
#include "vk.h"
#include <glm/gtc/quaternion.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#define MAX_FRAMES_IN_FLIGHT 2

struct FrameState {
    std::unordered_set<uint32_t> geometry_handles; // keep track of which geometries are used in this frame
};

struct CameraData {
    glm::mat4 view;
    glm::mat4 projection;
};

struct Transform {
    glm::vec3 position;
    glm::quat orientation;
    glm::vec3 scale;
};

struct Entity {
    uint32_t entity_id;
    uint32_t geometry_handle;
    Transform transform;
    glm::vec3 color;
};

struct Renderable {
    uint32_t entity_id;
    uint32_t geometry_handle;
    glm::mat4 model;
    glm::vec3 color;
};

struct AppState {
    uint32_t width;
    uint32_t height;
};

static TaskSystem task_system = {};
static GeometryRegistry geometry_registry = {};
static SDL_Window *window = NULL;
static bool window_has_focus = true; // 窗口焦点状态
static bool need_recreate_surface = false; // 是否需要重新创建 surface

static VkContext vk_context = {};
static uint64_t last_frame_time = 0;
static uint32_t frame_index = 0;

static std::vector<VkFence> fences = {}; // each in-flight frame has a fence
static std::vector<VkCommandBuffer> command_buffers = {}; // each in-flight frame has one command buffer
static std::vector<VkDescriptorPool> descriptor_pools = {}; // each in-flight frame has one descriptor pool
static std::vector<VkDescriptorSet> descriptor_sets = {}; // each in-flight frame has one descriptor set
static std::vector<VkSemaphore> image_acquired_semaphores = {}; // each swapchain image has one image acquired semaphore
static std::vector<VkSemaphore> render_complete_semaphores = {}; // each swapchain image has one render complete semaphore
static SemaphorePool semaphore_pool = {}; // currently used for image acquired semaphores and render complete semaphores of each swapchain image, each in-flight frame has one semaphore pool

// 离屏渲染资源（每个 in-flight 帧一份）
static std::vector<VkImage> depth_images;
static std::vector<VkImage> color_images;
static std::vector<VkDeviceMemory> depth_image_memories;
static std::vector<VkDeviceMemory> color_image_memories;
static std::vector<VkImageView> depth_image_views;
static std::vector<VkImageView> color_image_views;
static std::vector<VkFramebuffer> framebuffers;

static std::vector<FrameState> frame_states = {}; // each in-flight frame has one frame state
static Camera camera = {};
static glm::vec3 camera_orbit_target = glm::vec3(0.0f, 0.0f, 0.0f);
static float camera_orbit_radius = 5.0f;
static bool is_dragging = false;
static glm::vec2 prev_mouse_pos = glm::vec2(0.0f);
static glm::vec2 mouse_pos = glm::vec2(0.0f);

static CameraData camera_data[2] = {}; // [0] = scene camera, [1] = ui camera
static std::vector<VkBuffer> camera_buffers = {}; // each in-flight frame has one camera buffer
static std::vector<VkDeviceMemory> camera_buffer_memories = {}; // each in-flight frame has one camera buffer memory
static std::vector<Entity> entities = {};

static glm::mat4 compute_model_matrix(const Transform &transform) {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
    glm::mat4 rotation = glm::mat4_cast(transform.orientation);
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), transform.scale);
    return translation * rotation * scale;
}

static void create_descriptor_pools(VkContext *context) {
    descriptor_pools.resize(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolSize descriptor_pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, // camera buffer array (2 cameras)
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
    descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_create_info.maxSets = 1;
    descriptor_pool_create_info.poolSizeCount = std::size(descriptor_pool_sizes);
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkResult result = vkCreateDescriptorPool(context->device, &descriptor_pool_create_info, nullptr, &descriptor_pools[i]);
        assert(result == VK_SUCCESS);
    }
}

static void create_framebuffers(AppState *app_state) {
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
}

static void destroy_framebuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
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
}

static void app_resize(AppState *app_state) {
    vkDeviceWaitIdle(vk_context.device);
    for (FrameState &frame_state : frame_states) {
        for (uint32_t geometry_handle : frame_state.geometry_handles) {
            decrement_ref_geometry(&geometry_registry, &task_system, &vk_context, geometry_handle);
        }
        frame_state.geometry_handles.clear();
    }
    frame_states.clear();
    destroy_framebuffers();
    vkDestroySwapchainKHR(vk_context.device, vk_context.swapchain, nullptr);
    SDL_Vulkan_DestroySurface(vk_context.instance, vk_context.surface, nullptr);
    create_vulkan_surface(&vk_context, window);
    create_swapchain(&vk_context, app_state->width, app_state->height);
    create_framebuffers(app_state);
    frame_states.resize(MAX_FRAMES_IN_FLIGHT);
}

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **pp_app_state, int argc, char *argv[])
{
    start_task_system(&task_system);

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
    choose_depth_format(&vk_context);
    create_command_pool(&vk_context);
    create_render_pass(&vk_context);
    create_descriptor_set_layout(&vk_context);
    create_pipeline_layout(&vk_context, sizeof(PushConstants));
    create_pipelines(&vk_context);
    create_descriptor_pools(&vk_context);
    descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        allocate_descriptor_set(&vk_context, descriptor_pools[i], &descriptor_sets[i]);
    }

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

    create_framebuffers(app_state);

    frame_states.resize(MAX_FRAMES_IN_FLIGHT);

    camera_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    camera_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(&vk_context, sizeof(CameraData) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &camera_buffers[i], &camera_buffer_memories[i]);
    }

    {
        GeometryData geometry_data = generate_triangle_geometry_data();
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        Transform transform = {glm::vec3(0.0f, 0.5f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({1, geometry_handle, transform, glm::vec3(1.0f, 0.0f, 0.0f)});
    }
    {
        GeometryData geometry_data = generate_plane_geometry_data(2.0f, 2);
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        Transform transform = {glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({2, geometry_handle, transform, glm::vec3(0.0f, 1.0f, 0.0f)});
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
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        window_has_focus = false;
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        window_has_focus = true;
    } else if (event->type == SDL_EVENT_WINDOW_RESTORED) {
        need_recreate_surface = true;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            // 只记录输入状态，不处理逻辑
            is_dragging = true;
            mouse_pos = glm::vec2(event->button.x, event->button.y);
            prev_mouse_pos = mouse_pos;
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            is_dragging = false;
        }
        // 检测双击：SDL 会自动检测双击，clicks 字段表示点击次数
        if (event->button.clicks == 2) {
            SDL_Log("SDL_AppEvent: 双击检测到！位置: (%.1f, %.1f)", event->button.x, event->button.y);
            // 在这里处理双击逻辑
            // 例如：切换相机模式、聚焦目标等
        }
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        mouse_pos = glm::vec2(event->motion.x, event->motion.y);
    } else {
        SDL_Log("SDL_AppEvent: 0x%x (%u)", event->type, event->type);
    }
    return SDL_APP_CONTINUE;
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *p_app_state)
{
    AppState *app_state = (AppState *) p_app_state;

    if (!window_has_focus) {
        return SDL_APP_CONTINUE;
    }

    if (need_recreate_surface) {
        app_resize(app_state);
        need_recreate_surface = false;
        return SDL_APP_CONTINUE;
    }

    // 计算 delta time（以秒为单位）
    Uint64 current_time = SDL_GetTicksNS();
    Uint64 delta_time_ns = current_time - last_frame_time;
    double delta_time = (double) delta_time_ns / 1e9;  // 转换为秒
    last_frame_time = current_time;

    // SDL_Log("Delta time: %.6f ms (%.2f FPS)", delta_time * 1000, 1.0 / delta_time);

    // cpu logic goes here
    if (is_dragging) {
        glm::vec2 mouse_delta = mouse_pos - prev_mouse_pos;
        const float mouse_offset_threshold = 1;
        if (glm::length(mouse_delta) > mouse_offset_threshold) {
            const float rotation_sensitivity = 0.005f;  // 弧度/像素

            // Yaw：绕世界 Y 轴旋转（水平旋转，左右转头）
            if (float yaw_delta = -mouse_delta.x * rotation_sensitivity; fabsf(yaw_delta) > 0.0001f) {
                glm::quat yaw_quat = glm::angleAxis(yaw_delta, glm::vec3(0.0f, 1.0f, 0.0f));
                camera.orientation = glm::normalize(yaw_quat * camera.orientation);
            }

            // Pitch：绕相机本地 X 轴旋转（垂直旋转，上下抬头）
            glm::mat3 rot_matrix = glm::mat3_cast(camera.orientation);
            glm::vec3 local_x_axis = glm::normalize(rot_matrix[0]); // 右向量（本地 X 轴）

            // 确保轴向量有效
            if (glm::length(local_x_axis) < 0.001f) {
                local_x_axis = glm::vec3(1.0f, 0.0f, 0.0f); // 回退到世界 X 轴
            }

            if (float pitch_delta = -mouse_delta.y * rotation_sensitivity; fabsf(pitch_delta) > 0.0001f) {
                glm::quat pitch_quat = glm::angleAxis(pitch_delta, local_x_axis);
                camera.orientation = glm::normalize(pitch_quat * camera.orientation);
            }
        }
    }

    glm::mat3 rot_matrix = glm::mat3_cast(camera.orientation);
    glm::vec3 forward = -rot_matrix[2];  // Z 轴的反方向（相机朝向）

    // 确保 forward 向量有效
    if (float forward_length = glm::length(forward); forward_length < 0.001f) {
        // 如果 forward 无效，使用默认方向
        forward = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
    } else {
        forward = glm::normalize(forward);
    }

    camera.position = camera_orbit_target - forward * camera_orbit_radius;

    prev_mouse_pos = mouse_pos;

    // ========== GPU 同步阶段 ==========
    // wait for the fence - 等待上一帧 GPU 完成，确保可以安全使用该帧的资源
    VkResult result = vkWaitForFences(vk_context.device, 1, &fences[frame_index], VK_TRUE, UINT64_MAX);
    VK_CHECK(result);
    result = vkResetFences(vk_context.device, 1, &fences[frame_index]);
    VK_CHECK(result);

    // previous frame has been rendered
    // release the referenced geometries
    for (uint32_t geometry_handle : frame_states[frame_index].geometry_handles) {
        decrement_ref_geometry(&geometry_registry, &task_system, &vk_context, geometry_handle);
    }
    frame_states[frame_index].geometry_handles.clear();

    // ========== CPU 逻辑阶段 ==========
    // 在等待 fence 之后、记录命令缓冲区之前执行所有 CPU 逻辑
    // 这样可以最大化 CPU-GPU 并行度，同时确保数据准备完成后再记录命令

    // update scene camera
    int w = 0, h = 0;
    bool success = SDL_GetWindowSizeInPixels(window, &w, &h);
    assert(success);

    glm::mat4 view = compute_view_matrix(camera);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float) w / (float) h, 0.1f, 100.0f);

    // Vulkan clip space has inverted y and half z
    glm::mat4 clip = glm::mat4(
        1.0f,  0.0f, 0.0f, 0.0f, // 1st column of clip matrix
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 0.5f, 0.0f,
        0.0f,  0.0f, 0.5f, 1.0f
    );

    camera_data[0].view = view;
    camera_data[0].projection = clip * projection;

    void *p_data = nullptr;
    vkMapMemory(vk_context.device, camera_buffer_memories[frame_index], 0, sizeof(CameraData) * 2, 0, &p_data);
    memcpy(p_data, camera_data, sizeof(CameraData) * 2);
    vkUnmapMemory(vk_context.device, camera_buffer_memories[frame_index]);

    VkDescriptorBufferInfo buffer_infos[2] = {};
    buffer_infos[0].buffer = camera_buffers[frame_index];
    buffer_infos[0].offset = 0;
    buffer_infos[0].range = sizeof(CameraData);
    buffer_infos[1].buffer = camera_buffers[frame_index];
    buffer_infos[1].offset = sizeof(CameraData);
    buffer_infos[1].range = sizeof(CameraData);

    VkWriteDescriptorSet write_descriptor_set = {};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstSet = descriptor_sets[frame_index];
    write_descriptor_set.dstBinding = 0;
    write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write_descriptor_set.descriptorCount = 2;
    write_descriptor_set.pBufferInfo = buffer_infos;

    vkUpdateDescriptorSets(vk_context.device, 1, &write_descriptor_set, 0, nullptr);

    // Update game logic, physics, animations, etc.
    // Process input events
    // Update scene graph
    // Prepare render data (uniforms, descriptors, etc.)

    // collect renderables
    std::vector<Renderable> renderables = {};
    for (const Entity &entity : entities) {
        if (!is_geometry_uploaded(&geometry_registry, entity.geometry_handle)) { continue; } // skip if this geometry is not uploaded yet
        frame_states[frame_index].geometry_handles.insert(entity.geometry_handle);
        increment_ref_geometry(&geometry_registry, entity.geometry_handle);
        renderables.push_back({entity.entity_id, entity.geometry_handle, compute_model_matrix(entity.transform), entity.color});
    }

    // ========== GPU 资源获取阶段 ==========
    // acquire the next image
    uint32_t image_index;
    VkSemaphore image_acquired_semaphore = semaphore_pool.acquire_semaphore(&vk_context);
    result = vkAcquireNextImageKHR(vk_context.device, vk_context.swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &image_index);
    VK_CHECK(result);
    if (image_acquired_semaphores[image_index] != VK_NULL_HANDLE) { semaphore_pool.return_semaphore(image_acquired_semaphores[image_index]); }
    image_acquired_semaphores[image_index] = image_acquired_semaphore;

    // record the command buffer
    VkCommandBuffer command_buffer = command_buffers[frame_index];
    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    VK_CHECK(result);

    VkClearValue clear_values[2] = {};
    // clear_values[0].color = {.float32 = {0.5f, 0.8f, 1.0f, 1.0f}}; // 轻松活泼的天空蓝色 (RGB: 128, 204, 255)
    // clear_values[0].color = {.float32 = {0.5f, 1.0f, 0.8f}}; // 薄荷绿
    clear_values[0].color = {.float32 = {0.98f, 0.92f, 0.95f, 1.0f}}; // 樱花粉 (RGB: 250, 235, 242)
    clear_values[1].depthStencil = {.depth = 1.0f, .stencil = 0};
    begin_render_pass(&vk_context, command_buffer, vk_context.render_pass, framebuffers[frame_index], app_state->width, app_state->height, 2, clear_values);

    for (const Renderable &renderable : renderables) {
        PipelineKey pipeline_key = {};
        pipeline_key.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipeline_key.polygon_mode = VK_POLYGON_MODE_FILL;
        pipeline_key.depth_test_enabled = true;
        pipeline_key.depth_write_enabled = true;
        pipeline_key.shaders_hash = hash_strings("triangle", "triangle");
        VkPipeline pipeline = get_pipeline(&vk_context, pipeline_key);
        Geometry geometry = geometry_registry.entries[renderable.geometry_handle].geometry;

        // 1. Pipeline 状态（最稳定，必须先绑定，后续命令都依赖它）
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // 2. 描述符集（依赖 pipeline layout，必须在 pipeline 之后）
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_context.pipeline_layout, 0, 1, &descriptor_sets[frame_index], 0, nullptr);

        // 3. 动态状态（可以在 pipeline 绑定后设置，按使用频率和逻辑分组）
        set_viewport(command_buffer, 0, 0, app_state->width, app_state->height);
        set_scissor(command_buffer, 0, 0, app_state->width, app_state->height);
        vk_context.vkCmdSetCullMode(command_buffer, VK_CULL_MODE_NONE);

        // 4. 资源绑定（顶点和索引缓冲区，绘制数据）
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &geometry.vertex_buffer, offsets);
        if (geometry.index_count > 0) {
            vkCmdBindIndexBuffer(command_buffer, geometry.index_buffer, 0, geometry.index_type);
        }

        // 5. Push Constants（最后设置，因为可能频繁变化，放在绘制前）
        PushConstants push_constants = {};
        push_constants.model = renderable.model;
        push_constants.color = renderable.color;
        push_constants.camera_index = 0;
        vkCmdPushConstants(command_buffer, vk_context.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);

        // 6. 绘制调用（最后执行）
        if (geometry.index_count > 0) {
            vkCmdDrawIndexed(command_buffer, geometry.index_count, 1, 0, 0, 0);
        } else {
            vkCmdDraw(command_buffer, geometry.vertex_count, 1, 0, 0);
        }
    }

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
    VK_CHECK(result);
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
    VK_CHECK(result);

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
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyBuffer(vk_context.device, camera_buffers[i], nullptr);
        vkFreeMemory(vk_context.device, camera_buffer_memories[i], nullptr);
    }
    camera_buffers.clear();
    camera_buffer_memories.clear();
    for (FrameState &frame_state : frame_states) {
        for (uint32_t geometry_handle : frame_state.geometry_handles) {
            decrement_ref_geometry(&geometry_registry, &task_system, &vk_context, geometry_handle);
        }
        frame_state.geometry_handles.clear();
    }
    frame_states.clear();
    for (const Entity &entity : entities) {
        decrement_ref_geometry(&geometry_registry, &task_system, &vk_context, entity.geometry_handle);
    }
    entities.clear();
    destroy_framebuffers();
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
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkFreeDescriptorSets(vk_context.device, descriptor_pools[i], 1, &descriptor_sets[i]);
    }
    descriptor_sets.clear();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyDescriptorPool(vk_context.device, descriptor_pools[i], nullptr);
    }
    descriptor_pools.clear();
    cleanup_vulkan(&vk_context);
    SDL_DestroyWindow(window);
    stop_task_system(&task_system);
    SDL_Quit();
    SDL_Log("bye");
}


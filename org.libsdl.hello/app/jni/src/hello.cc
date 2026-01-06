#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include "camera.h"
#include "files.h"
#include "geometries.h"
#include "tasks.h"
#include "vk.h"
#include <glm/gtc/quaternion.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#define MAX_FRAMES_IN_FLIGHT 2

enum PickingState {
    PICKING_STATE_NONE = 0,
    PICKING_STATE_REQUESTED,
    PICKING_STATE_SUBMITTED,
};

struct FrameState {
    std::unordered_set<uint32_t> geometry_handles; // keep track of which geometries are rendered in this frame
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

struct PickingResult {
    uint32_t entity_id;
    uint32_t min_depth_bits; // 用于跟踪最小深度值（以整数形式存储，便于 atomicMin）
};

struct AppState {
    uint32_t width; // 窗口宽度（swap chain 尺寸）
    uint32_t height; // 窗口高度（swap chain 尺寸）
    uint32_t render_width; // 渲染宽度（窗口的 scale_factor 倍）
    uint32_t render_height; // 渲染高度（窗口的 scale_factor 倍）
};

static TaskSystem task_system = {};
static GeometryRegistry geometry_registry = {};
static SDL_Window *window = NULL;
static float scale_factor = 1.0f;
static bool window_has_focus = true; // 窗口焦点状态
static bool need_recreate_surface = false; // 是否需要重新创建 surface

static VkContext vk_context = {};
static uint64_t last_frame_time = 0;
static uint32_t frame_index = 0;

static std::vector<VkFence> fences = {}; // each in-flight frame has a fence
static std::vector<VkCommandBuffer> command_buffers = {}; // each in-flight frame has one command buffer
static std::vector<VkDescriptorPool> descriptor_pools = {}; // each in-flight frame has one descriptor pool
static std::vector<VkDescriptorSet> descriptor_sets = {}; // each in-flight frame has one descriptor set
static std::vector<VkSemaphore> image_acquired_semaphores = {}; // each in-flight frame has one image acquired semaphore
static std::vector<VkSemaphore> render_complete_semaphores = {}; // each swapchain image has one render complete semaphore

// 离屏渲染资源（每个 in-flight 帧一份）
static std::vector<VkImage> depth_images;
static std::vector<VkImage> color_images;
static std::vector<VkDeviceMemory> depth_image_memories;
static std::vector<VkDeviceMemory> color_image_memories;
static std::vector<VkImageView> depth_image_views;
static std::vector<VkImageView> color_image_views;

static std::vector<VkBuffer> picking_storage_buffers = {}; // each in-flight frame has one picking storage buffer
static std::vector<VkDeviceMemory> picking_storage_buffer_memories = {}; // each in-flight frame has one picking storage buffer memory

static std::vector<FrameState> frame_states = {}; // each in-flight frame has one frame state
static std::vector<PickingState> picking_states = {}; // each in-flight frame has one picking state

static Camera camera = {};
static glm::vec3 camera_orbit_target = glm::vec3(0.0f, 0.0f, 0.0f);
static float camera_orbit_radius = 8.0f;
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
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, // camera
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // picking storage buffer
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
    char name[256];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_image(&vk_context, vk_context.surface_format, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &color_images[i], &color_image_memories[i]);
        snprintf(name, sizeof(name), "ColorImageView[%u]", i);
        create_image_view(&vk_context, color_images[i], vk_context.surface_format, VK_IMAGE_ASPECT_COLOR_BIT, &color_image_views[i], name);

        create_image(&vk_context, vk_context.depth_image_format, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &depth_images[i], &depth_image_memories[i]);
        snprintf(name, sizeof(name), "DepthImageView[%u]", i);
        create_image_view(&vk_context, depth_images[i], vk_context.depth_image_format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &depth_image_views[i], name);
    }
}

static void destroy_framebuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(vk_context.device, color_image_views[i], nullptr);
        vkDestroyImageView(vk_context.device, depth_image_views[i], nullptr);
        vkDestroyImage(vk_context.device, color_images[i], nullptr);
        vkDestroyImage(vk_context.device, depth_images[i], nullptr);
        vkFreeMemory(vk_context.device, color_image_memories[i], nullptr);
        vkFreeMemory(vk_context.device, depth_image_memories[i], nullptr);
    }
    color_image_views.clear();
    depth_image_views.clear();
    color_images.clear();
    depth_images.clear();
    color_image_memories.clear();
    depth_image_memories.clear();
}

static void app_resize(AppState *app_state) {
    vkDeviceWaitIdle(vk_context.device);

    // 获取当前窗口大小
    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    app_state->width = (uint32_t) width;
    app_state->height = (uint32_t) height;

    // 更新渲染尺寸（窗口的 scale_factor 倍）
    app_state->render_width = (uint32_t) (app_state->width * scale_factor);
    app_state->render_height = (uint32_t) (app_state->height * scale_factor);

    picking_states.clear();
    for (FrameState &frame_state : frame_states) {
        for (uint32_t geometry_handle : frame_state.geometry_handles) {
            decrement_geometry_ref(&geometry_registry, &task_system, &vk_context, geometry_handle);
        }
        frame_state.geometry_handles.clear();
    }
    frame_states.clear();
    destroy_framebuffers();
    destroy_swap_chain(&vk_context);
    SDL_Vulkan_DestroySurface(vk_context.instance, vk_context.surface, nullptr);
    create_vulkan_surface(&vk_context, window);
    create_swap_chain(&vk_context, app_state->width, app_state->height);
    create_framebuffers(app_state);
    frame_states.resize(MAX_FRAMES_IN_FLIGHT);
    picking_states.resize(MAX_FRAMES_IN_FLIGHT, PICKING_STATE_NONE);
    frame_index = 0; // reset frame index
}

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **pp_app_state, int argc, char *argv[])
{
    start_task_system(&task_system);

    bool succeed = SDL_Init(SDL_INIT_VIDEO);
    assert(succeed);

    AppState *app_state = new AppState;

    SDL_DisplayID primary_display = SDL_GetPrimaryDisplay();
    assert(primary_display != 0);
    SDL_Rect usable_bounds;
    succeed = SDL_GetDisplayUsableBounds(primary_display, &usable_bounds);
    assert(succeed);
    app_state->width = (uint32_t) usable_bounds.w;
    app_state->height = (uint32_t) usable_bounds.h;

    // 渲染尺寸为屏幕尺寸的 scale_factor 倍
    app_state->render_width = (uint32_t) (app_state->width * scale_factor);
    app_state->render_height = (uint32_t) (app_state->height * scale_factor);

    *pp_app_state = app_state;

    window = SDL_CreateWindow("gfx demo", app_state->width, app_state->height, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    assert(window);

    create_vulkan_instance(&vk_context);
    create_vulkan_surface(&vk_context, window);
    choose_physical_device(&vk_context);
    create_device(&vk_context);
    create_swap_chain(&vk_context, app_state->width, app_state->height);
    choose_depth_format(&vk_context);
    create_command_pool(&vk_context);
    create_descriptor_set_layout(&vk_context);
    create_pipeline_layout(&vk_context, sizeof(PushConstants));
    create_pipelines(&vk_context);
    create_descriptor_pools(&vk_context);
    descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        allocate_descriptor_set(&vk_context, descriptor_pools[i], &descriptor_sets[i]);
    }

    image_acquired_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_semaphore(&vk_context, &image_acquired_semaphores[i]);
    }
    render_complete_semaphores.resize(vk_context.swap_chain_images.size());
    for (uint32_t i = 0; i < vk_context.swap_chain_images.size(); ++i) {
        create_semaphore(&vk_context, &render_complete_semaphores[i]);
    }

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
    picking_storage_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    picking_storage_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(&vk_context, sizeof(PickingResult), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &picking_storage_buffers[i], &picking_storage_buffer_memories[i]);
    }

    frame_states.resize(MAX_FRAMES_IN_FLIGHT);
    picking_states.resize(MAX_FRAMES_IN_FLIGHT, PICKING_STATE_NONE);

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
        mouse_pos = glm::vec2(event->button.x, event->button.y);
        if (event->button.clicks == 2) {
            SDL_Log("SDL_AppEvent: 双击检测到！位置: (%.1f, %.1f)", event->button.x, event->button.y);
            picking_states[frame_index] = PICKING_STATE_REQUESTED;
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
            const float rotation_sensitivity = 0.0025f;  // 弧度/像素

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
    if (picking_states[frame_index] == PICKING_STATE_SUBMITTED) {
        void *p_data = nullptr;
        vkMapMemory(vk_context.device, picking_storage_buffer_memories[frame_index], 0, sizeof(PickingResult), 0, &p_data);
        PickingResult *picking_result = (PickingResult *) p_data;
        float min_depth = (float) picking_result->min_depth_bits / 16777215.0f; // 转换回 [0, 1] 范围
        vkUnmapMemory(vk_context.device, picking_storage_buffer_memories[frame_index]);
        SDL_Log("Picking result: entity_id=%u, min_depth=%f (bits=0x%08X)", picking_result->entity_id, min_depth, picking_result->min_depth_bits);
        picking_states[frame_index] = PICKING_STATE_NONE;
    }
    // release the referenced geometries
    for (uint32_t geometry_handle : frame_states[frame_index].geometry_handles) {
        decrement_geometry_ref(&geometry_registry, &task_system, &vk_context, geometry_handle);
    }
    frame_states[frame_index].geometry_handles.clear();

    // ========== CPU 逻辑阶段 ==========
    // 在等待 fence 之后、记录命令缓冲区之前执行所有 CPU 逻辑
    // 这样可以最大化 CPU-GPU 并行度，同时确保数据准备完成后再记录命令

    // update scene camera
    glm::mat4 view = compute_view_matrix(camera);
    // 使用渲染尺寸的宽高比（保持比例）
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float) app_state->render_width / (float) app_state->render_height, 0.1f, 100.0f);

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

    {
        // 初始化 picking storage buffer
        void *p_data = nullptr;
        vkMapMemory(vk_context.device, picking_storage_buffer_memories[frame_index], 0, sizeof(PickingResult), 0, &p_data);
        PickingResult *picking_result = (PickingResult *) p_data;
        picking_result->entity_id = 0;
        picking_result->min_depth_bits = 0xFFFFFFFFu; // 最大深度值，表示最远
        vkUnmapMemory(vk_context.device, picking_storage_buffer_memories[frame_index]);
    }

    // update descriptor set
    std::vector<VkWriteDescriptorSet> write_descriptor_sets = {};

    {
        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = camera_buffers[frame_index];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(CameraData) * 2;

        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 0;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pBufferInfo = &buffer_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }

    if (picking_states[frame_index] == PICKING_STATE_REQUESTED) {
        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = picking_storage_buffers[frame_index];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(PickingResult);

        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 1;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pBufferInfo = &buffer_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }

    build_top_level_acceleration_structure();

    vkUpdateDescriptorSets(vk_context.device, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);

    // Update game logic, physics, animations, etc.
    // Process input events
    // Update scene graph
    // Prepare render data (uniforms, descriptors, etc.)

    // collect renderables
    std::vector<Renderable> renderables = {};
    for (const Entity &entity : entities) {
        if (!is_geometry_uploaded(&geometry_registry, entity.geometry_handle)) { continue; } // skip if this geometry is not uploaded yet
        frame_states[frame_index].geometry_handles.insert(entity.geometry_handle);
        increment_geometry_ref(&geometry_registry, entity.geometry_handle);
        renderables.push_back({entity.entity_id, entity.geometry_handle, compute_model_matrix(entity.transform), entity.color});
    }

    // ========== GPU 资源获取阶段 ==========
    // acquire the next image
    uint32_t image_index;
    result = vkAcquireNextImageKHR(vk_context.device, vk_context.swap_chain, UINT64_MAX, image_acquired_semaphores[frame_index], VK_NULL_HANDLE, &image_index);
    VK_CHECK(result);

    // record the command buffer
    VkCommandBuffer command_buffer = command_buffers[frame_index];
    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    VK_CHECK(result);

    // 转换 color image layout 为 COLOR_ATTACHMENT_OPTIMAL
    // 使用 BOTTOM_OF_PIPE 作为 src stage，表示之前所有操作（包括上一帧的 TRANSFER）都已完成，适合复用的资源（即使当前帧是第一次使用，也表示“之前没有操作”），更符合 in-flight 帧的语义
    record_pipeline_image_barrier(command_buffer, color_images[frame_index],
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  0,
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // 转换 depth image layout 为 DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    // 使用 BOTTOM_OF_PIPE 作为 src stage，表示之前所有操作（包括上一帧的深度测试）都已完成，适合复用的资源（即使当前帧是第一次使用，也表示“之前没有操作”），更符合 in-flight 帧的语义
    record_pipeline_image_barrier(command_buffer, depth_images[frame_index],
                                  VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                  0,
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // VkClearColorValue clear_color_value = {.float32 = {0.5f, 0.8f, 1.0f, 1.0f}}; // 轻松活泼的天空蓝色 (RGB: 128, 204, 255)
    // VkClearColorValue clear_color_value = {.float32 = {0.5f, 1.0f, 0.8f}}; // 薄荷绿
    VkClearColorValue clear_color_value = {.float32 = {0.98f, 0.92f, 0.95f, 1.0f}}; // 樱花粉 (RGB: 250, 235, 242)
    VkClearDepthStencilValue clear_depth_stencil_value = {.depth = 1.0f, .stencil = 0};
    // 使用渲染尺寸
    begin_rendering(&vk_context, command_buffer, color_image_views[frame_index], &clear_color_value, depth_image_views[frame_index], &clear_depth_stencil_value, app_state->render_width, app_state->render_height);

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
        set_viewport(command_buffer, 0, 0, app_state->render_width, app_state->render_height);
        set_scissor(command_buffer, 0, 0, app_state->render_width, app_state->render_height);
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
        push_constants.entity_id = renderable.entity_id;
        vkCmdPushConstants(command_buffer, vk_context.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);

        // 6. 绘制调用（最后执行）
        if (geometry.index_count > 0) {
            vkCmdDrawIndexed(command_buffer, geometry.index_count, 1, 0, 0, 0);
        } else {
            vkCmdDraw(command_buffer, geometry.vertex_count, 1, 0, 0);
        }
    }

    end_rendering(&vk_context, command_buffer);

    if (picking_states[frame_index] == PICKING_STATE_REQUESTED) {
        record_pipeline_image_barrier(command_buffer, depth_images[frame_index],
                                      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        begin_rendering(&vk_context, command_buffer, VK_NULL_HANDLE, nullptr, depth_image_views[frame_index], nullptr, app_state->render_width, app_state->render_height);

        for (const Renderable &renderable : renderables) {
            PipelineKey pipeline_key = {};
            pipeline_key.primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipeline_key.polygon_mode = VK_POLYGON_MODE_FILL;
            pipeline_key.depth_test_enabled = true;
            pipeline_key.depth_write_enabled = false;
            pipeline_key.shaders_hash = hash_strings("picking", "picking");
            VkPipeline pipeline = get_pipeline(&vk_context, pipeline_key);
            Geometry geometry = geometry_registry.entries[renderable.geometry_handle].geometry;

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_context.pipeline_layout, 0, 1, &descriptor_sets[frame_index], 0, nullptr);

            set_viewport(command_buffer, 0, 0, app_state->render_width, app_state->render_height);
            // Picking 的 scissor 需要根据渲染尺寸缩放鼠标坐标
            float scale_x = (float) app_state->render_width / (float) app_state->width;
            float scale_y = (float) app_state->render_height / (float) app_state->height;
            set_scissor(command_buffer, (uint32_t) (mouse_pos.x * scale_x), (uint32_t) (mouse_pos.y * scale_y), 1, 1);
            vk_context.vkCmdSetCullMode(command_buffer, VK_CULL_MODE_NONE);

            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &geometry.vertex_buffer, offsets);
            if (geometry.index_count > 0) {
                vkCmdBindIndexBuffer(command_buffer, geometry.index_buffer, 0, geometry.index_type);
            }

            PushConstants push_constants = {};
            push_constants.model = renderable.model;
            push_constants.color = renderable.color;
            push_constants.camera_index = 0;
            push_constants.entity_id = renderable.entity_id;
            vkCmdPushConstants(command_buffer, vk_context.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);

            if (geometry.index_count > 0) {
                vkCmdDrawIndexed(command_buffer, geometry.index_count, 1, 0, 0, 0);
            } else {
                vkCmdDraw(command_buffer, geometry.vertex_count, 1, 0, 0);
            }
        }

        end_rendering(&vk_context, command_buffer);
        picking_states[frame_index] = PICKING_STATE_SUBMITTED;
    }

    {
        // 转换 color image layout 为 TRANSFER_SRC_OPTIMAL
        record_pipeline_image_barrier(command_buffer, color_images[frame_index],
                                      VK_IMAGE_ASPECT_COLOR_BIT,
                                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      VK_ACCESS_TRANSFER_READ_BIT,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // 转换 swap chain image layout 为 TRANSFER_DST_OPTIMAL
        record_pipeline_image_barrier(command_buffer, vk_context.swap_chain_images[image_index],
                                      VK_IMAGE_ASPECT_COLOR_BIT,
                                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      0,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      VK_IMAGE_LAYOUT_UNDEFINED, // acquire 后通常是 UNDEFINED
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Blit 时从渲染尺寸缩放到屏幕尺寸，保持宽高比并居中
        blit_image(command_buffer, color_images[frame_index], vk_context.swap_chain_images[image_index], app_state->render_width, app_state->render_height, app_state->width, app_state->height);

        // 转换 swap chain image layout 为 PRESENT_SRC
        record_pipeline_image_barrier(command_buffer, vk_context.swap_chain_images[image_index],
                                      VK_IMAGE_ASPECT_COLOR_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      0, // present 操作不需要特定的 access mask
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    vkEndCommandBuffer(command_buffer);

    // submit the command buffer
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphores[frame_index];
    // vkAcquireNextImageKHR 的 semaphore 在图像可用时被 signal，通常发生在 COLOR_ATTACHMENT_OUTPUT_BIT 阶段
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[frame_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphores[image_index];
    result = vkQueueSubmit(vk_context.queue, 1, &submit_info, fences[frame_index]);
    VK_CHECK(result);

    // present the image
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete_semaphores[image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &vk_context.swap_chain;
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
    picking_states.clear();
    for (FrameState &frame_state : frame_states) {
        for (uint32_t geometry_handle : frame_state.geometry_handles) {
            decrement_geometry_ref(&geometry_registry, &task_system, &vk_context, geometry_handle);
        }
        frame_state.geometry_handles.clear();
    }
    frame_states.clear();
    for (const Entity &entity : entities) {
        decrement_geometry_ref(&geometry_registry, &task_system, &vk_context, entity.geometry_handle);
    }
    entities.clear();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyBuffer(vk_context.device, picking_storage_buffers[i], nullptr);
        vkFreeMemory(vk_context.device, picking_storage_buffer_memories[i], nullptr);
    }
    picking_storage_buffers.clear();
    picking_storage_buffer_memories.clear();
    destroy_framebuffers();
    vkFreeCommandBuffers(vk_context.device, vk_context.command_pool, MAX_FRAMES_IN_FLIGHT, command_buffers.data());
    command_buffers.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyFence(vk_context.device, fences[i], nullptr);
    }
    fences.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(vk_context.device, image_acquired_semaphores[i], nullptr);
    }
    image_acquired_semaphores.clear();
    for (size_t i = 0; i < vk_context.swap_chain_images.size(); ++i) {
        vkDestroySemaphore(vk_context.device, render_complete_semaphores[i], nullptr);
    }
    render_complete_semaphores.clear();
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


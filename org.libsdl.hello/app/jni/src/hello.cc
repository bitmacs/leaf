#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include "camera.h"
#include "files.h"
#include "geometries.h"
#include "tasks.h"
#include "vk.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <unordered_set>

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_TOP_LEVEL_ACCELERATION_STRUCTURE_INSTANCE_COUNT 1024

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

struct DirectionalLight {
    glm::vec3 direction; // 光线方向（归一化）
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
    uint32_t render_width; // 渲染宽度
    uint32_t render_height; // 渲染高度
};

static TaskSystem task_system = {};
static GeometryRegistry geometry_registry = {};
static SDL_Window *window = NULL;
static bool window_has_focus = true; // 窗口焦点状态
static bool need_recreate_surface = false; // 是否需要重新创建 surface

static VkContext vk_context = {};
static uint64_t last_frame_time = 0;
static uint32_t frame_index = 0;
static uint32_t frame_count = 0; // 全局帧计数

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

// 路径追踪输出图像（每个 in-flight 帧一份）
static std::vector<VkImage> path_tracing_images = {};
static std::vector<VkDeviceMemory> path_tracing_image_memories = {};
static std::vector<VkImageView> path_tracing_image_views = {};

static std::vector<VkImage> gbuffer_position_images = {};
static std::vector<VkImage> gbuffer_normal_images = {};
static std::vector<VkImage> gbuffer_albedo_images = {};
static std::vector<VkImage> gbuffer_depth_images = {};
static std::vector<VkDeviceMemory> gbuffer_position_memories = {};
static std::vector<VkDeviceMemory> gbuffer_normal_memories = {};
static std::vector<VkDeviceMemory> gbuffer_albedo_memories = {};
static std::vector<VkDeviceMemory> gbuffer_depth_memories = {};
static std::vector<VkImageView> gbuffer_position_views = {};
static std::vector<VkImageView> gbuffer_normal_views = {};
static std::vector<VkImageView> gbuffer_albedo_views = {};
static std::vector<VkImageView> gbuffer_depth_views = {};
// 直接光/间接光分离（rgba32f，与 G-buffer 同分辨率）
static std::vector<VkImage> direct_radiance_images = {};
static std::vector<VkDeviceMemory> direct_radiance_image_memories = {};
static std::vector<VkImageView> direct_radiance_image_views = {};
static std::vector<VkImage> indirect_radiance_images = {};
static std::vector<VkDeviceMemory> indirect_radiance_image_memories = {};
static std::vector<VkImageView> indirect_radiance_image_views = {};

static std::vector<FrameState> frame_states = {}; // each in-flight frame has one frame state
static std::vector<PickingState> picking_states = {}; // each in-flight frame has one picking state

static Camera camera = {};
static glm::vec3 camera_orbit_target = glm::vec3(0.0f, 0.0f, 0.0f);
static float camera_orbit_radius = 6.5f;
static bool is_dragging = false;
static glm::vec2 prev_mouse_pos = glm::vec2(0.0f);
static glm::vec2 mouse_pos = glm::vec2(0.0f);
static double total_time = 0.0;  // 累计总时间，用于动画

static CameraData camera_data[2] = {}; // [0] = scene camera, [1] = ui camera
static std::vector<VkBuffer> camera_buffers = {}; // each in-flight frame has one camera buffer
static std::vector<VkDeviceMemory> camera_buffer_memories = {}; // each in-flight frame has one camera buffer memory

static DirectionalLight directional_light = {
    .direction = glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f)),
};
static std::vector<VkBuffer> light_buffers = {}; // each in-flight frame has one light buffer
static std::vector<VkDeviceMemory> light_buffer_memories = {}; // each in-flight frame has one light buffer memory
static std::vector<VkAccelerationStructureKHR> tlas = {}; // each in-flight frame has one tlas
static std::vector<VkBuffer> tlas_buffers = {}; // each in-flight frame has one tlas buffer
static std::vector<VkDeviceMemory> tlas_buffer_memories = {}; // each in-flight frame has one tlas buffer memory
static std::vector<VkBuffer> scratch_buffers = {}; // each in-flight frame has one scratch buffer
static std::vector<VkDeviceMemory> scratch_buffer_memories = {}; // each in-flight frame has one scratch buffer memory
static std::vector<VkBuffer> instance_buffers = {}; // each in-flight frame has one instance buffer
static std::vector<VkDeviceMemory> instance_buffer_memories = {}; // each in-flight frame has one instance buffer memory

static std::vector<Entity> entities = {};

static glm::mat4 compute_model_matrix(const Transform &transform) {
    glm::mat4 identity = glm::mat4(1.0f);
    glm::mat4 translation = glm::translate(identity, transform.position);
    glm::mat4 rotation = glm::mat4_cast(transform.orientation);
    glm::mat4 scale = glm::scale(identity, transform.scale);
    return translation * rotation * scale;
}

static void create_descriptor_pools(VkContext *context) {
    descriptor_pools.resize(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolSize descriptor_pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, // camera + light (2 uniform buffers)
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, // picking storage buffer
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, // acceleration structure
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7}, // output + 4 gbuffer + direct radiance + indirect radiance
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
        destroy_image(&vk_context, color_images[i], color_image_memories[i]);
        destroy_image(&vk_context, depth_images[i], depth_image_memories[i]);
    }
    color_image_views.clear();
    depth_image_views.clear();
    color_images.clear();
    depth_images.clear();
    color_image_memories.clear();
    depth_image_memories.clear();
}

static void create_path_tracing_images(VkContext *context, AppState *app_state) {
    path_tracing_images.resize(MAX_FRAMES_IN_FLIGHT);
    path_tracing_image_memories.resize(MAX_FRAMES_IN_FLIGHT);
    path_tracing_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    char name[100];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_image(&vk_context, VK_FORMAT_R8G8B8A8_UNORM, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &path_tracing_images[i], &path_tracing_image_memories[i]);
        snprintf(name, sizeof(name), "path_tracing_image_%d", i);
        create_image_view(&vk_context, path_tracing_images[i], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &path_tracing_image_views[i], name);
    }
}

static void destroy_path_tracing_images(VkContext *context) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(context->device, path_tracing_image_views[i], nullptr);
        destroy_image(context, path_tracing_images[i], path_tracing_image_memories[i]);
    }
    path_tracing_image_views.clear();
    path_tracing_images.clear();
    path_tracing_image_memories.clear();
}

static void create_direct_indirect_radiance_images(VkContext *context, AppState *app_state) {
    direct_radiance_images.resize(MAX_FRAMES_IN_FLIGHT);
    direct_radiance_image_memories.resize(MAX_FRAMES_IN_FLIGHT);
    direct_radiance_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    indirect_radiance_images.resize(MAX_FRAMES_IN_FLIGHT);
    indirect_radiance_image_memories.resize(MAX_FRAMES_IN_FLIGHT);
    indirect_radiance_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    char name[100];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_image(context, VK_FORMAT_R32G32B32A32_SFLOAT, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &direct_radiance_images[i], &direct_radiance_image_memories[i]);
        create_image(context, VK_FORMAT_R32G32B32A32_SFLOAT, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &indirect_radiance_images[i], &indirect_radiance_image_memories[i]);
        snprintf(name, sizeof(name), "direct_radiance_image_%u", i);
        create_image_view(context, direct_radiance_images[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &direct_radiance_image_views[i], name);
        snprintf(name, sizeof(name), "indirect_radiance_image_%u", i);
        create_image_view(context, indirect_radiance_images[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &indirect_radiance_image_views[i], name);
    }
}

static void destroy_direct_indirect_radiance_images(VkContext *context) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(context->device, direct_radiance_image_views[i], nullptr);
        vkDestroyImageView(context->device, indirect_radiance_image_views[i], nullptr);
        destroy_image(context, direct_radiance_images[i], direct_radiance_image_memories[i]);
        destroy_image(context, indirect_radiance_images[i], indirect_radiance_image_memories[i]);
    }
    direct_radiance_image_views.clear();
    indirect_radiance_image_views.clear();
    direct_radiance_images.clear();
    indirect_radiance_images.clear();
    direct_radiance_image_memories.clear();
    indirect_radiance_image_memories.clear();
}

static void create_gbuffer_images(VkContext *context, AppState *app_state) {
    gbuffer_position_images.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_normal_images.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_albedo_images.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_depth_images.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_position_memories.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_normal_memories.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_albedo_memories.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_depth_memories.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_position_views.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_normal_views.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_albedo_views.resize(MAX_FRAMES_IN_FLIGHT);
    gbuffer_depth_views.resize(MAX_FRAMES_IN_FLIGHT);
    char name[100];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_image(context, VK_FORMAT_R32G32B32A32_SFLOAT, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &gbuffer_position_images[i], &gbuffer_position_memories[i]);
        create_image(context, VK_FORMAT_R32G32B32A32_SFLOAT, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &gbuffer_normal_images[i], &gbuffer_normal_memories[i]);
        create_image(context, VK_FORMAT_R8G8B8A8_UNORM, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &gbuffer_albedo_images[i], &gbuffer_albedo_memories[i]);
        create_image(context, VK_FORMAT_R32_SFLOAT, app_state->render_width, app_state->render_height, VK_IMAGE_USAGE_STORAGE_BIT, &gbuffer_depth_images[i], &gbuffer_depth_memories[i]);
        snprintf(name, sizeof(name), "gbuffer_position_%u", i);
        create_image_view(context, gbuffer_position_images[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &gbuffer_position_views[i], name);
        snprintf(name, sizeof(name), "gbuffer_normal_%u", i);
        create_image_view(context, gbuffer_normal_images[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &gbuffer_normal_views[i], name);
        snprintf(name, sizeof(name), "gbuffer_albedo_%u", i);
        create_image_view(context, gbuffer_albedo_images[i], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &gbuffer_albedo_views[i], name);
        snprintf(name, sizeof(name), "gbuffer_depth_%u", i);
        create_image_view(context, gbuffer_depth_images[i], VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &gbuffer_depth_views[i], name);
    }
}

static void destroy_gbuffer_images(VkContext *context) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(context->device, gbuffer_position_views[i], nullptr);
        vkDestroyImageView(context->device, gbuffer_normal_views[i], nullptr);
        vkDestroyImageView(context->device, gbuffer_albedo_views[i], nullptr);
        vkDestroyImageView(context->device, gbuffer_depth_views[i], nullptr);
        destroy_image(context, gbuffer_position_images[i], gbuffer_position_memories[i]);
        destroy_image(context, gbuffer_normal_images[i], gbuffer_normal_memories[i]);
        destroy_image(context, gbuffer_albedo_images[i], gbuffer_albedo_memories[i]);
        destroy_image(context, gbuffer_depth_images[i], gbuffer_depth_memories[i]);
    }
    gbuffer_position_views.clear();
    gbuffer_normal_views.clear();
    gbuffer_albedo_views.clear();
    gbuffer_depth_views.clear();
    gbuffer_position_images.clear();
    gbuffer_normal_images.clear();
    gbuffer_albedo_images.clear();
    gbuffer_depth_images.clear();
    gbuffer_position_memories.clear();
    gbuffer_normal_memories.clear();
    gbuffer_albedo_memories.clear();
    gbuffer_depth_memories.clear();
}

static void create_top_level_acceleration_structures(VkContext *context, uint32_t max_instance_count) {
    tlas.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    tlas_buffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    tlas_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    scratch_buffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    scratch_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    instance_buffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    instance_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkAccelerationStructureGeometryKHR geometry{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = {
                .instances = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .arrayOfPointers = VK_FALSE,
                    .data = { .deviceAddress = 0 } // 空 TLAS：地址为 0
                }
            },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };
        VkAccelerationStructureBuildGeometryInfoKHR build_geometry_info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries   = &geometry
        };
        VkAccelerationStructureBuildSizesInfoKHR build_sizes_info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
        };
        context->vkGetAccelerationStructureBuildSizesKHR(
            context->device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_geometry_info,
            &max_instance_count,
            &build_sizes_info
        );
        create_buffer(
            context,
            build_sizes_info.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &tlas_buffers[i],
            &tlas_buffer_memories[i]
        );
        VkAccelerationStructureCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = tlas_buffers[i],
            .size   = build_sizes_info.accelerationStructureSize,
            .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
        };
        VkResult result = context->vkCreateAccelerationStructureKHR(context->device, &create_info, nullptr, &tlas[i]);
        assert(result == VK_SUCCESS);
        VkDeviceSize scratch_buffer_size = build_sizes_info.buildScratchSize;
        create_buffer(
            context,
            scratch_buffer_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &scratch_buffers[i],
            &scratch_buffer_memories[i]
        );
        // 创建实例缓冲区（按最大实例数分配）
        VkDeviceSize instance_buffer_size = max_instance_count * sizeof(VkAccelerationStructureInstanceKHR);
        create_buffer(
            context,
            instance_buffer_size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &instance_buffers[i],
            &instance_buffer_memories[i]
        );
    }
}

static VkTransformMatrixKHR to_vk_transform_matrix(const glm::mat4 &m) {
    VkTransformMatrixKHR out{};

    // VkTransformMatrixKHR 是 row-major
    // glm::mat4 是 column-major (m[col][row])

    out.matrix[0][0] = m[0][0];
    out.matrix[0][1] = m[1][0];
    out.matrix[0][2] = m[2][0];
    out.matrix[0][3] = m[3][0];

    out.matrix[1][0] = m[0][1];
    out.matrix[1][1] = m[1][1];
    out.matrix[1][2] = m[2][1];
    out.matrix[1][3] = m[3][1];

    out.matrix[2][0] = m[0][2];
    out.matrix[2][1] = m[1][2];
    out.matrix[2][2] = m[2][2];
    out.matrix[2][3] = m[3][2];

    return out;
}

// TODO: 将函数拆分为两部分：CPU 操作（上传数据）在命令缓冲区记录之前，GPU 操作（构建命令）在命令缓冲区中。
static void build_top_level_acceleration_structure(VkCommandBuffer command_buffer, VkContext *context, const std::vector<Renderable> &renderables) {
    if (renderables.empty()) { return; }

    // 收集实例数据
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(renderables.size());

    for (const Renderable &renderable : renderables) {
        const Geometry &geometry = geometry_registry.entries[renderable.geometry_handle].geometry;

        VkAccelerationStructureInstanceKHR instance = {};

        // Vulkan 需要转置的变换矩阵
        // glm::mat4 transform_transposed = glm::transpose(renderable.model);
        // memcpy(instance.transform.matrix, glm::value_ptr(transform_transposed), sizeof(instance.transform.matrix));
        instance.transform = to_vk_transform_matrix(renderable.model);

        instance.instanceCustomIndex = renderable.entity_id;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = get_acceleration_structure_device_address(context, geometry.blas);

        instances.push_back(instance);
    }

    // 上传实例数据（instance_buffers 已在初始化时按最大大小创建）
    const VkDeviceSize instance_buffer_size = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    void *data;
    vkMapMemory(context->device, instance_buffer_memories[frame_index], 0, instance_buffer_size, 0, &data);
    memcpy(data, instances.data(), instance_buffer_size);
    vkUnmapMemory(context->device, instance_buffer_memories[frame_index]);

    // 准备 TLAS 几何体信息
    VkAccelerationStructureGeometryInstancesDataKHR geometry_instances_data = {};
    geometry_instances_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry_instances_data.arrayOfPointers = VK_FALSE;
    geometry_instances_data.data.deviceAddress = get_buffer_device_address(context, instance_buffers[frame_index]);

    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = geometry_instances_data;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    // 准备构建信息（每帧都使用 BUILD 模式）
    VkAccelerationStructureBuildGeometryInfoKHR build_geometry_info = {};
    build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_geometry_info.geometryCount = 1;
    build_geometry_info.pGeometries = &geometry;

    const uint32_t instance_count = instances.size();

    // 更新构建信息
    build_geometry_info.dstAccelerationStructure = tlas[frame_index];
    build_geometry_info.scratchData.deviceAddress = get_buffer_device_address(context, scratch_buffers[frame_index]);

    // 准备构建范围
    VkAccelerationStructureBuildRangeInfoKHR build_range_info = {};
    build_range_info.primitiveCount = instance_count;
    const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos[] = {&build_range_info};

    // 构建 TLAS（每帧都完全重建）
    context->vkCmdBuildAccelerationStructuresKHR(command_buffer, 1, &build_geometry_info, build_range_infos);
}

static void app_resize(AppState *app_state) {
    vkDeviceWaitIdle(vk_context.device);

    // 获取当前窗口大小
    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    app_state->width = (uint32_t) width;
    app_state->height = (uint32_t) height;

    app_state->render_width = app_state->width;
    app_state->render_height = app_state->height / 2;

    picking_states.clear();
    for (FrameState &frame_state : frame_states) {
        for (uint32_t geometry_handle : frame_state.geometry_handles) {
            decrement_geometry_ref(&geometry_registry, &task_system, &vk_context, geometry_handle);
        }
        frame_state.geometry_handles.clear();
    }
    frame_states.clear();
    destroy_gbuffer_images(&vk_context);
    destroy_direct_indirect_radiance_images(&vk_context);
    destroy_path_tracing_images(&vk_context);
    destroy_framebuffers();
    destroy_swap_chain(&vk_context);
    SDL_Vulkan_DestroySurface(vk_context.instance, vk_context.surface, nullptr);
    create_vulkan_surface(&vk_context, window);
    create_swap_chain(&vk_context, app_state->width, app_state->height);
    create_framebuffers(app_state);
    create_path_tracing_images(&vk_context, app_state);
    create_direct_indirect_radiance_images(&vk_context, app_state);
    create_gbuffer_images(&vk_context, app_state);
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

    app_state->render_width = app_state->width;
    app_state->render_height = app_state->height / 2;

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
    create_compute_pipeline_layout(&vk_context, sizeof(PathTracingPushConstants));
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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_fence(&vk_context, true, &fences[i]);
    }

    command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    allocate_command_buffers(&vk_context, vk_context.command_pool, MAX_FRAMES_IN_FLIGHT, command_buffers.data());

    create_framebuffers(app_state);
    create_path_tracing_images(&vk_context, app_state);
    create_direct_indirect_radiance_images(&vk_context, app_state);
    create_gbuffer_images(&vk_context, app_state);
    picking_storage_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    picking_storage_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(&vk_context, sizeof(PickingResult), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &picking_storage_buffers[i], &picking_storage_buffer_memories[i]);
    }
    create_top_level_acceleration_structures(&vk_context, MAX_TOP_LEVEL_ACCELERATION_STRUCTURE_INSTANCE_COUNT);

    frame_states.resize(MAX_FRAMES_IN_FLIGHT);
    picking_states.resize(MAX_FRAMES_IN_FLIGHT, PICKING_STATE_NONE);

    camera_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    camera_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(&vk_context, sizeof(CameraData) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &camera_buffers[i], &camera_buffer_memories[i]);
    }

    light_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    light_buffer_memories.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        create_buffer(&vk_context, sizeof(DirectionalLight), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &light_buffers[i], &light_buffer_memories[i]);
    }

    camera.position = glm::vec3(4.34f, 3.42f, 5.78f);
    camera.orientation = glm::quat(0.93f, -0.21f, 0.31f, 0.07f);

    {
        GeometryData geometry_data = generate_triangle_geometry_data();
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        Transform transform = {glm::vec3(0.0f, 0.5f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({1, geometry_handle, transform, glm::vec3(1.0f, 0.0f, 0.0f)});
    }
    {
        GeometryData geometry_data = generate_plane_geometry_data(4.0f, 4.0f, 2);
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        Transform transform = {glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({2, geometry_handle, transform, glm::vec3(0.0f, 1.0f, 0.0f)});
    }
    {
        GeometryData geometry_data = generate_cube_geometry_data(0.5f);
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        // 位置在三角形左前方：三角形在 (0, 0.5, 0)，立方体在 (-1.0, 0.5, 1.0)
        Transform transform = {glm::vec3(-1.0f, 0.5f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({3, geometry_handle, transform, glm::vec3(0.0f, 0.0f, 1.0f)});  // 蓝色立方体
    }
    {
        GeometryData geometry_data = generate_sphere_geometry_data(0.25f, 32);  // 半径0.25，32段细分
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        // 位置在三角形前方、立方体的对面：三角形在 (0, 0.5, 0)，立方体在 (-1.0, 0.5, 1.0)，球体在 (1.0, 0.5, 1.0)
        Transform transform = {glm::vec3(1.0f, 0.5f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({4, geometry_handle, transform, glm::vec3(1.0f, 1.0f, 0.0f)});  // 黄色球体
    }
    {
        // 左侧墙（Cornell box风格）- 竖着的，面向+x
        GeometryData geometry_data = generate_plane_geometry_data(2.0f, 4.0f, 2);
        uint32_t geometry_handle = request_geometry(&geometry_registry, &task_system, &vk_context, std::move(geometry_data));
        // 绕Z轴旋转-90度，使平面变成YZ平面（垂直），法线指向+X方向
        glm::quat rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        Transform transform = {glm::vec3(-2.0f, 1.0f, 0.0f), rotation, glm::vec3(1.0f, 1.0f, 1.0f)};
        entities.push_back({5, geometry_handle, transform, glm::vec3(1.0f, 0.0f, 0.0f)});  // 红色左侧墙
    }

    // 等待所有 mesh 上传完成后再进入首帧，避免“部分 mesh 不显示或很久才出现”
    {
        const unsigned timeout_ms = 30000;
        unsigned waited_ms = 0;
        while (waited_ms < timeout_ms) {
            bool all_uploaded = true;
            for (const Entity &e : entities) {
                if (!is_geometry_uploaded(&geometry_registry, e.geometry_handle)) {
                    all_uploaded = false;
                    break;
                }
            }
            if (all_uploaded) { break; }
            SDL_Delay(1);
            waited_ms += 1;
        }
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
    total_time += delta_time;  // 累计总时间

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

    // 上传光源数据
    p_data = nullptr;
    vkMapMemory(vk_context.device, light_buffer_memories[frame_index], 0, sizeof(DirectionalLight), 0, &p_data);
    memcpy(p_data, &directional_light, sizeof(DirectionalLight));
    vkUnmapMemory(vk_context.device, light_buffer_memories[frame_index]);

    {
        // 初始化 picking storage buffer
        void *p_data = nullptr;
        vkMapMemory(vk_context.device, picking_storage_buffer_memories[frame_index], 0, sizeof(PickingResult), 0, &p_data);
        PickingResult *picking_result = (PickingResult *) p_data;
        picking_result->entity_id = 0;
        picking_result->min_depth_bits = 0xFFFFFFFFu; // 最大深度值，表示最远
        vkUnmapMemory(vk_context.device, picking_storage_buffer_memories[frame_index]);
    }

    // Update game logic, physics, animations, etc.
    // Process input events
    // Update scene graph
    // Prepare render data (uniforms, descriptors, etc.)

    // 更新三角形上下振荡
    for (Entity &entity: entities) {
        if (entity.entity_id == 1) {  // 三角形的entity_id是1
            const float base_y = 0.6f;  // 基础Y坐标（提高以确保底部不低于0）
            const float amplitude = 0.3f;  // 振荡幅度
            const float frequency = 0.1f;  // 振荡频率（Hz），节奏类似立方体旋转（0.5弧度/秒 ≈ 0.08Hz）
            float oscillation = amplitude * sinf((float) total_time * frequency * 2.0f * 3.14159f);
            entity.transform.position.y = base_y + oscillation;
            break;
        }
    }

    // 更新立方体旋转（绕Y轴缓慢旋转）
    for (Entity &entity: entities) {
        if (entity.entity_id == 3) {  // 立方体的entity_id是3
            const float rotation_speed = 0.5f;  // 弧度/秒
            float rotation_angle = (float) delta_time * rotation_speed;
            glm::quat rotation = glm::angleAxis(rotation_angle, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.transform.orientation = glm::normalize(rotation * entity.transform.orientation);
            break;
        }
    }

    // 更新球体前后振荡
    for (Entity &entity: entities) {
        if (entity.entity_id == 4) {  // 球体的entity_id是4
            const float base_z = 1.0f;  // 基础Z坐标
            const float amplitude = 0.5f;  // 振荡幅度
            const float frequency = 0.1f;  // 振荡频率（Hz），节奏类似立方体旋转
            float oscillation = amplitude * sinf((float) total_time * frequency * 2.0f * 3.14159f);
            entity.transform.position.z = base_z + oscillation;
            break;
        }
    }

    // collect renderables
    std::vector<Renderable> renderables = {};
    for (const Entity &entity : entities) {
        if (!is_geometry_uploaded(&geometry_registry, entity.geometry_handle)) { continue; }
        frame_states[frame_index].geometry_handles.insert(entity.geometry_handle);
        increment_geometry_ref(&geometry_registry, entity.geometry_handle);
        renderables.push_back({entity.entity_id, entity.geometry_handle, compute_model_matrix(entity.transform), entity.color});
    }

    assert(renderables.size() <= MAX_TOP_LEVEL_ACCELERATION_STRUCTURE_INSTANCE_COUNT);

    // update descriptor set
    std::vector<VkWriteDescriptorSet> write_descriptor_sets = {};
    {
        VkDescriptorBufferInfo cameras_buffer_info = {};
        cameras_buffer_info.buffer = camera_buffers[frame_index];
        cameras_buffer_info.offset = 0;
        cameras_buffer_info.range = sizeof(CameraData) * 2;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 0;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pBufferInfo = &cameras_buffer_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    if (picking_states[frame_index] == PICKING_STATE_REQUESTED) {
        VkDescriptorBufferInfo picking_storage_buffer_info = {};
        picking_storage_buffer_info.buffer = picking_storage_buffers[frame_index];
        picking_storage_buffer_info.offset = 0;
        picking_storage_buffer_info.range = sizeof(PickingResult);
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 1;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pBufferInfo = &picking_storage_buffer_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkWriteDescriptorSetAccelerationStructureKHR tlas_write_descriptor_set = {};
        tlas_write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        tlas_write_descriptor_set.accelerationStructureCount = 1;
        tlas_write_descriptor_set.pAccelerationStructures = &tlas[frame_index];
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 2;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pNext = &tlas_write_descriptor_set;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorBufferInfo light_buffer_info = {};
        light_buffer_info.buffer = light_buffers[frame_index];
        light_buffer_info.offset = 0;
        light_buffer_info.range = sizeof(DirectionalLight);
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 3;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pBufferInfo = &light_buffer_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo path_tracing_image_info = {};
        path_tracing_image_info.imageView = path_tracing_image_views[frame_index];
        path_tracing_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 4;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &path_tracing_image_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo gbuffer_position_info = {};
        gbuffer_position_info.imageView = gbuffer_position_views[frame_index];
        gbuffer_position_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 5;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &gbuffer_position_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo gbuffer_normal_info = {};
        gbuffer_normal_info.imageView = gbuffer_normal_views[frame_index];
        gbuffer_normal_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 6;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &gbuffer_normal_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo gbuffer_albedo_info = {};
        gbuffer_albedo_info.imageView = gbuffer_albedo_views[frame_index];
        gbuffer_albedo_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 7;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &gbuffer_albedo_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo gbuffer_depth_info = {};
        gbuffer_depth_info.imageView = gbuffer_depth_views[frame_index];
        gbuffer_depth_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set  = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 8;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &gbuffer_depth_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo direct_radiance_info = {};
        direct_radiance_info.imageView = direct_radiance_image_views[frame_index];
        direct_radiance_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 9;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &direct_radiance_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    {
        VkDescriptorImageInfo indirect_radiance_info = {};
        indirect_radiance_info.imageView = indirect_radiance_image_views[frame_index];
        indirect_radiance_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet = descriptor_sets[frame_index];
        write_descriptor_set.dstBinding = 10;
        write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pImageInfo = &indirect_radiance_info;
        write_descriptor_sets.push_back(write_descriptor_set);
    }
    vkUpdateDescriptorSets(vk_context.device, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);

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

    build_top_level_acceleration_structure(command_buffer, &vk_context, renderables);

    // 转换路径追踪图像 layout 为 GENERAL
    record_pipeline_image_barrier(command_buffer, path_tracing_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // 转换 G-buffer 图像 layout 为 GENERAL
    record_pipeline_image_barrier(command_buffer, gbuffer_position_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    record_pipeline_image_barrier(command_buffer, gbuffer_normal_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    record_pipeline_image_barrier(command_buffer, gbuffer_albedo_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    record_pipeline_image_barrier(command_buffer, gbuffer_depth_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    record_pipeline_image_barrier(command_buffer, direct_radiance_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);
    record_pipeline_image_barrier(command_buffer, indirect_radiance_images[frame_index],
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // 确保 TLAS 构建完成，对 path tracing compute shader 可见
    record_pipeline_memory_barrier(command_buffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context.compute_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context.compute_pipeline_layout, 0, 1, &descriptor_sets[frame_index], 0, nullptr);

    PathTracingPushConstants path_tracing_push_constants = {};
    path_tracing_push_constants.camera_index = 0;
    path_tracing_push_constants.iteration = frame_count; // 渐进式渲染的全局迭代次数
    vkCmdPushConstants(command_buffer, vk_context.compute_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PathTracingPushConstants), &path_tracing_push_constants);

    uint32_t group_count_x = (app_state->render_width + 7) / 8;
    uint32_t group_count_y = (app_state->render_height + 7) / 8;
    vkCmdDispatch(command_buffer, group_count_x, group_count_y, 1);

    // 确保 compute shader 完成，path tracing 图像可以用于 transfer
    record_pipeline_memory_barrier(command_buffer,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_TRANSFER_READ_BIT);

    // 转换 path tracing 图像 layout 为 TRANSFER_SRC_OPTIMAL
    record_pipeline_image_barrier(command_buffer, path_tracing_images[frame_index],
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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

    // 确保 TLAS 构建完成，对 fragment shader 可见（用于 ray query）
    // 放在 begin_rendering 之前，使得图像布局转换可以与 TLAS 构建并行执行，最大化并行度
    record_pipeline_memory_barrier(command_buffer,
                                  VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                                  VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    VkClearColorValue clear_color_value = {.float32 = {0.5f, 0.8f, 1.0f, 1.0f}}; // 轻松活泼的天空蓝色 (RGB: 128, 204, 255)
    // VkClearColorValue clear_color_value = {.float32 = {0.5f, 1.0f, 0.8f}}; // 薄荷绿
    // VkClearColorValue clear_color_value = {.float32 = {0.98f, 0.92f, 0.95f, 1.0f}}; // 樱花粉 (RGB: 250, 235, 242)
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

        // Blit path tracing 结果到屏幕上方（水平居中，顶部对齐，保持原始尺寸）
        blit_image(command_buffer, path_tracing_images[frame_index], vk_context.swap_chain_images[image_index],
                   0, 0, app_state->render_width, app_state->render_height, // 源区域：整个 path tracing 图像
                   (app_state->width - app_state->render_width) / 2, 0, app_state->render_width, app_state->render_height); // 目标区域：屏幕上方

        // Blit 光栅化渲染结果到屏幕下半部分（水平居中，底部对齐，保持原始尺寸）
        blit_image(command_buffer, color_images[frame_index], vk_context.swap_chain_images[image_index],
                   0, 0, app_state->render_width, app_state->render_height, // 源区域：整个渲染图像
                   (app_state->width - app_state->render_width) / 2, app_state->height - app_state->render_height, app_state->render_width, app_state->render_height); // 目标区域：屏幕下半部分

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

    frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    frame_count++;

    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *p_app_state, SDL_AppResult result)
{
    vkDeviceWaitIdle(vk_context.device);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        destroy_buffer(&vk_context, light_buffers[i], light_buffer_memories[i]);
    }
    light_buffers.clear();
    light_buffer_memories.clear();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        destroy_buffer(&vk_context, camera_buffers[i], camera_buffer_memories[i]);
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
        vk_context.vkDestroyAccelerationStructureKHR(vk_context.device, tlas[i], nullptr);
        destroy_buffer(&vk_context, tlas_buffers[i], tlas_buffer_memories[i]);
        destroy_buffer(&vk_context, instance_buffers[i], instance_buffer_memories[i]);
        destroy_buffer(&vk_context, scratch_buffers[i], scratch_buffer_memories[i]);
    }
    tlas.clear();
    tlas_buffers.clear();
    tlas_buffer_memories.clear();
    instance_buffers.clear();
    instance_buffer_memories.clear();
    scratch_buffers.clear();
    scratch_buffer_memories.clear();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        destroy_buffer(&vk_context, picking_storage_buffers[i], picking_storage_buffer_memories[i]);
    }
    picking_storage_buffers.clear();
    picking_storage_buffer_memories.clear();
    destroy_gbuffer_images(&vk_context);
    destroy_direct_indirect_radiance_images(&vk_context);
    destroy_path_tracing_images(&vk_context);
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


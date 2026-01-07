#pragma once

#include "vulkan/vulkan_core.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <unordered_map>
#include <vector>

#define LOAD_INSTANCE_PROC_ADDR(instance, name) (PFN_ ## name) vkGetInstanceProcAddr(instance, #name);
#define LOAD_DEVICE_PROC_ADDR(device, name) (PFN_ ## name) vkGetDeviceProcAddr(device, #name);

#define VK_CHECK(result) do { \
    if (VkResult vk_result = (result); vk_result != VK_SUCCESS) { \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "error code %d, at %s:%d", vk_result, __FILE__, __LINE__); \
        assert(vk_result == VK_SUCCESS); \
    } \
} while(0)

struct Vertex {
    float position[3];
};

struct PushConstants {
    glm::mat4 model;
    alignas(16) glm::vec3 color;
    uint32_t camera_index;
    uint32_t entity_id;
};

struct PipelineKey {
    union {
        struct {
            VkPrimitiveTopology primitive_topology: 4; // bits 0-4
            VkPolygonMode polygon_mode: 2; // bits 5-6
            bool depth_test_enabled: 1; // bit 7
            bool depth_write_enabled: 1; // bit 8
        };
        uint32_t state_bits; // 低 32 位
    };
    uint32_t shaders_hash; // 高 32 位

    bool operator==(const PipelineKey &other) const {
        return state_bits == other.state_bits && shaders_hash == other.shaders_hash;
    }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey &key) const {
        return ((uint64_t) key.shaders_hash << 32) | key.state_bits;
    }
};

struct VkContext {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_utils_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    uint32_t queue_family_index;
    uint32_t transfer_queue_family_index;
    VkDevice device;
    VkQueue queue;
    VkQueue transfer_queue;
    VkSwapchainKHR swap_chain;
    VkFormat surface_format;
    VkColorSpaceKHR surface_color_space;
    std::vector<VkImage> swap_chain_images;
    std::vector<VkImageView> swap_chain_image_views;
    VkFormat depth_image_format;
    VkCommandPool command_pool;
    VkCommandPool transfer_command_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines;

    // 动态加载的函数指针
    PFN_vkCmdSetCullMode vkCmdSetCullMode;
    PFN_vkCmdBeginRendering vkCmdBeginRendering;
    PFN_vkCmdEndRendering vkCmdEndRendering;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
};

void create_vulkan_instance(VkContext *context);

void create_vulkan_surface(VkContext *context, SDL_Window *window);

void choose_physical_device(VkContext *context);

void create_device(VkContext *context);

void create_swap_chain(VkContext *context, uint32_t width, uint32_t height);
void destroy_swap_chain(VkContext *context);

void choose_depth_format(VkContext *context);

void create_command_pool(VkContext *context);

void create_pipelines(VkContext *context);

void allocate_command_buffers(VkContext *context, VkCommandPool command_pool, uint32_t count, VkCommandBuffer *command_buffers);

void get_memory_type_index(VkContext *context, const VkMemoryRequirements &memory_requirements, VkMemoryPropertyFlags memory_property_flags, uint32_t *memory_type_index);

void allocate_memory(VkContext *context, const VkMemoryRequirements &memory_requirements, VkMemoryPropertyFlags memory_property_flags, VkMemoryAllocateFlags memory_allocate_flags, VkDeviceMemory *memory);

void allocate_descriptor_set(VkContext *context, VkDescriptorPool descriptor_pool, VkDescriptorSet *descriptor_set);

void create_image(VkContext *context, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *image_memory);

void destroy_image(VkContext *context, VkImage image, VkDeviceMemory image_memory);

void create_image_view(VkContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, VkImageView *image_view, const char *name);

void create_buffer(VkContext *context, VkDeviceSize size, VkBufferUsageFlags buffer_usage_flags, VkMemoryPropertyFlags memory_property_flags, VkBuffer *buffer, VkDeviceMemory *buffer_memory);

void destroy_buffer(VkContext *context, VkBuffer buffer, VkDeviceMemory buffer_memory);

void set_debug_object_name(VkContext *context, VkObjectType object_type, uint64_t object_handle, const char *name);

void create_descriptor_set_layout(VkContext *context);

void create_pipeline_layout(VkContext *context, size_t push_constant_size);

void create_shader_module(VkContext *context, const char *filepath, VkShaderModule *shader_module);

void create_pipeline(VkContext *context, VkPrimitiveTopology primitive_topology, VkPolygonMode polygon_mode, bool depth_test_enabled, bool depth_write_enabled, VkFormat color_image_format, VkFormat depth_image_format, const char *vertex_shader_name, const char *fragment_shader_name);

VkPipeline get_pipeline(VkContext *context, PipelineKey pipeline_key);

void record_pipeline_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect_mask, VkPipelineStageFlags src_stage_flags, VkPipelineStageFlags dst_stage_flags, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask, VkImageLayout src_layout, VkImageLayout dst_layout);

void blit_image(VkCommandBuffer command_buffer, VkImage src_image, VkImage dst_image, uint32_t src_width, uint32_t src_height, uint32_t dst_width, uint32_t dst_height);

void set_viewport(VkCommandBuffer command_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void set_scissor(VkCommandBuffer command_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void copy_buffer(VkCommandBuffer command_buffer, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size);

void create_semaphore(VkContext *context, VkSemaphore *semaphore);

void create_fence(VkContext *context, bool signaled, VkFence *fence);

void begin_rendering(VkContext *context, VkCommandBuffer command_buffer, VkImageView color_image_view, VkClearColorValue *clear_color_value, VkImageView depth_image_view, VkClearDepthStencilValue *clear_depth_stencil_value, uint32_t width, uint32_t height);

void end_rendering(VkContext *context, VkCommandBuffer command_buffer);

void execute_one_time_submit(VkContext *context, VkCommandPool command_pool, VkQueue queue, std::function<void(VkCommandBuffer)> &&func);

VkDeviceAddress get_buffer_device_address(VkContext *context, VkBuffer buffer);

VkDeviceAddress get_acceleration_structure_device_address(VkContext *context, VkAccelerationStructureKHR acceleration_structure);

void cleanup_vulkan(VkContext *context);

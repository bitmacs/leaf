#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vector>

#define LOAD_INSTANCE_PROC_ADDR(instance, name) (PFN_ ## name) vkGetInstanceProcAddr(instance, #name);
#define LOAD_DEVICE_PROC_ADDR(device, name) (PFN_ ## name) vkGetDeviceProcAddr(device, #name);

struct VkContext {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_utils_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    uint32_t queue_family_index;
    VkDevice device;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkFormat surface_format;
    VkColorSpaceKHR surface_color_space;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat depth_image_format;
    VkCommandPool command_pool;
    VkRenderPass render_pass;
};

void create_vulkan_instance(VkContext *context);

void create_vulkan_surface(VkContext *context, SDL_Window *window);

void choose_physical_device(VkContext *context);

void create_device(VkContext *context);

void create_swapchain(VkContext *context, uint32_t width, uint32_t height);

void create_command_pool(VkContext *context);

void create_render_pass(VkContext *context);

void allocate_command_buffers(VkContext *context, uint32_t count, VkCommandBuffer *command_buffers);

void get_memory_type_index(VkContext *context, const VkMemoryRequirements &memory_requirements, VkMemoryPropertyFlags memory_property_flags, uint32_t *memory_type_index);

void allocate_memory(VkContext *context, VkDeviceSize size, uint32_t memory_type_index, VkDeviceMemory *memory);

void create_image(VkContext *context, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *image_memory);

void create_image_view(VkContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, VkImageView *image_view);

void create_framebuffer(VkContext *context, VkRenderPass render_pass, uint32_t attachment_count, VkImageView *attachments, uint32_t width, uint32_t height, VkFramebuffer *framebuffer);

void begin_render_pass(VkContext *context, VkCommandBuffer command_buffer, VkRenderPass render_pass, VkFramebuffer framebuffer, uint32_t width, uint32_t height, uint32_t clear_value_count, VkClearValue *clear_values);

void end_render_pass(VkContext *context, VkCommandBuffer command_buffer);

void record_pipeline_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkPipelineStageFlags src_stage_flags, VkPipelineStageFlags dst_stage_flags, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask, VkImageLayout src_layout, VkImageLayout dst_layout);

void blit_image(VkCommandBuffer command_buffer, VkImage src_image, VkImageLayout src_image_layout, VkImage dst_image, VkImageLayout dst_image_layout, uint32_t width, uint32_t height);

void cleanup_vulkan(VkContext *context);

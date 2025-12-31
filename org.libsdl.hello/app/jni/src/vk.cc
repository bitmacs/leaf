#include "vk.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
    const char *severity_str;
    const char *type_str;

    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity_str = "error";
    } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity_str = "warning";
    } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severity_str = "info";
    } else {
        severity_str = "debug";
    }

    if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        type_str = "general";
    } else if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        type_str = "validation";
    } else if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        type_str = "performance";
    } else {
        type_str = "unknown";
    }

    SDL_Log("validation layer: %s: %s: %s", severity_str, type_str, callback_data->pMessage);
    return VK_FALSE;
}

void create_vulkan_instance(VkContext *context) {
    std::vector<const char *> instance_extensions;
    std::vector<const char *> instance_layers;

    {
        uint32_t extension_count = 0;
        char const *const *extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        for (uint32_t i = 0; i < extension_count; ++i) {
            instance_extensions.push_back(extensions[i]);
        }
    }

    instance_extensions.push_back("VK_EXT_debug_utils");
    instance_layers.push_back("VK_LAYER_KHRONOS_validation");

    // {
    //     uint32_t extension_count = 0;
    //     vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    //     std::vector<VkExtensionProperties> extensions(extension_count);
    //     vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
    //     for (const auto &extension: extensions) {
    //         SDL_Log("instance extension: %s", extension.extensionName);
    //     }
    // }

    // {
    //     uint32_t layer_count = 0;
    //     vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    //     std::vector<VkLayerProperties> layers(layer_count);
    //     vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    //     for (const auto &layer: layers) {
    //         SDL_Log("instance layer: %s", layer.layerName);
    //     }
    // }

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "gfx demo";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "gfx demo";
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = VK_API_VERSION_1_3;

    std::vector<VkValidationFeatureEnableEXT> validation_feature_enables = {};
    validation_feature_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
    validation_feature_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
    validation_feature_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);

    VkValidationFeaturesEXT validation_features = {};
    validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validation_features.enabledValidationFeatureCount = validation_feature_enables.size();
    validation_features.pEnabledValidationFeatures = validation_feature_enables.data();
    validation_features.pNext = nullptr; // end of chain

    VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info = {};
    debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_utils_messenger_create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_utils_messenger_create_info.pfnUserCallback = debug_callback;
    debug_utils_messenger_create_info.pNext = &validation_features;

    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount = instance_extensions.size();
    instance_create_info.ppEnabledExtensionNames = instance_extensions.data();
    instance_create_info.enabledLayerCount = instance_layers.size();
    instance_create_info.ppEnabledLayerNames = instance_layers.data();
    instance_create_info.pNext = &debug_utils_messenger_create_info; // start with debug messenger

    VkResult result = vkCreateInstance(&instance_create_info, nullptr, &context->instance);
    assert(result == VK_SUCCESS);

    auto vkCreateDebugUtilsMessengerEXT = LOAD_INSTANCE_PROC_ADDR(context->instance, vkCreateDebugUtilsMessengerEXT);
    result = vkCreateDebugUtilsMessengerEXT(context->instance, &debug_utils_messenger_create_info, nullptr, &context->debug_utils_messenger);
    assert(result == VK_SUCCESS);
}

void create_vulkan_surface(VkContext *context, SDL_Window *window) {
    bool result = SDL_Vulkan_CreateSurface(window, context->instance, nullptr, &context->surface);
    assert(result);
}

void choose_physical_device(VkContext *context) {
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(context->instance, &physical_device_count, nullptr);
    assert(physical_device_count > 0);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices.data());

    for (const auto &physical_device: physical_devices) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

        uint32_t queue_family_index = UINT32_MAX;

        for (uint32_t i = 0; i < queue_family_count; ++i) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 presentation_support = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, context->surface, &presentation_support);
                if (presentation_support) {
                    queue_family_index = i;
                    break;
                }
            }
        }

        if (queue_family_index == UINT32_MAX) { continue; }

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        SDL_Log("chosen physical device: name: %s, type: %d, api version: %d.%d.%d",
                properties.deviceName, properties.deviceType,
                VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));

        context->physical_device = physical_device;
        context->queue_family_index = queue_family_index;
        // break;
    }
    assert(context->physical_device && "no suitable physical device found");
}

void create_device(VkContext *context) {
    // VkPhysicalDeviceFeatures features;
    // vkGetPhysicalDeviceFeatures(context->physical_device, &features);
    // assert(features.vertexPipelineStoresAndAtomics == VK_TRUE && "vertex pipeline stores and atomics is not supported");

    std::vector<const char *> required_device_extensions;
    std::vector<const char *> device_layers; // TODO

    required_device_extensions.push_back("VK_KHR_swapchain");
    required_device_extensions.push_back("VK_KHR_shader_non_semantic_info");
    required_device_extensions.push_back("VK_KHR_deferred_host_operations");
    required_device_extensions.push_back("VK_KHR_acceleration_structure");
    required_device_extensions.push_back("VK_KHR_ray_query");
    required_device_extensions.push_back("VK_KHR_pipeline_library");
    required_device_extensions.push_back("VK_KHR_ray_tracing_pipeline");
    device_layers.push_back("VK_LAYER_KHRONOS_validation");

    {
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_count, extensions.data());

        for (const auto &required_device_extension: required_device_extensions) {
            bool found = false;
            for (const auto &extension: extensions) {
                if (std::strcmp(extension.extensionName, required_device_extension) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "required device extension not found: %s", required_device_extension);
                assert(false);
            }
        }
    }

    // {
    //     uint32_t layer_count = 0;
    //     vkEnumerateDeviceLayerProperties(context->physical_device, &layer_count, nullptr);
    //
    //     std::vector<VkLayerProperties> layers(layer_count);
    //     vkEnumerateDeviceLayerProperties(context->physical_device, &layer_count, layers.data());
    //
    //     for (const auto &layer: layers) {
    //         SDL_Log("device layer: %s", layer.layerName);
    //     }
    // }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = context->queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features = {};
    device_features.fillModeNonSolid = VK_TRUE;
    device_features.vertexPipelineStoresAndAtomics = VK_TRUE;

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledExtensionCount = required_device_extensions.size();
    device_create_info.ppEnabledExtensionNames = required_device_extensions.data();
    device_create_info.enabledLayerCount = device_layers.size();
    device_create_info.ppEnabledLayerNames = device_layers.data();
    device_create_info.pEnabledFeatures = &device_features;

    VkResult result = vkCreateDevice(context->physical_device, &device_create_info, nullptr, &context->device);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "error creating device, result: %d", result);
        assert(false);
    }

    vkGetDeviceQueue(context->device, context->queue_family_index, 0, &context->queue);
}

void create_swapchain(VkContext *context, uint32_t width, uint32_t height) {
    // get supported formats
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &format_count, nullptr);
    assert(format_count > 0);

    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &format_count, formats.data());

    VkFormat surface_format = formats[0].format;
    VkColorSpaceKHR surface_color_space = formats[0].colorSpace;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, context->surface, &surface_capabilities);

    uint32_t min_image_count = 2;
    if (surface_capabilities.minImageCount > min_image_count) {
        min_image_count = surface_capabilities.minImageCount;
    }
    if (surface_capabilities.maxImageCount > 0 && surface_capabilities.maxImageCount < min_image_count) {
        min_image_count = surface_capabilities.maxImageCount;
    }

    // 选择支持的 composite alpha 模式
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR;
    VkCompositeAlphaFlagBitsKHR preferred_modes[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
    };
    for (const auto &mode : preferred_modes) {
        if (surface_capabilities.supportedCompositeAlpha & mode) {
            composite_alpha = mode;
            break;
        }
    }
    assert(composite_alpha != VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR && "no supported composite alpha mode found");

    // 检查 surface 是否支持 TRANSFER_DST_BIT（用于 blit 到 swapchain）
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    assert(surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT && "surface does not support TRANSFER_DST_BIT");
    image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkSwapchainCreateInfoKHR swapchain_create_info = {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = context->surface;
    swapchain_create_info.minImageCount = min_image_count;
    swapchain_create_info.imageFormat = surface_format;
    swapchain_create_info.imageColorSpace = surface_color_space;
    swapchain_create_info.imageExtent = {width, height};
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = image_usage;
    swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha = composite_alpha;
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(context->device, &swapchain_create_info, nullptr, &context->swapchain);
    assert(result == VK_SUCCESS);

    context->surface_format = surface_format;
    context->surface_color_space = surface_color_space;

    // get swapchain images
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &image_count, nullptr);
    assert(image_count > 0);

    context->swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(context->device, context->swapchain, &image_count, context->swapchain_images.data());

    // create swapchain image views
    context->swapchain_image_views.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info = {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = context->swapchain_images[i];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = surface_format;
        image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(context->device, &image_view_create_info, nullptr, &context->swapchain_image_views[i]);
        assert(result == VK_SUCCESS);
    }
}

void create_command_pool(VkContext *context) {
    VkCommandPoolCreateInfo command_pool_create_info = {};
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_create_info.queueFamilyIndex = context->queue_family_index;
    VkResult result = vkCreateCommandPool(context->device, &command_pool_create_info, nullptr, &context->command_pool);
    assert(result == VK_SUCCESS);
}

void create_render_pass(VkContext *context) {
    // a render pass with one color attachment and one depth attachment

    VkAttachmentDescription color_attachment = {};
    color_attachment.format = context->surface_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = context->depth_image_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkRenderPassCreateInfo render_pass_create_info = {};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = std::size(attachments);
    render_pass_create_info.pAttachments = attachments;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    VkResult result = vkCreateRenderPass(context->device, &render_pass_create_info, nullptr, &context->render_pass);
    assert(result == VK_SUCCESS);
}

void allocate_command_buffers(VkContext *context, uint32_t count, VkCommandBuffer *command_buffers) {
    VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.commandPool = context->command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = count;
    VkResult result = vkAllocateCommandBuffers(context->device, &command_buffer_allocate_info, command_buffers);
    assert(result == VK_SUCCESS);
}

void get_memory_type_index(VkContext *context, const VkMemoryRequirements &memory_requirements, VkMemoryPropertyFlags memory_property_flags, uint32_t *memory_type_index) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(context->physical_device, &memory_properties);

    *memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((memory_requirements.memoryTypeBits & (1 << i)) &&
            memory_properties.memoryTypes[i].propertyFlags & memory_property_flags) {
            *memory_type_index = i;
            break;
        }
    }
    assert(*memory_type_index != UINT32_MAX);
}

void allocate_memory(VkContext *context, VkDeviceSize size, uint32_t memory_type_index, VkDeviceMemory *memory) {
    VkMemoryAllocateInfo memory_allocate_info = {};
    memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_allocate_info.allocationSize = size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;
    VkResult result = vkAllocateMemory(context->device, &memory_allocate_info, nullptr, memory);
    assert(result == VK_SUCCESS);
}

void create_image(VkContext *context, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *image_memory) {
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = format;
    image_create_info.extent = {width, height, 1};
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = usage;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = vkCreateImage(context->device, &image_create_info, nullptr, image);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(context->device, *image, &memory_requirements);

    uint32_t memory_type_index = UINT32_MAX;
    get_memory_type_index(context, memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type_index);

    allocate_memory(context, memory_requirements.size, memory_type_index, image_memory);
    vkBindImageMemory(context->device, *image, *image_memory, 0);
}

void create_image_view(VkContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, VkImageView *image_view) {
    VkImageViewCreateInfo image_view_create_info = {};
    image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create_info.image = image;
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = format;
    image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_create_info.subresourceRange.aspectMask = aspect_mask;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = 1;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;
    VkResult result = vkCreateImageView(context->device, &image_view_create_info, nullptr, image_view);
    assert(result == VK_SUCCESS);
}

void create_framebuffer(VkContext *context, VkRenderPass render_pass, uint32_t attachment_count, VkImageView *attachments, uint32_t width, uint32_t height, VkFramebuffer *framebuffer) {
    VkFramebufferCreateInfo framebuffer_create_info = {};
    framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_create_info.renderPass = render_pass;
    framebuffer_create_info.attachmentCount = attachment_count;
    framebuffer_create_info.pAttachments = attachments;
    framebuffer_create_info.width = width;
    framebuffer_create_info.height = height;
    framebuffer_create_info.layers = 1;
    VkResult result = vkCreateFramebuffer(context->device, &framebuffer_create_info, nullptr, framebuffer);
    assert(result == VK_SUCCESS);
}

void begin_render_pass(VkContext *context, VkCommandBuffer command_buffer, VkRenderPass render_pass, VkFramebuffer framebuffer, uint32_t width, uint32_t height, uint32_t clear_value_count, VkClearValue *clear_values) {
    VkRenderPassBeginInfo render_pass_begin_info = {};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = framebuffer;
    render_pass_begin_info.renderArea.extent.width = width;
    render_pass_begin_info.renderArea.extent.height = height;
    render_pass_begin_info.clearValueCount = clear_value_count;
    render_pass_begin_info.pClearValues = clear_values;
    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void end_render_pass(VkContext *context, VkCommandBuffer command_buffer) {
    vkCmdEndRenderPass(command_buffer);
}

void record_pipeline_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkPipelineStageFlags src_stage_flags, VkPipelineStageFlags dst_stage_flags, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask, VkImageLayout src_layout, VkImageLayout dst_layout) {
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.srcAccessMask = src_access_mask;
    image_barrier.dstAccessMask = dst_access_mask;
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer, src_stage_flags, dst_stage_flags, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);
}

void blit_image(VkCommandBuffer command_buffer, VkImage src_image, VkImageLayout src_image_layout, VkImage dst_image, VkImageLayout dst_image_layout, uint32_t width, uint32_t height) {
    VkImageBlit blit_region = {};
    blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.srcSubresource.mipLevel = 0;
    blit_region.srcSubresource.baseArrayLayer = 0;
    blit_region.srcSubresource.layerCount = 1;
    blit_region.srcOffsets[0] = {0, 0, 0};
    blit_region.srcOffsets[1] = {(int32_t) width, (int32_t) height, 1};
    blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.dstSubresource.mipLevel = 0;
    blit_region.dstSubresource.baseArrayLayer = 0;
    blit_region.dstSubresource.layerCount = 1;
    blit_region.dstOffsets[0] = {0, 0, 0};
    blit_region.dstOffsets[1] = {(int32_t) width, (int32_t) height, 1};
    vkCmdBlitImage(command_buffer, src_image, src_image_layout, dst_image, dst_image_layout, 1, &blit_region, VK_FILTER_LINEAR);
}

void cleanup_vulkan(VkContext *context) {
    vkDestroyRenderPass(context->device, context->render_pass, nullptr);
    vkDestroyCommandPool(context->device, context->command_pool, nullptr);
    for (const auto &image_view: context->swapchain_image_views) {
        vkDestroyImageView(context->device, image_view, nullptr);
    }
    context->swapchain_image_views.clear();
    context->swapchain_images.clear();
    vkDestroySwapchainKHR(context->device, context->swapchain, nullptr);
    vkDestroyDevice(context->device, nullptr);
    SDL_Vulkan_DestroySurface(context->instance, context->surface, nullptr);
    auto vkDestroyDebugUtilsMessengerEXT = LOAD_INSTANCE_PROC_ADDR(context->instance, vkDestroyDebugUtilsMessengerEXT);
    vkDestroyDebugUtilsMessengerEXT(context->instance, context->debug_utils_messenger, nullptr);
    vkDestroyInstance(context->instance, nullptr);
}

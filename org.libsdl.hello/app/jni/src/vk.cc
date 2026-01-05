#include "vk.h"
#include "files.h"
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
    instance_extensions.push_back("VK_KHR_get_physical_device_properties2");
    instance_layers.push_back("VK_LAYER_KHRONOS_validation");

    {
        uint32_t extension_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
        for (const auto &extension: extensions) {
            SDL_Log("instance extension: %s", extension.extensionName);
        }
    }

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
        break;
    }
    assert(context->physical_device && "no suitable physical device found");
}

void create_device(VkContext *context) {
    // VkPhysicalDeviceFeatures features;
    // vkGetPhysicalDeviceFeatures(context->physical_device, &features);
    // assert(features.vertexPipelineStoresAndAtomics == VK_TRUE && "vertex pipeline stores and atomics is not supported");

    {
        // query ray tracing pipeline properties
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties = {};
        ray_tracing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        ray_tracing_properties.pNext = nullptr;

        // query acceleration structure properties
        VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties = {};
        acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        acceleration_structure_properties.pNext = &ray_tracing_properties;

        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
        properties.pNext = &acceleration_structure_properties;

        auto vkGetPhysicalDeviceProperties2KHR = LOAD_INSTANCE_PROC_ADDR(context->instance, vkGetPhysicalDeviceProperties2KHR);
        vkGetPhysicalDeviceProperties2KHR(context->physical_device, &properties);
        SDL_Log("ray tracing pipeline properties: max ray recursion depth: %d", ray_tracing_properties.maxRayRecursionDepth);
        SDL_Log("acceleration structure properties: max instance count: %lu", acceleration_structure_properties.maxInstanceCount);
    }

    std::vector<const char *> required_device_extensions;
    std::vector<const char *> device_layers; // TODO

    required_device_extensions.push_back("VK_KHR_swapchain");

    required_device_extensions.push_back("VK_KHR_dynamic_rendering");
    required_device_extensions.push_back("VK_KHR_dynamic_rendering_local_read");

    required_device_extensions.push_back("VK_KHR_shader_non_semantic_info");
    required_device_extensions.push_back("VK_KHR_deferred_host_operations");
    required_device_extensions.push_back("VK_KHR_acceleration_structure");
    required_device_extensions.push_back("VK_KHR_ray_query");
    required_device_extensions.push_back("VK_KHR_pipeline_library");
    required_device_extensions.push_back("VK_KHR_ray_tracing_pipeline"); // use vkCmdTraceRaysKHR
    device_layers.push_back("VK_LAYER_KHRONOS_validation");

    {
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(context->physical_device, nullptr, &extension_count, extensions.data());

        for (const auto &extension: extensions) {
            SDL_Log("extension: %s", extension.extensionName);
        }

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

    // 加载动态函数（Vulkan 1.3 核心函数，但某些实现可能需要动态加载）
    context->vkCmdSetCullMode = LOAD_DEVICE_PROC_ADDR(context->device, vkCmdSetCullMode);
    assert(context->vkCmdSetCullMode && "vkCmdSetCullMode not available");
}

void create_swap_chain(VkContext *context, uint32_t width, uint32_t height) {
    // get supported formats
    uint32_t format_count = 0;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &format_count, nullptr);
    VK_CHECK(result);
    assert(format_count > 0);

    std::vector<VkSurfaceFormatKHR> formats(format_count);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface, &format_count, formats.data());
    VK_CHECK(result);

    VkFormat surface_format = formats[0].format;
    VkColorSpaceKHR surface_color_space = formats[0].colorSpace;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, context->surface, &surface_capabilities);
    VK_CHECK(result);

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

    result = vkCreateSwapchainKHR(context->device, &swapchain_create_info, nullptr, &context->swap_chain);
    assert(result == VK_SUCCESS);

    context->surface_format = surface_format;
    context->surface_color_space = surface_color_space;

    // get swapchain images
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(context->device, context->swap_chain, &image_count, nullptr);
    assert(image_count > 0);

    context->swap_chain_images.resize(image_count);
    vkGetSwapchainImagesKHR(context->device, context->swap_chain, &image_count, context->swap_chain_images.data());

    // create swapchain image views
    context->swap_chain_image_views.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info = {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = context->swap_chain_images[i];
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
        result = vkCreateImageView(context->device, &image_view_create_info, nullptr, &context->swap_chain_image_views[i]);
        assert(result == VK_SUCCESS);
    }
}

void destroy_swap_chain(VkContext *context) {
    for (const auto &image_view: context->swap_chain_image_views) {
        vkDestroyImageView(context->device, image_view, nullptr);
    }
    context->swap_chain_image_views.clear();
    context->swap_chain_images.clear();
    vkDestroySwapchainKHR(context->device, context->swap_chain, nullptr);
}

void choose_depth_format(VkContext *context) {
    // 按优先级排序的深度/模板格式列表
    std::vector<VkFormat> candidate_formats = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,  // 32位深度（浮点）+ 8位模板（最佳精度）
        VK_FORMAT_D24_UNORM_S8_UINT,   // 24位深度（归一化）+ 8位模板（最常用，广泛支持）
        VK_FORMAT_D16_UNORM_S8_UINT,   // 16位深度 + 8位模板
    };

    for (VkFormat format : candidate_formats) {
        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(context->physical_device, format, &format_properties);

        // 检查格式是否支持作为深度/模板附件
        if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            context->depth_image_format = format;
            SDL_Log("chosen depth format: %d", format);
            return;
        }
    }
    assert(false && "no supported depth format found");
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
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

void create_picking_render_pass(VkContext *context) {
    // a render pass with no color attachment and one depth attachment

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = context->depth_image_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[1] = {depth_attachment};

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 0;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkRenderPassCreateInfo render_pass_create_info = {};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = std::size(attachments);
    render_pass_create_info.pAttachments = attachments;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    VkResult result = vkCreateRenderPass(context->device, &render_pass_create_info, nullptr, &context->picking_render_pass);
    assert(result == VK_SUCCESS);
}

void create_pipelines(VkContext *context) {
    // shader 名称（不包含路径和扩展名），函数会自动添加 shaders/ 前缀和 .vert.spv/.frag.spv 后缀
    create_pipeline(context, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, true, true, true, context->render_pass, "triangle", "triangle");
    create_pipeline(context, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, false, true, false, context->picking_render_pass, "picking", "picking");
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

void allocate_descriptor_set(VkContext *context, VkDescriptorPool descriptor_pool, VkDescriptorSet *descriptor_set) {
    VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
    descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocate_info.descriptorPool = descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    descriptor_set_allocate_info.pSetLayouts = &context->descriptor_set_layout;
    VkResult result = vkAllocateDescriptorSets(context->device, &descriptor_set_allocate_info, descriptor_set);
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

void create_buffer(VkContext *context, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
    VkBufferCreateInfo buffer_create_info = {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = size;
    buffer_create_info.usage = usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.queueFamilyIndexCount = 1;
    buffer_create_info.pQueueFamilyIndices = &context->queue_family_index;
    VkResult result = vkCreateBuffer(context->device, &buffer_create_info, nullptr, buffer);
    assert(result == VK_SUCCESS);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(context->device, *buffer, &memory_requirements);

    uint32_t memory_type_index = UINT32_MAX;
    get_memory_type_index(context, memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memory_type_index);

    allocate_memory(context, memory_requirements.size, memory_type_index, buffer_memory);

    vkBindBufferMemory(context->device, *buffer, *buffer_memory, 0);
}

void create_descriptor_set_layout(VkContext *context) {
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}, // camera
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // picking storage buffer
    };

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
    descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_create_info.bindingCount = std::size(bindings);
    descriptor_set_layout_create_info.pBindings = bindings;
    VkResult result = vkCreateDescriptorSetLayout(context->device, &descriptor_set_layout_create_info, nullptr, &context->descriptor_set_layout);
    assert(result == VK_SUCCESS);
}

void create_pipeline_layout(VkContext *context, size_t push_constant_size) {
    VkPushConstantRange push_constant_range = {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = push_constant_size;

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts = &context->descriptor_set_layout;
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
    VkResult result = vkCreatePipelineLayout(context->device, &pipeline_layout_create_info, nullptr, &context->pipeline_layout);
    assert(result == VK_SUCCESS);
}

void create_shader_module(VkContext *context, const char *filepath, VkShaderModule *shader_module) {
    auto code = read_binary_file(filepath);
    VkShaderModuleCreateInfo shader_module_create_info = {};
    shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.codeSize = code.size();
    shader_module_create_info.pCode = (const uint32_t *) code.data();
    VkResult result = vkCreateShaderModule(context->device, &shader_module_create_info, nullptr, shader_module);
    assert(result == VK_SUCCESS);
}

void create_pipeline(VkContext *context, VkPrimitiveTopology primitive_topology, VkPolygonMode polygon_mode,
                     bool has_color_attachment, bool depth_test_enabled, bool depth_write_enabled, VkRenderPass render_pass,
                     const char *vertex_shader_name, const char *fragment_shader_name) {
    if (primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST || primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        assert(polygon_mode == VK_POLYGON_MODE_LINE); // polygon mode must be line for line list or line strip topology
    }

    VkVertexInputBindingDescription vertex_input_binding_description = {};
    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(Vertex);
    vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> vertex_input_attribute_descriptions = {};
    {
        VkVertexInputAttributeDescription vertex_input_attribute_description = {};
        vertex_input_attribute_description.binding = 0;
        vertex_input_attribute_description.location = 0;
        vertex_input_attribute_description.format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_input_attribute_description.offset = offsetof(Vertex, position);
        vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {};
    vertex_input_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_input_binding_description;
    vertex_input_state_create_info.vertexAttributeDescriptionCount = vertex_input_attribute_descriptions.size();
    vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {};
    input_assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state_create_info.topology = primitive_topology;
    if (primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
        input_assembly_state_create_info.primitiveRestartEnable = VK_TRUE;
    }

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {};
    rasterization_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.polygonMode = polygon_mode;
    rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.lineWidth = 1.0f;

    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment_states = {};
    if (has_color_attachment) {
        VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
        color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment_state.blendEnable = VK_FALSE;
        color_blend_attachment_states.push_back(color_blend_attachment_state);
    }

    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
    color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.attachmentCount = color_blend_attachment_states.size();
    color_blend_state_create_info.pAttachments = color_blend_attachment_states.data();

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {};
    depth_stencil_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state_create_info.depthTestEnable = depth_test_enabled ? VK_TRUE : VK_FALSE;
    depth_stencil_state_create_info.depthWriteEnable = depth_write_enabled ? VK_TRUE : VK_FALSE;
    depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
    viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisample_state_create_info = {};
    multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkShaderModule vertex_shader_module;
    VkShaderModule fragment_shader_module;
    // shader 文件在 assets/shaders/ 目录中
    std::string vertex_shader_filepath = std::string("shaders/") + std::string(vertex_shader_name) + ".vert.spv";
    std::string fragment_shader_filepath = std::string("shaders/") + std::string(fragment_shader_name) + ".frag.spv";
    create_shader_module(context, vertex_shader_filepath.c_str(), &vertex_shader_module);
    create_shader_module(context, fragment_shader_filepath.c_str(), &fragment_shader_module);

    VkPipelineShaderStageCreateInfo shader_stage_create_infos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader_module,
            .pName = "main",
        },
    };

    std::vector<VkDynamicState> dynamic_states = {};
    dynamic_states.emplace_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamic_states.emplace_back(VK_DYNAMIC_STATE_SCISSOR);
    if (primitive_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) { dynamic_states.emplace_back(VK_DYNAMIC_STATE_CULL_MODE); }

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
    dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = dynamic_states.size();
    dynamic_state_create_info.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = std::size(shader_stage_create_infos);
    pipeline_create_info.pStages = shader_stage_create_infos;
    pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;
    pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
    pipeline_create_info.pViewportState = &viewport_state_create_info;
    pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
    pipeline_create_info.pMultisampleState = &multisample_state_create_info;
    pipeline_create_info.pDepthStencilState = &depth_stencil_state_create_info;
    pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    pipeline_create_info.layout = context->pipeline_layout;
    pipeline_create_info.renderPass = render_pass;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(context->device, nullptr, 1, &pipeline_create_info, nullptr, &pipeline);
    assert(result == VK_SUCCESS);

    PipelineKey pipeline_key = {};
    pipeline_key.primitive_topology = primitive_topology;
    pipeline_key.polygon_mode = polygon_mode;
    pipeline_key.depth_test_enabled = depth_test_enabled;
    pipeline_key.depth_write_enabled = depth_write_enabled;
    pipeline_key.shaders_hash = hash_strings(vertex_shader_name, fragment_shader_name);
    context->pipelines[pipeline_key] = pipeline;

    vkDestroyShaderModule(context->device, vertex_shader_module, nullptr);
    vkDestroyShaderModule(context->device, fragment_shader_module, nullptr);
}

VkPipeline get_pipeline(VkContext *context, PipelineKey pipeline_key) {
    if (auto it = context->pipelines.find(pipeline_key); it != context->pipelines.end()) {
        return it->second;
    }
    assert(false);
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

void record_pipeline_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect_mask, VkPipelineStageFlags src_stage_flags, VkPipelineStageFlags dst_stage_flags, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask, VkImageLayout src_layout, VkImageLayout dst_layout) {
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.srcAccessMask = src_access_mask;
    image_barrier.dstAccessMask = dst_access_mask;
    image_barrier.oldLayout = src_layout;
    image_barrier.newLayout = dst_layout;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange.aspectMask = aspect_mask;
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

void set_viewport(VkCommandBuffer command_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    VkViewport viewport = {};
    viewport.x = (float) x;
    viewport.y = (float) y;
    viewport.width = (float) width;
    viewport.height = (float) height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
}

void set_scissor(VkCommandBuffer command_buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor = {};
    scissor.offset.x = x;
    scissor.offset.y = y;
    scissor.extent.width = width;
    scissor.extent.height = height;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void create_semaphore(VkContext *context, VkSemaphore *semaphore) {
    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.flags = VK_SEMAPHORE_TYPE_BINARY;
    VkResult result = vkCreateSemaphore(context->device, &semaphore_create_info, nullptr, semaphore);
    assert(result == VK_SUCCESS);
}

void cleanup_vulkan(VkContext *context) {
    for (const auto &[pipeline_key, pipeline]: context->pipelines) {
        vkDestroyPipeline(context->device, pipeline, nullptr);
    }
    context->pipelines.clear();
    vkDestroyPipelineLayout(context->device, context->pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(context->device, context->descriptor_set_layout, nullptr);
    vkDestroyRenderPass(context->device, context->picking_render_pass, nullptr);
    vkDestroyRenderPass(context->device, context->render_pass, nullptr);
    vkDestroyCommandPool(context->device, context->command_pool, nullptr);
    destroy_swap_chain(context);
    vkDestroyDevice(context->device, nullptr);
    SDL_Vulkan_DestroySurface(context->instance, context->surface, nullptr);
    auto vkDestroyDebugUtilsMessengerEXT = LOAD_INSTANCE_PROC_ADDR(context->instance, vkDestroyDebugUtilsMessengerEXT);
    vkDestroyDebugUtilsMessengerEXT(context->instance, context->debug_utils_messenger, nullptr);
    vkDestroyInstance(context->instance, nullptr);
}

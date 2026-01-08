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

    // 当级别为 error 时显式崩溃
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        assert(false && "Vulkan validation layer error");
    }

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

        // 查找专门的 transfer-only queue family（性能更好）
        uint32_t transfer_queue_family_index = UINT32_MAX;
        for (uint32_t i = 0; i < queue_family_count; ++i) {
            // 查找只支持 TRANSFER 的 queue family（不包含 GRAPHICS 和 COMPUTE）
            if ((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !(queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                transfer_queue_family_index = i;
                break;
            }
        }
        // 如果没有找到专门的 transfer queue，使用 graphics queue family（也支持 transfer）
        if (transfer_queue_family_index == UINT32_MAX) {
            transfer_queue_family_index = queue_family_index;
        }
        context->transfer_queue_family_index = transfer_queue_family_index;

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

    // 扩展顺序（按依赖关系）：基础扩展在前，依赖扩展在后
    // 1. 独立扩展（不依赖其他）
    required_device_extensions.push_back("VK_KHR_shader_non_semantic_info");
    // 2. 基础扩展（光线追踪功能链的基础）
    required_device_extensions.push_back("VK_KHR_buffer_device_address");
    // 3. 延迟主机操作（acceleration_structure 依赖它）
    required_device_extensions.push_back("VK_KHR_deferred_host_operations");
    // 4. 加速结构（依赖 buffer_device_address 和 deferred_host_operations）
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

    // 准备 queue 创建信息
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;

    // 主渲染 queue
    VkDeviceQueueCreateInfo graphics_queue_create_info = {};
    graphics_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics_queue_create_info.queueFamilyIndex = context->queue_family_index;

    // 如果 transfer queue family 与 graphics queue family 不同，需要创建额外的 queue
    if (context->transfer_queue_family_index != context->queue_family_index) {
        // 使用不同的 queue family，每个 queue family 请求 1 个 queue
        // graphics queue 优先级更高（1.0），transfer queue 优先级较低（0.5）
        float graphics_priority = 1.0f;
        float transfer_priority = 0.5f;

        graphics_queue_create_info.queueCount = 1;
        graphics_queue_create_info.pQueuePriorities = &graphics_priority;
        queue_create_infos.push_back(graphics_queue_create_info);

        VkDeviceQueueCreateInfo transfer_queue_create_info = {};
        transfer_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        transfer_queue_create_info.queueFamilyIndex = context->transfer_queue_family_index;
        transfer_queue_create_info.queueCount = 1;
        transfer_queue_create_info.pQueuePriorities = &transfer_priority;
        queue_create_infos.push_back(transfer_queue_create_info);
    } else {
        // 如果使用同一个 queue family，请求 2 个 queue（一个用于渲染，一个用于传输）
        // graphics queue (index 0) 优先级 1.0，transfer queue (index 1) 优先级 0.5
        float queue_priorities[2] = {1.0f, 0.5f};
        graphics_queue_create_info.queueCount = 2;
        graphics_queue_create_info.pQueuePriorities = queue_priorities;
        queue_create_infos.push_back(graphics_queue_create_info);
    }

    VkPhysicalDeviceFeatures device_features = {};
    device_features.fillModeNonSolid = VK_TRUE;
    device_features.vertexPipelineStoresAndAtomics = VK_TRUE;

    // 启用特性（pNext 链顺序：基础特性在前，依赖特性在后）

    // 3. Acceleration Structure 特性（依赖 buffer_device_address，放在 buffer_device_address 之后）
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {};
    acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    acceleration_structure_features.accelerationStructure = VK_TRUE;
    acceleration_structure_features.pNext = nullptr; // 链的末尾

    // 2. Buffer Device Address 特性（基础特性，acceleration_structure 依赖它，放在链的前面）
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features = {};
    buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    buffer_device_address_features.bufferDeviceAddress = VK_TRUE;
    buffer_device_address_features.pNext = &acceleration_structure_features;  // 链接到 acceleration structure

    // 1. Dynamic Rendering 特性（独立特性，不依赖其他）
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;
    dynamic_rendering_features.pNext = &buffer_device_address_features;

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &dynamic_rendering_features; // 通过 pNext 链传递特性
    device_create_info.queueCreateInfoCount = queue_create_infos.size();
    device_create_info.pQueueCreateInfos = queue_create_infos.data();
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

    // 获取 transfer queue
    if (context->transfer_queue_family_index != context->queue_family_index) {
        // 使用独立的 transfer queue family
        vkGetDeviceQueue(context->device, context->transfer_queue_family_index, 0, &context->transfer_queue);
    } else {
        // 使用同一个 queue family 的第二个 queue
        vkGetDeviceQueue(context->device, context->transfer_queue_family_index, 1, &context->transfer_queue);
    }

    // 加载动态函数（Vulkan 1.3 核心函数，但某些实现可能需要动态加载）
    context->vkCmdSetCullMode = LOAD_DEVICE_PROC_ADDR(context->device, vkCmdSetCullMode);
    assert(context->vkCmdSetCullMode && "vkCmdSetCullMode not available");
    context->vkCmdBeginRendering = LOAD_DEVICE_PROC_ADDR(context->device, vkCmdBeginRendering);
    assert(context->vkCmdBeginRendering && "vkCmdBeginRendering not available");
    context->vkCmdEndRendering = LOAD_DEVICE_PROC_ADDR(context->device, vkCmdEndRendering);
    assert(context->vkCmdEndRendering && "vkCmdEndRendering not available");
    context->vkGetBufferDeviceAddressKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkGetBufferDeviceAddressKHR);
    assert(context->vkGetBufferDeviceAddressKHR && "vkGetBufferDeviceAddressKHR not available");
    context->vkGetAccelerationStructureBuildSizesKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkGetAccelerationStructureBuildSizesKHR);
    assert(context->vkGetAccelerationStructureBuildSizesKHR && "vkGetAccelerationStructureBuildSizesKHR not available");
    context->vkCreateAccelerationStructureKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkCreateAccelerationStructureKHR);
    assert(context->vkCreateAccelerationStructureKHR && "vkCreateAccelerationStructureKHR not available");
    context->vkDestroyAccelerationStructureKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkDestroyAccelerationStructureKHR);
    assert(context->vkDestroyAccelerationStructureKHR && "vkDestroyAccelerationStructureKHR not available");
    context->vkGetAccelerationStructureDeviceAddressKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkGetAccelerationStructureDeviceAddressKHR);
    assert(context->vkGetAccelerationStructureDeviceAddressKHR && "vkGetAccelerationStructureDeviceAddressKHR not available");
    context->vkCmdBuildAccelerationStructuresKHR = LOAD_DEVICE_PROC_ADDR(context->device, vkCmdBuildAccelerationStructuresKHR);
    assert(context->vkCmdBuildAccelerationStructuresKHR && "vkCmdBuildAccelerationStructuresKHR not available");

    // 加载 Debug Utils 函数（用于资源命名）
    context->vkSetDebugUtilsObjectNameEXT = LOAD_DEVICE_PROC_ADDR(context->device, vkSetDebugUtilsObjectNameEXT);
    assert(context->vkSetDebugUtilsObjectNameEXT && "vkSetDebugUtilsObjectNameEXT not available");
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

    // 检查 surface 是否支持 TRANSFER_DST_BIT（用于 blit 到 swap chain image）
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    assert(surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT && "surface does not support TRANSFER_DST_BIT");
    image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkSwapchainCreateInfoKHR swap_chain_create_info = {};
    swap_chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_chain_create_info.surface = context->surface;
    swap_chain_create_info.minImageCount = min_image_count;
    swap_chain_create_info.imageFormat = surface_format;
    swap_chain_create_info.imageColorSpace = surface_color_space;
    swap_chain_create_info.imageExtent = {width, height};
    swap_chain_create_info.imageArrayLayers = 1;
    swap_chain_create_info.imageUsage = image_usage;
    swap_chain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swap_chain_create_info.compositeAlpha = composite_alpha;
    swap_chain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swap_chain_create_info.clipped = VK_TRUE;
    swap_chain_create_info.oldSwapchain = VK_NULL_HANDLE;

    result = vkCreateSwapchainKHR(context->device, &swap_chain_create_info, nullptr, &context->swap_chain);
    assert(result == VK_SUCCESS);

    context->surface_format = surface_format;
    context->surface_color_space = surface_color_space;

    // get swap chain images
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(context->device, context->swap_chain, &image_count, nullptr);
    assert(image_count > 0);

    context->swap_chain_images.resize(image_count);
    vkGetSwapchainImagesKHR(context->device, context->swap_chain, &image_count, context->swap_chain_images.data());

    // create swap chain image views
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

        char name[256];
        snprintf(name, sizeof(name), "SwapChainImageView[%u]", i);
        set_debug_object_name(context, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t) context->swap_chain_image_views[i], name);
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

    // 创建用于后台线程传输操作的 command pool
    // 使用 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 标志，因为传输操作通常是短暂的
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool_create_info.queueFamilyIndex = context->transfer_queue_family_index;
    result = vkCreateCommandPool(context->device, &command_pool_create_info, nullptr, &context->transfer_command_pool);
    assert(result == VK_SUCCESS);
}

void create_pipelines(VkContext *context) {
    // shader 名称（不包含路径和扩展名），函数会自动添加 shaders/ 前缀和 .vert.spv/.frag.spv 后缀
    create_pipeline(context, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, true, true, context->surface_format, context->depth_image_format, "triangle", "triangle");
    create_pipeline(context, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, true, false, VK_FORMAT_UNDEFINED, context->depth_image_format, "picking", "picking");
}

void allocate_command_buffers(VkContext *context, VkCommandPool command_pool, uint32_t count, VkCommandBuffer *command_buffers) {
    VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.commandPool = command_pool;
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

void allocate_memory(VkContext *context, const VkMemoryRequirements &memory_requirements, VkMemoryPropertyFlags memory_property_flags, VkMemoryAllocateFlags memory_allocate_flags, VkDeviceMemory *memory) {
    uint32_t memory_type_index = UINT32_MAX;
    get_memory_type_index(context, memory_requirements, memory_property_flags, &memory_type_index);

    VkMemoryAllocateFlagsInfo memory_allocate_flags_info = {};
    memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memory_allocate_flags_info.flags = memory_allocate_flags;

    VkMemoryAllocateInfo memory_allocate_info = {};
    memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_allocate_info.pNext = memory_allocate_flags != 0 ? &memory_allocate_flags_info : nullptr;
    memory_allocate_info.allocationSize = memory_requirements.size;
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
    VK_CHECK(result);
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
    VK_CHECK(result);

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(context->device, *image, &memory_requirements);

    allocate_memory(context, memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, image_memory);
    vkBindImageMemory(context->device, *image, *image_memory, 0);
}

void destroy_image(VkContext *context, VkImage image, VkDeviceMemory image_memory) {
    vkDestroyImage(context->device, image, nullptr);
    vkFreeMemory(context->device, image_memory, nullptr);
}

void create_image_view(VkContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask, VkImageView *image_view, const char *name) {
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
    VK_CHECK(result);

    set_debug_object_name(context, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t) *image_view, name);
}

void create_buffer(VkContext *context, VkDeviceSize size, VkBufferUsageFlags buffer_usage_flags, VkMemoryPropertyFlags memory_property_flags, VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
    VkBufferCreateInfo buffer_create_info = {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = size;
    buffer_create_info.usage = buffer_usage_flags;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_create_info.queueFamilyIndexCount = 1;
    buffer_create_info.pQueueFamilyIndices = &context->queue_family_index;
    VkResult result = vkCreateBuffer(context->device, &buffer_create_info, nullptr, buffer);
    VK_CHECK(result);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(context->device, *buffer, &memory_requirements);

    VkMemoryAllocateFlags memory_allocate_flags = buffer_usage_flags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
    allocate_memory(context, memory_requirements, memory_property_flags, memory_allocate_flags, buffer_memory);

    vkBindBufferMemory(context->device, *buffer, *buffer_memory, 0);
}

void destroy_buffer(VkContext *context, VkBuffer buffer, VkDeviceMemory buffer_memory) {
    vkDestroyBuffer(context->device, buffer, nullptr);
    vkFreeMemory(context->device, buffer_memory, nullptr);
}

void set_debug_object_name(VkContext *context, VkObjectType object_type, uint64_t object_handle, const char *name) {
    VkDebugUtilsObjectNameInfoEXT name_info = {};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    name_info.objectType = object_type;
    name_info.objectHandle = object_handle;
    name_info.pObjectName = name;
    VkResult result = context->vkSetDebugUtilsObjectNameEXT(context->device, &name_info);
    VK_CHECK(result);
}

void create_descriptor_set_layout(VkContext *context) {
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}, // camera
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // picking storage buffer
        {2, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // acceleration structure
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // directional light
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
                     bool depth_test_enabled, bool depth_write_enabled, VkFormat color_image_format, VkFormat depth_image_format,
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
    {
        VkVertexInputAttributeDescription vertex_input_attribute_description = {};
        vertex_input_attribute_description.binding = 0;
        vertex_input_attribute_description.location = 1;
        vertex_input_attribute_description.format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_input_attribute_description.offset = offsetof(Vertex, normal);
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
    if (color_image_format != VK_FORMAT_UNDEFINED) {
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
    // Picking shader 需要使用 LESS_OR_EQUAL，因为主渲染 pass 已经写入了最前面物体的深度值
    // 如果使用 LESS，深度值等于深度缓冲区值的片段（最前面的物体）无法通过深度测试
    // 当启用 early_fragment_tests 时，这会导致 fragment shader 不执行，entity_id 始终为 0
    depth_stencil_state_create_info.depthCompareOp = (strcmp(fragment_shader_name, "picking") == 0) ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

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

    VkPipelineRenderingCreateInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = color_image_format != VK_FORMAT_UNDEFINED ? 1 : 0;
    if (color_image_format != VK_FORMAT_UNDEFINED) {
        rendering_info.pColorAttachmentFormats = &color_image_format;
    }
    if (depth_test_enabled) {
        rendering_info.depthAttachmentFormat = depth_image_format;
    }

    VkGraphicsPipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.pNext = &rendering_info;  // Dynamic Rendering 信息
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
    pipeline_create_info.renderPass = VK_NULL_HANDLE;  // Dynamic Rendering 必须设置为 NULL

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

void blit_image(VkCommandBuffer command_buffer, VkImage src_image, VkImage dst_image, uint32_t src_width, uint32_t src_height, uint32_t dst_width, uint32_t dst_height) {
    // 计算缩放比例，保持宽高比
    float scale_x = (float) dst_width / (float) src_width;
    float scale_y = (float) dst_height / (float) src_height;
    float scale = std::min(scale_x, scale_y); // 使用较小的比例，保持宽高比

    // 计算目标区域（居中）
    uint32_t scaled_width = (uint32_t) (src_width * scale);
    uint32_t scaled_height = (uint32_t) (src_height * scale);
    int32_t offset_x = (dst_width - scaled_width) / 2;
    int32_t offset_y = (dst_height - scaled_height) / 2;

    VkImageBlit blit_region = {};
    blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.srcSubresource.mipLevel = 0;
    blit_region.srcSubresource.baseArrayLayer = 0;
    blit_region.srcSubresource.layerCount = 1;
    blit_region.srcOffsets[0] = {0, 0, 0};
    blit_region.srcOffsets[1] = {(int32_t) src_width, (int32_t) src_height, 1};

    blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_region.dstSubresource.mipLevel = 0;
    blit_region.dstSubresource.baseArrayLayer = 0;
    blit_region.dstSubresource.layerCount = 1;
    blit_region.dstOffsets[0] = {offset_x, offset_y, 0};
    blit_region.dstOffsets[1] = {offset_x + (int32_t) scaled_width, offset_y + (int32_t) scaled_height, 1};

    vkCmdBlitImage(command_buffer, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_NEAREST);
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

void copy_buffer(VkCommandBuffer command_buffer, VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) {
    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);
}

void create_semaphore(VkContext *context, VkSemaphore *semaphore) {
    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_create_info.flags = VK_SEMAPHORE_TYPE_BINARY;
    VkResult result = vkCreateSemaphore(context->device, &semaphore_create_info, nullptr, semaphore);
    assert(result == VK_SUCCESS);
}

void create_fence(VkContext *context, bool signaled, VkFence *fence) {
    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    VkResult result = vkCreateFence(context->device, &fence_create_info, nullptr, fence);
    assert(result == VK_SUCCESS);
}

void begin_rendering(VkContext *context, VkCommandBuffer command_buffer, VkImageView color_image_view, VkClearColorValue *clear_color_value, VkImageView depth_image_view, VkClearDepthStencilValue *clear_depth_stencil_value, uint32_t width, uint32_t height) {
    VkRenderingAttachmentInfo color_attachment = {};
    if (color_image_view) {
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = color_image_view;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = clear_color_value ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clear_color_value) {
            color_attachment.clearValue.color = *clear_color_value;
        }
    }

    VkRenderingAttachmentInfo depth_attachment = {};
    if (depth_image_view) {
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_image_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = clear_depth_stencil_value ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clear_depth_stencil_value) {
            depth_attachment.clearValue.depthStencil = *clear_depth_stencil_value;
        }
    }

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea = {0, 0, width, height};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = color_image_view ? 1 : 0;
    rendering_info.pColorAttachments = color_image_view ? &color_attachment : nullptr;
    rendering_info.pDepthAttachment = depth_image_view ? &depth_attachment : nullptr;
    context->vkCmdBeginRendering(command_buffer, &rendering_info);
}

void end_rendering(VkContext *context, VkCommandBuffer command_buffer) {
    context->vkCmdEndRendering(command_buffer);
}

void execute_one_time_submit(VkContext *context, VkCommandPool command_pool, VkQueue queue, std::function<void(VkCommandBuffer)> &&func) {
    VkCommandBuffer command_buffer;
    allocate_command_buffers(context, command_pool, 1, &command_buffer);

    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    VK_CHECK(result);

    func(command_buffer);

    result = vkEndCommandBuffer(command_buffer);
    VK_CHECK(result);

    VkFence fence;
    create_fence(context, false, &fence);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    result = vkQueueSubmit(queue, 1, &submit_info, fence);
    VK_CHECK(result);

    result = vkWaitForFences(context->device, 1, &fence, VK_TRUE, UINT64_MAX);
    VK_CHECK(result);

    vkDestroyFence(context->device, fence, nullptr);
    vkFreeCommandBuffers(context->device, command_pool, 1, &command_buffer);
}

VkDeviceAddress get_buffer_device_address(VkContext *context, VkBuffer buffer) {
    VkBufferDeviceAddressInfo address_info = {};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = buffer;
    return context->vkGetBufferDeviceAddressKHR(context->device, &address_info);
}

VkDeviceAddress get_acceleration_structure_device_address(VkContext *context, VkAccelerationStructureKHR acceleration_structure) {
    VkAccelerationStructureDeviceAddressInfoKHR address_info = {};
    address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address_info.accelerationStructure = acceleration_structure;
    return context->vkGetAccelerationStructureDeviceAddressKHR(context->device, &address_info);
}

void cleanup_vulkan(VkContext *context) {
    for (const auto &[pipeline_key, pipeline]: context->pipelines) {
        vkDestroyPipeline(context->device, pipeline, nullptr);
    }
    context->pipelines.clear();
    vkDestroyPipelineLayout(context->device, context->pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(context->device, context->descriptor_set_layout, nullptr);
    vkDestroyCommandPool(context->device, context->transfer_command_pool, nullptr);
    vkDestroyCommandPool(context->device, context->command_pool, nullptr);
    destroy_swap_chain(context);
    vkDestroyDevice(context->device, nullptr);
    SDL_Vulkan_DestroySurface(context->instance, context->surface, nullptr);
    auto vkDestroyDebugUtilsMessengerEXT = LOAD_INSTANCE_PROC_ADDR(context->instance, vkDestroyDebugUtilsMessengerEXT);
    vkDestroyDebugUtilsMessengerEXT(context->instance, context->debug_utils_messenger, nullptr);
    vkDestroyInstance(context->instance, nullptr);
}

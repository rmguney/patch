#include "vk_context.h"
#include "voxel_push_constants.h"
#include "engine/platform/window.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

/*
 * Validation layers are a debug-build feature. Debug builds enable
 * VK_LAYER_KHRONOS_validation (with synchronization validation) and
 * VK_EXT_debug_utils when the Vulkan SDK provides them; set
 * PATCH_VK_VALIDATION=0 to opt out, PATCH_VK_SYNC_VALIDATION=0 to keep
 * core validation but skip the (slower) synchronization checks.
 */
#if !defined(NDEBUG)
#define PATCH_VK_VALIDATION_AVAILABLE 1
#else
#define PATCH_VK_VALIDATION_AVAILABLE 0
#endif

namespace patch
{

    static const char *K_VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
    static const char *PIPELINE_CACHE_PATH = "pipeline_cache.bin";

    static std::atomic<uint32_t> g_vk_validation_errors{0};
    static std::atomic<uint32_t> g_vk_validation_warnings{0};

    static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT *data,
        void *user_data)
    {
        (void)user_data;
        const char *sev = "info";
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            sev = "error";
            g_vk_validation_errors.fetch_add(1, std::memory_order_relaxed);
        }
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            sev = "warning";
            g_vk_validation_warnings.fetch_add(1, std::memory_order_relaxed);
        }
        const char *kind = (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)    ? "validation"
                           : (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "performance"
                                                                                        : "general";
        fprintf(stderr, "[vulkan %s/%s] %s\n", kind, sev, data && data->pMessage ? data->pMessage : "(null)");
        return VK_FALSE;
    }

    static void fill_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *info)
    {
        *info = {};
        info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info->pfnUserCallback = vk_debug_callback;
    }

    static bool env_flag(const char *name, bool default_value)
    {
        const char *v = getenv(name);
        if (!v || !v[0])
            return default_value;
        return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
    }

    static bool has_extension(const std::vector<VkExtensionProperties> &exts, const char *name)
    {
        for (const auto &e : exts)
        {
            if (strcmp(e.extensionName, name) == 0)
                return true;
        }
        return false;
    }

    const char *vk_result_name(VkResult r)
    {
        switch (r)
        {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        default:
            return "VkResult";
        }
    }

    bool VulkanContext::create(Window &window, const char **error)
    {
        if (!create_instance())
        {
            *error = "Failed to create Vulkan instance";
            return false;
        }

        surface_ = window.create_surface(instance_);
        if (surface_ == VK_NULL_HANDLE)
        {
            *error = "Failed to create Vulkan surface";
            return false;
        }

        if (!select_physical_device(error))
        {
            if (!*error)
                *error = "Failed to select physical device";
            return false;
        }

        if (!find_queue_families())
        {
            *error = "Failed to find queue families";
            return false;
        }

        if (!create_logical_device(error))
        {
            if (!*error)
                *error = "Failed to create logical device";
            return false;
        }

        if (!allocator_.init(instance_, physical_device_, device_, VK_API_VERSION_1_3))
        {
            *error = "Failed to initialize GPU memory allocator";
            return false;
        }

        create_pipeline_cache();

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_family_;
        if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS)
        {
            *error = "Failed to create command pool";
            return false;
        }
        return true;
    }

    /* Every subsystem has released its objects; the device is idle */
    void VulkanContext::destroy()
    {
        if (device_ != VK_NULL_HANDLE)
        {
            save_pipeline_cache();
            if (pipeline_cache_)
            {
                vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
                pipeline_cache_ = VK_NULL_HANDLE;
            }

            /* Also frees every command buffer allocated from it */
            if (command_pool_)
            {
                vkDestroyCommandPool(device_, command_pool_, nullptr);
                command_pool_ = VK_NULL_HANDLE;
            }

            allocator_.destroy();
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (surface_ && instance_)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        destroy_debug_messenger();
        if (instance_)
        {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
            print_validation_summary();
        }
    }

    bool VulkanContext::create_instance()
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Patch";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "PatchEngine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

        std::vector<const char *> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        std::vector<const char *> layers;

        uint32_t ext_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, exts.data());

        /* What the device's VK_EXT_swapchain_maintenance1 (present fences) builds on */
        surface_maintenance1_ = has_extension(exts, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
                                has_extension(exts, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (surface_maintenance1_)
        {
            extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }

        validation_enabled_ = false;
        sync_validation_enabled_ = false;
        bool has_debug_utils = false;
        bool has_validation_features = false;

        bool want_validation = false;
        if constexpr (PATCH_VK_VALIDATION_AVAILABLE != 0)
            want_validation = env_flag("PATCH_VK_VALIDATION", true);
        if (want_validation)
        {
            uint32_t layer_count = 0;
            vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
            std::vector<VkLayerProperties> available_layers(layer_count);
            vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
            for (const auto &l : available_layers)
            {
                if (strcmp(l.layerName, K_VALIDATION_LAYER) == 0)
                    validation_enabled_ = true;
            }

            if (validation_enabled_)
            {
                layers.push_back(K_VALIDATION_LAYER);

                uint32_t layer_ext_count = 0;
                vkEnumerateInstanceExtensionProperties(K_VALIDATION_LAYER, &layer_ext_count, nullptr);
                std::vector<VkExtensionProperties> layer_exts(layer_ext_count);
                vkEnumerateInstanceExtensionProperties(K_VALIDATION_LAYER, &layer_ext_count, layer_exts.data());

                has_debug_utils = has_extension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) ||
                                  has_extension(layer_exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                has_validation_features = has_extension(layer_exts, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

                if (has_debug_utils)
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                if (has_validation_features && env_flag("PATCH_VK_SYNC_VALIDATION", true))
                {
                    extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
                    sync_validation_enabled_ = true;
                }
            }
            else
            {
                printf("Vulkan validation layer not found (install the Vulkan SDK to enable it)\n");
            }
        }

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        create_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

        /* Chain: instance -> debug messenger (covers vkCreate/DestroyInstance) -> validation features */
        VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
        VkValidationFeatureEnableEXT sync_feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation_features{};
        const void **tail = &create_info.pNext;
        if (has_debug_utils)
        {
            fill_messenger_info(&messenger_info);
            *tail = &messenger_info;
            tail = &messenger_info.pNext;
        }
        if (sync_validation_enabled_)
        {
            validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            validation_features.enabledValidationFeatureCount = 1;
            validation_features.pEnabledValidationFeatures = &sync_feature;
            *tail = &validation_features;
        }

        VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkCreateInstance failed: %d\n", result);
            return false;
        }

        if (validation_enabled_)
        {
            printf("Vulkan validation: enabled (%s%s)\n",
                   has_debug_utils ? "debug utils messenger -> stderr" : "layer default output",
                   sync_validation_enabled_ ? ", synchronization validation" : "");
            if (has_debug_utils)
                create_debug_messenger();
        }
        return true;
    }

    bool VulkanContext::create_debug_messenger()
    {
        auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (!create_fn)
            return false;

        VkDebugUtilsMessengerCreateInfoEXT info{};
        fill_messenger_info(&info);
        return create_fn(instance_, &info, nullptr, &debug_messenger_) == VK_SUCCESS;
    }

    void VulkanContext::destroy_debug_messenger()
    {
        if (debug_messenger_ == VK_NULL_HANDLE || instance_ == VK_NULL_HANDLE)
            return;
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn)
            destroy_fn(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }

    void VulkanContext::print_validation_summary() const
    {
        if (!validation_enabled_)
            return;
        printf("Vulkan validation summary: %u error(s), %u warning(s)\n",
               g_vk_validation_errors.load(), g_vk_validation_warnings.load());
    }

    bool VulkanContext::select_physical_device(const char **error)
    {
        printf("Requesting Vulkan 1.3\n");

        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        if (device_count == 0)
        {
            return false;
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

        VkPhysicalDevice discrete_gpu = VK_NULL_HANDLE;
        VkPhysicalDevice integrated_gpu = VK_NULL_HANDLE;

        for (uint32_t i = 0; i < device_count; i++)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);

            uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
            uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
            uint32_t patch = VK_API_VERSION_PATCH(props.apiVersion);

            const char *type_str = "Unknown";
            switch (props.deviceType)
            {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                type_str = "Discrete";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                type_str = "Integrated";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                type_str = "Virtual";
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                type_str = "CPU";
                break;
            default:
                break;
            }

            printf("GPU %u: %s (%s, Vulkan %u.%u.%u)\n", i, props.deviceName, type_str, major, minor, patch);

            if (props.apiVersion >= VK_API_VERSION_1_3)
            {
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && discrete_gpu == VK_NULL_HANDLE)
                {
                    discrete_gpu = devices[i];
                }
                else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && integrated_gpu == VK_NULL_HANDLE)
                {
                    integrated_gpu = devices[i];
                }
            }
        }

        physical_device_ = (discrete_gpu != VK_NULL_HANDLE) ? discrete_gpu : integrated_gpu;
        if (physical_device_ == VK_NULL_HANDLE)
        {
            printf("No GPU with Vulkan 1.3+ support found\n");
            return false;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_device_, &props);
        strncpy(gpu_name_, props.deviceName, sizeof(gpu_name_) - 1);
        gpu_name_[sizeof(gpu_name_) - 1] = '\0';
        printf("  Selected: %s\n", gpu_name_);
        if (discrete_gpu == VK_NULL_HANDLE)
            printf("  WARNING: Using integrated GPU. No discrete GPU found.\n");

        /* Every compute/lighting pipeline pushes a VoxelPushConstants block */
        if (props.limits.maxPushConstantsSize < sizeof(VoxelPushConstants))
        {
            fprintf(stderr, "GPU '%s' supports only %u bytes of push constants; Patch needs %zu\n",
                    gpu_name_, props.limits.maxPushConstantsSize, sizeof(VoxelPushConstants));
            *error = "GPU push constant limit too small";
            return false;
        }

        timestamp_period_ns_ = props.limits.timestampPeriod;
        return true;
    }

    bool VulkanContext::find_queue_families()
    {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_families.data());

        graphics_family_ = UINT32_MAX;
        present_family_ = UINT32_MAX;

        for (uint32_t i = 0; i < queue_family_count; i++)
        {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphics_family_ = i;
            }

            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
            if (present_support)
            {
                present_family_ = i;
            }

            if (graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX)
            {
                break;
            }
        }

        if (graphics_family_ == UINT32_MAX || present_family_ == UINT32_MAX)
            return false;
        /* timestampComputeAndGraphics only promises timestamps when it is set: the queue's own count decides */
        timestamp_valid_bits_ = queue_families[graphics_family_].timestampValidBits;
        return true;
    }

    bool VulkanContext::create_logical_device(const char **error)
    {
        float queue_priority = 1.0f;

        VkDeviceQueueCreateInfo queue_infos[2]{};
        uint32_t queue_count = 1;

        queue_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[0].queueFamilyIndex = graphics_family_;
        queue_infos[0].queueCount = 1;
        queue_infos[0].pQueuePriorities = &queue_priority;

        if (graphics_family_ != present_family_)
        {
            queue_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_infos[1].queueFamilyIndex = present_family_;
            queue_infos[1].queueCount = 1;
            queue_infos[1].pQueuePriorities = &queue_priority;
            queue_count = 2;
        }

        std::vector<const char *> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, exts.data());
        const bool has_swapchain_maintenance1 =
            surface_maintenance1_ && has_extension(exts, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

        /*
         * Vulkan 1.3 core features the renderer is built on. Mandatory in 1.3, but still feature
         * bits: render passes are begun with vkCmdBeginRendering, barriers are synchronization2,
         * and glslc targeting 1.3 lowers `discard` to OpDemoteToHelperInvocation.
         */
        VkPhysicalDeviceVulkan13Features supported13{};
        supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT supported_maintenance1{};
        supported_maintenance1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported.pNext = &supported13;
        if (has_swapchain_maintenance1)
            supported13.pNext = &supported_maintenance1;
        vkGetPhysicalDeviceFeatures2(physical_device_, &supported);

        const struct
        {
            VkBool32 supported;
            const char *name;
        } required[] = {
            {supported13.dynamicRendering, "dynamicRendering"},
            {supported13.synchronization2, "synchronization2"},
            {supported13.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation"}};
        for (const auto &feature : required)
        {
            if (!feature.supported)
            {
                fprintf(stderr, "GPU lacks the Vulkan 1.3 %s feature\n", feature.name);
                *error = "GPU lacks a required Vulkan 1.3 feature";
                return false;
            }
        }

        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = VK_TRUE;
        enabled13.synchronization2 = VK_TRUE;
        enabled13.shaderDemoteToHelperInvocation = VK_TRUE;
        VkPhysicalDeviceFeatures2 enabled{};
        enabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        enabled.pNext = &enabled13;

        /* Present fences tell when a present's semaphore wait is done (Swapchain retires the
         * semaphores of a rebuilt swapchain by them); optional */
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabled_maintenance1{};
        enabled_maintenance1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        swapchain_maintenance1_ = has_swapchain_maintenance1 && supported_maintenance1.swapchainMaintenance1;
        if (swapchain_maintenance1_)
        {
            enabled_maintenance1.swapchainMaintenance1 = VK_TRUE;
            enabled13.pNext = &enabled_maintenance1;
            device_extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        }

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pNext = &enabled;
        create_info.queueCreateInfoCount = queue_count;
        create_info.pQueueCreateInfos = queue_infos;
        create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        create_info.ppEnabledExtensionNames = device_extensions.data();

        if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS)
        {
            return false;
        }

        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
        return true;
    }

    /* A missing or rejected cache only costs pipeline compile time */
    void VulkanContext::create_pipeline_cache()
    {
        VkPipelineCacheCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        FILE *cache_file = fopen(PIPELINE_CACHE_PATH, "rb");
        std::vector<uint8_t> cache_data;

        if (cache_file)
        {
            fseek(cache_file, 0, SEEK_END);
            long size = ftell(cache_file);
            fseek(cache_file, 0, SEEK_SET);

            if (size > 0)
            {
                cache_data.resize(static_cast<size_t>(size));
                size_t read = fread(cache_data.data(), 1, cache_data.size(), cache_file);
                if (read == cache_data.size())
                {
                    create_info.initialDataSize = cache_data.size();
                    create_info.pInitialData = cache_data.data();
                }
            }
            fclose(cache_file);
        }

        if (vkCreatePipelineCache(device_, &create_info, nullptr, &pipeline_cache_) != VK_SUCCESS)
        {
            fprintf(stderr, "Warning: Failed to create pipeline cache\n");
            pipeline_cache_ = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::save_pipeline_cache()
    {
        if (pipeline_cache_ == VK_NULL_HANDLE)
            return;

        size_t cache_size = 0;
        VkResult result = vkGetPipelineCacheData(device_, pipeline_cache_, &cache_size, nullptr);
        if (result != VK_SUCCESS || cache_size == 0)
            return;

        std::vector<uint8_t> cache_data(cache_size);
        result = vkGetPipelineCacheData(device_, pipeline_cache_, &cache_size, cache_data.data());
        if (result != VK_SUCCESS)
            return;

        FILE *cache_file = fopen(PIPELINE_CACHE_PATH, "wb");
        if (cache_file)
        {
            fwrite(cache_data.data(), 1, cache_size, cache_file);
            fclose(cache_file);
        }
    }

    bool VulkanContext::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties, VulkanBuffer *buffer, bool own_memory)
    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        /* Host-visible buffers are written sequentially by the CPU and stay mapped */
        VmaAllocationCreateFlags vma_flags = 0;
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (own_memory)
            vma_flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        buffer->buffer = allocator_.create_buffer(buffer_info, vma_flags, properties,
                                                  &buffer->allocation, &buffer->mapped);
        return buffer->buffer != VK_NULL_HANDLE;
    }

    void VulkanContext::destroy_buffer(VulkanBuffer *buffer)
    {
        allocator_.destroy_buffer(buffer->buffer, buffer->allocation);
        *buffer = {};
    }

    bool VulkanContext::create_image(const ImageDesc &desc, GpuImage *image)
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = desc.type;
        image_info.extent = desc.extent;
        image_info.mipLevels = desc.mip_levels;
        image_info.arrayLayers = 1;
        image_info.format = desc.format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = desc.usage;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        image->image = allocator_.create_image(image_info, VMA_MEMORY_USAGE_AUTO, &image->allocation);
        if (image->image == VK_NULL_HANDLE)
            return false;
        image->range = {desc.aspect, 0, desc.mip_levels, 0, 1};
        image->state = {};

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image->image;
        view_info.viewType = desc.type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = desc.format;
        view_info.subresourceRange = image->range;
        return vkCreateImageView(device_, &view_info, nullptr, &image->view) == VK_SUCCESS;
    }

    void VulkanContext::destroy_image(GpuImage *image)
    {
        if (image->view)
            vkDestroyImageView(device_, image->view, nullptr);
        allocator_.destroy_image(image->image, image->allocation);
        *image = {};
    }

    bool VulkanContext::allocate_command_buffers(uint32_t count, VkCommandBuffer *out)
    {
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = count;
        return vkAllocateCommandBuffers(device_, &alloc_info, out) == VK_SUCCESS;
    }

    VkCommandBuffer VulkanContext::begin_immediate_commands()
    {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!allocate_command_buffers(1, &cmd))
            return VK_NULL_HANDLE;

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);
        return cmd;
    }

    bool VulkanContext::submit_immediate_commands(VkCommandBuffer cmd)
    {
        VkResult result = vkEndCommandBuffer(cmd);
        if (result == VK_SUCCESS)
        {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            result = vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
        }
        if (result == VK_SUCCESS)
            result = vkQueueWaitIdle(graphics_queue_);
        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "Immediate GPU commands failed: %s (%d)\n", vk_result_name(result), result);
            return false;
        }
        return true;
    }

}

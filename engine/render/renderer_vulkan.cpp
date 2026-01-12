#include "renderer.h"
#include "shaders_embedded.h" // this might have issues due to shaders_embedded_prebuilt.h creation
#include <cstring>
#include <cstdio>
#include <vector>

namespace patch
{

    bool Renderer::create_instance()
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Patch";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "PatchEngine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        const char *extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME};

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = 2;
        create_info.ppEnabledExtensionNames = extensions;

        return vkCreateInstance(&create_info, nullptr, &instance_) == VK_SUCCESS;
    }

    bool Renderer::select_physical_device()
    {
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

            if (props.apiVersion >= VK_API_VERSION_1_2)
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
            printf("No GPU with Vulkan 1.2+ support found\n");
            return false;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_device_, &props);
        strncpy(gpu_name_, props.deviceName, sizeof(gpu_name_) - 1);
        gpu_name_[sizeof(gpu_name_) - 1] = '\0';
        printf("  Selected: %s\n", gpu_name_);
        printf("  Max push constants: %u bytes\n", props.limits.maxPushConstantsSize);

        if (discrete_gpu == VK_NULL_HANDLE && integrated_gpu != VK_NULL_HANDLE)
        {
            printf("  WARNING: Using integrated GPU. No discrete GPU found.\n");
        }

        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> extensions(ext_count);
        vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, extensions.data());

        bool has_ray_query = false;
        bool has_accel_struct = false;
        for (uint32_t e = 0; e < ext_count; e++)
        {
            if (strcmp(extensions[e].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
                has_ray_query = true;
            if (strcmp(extensions[e].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
                has_accel_struct = true;
        }

        rt_supported_ = has_ray_query && has_accel_struct;
        printf("  RT ray query: %s\n", has_ray_query ? "supported" : "not supported");
        printf("  RT acceleration structure: %s\n", has_accel_struct ? "supported" : "not supported");

        VkPhysicalDeviceProperties device_props;
        vkGetPhysicalDeviceProperties(physical_device_, &device_props);
        timestamp_period_ns_ = device_props.limits.timestampPeriod;
        printf("  GPU timestamp period: %.3f ns\n", timestamp_period_ns_);

        return true;
    }

    bool Renderer::find_queue_families()
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

        return graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX;
    }

    bool Renderer::create_logical_device()
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

        const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = queue_count;
        create_info.pQueueCreateInfos = queue_infos;
        create_info.enabledExtensionCount = 1;
        create_info.ppEnabledExtensionNames = device_extensions;

        if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS)
        {
            return false;
        }

        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
        return true;
    }

    bool Renderer::create_swapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

        VkSurfaceFormatKHR surface_format = formats[0];
        for (auto &f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surface_format = f;
                break;
            }
        }

        /* Query available present modes */
        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());

        /* Check available modes */
        bool has_mailbox = false, has_immediate = false;
        for (auto mode : present_modes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) has_mailbox = true;
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) has_immediate = true;
        }

        /* Select based on preference (default: Mailbox for uncapped FPS without tearing) */
        VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        if (present_mode_ == PresentMode::Mailbox && has_mailbox)
        {
            selected_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        else if (present_mode_ == PresentMode::Immediate && has_immediate)
        {
            selected_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        else if (present_mode_ != PresentMode::VSync && has_mailbox)
        {
            selected_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }

        printf("  Present mode: %s\n",
               selected_present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX (uncapped)" :
               selected_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE (uncapped)" :
               "FIFO (vsync)");

        swapchain_format_ = surface_format.format;
        swapchain_extent_ = capabilities.currentExtent;

        if (swapchain_extent_.width == UINT32_MAX)
        {
            swapchain_extent_.width = static_cast<uint32_t>(window_.width());
            swapchain_extent_.height = static_cast<uint32_t>(window_.height());
        }

        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        {
            image_count = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = swapchain_extent_;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queue_family_indices[] = {graphics_family_, present_family_};
        if (graphics_family_ != present_family_)
        {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queue_family_indices;
        }
        else
        {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        create_info.preTransform = capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        create_info.presentMode = selected_present_mode;
        create_info.clipped = VK_TRUE;

        VkResult result = vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_);
        if (result != VK_SUCCESS)
        {
            fprintf(stderr, "vkCreateSwapchainKHR failed: %d (extent: %ux%u)\n",
                    result, swapchain_extent_.width, swapchain_extent_.height);
            return false;
        }

        uint32_t retrieved_count = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &retrieved_count, nullptr);
        swapchain_images_.resize(retrieved_count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &retrieved_count, swapchain_images_.data());

        swapchain_image_views_.resize(retrieved_count);
        for (uint32_t i = 0; i < retrieved_count; i++)
        {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = swapchain_images_[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format_;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]);
        }

        return true;
    }

    bool Renderer::create_render_pass()
    {
        VkAttachmentDescription attachments[2]{};

        attachments[0].format = swapchain_format_;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 2;
        render_pass_info.pAttachments = attachments;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        return vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_) == VK_SUCCESS;
    }

    bool Renderer::create_depth_resources()
    {
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = swapchain_extent_.width;
        image_info.extent.height = swapchain_extent_.height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = depth_format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        vkCreateImage(device_, &image_info, nullptr, &depth_image_);

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device_, depth_image_, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device_, &alloc_info, nullptr, &depth_image_memory_);
        vkBindImageMemory(device_, depth_image_, depth_image_memory_, 0);

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = depth_image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = depth_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        vkCreateImageView(device_, &view_info, nullptr, &depth_image_view_);

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_info.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(device_, &sampler_info, nullptr, &depth_sampler_);

        return true;
    }

    bool Renderer::create_pipelines()
    {
        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 0;
        pipeline_layout_info.pSetLayouts = nullptr;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant_range;

        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        {
            return false;
        }

        return create_pipeline(
            shaders::k_shader_ui_vert_spv, shaders::k_shader_ui_vert_spv_size,
            shaders::k_shader_ui_frag_spv, shaders::k_shader_ui_frag_spv_size,
            true,
            false,
            VK_CULL_MODE_NONE,
            &ui_pipeline_);
    }

    bool Renderer::create_pipeline(const uint32_t *vert_code, size_t vert_size,
                                   const uint32_t *frag_code, size_t frag_size,
                                   bool enable_blend, bool depth_write,
                                   VkCullModeFlags cull_mode, VkPipeline *out_pipeline)
    {
        VkShaderModuleCreateInfo vert_module_info{};
        vert_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_module_info.codeSize = vert_size;
        vert_module_info.pCode = vert_code;

        VkShaderModuleCreateInfo frag_module_info{};
        frag_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_module_info.codeSize = frag_size;
        frag_module_info.pCode = frag_code;

        VkShaderModule vert_module;
        VkShaderModule frag_module;
        if (vkCreateShaderModule(device_, &vert_module_info, nullptr, &vert_module) != VK_SUCCESS)
        {
            return false;
        }
        if (vkCreateShaderModule(device_, &frag_module_info, nullptr, &frag_module) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device_, vert_module, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo shader_stages[2]{};
        shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shader_stages[0].module = vert_module;
        shader_stages[0].pName = "main";

        shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_stages[1].module = frag_module;
        shader_stages[1].pName = "main";

        VkVertexInputBindingDescription binding_desc{};
        binding_desc.binding = 0;
        binding_desc.stride = sizeof(Vertex);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attr_descs[2]{};
        attr_descs[0].binding = 0;
        attr_descs[0].location = 0;
        attr_descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attr_descs[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

        attr_descs[1].binding = 0;
        attr_descs[1].location = 1;
        attr_descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attr_descs[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));

        VkPipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &binding_desc;
        vertex_input_info.vertexAttributeDescriptionCount = 2;
        vertex_input_info.pVertexAttributeDescriptions = attr_descs;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchain_extent_.width);
        viewport.height = static_cast<float>(swapchain_extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchain_extent_;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
        dynamic_state.pDynamicStates = dynamic_states;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = cull_mode;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (enable_blend)
        {
            color_blend_attachment.blendEnable = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout_;
        pipeline_info.renderPass = render_pass_;
        pipeline_info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, out_pipeline);

        vkDestroyShaderModule(device_, vert_module, nullptr);
        vkDestroyShaderModule(device_, frag_module, nullptr);

        return result == VK_SUCCESS;
    }

    bool Renderer::create_framebuffers()
    {
        framebuffers_.resize(swapchain_image_views_.size());

        for (size_t i = 0; i < swapchain_image_views_.size(); i++)
        {
            VkImageView attachments[] = {swapchain_image_views_[i], depth_image_view_};

            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass_;
            framebuffer_info.attachmentCount = 2;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = swapchain_extent_.width;
            framebuffer_info.height = swapchain_extent_.height;
            framebuffer_info.layers = 1;

            if (vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            {
                return false;
            }
        }

        return true;
    }

    bool Renderer::create_command_pool()
    {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_family_;

        if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS)
        {
            return false;
        }

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        return vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_) == VK_SUCCESS;
    }

    bool Renderer::create_sync_objects()
    {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
                vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS)
            {
                return false;
            }
        }

        VkSemaphoreTypeCreateInfo timeline_info{};
        timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = 0;

        VkSemaphoreCreateInfo timeline_sem_info{};
        timeline_sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        timeline_sem_info.pNext = &timeline_info;

        if (vkCreateSemaphore(device_, &timeline_sem_info, nullptr, &upload_timeline_semaphore_) != VK_SUCCESS)
        {
            return false;
        }

        upload_timeline_value_ = 0;
        pending_destroy_count_ = 0;

        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool = command_pool_;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device_, &cmd_alloc, &upload_cmd_) != VK_SUCCESS)
        {
            return false;
        }

        return true;
    }

    void Renderer::destroy_swapchain_objects()
    {
        for (auto fb : framebuffers_)
        {
            if (fb)
                vkDestroyFramebuffer(device_, fb, nullptr);
        }
        framebuffers_.clear();

        if (depth_image_view_)
        {
            vkDestroyImageView(device_, depth_image_view_, nullptr);
            depth_image_view_ = VK_NULL_HANDLE;
        }
        if (depth_image_)
        {
            vkDestroyImage(device_, depth_image_, nullptr);
            depth_image_ = VK_NULL_HANDLE;
        }
        if (depth_image_memory_)
        {
            vkFreeMemory(device_, depth_image_memory_, nullptr);
            depth_image_memory_ = VK_NULL_HANDLE;
        }

        if (ui_pipeline_)
        {
            vkDestroyPipeline(device_, ui_pipeline_, nullptr);
            ui_pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_)
        {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        if (voxel_descriptor_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, voxel_descriptor_layout_, nullptr);
            voxel_descriptor_layout_ = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            destroy_buffer(&lighting_ubo_[i]);
        }

        if (render_pass_)
        {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }

        for (auto view : swapchain_image_views_)
        {
            if (view)
                vkDestroyImageView(device_, view, nullptr);
        }
        swapchain_image_views_.clear();
        swapchain_images_.clear();

        if (swapchain_)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    bool Renderer::recreate_swapchain()
    {
        if (device_ == VK_NULL_HANDLE)
        {
            return false;
        }
        vkDeviceWaitIdle(device_);
        destroy_swapchain_objects();

        if (!create_swapchain())
            return false;
        if (!create_render_pass())
            return false;
        if (!create_depth_resources())
            return false;
        if (!create_pipelines())
            return false;
        if (!create_framebuffers())
            return false;

        /* Recreate lighting UBO */
        VkDeviceSize ubo_size = sizeof(ShadowUniforms);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            create_buffer(ubo_size,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &lighting_ubo_[i]);
        }

        update_voxel_depth_descriptor();

        return true;
    }

    bool Renderer::create_compute_pipeline(const uint32_t *code, size_t code_size,
                                           VkPipelineLayout layout, VkPipeline *out_pipeline)
    {
        VkShaderModuleCreateInfo module_info{};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = code_size;
        module_info.pCode = code;

        VkShaderModule shader_module;
        if (vkCreateShaderModule(device_, &module_info, nullptr, &shader_module) != VK_SUCCESS)
        {
            return false;
        }

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader_module;
        stage.pName = "main";

        VkComputePipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = stage;
        pipeline_info.layout = layout;

        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, out_pipeline);

        vkDestroyShaderModule(device_, shader_module, nullptr);

        return result == VK_SUCCESS;
    }

    bool Renderer::create_timestamp_query_pool()
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical_device_, &props);

        if (props.limits.timestampComputeAndGraphics == VK_FALSE)
        {
            printf("  GPU timestamps not supported\n");
            timestamps_supported_ = false;
            return true;
        }

        timestamp_period_ns_ = props.limits.timestampPeriod;
        printf("  GPU timestamp period: %.3f ns\n", timestamp_period_ns_);

        VkQueryPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        pool_info.queryCount = GPU_TIMESTAMP_COUNT * MAX_FRAMES_IN_FLIGHT;

        if (vkCreateQueryPool(device_, &pool_info, nullptr, &timestamp_query_pool_) != VK_SUCCESS)
        {
            printf("  Failed to create timestamp query pool\n");
            timestamps_supported_ = false;
            return true;
        }

        timestamps_supported_ = true;
        return true;
    }

    void Renderer::destroy_timestamp_query_pool()
    {
        if (timestamp_query_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_, timestamp_query_pool_, nullptr);
            timestamp_query_pool_ = VK_NULL_HANDLE;
        }
    }

    bool Renderer::get_gpu_timings(GPUTimings *out_timings) const
    {
        if (!timestamps_supported_ || !out_timings)
            return false;

        uint32_t prev_frame = (current_frame_ + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
        uint32_t query_offset = prev_frame * GPU_TIMESTAMP_COUNT;

        uint64_t timestamps[GPU_TIMESTAMP_COUNT];
        VkResult result = vkGetQueryPoolResults(
            device_, timestamp_query_pool_,
            query_offset, GPU_TIMESTAMP_COUNT,
            sizeof(timestamps), timestamps, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);

        if (result != VK_SUCCESS)
        {
            out_timings->shadow_pass_ms = 0.0f;
            out_timings->main_pass_ms = 0.0f;
            out_timings->total_gpu_ms = 0.0f;
            return false;
        }

        float ns_to_ms = timestamp_period_ns_ / 1000000.0f;

        out_timings->shadow_pass_ms = (float)(timestamps[1] - timestamps[0]) * ns_to_ms;
        out_timings->main_pass_ms = (float)(timestamps[3] - timestamps[2]) * ns_to_ms;
        out_timings->total_gpu_ms = (float)(timestamps[3] - timestamps[0]) * ns_to_ms;

        return true;
    }

}

#define EXPORT_LIB
#include <immintrin.h>
#include <stdio.h>

#include "vulkan_renderer.h"

static struct vulkan_context* g_context;

#include "vulkan_descriptor.c"
#include "vulkan_pipeline.c"
#include "vulkan_command.c"
#include "vulkan_memory.c"
#include "vulkan_texture.c"

static const char* instance_layers[] = {
    "VK_LAYER_KHRONOS_validation"
};

static const char* instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};

static const char* device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static void rebuild_swapchain(u32 width, u32 height)
{
    VkResult res;
    u32 i;
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_context->physical_device, g_context->surface, &surface_caps);

    VkExtent2D extent;
    if (surface_caps.currentExtent.width == 0xffffffff)
    {
        extent.width = width;
        extent.height = height;
    }
    else
    {
        extent = surface_caps.currentExtent;
    }

    VkSwapchainKHR old_swapchain = g_context->swapchain;

    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = g_context->surface;
    swapchain_info.minImageCount = g_context->num_images; // TODO: replace
    swapchain_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = old_swapchain;

    res = vkCreateSwapchainKHR(g_context->device, &swapchain_info, NULL, &g_context->swapchain);
    if (res != VK_SUCCESS)
    {
        printf("Unable to create the swapchain!\n");
        return;
    }

    g_context->swapchain_extent = extent;

    if (old_swapchain)
    {
        vkDestroySwapchainKHR(g_context->device, old_swapchain, NULL);
    }

    res = vkGetSwapchainImagesKHR(g_context->device, g_context->swapchain, &g_context->num_images, g_context->swapchain_images);
}

static void handle_resize(u32 width, u32 height) // called in OS event loop
{
    if (width == 0 || height == 0)
    {
        g_context->minimized = SY_TRUE;
        return;
    }
    g_context->minimized = SY_FALSE;
    vkDeviceWaitIdle(g_context->device);
    rebuild_swapchain(width, height);
}

static b8 renderer_init(void* hwnd, void* hinstance, u32 width, u32 height)
{
    VkResult res;
    u32 i;
    HMODULE lib = LoadLibraryA("vulkan-1.dll");

    if (!lib)
    {
        return 0;
    }

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if SY_DEBUG
    instance_info.enabledLayerCount = ARRAYCOUNT(instance_layers);
    instance_info.ppEnabledLayerNames = instance_layers;
#endif
    instance_info.enabledExtensionCount = ARRAYCOUNT(instance_extensions);
    instance_info.ppEnabledExtensionNames = instance_extensions;

    res = vkCreateInstance(&instance_info, NULL, &g_context->instance);
    if (res != VK_SUCCESS)
    {
        printf("Failed to create vulkan instance\n");
        return 0;
    }

    VK_INSTANCE_FUNCTION(VK_LOAD_INSTANCE_FUNC)

    VkPhysicalDevice physical_devices[MAX_PHYSICAL_DEVICE_COUNT] = {0};
    u32 physical_device_count = ARRAYCOUNT(physical_devices);
    res = vkEnumeratePhysicalDevices(g_context->instance, &physical_device_count, physical_devices);

    if (!physical_device_count)
    {
        printf("Failed to find any physical devices on the host!\n");
        return 0;
    }
    // select a physical device with the features we want - select any for now, can be eventually configured by user
    VkPhysicalDeviceProperties properties = {0};
    VkPhysicalDeviceFeatures features = {0};

    for (i = 0; i < ARRAYCOUNT(physical_devices); ++i) // TODO: fix
    {
        if (!physical_devices[i])
        {
            g_context->physical_device = physical_devices[0];
            break;
        }
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);
        if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) && (properties.apiVersion))
        {
            g_context->physical_device = physical_devices[i];
            break;
        }
    }
    printf("GPU: %s\n", properties.deviceName);

    // TODO: check if image format is supported by the device
    VkFormatProperties format_props;
    vkGetPhysicalDeviceFormatProperties(g_context->physical_device, VK_FORMAT_A1R5G5B5_UNORM_PACK16, &format_props);

    VkQueueFamilyProperties queue_properties[8] = {0};
    u32 properties_count = ARRAYCOUNT(queue_properties);
    vkGetPhysicalDeviceQueueFamilyProperties(g_context->physical_device, &properties_count, queue_properties);

    if (!properties_count)
    {
        printf("Could not find any queues on this device!\n");
        return 0;
    }

    s32 graphics_queue_index = -1;

    for (i = 0; i < ARRAYCOUNT(queue_properties); ++i) // TODO: check if the queue supports presenting
    {
        if (queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphics_queue_index = i;
            break;
        }
    }

    if (graphics_queue_index < 0)
    {
        printf("Could not find a suitable hardware queue!\n");
        return 0;
    }

    f32 queue_priority[] = {1.0f};

    VkDeviceQueueCreateInfo graphics_queue = {0};
    graphics_queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics_queue.pQueuePriorities = queue_priority;
    graphics_queue.queueCount = 1;
    graphics_queue.queueFamilyIndex = graphics_queue_index;

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.enabledExtensionCount = ARRAYCOUNT(device_extensions);
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &graphics_queue;

    res = vkCreateDevice(g_context->physical_device, &device_info, NULL, &g_context->device);

    if (res != VK_SUCCESS)
    {
        printf("Failed to create a logical device!\n");
        return 0;
    }
    // load the rest of the function pointers
    VK_DEVICE_FUNCTION(VK_LOAD_DEVICE_FUNC)

    vkGetDeviceQueue(g_context->device, graphics_queue_index, 0, &g_context->graphics_queue);

    VkPhysicalDeviceMemoryProperties memory_props = {0};
    vkGetPhysicalDeviceMemoryProperties(g_context->physical_device, &memory_props);

    VkMemoryPropertyFlags desired_flags[] = {VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    b32 satisfied[ARRAYCOUNT(desired_flags)] = {0};

    for (i = 0; i < memory_props.memoryTypeCount; ++i)
    {
        for (u32 flag = 0; flag < ARRAYCOUNT(desired_flags); ++flag)
        {
            VkMemoryPropertyFlags type = memory_props.memoryTypes[i].propertyFlags;
            if ((type & desired_flags[flag]) == desired_flags[flag]) // NOTE: this can still include other flags
            {
                if (!satisfied[flag] || (type < g_context->memory[flag].flags))
                {
                    g_context->memory[flag].index = i;
                    g_context->memory[flag].flags = type;
                    satisfied[flag] = 1;
                }
            }
        }
    }

    for (i = 0; i < ARRAYCOUNT(desired_flags); ++i)
    {
        if (!satisfied[i])
        {
            printf("Could not find suitable memory types\n");
            return 0;
        }
    }

    VkMemoryAllocateInfo device_alloc_info = {0};
    device_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    device_alloc_info.allocationSize = megabytes(16); // TODO: tune
    
    for (u32 alloc = 0; alloc < ARRAYCOUNT(desired_flags); ++alloc)
    {
        device_alloc_info.memoryTypeIndex = g_context->memory[alloc].index;
        res = vkAllocateMemory(g_context->device, &device_alloc_info, NULL, &g_context->memory[alloc].handle);
        //g_context->memory[alloc].size = 
    }
    // TODO: remove
    device_alloc_info.memoryTypeIndex = get_memory_type_index(&memory_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    res = vkAllocateMemory(g_context->device, &device_alloc_info, NULL, &g_context->staging_memory);
    vkMapMemory(g_context->device, g_context->staging_memory, 0, VK_WHOLE_SIZE, 0, &g_context->staging_data);

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = VERTEX_ARRAY_LEN * sizeof(Vertex);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = vkCreateBuffer(g_context->device, &buffer_info, NULL, &g_context->vertex_buffer);
    allocate_buffer(g_context->vertex_buffer, &g_context->memory[0]);

    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.size = megabytes(8); // TODO: tune

    res = vkCreateBuffer(g_context->device, &buffer_info, NULL, &g_context->staging_buffer);
    vkBindBufferMemory(g_context->device, g_context->staging_buffer, g_context->staging_memory, 0);

    // create VRAM clone to sample from
    texture_init(&g_context->sample_vram, &g_context->sample_vram_view, VRAM_WIDTH, VRAM_HEIGHT, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &g_context->memory[1]); // TODO: error handling

    VkWin32SurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hwnd = hwnd;
    surface_info.hinstance = hinstance;

    vkCreateWin32SurfaceKHR(g_context->instance, &surface_info, NULL, &g_context->surface);

    VkBool32 present_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_context->physical_device, graphics_queue_index, g_context->surface, &present_supported);

    if (present_supported == VK_FALSE)
    {
        printf("Presentation is not supported on this device\n");
        return 0;
    }

    // create the render target
    texture_init(&g_context->render_vram, &g_context->render_vram_view, VRAM_WIDTH, VRAM_HEIGHT, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &g_context->memory[1]);

    {
        VkAttachmentDescription attachments[3] = {0};
        // "render target" attachment
        attachments[0].format = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentReference target_attachment = {0};
        target_attachment.attachment = 0;
        target_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription descriptions[2] = {0};
        descriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        descriptions[0].colorAttachmentCount = 1;
        descriptions[0].pColorAttachments = &target_attachment;

        VkSubpassDependency dependencies[2] = {0};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;//VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;//VK_ACCESS_SHADER_READ_BIT;
        //dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // NOTE: not sure if this is needed
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo renderpass_info = {0};
        renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderpass_info.attachmentCount = 1;
        renderpass_info.pAttachments = attachments;
        renderpass_info.subpassCount = 1;
        renderpass_info.pSubpasses = descriptions;
        renderpass_info.dependencyCount = 1;
        renderpass_info.pDependencies = &dependencies[1];
        
        res = vkCreateRenderPass(g_context->device, &renderpass_info, NULL, &g_context->renderpass);

        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = g_context->renderpass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &g_context->render_vram_view;
        framebuffer_info.width = VRAM_WIDTH;
        framebuffer_info.height = VRAM_HEIGHT;
        framebuffer_info.layers = 1;
        vkCreateFramebuffer(g_context->device, &framebuffer_info, NULL, &g_context->framebuffer);
    }

    VkSurfaceCapabilitiesKHR surface_caps = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_context->physical_device, g_context->surface, &surface_caps);
    // might move this into the swapchain rebuild
    u32 image_count = surface_caps.maxImageCount > 0 ? surface_caps.minImageCount : 3; // ideally we would like 3 images
    g_context->num_images = image_count;

    rebuild_swapchain(width, height);

    VkCommandPoolCreateInfo command_pool_info = {0};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = graphics_queue_index;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(g_context->device, &command_pool_info, NULL, &g_context->command_pool);

    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = g_context->command_pool;
    alloc_info.commandBufferCount = 1;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    res = vkAllocateCommandBuffers(g_context->device, &alloc_info, &g_context->command_buffer);

    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(g_context->device, &semaphore_info, NULL, &g_context->submission_complete);
    vkCreateSemaphore(g_context->device, &semaphore_info, NULL, &g_context->image_acquired);

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(g_context->device, &fence_info, NULL, &g_context->render_fence);

    begin_command_buffer();
    transition_layout(g_context->render_vram, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    //transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_UNDEFINED, vk_image_layout)
    vkEndCommandBuffer(g_context->command_buffer);
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &g_context->command_buffer;

    vkQueueSubmit(g_context->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

    vkQueueWaitIdle(g_context->graphics_queue); // not worth waiting on a fence here?

    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    //sampler_info.minFilter = sampler_info.magFilter = VK_FILTER_LINEAR;
    vkCreateSampler(g_context->device, &sampler_info, NULL, &g_context->sampler);

    g_context->descriptor_pool = create_descriptor_pool(2);

    VkDescriptorSetLayoutBinding bindings[2] = {0};
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = &g_context->sampler;

    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = bindings;

    vkCreateDescriptorSetLayout(g_context->device, &layout_info, NULL, &g_context->descriptor_set_layout);

    VkDescriptorSetLayout layouts[] = {g_context->descriptor_set_layout};
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {0};
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.descriptorPool = g_context->descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1;
    descriptor_alloc_info.pSetLayouts = layouts;
    res = vkAllocateDescriptorSets(g_context->device, &descriptor_alloc_info, &g_context->descriptor_set);

    VkDescriptorImageInfo vram_descriptor = {0};
    vram_descriptor.imageView = g_context->sample_vram_view;
    vram_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vram_descriptor.sampler = g_context->sampler;

    VkWriteDescriptorSet descriptor_writes[2] = {0};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = g_context->descriptor_set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &vram_descriptor;

    vkUpdateDescriptorSets(g_context->device, 1, descriptor_writes, 0, NULL);

    struct pipeline_config config = {0};
    config.vertex_input = SY_TRUE;
    config.dynamic_scissor = SY_TRUE;
    config.renderpass = g_context->renderpass;
    config.fragment_shader.push_constant_size = sizeof(u32);
    build_pipeline_layout(&config, &g_context->descriptor_set_layout);
    g_context->pipeline_layout = config.pipeline_layout;
    config.vertex_shader = prepare_shader("shader/vertshader.spv", VK_SHADER_STAGE_VERTEX_BIT);
    config.fragment_shader = prepare_shader("shader/fragshader.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    initialize_pipeline(&config, &g_context->pipeline);

    g_context->current_entry_type = RENDER_ENTRY_TYPE_INVALID;

    return 1;
}

static inline vec2 get_texcoord(u32 texcoord)
{
    return v2f((f32)(texcoord & 0xff), (f32)((texcoord >> 8) & 0xff));
}

static void flush_render_commands(b8 submit);

static inline struct render_entry* push_render_entry(enum render_entry_type type)
{
    if (g_context->entry_count == MAX_RENDER_ENTRY_COUNT)
    {
        flush_render_commands(1);
    }
    g_context->current_entry_type = type;
    g_context->entries[g_context->entry_count].type = type;
    return &g_context->entries[g_context->entry_count++];
}
// NOTE: This is poor man's batching, basically globbing together entries if the previous entry shares the same type.
// Eventually we'll want to track dirty areas of the vram and sort the draws that way
static inline void push_primitive(u32 num_vertices, u32 texture_mode)
{
    if ((g_context->current_entry_type & RENDER_ENTRY_TYPE_DRAW_PRIMITIVE) && (g_context->entries[g_context->entry_count - 1].texture_mode == texture_mode))
    {
        g_context->entries[g_context->entry_count - 1].num_vertices += num_vertices;
    }
    else
    {
        struct render_entry* entry = push_render_entry(texture_mode ? RENDER_ENTRY_TYPE_TEXTURED : RENDER_ENTRY_TYPE_SHADED);
        entry->num_vertices = num_vertices;
        entry->texture_mode = texture_mode;
    }
}

void push_polygon(const u32* commands, u32 flags, vec2 draw_offset)
{
    u32 in_color = commands[0];
    u32 color[4];

    u32 num_vertices = (flags & POLYGON_FLAG_IS_QUAD) ? 4 : 3;
    u32 stride = 1;

    u32 mode = 0;
    vec2i texture_page;
    vec2i clut_base;

    if (flags & POLYGON_FLAG_TEXTURED)
    {
        stride += 1;
        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;

        u32 page_offset = (flags & POLYGON_FLAG_GOURAUD_SHADED) ? 5 : 4;
        u32 texpage_x = (commands[page_offset] >> 16) & 0xf;
        u32 texpage_y = (commands[page_offset] >> 20) & 0x1;

        texture_page.x = texpage_x * 64;
        texture_page.y = texpage_y * 256;

        clut_base.x = clut_x * 16;
        clut_base.y = clut_y;
     
        switch ((commands[page_offset] >> 23) & 0x3)
        {
        case 0: // 4-bit CLUT mode
            mode = 2;
            break;
        case 1: // 8-bit CLUT mode
            SY_ASSERT(0);
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 1;
            break;
        }
    }

    if (flags & POLYGON_FLAG_RAW_TEXTURE)
    {
        color[0] = 0x00ffffff;
        color[3] = color[2] = color[1] = color[0];
    }
    else
    {
        if (flags & POLYGON_FLAG_GOURAUD_SHADED)
        {
            stride += 1;
            for (u32 i = 0; i < num_vertices; ++i)
            {
                color[i] = commands[stride * i];
            }
        }
        else
        {
            color[3] = color[2] = color[1] = color[0] = in_color;
        }
    }

    Vertex* v = g_context->vertex_array + g_context->vertex_count;
    u32 added_vertices = 3;

    for (u32 i = 0; i < 3; ++i)
    {
        v[i].pos = commands[1 + stride * i];//v2add(temp_cvt(commands[1 + stride * i]), draw_offset);
        if (flags & POLYGON_FLAG_TEXTURED)
        {
            v[i].uv = get_texcoord(commands[2 + stride * i]); // NOTE: yucky
            v[i].texture_page = texture_page;
            v[i].clut = clut_base;
        }
        v[i].color = color[i];

    }

    if (flags & POLYGON_FLAG_IS_QUAD)
    {
        added_vertices += 3;

        for (u32 i = 1; i < 4; ++i)
        {
            v[i + 2].pos = commands[1 + stride * i];//temp_cvt(commands[1 + stride * i]);
            if (flags & POLYGON_FLAG_TEXTURED)
            {
                v[i + 2].uv = get_texcoord(commands[2 + stride * i]); // NOTE: yucky
                v[i + 2].texture_page = texture_page;
                v[i + 2].clut = clut_base;
            }
            v[i + 2].color = color[i];
        }
    }

    g_context->vertex_count += added_vertices;
    push_primitive(added_vertices, mode);
}

void push_rect(const u32* commands, u32 flags, u32 texpage)
{
    vec2i texture_page = v2i((texpage & 0xf) * 64, ((texpage >> 4) & 0x1) * 256);
    vec2i clut_base;
    u32 color = commands[0];
    u32 offset = 0;
    u32 mode = 0;

    if (flags & RECT_FLAG_TEXTURED)
    {
        ++offset;
        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;
        clut_base.x = clut_x * 16;
        clut_base.y = clut_y;

        switch ((texpage >> 7) & 0x3)
        {
        case 0: // 4-bit CLUT mode
            mode = 2;
            break;
        case 1: // 8-bit CLUT mode
            SY_ASSERT(0);
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 1;
            break;
        }
    }

    if (flags & RECT_FLAG_RAW_TEXTURE)
    {
        color = 0x00ffffff;
    }
    else
    {

    }

    u32 width;
    u32 height;

    switch ((flags >> 3) & 0x3)
    {
    case 0x0: // variable size
        width = (u16)commands[2 + offset];
        height = (u16)(commands[2 + offset] >> 16);
        break;
    case 0x1: // 1x1
        width = height = 1;
        break;
    case 0x2: // 8x8
        width = height = 8;
        break;
    case 0x3: // 16x16
        width = height = 16;
        break;
    }

    Vertex* v = g_context->vertex_array + g_context->vertex_count;
    g_context->vertex_count += 6;

    height <<= 16;
    //u32 base_vertex = commands[1];
    vertex_attrib base_vertex = {.vertex = commands[1]};
    u32 sizes[] = {0, width, height, width | height};
    
    u8 uv_x = commands[2] & 0xff;
    u8 uv_y = (u8)(commands[2] >> 8);
    vertex_attrib base_texcoord = {.x = uv_x, .y = uv_y};

    u32 loop[] = {0, 1, 2, 1, 2, 3};
    for (u32 l = 0, i = loop[0]; l < ARRAYCOUNT(loop); ++l, i = loop[l])
    {
        vertex_attrib current_vertex = {.vertex = sizes[i]};
        current_vertex.x += base_vertex.x;
        current_vertex.y += base_vertex.y;
        v[l].pos = current_vertex.vertex;

        if (flags & RECT_FLAG_TEXTURED)
        {
            vertex_attrib a = {.vertex = sizes[i]};

            a.x += base_texcoord.x;
            a.y += base_texcoord.y;

            vec2 final_uv = v2f(a.x, a.y);

            v[l].uv = final_uv;
            v[l].clut = clut_base;
            v[l].texture_page = texture_page;
        }
        
        v[l].color = color;

    }

    push_primitive(6, mode);
}

static inline u32 staging_buffer_write(void* data, u32 size)
{
    u32 result = g_context->staging_buffer_offset;
    memcpy(((u8*)g_context->staging_data + g_context->staging_buffer_offset), data, size);
    g_context->staging_buffer_offset += size;
    return result;
}

void renderer_transfer(void* data, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
    struct vram_transfer* entry = &push_render_entry(RENDER_ENTRY_TYPE_CPU_TO_VRAM)->transfer;
    entry->x = dst_x;
    entry->y = dst_y;
    entry->width = width;
    entry->height = height;
    vkWaitForFences(g_context->device, 1, &g_context->render_fence, VK_TRUE, UINT64_MAX); // TODO: 2 staging buffers to avoid busy waiting
    entry->offset = staging_buffer_write(data, (width * height * 2));
}

void renderer_copy(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
    struct vram_transfer* entry = &push_render_entry(RENDER_ENTRY_TYPE_VRAM_TO_VRAM)->transfer;
    entry->x = src_x;
    entry->y = src_y;
    entry->x2 = dst_x;
    entry->y2 = dst_y;
    entry->width = width;
    entry->height = height;
}

void renderer_read_vram(void* dest, u32 src_x, u32 src_y, u32 width, u32 height)
{
    flush_render_commands(0);

    VkBufferImageCopy copy_region = {0};
    copy_region.imageExtent.depth = 1;
    copy_region.imageExtent.width = width;
    copy_region.imageExtent.height = height;
    copy_region.imageOffset.x = src_x;
    copy_region.imageOffset.y = src_y;
    copy_region.bufferOffset = 0; // doesnt matter for now since writes are flushed
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1; 
    // NOTE: dont think this is needed
    VkBufferMemoryBarrier buffer_barrier = {0};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = 0;
    buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_barrier.buffer = g_context->staging_buffer;
    buffer_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(g_context->command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &buffer_barrier, 0, NULL);

    vkCmdCopyImageToBuffer(g_context->command_buffer, g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_context->staging_buffer, 1, &copy_region);

    vkEndCommandBuffer(g_context->command_buffer);

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &g_context->command_buffer;

    vkQueueSubmit(g_context->graphics_queue, 1, &submit_info, g_context->render_fence);

    vkWaitForFences(g_context->device, 1, &g_context->render_fence, VK_TRUE, UINT64_MAX);

    memcpy(dest, g_context->staging_data, (width * height * 2));
}

static void push_scissor(s16 x1, s16 y1, s16 x2, s16 y2)
{
    VkRect2D* scissor = &push_render_entry(RENDER_ENTRY_TYPE_SET_SCISSOR)->scissor;
    scissor->offset.x = x1;
    scissor->offset.y = y1;
    scissor->extent.width = x2;
    scissor->extent.height = y2;
}

static inline void begin_renderpass(void)
{
    if (g_context->in_renderpass)
        return;
    
    VkExtent2D image_extent = {.width = 1024, .height = 512};
    //VkClearValue clear = {.color.float32 = {0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo renderpass_begin_info = {0};
    renderpass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderpass_begin_info.renderPass = g_context->renderpass;
    renderpass_begin_info.framebuffer = g_context->framebuffer;
    renderpass_begin_info.renderArea.extent = image_extent; // TODO: handle this
    //renderpass_begin_info.clearValueCount = 1;
    //renderpass_begin_info.pClearValues = &clear;

    vkCmdBeginRenderPass(g_context->command_buffer, &renderpass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    g_context->in_renderpass = 1;
}

static inline void end_renderpass(void)
{
    if (g_context->in_renderpass)
    {
        vkCmdEndRenderPass(g_context->command_buffer);
        g_context->in_renderpass = 0;
    }
}

static void flush_render_commands(b8 submit)
{
    g_context->staging_buffer_offset = 0;

    vkWaitForFences(g_context->device, 1, &g_context->render_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(g_context->device, 1, &g_context->render_fence);

    vkResetCommandPool(g_context->device, g_context->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_context->command_buffer, &begin_info);

    transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    void* data;
    // TODO: handle memory indices and map once
    vkMapMemory(g_context->device, g_context->memory[g_context->allocation_index].handle, 0, VK_WHOLE_SIZE, 0, &data);
    memcpy(data, g_context->vertex_array, sizeof(Vertex) * g_context->vertex_count);
    vkUnmapMemory(g_context->device, g_context->memory[g_context->allocation_index].handle);

    u32 vertex_offset = 0;
    vkCmdBindPipeline(g_context->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_context->pipeline);
    vkCmdBindDescriptorSets(g_context->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_context->pipeline_layout, 0, 1, &g_context->descriptor_set, 0, NULL);

    VkDeviceSize vb_offsets[] = {0};
    vkCmdBindVertexBuffers(g_context->command_buffer, 0, 1, &g_context->vertex_buffer, vb_offsets);

    for (u32 entry = 0; entry < g_context->entry_count; ++entry)
    {
        switch (g_context->entries[entry].type)
        {
        case RENDER_ENTRY_TYPE_TEXTURED:
        {
            // NOTE: we need to flush the vram copy before sampling
            end_renderpass();
            transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy vram_copy = {.srcSubresource = {.layerCount = 1, .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                .dstSubresource = vram_copy.srcSubresource, .extent = {.width = VRAM_WIDTH, .height = VRAM_HEIGHT, .depth = 1}};
            
            vkCmdCopyImage(g_context->command_buffer, g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_context->sample_vram, 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vram_copy);

            transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        case RENDER_ENTRY_TYPE_SHADED:
        {
            begin_renderpass();
            vkCmdPushConstants(g_context->command_buffer, g_context->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32), &g_context->entries[entry].texture_mode);
            vkCmdDraw(g_context->command_buffer, g_context->entries[entry].num_vertices, 1, vertex_offset, 0);
            vertex_offset += g_context->entries[entry].num_vertices;
        }   break;
        case RENDER_ENTRY_TYPE_CPU_TO_VRAM:
        {
            // TODO: when we implement batching, the transfer has to be marked as dirty
            end_renderpass();

            transition_layout(g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            struct vram_transfer transfer = g_context->entries[entry].transfer;

            VkBufferImageCopy copy_region = {0};
            copy_region.imageExtent.depth = 1;
            copy_region.imageExtent.width = transfer.width;
            copy_region.imageExtent.height = transfer.height;
            copy_region.imageOffset.x = transfer.x;
            copy_region.imageOffset.y = transfer.y;
            copy_region.bufferOffset = transfer.offset;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.layerCount = 1; 

            vkCmdCopyBufferToImage(g_context->command_buffer, g_context->staging_buffer, g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

            transition_layout(g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        }   break;
        case RENDER_ENTRY_TYPE_VRAM_TO_VRAM:
        {
            end_renderpass();
            transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy vram_copy = {.srcSubresource = {.layerCount = 1, .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                .dstSubresource = vram_copy.srcSubresource, .extent = {.width = VRAM_WIDTH, .height = VRAM_HEIGHT, .depth = 1}};
            
            vkCmdCopyImage(g_context->command_buffer, g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_context->sample_vram, 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vram_copy);

            transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            transition_layout(g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            // NOTE: need to read more on the behavior for VRAM-VRAM, for now im playing it safe by flushing the copy and copying from there
            struct vram_transfer transfer = g_context->entries[entry].transfer;
            VkImageCopy copy = {0};
            copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.srcSubresource.layerCount = 1;
            copy.dstSubresource = copy.srcSubresource;
            copy.srcOffset.x = transfer.x;
            copy.srcOffset.y = transfer.y;
            copy.dstOffset.x = transfer.x2;
            copy.dstOffset.y = transfer.y2;
            copy.extent.width = transfer.width;
            copy.extent.height = transfer.height;
            copy.extent.depth = 1;

            vkCmdCopyImage(g_context->command_buffer, g_context->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_context->render_vram, 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            transition_layout(g_context->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            transition_layout(g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        }   break;
        case RENDER_ENTRY_TYPE_SET_SCISSOR:
        {
            vkCmdSetScissor(g_context->command_buffer, 0, 1, &g_context->entries[entry].scissor);
        } 
        break;
        default:
            printf("Encountered invalid render entry type\n");
            break;
        }
    }

    end_renderpass(); // NOTE: temp

    g_context->entry_count = 0;
    g_context->current_entry_type = RENDER_ENTRY_TYPE_INVALID;
    g_context->vertex_count = 0;
    if (submit)
    {
        vkEndCommandBuffer(g_context->command_buffer);

        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &g_context->command_buffer;

        vkQueueSubmit(g_context->graphics_queue, 1, &submit_info, g_context->render_fence);
    }

}

void render_frame(void)
{
    flush_render_commands(0);

    u32 image_index = 0;
    b8 should_present = 0;
    if (!g_context->minimized)
    {
        should_present = 1;
        vkAcquireNextImageKHR(g_context->device, g_context->swapchain, UINT64_MAX, g_context->image_acquired, NULL, &image_index);
        VkImage dstImage = g_context->swapchain_images[image_index];
        {
            VkImageMemoryBarrier image_barrier = {0};
            image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.image = dstImage;
            image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            image_barrier.srcAccessMask = 0;
            image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_barrier.subresourceRange.layerCount = 1;
            image_barrier.subresourceRange.levelCount = 1;

            vkCmdPipelineBarrier(g_context->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, NULL, 0, NULL, 1, &image_barrier);
        }

        VkImageBlit blit = {0};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1].x = 1024;
        blit.srcOffsets[1].y = 512;
        blit.srcOffsets[1].z = 1;
        blit.dstOffsets[1].x = g_context->swapchain_extent.width;
        blit.dstOffsets[1].y = g_context->swapchain_extent.height;
        blit.dstOffsets[1].z = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        // TODO: check VK_FORMAT_FEATURE_BLIT_
        vkCmdBlitImage(g_context->command_buffer, g_context->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        {
            VkImageMemoryBarrier image_barrier = {0};
            image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.image = dstImage;
            image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            image_barrier.dstAccessMask = 0;
            image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_barrier.subresourceRange.layerCount = 1;
            image_barrier.subresourceRange.levelCount = 1;

            vkCmdPipelineBarrier(g_context->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);
        }
    }
    execute_command_buffer(should_present, image_index);
}

static void renderer_destroy(void)
{
    vkDeviceWaitIdle(g_context->device);

    vkDestroySemaphore(g_context->device, g_context->image_acquired, NULL);
    vkDestroySemaphore(g_context->device, g_context->submission_complete, NULL);
    vkDestroyFence(g_context->device, g_context->render_fence, NULL);

    vkDestroyCommandPool(g_context->device, g_context->command_pool, NULL);

    vkDestroyRenderPass(g_context->device, g_context->renderpass, NULL);
    vkDestroyFramebuffer(g_context->device, g_context->framebuffer, NULL);

    vkDestroyBuffer(g_context->device, g_context->staging_buffer, NULL);
    vkDestroyBuffer(g_context->device, g_context->vertex_buffer, NULL);

    vkDestroyImage(g_context->device, g_context->render_vram, NULL);
    vkDestroyImage(g_context->device, g_context->sample_vram, NULL);
    vkDestroyImageView(g_context->device, g_context->render_vram_view, NULL);
    vkDestroyImageView(g_context->device, g_context->sample_vram_view, NULL);

    vkUnmapMemory(g_context->device, g_context->staging_memory);

    for (u32 i = 0; i < 2; ++i)
        vkFreeMemory(g_context->device, g_context->memory[i].handle, NULL);
    
    vkFreeMemory(g_context->device, g_context->staging_memory, NULL);    

    vkDestroySwapchainKHR(g_context->device, g_context->swapchain, NULL);

    vkDestroyPipelineLayout(g_context->device, g_context->pipeline_layout, NULL);
    vkDestroyPipeline(g_context->device, g_context->pipeline, NULL);

    vkDestroySampler(g_context->device, g_context->sampler, NULL);
    vkDestroyDescriptorSetLayout(g_context->device, g_context->descriptor_set_layout, NULL);
    vkDestroyDescriptorPool(g_context->device, g_context->descriptor_pool, NULL);
    
    vkDestroyDevice(g_context->device, NULL);

    vkDestroySurfaceKHR(g_context->instance, g_context->surface, NULL);
    vkDestroyInstance(g_context->instance, NULL);

    free(g_context);
}

SUNNY_API Renderer* load_renderer(void)
{
    g_context = malloc(sizeof(struct vulkan_context));
    memset(g_context, 0, sizeof(struct vulkan_context));
    
    Renderer* renderer = &g_context->header;
    renderer->initialize = renderer_init;
    renderer->shutdown = renderer_destroy;
    renderer->render_frame = render_frame;
    renderer->handle_resize = handle_resize;
    renderer->draw_polygon = push_polygon;
    renderer->draw_rect = push_rect;
    renderer->transfer = renderer_transfer;
    renderer->read_vram = renderer_read_vram;
    renderer->copy = renderer_copy;
    renderer->set_scissor = push_scissor;
    return renderer;
}
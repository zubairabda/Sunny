#define EXPORT_LIB
#include <immintrin.h>
#include <stdio.h>

#include "vulkan_renderer.h"

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

static void vulkan_create_swapchain(struct vulkan_context *vk, u32 width, u32 height)
{
    VkResult res;
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical_device, vk->surface, &surface_caps);

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

    VkSwapchainKHR old_swapchain = vk->swapchain.handle;

    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = vk->surface;
    swapchain_info.minImageCount = surface_caps.minImageCount;
    swapchain_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = old_swapchain;

    res = vkCreateSwapchainKHR(vk->device, &swapchain_info, NULL, &vk->swapchain.handle);
    if (res != VK_SUCCESS) {
        printf("Unable to create the swapchain!\n");
        return;
    }

    vk->swapchain.format = swapchain_info.imageFormat;
    vk->swapchain.width = extent.width;
    vk->swapchain.height = extent.height;

    if (old_swapchain)
    {
        for (u32 i = 0; i < vk->swapchain.image_count; ++i)
        {
            vkDestroyImageView(vk->device, vk->swapchain.image_views[i], VK_NULL_HANDLE);
            vkDestroyFramebuffer(vk->device, vk->swapchain.framebuffers[i], VK_NULL_HANDLE);
        }
        vkDestroySwapchainKHR(vk->device, old_swapchain, NULL);
    }

    vkGetSwapchainImagesKHR(vk->device, vk->swapchain.handle, &vk->swapchain.image_count, VK_NULL_HANDLE);
    SY_ASSERT(vk->swapchain.image_count <= ARRAYCOUNT(vk->swapchain.images));
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain.handle, &vk->swapchain.image_count, vk->swapchain.images);
}

static void vulkan_create_swapchain_framebuffers(struct vulkan_context *vk)
{
    for (u32 i = 0; i < vk->swapchain.image_count; ++i)
    {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.format = vk->swapchain.format;
        view_info.image = vk->swapchain.images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.layerCount = 1;
        view_info.subresourceRange.levelCount = 1;

        vkCreateImageView(vk->device, &view_info, VK_NULL_HANDLE, &vk->swapchain.image_views[i]);

        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.width = vk->swapchain.width;
        framebuffer_info.height = vk->swapchain.height;
        framebuffer_info.layers = 1;
        framebuffer_info.renderPass = vk->swapchain_renderpass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &vk->swapchain.image_views[i];
        vkCreateFramebuffer(vk->device, &framebuffer_info, VK_NULL_HANDLE, &vk->swapchain.framebuffers[i]);
    }
}

static void vulkan_create_swapchain_renderpass(struct vulkan_context *vk)
{
    // TODO: if the swapchains format changes, we need to call this function (though unlikely)
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = vk->swapchain.format;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;

    VkAttachmentReference color_ref = {0};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo renderpass_info = {0};
    renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderpass_info.attachmentCount = 1;
    renderpass_info.pAttachments = &color_attachment;
    renderpass_info.subpassCount = 1;
    renderpass_info.pSubpasses = &subpass;

    vkCreateRenderPass(vk->device, &renderpass_info, VK_NULL_HANDLE, &vk->swapchain_renderpass);
}

static void vulkan_handle_resize(renderer_interface *renderer, u32 new_width, u32 new_height)
{
    struct vulkan_context *vk = (struct vulkan_context *)renderer;
    if (new_width == 0 || new_height == 0)
    {
        return;
    }
    vkDeviceWaitIdle(vk->device); // NOTE: not sure if this is actually needed
    //VkFormat prev_format = vk->swapchain.format;
    vulkan_create_swapchain(vk, new_width, new_height);
    //if (vk->swapchain.format != prev_format)
    //{
    //    vkDestroyRenderPass(vk->device, vk->swapchain_renderpass, VK_NULL_HANDLE);
    //    create_swapchain_renderpass(vk);
    //}
    vulkan_create_swapchain_framebuffers(vk);
}

static int vulkan_make_instance(struct vulkan_context *vk)
{
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if SY_DEBUG
    instance_info.enabledLayerCount = ARRAYCOUNT(instance_layers);
    instance_info.ppEnabledLayerNames = instance_layers;
#endif
    instance_info.enabledExtensionCount = ARRAYCOUNT(instance_extensions);
    instance_info.ppEnabledExtensionNames = instance_extensions;

    VkResult res = vkCreateInstance(&instance_info, NULL, &vk->instance);
    if (res != VK_SUCCESS) {
        printf("Failed to create vulkan instance\n");
        return 0;
    }

    #define VK_INSTANCE_VARIABLE vk->instance
    VK_INSTANCE_FUNCTION(VK_LOAD_INSTANCE_FUNC)
    return 1;
}

static int vulkan_make_device(struct vulkan_context *vk)
{
    VkPhysicalDevice physical_devices[MAX_PHYSICAL_DEVICE_COUNT] = {0};
    u32 physical_device_count = ARRAYCOUNT(physical_devices);
    VkResult res = vkEnumeratePhysicalDevices(vk->instance, &physical_device_count, physical_devices);

    if (!physical_device_count) {
        printf("Failed to find any physical devices on the host!\n");
        return 0;
    }
    // select a physical device with the features we want - select any for now, can be eventually configured by user
    VkPhysicalDeviceProperties properties = {0};
    VkPhysicalDeviceFeatures features = {0};

    VkPhysicalDevice physical_device = physical_devices[0];

    for (u32 i = 0; i < physical_device_count; ++i) // TODO: fix
    {
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
        vkGetPhysicalDeviceFeatures(physical_devices[i], &features);
        
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physical_device = physical_devices[i];
            break;
        }
    }
    vk->physical_device = physical_device;

    printf("HW: %s\n", properties.deviceName);

    // TODO: check if image format is supported by the device
    VkFormatProperties format_props;
    vkGetPhysicalDeviceFormatProperties(vk->physical_device, VK_FORMAT_A1R5G5B5_UNORM_PACK16, &format_props);

    VkQueueFamilyProperties queue_properties[8] = {0};
    u32 properties_count = ARRAYCOUNT(queue_properties);
    vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &properties_count, queue_properties);

    if (!properties_count) {
        printf("Could not find any queues on this device!\n");
        return 0;
    }

    u32 graphics_queue_index = 0xffffffff;

    for (u32 i = 0; i < properties_count; ++i) // TODO: check if the queue supports presenting
    {
        if (queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_queue_index = i;
            break;
        }
    }

    if (graphics_queue_index == 0xffffffff) {
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

    res = vkCreateDevice(vk->physical_device, &device_info, NULL, &vk->device);
    if (res != VK_SUCCESS) {
        printf("Failed to create a logical device!\n");
        return 0;
    }
    // load the rest of the function pointers
    #define VK_DEVICE_VARIABLE vk->device
    VK_DEVICE_FUNCTION(VK_LOAD_DEVICE_FUNC)

    vkGetDeviceQueue(vk->device, graphics_queue_index, 0, &vk->graphics_queue);
    vk->queue_indices.graphics = graphics_queue_index;

    return 1;
}

static int vulkan_init_internal(struct vulkan_context *vk)
{
    u32 i;

    {
        VkBufferCreateInfo buffer_info = {0};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = VERTEX_ARRAY_LEN * sizeof(render_vertex);
        buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vk->device, &buffer_info, NULL, &vk->vertex_buffer);
        
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(vk->device, vk->vertex_buffer, &requirements);

        VkMemoryAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = vulkan_find_memory_type(vk->physical_device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requirements.memoryTypeBits);
        vkAllocateMemory(vk->device, &alloc_info, VK_NULL_HANDLE, &vk->vertex_buffer_memory);

        vkBindBufferMemory(vk->device, vk->vertex_buffer, vk->vertex_buffer_memory, 0);
    }
    vkMapMemory(vk->device, vk->vertex_buffer_memory, 0, VK_WHOLE_SIZE, 0, (void **)&vk->renderer.vertex_array);

    {
        VkBufferCreateInfo buffer_info = {0};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = MEGABYTES(8);
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(vk->device, &buffer_info, VK_NULL_HANDLE, &vk->staging_buffer);
        
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(vk->device, vk->staging_buffer, &requirements);

        VkMemoryAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = vulkan_find_memory_type(vk->physical_device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requirements.memoryTypeBits);
        vkAllocateMemory(vk->device, &alloc_info, VK_NULL_HANDLE, &vk->staging_buffer_memory);

        vkBindBufferMemory(vk->device, vk->staging_buffer, vk->staging_buffer_memory, 0);
    }
    vkMapMemory(vk->device, vk->staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &vk->staging_data);

    // create VRAM clone to sample from
    texture_init(vk, &vk->sample_vram, &vk->sample_vram_view, VRAM_WIDTH, VRAM_HEIGHT, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &vk->sample_vram_memory);

    // create the VRAM render target which we draw to
    texture_init(vk, &vk->render_vram, &vk->render_vram_view, VRAM_WIDTH, VRAM_HEIGHT, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &vk->render_vram_memory);

    // create the display vram which is only updated on vblank
    texture_init(vk, &vk->display_vram, &vk->display_vram_view, VRAM_WIDTH, VRAM_HEIGHT, VK_FORMAT_A1R5G5B5_UNORM_PACK16,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &vk->display_vram_memory);

    {
        VkAttachmentDescription attachments[3] = {0};
        // "render target" attachment -> render_vram image
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
#if 0
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
#endif
        VkRenderPassCreateInfo renderpass_info = {0};
        renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderpass_info.attachmentCount = 1;
        renderpass_info.pAttachments = attachments;
        renderpass_info.subpassCount = 1;
        renderpass_info.pSubpasses = descriptions;
        //renderpass_info.dependencyCount = 1;
        //renderpass_info.pDependencies = &dependencies[1];
        
        vkCreateRenderPass(vk->device, &renderpass_info, NULL, &vk->renderpass);

        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = vk->renderpass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &vk->render_vram_view;
        framebuffer_info.width = VRAM_WIDTH;
        framebuffer_info.height = VRAM_HEIGHT;
        framebuffer_info.layers = 1;
        vkCreateFramebuffer(vk->device, &framebuffer_info, NULL, &vk->framebuffer);
    }
    // I'm not sure these values even matter, but we may want to pass in the window width and height
    vulkan_create_swapchain(vk, 1280, 720);
    vulkan_create_swapchain_renderpass(vk);
    vulkan_create_swapchain_framebuffers(vk);

    VkCommandPoolCreateInfo command_pool_info = {0};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queueFamilyIndex = vk->queue_indices.graphics;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(vk->device, &command_pool_info, VK_NULL_HANDLE, &vk->command_pool);
    vkCreateCommandPool(vk->device, &command_pool_info, VK_NULL_HANDLE, &vk->present_thread_command_pool);

    {
        VkCommandBufferAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = vk->command_pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        vkAllocateCommandBuffers(vk->device, &alloc_info, &vk->command_buffer);
    }

    {
        VkCommandBufferAllocateInfo alloc_info = {0};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = vk->present_thread_command_pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        vkAllocateCommandBuffers(vk->device, &alloc_info, &vk->present_thread_command_buffer);
    }

    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(vk->device, &semaphore_info, VK_NULL_HANDLE, &vk->submission_complete);
    vkCreateSemaphore(vk->device, &semaphore_info, VK_NULL_HANDLE, &vk->image_acquired);
    vkCreateSemaphore(vk->device, &semaphore_info, VK_NULL_HANDLE, &vk->present_thread_semaphore);

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(vk->device, &fence_info, NULL, &vk->render_fence);
    vkCreateFence(vk->device, &fence_info, VK_NULL_HANDLE, &vk->present_thread_fence);
    // transition our images
    {
        vkResetCommandPool(vk->device, vk->command_pool, 0);

        VkCommandBufferBeginInfo begin_info = {0};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(vk->command_buffer, &begin_info);

        transition_layout(vk, vk->render_vram, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        transition_layout(vk, vk->sample_vram, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        transition_layout(vk, vk->display_vram, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        //transition_layout(vk->sample_vram, VK_IMAGE_LAYOUT_UNDEFINED, vk_image_layout)
        vkEndCommandBuffer(vk->command_buffer);

        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &vk->command_buffer;
        vkQueueSubmit(vk->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(vk->graphics_queue);
    }

    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    //sampler_info.minFilter = sampler_info.magFilter = VK_FILTER_LINEAR;
    vkCreateSampler(vk->device, &sampler_info, NULL, &vk->sampler);

    vk->descriptor_pool = vulkan_create_descriptor_pool(vk, 4);

    VkDescriptorSetLayoutBinding layout_binding = {0};
    layout_binding.binding = 0;
    layout_binding.descriptorCount = 1;
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layout_binding.pImmutableSamplers = &vk->sampler;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &layout_binding;

    vkCreateDescriptorSetLayout(vk->device, &layout_info, VK_NULL_HANDLE, &vk->descriptor_set_layout);

    VkDescriptorSet sets[2];
    VkDescriptorSetLayout layouts[] = {vk->descriptor_set_layout, vk->descriptor_set_layout};
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {0};
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.descriptorPool = vk->descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 2;
    descriptor_alloc_info.pSetLayouts = layouts;
    vkAllocateDescriptorSets(vk->device, &descriptor_alloc_info, sets);
    vk->descriptor_set = sets[0];
    vk->fullscreen_descriptor_set = sets[1];

    VkDescriptorImageInfo vram_descriptor = {0};
    vram_descriptor.imageView = vk->sample_vram_view;
    vram_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vram_descriptor.sampler = vk->sampler;

    VkDescriptorImageInfo fullscreen_descriptor = {0};
    fullscreen_descriptor.imageView = vk->display_vram_view;
    fullscreen_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fullscreen_descriptor.sampler = vk->sampler;

    VkWriteDescriptorSet descriptor_writes[2] = {0};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = vk->descriptor_set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &vram_descriptor;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = vk->fullscreen_descriptor_set;
    descriptor_writes[1].dstBinding = 0;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].pImageInfo = &fullscreen_descriptor;

    vkUpdateDescriptorSets(vk->device, 2, descriptor_writes, 0, NULL);

    struct vulkan_pipeline_config vram_pipeline = {0};
    vram_pipeline.dynamic_scissor = VK_TRUE;

    VkPushConstantRange pushconst = {0};
    pushconst.size = sizeof(u32);
    pushconst.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    vram_pipeline.push_constants = &pushconst;
    vram_pipeline.push_constants_count = 1;
    vram_pipeline.set_layout_count = 1;
    vram_pipeline.set_layouts = &vk->descriptor_set_layout;
    VkVertexInputAttributeDescription vertex_attributes[5];
    {
        struct shader_obj shaders[2];
        load_shader_from_file(vk, "shader/vertshader.spv", VK_SHADER_STAGE_VERTEX_BIT, &shaders[0]);
        load_shader_from_file(vk, "shader/fragshader.spv", VK_SHADER_STAGE_FRAGMENT_BIT, &shaders[1]);

        vram_pipeline.shaders = shaders;
        vram_pipeline.shader_stage_count = 2;

        vram_pipeline.vertex_stride = sizeof(render_vertex);

        VkVertexInputAttributeDescription pos = {0};
        pos.location = 0;
        pos.binding = 0;
        pos.format = VK_FORMAT_R16G16_SINT;
        pos.offset = offsetof(render_vertex, pos);

        VkVertexInputAttributeDescription uv = {0};
        uv.location = 1;
        uv.binding = 0;
        uv.format = VK_FORMAT_R32G32_SFLOAT;
        uv.offset = offsetof(render_vertex, uv);

        VkVertexInputAttributeDescription texpage = {0};
        texpage.location = 2;
        texpage.binding = 0;
        texpage.format = VK_FORMAT_R32G32_SINT;
        texpage.offset = offsetof(render_vertex, texture_page);

        VkVertexInputAttributeDescription clut = {0};
        clut.location = 3;
        clut.binding = 0;
        clut.format = VK_FORMAT_R32G32_SINT;
        clut.offset = offsetof(render_vertex, clut);

        VkVertexInputAttributeDescription color = {0};
        color.location = 4;
        color.binding = 0;
        color.format = VK_FORMAT_R8G8B8A8_UNORM;
        color.offset = offsetof(render_vertex, color);

        vertex_attributes[0] = pos;
        vertex_attributes[1] = uv;
        vertex_attributes[2] = texpage;
        vertex_attributes[3] = clut;
        vertex_attributes[4] = color;
        vram_pipeline.vertex_attributes = vertex_attributes;
        vram_pipeline.vertex_attribute_count = ARRAYCOUNT(vertex_attributes);
    }
    vram_pipeline.renderpass = vk->renderpass;
    pipeline_create(vk, &vram_pipeline, &vk->pipeline, &vk->pipeline_layout);

    vkDestroyShaderModule(vk->device, vram_pipeline.shaders[0].module, VK_NULL_HANDLE);
    vkDestroyShaderModule(vk->device, vram_pipeline.shaders[1].module, VK_NULL_HANDLE);

    struct vulkan_pipeline_config fullscreen_pipeline = {0};
    fullscreen_pipeline.dynamic_scissor = 1;
    fullscreen_pipeline.dynamic_viewport = 1;
    fullscreen_pipeline.renderpass = vk->swapchain_renderpass;
    fullscreen_pipeline.set_layout_count = 1;
    fullscreen_pipeline.set_layouts = &vk->descriptor_set_layout;
    struct shader_obj shaders[2];
    load_shader_from_file(vk, "shader/fullscreen_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT, &shaders[0]);
    load_shader_from_file(vk, "shader/fullscreen_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT, &shaders[1]);
    fullscreen_pipeline.shader_stage_count = 2;
    fullscreen_pipeline.shaders = shaders;

    pipeline_create(vk, &fullscreen_pipeline, &vk->fullscreen_pipeline, &vk->fullscreen_pipeline_layout);

    vkDestroyShaderModule(vk->device, fullscreen_pipeline.shaders[0].module, VK_NULL_HANDLE);
    vkDestroyShaderModule(vk->device, fullscreen_pipeline.shaders[1].module, VK_NULL_HANDLE);

    return 1;
}

static inline u32 staging_buffer_write(struct vulkan_context *vk, void* data, u32 size)
{
    u32 result = vk->staging_buffer_offset;
    memcpy(((u8*)vk->staging_data + vk->staging_buffer_offset), data, size);
    vk->staging_buffer_offset += size;
    return result;
}

static inline void vulkan_begin_renderpass_instance(struct vulkan_context *vk)
{
    if (vk->in_renderpass)
        return;
    
    VkExtent2D image_extent = {.width = 1024, .height = 512};

    VkRenderPassBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass = vk->renderpass;
    begin_info.framebuffer = vk->framebuffer;
    begin_info.renderArea.extent = image_extent;

    vkCmdBeginRenderPass(vk->command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vk->in_renderpass = 1;
}

static inline void vulkan_end_renderpass_instance(struct vulkan_context *vk)
{
    if (vk->in_renderpass)
    {
        vkCmdEndRenderPass(vk->command_buffer);
        vk->in_renderpass = 0;
    }
}

static void vulkan_flush_commands(renderer_interface *renderer)
{
    struct vulkan_context *vk = (struct vulkan_context *)renderer;
    // no commands have been recorded
    if (renderer->commands_at == renderer->render_commands)
        return;

    //vk->staging_buffer_offset = 0;
    u32 staging_buffer_offset = 0;

    //vk->renderer.dirty_region_count = 0;

    //vkWaitForFences(vk->device, 1, &vk->render_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(vk->device, 1, &vk->render_fence);

    vkResetCommandPool(vk->device, vk->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vk->command_buffer, &begin_info);

    u32 vertex_offset = 0;
    vkCmdBindPipeline(vk->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline);
    vkCmdBindDescriptorSets(vk->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipeline_layout, 0, 1, &vk->descriptor_set, 0, NULL);

    VkDeviceSize vb_offsets[] = {0};
    VkBuffer vertex_buffers[] = {vk->vertex_buffer};
    vkCmdBindVertexBuffers(vk->command_buffer, 0, 1, vertex_buffers, vb_offsets);
    b8 scissor_set = 0;
    u8 *at = renderer->render_commands;
    for (u32 i = 0; i < renderer->render_commands_count; ++i)
    {
        struct render_command_header *header = (struct render_command_header *)at;
        switch (header->type)
        {
        case RENDER_COMMAND_DRAW_SHADED_PRIMITIVE:
        case RENDER_COMMAND_DRAW_TEXTURED_PRIMITIVE:
        {
            at += sizeof(struct render_command_draw);
            struct render_command_draw *cmd = (struct render_command_draw *)header;
        
            vulkan_begin_renderpass_instance(vk);

            vkCmdPushConstants(vk->command_buffer, vk->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32), &cmd->texture_mode);
            vkCmdDraw(vk->command_buffer, cmd->vertex_count, 1, cmd->vertex_array_offset, 0);
            SY_ASSERT(scissor_set);
        } break;
        case RENDER_COMMAND_SET_DRAW_AREA:
        {
            scissor_set = 1;
            at += sizeof(struct render_command_set_draw_area);
            struct render_command_set_draw_area *cmd = (struct render_command_set_draw_area *)header;
            VkRect2D scissor = {0};
            scissor.extent.width = cmd->draw_area.right - cmd->draw_area.left;
            scissor.extent.height = cmd->draw_area.bottom - cmd->draw_area.top;
            scissor.offset.x = cmd->draw_area.left;
            scissor.offset.y = cmd->draw_area.top;
            vkCmdSetScissor(vk->command_buffer, 0, 1, &scissor);
        } break;
        case RENDER_COMMAND_FLUSH_VRAM:
        {
            at += sizeof(struct render_command_flush_vram);

            vulkan_end_renderpass_instance(vk);

            transition_layout(vk, vk->sample_vram, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy image_copy = {0};
            image_copy.extent.depth = 1;
            image_copy.extent.width = 1024;
            image_copy.extent.height = 512;
            image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_copy.srcSubresource.layerCount = 1;
            image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_copy.dstSubresource.layerCount = 1;
            vkCmdCopyImage(vk->command_buffer, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

            transition_layout(vk, vk->sample_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } break;
        case RENDER_COMMAND_TRANSFER_CPU_TO_VRAM:
        {
            at += sizeof(struct render_command_transfer);
            struct render_command_transfer *cmd = (struct render_command_transfer *)header;
            
            vulkan_end_renderpass_instance(vk);

            transition_layout(vk, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            void *dst = (u8 *)vk->staging_data + staging_buffer_offset;
            size_t size = cmd->width * cmd->height * 2;
            memcpy(dst, (void *)cmd->buffer, size);
            VkBufferImageCopy buffer_copy = {0};
            buffer_copy.bufferOffset = staging_buffer_offset;
            buffer_copy.imageExtent.depth = 1;
            buffer_copy.imageExtent.width = cmd->width;
            buffer_copy.imageExtent.height = cmd->height;
            buffer_copy.imageOffset.x = cmd->x;
            buffer_copy.imageOffset.y = cmd->y;
            buffer_copy.imageSubresource.layerCount = 1;
            buffer_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vkCmdCopyBufferToImage(vk->command_buffer, vk->staging_buffer, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_copy);

            transition_layout(vk, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            staging_buffer_offset += size;
        } break;
        case RENDER_COMMAND_TRANSFER_VRAM_TO_CPU:
        {
            at += sizeof(struct render_command_transfer);
            struct render_command_transfer *cmd = (struct render_command_transfer *)header;
            vulkan_end_renderpass_instance(vk);

            VkBufferImageCopy image_copy = {0};
            image_copy.bufferOffset = staging_buffer_offset;
            image_copy.imageExtent.depth = 1;
            image_copy.imageExtent.width = cmd->width;
            image_copy.imageExtent.height = cmd->height;
            image_copy.imageOffset.x = cmd->x;
            image_copy.imageOffset.y = cmd->y;
            image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_copy.imageSubresource.layerCount = 1;

            vkCmdCopyImageToBuffer(vk->command_buffer, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk->staging_buffer, 1, &image_copy);
            // TODO: only somewhat works since we flush commands right after a vram->cpu command
            *cmd->buffer = (u8 *)vk->staging_data + staging_buffer_offset;
            staging_buffer_offset += (cmd->width * cmd->height * 2);
        } break;
        case RENDER_COMMAND_TRANSFER_VRAM_TO_VRAM:
        {
            SY_ASSERT(0);
        } break;
        default:
            break;
        }
    }

    vulkan_end_renderpass_instance(vk);

    vk->renderer.render_commands_count = 0;
    vk->renderer.commands_at = vk->renderer.render_commands;
    vk->renderer.total_vertex_count = 0;
    
    vkEndCommandBuffer(vk->command_buffer);

    VkCommandBuffer cmdbuffers[] = {vk->command_buffer};

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = cmdbuffers;
    
    EnterCriticalSection(&vk->critical_section);

    vkQueueSubmit(vk->graphics_queue, 1, &submit_info, vk->render_fence);

    LeaveCriticalSection(&vk->critical_section);

    vkWaitForFences(vk->device, 1, &vk->render_fence, VK_TRUE, UINT64_MAX);
}

void present_frame(renderer_interface *renderer)
{
    struct vulkan_context *vk = (struct vulkan_context *)renderer;
    VkFence fences[] = {vk->present_thread_fence};

    vkWaitForFences(vk->device, 1, fences, VK_TRUE, UINT64_MAX);

    u32 image_index;
    vkAcquireNextImageKHR(vk->device, vk->swapchain.handle, UINT64_MAX, vk->image_acquired, VK_NULL_HANDLE, &image_index);

    vkResetFences(vk->device, 1, fences);
    vkResetCommandPool(vk->device, vk->present_thread_command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vk->present_thread_command_buffer, &begin_info);

    VkRenderPassBeginInfo rp_begin = {0};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderArea.extent.width = vk->swapchain.width;
    rp_begin.renderArea.extent.height = vk->swapchain.height;
    rp_begin.framebuffer = vk->swapchain.framebuffers[image_index];
    rp_begin.renderPass = vk->swapchain_renderpass;
    vkCmdBeginRenderPass(vk->present_thread_command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport fullscreen_viewport = {.width = vk->swapchain.width, vk->swapchain.height, .maxDepth = 1.0f};
    vkCmdSetViewport(vk->present_thread_command_buffer, 0, 1, &fullscreen_viewport);

    VkRect2D fullscreen_scissor = {.extent = {.width = vk->swapchain.width, .height =  vk->swapchain.height}};
    vkCmdSetScissor(vk->present_thread_command_buffer, 0, 1, &fullscreen_scissor);

    vkCmdBindPipeline(vk->present_thread_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->fullscreen_pipeline);
    vkCmdBindDescriptorSets(vk->present_thread_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->fullscreen_pipeline_layout, 0, 1, &vk->fullscreen_descriptor_set, 0, VK_NULL_HANDLE);

    vkCmdDraw(vk->present_thread_command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(vk->present_thread_command_buffer);

    vkEndCommandBuffer(vk->present_thread_command_buffer);

    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &vk->submission_complete;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &vk->image_acquired;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk->present_thread_command_buffer;

    VkSwapchainKHR swapchains[] = {vk->swapchain.handle};
    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &vk->submission_complete;

    EnterCriticalSection(&vk->critical_section);

    vkQueueSubmit(vk->graphics_queue, 1, &submit_info, vk->present_thread_fence);
    vkQueuePresentKHR(vk->graphics_queue, &present_info);

    LeaveCriticalSection(&vk->critical_section);
}

void vulkan_update_display(renderer_interface *renderer)
{
    struct vulkan_context *vk = (struct vulkan_context *)renderer;
    
    vkResetFences(vk->device, 1, &vk->render_fence);
    vkResetCommandPool(vk->device, vk->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vk->command_buffer, &begin_info);

    transition_layout(vk, vk->display_vram, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy image_copy = {0};
    image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.srcSubresource.layerCount = 1;
    image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.dstSubresource.layerCount = 1;
    image_copy.extent.depth = 1;
    image_copy.extent.width = VRAM_WIDTH;
    image_copy.extent.height = VRAM_HEIGHT;

    vkCmdCopyImage(vk->command_buffer, vk->render_vram, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk->display_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

    transition_layout(vk, vk->display_vram, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(vk->command_buffer);

    VkCommandBuffer cmdbuffers[] = {vk->command_buffer};
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = cmdbuffers;
    
    EnterCriticalSection(&vk->critical_section);

    vkQueueSubmit(vk->graphics_queue, 1, &submit_info, vk->render_fence);

    LeaveCriticalSection(&vk->critical_section);

    vkWaitForFences(vk->device, 1, &vk->render_fence, VK_TRUE, UINT64_MAX);
}

void vulkan_destroy_context(renderer_interface *renderer)
{
    struct vulkan_context *vk = (struct vulkan_context *)renderer;
    vkDeviceWaitIdle(vk->device);

    vkDestroySemaphore(vk->device, vk->image_acquired, NULL);
    vkDestroySemaphore(vk->device, vk->submission_complete, NULL);
    vkDestroyFence(vk->device, vk->render_fence, NULL);

    vkDestroyCommandPool(vk->device, vk->command_pool, NULL);

    vkDestroyRenderPass(vk->device, vk->renderpass, NULL);
    vkDestroyFramebuffer(vk->device, vk->framebuffer, NULL);

    vkDestroyBuffer(vk->device, vk->staging_buffer, NULL);
    vkDestroyBuffer(vk->device, vk->vertex_buffer, NULL);

    vkDestroyImage(vk->device, vk->render_vram, NULL);
    vkDestroyImage(vk->device, vk->sample_vram, NULL);
    vkDestroyImageView(vk->device, vk->render_vram_view, NULL);
    vkDestroyImageView(vk->device, vk->sample_vram_view, NULL);

    vkUnmapMemory(vk->device, vk->staging_buffer_memory);
    
    vkFreeMemory(vk->device, vk->staging_buffer_memory, NULL);    

    vkDestroySwapchainKHR(vk->device, vk->swapchain.handle, NULL);

    vkDestroyPipelineLayout(vk->device, vk->pipeline_layout, NULL);
    vkDestroyPipeline(vk->device, vk->pipeline, NULL);

    vkDestroySampler(vk->device, vk->sampler, NULL);
    vkDestroyDescriptorSetLayout(vk->device, vk->descriptor_set_layout, NULL);
    vkDestroyDescriptorPool(vk->device, vk->descriptor_pool, NULL);
    
    vkDestroyDevice(vk->device, NULL);

    vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);
    vkDestroyInstance(vk->instance, NULL);

    DeleteCriticalSection(&vk->critical_section);

    VirtualFree(renderer->render_commands, 0, MEM_DECOMMIT | MEM_RELEASE);
    VirtualFree(vk, 0, MEM_DECOMMIT | MEM_RELEASE);
}

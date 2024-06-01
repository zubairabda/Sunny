#if 0

static inline void allocate_buffer(VkBuffer buffer, struct gpu_allocation* memory)
{
    VkResult res;
    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(g_context->device, buffer, &mem_req);

    size_t mask = mem_req.alignment - 1;
    memory->marker = (memory->marker + mask) & ~(mask);

    res = vkBindBufferMemory(g_context->device, buffer, memory->handle, memory->marker);
    memory->marker += mem_req.size;
}

static inline void allocate_image(VkImage image, struct gpu_allocation* memory)
{
    VkResult res;
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(g_context->device, image, &mem_req);

    size_t mask = mem_req.alignment - 1;
    memory->marker = (memory->marker + mask) & ~(mask);

    res = vkBindImageMemory(g_context->device, image, memory->handle, memory->marker);
    memory->marker += mem_req.size;
}

static inline u32 get_memory_type_index(VkPhysicalDeviceMemoryProperties* memory_properties, VkMemoryPropertyFlags properties)
{
    for (u32 i = 0; i < memory_properties->memoryTypeCount; ++i)
    {
        u32 type = memory_properties->memoryTypes[i].propertyFlags;
        if ((type & properties) == properties)
        {
            return i;
        }
    }
    return 0xffffffff;
}
#endif

static void transition_layout(struct vulkan_context *vk, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{    
    VkAccessFlags src_access = 0, dst_access = 0;
    VkPipelineStageFlags src_stage = 0, dst_stage = 0;

    VkImageMemoryBarrier image_barrier = {0};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.image = image;
    image_barrier.oldLayout = old_layout;
    image_barrier.newLayout = new_layout;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.layerCount = 1;
    image_barrier.subresourceRange.levelCount = 1;

    switch (old_layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        src_access = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        src_access = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    default:
        src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        break;
    }

    switch (new_layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        dst_access = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        dst_access = VK_ACCESS_SHADER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        dst_access = VK_ACCESS_TRANSFER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    default:
        printf("WARN: new_layout using top of pipe dst stage\n");
        dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        break;
    }

    image_barrier.srcAccessMask = src_access;
    image_barrier.dstAccessMask = dst_access;

    vkCmdPipelineBarrier(vk->command_buffer, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &image_barrier);
}

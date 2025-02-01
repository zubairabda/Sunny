static void texture_init(VkImage *image, VkImageView *image_view, u32 width, u32 height,
                                VkFormat format, VkImageUsageFlags usage, VkDeviceMemory *image_memory)
{
    VkExtent3D extent = {.width = width, .height = height, .depth = 1};
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.flags = 0;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = extent;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(vk->device, &image_info, NULL, image);

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk->device, *image, &requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.memoryTypeIndex = vulkan_find_memory_type(vk->physical_device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.memoryTypeBits);
    alloc_info.allocationSize = requirements.size;
    
    vkAllocateMemory(vk->device, &alloc_info, VK_NULL_HANDLE, image_memory);

    vkBindImageMemory(vk->device, *image, *image_memory, 0);

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
#if 0
    view_info.components.r = VK_COMPONENT_SWIZZLE_B;
    view_info.components.g = VK_COMPONENT_SWIZZLE_G;
    view_info.components.b = VK_COMPONENT_SWIZZLE_R;
    view_info.components.a = VK_COMPONENT_SWIZZLE_A;
#endif
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.layerCount = 1;
    view_info.subresourceRange.levelCount = 1;
    vkCreateImageView(vk->device, &view_info, NULL, image_view);
}

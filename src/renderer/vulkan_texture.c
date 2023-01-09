static inline void texture_init(VkImage* image, VkImageView* image_view, u32 width, u32 height,
                                VkFormat format, VkImageUsageFlags usage, struct gpu_allocation* memory)
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

    VkResult res = vkCreateImage(g_context->device, &image_info, NULL, image);

    allocate_image(*image, memory);

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
    vkCreateImageView(g_context->device, &view_info, NULL, image_view);
}
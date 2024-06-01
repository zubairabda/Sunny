static inline VkDescriptorPool vulkan_create_descriptor_pool(struct vulkan_context *vk, u32 max_sets)
{
    VkDescriptorPoolSize pool_sizes[2] = {0};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = 8;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    
    VkDescriptorPool pool;
    VkResult res = vkCreateDescriptorPool(vk->device, &pool_info, NULL, &pool);
    if (res != VK_SUCCESS)
    {
        printf("Failed to create a descriptor pool\n");
        return VK_NULL_HANDLE;
    }
    return pool;
}

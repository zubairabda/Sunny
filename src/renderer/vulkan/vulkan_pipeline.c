struct shader_obj
{
    VkShaderStageFlags stage;
    u32 reserved;
    VkShaderModule module;
};

struct vulkan_pipeline_config
{
    VkRenderPass renderpass;
    b8 dynamic_scissor;
    b8 dynamic_viewport;
    u32 shader_stage_count;
    u32 set_layout_count;
    u32 push_constants_count;
    u32 vertex_attribute_count;
    u32 vertex_stride;
    u32 reserved;
    struct shader_obj *shaders;
    VkDescriptorSetLayout *set_layouts;
    VkPushConstantRange *push_constants;
    VkVertexInputAttributeDescription *vertex_attributes;
};

static b8 load_shader_from_file(struct vulkan_context *vk, char *path, VkShaderStageFlags stage, struct shader_obj *shader)
{
    b8 result = 0;
    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u32 *buff = malloc(size);
    if (!buff)
    {
        printf("Failed to allocate file buffer\n");
        goto exit;
    }
    if (fread(buff, 1, size, f) != size)
    {
        printf("Failed to read from file\n");
        goto exit_free;
    }
    fclose(f);

    VkShaderModuleCreateInfo shader_info = {0};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = size;
    shader_info.pCode = buff;
    VkResult res = vkCreateShaderModule(vk->device, &shader_info, NULL, &shader->module);
    if (res != VK_SUCCESS)
    {
        printf("Failed to create shader module, from path: %s\n", path);
        goto exit_free;
    }
    shader->stage = stage;
    result = 1;
exit_free:
    free(buff);
exit:
    return result;
}

static void pipeline_create(struct vulkan_context *vk, struct vulkan_pipeline_config *config, VkPipeline *out_pipeline, VkPipelineLayout *out_pipeline_layout)
{
    VkResult res;

    VkPipelineInputAssemblyStateCreateInfo input_info = {0};
    input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_info.primitiveRestartEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    VkPipelineDynamicStateCreateInfo dynamic_info = {0};
    VkPipelineViewportStateCreateInfo viewport_info = {0};

    VkVertexInputBindingDescription vertex_binding = {0};
    vertex_binding.binding = 0;
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertex_binding.stride = config->vertex_stride;

    // default scissor and viewport
    VkRect2D scissor = {0};
    VkViewport viewport = {0};
    scissor.extent.width = 1024;
    scissor.extent.height = 512;
    viewport.maxDepth = 1.0f;
    viewport.width = 1024.0f;
    viewport.height = 512.0f;

    VkPipelineRasterizationStateCreateInfo raster_info = {0};
    raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    raster_info.cullMode = VK_CULL_MODE_NONE;
    raster_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms_info = {0};
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms_info.sampleShadingEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo blend_info = {0};
    blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_info.attachmentCount = 1;
    blend_info.pAttachments = &blend_attachment;

    VkDynamicState dynamic_states[2];
    
    SY_ASSERT(config->shader_stage_count <= 8);
    VkPipelineShaderStageCreateInfo shader_infos[8] = {0};
    for (u32 i = 0; i < config->shader_stage_count; ++i)
    {
        shader_infos[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_infos[i].pName = "main";
        shader_infos[i].module = config->shaders[i].module;
        shader_infos[i].stage = config->shaders[i].stage;
    }

    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config->vertex_attribute_count)
    {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_binding;
        vertex_input.vertexAttributeDescriptionCount = config->vertex_attribute_count;
        vertex_input.pVertexAttributeDescriptions = config->vertex_attributes;
    }
    
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.scissorCount = 1;

    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

    if (config->dynamic_scissor)
    {
        dynamic_states[dynamic_info.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
    }
    else
    {
        viewport_info.pScissors = &scissor;
    }

    if (config->dynamic_viewport)
    {
        dynamic_states[dynamic_info.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
    }
    else
    {
        viewport_info.pViewports = &viewport;
    }

    dynamic_info.pDynamicStates = dynamic_states;

    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pPushConstantRanges = config->push_constants;
    layout_info.pushConstantRangeCount = config->push_constants_count;
    layout_info.setLayoutCount = config->set_layout_count;
    layout_info.pSetLayouts = config->set_layouts;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    res = vkCreatePipelineLayout(vk->device, &layout_info, NULL, &pipeline_layout);

    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = config->shader_stage_count;
    pipeline_info.pStages = shader_infos;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &raster_info;
    pipeline_info.pMultisampleState = &ms_info;
    pipeline_info.pColorBlendState = &blend_info;
    pipeline_info.pDynamicState = dynamic_info.dynamicStateCount ? &dynamic_info : NULL;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = config->renderpass;

    res = vkCreateGraphicsPipelines(vk->device, NULL, 1, &pipeline_info, NULL, out_pipeline);
    *out_pipeline_layout = pipeline_layout;
}

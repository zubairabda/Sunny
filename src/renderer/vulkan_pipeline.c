struct shader_program
{
    VkShaderStageFlags shader_stage;
    u32 reserved;
    VkShaderModule shader;
    u32 push_constant_size;
};

struct pipeline_config
{
    struct shader_program vertex_shader;
    struct shader_program fragment_shader;
    VkPipelineLayout pipeline_layout;
    VkRenderPass renderpass;
    b8 dynamic_scissor;
    b8 dynamic_viewport;
    b8 vertex_input;
};

static void build_pipeline_layout(struct pipeline_config* settings, VkDescriptorSetLayout* set_layout)
{
    VkPushConstantRange push_constants[2] = {0};
    u32 push_constants_count = 0;
    if (settings->vertex_shader.push_constant_size)
    {
        push_constants[push_constants_count].offset = 0;
        push_constants[push_constants_count].size = settings->vertex_shader.push_constant_size;
        push_constants[push_constants_count].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        ++push_constants_count;
    }
    if (settings->fragment_shader.push_constant_size)
    {
        push_constants[push_constants_count].offset = 0;
        push_constants[push_constants_count].size = settings->fragment_shader.push_constant_size;
        push_constants[push_constants_count].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        ++push_constants_count;
    }
    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (set_layout)
    {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = set_layout;
    }
    layout_info.pushConstantRangeCount = push_constants_count;
    layout_info.pPushConstantRanges = push_constants;

    VkResult res = vkCreatePipelineLayout(g_context->device, &layout_info, NULL, &settings->pipeline_layout);
}

static struct shader_program prepare_shader(char* path, VkShaderStageFlags stage)
{
    struct shader_program result = {0};
    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u32* buff = malloc(size);
    if (fread(buff, 1, size, f) != size)
    {
        printf("Failed to read from file");
        return result;
    }
    fclose(f);

    VkShaderModuleCreateInfo shader_info = {0};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = size;
    shader_info.pCode = buff;
    vkCreateShaderModule(g_context->device, &shader_info, NULL, &result.shader);
    free(buff);
    result.shader_stage = stage;
    return result;
}

static void initialize_pipeline(struct pipeline_config* config, VkPipeline* pipeline)
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
    vertex_binding.stride = sizeof(Vertex);

    VkVertexInputAttributeDescription pos = {0};
    pos.location = 0;
    pos.binding = 0;
    pos.format = VK_FORMAT_R16G16_SINT;
    pos.offset = offsetof(Vertex, pos);

    VkVertexInputAttributeDescription uv = {0};
    uv.location = 1;
    uv.binding = 0;
    uv.format = VK_FORMAT_R32G32_SFLOAT;
    uv.offset = offsetof(Vertex, uv);

    VkVertexInputAttributeDescription texpage = {0};
    texpage.location = 2;
    texpage.binding = 0;
    texpage.format = VK_FORMAT_R32G32_SINT;
    texpage.offset = offsetof(Vertex, texture_page);

    VkVertexInputAttributeDescription clut = {0};
    clut.location = 3;
    clut.binding = 0;
    clut.format = VK_FORMAT_R32G32_SINT;
    clut.offset = offsetof(Vertex, clut);

    VkVertexInputAttributeDescription color = {0};
    color.location = 4;
    color.binding = 0;
    color.format = VK_FORMAT_R8G8B8_UNORM;//VK_FORMAT_R32G32B32A32_SFLOAT;
    color.offset = offsetof(Vertex, color);

    VkVertexInputAttributeDescription vtx_attrib[] = {pos, uv, texpage, clut, color};
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
    
    VkPipelineShaderStageCreateInfo vertshader_info = {0};
    VkPipelineShaderStageCreateInfo fragshader_info = {0};
        
    vertshader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertshader_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertshader_info.module = config->vertex_shader.shader;
    vertshader_info.pName = "main";

    fragshader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragshader_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragshader_info.module = config->fragment_shader.shader;
    fragshader_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vertshader_info, fragshader_info};

    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config->vertex_input)
    {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_binding;
        vertex_input.vertexAttributeDescriptionCount = ARRAYCOUNT(vtx_attrib);
        vertex_input.pVertexAttributeDescriptions = vtx_attrib;   
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

    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &raster_info;
    pipeline_info.pMultisampleState = &ms_info;
    pipeline_info.pColorBlendState = &blend_info;
    pipeline_info.pDynamicState = dynamic_info.dynamicStateCount ? &dynamic_info : NULL;
    pipeline_info.layout = config->pipeline_layout;
    pipeline_info.renderPass = config->renderpass;

    res = vkCreateGraphicsPipelines(g_context->device, NULL, 1, &pipeline_info, NULL, pipeline);

    vkDestroyShaderModule(g_context->device, config->vertex_shader.shader, NULL);
    vkDestroyShaderModule(g_context->device, config->fragment_shader.shader, NULL);
}
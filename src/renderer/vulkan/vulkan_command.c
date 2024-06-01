#if 0

static inline void begin_command_buffer(void)
{
    vkResetCommandPool(g_context->device, g_context->command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(g_context->command_buffer, &begin_info);
}

static inline void execute_command_buffer(b8 present_swapchain, u32 image_index)
{
    vkEndCommandBuffer(g_context->command_buffer);

    VkPipelineStageFlags pipeline_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (present_swapchain)
    {
        submit_info.pWaitDstStageMask = pipeline_stages;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &g_context->image_acquired;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &g_context->submission_complete;
    } 
    
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &g_context->command_buffer;

    vkQueueSubmit(g_context->graphics_queue, 1, &submit_info, g_context->render_fence);
    
    if (present_swapchain)
    {
        VkPresentInfoKHR present_info = {0};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &g_context->submission_complete;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &g_context->swapchain;
        present_info.pImageIndices = &image_index;
        
        vkQueuePresentKHR(g_context->graphics_queue, &present_info);
    }
}

#endif

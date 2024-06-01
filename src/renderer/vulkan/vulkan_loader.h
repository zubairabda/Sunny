#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define VK_FUNC_PTR(func) PFN_##func func;

#define VK_INSTANCE_FUNCTION(MACRO) \
MACRO(vkEnumeratePhysicalDevices) \
MACRO(vkCreateDevice) \
MACRO(vkDestroyInstance) \
MACRO(vkGetPhysicalDeviceProperties) \
MACRO(vkGetPhysicalDeviceFeatures) \
MACRO(vkGetPhysicalDeviceFeatures2) \
MACRO(vkEnumerateDeviceExtensionProperties) \
MACRO(vkGetPhysicalDeviceQueueFamilyProperties) \
MACRO(vkGetDeviceProcAddr) \
MACRO(vkCreateWin32SurfaceKHR) \
MACRO(vkDestroySurfaceKHR) \
MACRO(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
MACRO(vkGetPhysicalDeviceSurfaceSupportKHR) \
MACRO(vkGetPhysicalDeviceMemoryProperties) \
MACRO(vkGetPhysicalDeviceFormatProperties)

#define VK_DEVICE_FUNCTION(MACRO) \
MACRO(vkCmdDraw) \
MACRO(vkCreateSwapchainKHR) \
MACRO(vkCreateCommandPool) \
MACRO(vkAllocateCommandBuffers) \
MACRO(vkCreateSwapchainKHR) \
MACRO(vkCreateCommandPool) \
MACRO(vkAllocateCommandBuffers) \
MACRO(vkResetCommandPool) \
MACRO(vkBeginCommandBuffer) \
MACRO(vkEndCommandBuffer) \
MACRO(vkQueueSubmit) \
MACRO(vkGetDeviceQueue) \
MACRO(vkCreateSemaphore) \
MACRO(vkCreateFence) \
MACRO(vkWaitForFences) \
MACRO(vkResetFences) \
MACRO(vkQueuePresentKHR) \
MACRO(vkAcquireNextImageKHR) \
MACRO(vkGetSwapchainImagesKHR) \
MACRO(vkCreateRenderPass) \
MACRO(vkCreateFramebuffer) \
MACRO(vkCmdBeginRenderPass) \
MACRO(vkCmdEndRenderPass) \
MACRO(vkCreateImageView) \
MACRO(vkDestroySwapchainKHR) \
MACRO(vkDestroyImage) \
MACRO(vkDestroyImageView) \
MACRO(vkDestroyFramebuffer) \
MACRO(vkDeviceWaitIdle) \
MACRO(vkDestroyRenderPass) \
MACRO(vkDestroyCommandPool) \
MACRO(vkDestroyDevice) \
MACRO(vkDestroySemaphore) \
MACRO(vkDestroyFence) \
MACRO(vkDestroyBuffer) \
MACRO(vkDestroyShaderModule) \
MACRO(vkDestroyPipeline) \
MACRO(vkDestroyPipelineLayout) \
MACRO(vkDestroySampler) \
MACRO(vkDestroyDescriptorPool) \
MACRO(vkDestroyDescriptorSetLayout) \
MACRO(vkFreeMemory) \
MACRO(vkCreateGraphicsPipelines) \
MACRO(vkCreateShaderModule) \
MACRO(vkCreatePipelineLayout) \
MACRO(vkAllocateDescriptorSets) \
MACRO(vkCreateDescriptorPool) \
MACRO(vkCreateDescriptorSetLayout) \
MACRO(vkCreateBuffer) \
MACRO(vkAllocateMemory) \
MACRO(vkBindBufferMemory) \
MACRO(vkCreateImage) \
MACRO(vkBindImageMemory) \
MACRO(vkGetBufferMemoryRequirements) \
MACRO(vkGetImageMemoryRequirements) \
MACRO(vkMapMemory) \
MACRO(vkUnmapMemory) \
MACRO(vkCmdBindVertexBuffers) \
MACRO(vkCmdBindPipeline) \
MACRO(vkCmdSetViewport) \
MACRO(vkCmdSetScissor) \
MACRO(vkCmdBindDescriptorSets) \
MACRO(vkUpdateDescriptorSets) \
MACRO(vkCmdPipelineBarrier) \
MACRO(vkCmdCopyBufferToImage) \
MACRO(vkQueueWaitIdle) \
MACRO(vkCreateSampler) \
MACRO(vkCmdPushConstants) \
MACRO(vkCmdBlitImage) \
MACRO(vkCmdCopyImageToBuffer) \
MACRO(vkCmdCopyImage) \
MACRO(vkCmdDispatch)

// declare all function pointers
VK_INSTANCE_FUNCTION(VK_FUNC_PTR)
VK_DEVICE_FUNCTION(VK_FUNC_PTR)

VK_FUNC_PTR(vkGetInstanceProcAddr)
VK_FUNC_PTR(vkCreateInstance)
VK_FUNC_PTR(vkEnumerateInstanceExtensionProperties)

#define VK_LOAD_INSTANCE_FUNC(func) func = (PFN_##func)vkGetInstanceProcAddr(VK_INSTANCE_VARIABLE, #func);
#define VK_LOAD_DEVICE_FUNC(func) func = (PFN_##func)vkGetDeviceProcAddr(VK_DEVICE_VARIABLE, #func);

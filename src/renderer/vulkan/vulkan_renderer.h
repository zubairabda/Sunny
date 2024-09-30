#include "../renderer.h"
#include "vulkan_loader.h"

#define MAX_PHYSICAL_DEVICE_COUNT 4
#define VERTEX_ARRAY_LEN 65536

struct vulkan_swapchain
{
    VkSemaphore image_acquired_semaphore;
    VkSemaphore submission_complete_semaphore;
    u32 image_count;
    u32 width;
    u32 height;
    VkFormat format;
    VkSwapchainKHR handle;
    VkImage images[3];
    VkImageView image_views[3];
    VkFramebuffer framebuffers[3];
};

struct vulkan_context
{
    renderer_interface renderer;

    CRITICAL_SECTION critical_section;

    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;

    struct {
        u32 graphics;
        u32 compute;
    } queue_indices;

    u32 num_images;

    b8 minimized;
    b8 in_renderpass;

    VkSemaphore submission_complete;
    VkSemaphore image_acquired;
    VkSemaphore present_thread_semaphore;
    VkFence render_fence;
    VkFence present_thread_fence;

    VkSurfaceKHR surface;
    struct vulkan_swapchain swapchain;
    VkRenderPass swapchain_renderpass;

    VkPipeline fullscreen_pipeline;
    VkPipelineLayout fullscreen_pipeline_layout;
    VkDescriptorSet fullscreen_descriptor_set;

    VkDeviceMemory sample_vram_memory;
    VkImage sample_vram; // second copy of vram used for vram transfers
    VkImageView sample_vram_view;

    VkDeviceMemory render_vram_memory;
    VkImage render_vram; // main copy used as render target, can think of as the most 'recently updated' view of the VRAM
    VkImageView render_vram_view;

    VkDeviceMemory display_vram_memory;
    VkImage display_vram; // present copy that is used for updates on vblank
    VkImageView display_vram_view;

    VkFramebuffer framebuffer;
    VkRenderPass renderpass;

    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;

    VkSampler sampler;

    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    VkCommandPool present_thread_command_pool;
    VkCommandBuffer present_thread_command_buffer;

    VkDeviceMemory staging_buffer_memory;
    VkBuffer staging_buffer;
    void *staging_data;
    u32 staging_buffer_offset;

    VkDeviceMemory vertex_buffer_memory;
    VkBuffer vertex_buffer;
};

static inline u32 vulkan_find_memory_type(VkPhysicalDevice physical_device, VkMemoryPropertyFlags desired_properties, u32 type_bits)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    for (u32 i = 0; i < properties.memoryTypeCount; ++i)
    {
        if (((properties.memoryTypes[i].propertyFlags & desired_properties) == desired_properties) && (type_bits & (1 << i)))
        {
            return i;
        }
    }
    return 0xffffffff;
}

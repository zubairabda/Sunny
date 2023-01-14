#include "renderer.h"
#include "vulkan_loader.h"

#define MAX_PHYSICAL_DEVICE_COUNT 4
#define MAX_DEVICE_MEMORY_OBJECTS 2
#define VERTEX_ARRAY_LEN 65536 * 4
#define MAX_RENDER_ENTRY_COUNT 32

struct gpu_allocation
{
    VkDeviceMemory handle;
    size_t marker;
    VkMemoryPropertyFlags flags;
    u32 index;
    size_t size;
};

enum render_entry_type
{
    RENDER_ENTRY_TYPE_INVALID = 0x0,
    RENDER_ENTRY_TYPE_SHADED = 0x1,
    RENDER_ENTRY_TYPE_TEXTURED = 0x2,
    RENDER_ENTRY_TYPE_DRAW_PRIMITIVE = RENDER_ENTRY_TYPE_SHADED | RENDER_ENTRY_TYPE_TEXTURED,
    RENDER_ENTRY_TYPE_CPU_TO_VRAM = 0x4,
    RENDER_ENTRY_TYPE_VRAM_TO_CPU = 0x8,
    RENDER_ENTRY_TYPE_VRAM_TO_VRAM = 0x10,
    RENDER_ENTRY_TYPE_SET_SCISSOR = 0x20
};

struct render_entry
{
    enum render_entry_type type;
    union
    {
        struct
        {
            u32 num_vertices;
            u32 texture_mode; // 0 - no sampling, 1 - 4bit, 2 - 8bit, 3 - direct
        };
        struct vram_transfer
        {
            u16 x;
            u16 y;
            u16 x2;
            u16 y2;
            u16 width;
            u16 height;
            u32 offset; // offset into staging buffer
        } transfer;
        VkRect2D scissor;
    };
};

struct vulkan_context
{
    Renderer header;

    u32 num_images;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;

    struct
    {
        u32 graphics;
        u32 compute;
    } queue_indices;

    VkSemaphore submission_complete;
    VkSemaphore image_acquired;
    VkFence render_fence;

    b32 minimized;
    b32 in_renderpass;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    VkImage swapchain_images[3];
    //VkImageView swapchain_imageviews[3];
    VkFramebuffer swapchain_framebuffers[3];
    VkRenderPass screen_pass;

    VkImage sample_vram; // second copy of vram used for vram transfers
    VkImageView sample_vram_view;

    VkImage render_vram; // main copy used as render target
    VkImageView render_vram_view;
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

    enum render_entry_type current_entry_type;
    u32 entry_count;
    struct render_entry entries[MAX_RENDER_ENTRY_COUNT];

    struct gpu_allocation memory[2];
    VkDeviceMemory staging_memory;
    void* staging_data;
    VkBuffer staging_buffer;
    u32 staging_buffer_offset;
    VkBuffer vertex_buffer;
    u32 allocation_index; // index into memory array from which the buffer was allocated from
    u32 vertex_count;
    Vertex vertex_array[VERTEX_ARRAY_LEN];
};
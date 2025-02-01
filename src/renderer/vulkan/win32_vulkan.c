#include "vulkan_renderer.c"

static vulkan_context *win32_vulkan_init(HWND hwnd, HINSTANCE hinstance)
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");

    if (!lib)
    {
        printf("[FATAL] [VULKAN]: Failed to load the vulkan dll!\n");
        SY_ASSERT(0);
    }

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");

    vk = malloc(sizeof(vulkan_context));
    if (!vk) {
        return NULL;
    }
    memset(vk, 0, sizeof(vulkan_context));

    vk->hw.render_commands_size = KILOBYTES(64);
    vk->hw.render_commands = malloc(vk->hw.render_commands_size);
    if (!vk->hw.render_commands) {
        goto exit_error;
    }
    memset(vk->hw.render_commands, 0, vk->hw.render_commands_size);
    

    vk->hw.vertex_array = malloc(KILOBYTES(64));
    if (!vk->hw.vertex_array) {
        free(vk->hw.render_commands);
        goto exit_error;
    }
    memset(vk->hw.vertex_array, 0, KILOBYTES(64));

    platform_create_mutex(&vk->mutex);

    vulkan_make_instance();

    vulkan_make_device();

    VkWin32SurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hwnd = hwnd;
    surface_info.hinstance = hinstance;

    vkCreateWin32SurfaceKHR(vk->instance, &surface_info, NULL, &vk->surface);

    VkBool32 present_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(vk->physical_device, vk->queue_indices.graphics, vk->surface, &present_supported);
    if (present_supported == VK_FALSE)
    {
        printf("Presentation is not supported on this device\n");
        goto exit_error;
    }
    // init here
    vulkan_init_internal();

    return vk;

exit_error:
    free(vk);
    return NULL;
}

SY_EXPORT renderer_context *win32_load_renderer(HWND hwnd, HINSTANCE hinstance)
{
    vk = win32_vulkan_init(hwnd, hinstance);
    if (!vk) {
        return NULL;
    }
    // link function pointers
    vk->hw.base.handle_resize = vulkan_handle_resize;
    vk->hw.base.update_display = vulkan_update_display;
    vk->hw.base.present = present_frame;
    vk->hw.base.shutdown = vulkan_destroy_context;

    vk->hw.flush_commands = vulkan_flush_commands;

    return (renderer_context *)vk;
}

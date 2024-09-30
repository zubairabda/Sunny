#include "vulkan_renderer.c"

static struct vulkan_context *win32_vulkan_init(HWND hwnd, HINSTANCE hinstance)
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");

    if (!lib)
    {
        printf("[FATAL] [VULKAN]: Failed to load the vulkan dll!\n");
        SY_ASSERT(0);
    }

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");

    struct vulkan_context *vk = VirtualAlloc(0, sizeof(struct vulkan_context), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    InitializeCriticalSection(&vk->critical_section);

    vulkan_make_instance(vk);

    vulkan_make_device(vk);

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
        return 0;
    }
    // init here
    vulkan_init_internal(vk);

    return vk;
}

SUNNY_API renderer_interface *win32_load_renderer(HWND hwnd, HINSTANCE hinstance)
{
    renderer_interface *result = (renderer_interface *)win32_vulkan_init(hwnd, hinstance);
    size_t render_commands_size = MEGABYTES(1);//KILOBYTES(64);
    result->render_commands = VirtualAlloc(0, render_commands_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    result->commands_at = result->render_commands;
    result->render_commands_size = (u32)render_commands_size;
    // link function pointers
    result->handle_resize = vulkan_handle_resize;
    result->update_display = vulkan_update_display;
    result->flush_commands = vulkan_flush_commands;
    result->present = present_frame;
    //result->shutdown = vulkan_destroy_context;

    return result;
}

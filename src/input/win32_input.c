#define INITGUID
#include <dinput.h>
#include "input.h"
#if 0
typedef struct
{
    input_state input;
    IDirectInput8 *dinput;
    LPDIRECTINPUTDEVICE8 device;
} dinput_state;

static BOOL input_devices_enum_callback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvref)
{
    debug_log("%s: %s\n", lpddi->tszInstanceName, lpddi->tszProductName);
    *(GUID *)pvref = lpddi->guidInstance;
    return DIENUM_CONTINUE;
}

b8 dinput_init(HINSTANCE hinstance, dinput_state *input)
{
    IDirectInput8 *dinput;
    HRESULT hr;
    hr = DirectInput8Create(hinstance, DIRECTINPUT_VERSION, &IID_IDirectInput8, (void **)&dinput, NULL);
    if (hr != DI_OK) {
        debug_log("DirectInput interface failed to initialize!\n");
        return 0;
    }

    GUID gamepad_guid;
    dinput->lpVtbl->EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, input_devices_enum_callback, &gamepad_guid, DIEDFL_ATTACHEDONLY); // NOTE: msdn says pvref is 32 bit value?

    LPDIRECTINPUTDEVICE8 lpdi_gamepad;
    hr = dinput->lpVtbl->CreateDevice(dinput, &gamepad_guid, &lpdi_gamepad, NULL);
    if (hr != DI_OK) {
        debug_log("DirectInput failed to create a device!\n");
        dinput->lpVtbl->Release(dinput);
        return 0;
    }

    lpdi_gamepad->lpVtbl->SetDataFormat(lpdi_gamepad, &c_dfDIJoystick);

    lpdi_gamepad->lpVtbl->Acquire(lpdi_gamepad);

    DIDEVCAPS devcaps = {.dwSize = sizeof(DIDEVCAPS)};
    lpdi_gamepad->lpVtbl->GetCapabilities(lpdi_gamepad, &devcaps);
    if (devcaps.dwFlags & DIDC_POLLEDDATAFORMAT) {
        debug_log("polling is enabled.\n");
    }
    
    dinput_state *state = VirtualAlloc(NULL, sizeof(dinput_state), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!state) {
        debug_log("Failed to allocate DirectInput state!\n");
        lpdi_gamepad->lpVtbl->Release(lpdi_gamepad);
        dinput->lpVtbl->Release(dinput);
        return 0;
    }

    plugin->dinput = dinput;
    plugin->device = lpdi_gamepad;

    input->plugin = (input_plugin *)state;

    return 1;
}

#define PAD_BUTTON_DOWN(button) (((button) & 0x80) == 0)

static void dinput_get_data(dinput_state *input)
{
    LPDIRECTINPUTDEVICE8 device = input->device;

    DIJOYSTATE joydata;
    HRESULT hr = device->lpVtbl->GetDeviceState(device, sizeof(DIJOYSTATE), &joydata);
    if (hr != DI_OK) {
        debug_log("Input device lost.\n");
        input->pad.value = 0xffff;
        return;
    }

    input->pad.square = PAD_BUTTON_DOWN(joydata.rgbButtons[0]);
    input->pad.cross = PAD_BUTTON_DOWN(joydata.rgbButtons[1]);
    input->pad.circle = PAD_BUTTON_DOWN(joydata.rgbButtons[2]);
    input->pad.triangle = PAD_BUTTON_DOWN(joydata.rgbButtons[3]);

    input->pad.l1 = PAD_BUTTON_DOWN(joydata.rgbButtons[4]);
    input->pad.r1 = PAD_BUTTON_DOWN(joydata.rgbButtons[5]);
    input->pad.l2 = PAD_BUTTON_DOWN(joydata.rgbButtons[6]);
    input->pad.r2 = PAD_BUTTON_DOWN(joydata.rgbButtons[7]);

    input->pad.select = PAD_BUTTON_DOWN(joydata.rgbButtons[8]);
    input->pad.start = PAD_BUTTON_DOWN(joydata.rgbButtons[9]);

    input->pad.l3 = PAD_BUTTON_DOWN(joydata.rgbButtons[10]);
    input->pad.r3 = PAD_BUTTON_DOWN(joydata.rgbButtons[11]);

    // directional buttons
    DWORD pov = joydata.rgdwPOV[0];
    if ((int)pov > -1)
    {
        //debug_log("%d\n", pov);
        input->pad.joypad_up = !(pov < 9000 || pov > 27000);
        input->pad.joypad_right = !(pov > 0 && pov < 18000);
        input->pad.joypad_down = !(pov > 9000 && pov < 27000);
        input->pad.joypad_left = !(pov > 18000 && pov < 36000);
    }
    else
    {
        input->pad.value |= 0xf0;
        //input->pad.joypad_up = 1;
        //input->pad.joypad_right = 1;
        //input->pad.joypad_down = 1;
        //input->pad.joypad_left = 1;
    }

    //debug_log("UP: %d, RIGHT: %d, DOWN: %d, LEFT: %d\n", input->pad.joypad_up, input->pad.joypad_right, input->pad.joypad_down, input->pad.joypad_left);

}

static void dinput_release_device(LPDIRECTINPUTDEVICE8 device)
{
    device->lpVtbl->Release(device);
}

static void dinput_shutdown(IDirectInput8 *dinput)
{
    dinput->lpVtbl->Release(dinput);
}
#endif
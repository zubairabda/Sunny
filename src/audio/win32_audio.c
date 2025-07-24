#define COBJMACROS
#include <initguid.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>

#include <stdio.h>

#include "audio.h"
#include "debug.h"
#include "psx.h"

DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);

DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);

DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);

typedef struct
{
    IAudioClient *client;
    IAudioRenderClient *render_client;
    void *buffer;
    u32 buffer_size;
    u32 frames_buffered;
    u32 frames_available;
    f32 volume;
    b8 mute;
    HANDLE event;
} win32_audio_impl;

static win32_audio_impl *g_audio;
static u32 avail = 0;

static const char *hr_to_str(HRESULT hr)
{
    switch (hr)
    {
    case E_POINTER:
        return "pdata was null";
    case AUDCLNT_E_BUFFER_ERROR:
        return "Could not get buffer of requested size";
    case AUDCLNT_E_BUFFER_SIZE_ERROR:
        return "Encountered buffer size error";
    case AUDCLNT_E_BUFFER_TOO_LARGE:
        return "Requested buffer size is too large";
    case AUDCLNT_E_OUT_OF_ORDER:
        return "Previous GetBuffer call is still in effect";
    case AUDCLNT_E_DEVICE_INVALIDATED:
        return "Device was invalidated";
    case AUDCLNT_E_BUFFER_OPERATION_PENDING:
        return "Buffer operation is pending";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return "Audio client was not started";
    default:
        return "";
    }
}

void audio_set_volume(f32 volume)
{
    g_audio->volume = volume;
}

f32 audio_get_volume(void)
{
    return g_audio->volume;
}

b8 audio_is_muted(void)
{
    return g_audio->mute;
}

void audio_set_mute(b8 muted)
{
    g_audio->mute = muted;
}

b32 emulate_from_audio(void)
{
    u32 pad = 0;
    HRESULT hr;
    g_audio->buffer = NULL;

    DWORD res = WaitForSingleObject(g_audio->event, INFINITE);
    if (res != WAIT_OBJECT_0)
    {
        debug_error("Failed to retrieve the audio event handle");
        return false;
    }

    hr = IAudioClient_GetCurrentPadding(g_audio->client, &pad);
    u32 available_frames = g_audio->buffer_size - pad;
    avail = available_frames;

    hr = IAudioRenderClient_GetBuffer(g_audio->render_client, available_frames, (BYTE **)&g_audio->buffer);
    if (hr != S_OK)
    {
        debug_error("GetBuffer failed with error: %s\n", hr_to_str(hr));
        return false;
    }

    g_audio->frames_buffered = 0;
    g_audio->frames_available = available_frames;

    // still deciding if breakpoints will be removed at some point or kept in a debug only scenario
    while (g_audio->frames_buffered < available_frames)
    {
        if (!psx_run())
        {
            g_state = SYSTEM_STATE_PAUSED;
            break;
        }
    }

    DWORD frames_written = MIN(g_audio->frames_buffered, available_frames);

    IAudioRenderClient_ReleaseBuffer(g_audio->render_client, frames_written, g_audio->mute ? AUDCLNT_BUFFERFLAGS_SILENT : 0);

    return true;
}

void audio_buffer_write(s16 left, s16 right)
{
    s16 *buffer = g_audio->buffer;
    u32 frames_buffered = g_audio->frames_buffered;
    // samples are dropped if the SPU is ticked multiple times past the available frame count
    if (buffer && (frames_buffered < g_audio->frames_available))
    {
        left *= g_audio->volume;
        right *= g_audio->volume;
        int index = frames_buffered << 1;
        buffer[index] = left;
        buffer[index + 1] = right;
        ++g_audio->frames_buffered;
    }
}

void play_sound(s16 *data, u32 num_frames)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    //IAudioClient_Start(audio->client);

    s32 remaining_frames = (s32)num_frames;

    while (remaining_frames)
    {
        u32 available_frames;
        hr = IAudioClient_GetCurrentPadding(g_audio->client, &pad);
        available_frames = g_audio->buffer_size - pad;  

        u32 frames_written = (s32)(remaining_frames - available_frames) < 0 ? remaining_frames : available_frames;

        hr = IAudioRenderClient_GetBuffer(g_audio->render_client, frames_written, &pdata);
        if (hr != S_OK)
        {
            debug_log("%s\n", hr_to_str(hr));
        }

        u32 buffer_size = frames_written * 4; // multiply by nBlockAlign

        memcpy(pdata, data, buffer_size);
        for (u32 i = 0; i < frames_written * 2; ++i)
        {
            ((s16 *)pdata)[i] /= 2;
        }

        hr = IAudioRenderClient_ReleaseBuffer(g_audio->render_client, frames_written, 0);
        remaining_frames -= frames_written;
    }
    //IAudioClient_Stop(audio->client);
}

b8 audio_init(void)
{
    g_audio = VirtualAlloc(NULL, sizeof(win32_audio_impl), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_audio)
    {
        debug_error("Failed to allocate the audio player\n");
        return false;
    }

    typedef HRESULT (*fp_CoInitializeEx)(LPVOID pvReserved, DWORD dwCoInit);
    typedef HRESULT (*fp_CoCreateInstance)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);

    HMODULE lib = LoadLibraryA("Ole32.dll");
    fp_CoCreateInstance CoCreateInstance = (fp_CoCreateInstance)GetProcAddress(lib, "CoCreateInstance");
    fp_CoInitializeEx CoInitializeEx = (fp_CoInitializeEx)GetProcAddress(lib, "CoInitializeEx");

    HRESULT hr;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *audio_client = NULL;
    IAudioRenderClient *render_client = NULL;
    WAVEFORMATEXTENSIBLE *wave_fmt;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    
    FreeLibrary(lib);

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&audio_client);

    //hr = IAudioClient_GetMixFormat(audio_client, (WAVEFORMATEX **)&wave_fmt);
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nSamplesPerSec = 44100;
    format.nChannels = 2;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 176400;
    hr = IAudioClient_Initialize(audio_client, AUDCLNT_SHAREMODE_SHARED, 
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | 
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | 
                                 AUDCLNT_STREAMFLAGS_RATEADJUST |
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 
                                 0, 0, &format, NULL);
    if (hr != S_OK)
    {
        debug_error("Failed to initialize the audio client\n");
        VirtualFree(g_audio, 0, MEM_RELEASE);
        return false;
    }

    g_audio->event = CreateEvent(NULL, FALSE, FALSE, NULL);

    IAudioClient_SetEventHandle(audio_client, g_audio->event);
#if 0
    //hr = audio_client->SetEventHandle(win32audio->event);

    USHORT format = EXTRACT_WAVEFORMATEX_ID(&wave_fmt->SubFormat);

    u16 sample_blockalign = *(u16*)(sample->data + 32);
    u16 channel_count = *(u16*)(sample->data + 22);
    u16 bits_per_sample = *(u16*)(sample->data + 34);
    u32 sample_size = *(u32*)(sample->data + 40);
    u8* sample_data = sample->data + 44;

    REFERENCE_TIME default_period;
    
    IAudioClient_GetDevicePeriod(audio_client, &default_period, NULL);
#endif
    IAudioClient_GetBufferSize(audio_client, &g_audio->buffer_size);

    hr = IAudioClient_Start(audio_client);

    g_audio->client = audio_client;

    hr = IAudioClient_GetService(g_audio->client, &IID_IAudioRenderClient, (void **)&render_client);

    g_audio->render_client = render_client;
    //audio->thread = CreateThread(NULL, 0, begin_playing_sounds, audio, 0, NULL);
    return true;
}

void audio_shutdown(void)
{
    IAudioClient_Stop(g_audio->client);
    IAudioClient_Release(g_audio->client);
    VirtualFree(g_audio, 0, MEM_RELEASE);
}

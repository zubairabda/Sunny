#define COBJMACROS
#include <initguid.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>

#include <stdio.h>

#include "audio.h"
#include "spu.h"
#include "debug.h"
#include "psx.h"

DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);

DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);

DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);

struct audio_player
{
    IAudioClient *client;
    IAudioRenderClient *render_client;
    u32 buffer_size;
    u32 src_len;
    u8 *src_cursor;
    HANDLE event;
};

static inline const char *hr_to_str(HRESULT hr)
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

void emulate_from_audio(audio_player *audio)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    u32 available_frames;

    DWORD res = WaitForSingleObject(audio->event, INFINITE);
    if (res != WAIT_OBJECT_0)
    {
        debug_error("Failed to retrieve the audio event handle");
    }
    hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
    available_frames = audio->buffer_size - pad;

    hr = IAudioRenderClient_GetBuffer(audio->render_client, available_frames, &pdata);
    if (hr != S_OK)
    {
        debug_error("GetBuffer failed with error: %s\n", hr_to_str(hr));
    }

    u32 buffer_size = available_frames * 4; // multiply by nBlockAlign

    g_spu.audio_buffer = (s16 *)pdata;

    while (g_spu.num_buffered_frames < available_frames && g_state == SYSTEM_STATE_RUNNING)
    {
        psx_run();
    }
    g_spu.num_buffered_frames = 0;

    hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, available_frames, g_spu.enable_output ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
}


void play_sound(audio_player *audio, s16 *data, u32 num_frames)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    //IAudioClient_Start(audio->client);

    s32 remaining_frames = (s32)num_frames;

    while (remaining_frames)
    {
        u32 available_frames;
        hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
        available_frames = audio->buffer_size - pad;  

        u32 frames_written = (s32)(remaining_frames - available_frames) < 0 ? remaining_frames : available_frames;

        hr = IAudioRenderClient_GetBuffer(audio->render_client, frames_written, &pdata);
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

        hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, frames_written, 0);
        remaining_frames -= frames_written;
    }
    //IAudioClient_Stop(audio->client);
}

audio_player *audio_init(void)
{
    audio_player *audio = VirtualAlloc(NULL, sizeof(audio_player), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!audio)
    {
        debug_error("Failed to allocate the audio player\n");
        return NULL;
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
        return NULL;
    }

    audio->event = CreateEvent(NULL, FALSE, FALSE, NULL);

    IAudioClient_SetEventHandle(audio_client, audio->event);
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
    IAudioClient_GetBufferSize(audio_client, &audio->buffer_size);

    hr = IAudioClient_Start(audio_client);

    audio->client = audio_client;

    hr = IAudioClient_GetService(audio->client, &IID_IAudioRenderClient, (void **)&render_client);

    audio->render_client = render_client;
    //audio->thread = CreateThread(NULL, 0, begin_playing_sounds, audio, 0, NULL);
    return audio; 
}

void audio_shutdown(audio_player* audio)
{
    VirtualFree(audio, 0, MEM_RELEASE);
}

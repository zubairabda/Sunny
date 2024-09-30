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

struct audio_source
{
    void *data;
    u64 size;
};

void load_audio_source(audio_player *audio, u8 *src, u32 src_len_bytes)
{
    audio->src_cursor = src;
    audio->src_len = src_len_bytes;
}

void play_sound_test(audio_player *audio)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    //IAudioClient_Start(audio->client);

    hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
    u32 available_frames = audio->buffer_size - pad;
    //Sleep(0);
    hr = IAudioRenderClient_GetBuffer(audio->render_client, available_frames, &pdata);

    u32 buffer_size = available_frames * 4; // multiply by nBlockAlign
    memcpy(pdata, audio->src_cursor, buffer_size);
    #if 1
    for (u32 i = 0; i < available_frames * 2; ++i)
    {
        s32 sample = ((s16 *)pdata)[i];
        #if 1
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;
        #endif
        ((s16 *)pdata)[i] = sample / 4;
    }
    #endif
    audio->src_cursor += buffer_size;
    hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, available_frames, 0);

    //IAudioClient_Stop(audio->client);
}

void debug_play_sound(audio_player *audio, u8 *source_data)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    u8 *current = source_data;

    IAudioClient_Start(audio->client);

    while (1)
    {
        hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
        u32 available_frames = audio->buffer_size - pad;
        //Sleep(0);
        hr = IAudioRenderClient_GetBuffer(audio->render_client, available_frames, &pdata);

        u32 buffer_size = available_frames * 4; // multiply by nBlockAlign
        memcpy(pdata, current, buffer_size);
        #if 0
        for (u32 i = 0; i < available_frames * 2; ++i)
        {
            s32 sample = ((s16 *)pdata)[i];
            #if 1
            if (sample > 32767)
                sample = 32767;
            else if (sample < -32768)
                sample = -32768;
            #endif
            ((s16 *)pdata)[i] = sample;
        }
        #endif
        current += buffer_size;
        hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, available_frames, 0);
    }

    IAudioClient_Stop(audio->client);

}

static inline char *hr_to_str(HRESULT hr)
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
#if 1
void emulate_from_audio(audio_player *audio)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    u32 available_frames;
    //hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
    //available_frames = audio->buffer_size - pad;
    #if 1
#if 0
    do
    {
        DWORD res = WaitForSingleObject(audio->event, INFINITE);
        if (res != WAIT_OBJECT_0)
        {
            DebugBreak();
        }
        hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
        available_frames = audio->buffer_size - pad;
    } while (!available_frames);
#else
    DWORD res = WaitForSingleObject(audio->event, INFINITE);
    if (res != WAIT_OBJECT_0)
    {
        SY_ASSERT(0);
    }
    hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
    available_frames = audio->buffer_size - pad;
#endif
    #endif
#if 0
    if (!available_frames)
        DebugBreak();
#endif
    hr = IAudioRenderClient_GetBuffer(audio->render_client, available_frames, &pdata);
    if (hr != S_OK)
    {
        debug_log("%s\n", hr_to_str(hr));
    }

    u32 buffer_size = available_frames * 4; // multiply by nBlockAlign

    g_spu.audio_buffer = (s16 *)pdata;

    while (g_spu.num_buffered_frames < available_frames)
    {
        psx_run();
    }
    g_spu.num_buffered_frames = 0;
    // half volume
    for (u32 i = 0; i < available_frames * 2; ++i)
    {
        ((s16 *)pdata)[i] /= 2;
    }

    hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, available_frames, 0);
}
#endif
#if 1
static inline void play_sound(audio_player *audio, s16 *data, u32 num_buffered_frames)
{
    u32 pad = 0;
    HRESULT hr;
    BYTE *pdata = NULL;

    //IAudioClient_Start(audio->client);

    s32 remaining_frames = (s32)num_buffered_frames;

    while (remaining_frames)
    {
        u32 available_frames;
    #if 0
        do
        {
            DWORD res = WaitForSingleObject(audio->event, 2000);
            if (res != WAIT_OBJECT_0)
            {
                DebugBreak();
            }
            hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
            available_frames = audio->buffer_size - pad;
        } while (!available_frames);
    #elif 0
        DWORD res = WaitForSingleObject(audio->event, 2000);
        if (res != WAIT_OBJECT_0)
        {
            DebugBreak();
        }
        hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
        available_frames = audio->buffer_size - pad;
    #else
        hr = IAudioClient_GetCurrentPadding(audio->client, &pad);
        available_frames = audio->buffer_size - pad;  
    #endif
        u32 frames_written = (s32)(remaining_frames - available_frames) < 0 ? remaining_frames : available_frames;
    #if 0
        if (!available_frames)
            DebugBreak();
    #endif
        hr = IAudioRenderClient_GetBuffer(audio->render_client, frames_written, &pdata);
        if (hr != S_OK)
        {
            debug_log("%s\n", hr_to_str(hr));
        }

        u32 buffer_size = frames_written * 4; // multiply by nBlockAlign

        memcpy(pdata, data, buffer_size);
        // damn this is loud lol
        for (u32 i = 0; i < frames_written * 2; ++i)
        {
            ((s16 *)pdata)[i] /= 2;
        }

        hr = IAudioRenderClient_ReleaseBuffer(audio->render_client, frames_written, 0);
        remaining_frames -= frames_written;
    }
    //IAudioClient_Stop(audio->client);
}
#endif
audio_player *audio_init(void)
{
    audio_player *audio = VirtualAlloc(NULL, sizeof(audio_player), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!audio)
        return NULL;

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

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    // TODO: unload dll here?
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
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, &format, NULL);
    if (hr != S_OK) {
        debug_log("Failed to initialize the audio client\n");
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

static void audio_shutdown(audio_player* audio)
{
    // wait until audio thread finishes
    VirtualFree(audio, 0, MEM_RELEASE);
}

static void write_wav_file(void *sample_data, u32 size_in_bytes, char *dstpath)
{
    FILE *f = fopen(dstpath, "wb");
    struct wav_header header = {0};
    header.riff[0] = 'R';
    header.riff[1] = 'I';
    header.riff[2] = 'F';
    header.riff[3] = 'F';

    header.wave[0] = 'W';
    header.wave[1] = 'A';
    header.wave[2] = 'V';
    header.wave[3] = 'E';

    header.chunk_size = size_in_bytes + 36;

    fwrite(&header, sizeof(struct wav_header), 1, f);

    struct wav_format_chunk format = {0};
    format.fmt[0] = 'f';
    format.fmt[1] = 'm';
    format.fmt[2] = 't';
    format.fmt[3] = ' ';
    format.bits_per_sample = 16;
    format.block_align = 4;
    format.bytes_per_second = 44100 * 4;
    format.chunk_size = 16;
    format.format_tag = 1;
    format.num_channels = 2;
    format.samples_per_second = 44100;
    
    fwrite(&format, sizeof(struct wav_format_chunk), 1, f);

    struct wav_data_chunk chunk = {0};
    chunk.data[0] = 'd';
    chunk.data[1] = 'a';
    chunk.data[2] = 't';
    chunk.data[3] = 'a';
    chunk.chunk_size = size_in_bytes;

    fwrite(&chunk, sizeof(struct wav_data_chunk), 1, f);
    fwrite(sample_data, 1, size_in_bytes, f);
    fclose(f);
}

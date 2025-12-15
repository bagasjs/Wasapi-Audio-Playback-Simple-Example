#include <combaseapi.h>
#include <objbase.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

void *load_file_data(const char *filepath, size_t *file_size)
{
    FILE *f = NULL;
    void *data = NULL;

    f = fopen(filepath, "rb");
    fseek(f, 0L, SEEK_END);
    size_t fsz = ftell(f);
    *file_size = fsz;
    fseek(f, 0L, SEEK_SET);
    data = malloc(fsz);
    if(!data) {
        fprintf(stderr, "load_file_data: failed to allocate memory (%s)\n", filepath);
        fclose(f);
        return NULL;
    }
    size_t rsz = fread(data, 1, fsz, f);
    if(rsz != fsz) {
        fprintf(stderr, "load_file_data: failed to read file (%s)\n", filepath);
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return data;
}

typedef struct AudioSource {
    void *data;
    uint32_t sampleRate;
    uint32_t frameCount; 
    uint32_t nChannels;
    uint32_t bitsPerSample;
} AudioSource;

#include <aviriff.h>

AudioSource load_wave_from_memory(void *buf, size_t bufsz)
{
    AudioSource source = {0};
    RIFFLIST *header = (RIFFLIST*)buf;
    assert(header->fcc == FCC('RIFF') && header->fccListType == FCC('WAVE'));
    void *subchunks = buf + sizeof(RIFFLIST);
    void *eof = buf + bufsz;
    for(RIFFCHUNK *chunk = (RIFFCHUNK*)(subchunks); (void*)chunk < eof; chunk = RIFFNEXT(chunk)) {
        if(chunk->fcc == FCC('fmt ')) {
            WAVEFORMATEX *fmt = (WAVEFORMATEX*)(chunk+1);
            assert(fmt->wFormatTag == WAVE_FORMAT_PCM);
            assert(chunk->cb == 16 || chunk->cb == 18);
            assert(fmt->nBlockAlign == fmt->nChannels * fmt->wBitsPerSample / 8);
            assert(fmt->nAvgBytesPerSec == fmt->nSamplesPerSec * fmt->nBlockAlign);
            source.nChannels     = fmt->nChannels;
            source.sampleRate    = fmt->nSamplesPerSec;
            source.bitsPerSample = fmt->wBitsPerSample;
        } else if(chunk->fcc == FCC('data')) {
            source.frameCount = chunk->cb/sizeof(uint16_t);
            source.data = ((uint8_t*)chunk + sizeof(RIFFCHUNK));
            assert((void*)source.data + chunk->cb - 1 < (void*)eof);
        }
    }
    return source;
}

float GetAudioSourceDuration(AudioSource source)
{
    return (float)source.frameCount/(float)source.sampleRate/(float)source.nChannels;
}

// Taken from miniaudio.h
static const IID MA_CLSID_MMDeviceEnumerator                     = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}}; /* BCDE0395-E52F-467C-8E3D-C4579291692E = __uuidof(MMDeviceEnumerator) */
static const IID MA_IID_IMMDeviceEnumerator                      = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}}; /* A95664D2-9614-4F35-A746-DE8DB63617E6 = __uuidof(IMMDeviceEnumerator) */
static const IID MA_IID_IAudioClient2                            = {0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}}; /* 726778CD-F60A-4EDA-82DE-E47610CD78AA = __uuidof(IAudioClient2) */
static const IID MA_IID_IAudioRenderClient                       = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}}; /* F294ACFC-3146-4483-A7BF-ADDCA7C260E2 = __uuidof(IAudioRenderClient) */

#define COM_CALL(INSTANCE, METHOD, ...) (INSTANCE)->lpVtbl->METHOD((INSTANCE), __VA_ARGS__)
#define COM_CALL_OK(INSTANCE, METHOD, ...) assert(!FAILED(COM_CALL(INSTANCE, METHOD, __VA_ARGS__)))
int main(int argc, char *argv[])
{
    assert(argc > 1 && "Provide an input filepath");
    const char *input_filepath = argv[1];

    size_t file_size = 0;
    void *file_data = load_file_data(input_filepath, &file_size);
    assert(file_data && file_size > 0);
    AudioSource source = load_wave_from_memory(file_data, file_size);
    printf("Playing: %s\n", input_filepath);
    printf("   sampleRate:     %u\n", source.sampleRate);
    printf("   frameCount:     %u\n", source.frameCount);
    printf("   nChannels:      %u\n", source.nChannels);
    printf("   bitsPerSample:  %u\n", source.bitsPerSample);
    printf("   +duration:      %5.5fs\n", GetAudioSourceDuration(source));

    HRESULT hr = CoInitializeEx(NULL, COINIT_SPEED_OVER_MEMORY);
    assert(!FAILED(hr));
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    hr = CoCreateInstance(&MA_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &MA_IID_IMMDeviceEnumerator, (LPVOID*)(&deviceEnumerator));
    assert(!FAILED(hr));

    IMMDevice *audioDevice = 0;
    COM_CALL_OK(deviceEnumerator, GetDefaultAudioEndpoint, eRender, eConsole, &audioDevice);
    COM_CALL(deviceEnumerator, Release);

    IAudioClient2 *audioClient;
    COM_CALL_OK(audioDevice, Activate, &MA_IID_IAudioClient2, CLSCTX_ALL, NULL, (LPVOID*)(&audioClient));

    WAVEFORMATEX mixFormat = {0};
    mixFormat.wFormatTag = WAVE_FORMAT_PCM;
    mixFormat.nChannels  = 2;
    mixFormat.nSamplesPerSec = source.sampleRate;
    mixFormat.wBitsPerSample = 16;
    mixFormat.nBlockAlign = (mixFormat.nChannels * mixFormat.wBitsPerSample) / 8;
    mixFormat.nAvgBytesPerSec = mixFormat.nSamplesPerSec * mixFormat.nBlockAlign;

    float duration = GetAudioSourceDuration(source);
    const int64_t REFTIMES_PER_SEC = 100000000; // hundred nanoseconds
    REFERENCE_TIME requestedSoundBufferDuration = (REFERENCE_TIME)(REFTIMES_PER_SEC*duration);
    DWORD initStreamFlags = AUDCLNT_STREAMFLAGS_RATEADJUST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    COM_CALL_OK(audioClient, Initialize, 
            AUDCLNT_SHAREMODE_SHARED, 
            initStreamFlags,
            requestedSoundBufferDuration,
            0,
            &mixFormat,
            NULL);

    IAudioRenderClient* audioRenderClient;
    COM_CALL_OK(audioClient, GetService, &MA_IID_IAudioRenderClient, (LPVOID*)(&audioRenderClient));

    UINT32 bufferSizeInFrames;
    COM_CALL_OK(audioClient, GetBufferSize, &bufferSizeInFrames);
    COM_CALL_OK(audioClient, Start);

    uint32_t wavPlaybackSample = 0;
    while(1) {
        UINT32 bufferPadding;
        COM_CALL_OK(audioClient, GetCurrentPadding, &bufferPadding);

        const float TARGET_BUFFER_PADDING_IN_SECONDS = 1 / 60.f;
        UINT32 targetBufferPadding = (UINT32)(bufferSizeInFrames * TARGET_BUFFER_PADDING_IN_SECONDS);
        UINT32 numFramesToWrite = targetBufferPadding - bufferPadding;

        uint16_t *buffer;
        COM_CALL_OK(audioRenderClient, GetBuffer, numFramesToWrite, (BYTE**)&buffer);
        for(UINT32 frameIndex = 0; frameIndex < numFramesToWrite; ++frameIndex)
        {
            uint32_t leftSampleIndex = wavPlaybackSample;
            uint32_t rightSampleIndex = wavPlaybackSample + source.nChannels - 1;
            *buffer++ = ((uint16_t*)source.data)[leftSampleIndex];
            *buffer++ = ((uint16_t*)source.data)[rightSampleIndex];
            wavPlaybackSample += source.nChannels;
            wavPlaybackSample %= source.frameCount; // Loop if we reach end of wav file
        }
        COM_CALL_OK(audioRenderClient, ReleaseBuffer, numFramesToWrite, 0);
    }

    COM_CALL(audioRenderClient, Release);
    COM_CALL(audioClient, Stop);
    COM_CALL(audioClient, Release);

    CoUninitialize();
    return 0;
}

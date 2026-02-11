#include "AudioThreads.h"

void AudioThreads::CaptureThread()
{
    CoInitialize(nullptr);

    IMMDeviceEnumerator* e;
    IMMDevice* d;
    IAudioClient* c;
    IAudioCaptureClient* cap;

    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&e);

    e->GetDefaultAudioEndpoint(eRender, eConsole, &d);
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&c);

    WAVEFORMATEX* fmt;
    c->GetMixFormat(&fmt);

    c->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        10000000, 0, fmt, nullptr);

    c->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
    c->Start();

    while (true)
    {
        UINT32 frames;
        cap->GetNextPacketSize(&frames);
        if (!frames)
        {
            Sleep(3);
            continue;
        }

        BYTE* data;
        DWORD flags;
        cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);

        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
            AudioGlobalUtils::Push((float*)data, frames * CHANNELS);

        cap->ReleaseBuffer(frames);
    }
}


void AudioThreads::PlaybackThread()
{
    CoInitialize(nullptr);

    IMMDeviceEnumerator* e;
    IMMDevice* d;
    IAudioClient* c;
    IAudioRenderClient* r;

    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&e);

    e->GetDefaultAudioEndpoint(eRender, eConsole, &d);
    d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&c);

    WAVEFORMATEX* fmt;
    c->GetMixFormat(&fmt);

    c->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
        10000000, 0, fmt, nullptr);

    c->GetService(__uuidof(IAudioRenderClient), (void**)&r);
    c->Start();

    UINT32 bufFrames;
    c->GetBufferSize(&bufFrames);

    while (true)
    {
        if (!g_playing.load(std::memory_order_acquire))
        {
            Sleep(2);
            continue;
        }

        UINT32 padding;
        c->GetCurrentPadding(&padding);

        UINT32 avail = bufFrames - padding;
        if (!avail)
        {
            Sleep(1);
            continue;
        }

        BYTE* out;
        r->GetBuffer(avail, &out);
        float* f = (float*)out;

        size_t pos = g_playPos.load(std::memory_order_relaxed);

        for (UINT32 i = 0; i < avail * CHANNELS; i++)
        {
            if (pos >= g_playEnd)
            {
                if (g_playing.exchange(false)) PostMessage(g_hwndMain, WM_PLAYBACK_ENDED, 0, 0);
                f[i] = 0;
            }
            else
                f[i] = g_snapshot[pos++];
        }

        g_playPos.store(pos, std::memory_order_release);
        r->ReleaseBuffer(avail, 0);
    }
}

void AudioThreads::MicToCableThread()
{
    CoInitialize(nullptr);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* mic = nullptr;
    IMMDevice* cable = nullptr;

    IAudioClient* micClient = nullptr;
    IAudioClient* cableClient = nullptr;

    IAudioCaptureClient* cap = nullptr;
    IAudioRenderClient* ren = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );
    if (FAILED(hr)) return;

    enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &mic);
    cable = AudioGlobalUtils::FindRenderDeviceByName(L"CABLE Input");

    mic->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&micClient);
    cable->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&cableClient);

    WAVEFORMATEX* micFmt = nullptr;
    WAVEFORMATEX* cableFmt = nullptr;

    micClient->GetMixFormat(&micFmt);       // usually mono
    cableClient->GetMixFormat(&cableFmt);   // stereo 2 channel for Cable Input

    wprintf(L"Mic:   %d Hz, %d ch, %d bits\n",
        micFmt->nSamplesPerSec, micFmt->nChannels, micFmt->wBitsPerSample);

    wprintf(L"Cable: %d Hz, %d ch, %d bits\n",
        cableFmt->nSamplesPerSec, cableFmt->nChannels, cableFmt->wBitsPerSample);

    REFERENCE_TIME bufferTime = 1'000'000;

    hr = micClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        bufferTime,
        0,
        cableFmt,   // request stereo output but doesn't get it normally
        nullptr
    );
    if (FAILED(hr)) return;

    hr = cableClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        bufferTime,
        0,
        cableFmt,
        nullptr
    );
    if (FAILED(hr)) return;

    micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
    cableClient->GetService(__uuidof(IAudioRenderClient), (void**)&ren);

    micClient->Start();
    cableClient->Start();

    UINT32 bufferFrames = 0;
    cableClient->GetBufferSize(&bufferFrames);

    size_t playPos = g_playStart;

    while (true)
    {
        UINT32 frames = 0;
        cap->GetNextPacketSize(&frames);
        if (!frames)
        {
            Sleep(1);
            continue;
        }

        BYTE* inData = nullptr;
        DWORD flags = 0;

        cap->GetBuffer(&inData, &frames, &flags, nullptr, nullptr);

        UINT32 padding = 0;
        cableClient->GetCurrentPadding(&padding);

        UINT32 avail = bufferFrames - padding;
        if (frames > avail)
            frames = avail;

        if (frames == 0)
        {
            cap->ReleaseBuffer(0);
            continue;
        }

        BYTE* outData = nullptr;
        ren->GetBuffer(frames, &outData);

        if (g_playing.load())
        {
            float* out = (float*)outData;

            for (UINT32 i = 0; i < frames * cableFmt->nChannels; ++i)
            {
                if (playPos >= g_playEnd)
                {
                    out[i] = 0.0f;
                    g_playing.store(false);
                }
                else
                {
                    out[i] = g_snapshot[playPos++];
                }
            }
        }
        else
        {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                memset(outData, 0, frames * cableFmt->nBlockAlign);
            }
            else
            {
                // formats the same, usually doesn't happen for me
                memcpy(outData, inData, frames * cableFmt->nBlockAlign);
            }

            playPos = 0;
        }

        ren->ReleaseBuffer(frames, 0);
        cap->ReleaseBuffer(frames);
    }
}

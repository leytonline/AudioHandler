#include "AudioGlobals.h"

std::vector<float> g_ring(BUFFER_SAMPLES);
std::vector<float> g_snapshot(BUFFER_SAMPLES);

std::atomic<size_t> g_writePos{ 0 };
std::atomic<size_t> g_playPos{ 0 };
size_t g_playStart{ 0 };
size_t g_playEnd{ BUFFER_SAMPLES };
std::atomic<bool>   g_playing{ false };

HWND g_hwndMain = nullptr;

void AudioGlobalUtils::Push(const float* in, size_t n)
{
    size_t p = g_writePos.load(std::memory_order_relaxed);

    for (size_t i = 0; i < n; ++i)
    {
        g_ring[p] = in[i];
        p = (p + 1) % BUFFER_SAMPLES;
    }

    g_writePos.store(p, std::memory_order_release);
}

IMMDevice* AudioGlobalUtils::FindRenderDeviceByName(const wchar_t* target)
{
    IMMDeviceEnumerator* e;
    IMMDeviceCollection* c;

    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&e);

    e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &c);

    UINT count;
    c->GetCount(&count);

    for (UINT i = 0; i < count; i++)
    {
        IMMDevice* d;
        c->Item(i, &d);

        IPropertyStore* props;
        d->OpenPropertyStore(STGM_READ, &props);

        PROPVARIANT v;
        PropVariantInit(&v);
        props->GetValue(PKEY_Device_FriendlyName, &v);

        if (wcsstr(v.pwszVal, target))
        {
            props->Release();
            c->Release();
            e->Release();
            return d;
        }

        PropVariantClear(&v);
        props->Release();
        d->Release();
    }

    c->Release();
    e->Release();
    return nullptr;
}
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <fcntl.h>
#include <stdio.h>
#include <io.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include "AudioMessages.h"
#include "AudioGlobals.h"
#include "AudioThreads.h"

//#define __SAMPLE_DEBUG_DISPLAY

#pragma comment(lib, "ole32.lib")

#define ID_BTN_SAVE 1001
#define ID_BTN_PLAY 1002

float gStartNorm = 0.0f; 
float gEndNorm = 1.0f;

bool gDraggingStart = false;
bool gDraggingEnd = false;

constexpr int SLIDER_HEIGHT = 40;
constexpr int WAVEFORM_MARGIN = 10;
constexpr int HANDLE_WIDTH = 6;

HWND selectionHwnd;

int Clamp(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

float Clampf(float f, float l, float h) {
    return std::max(l, std::min(f, h));
}

void SaveLast()
{
    // sequentially consistent ?? -> shouldn't collide
    if (g_playing.load()) {
        wprintf_s(L"AUDIO IS PLAYING; not saving...\n");
        return;
    }

    size_t p = g_writePos.load(std::memory_order_acquire);
    size_t tail = BUFFER_SAMPLES - p;
    memcpy(g_snapshot.data(), g_ring.data() + p, tail * sizeof(float));
    memcpy(g_snapshot.data() + tail, g_ring.data(), p * sizeof(float));
    InvalidateRect(selectionHwnd, nullptr, TRUE);
}

void StartPlay()
{
    g_playPos.store(g_playStart, std::memory_order_relaxed);
    g_playing.store(true, std::memory_order_release);
    PostMessage(g_hwndMain, WM_PLAYBACK_STARTED, 0, 0);
}

void DrawSlider(HDC hdc, RECT rc) {
    int width = rc.right - rc.left;
    int yMid = (rc.top + rc.bottom) / 2;

    int xStart = rc.left + int(gStartNorm * width);
    int xEnd = rc.left + int(gEndNorm * width);

    // Track
    HPEN trackPen = CreatePen(PS_SOLID, 2, RGB(120, 120, 120));
    SelectObject(hdc, trackPen);
    MoveToEx(hdc, rc.left, yMid, nullptr);
    LineTo(hdc, rc.right, yMid);

    // Selected region
    HPEN selPen = CreatePen(PS_SOLID, 4, RGB(0, 180, 255));
    SelectObject(hdc, selPen);
    MoveToEx(hdc, xStart, yMid, nullptr);
    LineTo(hdc, xEnd, yMid);

    // Handles
    HBRUSH handleBrush = CreateSolidBrush(RGB(0, 140, 200));

    RECT h1{ xStart - HANDLE_WIDTH, rc.top, xStart + HANDLE_WIDTH, rc.bottom };
    RECT h2{ xEnd - HANDLE_WIDTH, rc.top, xEnd + HANDLE_WIDTH, rc.bottom };

    FillRect(hdc, &h1, handleBrush);
    FillRect(hdc, &h2, handleBrush);

    DeleteObject(trackPen);
    DeleteObject(selPen);
    DeleteObject(handleBrush);
}

void DrawWaveform(HDC hdc, RECT rc) {
    if (g_snapshot.empty())
        return;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int centerY = rc.top + height / 2;

    int samples = (int) g_snapshot.size();

    //wprintf(L"%d samples per bar (%d, %d)\n", samples / width, samples, width);

    int samplesPerBar = std::max(1, samples / width);

    HPEN wavePen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    SelectObject(hdc, wavePen);

    for (int x = 0; x < width; x++) {
        int start = x * samplesPerBar;
        int end = std::min(start + samplesPerBar, samples);

        float peak = 0.0f;
        for (int i = start; i < end; i++) {
            peak = std::max(peak, std::abs(g_snapshot[i]));
        }

        int barH = int(peak * (height));
        MoveToEx(hdc, rc.left + x, centerY - barH, nullptr);
        LineTo(hdc, rc.left + x, centerY + barH);
    }

    // Draw start/end markers
    int xStart = rc.left + int(gStartNorm * width);
    int xEnd = rc.left + int(gEndNorm * width);

    HPEN markerPen = CreatePen(PS_SOLID, 2, RGB(255, 100, 100));
    SelectObject(hdc, markerPen);

    MoveToEx(hdc, xStart, rc.top, nullptr);
    LineTo(hdc, xStart, rc.bottom);

    MoveToEx(hdc, xEnd, rc.top, nullptr);
    LineTo(hdc, xEnd, rc.bottom);

    DeleteObject(wavePen);
    DeleteObject(markerPen);
}


void ListClients(EDataFlow flow) {
    IMMDeviceEnumerator* enumerator;
    IMMDeviceCollection* devices;

    CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    enumerator->EnumAudioEndpoints(
        flow,                    // eRender or eCapture
        DEVICE_STATE_ACTIVE,
        &devices
    );

    UINT count;
    devices->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device;
        devices->Item(i, &device);

        IPropertyStore* props;
        device->OpenPropertyStore(STGM_READ, &props);

        PROPVARIANT name;
        PropVariantInit(&name);

        props->GetValue(PKEY_Device_FriendlyName, &name);

        wprintf(L"%u: %s\n", i, name.pwszVal);

        PropVariantClear(&name);
        props->Release();
        device->Release();
    }

    devices->Release();
    enumerator->Release();

}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{

    switch (m) {

    case WM_COMMAND: {
        if (LOWORD(w) == ID_BTN_SAVE) SaveLast();
        if (LOWORD(w) == ID_BTN_PLAY) StartPlay();
        break;
    }

    // IF OWNER DRAW, THEN DO THIS
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*) l;

        if (dis->CtlID == ID_BTN_SAVE)
        {
           HDC dc = dis->hDC;
            RECT rc = dis->rcItem;

            // Pick color based on atomic<bool>
            COLORREF bg = g_playing.load()
                ? RGB(220, 60, 60) : // active (red)
                  RGB(255, 255, 255);

            HBRUSH brush = CreateSolidBrush(bg);
            FillRect(dc, &rc, brush);
            DeleteObject(brush);

            // Border
            FrameRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

            // Text
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));

            DrawText(
                dc,
                L"Save last 5s",
                -1,
                &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE
            );
        }
        break;
    }

    case WM_PLAYBACK_STARTED:
    case WM_PLAYBACK_ENDED: {
        HWND hBtnSave = GetDlgItem(g_hwndMain, ID_BTN_SAVE);
        InvalidateRect(hBtnSave, nullptr, TRUE);
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);

    } // end switch

    return DefWindowProc(h, m, w, l);
}

LRESULT CALLBACK SelectionWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {

    case WM_LBUTTONDOWN: {

        if (g_playing.load(std::memory_order_acquire)) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        int width = rc.right;
        int x = LOWORD(l);

        int xStart = int(gStartNorm * width);
        int xEnd = int(gEndNorm * width);

        if (abs(x - xStart) < 10) gDraggingStart = true;
        else if (abs(x - xEnd) < 10) gDraggingEnd = true;

        SetCapture(hwnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!gDraggingStart && !gDraggingEnd)
            return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right;

        int x = GET_X_LPARAM(l);
        float norm = Clampf((float)x / width, 0.0f, 1.0f);

        if (gDraggingStart) {
            gStartNorm = Clampf(norm, 0.0f, gEndNorm);
        }
        else if (gDraggingEnd) {
            gEndNorm = Clampf(norm, gStartNorm, 1.0f);
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        gDraggingStart = gDraggingEnd = false;

        int sampleCount = g_snapshot.size();
        int startSample = int(gStartNorm * sampleCount);
        int endSample = int(gEndNorm * sampleCount);

        gStartNorm = startSample / (float)sampleCount;

        g_playStart = startSample;
        g_playEnd = endSample;

        wprintf_s(L"Start: %d, End: %d\n", startSample, endSample);

        ReleaseCapture();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        RECT sliderRc = { 0, 0, rc.right, SLIDER_HEIGHT };
        RECT waveRc = {
            WAVEFORM_MARGIN,
            SLIDER_HEIGHT + WAVEFORM_MARGIN,
            rc.right - WAVEFORM_MARGIN,
            rc.bottom - WAVEFORM_MARGIN
        };

        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        DrawSlider(hdc, sliderRc);
        DrawWaveform(hdc, waveRc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int)
{

    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    _setmode(_fileno(stdout), _O_U16TEXT);

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.lpszClassName = L"AudioReplay";
    wc.hbrBackground = CreateSolidBrush(RGB(180, 180, 180)); // grey background
    RegisterClass(&wc);

    CoInitialize(nullptr);

    wprintf(L"Render (app -> device)\n");
    ListClients(eRender);

    wprintf(L"Render (device -> app)\n");
    ListClients(eCapture);


    g_hwndMain = CreateWindow(wc.lpszClassName, L"WASAPI Instant Replay",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        200, 200, 600, 400,
        nullptr, nullptr, h, nullptr);

    WNDCLASS swc = {};
    swc.lpfnWndProc = SelectionWndProc;
    swc.hInstance = h;
    swc.lpszClassName = L"SliderClass";
    RegisterClass(&swc);

    selectionHwnd = CreateWindow(L"SliderClass", nullptr,
        WS_CHILD | WS_VISIBLE,
        180, 30, 400, 100,
        g_hwndMain, nullptr, h, nullptr
    );
    

    CreateWindow(L"BUTTON", L"Save last 5s",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        20, 20, 130, 45,
        g_hwndMain, (HMENU)ID_BTN_SAVE, h, nullptr);

    CreateWindow(L"BUTTON", L"Play",
        WS_CHILD | WS_VISIBLE,
        20, 90, 130, 45,
        g_hwndMain, (HMENU)ID_BTN_PLAY, h, nullptr);

    std::thread(AudioThreads::CaptureThread)
        .detach();
    std::thread(AudioThreads::PlaybackThread).detach();
    std::thread(AudioThreads::MicToCableThread).detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}



#pragma once
#include <atomic>
#include <vector>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <windows.h>

constexpr int SAMPLE_RATE = 48000; // probably will change
constexpr int CHANNELS = 2;
constexpr int SECONDS = 5;
constexpr size_t BUFFER_SAMPLES = SAMPLE_RATE * CHANNELS * SECONDS;

extern std::vector<float> g_ring;
extern std::vector<float> g_snapshot;

extern std::atomic<size_t> g_writePos;
extern std::atomic<size_t> g_playPos;
extern size_t g_playStart;
extern size_t g_playEnd;
extern std::atomic<bool>   g_playing;

extern HWND g_hwndMain;

namespace AudioGlobalUtils {
	void Push(const float* in, size_t n);
	IMMDevice* FindRenderDeviceByName(const wchar_t* target);
};

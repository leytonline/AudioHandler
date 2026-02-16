#pragma once
#define NOMINMAX
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "AudioGlobals.h"
#include "AudioMessages.h"
#include <algorithm>
#include <cmath>
#include <bungee/Stream.h>

const Bungee::SampleRates BUNGEE_SAMPLE_RATES { 48000, 48000 };

namespace AudioThreads {
	void CaptureThread();
	void PlaybackThread();
	void MicToCableThread();
}
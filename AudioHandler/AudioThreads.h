#pragma once
#define NOMINMAX
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "AudioGlobals.h"
#include "AudioMessages.h"
#include <algorithm>
#include <cmath>

namespace AudioThreads {
	void CaptureThread();
	void PlaybackThread();
	void MicToCableThread();
}
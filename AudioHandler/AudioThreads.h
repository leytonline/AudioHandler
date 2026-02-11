#pragma once
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "AudioGlobals.h"
#include "AudioMessages.h"

namespace AudioThreads {
	void CaptureThread();
	void PlaybackThread();
	void MicToCableThread();
}
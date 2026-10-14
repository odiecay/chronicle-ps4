#pragma once

using AudioRenderFn = void (*)(void *user, float *out, int frames);

// Opens the default playback device as a float stereo stream at rate and pulls interleaved
// frames from render on SDL's audio thread. Returns false when there is no device, or when
// <prefix>_AUDIO is "off"; the caller then simply has no output. With <prefix>_AUDIO_WAV set to a path, no
// device is opened: render is pulled on the game thread as the clock ticks and written there as
// 16-bit stereo WAV.
bool AudioOutputStart(int rate, AudioRenderFn render, void *user);

void AudioOutputStop();

// Surround upmix of the stereo mix to 5.1 (front pair kept, centre from the sum, bass from its low
// end, rear pair from the difference, delayed and softened). SDL folds it down on a device with
// fewer speakers. Applies at once to a running stream.
void AudioSetSurround(bool on);

bool AudioOutputRunning();

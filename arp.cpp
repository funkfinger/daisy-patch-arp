#include <cmath>

#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// Hardware object
daisy::patch_sm::DaisyPatchSM hw;

// Breathing LED variables
float breath_phase = 0.0f;
const float breath_speed = 0.002f;  // Speed of breathing

// Audio callback function
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  // Process CV and Gate inputs
  hw.ProcessAllControls();

  for (size_t i = 0; i < size; i++) {
    // Pass through audio for now
    out[0][i] = in[0][i];
    out[1][i] = in[1][i];
  }
}

int main(void) {
  // Initialize hardware
  hw.Init();
  hw.SetAudioBlockSize(4);  // Number of samples handled per callback
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  // Start audio
  hw.StartAudio(AudioCallback);

  // Loop forever
  while (1) {
    // Update breathing LED animation
    breath_phase += breath_speed;
    if (breath_phase >= 1.0f) {
      breath_phase -= 1.0f;
    }

    // Create smooth breathing effect using sine wave
    float breath_value = (sinf(breath_phase * 2.0f * M_PI) + 1.0f) * 0.5f;

    // Set CV output 2 for LED (0-5V range)
    hw.WriteCvOut(patch_sm::CV_OUT_2, breath_value * 5.0f);

    // Small delay to control update rate
    hw.Delay(1);
  }
}

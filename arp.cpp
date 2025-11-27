#include <cmath>

#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// Hardware object
daisy::patch_sm::DaisyPatchSM hw;

// Arpeggiator state
float base_note_cv = 0.0f;        // CV input for base note (1V/octave)
int quantized_note = 0;           // Quantized MIDI note number (C4 = 60)
float bpm = 120.0f;               // Detected BPM
uint32_t last_gate_time = 0;      // Time of last gate trigger
bool gate_triggered = false;      // Gate trigger flag
int arp_step = 0;                 // Current step in arpeggio (0-3)
uint32_t last_step_time = 0;      // Time of last arpeggio step
float step_interval_ms = 125.0f;  // Time between steps (ms) - 120 BPM = 500ms
                                  // per quarter, 4 notes = 125ms each

// Dominant 7th chord intervals (in semitones from root)
const int chord_intervals[4] = {0, 4, 7,
                                10};  // Root, Major 3rd, Perfect 5th, Minor 7th

// Function to quantize CV to nearest semitone and return MIDI note number
int QuantizeCvToNote(float cv) {
  // 1V/octave standard: 0V = C4 (MIDI 60)
  // Each semitone = 1/12 V = 0.0833V
  float note_float = cv * 12.0f + 60.0f;        // Convert to MIDI note
  return static_cast<int>(roundf(note_float));  // Quantize to nearest semitone
}

// Function to convert MIDI note to CV (1V/octave)
float NoteToCv(int midi_note) { return (midi_note - 60) / 12.0f; }

// Audio callback function
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
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
    // Process all controls (CV and Gate inputs)
    hw.ProcessAllControls();

    // Read CV input 1 for base note (bipolar -5V to +5V)
    base_note_cv = hw.GetAdcValue(patch_sm::CV_1);

    // Quantize to nearest semitone
    quantized_note = QuantizeCvToNote(base_note_cv);

    // Check gate input 1 for trigger
    if (hw.gate_in_1.Trig()) {
      uint32_t current_time = System::GetNow();

      if (last_gate_time > 0) {
        // Calculate BPM from time between gates
        uint32_t interval_ms = current_time - last_gate_time;
        if (interval_ms > 0) {
          // 1 gate = 1 quarter note
          bpm = 60000.0f / interval_ms;
          // Update step interval (4 notes per quarter note)
          step_interval_ms = interval_ms / 4.0f;
        }
      }

      last_gate_time = current_time;
      gate_triggered = true;

      // Reset arpeggio to start
      arp_step = 0;
      last_step_time = current_time;
    }

    // Arpeggiator logic - play notes if gate has been triggered
    if (gate_triggered) {
      uint32_t current_time = System::GetNow();

      // Check if it's time for the next step
      if (current_time - last_step_time >= step_interval_ms) {
        // Calculate the note for this step
        int note_offset = chord_intervals[arp_step];
        int current_note = quantized_note + note_offset;
        float output_cv = NoteToCv(current_note);

        // Output CV for the current note
        hw.WriteCvOut(patch_sm::CV_OUT_1,
                      output_cv + 2.5f);  // Offset to 0-5V range

        // Output gate high
        hw.gate_out_1.Write(true);

        // Move to next step
        arp_step = (arp_step + 1) % 4;
        last_step_time = current_time;

        // Gate will be turned off in next iteration (short pulse)
      } else if (current_time - last_step_time > 10) {
        // Turn off gate after 10ms (short pulse)
        hw.gate_out_1.Write(false);
      }
    }

    // Visual feedback - use CV OUT 2 for LED breathing based on BPM
    float led_phase = fmodf((System::GetNow() % 1000) / 1000.0f, 1.0f);
    float led_value = (sinf(led_phase * 2.0f * M_PI) + 1.0f) * 0.5f;
    hw.WriteCvOut(patch_sm::CV_OUT_2, led_value * 5.0f);

    // Small delay to control update rate
    hw.Delay(1);
  }
}

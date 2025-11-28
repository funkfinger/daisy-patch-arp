#include <cmath>

#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// Hardware object
daisy::patch_sm::DaisyPatchSM hw;

// Button for internal clock toggle
Switch clock_button;

// Arpeggiator state
float base_note_cv = 0.0f;         // CV input for base note (1V/octave)
int quantized_note = 0;            // Quantized MIDI note number (C4 = 60)
int next_quantized_note = 0;       // Next note to use (after pattern completes)
bool note_change_pending = false;  // Flag to indicate note change is waiting
float bpm = 120.0f;                // Detected BPM
uint32_t last_gate_time = 0;       // Time of last gate trigger
bool gate_triggered = false;       // Gate trigger flag
int arp_step = 0;                  // Current step in arpeggio (0-3)
uint32_t last_step_time = 0;       // Time of last arpeggio step
float step_interval_ms = 125.0f;   // Time between steps (ms) - 120 BPM = 500ms
                                   // per quarter, 4 notes = 125ms each

// Internal clock state
bool internal_clock_enabled = false;      // Toggle for internal clock
uint32_t last_internal_clock_time = 0;    // Time of last internal clock tick
const float internal_clock_bpm = 120.0f;  // Internal clock BPM
const float internal_clock_interval_ms =
    500.0f;  // 120 BPM = 500ms per quarter note

// Dominant 7th chord intervals (in semitones from root)
const int chord_intervals[4] = {0, 4, 7,
                                10};  // Root, Major 3rd, Perfect 5th, Minor 7th

// Arpeggio pattern types
enum ArpPattern {
  ARP_UP = 0,        // 0, 1, 2, 3 (4 steps)
  ARP_DOWN,          // 3, 2, 1, 0 (4 steps)
  ARP_UP_DOWN_4,     // 0, 1, 2, 3 (4 steps, truncated)
  ARP_DOWN_UP_4,     // 3, 2, 1, 0 (4 steps, truncated)
  ARP_UP_DOWN_8,     // 0, 1, 2, 3, 3, 2, 1, 0 (8 steps, full bounce)
  ARP_DOWN_UP_8,     // 3, 2, 1, 0, 0, 1, 2, 3 (8 steps, full bounce)
  ARP_RANDOM,        // Random order (4 steps)
  ARP_1_3_2_4,       // 0, 2, 1, 3 (4 steps, custom pattern)
  ARP_PATTERN_COUNT  // Total number of patterns
};

// Pattern sequences (max 8 steps)
const int pattern_up[4] = {0, 1, 2, 3};
const int pattern_down[4] = {3, 2, 1, 0};
const int pattern_up_down_4[4] = {0, 1, 2, 3};
const int pattern_down_up_4[4] = {3, 2, 1, 0};
const int pattern_up_down_8[8] = {0, 1, 2, 3, 3, 2, 1, 0};
const int pattern_down_up_8[8] = {3, 2, 1, 0, 0, 1, 2, 3};
const int pattern_1_3_2_4[4] = {0, 2, 1, 3};

// Pattern lengths
const int pattern_lengths[ARP_PATTERN_COUNT] = {
    4,  // UP
    4,  // DOWN
    4,  // UP_DOWN_4
    4,  // DOWN_UP_4
    8,  // UP_DOWN_8
    8,  // DOWN_UP_8
    4,  // RANDOM
    4   // 1_3_2_4
};

// Current pattern
ArpPattern current_pattern = ARP_UP;
int pattern_length = 4;

// Function to quantize CV to nearest semitone and return MIDI note number
int QuantizeCvToNote(float cv_normalized) {
  // cv_normalized is -1.0 to 1.0 representing -5V to 5V
  // Convert to actual voltage
  float cv_volts = cv_normalized * 5.0f;

  // 1V/octave standard: C0 (MIDI 12) = 0.0833V (1/12 volt)
  // This means 0V = B-1 (MIDI 11)
  // Each semitone = 1/12 V = 0.0833V
  // Formula: voltage = (midi_note - 11) / 12
  // Inverse: midi_note = (voltage * 12) + 11
  float note_float = cv_volts * 12.0f + 11.0f;  // Convert to MIDI note
  return static_cast<int>(roundf(note_float));  // Quantize to nearest semitone
}

// Function to convert MIDI note to CV voltage (1V/octave)
// Returns voltage value (not normalized)
float NoteToCv(int midi_note) {
  // 1V/octave standard: C0 (MIDI 12) = 0.0833V
  // 0V = B-1 (MIDI 11)
  return (midi_note - 11) / 12.0f;
}

// Function to get the chord interval index based on current pattern and step
int GetPatternIndex(ArpPattern pattern, int step) {
  switch (pattern) {
    case ARP_UP:
      return pattern_up[step % 4];
    case ARP_DOWN:
      return pattern_down[step % 4];
    case ARP_UP_DOWN_4:
      return pattern_up_down_4[step % 4];
    case ARP_DOWN_UP_4:
      return pattern_down_up_4[step % 4];
    case ARP_UP_DOWN_8:
      return pattern_up_down_8[step % 8];
    case ARP_DOWN_UP_8:
      return pattern_down_up_8[step % 8];
    case ARP_RANDOM:
      return rand() % 4;  // Random 0-3
    case ARP_1_3_2_4:
      return pattern_1_3_2_4[step % 4];
    default:
      return pattern_up[step % 4];
  }
}

// Function to select pattern based on CV_1 input
ArpPattern SelectPattern(float cv_normalized) {
  // cv_normalized is -1.0 to 1.0, map to 0.0 to 1.0
  float cv_0_to_1 = (cv_normalized + 1.0f) / 2.0f;

  // Divide range into equal segments for each pattern
  int pattern_index = static_cast<int>(cv_0_to_1 * ARP_PATTERN_COUNT);

  // Clamp to valid range
  if (pattern_index < 0) pattern_index = 0;
  if (pattern_index >= ARP_PATTERN_COUNT) pattern_index = ARP_PATTERN_COUNT - 1;

  return static_cast<ArpPattern>(pattern_index);
}

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

  // Initialize toggle switch on B8 for internal clock
  clock_button.Init(daisy::patch_sm::DaisyPatchSM::B8, 1000.0f,
                    Switch::TYPE_TOGGLE, Switch::POLARITY_INVERTED,
                    GPIO::Pull::PULLUP);

  // Start audio
  hw.StartAudio(AudioCallback);

  // Loop forever
  while (1) {
    // Process all controls (CV and Gate inputs)
    hw.ProcessAllControls();

    // Debounce the clock toggle switch
    clock_button.Debounce();

    // Read the toggle switch state directly (on = internal clock, off =
    // external)
    bool prev_internal_clock_enabled = internal_clock_enabled;
    internal_clock_enabled = clock_button.Pressed();

    // If we just enabled internal clock, reset timing
    if (internal_clock_enabled && !prev_internal_clock_enabled) {
      last_internal_clock_time = System::GetNow();
      bpm = internal_clock_bpm;
      step_interval_ms = internal_clock_interval_ms / 4.0f;
    }

    // If we just disabled internal clock, reset the gate trigger state
    if (!internal_clock_enabled && prev_internal_clock_enabled) {
      gate_triggered = false;
      last_gate_time = 0;
    }

    // Read CV input 1 for pattern selection (bipolar -5V to +5V)
    float pattern_cv = hw.GetAdcValue(patch_sm::CV_1);
    ArpPattern new_pattern = SelectPattern(pattern_cv);

    // Update pattern if it changed
    if (new_pattern != current_pattern) {
      current_pattern = new_pattern;
      pattern_length = pattern_lengths[current_pattern];
      arp_step = 0;  // Reset step when pattern changes
    }

    // Read CV input 5 for base note (bipolar -5V to +5V)
    base_note_cv = hw.GetAdcValue(patch_sm::CV_5);

    // Quantize to nearest semitone
    int new_note = QuantizeCvToNote(base_note_cv);

    // Check if the note has changed
    if (new_note != quantized_note && new_note != next_quantized_note) {
      // If we're in the middle of a pattern, queue the change
      if (arp_step != 0 && gate_triggered) {
        next_quantized_note = new_note;
        note_change_pending = true;
      } else {
        // If at the start of a pattern or not playing, change immediately
        quantized_note = new_note;
        next_quantized_note = new_note;
        note_change_pending = false;
      }
    }

    // Handle clock source - either internal or external gate
    bool clock_trigger = false;

    if (internal_clock_enabled) {
      // Use internal clock at 120 BPM (only when switch is ON)
      uint32_t current_time = System::GetNow();
      if (current_time - last_internal_clock_time >=
          internal_clock_interval_ms) {
        clock_trigger = true;
        last_internal_clock_time = current_time;
      }
    } else {
      // Use external gate input (when switch is OFF)
      clock_trigger = hw.gate_in_1.Trig();
    }

    // Check for clock trigger (either internal or external)
    if (clock_trigger) {
      uint32_t current_time = System::GetNow();

      // Only calculate BPM from external gate timing
      if (!internal_clock_enabled && last_gate_time > 0) {
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
        // If we're at step 0 and there's a pending note change, apply it now
        if (arp_step == 0 && note_change_pending) {
          quantized_note = next_quantized_note;
          note_change_pending = false;
        }

        // Get the chord index based on current pattern and step
        int chord_index = GetPatternIndex(current_pattern, arp_step);

        // Calculate the note for this step
        int note_offset = chord_intervals[chord_index];
        int current_note = quantized_note + note_offset;
        float output_cv = NoteToCv(current_note);

        // Output CV for the current note (0-5V range)
        // Clamp to valid DAC range
        if (output_cv < 0.0f) output_cv = 0.0f;
        if (output_cv > 5.0f) output_cv = 5.0f;
        hw.WriteCvOut(patch_sm::CV_OUT_1, output_cv);

        // Output gate high
        hw.gate_out_1.Write(true);

        // Move to next step (wrap around based on pattern length)
        arp_step = (arp_step + 1) % pattern_length;
        last_step_time = current_time;

        // Gate will be turned off in next iteration (short pulse)
      } else if (current_time - last_step_time > 10) {
        // Turn off gate after 10ms (short pulse)
        hw.gate_out_1.Write(false);
      }
    } else {
      // If not triggered, make sure gate is off
      hw.gate_out_1.Write(false);
    }

    // Visual feedback - use CV OUT 2 for LED breathing based on BPM
    float led_phase = fmodf((System::GetNow() % 1000) / 1000.0f, 1.0f);
    float led_value = (sinf(led_phase * 2.0f * M_PI) + 1.0f) * 0.5f;
    hw.WriteCvOut(patch_sm::CV_OUT_2, led_value * 5.0f);

    // Small delay to control update rate
    hw.Delay(1);
  }
}

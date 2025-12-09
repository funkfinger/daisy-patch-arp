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
bool internal_clock_enabled = false;    // Toggle for internal clock
uint32_t last_internal_clock_time = 0;  // Time of last internal clock tick

// Tempo control constants
const float MIN_BPM = 20.0f;
const float MAX_BPM = 200.0f;

// Dominant 7th chord intervals (in semitones from root)
const int chord_intervals[4] = {0, 4, 7,
                                10};  // Root, Major 3rd, Perfect 5th, Minor 7th

// Arpeggio pattern types
enum ArpPattern {
  ARP_UP = 0,        // 0, 1, 2, 3 (4 steps)
  ARP_DOWN,          // 3, 2, 1, 0 (4 steps)
  ARP_UP_DOWN,       // 0, 1, 2, 3, 2, 1 (6 steps, smooth bounce)
  ARP_DOWN_UP,       // 3, 2, 1, 0, 1, 2 (6 steps, smooth bounce)
  ARP_RANDOM,        // Random order (4 steps)
  ARP_1_3_2_4,       // 0, 2, 1, 3 (4 steps, custom pattern)
  ARP_PATTERN_COUNT  // Total number of patterns
};

// Pattern sequences
const int pattern_up[4] = {0, 1, 2, 3};
const int pattern_down[4] = {3, 2, 1, 0};
const int pattern_up_down[6] = {0, 1, 2, 3, 2, 1};
const int pattern_down_up[6] = {3, 2, 1, 0, 1, 2};
const int pattern_1_3_2_4[4] = {0, 2, 1, 3};

// Pattern lengths
const int pattern_lengths[ARP_PATTERN_COUNT] = {
    4,  // UP
    4,  // DOWN
    6,  // UP_DOWN
    6,  // DOWN_UP
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
    case ARP_UP_DOWN:
      return pattern_up_down[step % 6];
    case ARP_DOWN_UP:
      return pattern_down_up[step % 6];
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
  // CV inputs return -1.0 to 1.0 for bipolar (-5V to +5V)
  // But pots on Patch.init() are wired 0-5V, so we get roughly 0.0 to 1.0
  // Map the input range to 0.0 to 1.0 for pattern selection
  float cv_0_to_1;
  if (cv_normalized < 0.0f) {
    // Handle any negative values (bipolar CV input)
    cv_0_to_1 = (cv_normalized + 1.0f) / 2.0f;
  } else {
    // Pot gives 0.0 to 1.0 range directly
    cv_0_to_1 = cv_normalized;
  }

  // Clamp to 0-1 range
  if (cv_0_to_1 < 0.0f) cv_0_to_1 = 0.0f;
  if (cv_0_to_1 > 1.0f) cv_0_to_1 = 1.0f;

  // Divide range into equal segments for each pattern
  int pattern_index = static_cast<int>(cv_0_to_1 * ARP_PATTERN_COUNT);

  // Clamp to valid range
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

    // Read CV_2 for tempo control
    // Pots on Patch.init() return 0 to 1 range
    float tempo_cv = hw.GetAdcValue(patch_sm::CV_2);
    // Handle pot (0 to 1) or bipolar CV (-1 to +1)
    float tempo_cv_normalized;
    if (tempo_cv < 0.0f) {
      // Bipolar CV input: convert -1..+1 to 0..1
      tempo_cv_normalized = (tempo_cv + 1.0f) / 2.0f;
    } else {
      // Pot: already 0..1
      tempo_cv_normalized = tempo_cv;
    }
    // Clamp to 0-1 range
    if (tempo_cv_normalized < 0.0f) tempo_cv_normalized = 0.0f;
    if (tempo_cv_normalized > 1.0f) tempo_cv_normalized = 1.0f;
    float tempo_bpm = MIN_BPM + tempo_cv_normalized * (MAX_BPM - MIN_BPM);

    // Update tempo from pot when internal clock is enabled
    if (internal_clock_enabled) {
      bpm = tempo_bpm;
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
      // Recalculate step interval for new pattern length
      float quarter_note_ms = 60000.0f / bpm;
      step_interval_ms = quarter_note_ms / (float)pattern_length;
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
    if (internal_clock_enabled) {
      // Internal clock mode: BPM controls note rate directly
      // Calculate step interval from BPM (each beat = one note)
      float step_interval_from_bpm = 60000.0f / bpm;
      step_interval_ms = step_interval_from_bpm;

      // Auto-start the arpeggio if not already triggered
      if (!gate_triggered) {
        gate_triggered = true;
        arp_step = 0;
        last_step_time = System::GetNow();
      }
    } else {
      // External gate input mode (when switch is OFF)
      bool clock_trigger = hw.gate_in_1.Trig();

      if (clock_trigger) {
        uint32_t current_time = System::GetNow();

        // Calculate BPM from time between gates
        if (last_gate_time > 0) {
          uint32_t interval_ms = current_time - last_gate_time;
          if (interval_ms > 0) {
            // 1 gate = 1 quarter note
            bpm = 60000.0f / interval_ms;
            // Update step interval based on pattern length
            step_interval_ms = interval_ms / (float)pattern_length;
          }
        }

        last_gate_time = current_time;
        gate_triggered = true;

        // Reset arpeggio to start
        arp_step = 0;
        last_step_time = current_time;
      }
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

    // Visual feedback - blink LED at tempo rate
    // Calculate ms per beat from BPM
    float ms_per_beat = 60000.0f / bpm;
    // Get current position within the beat (0.0 to 1.0)
    float beat_phase = fmodf(System::GetNow(), ms_per_beat) / ms_per_beat;
    // LED on for first 25% of beat, off for rest
    float led_value = (beat_phase < 0.25f) ? 5.0f : 0.0f;
    hw.WriteCvOut(patch_sm::CV_OUT_2, led_value);

    // Small delay to control update rate
    hw.Delay(1);
  }
}

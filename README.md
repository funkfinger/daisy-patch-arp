# Daisy Patch SM Based Arpeggiator

> A simple arpeggiator based on the Daisy Patch SM platform. This is a work in progress and is not yet complete. Also, heavily using AI to create this project, so use at your own risk.

## Instructions for AI

- CV 1v/octave input for the base key. This should be quantized.
- Gate input for the trigger - this will determine the BPM.
- To begin with, only use 1 gate trigger per 1/4 note. This will likely need to change later.
- To begin with, I would like to play 4 notes per input key - these can play up dominant 7th chords. For example a C7 would play C, E, G, Bb.

### Concepts from Perplexity AI

#### Map Patch.init() Hardware to Functions

Given Patch.init()'s fixed panel, a practical first-pass mapping for your arp engine could be:

**Knobs:**
- **K1:** Rate / clock division (or "swing" when externally clocked)
- **K2:** Pattern / bounce-jump selection (or direction)
- **K3:** Octave range / transposition
- **K4:** Density / gate length macro

**CV Inputs (normal use):**
- **CV1:** Transpose (1V/oct add to internal pitch pool)
- **CV2:** Density / probability (affects how often steps fire)
- **CV3:** Pattern index / "super parameter" (morphs bounce/jump/repeat presets)
- **CV4:** Scale or chord-degree select (quantize incoming chord source)

**Gates:**
- **Gate In 1:** External clock
- **Gate In 2:** Reset / run
- **Gate Out 1:** Main track gate
- **Gate Out 2:** Accent / fill / "next scene" trigger

**CV Out:**
- Pitch out for primary track (1V/oct)

**Button / Toggle / LED:**
- **Button:** Tap tempo / shift / hold-latch
- **Toggle:** Mode (Edit vs Performance, or Internal vs External clock)
- **LED:** Clock / activity / error indication

---

#### Feature Scope for a Patch.init Arp v1

Within Patch.init's constraints, a focused v1 feature set that still nods to Midicake ARP would be:

**Core:**
- Single main CV/gate track with external clock support and reset
- Directions: up, down, up/down, random, as-played, and maybe "chord order"
- Octave range, transposition, gate length, and note repeat count

**Harmonic:**
- Built-in scales; quantize output to selected scale
- Chord-aware: take a buffered set of input notes (e.g., via CV+gate or an internal table) and arp over them
- Optional simple chord-degree stepping via CV input (CV4)

**Rhythmic / Parametric:**
- Per-pattern settings for:
  - Step count and clock division (for polymetric fun against external clock)
  - Bounce/jump style patterns that define note order instead of a simple direction
  - Density / probability to thin out steps
- A handful of "super patterns" where CV3 morphs between algorithmic behaviors (e.g., straight, Euclidean-ish, random-walk, octave-skipping)

**Performance:**
- Scene/preset recall from SD (or a small internal bank) to change complete parameter sets
- Button combos for "panic", "reload scene", and quick mute/hold

---

If you share how many tracks you want active in v1 (just one vs a pseudo-second track derived from the first), the next step can be a more concrete control map and minimal parameter set for your DSP data structures, tailored around Patch.init's exact controls.

# Changelog

## v1.9.1

- **UI & Suite Fixes:** UI polish, 11 px minimum labels, LENGTH/RATE grid accuracy, unit formatting and typed-value entry, peak-hold meters with clip latch, per-slot A/B undo, inline confirms, uniform scaling, and screen-reader titles.

## v1.9.0 — 2026-11-20

- **Dual-head Buffer Stutter**: Crossfades overlapping read heads at loop wraps and retriggers to eliminate tremolo pumping at 1/64 divisions.
- **Circular Varispeed Tape Stop**: Uses a continuous ring buffer with dynamic polyphase sinc resampling for artifact-free deceleration beyond 15 seconds.
- **Dual-grain Reverse**: Hermite-interpolated overlapping grains eliminate zero-dip dropouts across fractional beat wraps.
- **WSOLA Pitch Glitch**: Keeps drum transients tight within 22 ms without flam echoes.
- **2x Oversampled Bitrate Crusher & PolyBLEP Decimator**: Suppresses foldback aliasing in sample-and-hold and bit-depth reduction.
- **12-stage ZDF Phaser & Thiran Flanger**: Multi-notch dispersion and fractional allpass delay without click spikes.
- **SVF Band-Limited Ping-Pong Echo & Post-Fader Send Gating**: Band-limits repeats to 120 Hz–7 kHz, morphs tempo changes smoothly, and lets Ping-Pong and Comb Resonator tails ring across disabled steps.
- **Per-sample Smoothed ZDF Wet Filter**: Smooths resonant cutoff jumps per sample without zipper or crackle spikes.
- **LR4 24 dB/oct Keep Sub Crossover**: Blocks uncorrelated wet sub bleed and sums flat with the dry signal.
- **Asymmetric Category Micro-Envelopes & Multi-FX Gain Staging**: Per-effect attack/release shaping, RMS loudness guards on nonlinear effects, and parallel time-vs-spectral FX bus summing.

## 1.8.0 — 2026-11-19

- Added 36 factory patterns. A new plugin opens on Club Stutter instead of an empty grid. The same list is in the editor and the DAW program list.
- Added undo and redo for knob moves, pad edits, and randomize, with editor buttons and Cmd/Ctrl+Z.
- Added A/B compare and user presets saved under Documents/Alan Marcero/openGlitchFX/Presets. Factory patterns stay read-only.
- The build also produces Audio Unit and Standalone. Existing VST3 zip names are unchanged.

## 1.6.0 — 2026-11-17

- A segmented SVG waveform scans the header on the sequencer clock, follows host seeks and holds while transport is stopped.
- All four dark themes now style routing menus, tooltips and text entry. Bypassed modules and the master switch display OFF.

## 1.5.0 — 2026-11-16

- Added an automatable sequencer length (1–32 steps) and swing (0–100%) control.
- Added seeded pattern randomization at three density levels. An FX brush selection scopes randomization to that route and preserves other pad state.
- Refreshed the native editor screenshot and documented commercial feature research.

## 1.2.1 — 2026-11-06

- Card subtitles name the process. The README keeps the sequencer, effect, and install facts without the brochure wording.
- macOS builds run on the macos-15 runner. Universal arm64/x86_64, the macOS 11 deployment target, unit tests, and pluginval strictness level 5 are unchanged.
- No DSP, parameter, or routing changes.

## 1.2.0 — 2026-10-27

- Reverse plays one long reversal across consecutive active pads, including runs across the bar line. Removed the repeating 256-sample cold-start fallback and extended history for 12-second spans. Cold starts capture a complete region before reversing.
- Fixed first-sample triggers and wrapped envelope phases. Verified all 15 effects sustain across adjacent pads, mixed routing and audio-block boundaries.
- Tape Stop's ramp now covers the complete pad span. Stutter respects synced lengths at slow host tempos. Wavefold keeps a consistent bass response across sample rates.
- Added a third row: Chorus, Flanger, Phaser, Ping-Pong Echo and antialiased Saturation. Added pad painting, context menus, routing LEDs, MIDI notes 70–74 and session storage for them.
- Preserved existing parameter IDs and routing automation ranges. New effects load at zero amount in older sessions.
- Added a resizable editor and derive its displayed version from the build version.

See [DSP notes and algorithm sources](docs/DSP.md) for details.

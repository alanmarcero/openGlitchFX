# Changelog

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

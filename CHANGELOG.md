# Changelog

## 1.2.0 — 2026-10-27

- Reverse plays one long reversal across consecutive active pads, including runs across the bar line. Removed the repeating 256-sample cold-start fallback and extended history for 12-second spans. Cold starts capture a complete region before reversing.
- Fixed first-sample triggers and wrapped envelope phases. Verified all 15 effects sustain across adjacent pads, mixed routing and audio-block boundaries.
- Tape Stop's ramp now covers the complete pad span. Stutter respects synced lengths at slow host tempos. Wavefold keeps a consistent bass response across sample rates.
- Added a third row: Chorus, Flanger, Phaser, Ping-Pong Echo and antialiased Saturation. Added pad painting, context menus, routing LEDs, MIDI notes 70–74 and session storage for them.
- Preserved existing parameter IDs and routing automation ranges. New effects load at zero amount in older sessions.
- Added a resizable editor and derive its displayed version from the build version.

See [DSP notes and algorithm sources](docs/DSP.md) for details.

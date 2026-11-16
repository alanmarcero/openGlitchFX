# Commercial feature research

## Reference: Sugar Bytes Effectrix 2

The vendor describes Effectrix 2 as a step-based multi-effect sequencer with 32 steps, per-effect sequencing, swing, variable pattern length, pattern setups, and intelligent randomization. These are product capabilities used as reference points, not an attempt to copy its interface or implementation.

Primary vendor source: [Effectrix 2](https://sugar-bytes.de/effectrix2) (accessed 2026-11-16).

## Added to openGlitchFX 1.5.0

1. **Swing:** a host-automatable 0–100% parameter delays the odd step in each pair while leaving pair boundaries on the host grid.
2. **Variable sequence length:** a host-automatable 1–32 step parameter repeats the selected portion of the existing pattern.
3. **Scoped pattern randomization:** Light, Medium and Wild levels generate repeatable-density patterns. With no FX brush selected, active pads receive randomized routes from a level-specific FX pool. With a brush selected, only that FX route changes; pad activation and other routes are preserved.

## Scope and limits

The sequencer still stores 32 pad states and 32 route masks. A shorter length loops that prefix; it does not trim or erase the remaining pattern. The randomizer is a simple density and route generator. Effectrix 2's multiple pattern setups and more elaborate variation tools are outside this change.

# DSP notes for v1.2.0

## Continuous pad regions

The grid has 32 pads per bar; two pads make a sixteenth note. Each effect tracks its own consecutive active pads, even if other effects change between them. A gap starts a new capture. Runs can cross the bar boundary. The modulation envelopes also follow joined regions across that boundary.

Reverse reads the preceding region backwards at its original sample rate. Its history buffer holds twice the maximum supported span, so recording cannot overwrite the audio being reversed. A full bar at 20 BPM lasts 12 seconds. With no complete history yet, reverse passes dry audio for the capture region, then fades into the first complete reversal. It never substitutes a short repeated grain. Continuously enabled reverse captures another region at the end of each full-span reversal.

Tape Stop now uses the whole selected duration for its speed ramp. Stutter captures once per run, then repeats its selected slice; its repeats are intentional. Chopper and Pitch Glitch similarly retain their rhythmic modulation. Their oscillators and the other effects do not reset at internal pad boundaries. Stutter's synced capture supports the host's 20–300 BPM range.

## Algorithms and sources

These are original implementations based on published DSP structures, not copied library implementations.

| Processor | Implementation and reference |
| --- | --- |
| Chorus | Two interpolated delay taps per channel, with different modulation phases and 18/27 ms centers. Based on Julius O. Smith's [Chorus Effect](https://www.dsprelated.com/freebooks/pasp/Chorus_Effect.html). |
| Flanger | Modulated fractional delay around 3 ms, dry/delayed mixing and damped feedback. Based on Smith's [Flanging](https://www.dsprelated.com/freebooks/pasp/Flanging.html). |
| Phaser | Six first-order allpass stages per channel, swept logarithmically with stereo offsets and bounded feedback. Based on Smith's [Phasing with Allpass Filters](https://www.dsprelated.com/freebooks/pasp/Virtual_Analog_Example_Phasing.html). |
| Ping-Pong | Eighth-note delay with a mono send, alternating stereo feedback, low-pass damping and feedback below unity. Dry audio keeps its stereo image. Based on [Feedback Comb Filters](https://www.dsprelated.com/freebooks/pasp/Feedback_Comb_Filters.html). The delay advances silently through inactive pads; tails are audible only on routed pads. |
| Saturation | Tanh drive with first-order antiderivative antialiasing, using a stable log-cosh antiderivative, gain compensation and DC removal. Based on Parker, Zavalishin and Le Bivic's [DAFx 2016 paper](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf). This reduces aliasing; it does not eliminate it. |
| Wavefold | Retains the existing antiderivative antialiasing. The [DC blocker](https://www.dsprelated.com/freebooks/filters/DC_Blocker.html) now keeps the same cutoff in Hz at every host sample rate, preserving bass at 96/192 kHz. |

Bitcrush and Downsample retain their deliberate quantization and aliasing character. Ring Mod, Comb Reso, Pitch Glitch and the existing wet filter retain their established algorithms after the continuity audit.

## Verification

The tests check exact reverse sample order over short, half-second and 6/12-second spans, cold capture, transition smoothing and extending a region. A table-driven audio comparison covers all 15 effects across mixed routing, bar wrap and unequal audio-block sizes. Additional checks cover slow-tempo tape/stutter timing, echo direction and timing through gaps, Wavefold bass response at 44.1–192 kHz, and Saturation alias energy versus direct tanh clipping.

Plugin tests cover all 15 pad brushes, routing/amount state round trips, and loading older sessions after using the new effects. The original ten-bit routing parameter IDs, ranges and positions are preserved; the additional five bits use appended parameters. New effects default to zero amount and no pad assignments when absent from an older session. MIDI notes 60–74 now select individual effects, so notes 70–74 change from the old all-effects trigger to the new row.

Release validation: 41 CTest cases pass, the native editor was captured from the built VST3, and pluginval strictness level 5 passes. The enabled echo reports a conservative 40-second tail to the host.

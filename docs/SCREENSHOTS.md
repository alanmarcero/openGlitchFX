# Native UI screenshot

![openGlitchFX 1.8.0 editor](../assets/native-ui.png)

Captured from the default 1440 × 1120 editor in Cyber Neon with the Club Stutter factory patch. The eight-slice waveform appears between the step indicator and the rack title. The editor was rendered at 2× scale (2880 × 2240).

The native GUI test also checks the default and minimum sizes, all four theme menus, and how the waveform responds to transport changes at 48 and 96 kHz.

Screenshot SHA-256: `22a8464c50a2632814036573801e6c1edd73aa3ce85b815069af551f477df838`.


## OpenVST Manager preview

OpenVST Manager shows `docs/screenshots/preview.png` from `main` (see `screenshotPath` in its catalog), and the GitHub release body and README hero point at the same file. This is the single image to keep current. Refresh it with the main editor capture whenever the UI changes, and commit it before tagging a release:

```sh
build/openglitch_plugin_tests '[plugin][gui]'   # writes assets/native-ui.png
cp assets/native-ui.png docs/screenshots/preview.png
```

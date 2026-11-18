# Native UI screenshot

![openGlitchFX 1.6.0 editor](../assets/native-ui.png)

Captured on macOS from the v1.6.0 Release editor in Cyber Neon. The eight-slice waveform appears between the step indicator and the rack title. The 1440 × 1120 editor was rendered at 2× scale (2880 × 2240).

The native GUI test also checks the default and minimum sizes, all four theme menus, and how the waveform responds to transport changes at 48 and 96 kHz. All 49 local CTest cases passed.

Screenshot SHA-256: `a6e931db981dd6687bfd8633fc7a5f99f1c21ad6ffd562b52ccd230293524936`.


## OpenVST Manager preview

OpenVST Manager shows `docs/screenshots/preview.png` from `main` (see `screenshotPath` in its catalog), and the GitHub release body and README hero point at the same file. This is the single image to keep current. Refresh it with the main editor capture whenever the UI changes, and commit it before tagging a release:

```sh
build/openglitch_plugin_tests '[plugin][gui]'   # writes assets/native-ui.png
cp assets/native-ui.png docs/screenshots/preview.png
```

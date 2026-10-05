# Assblaster — unofficial software model of the Metasonix KV-100

A VST3 (plus standalone app) that *attempts* to recreate the character of the
Metasonix KV-100 "Assblaster" tube distortion unit. Built with JUCE, 4x oversampled,
stereo or mono, with an optional sidechain input that acts as the ring-mod carrier.

![UI](docs/ui.png)

> **Not affiliated with or endorsed by Metasonix.** "Metasonix" and "Assblaster" are
> their names; this is an independent, behavioural model built from Metasonix's public
> descriptions of the hardware. It is *not* a circuit-level emulation and has **not**
> been compared against a real unit. Treat it as "inspired by", not "identical to".

## Signal chain

```
in -> pentode preamp (V1 -> V2, screen voltage on V2)      TM-7 style
   -> pulser (Schmitt + hard-synced saw) + ring mod        TM-1 style
   -> thyratron relaxation VCO ("signal degradation")      TM-3 style
   -> bandpass filter (manual / envelope / "wah")
   -> vactrol noise gate (driven by the envelope follower)
   -> out          (CHAOS switch feeds the filter output back to the grid)
```

| Section | Control | What it does in this model |
|---|---|---|
| Preamp | Input | Gain into V1 (also sets envelope-follower / gate sensitivity) |
| | V2 Screen | Low = starved: more grit, less level. High = cleaner, louder. *(my reading of "overdrive without excessive loudness")* |
| | Level | Preamp output into the pulser |
| Pulser | switch / Amount | Blend of the pulser output into the signal |
| | Thresh | Schmitt trigger threshold, relative to recent signal peak |
| | Ratio | Sync-saw frequency as a multiple of the detected input pitch (non-integer values give hard-sync sweeps) |
| | Ring Mod | Ring-modulates with the **sidechain input** if connected, otherwise the VCO |
| Thyratron VCO | switch / Level | Adds the oscillator to the signal |
| | Pitch | Free-run frequency. Not pitch-accurate (about 1% flat), which fits its "special effect" role |
| | Grid | How hard the audio drives the thyratron control grid, modulating strike point (pitch + amplitude) |
| Filter | switch, Freq, Q | Wide-sweep bandpass (60 Hz to 12 kHz) |
| | Env | Envelope -> cutoff, up to 5 octaves, negative values invert |
| | Decay | Envelope-follower release |
| Gate / Out | Gate | Vactrol-style gate threshold (0 = off) |
| | Chaos | "Terminal edition" feedback switch. Gets wild with high Input |
| | Master, Mix | Plugin-side conveniences; not on the hardware |
| | Bypass | Bypass is latency-aligned and bit-exact (4 samples of reported latency) |

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.9 is downloaded automatically
(or pass `-DJUCE_DIR=/path/to/JUCE`; change `-DJUCE_GIT_TAG=` to try another version).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Assblaster_VST3
```

The plugin appears under `build/Assblaster_artefacts/Release/VST3/Assblaster.vst3`.
Copy it to your VST3 folder:

* **Windows:** `C:\Program Files\Common Files\VST3\`
* **macOS:** `~/Library/Audio/Plug-Ins/VST3/` (add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build; unsigned builds may need `xattr -cr Assblaster.vst3`)
* **Linux:** `~/.vst3/` (needs `libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`)

Add `--target Assblaster_Standalone` for a standalone app.

**No local toolchain?** Push this folder to a GitHub repo; `.github/workflows/build.yml`
builds Windows, macOS and Linux VST3s and attaches them as downloadable artifacts.

## Tests

```bash
cmake -B build -DASSBLASTER_BUILD_TESTS=ON
cmake --build build --target dsp_test pitch_test
./build/dsp_test && ./build/pitch_test
```

These are JUCE-free checks of the DSP core: levels per setting, finiteness, bounded output
in worst-case settings, gate behaviour, thyratron pitch and pulser pitch tracking.

## What was and wasn't verified

Verified (Linux x86-64, JUCE 8.0.9, GCC 13):

* Builds cleanly; passes **pluginval at strictness 5** (including editor and automation tests, 44.1/48/96 kHz, block sizes 32 to 1024).
* Processor-level tests: latency-aligned bypass, mono layout, sidechain carrier path, and an "everything maxed" case that stays finite and under 0 dBFS.
* DSP behaviour: screen knob trades level for distortion; envelope filter sweeps as intended; pulser locks to input pitch.

**Not** verified: Windows and macOS builds and the GitHub Actions workflow (written, never run); loading in any actual DAW; how it sounds next to a real KV-100.

## Licensing note

JUCE is dual-licensed (AGPLv3 or a commercial license). If you distribute binaries built
with it you must comply with one of those. Pick whichever fits your use; I'm not a lawyer.
The DSP code in `Source/AssblasterDSP.h` has no JUCE dependency.

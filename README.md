# Halfcock

A parked-wah plugin for macOS, modelled on the Fulltone Custom Shop WahFull: a
wah circuit with a knob where the treadle would be, for the fixed midrange
"half-cocked wah" sound. Built with JUCE 9 as a VST3, an AU and a standalone
app.

It is named "Halfcock" because WahFull and Fulltone are trademarks. It is a
sibling of [Sighs](https://github.com/nickdc8/sighs), and by default builds against that
project's JUCE checkout.

## Download

Prebuilt for macOS 12 or later, on Apple silicon and Intel. Get the zip from the [latest release](https://github.com/nickdc8/halfcock/releases/latest). It contains the VST3, the Audio Unit and the standalone app, and the release notes have the install steps.

## Install locations

Building installs the plugins automatically:

- VST3: `~/Library/Audio/Plug-Ins/VST3/Halfcock.vst3`
- AU: `~/Library/Audio/Plug-Ins/Components/Halfcock.component`
- Standalone: `build/Halfcock_artefacts/Release/Standalone/Halfcock.app`

## Controls

| Control | Range | What it does |
| --- | --- | --- |
| Frequency | 0 to 1 | Where the wah is parked. Clockwise is higher. |
| Mode | Wacked / Jimi / Shaft | Which wah circuit: Colorsound-style, Vox Clyde McCoy, early-70s CryBaby |
| Pickup Compensator | 0 to 1 | How hard the circuit is driven: 12 dB less to 12 dB more, noon is neutral |
| Volume | -10 to +10 dB | Output level of the effect, unity at noon |
| On/Off | switch | The footswitch. Off passes the input straight through. |

The pedal's expression jack has no socket here. Automate or MIDI-map
**Frequency** to get a moving wah.

Turning the Pickup Compensator up pushes the transistor and the inductor into
saturation sooner, which compresses and thickens the peak. Half of its gain is
taken back out at the output, so it gets 6 dB louder at full, not 12.

Sweep ranges, from the test program:

| Mode | Lowest | Highest |
| --- | --- | --- |
| Wacked | 435 Hz | 2.7 kHz |
| Jimi | 421 Hz | 2.2 kHz |
| Shaft | 342 Hz | 2.0 kHz |

## How it is modelled

All three modes are the classic two-transistor inductor wah with different
parts. The input passes through a resistor and capacitor to a node where an
inductor and a damping resistor go to ground. A transistor amplifies that node
by about 30 and inverts it. The wah pot sends a fraction of that back to the
node through a small capacitor, which makes the capacitor look up to 30 times
larger. That apparent capacitance resonates with the inductor, so the pot
moves the peak.

The engine solves that node directly, every sample, rather than fitting a
filter to it. The capacitors and inductor are trapezoidal companion models,
and Newton's method handles the two nonlinear parts:

- the transistor running out of headroom, as an asymmetric soft clip inside the
  feedback loop
- the inductor core saturating

Everything runs at 2x oversampling. Reported latency is 49 samples, and the
bypassed signal takes the same path so it stays aligned.

Part values per mode:

| | Wacked | Jimi | Shaft |
| --- | --- | --- | --- |
| Input resistor / capacitor | 68 k / 10 nF | 68 k / 10 nF | 68 k / 10 nF |
| Sweep capacitor | 6.8 nF | 10 nF | 10 nF |
| Inductor | 500 mH, 45 ohm | 500 mH, 33 ohm | 620 mH, 70 ohm |
| Resistor across inductor | 100 k | 33 k | 33 k |
| Emitter resistor | 330 | 510 | 390 |

### From a source

The Jimi column's 68 k, 10 nF, 10 nF, 500 mH, 33 k, the 1.5 k base resistor,
22 k collector resistor and 100 k pot are the standard Vox Clyde McCoy / V846
values. Fuzz Central's McCoy page confirms most of them, including that some
originals had 100 k across the inductor and that the emitter resistor varies
between 390, 470 and 510.

The control set and what the three modes are meant to be come from Fulltone's
manual and the Japanese distributor's manual.

### Estimates, not from a source

- **Everything that differs in Wacked and Shaft.** Fulltone publishes no
  schematic, so these are voicings in the right direction, not the pedal's
  values.
- Inductor winding resistances, transistor gain and bias point.
- Clip level and asymmetry, and where each inductor saturates.
- The pot taper. The knob is mapped so the peak moves evenly in pitch, and it
  uses the whole electrical range, which a treadle's rack does not.
- Output level. Each mode is trimmed to about +8 dB at the peak with the knob
  at noon. The real circuit peaks nearer +18 dB.
- The Pickup Compensator's range.

### Not modelled

- The 2.2 M input buffer. Its job is to stop the wah loading the pickups, and
  a plugin never sees the pickups.
- The bias network's 4.7 uF capacitor (treated as a perfect AC ground), the
  second transistor (treated as a perfect follower), and pot loading.
- The circuit inverts the signal; the plugin does not, so switching in and out
  does not flip polarity.
- Noise, battery sag, and part tolerances.

## Status

- The engine matches the circuit's transfer function in all three modes at
  five knob positions: peak frequency within 3%, peak gain and Q within 10%
  (in practice well under 1% each).
- It stays finite and bounded under full-scale noise and square waves with
  every control jumping each block.
- A headless test runs the processor at 44.1/48/96 kHz, mono and stereo, block
  sizes 32 to 1024. State save/restore works, bypass returns the input, and the
  editor renders.
- Not yet loaded in a real host.
- `auval -v aufx Hfck Ncox` fails with "didn't find the component", as it does
  for Sighs. The bundle is ad-hoc signed and installed in the right place.
  The cause is not known.
- The VST3 has not been run through a validator such as pluginval.
- How close it sounds to a real unit has not been judged by ear.

## Building

Requires Xcode and CMake. JUCE is not committed. The build uses, in order:

1. `-DJUCE_DIR=/path/to/JUCE` if given
2. a `JUCE/` directory in this project
3. `../univibe/JUCE`, the checkout Sighs uses

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 10
```

To get JUCE from scratch:

```sh
git clone --depth 1 --branch 9.0.5 https://github.com/juce-framework/JUCE.git JUCE
```

The build is native (arm64 on Apple silicon), minimum macOS 12.

## Tests

```sh
# DSP only, no JUCE: response against the circuit, levels, bypass, stress.
# Pass a path to also render an audition clip.
clang++ -std=c++20 -O2 tests/wah_test.cpp -o build/wah_test
build/wah_test build/halfcock_demo.wav

# The JUCE processor, driven the way a host would.
cmake --build build --target HalfcockProcessorTest -j 10
build/HalfcockProcessorTest_artefacts/Release/HalfcockProcessorTest
```

The audition clip is 36 seconds: Wacked, Jimi, then Shaft, each parked low,
parked in the middle, then swept. The second test also writes a snapshot of
the UI to `build/editor.png`; run it from the project root.

## Layout

- `Source/WahDSP.h`: the whole model, with no JUCE dependency. The part values
  for each mode are in the table at the top.
- `Source/PluginProcessor.*`: parameters, oversampling, state.
- `Source/PluginEditor.*`: the UI.
- `tests/`: the two test programs above.

## Sources

- [Fulltone CS WahFull manual](https://cdn.shopify.com/s/files/1/0845/8652/9070/files/CS-Wahfull_manual_USA.pdf)
- [WahFull user manual, Kikutani (Japanese)](https://www.kikutani.co.jp/wp-content/uploads/2025/04/WahFull.pdf)
- [Fuzz Central: Clyde McCoy wah](https://www.fuzzcentral.ssguitar.com/mccoy.php)

# Seraph for Noise Engineering Versio

A drone and pad engine, built from measurements of *Seraphim* by How To Disappear Completely, with a panel and note logic modelled on the Sdkc Instruments Helical.
Six voices, each a cluster of four slightly detuned copies. A dark 24 dB low-pass, a sub sine, and an eight-line reverb that can ring for about 30 seconds.

## How the notes work (the Helical idea)

Every voice plays its own note with its own swell. When a note ends, the voice works out the next one:

- the next **pitch** comes from the length of the last note,
- the next **length** comes from the new pitch: higher notes last longer (the length grows with the square root of the pitch ratio, so an octave up lasts about 1.4 times longer, three octaves up about 2.8 times).

Because every pitch has its own length, the voices drift in and out of step and make slow rhythms that are not tied to any tempo. The same seed always gives the same pattern. A short tap on the button makes a new seed.

## One flat panel

There are no Setup pages, no modes and nothing to pick up. Every knob and switch does what it says at once, and every knob takes CV.

**Knobs**

| Knob | Does |
|---|---|
| 1 Root | Root note, 37 steps from A#0 (29 Hz) to A#3. Glides to a new note (glide is capped at 2 s here). |
| 2 Scale | Eight scales, dark on the left to bright on the right: open fifths, minor pentatonic, hirajoshi, phrygian, dorian, major pentatonic, lydian, whole tone. LED 3 shows the scale colour. Changing it moves the notes at once. |
| 3 Voices | One to six voices, in steps. The 1 voice setting is a single melodic line. Voices fade in and out in about a second. |
| 4 Rate | How long notes last at the root: 0.12 s (fast, plucked runs) to 48 s (slow drone). Takes effect at once, even in the middle of a note. This is Helical's Helicity. |
| 5 Glide | How long notes slide to the next pitch: 0.01 s (stepped) to 12 s (long slides). |
| 6 Spread | Pitch range above the root. Fully left, every voice plays the root only (a wide chorus). Fully right, notes reach three octaves up, with the higher voices staying higher. Takes effect at once. |
| 7 Wave | Sine on the left to saw on the right, and the filter opens as it goes. The loudness is levelled across the knob. |

**Switches** (they slide left, centre and right)

| Switch | Left | Centre | Right |
|---|---|---|---|
| 1 Env | Soft: slow swell in, long fade, never fully silent | Even: symmetric swell | Pluck: fast attack, long decay |
| 2 Space | Room: about 4 s | Hall: about 10 s | Vast: about 30 s |

I assumed left is the first label printed beside each switch. That is not confirmed.

**Tap button** (same everywhere)

| Press | Does |
|---|---|
| Short | New seed: every voice starts a new note now, in a new pattern. (Helical's reload.) |
| Hold 0.8 s or more | Lock on or off. Locked, every voice repeats its current pitch and length, so the pattern loops. Scale and Spread still work while locked, and apply at once. |

**Gate input (Tap jack).** Each rising edge starts the next voice's note, going round the voices in turn. Use a clock or sequencer to play the notes. Locked, it just retriggers them.

**Audio inputs.** Not used in this version. The old input mix was part of Setup, which is gone.

Things that used to have knobs are now fixed at the values measured from the album: cluster 16 cents, drift 0.5, wow 0.4, noise bed 0.15, sub 0.5, width full, damping 0.4. Space switch sets reverb amount, size and decay together.

## LEDs

| LED | Shows |
|---|---|
| 1 | Output level (blue-white) |
| 2 | Note activity: brightens as voices swell (teal) |
| 3 | Scale colour: crimson (open fifths), magenta, violet, blue, teal, green, yellow, warm white (whole tone) |
| 4 | Events: white flash on a new seed, green flash on a gate, slow amber pulse while Locked |

## Files

| File | What it is |
|---|---|
| `seraph.h` | The engine. No libDaisy dependency, so it also builds on a computer. |
| `main.cpp` | Versio controls, LEDs, audio callback. |
| `Makefile` | Build file. Set `LIBDAISY_DIR` to your libDaisy folder. |

## Building

Every push to `main` builds in GitHub Actions and puts `Seraph.bin` on the "latest" release. To build by hand:

1. Install the Arm GNU toolchain, then build libDaisy once: run `make` inside the libDaisy folder.
2. Put this folder next to libDaisy, or edit `LIBDAISY_DIR` in the Makefile.
3. Run `make` in this folder. The result is `build/Seraph.bin`.
4. Flash with https://flash.daisy.audio/ (Chrome, File Upload tab). If something goes wrong, you can reflash a Noise Engineering firmware with their flash app.

## What is verified and what is not

Verified on a computer:
- `seraph.h` compiles with no warnings under `g++ -Wall -Wextra`, renders without NaNs, and was tested with AddressSanitizer and UBSan.
- A scripted run of the control logic in `main.cpp` (41 checks): every knob's range, all eight scales and six voice counts selectable, no flutter on step edges, switch mapping, tap short and hold, gate, and 3 minutes of random knob, switch, tap and gate input.
- Note behaviour (16 checks): Rate speeds up notes within a second, Spread 0 is root only and Spread 1 reaches three octaves, Lock repeats pitch and length, scale changes apply at once while locked, the same seed gives the same pattern, length follows pitch, the pattern does not fall into a short loop, removed voices fade in about a second.
- Level: the Wave knob stays within about 1 dB across its range, and the worst case over every switch, rate, spread and root combination stays below the limiter ceiling, with the output trim at 0.5.
- The cloud build compiles against the real libDaisy.

Not verified, because nobody has flashed it yet:
- How it sounds and feels on a real Versio.
- Whether the audio callback fits in the CPU budget. Set `SERAPH_CPU_METER` to 1 in `main.cpp` to show CPU load on LED 4 (green is low, red is high).
- The three-way switch directions, which knob is "knob 1", and whether the Tap jack reads as a gate.
- Output level into your system. `kOutTrim` in `main.cpp` adjusts it.

## Safety measures built into the firmware

- **Output level:** final trim of 0.5, and the output is limited by a soft clipper, so it will not slam a mixer or a hot module input.
- **Start-up fade-in:** about 2 s, so there is no pop or thump when the case powers on.
- **DC blocker:** 15 Hz high-pass before the output stage, so no DC offset reaches DC-coupled modules.
- **No clicks:** note swells never start faster than 12 ms, the reverb level changes over about 10 ms, and voice changes fade over a fraction of a second.
- **Parameter sanitising:** every parameter is clamped, and NaN-safe, every audio block.
- **No allocation at run time:** all buffers are static (large ones in SDRAM) and cleared at start-up.
- **Controls:** the knobs are read once on boot, so the root, scale and voices start where the knobs are.

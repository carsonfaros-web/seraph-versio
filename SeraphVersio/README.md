# Seraph for Noise Engineering Versio

A soft, round bell and pad engine, with a panel and note logic modelled on the Sdkc Instruments Helical. The whole Wave range stays gentle: the sine end comes from measurements of a recording of the Helical, and the other end is a soft triangle with a little shimmer. There is no saw, no heavy detune and no drone bed.
Twelve voices, each a pair of slightly detuned copies. A 24 dB low-pass, a quiet sub sine, a faint band-limited noise bed, and an eight-line reverb that can ring for about 30 seconds.

## How the notes work (the Helical idea)

Every voice plays its own note with its own swell. When a note ends, the voice works out the next one:

- the next **pitch** comes from the length of the last note,
- the next **length** comes from the new pitch: higher notes last longer (the length grows with the square root of the pitch ratio, so an octave up lasts about 1.4 times longer, three octaves up about 2.8 times).

Because every pitch has its own length, the voices drift in and out of step and make slow rhythms that are not tied to any tempo. The same seed always gives the same pattern. A short tap on the button makes a new seed.

## The sine end sounds like the Helical

I measured a five-minute recording of the Helical ("First play 1"). It is almost pure sine tones, with every harmonic at least 35 dB below the note and nothing above about 700 Hz. Each note starts instantly and fades smoothly to silence over 1.3 to 4 seconds. About six notes sound at once, 2.8 new notes start every second, each pitch keeps its own length, and every note sits hard left or hard right with no chorus or detune. The notes are E major, from E2 to E5.

With Wave fully left, Seraph does the same: pure sines, each voice hard left or hard right, small detune, almost no sub or noise, and notes that start fast and fade out. Turning Wave right rounds the tone toward a soft triangle (the third harmonic is only 19 dB below the note, and fades as the note rings down), with a little more detune (up to 6 cents), a touch of sub and a touch of air. It never becomes a saw or a dense drone. In between, each note starts at the Wave sound and rings down to a pure sine as it fades, which is the same trick a struck bar or string does (the bright overtones die first).

To get close to the recording: Root at the middle (E2), Scale on the sixth step (major), Voices full right, Rate about 46%, Glide full left, Spread full right, Wave full left, switch 1 right (Pluck), switch 2 left (Room).

## One flat panel

There are no Setup pages, no modes and nothing to pick up. Every knob and switch does what it says at once, and every knob takes CV.

**Knobs**

| Knob | Does |
|---|---|
| 1 Root | Root note, 37 steps from A#0 (29 Hz) to A#3. Glides to a new note (glide is capped at 2 s here). |
| 2 Scale | Eight scales, dark on the left to bright on the right: open fifths, minor pentatonic, hirajoshi, phrygian, dorian, major, lydian, whole tone. LED 3 shows the scale colour. Changing it moves the notes at once. |
| 3 Voices | One to twelve voices, in steps. One voice is a single melodic line. Voices fade in and out in about a second. |
| 4 Rate | How long notes last at the root: 0.12 s (fast, plucked runs) to 48 s (slow drone). Takes effect at once, even in the middle of a note. This is Helical's Helicity. With a clock patched into the Tap jack it becomes 13 steps from a quarter to 32 times the clock period (1/4, 1/2, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32). |
| 5 Glide | How long notes slide to the next pitch: 0.01 s (stepped) to 12 s (long slides). |
| 6 Spread | Pitch range above the root. Fully left, every voice plays the root only (a wide chorus). Fully right, notes reach three octaves up, with the higher voices staying higher. Takes effect at once. |
| 7 Wave | Left: clean sine, tight detune, almost no sub or noise (the Helical sound). Right: soft round triangle, a little more shimmer (up to 6 cents), a touch of sub and air. The filter, detune, sub and noise all move with it, and the loudness is levelled across the knob. |

**Switches** (they slide left, centre and right)

| Switch | Left | Centre | Right |
|---|---|---|---|
| 1 Env | Soft: slow swell in, long fade, never fully silent | Even: symmetric swell | Pluck: fast attack, long decay |
| 2 Space | Room: almost dry, about 2 s | Hall: about 10 s | Vast: about 30 s |

I assumed left is the first label printed beside each switch. That is not confirmed.

**Tap button** (same everywhere)

| Press | Does |
|---|---|
| Short | New seed: every voice starts a new note now, in a new pattern. (Helical's reload.) |
| Hold 0.8 s or more | Lock on or off. Locked, every voice repeats its current pitch and length, so the pattern loops. Scale and Spread still work while locked, and apply at once. |

**Clock input (Tap jack).** Patch a clock or gate into the Tap jack and its pulses set the tempo. The note length becomes the clock period times the step the Rate knob is on, so the whole pattern speeds up and slows down with your clock. It does not line notes up with the beats: each voice still picks its own pitch and length (higher notes are longer), and a pulse never starts a note, so this sets the timescale, not a rhythm. Use a steady clock; a tempo change is followed within a few pulses. With nothing patched in, or if the clock stops for about two seconds (2.5 periods on a slow clock), Rate goes back to free seconds, just as before. The Tap button works as before.

**Audio inputs (In L and In R).** Patch any sound in and it steers the drones. There is no knob and nothing to set.

- Its **loudness** swells the voices: the quietest part of the input brings the drones down to about 30% of their level (about -10 dB), the loudest part leaves them at full level. Loudness is measured against the input's own recent peak, so a quiet source and a hot one steer the same.
- Its **brightness** makes the tone rounder (sine toward the soft triangle) and opens the filter a little: a dark source (kick, bass) keeps the voices pure, a bright one (hi-hats, noise, cymbals) rounds them.
- The notes still come from the scale. The input never changes the pitches.
- It switches itself on when the input is louder than about -48 dBFS, fades in over about 0.3 s, and fades out about 2.5 s after the signal has gone for 1.5 s. With nothing patched in, or only a noise floor, the sound is identical to before (checked bit for bit).
- LED 4 glows violet while the input is steering, brighter with the input level.

To turn it off, set `kInAmount` to 0 in `main.cpp`. With Wave fully left (pure sines) the main effect is the swell; with Wave to the right the rounding of bright input is audible too.

The audio engine's old input mix is still in the code (`P.inLevel`) but the panel keeps it at 0, so the input is never mixed into the sound itself.

Fixed in the firmware: drift 0.5, wow 0.05, damping 0.4, full stereo width, every voice hard left or right, and the strike effect on. The Space switch sets reverb amount, size and decay together. The Room position is nearly dry on purpose: the reverb spreads each note to both sides, which blurs the hard panning.

## LEDs

| LED | Shows |
|---|---|
| 1 | Output level (blue-white) |
| 2 | Note activity: brightens as voices swell (teal) |
| 3 | Scale colour: crimson (open fifths), magenta, violet, blue, teal, green, yellow, warm white (whole tone) |
| 4 | Events: white flash on a new seed, green flash on each clock pulse (and a faint green glow while a clock is followed), slow amber pulse while Locked |

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
- A scripted run of the control logic in `main.cpp` (41 checks): every knob's range, all eight scales and six voice counts selectable, no flutter on step edges, switch mapping, tap short and hold, the clock pulse LED, and 3 minutes of random knob, switch, tap and gate input.
- Clock input (29 checks, also under address and undefined-behaviour sanitizers): 120 bpm reads as 0.5 s, jitter of 3 ms is averaged out, tempo changes are followed, a glitch under 10 ms is ignored, a gap over 12 s resets, the clock times out back to free Rate after about 2 s, all 13 steps give period times ratio, Rate is clamped to 0.05 to 120 s, a stuck-high gate is not a clock, the millisecond counter wrapping is handled, and 3 minutes of random gate noise stays in range with no NaN.
- Note behaviour (16 checks): Rate speeds up notes within a second, Spread 0 is root only and Spread 1 reaches three octaves, Lock repeats pitch and length, scale changes apply at once while locked, the same seed gives the same pattern, length follows pitch, the pattern does not fall into a short loop, removed voices fade in about a second.
- Audio input (host tests with a kick, hi-hats, bass, noise swell, sines, a noise floor, NaN, infinity, 1e30 values, DC and full-scale noise): with a silent input the output is bit-identical to the version without input steering, a noise floor never switches it on, a hot and a quiet input steer the same, kick and bass read dark and hats and noise read bright, it releases about 10 s after the input stops, and nothing produced NaN or tripped the sanitizers. The browser simulator matches the C++ within 0.3 dB.
- Level: the Wave knob stays within about 0.5 dB across its range, and the worst case over every switch, rate, spread and root combination stays below the limiter ceiling, with the output trim at 0.5.
- Sound: with Wave fully left a single voice has harmonics at least 69 dB below the note, each voice can sit entirely on one side, notes start bright and ring down to a sine, and with 12 voices at Rate about 46% there are about 6 notes sounding at once and 3 new notes a second, like the recording. I measured the recording; I cannot hear it, so whether it feels the same is for your ears.
- The cloud build compiles against the real libDaisy.

Not verified, because nobody has flashed it yet:
- Audio input steering on the real module: the input level scaling, how quiet a signal still switches it on (`kInGate`), and whether the inputs are quiet enough with nothing patched in.
- How it sounds and feels on a real Versio.
- Whether the audio callback fits in the CPU budget. Set `SERAPH_CPU_METER` to 1 in `main.cpp` to show CPU load on LED 4 (green is low, red is high).
- The three-way switch directions, which knob is "knob 1", and whether the Tap jack reads as a gate (the clock input relies on it).
- Output level into your system. `kOutTrim` in `main.cpp` adjusts it.

## Safety measures built into the firmware

- **Output level:** final trim of 0.5, and the output is limited by a soft clipper, so it will not slam a mixer or a hot module input.
- **Start-up fade-in:** about 2 s, so there is no pop or thump when the case powers on.
- **DC blocker:** 15 Hz high-pass before the output stage, so no DC offset reaches DC-coupled modules.
- **No clicks:** note swells never start faster than 12 ms, the reverb level changes over about 10 ms, and voice changes fade over a fraction of a second.
- **Parameter sanitising:** every parameter is clamped, and NaN-safe, every audio block.
- **No allocation at run time:** all buffers are static (large ones in SDRAM) and cleared at start-up.
- **Controls:** the knobs are read once on boot, so the root, scale and voices start where the knobs are.

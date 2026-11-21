# Seraph for Noise Engineering Versio

A slow, dark, wide drone and pad engine, built from measurements of *Seraphim* by How To Disappear Completely.
Six voices, each a cluster of four slightly detuned copies. A dark 24 dB low-pass, a sub sine, an eight-line
reverb that can ring for up to about 40 seconds, and slow random swells. The sound was measured, not listened to.

## Files

| File | What it is |
|---|---|
| `seraph.h` | The engine. No libDaisy dependency, so it also builds on a computer. |
| `main.cpp` | Versio controls, LEDs, audio callback. |
| `Makefile` | Build file. Set `LIBDAISY_DIR` to your libDaisy folder. |

## Controls

**Knobs (all seven take CV, so every one of them can be modulated)**

| Knob | Does |
|---|---|
| 1 | Root note, 37 semitones from A#0 (29 Hz) to A#3. Steps are quantised. The root glides to a new note. |
| 2 | Change: how often voices move to a new note. Fully left is about every 2 minutes, fully right about every 15 seconds. |
| 3 | Cluster: spread of the detuned copies, 0 to 40 cents. |
| 4 | Darkness: low-pass cutoff, 120 Hz to about 5.5 kHz. |
| 5 | Breath: how deep and how fast the swells are. |
| 6 | Space: reverb amount. |
| 7 | Decay: reverb time, about 2 to 40 seconds. |

**Switches**

| Switch | Left | Centre | Right |
|---|---|---|---|
| 1 Voicing | Choir: 4 voices, mostly sine | Drone: 5 voices, mixed | Mass: 6 voices, mostly saw |
| 2 Mode | Setup page A | Play | Setup page B |

**Tap button**

| Press | Does |
|---|---|
| Short | New seed. The voices glide to a new chord. |
| Hold 0.8 s or more | Freeze the harmony, or release it. Swells, drift and reverb keep moving. |

The tap button does the same in every mode. Setup is now chosen with switch 2, not the button.

**Gate input.** Each rising edge moves one voice to a new note, so a clock or sequencer can drive the harmony.

**Audio inputs.** Mixed into the filter and reverb. The level is set in Setup page B and starts at zero.

## Setup

Switch 2 picks the mode: centre is Play, left is Setup page A, right is Setup page B. The switches slide left, centre and right. I assumed left is the first label printed beside each switch, which is not confirmed. LED 1 pulses orange in Setup, and LED 2 shows the page: orange for A, purple for B.
A knob does nothing until it passes the stored value, then it takes over (pick-up), so entering Setup never makes a jump.
Flip switch 2 back to the centre to return to Play. Values stay until power-off; nothing is saved to flash yet.

**Play settings are held while you are in Setup.** When you return to Play, the knobs are in different places, so the sound does not jump to them. Each Play setting stays at its held value until you move that knob past it (pick-up), the same as in Setup. LED 4 glows dimly white while any Play knob is still waiting to pick up. Sliding switch 2 across the centre from one Setup page to the other does not change the sound either.
If the module powers up with switch 2 away from the centre, it starts in that Setup page.

| Knob | Page A (switch left) | Page B (switch right) |
|---|---|---|
| 1 | Drift | Air (low noise bed) |
| 2 | Wow (tape-style tuning drift) | Audio input level |
| 3 | Tune, ±50 cents | Glide time, 1 to 20 s |
| 4 | Reverb size | Sub body |
| 5 | Damping | Scale: left third minor pentatonic, middle Dorian, right third Lydian |
| 6 | Width | not used |
| 7 | Output level | not used |

The scale used to be on switch 2. It moved to Setup B knob 5 so the switch could become the mode switch. It keeps its value in Play.

## LEDs

Normal mode: LED 1 output level, LED 2 swell depth, LED 3 voicing colour (teal choir, violet drone, magenta mass),
LED 4 events (white flash on a new seed, green flash on a gate, slow amber pulse while frozen).

## Building

1. Install the Arm GNU toolchain, then build libDaisy once: run `make` inside the libDaisy folder.
2. Put this folder next to libDaisy, or edit `LIBDAISY_DIR` in the Makefile.
3. Run `make` in this folder. The result is `build/Seraph.bin`.
4. Put the Versio in DFU mode and flash with the Electro-Smith web programmer. If something goes wrong, you can reflash a Noise Engineering firmware with their flash app.

## What is verified and what is not

Verified here:
- `seraph.h` compiles with no warnings under `g++ -Wall -Wextra`, renders without NaNs, and matches the web prototype's measurements (level, spectrum, stereo width and harmony movement).
- `main.cpp` compiles with no warnings against a stand-in written from the documented libDaisy `DaisyVersio` listing.

Not verified, because there is no Versio or libDaisy toolchain in this session:
- A real build with the real libDaisy headers.
- Whether the audio callback fits in the CPU budget. Set `SERAPH_CPU_METER` to 1 in `main.cpp` to show CPU load on LED 4 (green is low, red is high). This uses libDaisy's `CpuLoadMeter`, which I have not checked against your version.
- The three-way switch constants. `SwPos()` in `main.cpp` is the only place they are used.
- Whether the knob values include the CV exactly as I assumed, and the range of the Versio's CV inputs.
- Output level into your system. `kOutTrim` in `main.cpp` adjusts it.
- How it sounds.

If `make` stops with an error, send me the first error line and I will fix it.

## Safety measures built into the firmware

- **Output level:** final trim of 0.5 (peak about 0.4 at defaults, never above about 0.47 in worst-case tests), so it will not slam a mixer or a hot module input.
- **Start-up fade-in:** about 2 s, so there is no pop or thump when the case powers on.
- **DC blocker:** 15 Hz high-pass before the output stage, so no DC offset reaches DC-coupled modules (VCAs, filters, scopes).
- **Band-limited noise bed:** the "Air" layer has no sub-20 Hz content.
- **Parameter sanitising:** every parameter is clamped, and NaN-safe, every audio block.
- **No allocation at run time:** all buffers are static (large ones in SDRAM) and cleared at start-up.
- **Controls:** the knobs are read once on boot, so the root note does not jump. Setup pages use pick-up, so entering Setup never makes a value jump.

Tested on a PC build with AddressSanitizer and UBSan: random and extreme knob settings, out-of-range values, 30-minute runs, sample rates from 32 to 192 kHz, block sizes from 1 to 1024, loud external input, and 3 minutes of random knob, switch, tap and gate input. Not tested without the hardware: the real Versio output voltage scaling, CPU load on the Cortex-M7, and how it sounds in your rack. Set `SERAPH_CPU_METER` to 1 to show CPU load on LED 4.

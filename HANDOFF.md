# Seraph for Noise Engineering Versio: handoff

State as of 2026-11-21. Use this to continue in a new chat.

## What it is
Drone and ambient firmware for a Noise Engineering Versio (Daisy Seed inside), built from measurements of *Seraphim* by How To Disappear Completely. Six voices of four detuned copies, a dark low-pass, a sub sine, and an eight-line reverb with slow random swells.

## Where things are
- Repo: https://github.com/carsonfaros-web/seraph-versio (public). `main` holds the firmware in `SeraphVersio/` (`main.cpp`, `seraph.h`, `Makefile`, `README.md`).
- Every push to `main` builds in GitHub Actions and replaces the release "latest" with `Seraph.bin`: https://github.com/carsonfaros-web/seraph-versio/releases/tag/latest
- Branch `timbre-experiment` holds parked work: Warp (cross-mod and ring mod), Shift (frequency shifter in the reverb loop) and Fold (wave folder), replacing Tune, Size and Width on Setup A. Tested (bit-exact at zero, ±30 Hz shifter with image 45 dB down, full safety suite passed) but not shipped, because the user preferred the original setup.
- Faceplate guide: https://claude.ai/artifact/9jmgdG25f1zfGQgWac8J8i

## Controls (current main)
- Knobs in Play: root note, change, cluster, darkness, breath, space, decay. All take CV.
- Switch 1: voicing (up choir, centre drone, down mass).
- Switch 2: mode. Centre Play, up Setup A, down Setup B. Setup knobs use pick-up.
- Tap: short = new seed, hold 0.8 s or more = freeze. Same in every mode. Gate in moves one voice to a new note.
- Setup A: drift, wow, tune, size, damp, width, level. Setup B: air, input level, glide, body, scale (left third minor pentatonic, middle Dorian, right third Lydian).
- LEDs in Setup: LED 1 pulses orange, LED 2 orange for page A or purple for page B.

## Tested
PC build with ASan and UBSan: random and extreme settings, out-of-range values, 30-minute runs, sample rates 32 to 192 kHz, block sizes 1 to 1024, loud input, a scripted simulation of the Versio control logic (49 checks) and 3 minutes of random knob, switch, tap and gate input. The cloud build compiles against the real libDaisy.

## Not verified
- How the firmware sounds and behaves on a real Versio. Nobody has flashed the new build yet.
- Versio DFU entry method (check Noise Engineering's manual), real output voltage scaling, CPU load (`SERAPH_CPU_METER` 1 shows it on LED 4), and which physical knob is "knob 1".
- Flash with https://flash.daisy.audio/ (Chrome, File Upload tab). Power the case off and unplug the ribbon before connecting USB.

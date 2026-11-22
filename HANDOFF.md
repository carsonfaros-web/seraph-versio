# Seraph for Noise Engineering Versio: handoff

State as of 2026-11-21. Use this to continue in a new chat.

## What it is
Drone and ambient firmware for a Noise Engineering Yester Versio (Daisy Seed inside), built from measurements of *Seraphim* by How To Disappear Completely. Six voices of four detuned copies, a dark low-pass, a sub sine, and an eight-line reverb. Since 2026-11-21 the panel and note logic copy the Sdkc Instruments Helical, because the earlier scheme "did not feel intuitive" and "took too long to react".

## Where things are
- Repo: https://github.com/carsonfaros-web/seraph-versio (public). `main` holds the firmware in `SeraphVersio/` (`main.cpp`, `seraph.h`, `Makefile`, `README.md`).
- Every push to `main` builds in GitHub Actions and replaces the release "latest" with `Seraph.bin`: https://github.com/carsonfaros-web/seraph-versio/releases/tag/latest
- Branch `timbre-experiment` holds parked work (Warp, Shift, Fold). It predates the Helical redesign and would need rework to merge.
- Faceplate guide: https://claude.ai/artifact/9jmgdG25f1zfGQgWac8J8i

## Controls (current main, Helical-style, one flat panel, no Setup pages)
- Knobs, all live and all take CV: 1 Root, 2 Scale (8 scales, dark to bright), 3 Voices (1 to 6), 4 Rate (note length 0.12 to 48 s), 5 Glide (0.01 to 12 s), 6 Spread (root only to three octaves), 7 Wave (sine to saw, filter opens, loudness levelled).
- Switch 1 = Env (left soft, centre even, right pluck). Switch 2 = Space (left room ~4 s, centre hall ~10 s, right vast ~30 s).
- Tap: short = new seed (every voice starts a new note), hold 0.8 s or more = Lock (voices repeat pitch and length; Scale and Spread still work). Gate in (Tap jack) starts the next voice's note, round robin.
- Note logic: when a note ends, next pitch comes from the last note's length, next length from the new pitch (sqrt of pitch ratio). Same seed, same pattern. Rate is applied as a phase speed, so it works mid-note.
- LEDs: 1 level, 2 note activity, 3 scale colour, 4 events (white new seed, green gate, amber pulse while Locked).
- Removed on purpose: Setup pages, pick-up, audio input mix, cluster/drift/wow/tune/width/level knobs (fixed at album values).

## Tested
PC build with ASan and UBSan: a scripted simulation of the Versio control logic (41 checks), note behaviour (16 checks: rate reacts at once, spread range, lock, scale change while locked, determinism, length follows pitch, no short loops, voices fade), loudness across the Wave knob (within ~1 dB), worst-case peak below the limiter, random and extreme settings, out-of-range values, 30-minute runs, sample rates 32 to 192 kHz, block sizes 1 to 1024, and 3 minutes of random knob, switch, tap and gate input. The cloud build compiles against the real libDaisy.

## Not verified
- How the firmware sounds and behaves on a real Versio. Nobody has flashed the Helical-style build yet.
- Versio DFU entry method (check Noise Engineering's manual), real output voltage scaling, CPU load (`SERAPH_CPU_METER` 1 shows it on LED 4), which physical knob is "knob 1", switch directions, and whether the Tap jack reads as a gate.
- Flash with https://flash.daisy.audio/ (Chrome, File Upload tab). Power the case off and unplug the ribbon before connecting USB.

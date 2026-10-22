# Switch Blade

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

Free firmware for the **M-VAVE FM-1** that packs four synth machines into the box: one per track, eight voices shared.
No drums, on purpose: it is built to be a synth expansion, for example for an Elektron Syntakt with MIDI channels 1-4
playing the four machines.

- **OB-Z**: a big, brassy poly
- **WAVE**: the voice of [Void Wave](https://ollienicklin.com/projects/void-synth.html), a wavetable synth
- **NANO**: a 303 (presets 1-2) plus a polyphonic subtractive synth (presets 3-32)
- **JUNO**: a warm poly with a Juno-60-style chorus

From the HOME screen, knobs 1-4 play cutoff, resonance, envelope and one parameter per machine; SELECT is the track's
volume. Every track also has a drive, insert effects and the shared chorus, delay and reverb.

**Install, try it in your browser, read more:** https://ollienicklin.com/switchblade/
(install from desktop Chrome or Edge over USB in about two minutes; the flasher also returns the FM-1 to the official
firmware).

## Building

The firmware builds with the JieLi toolchain in Docker: see [BUILDING.md](BUILDING.md). Host tests: `sh tests/run_tests.sh`.
The browser version is `web/emu/` (`sh web/emu/build.sh`, needs emsdk).

## Built on Felucca

Switch Blade is a fork of [Felucca](https://hugelton.github.io/Felucca/) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic), [Hügelton Instruments](https://hugelton.com)), the open FM-1 firmware.
Felucca's own README is kept in [docs/FELUCCA_README.md](docs/FELUCCA_README.md). Every other credit and licence:
[CREDITS.md](CREDITS.md), [LICENSING.md](LICENSING.md).

Switch Blade is an independent project, not affiliated with or endorsed by M-VAVE. Product names belong to their owners.
Install at your own risk.

## Licence

GPL-3.0-only, as Felucca: [LICENSE](LICENSE).

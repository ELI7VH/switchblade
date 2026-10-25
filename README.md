# Switch Blade

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## WaveLoop build

This fork (branch `waveloop/main` of [ELI7VH/switchblade](https://github.com/ELI7VH/switchblade)) plays Switch Blade as one
synth from an **M-Audio Oxygen Pro 61** set to the preset of the WaveLoop Melodee fork
(`controllers/Melodee.OxygenPro61UserPreset` there; programmed in its slot 16). Set the keyboard's channel to 16:

- **Channel 16 follows the FM-1.** With MIDI IN on CH1-4 (also CH5-8, CH9-12), channel 16 plays and edits the selected
  track. Notes held when you change track end where they started. Keep MENU > MIDI > AUTO SELECT off (the default), or a
  pad (channel 4) selects track 4 and the keyboard follows it there.
- **The knob card.** Turn any MIDI knob, fader or the mod wheel, or an FM-1 knob on HOME, and a card names the track and
  machine, what the knob does and its value, with a bar. It goes 0.8 s after the last move.
- **The mod wheel is a vibrato** on every machine: about 5.4 Hz, up to half a semitone at the top, the wheel squared so
  the bottom is gentle. It adds to any MOD slot that uses MODW. Pitch bend is unchanged.

| Oxygen control | Sends | Does (every machine) |
| --- | --- | --- |
| Knob 1 | CC74 | CUTOFF of the machine (OB-Z, WAVE, NANO, JUNO) |
| Knob 2 | CC71 | RESONANCE |
| Knob 3 | CC5 | GLIDE |
| Knob 4 | CC10 | PAN |
| Knob 5 | CC76 | LFO RATE |
| Knob 6 | CC77 | LFO > FILTER (middle = none) |
| Knob 7 | CC78 | LFO > AMP (tremolo) |
| Knob 8 | CC70 | LFO WAVE (SIN TRI SAW SQR S&H) |
| Faders 1-4 | CC7, channels 1-4 | VOLUME of tracks 1-4 |
| Faders 5-8 | CC73 75 79 72 | ATTACK, DECAY, SUSTAIN, RELEASE of the amp envelope. NANO's 303 (sounds 1-2): fader 6 is its DECAY, the others do nothing |
| Fader 9 | CC8 | nothing (Switch Blade does not play the computer's audio) |
| Encoder turn | CC114 (relative) | next / previous sound on the selected track, on into the next machine's sounds after the last (PRESETS' list) |
| Fader buttons 1 / 2 | CC112 (63 / 65) | previous / next machine on the selected track (its first sound) |
| Encoder press | CC115 | next track (after T4, T1) |
| Pads | notes, channel 4 | track 4 |
| Mod wheel | CC1 | vibrato |

EDIT's knob 3 (ENV) and knob 4 (the machine's own control) moved from CC79 / CC70 to CC16 / CC17, in and out
([docs/MIDI.md](docs/MIDI.md)). Nothing new is stored: the card, the wheel and the encoder are live controls.

---

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

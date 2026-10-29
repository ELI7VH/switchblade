# TEST audio diagnostics — 2026-10-29

Measured on user-confirmed FM1-TEST, installed glo10/source 47ba86f. No firmware
was installed. SHOW and GHOUL were not accessed. USB MIDI INFO and transport/FX
commands worked through the Mac's `Felucca` MIDI port.

The original stopped current song was backed up before playback. Track 2 was
WAVE in UNISON mode with level 0 and release 120; it was not muted. The other
tracks were OB-Z, NANO and DRUM. Each run below lasted 30 seconds. CPU ranges
are two-second samples of the smoothed meter, not instantaneous peak loads.

| Playback condition | CPU samples | New late halves | Overload voices shed | Total voices given up |
| --- | --- | --- | --- | --- |
| Original four tracks, neutral live FX | 38–69% | 2 | 3 | 124 |
| Mute only silent WAVE track 2 | 34–52% | 0 | 0 | 0 |
| DRUM only | 23–28% | 0 | 0 | 0 |
| OB-Z and NANO, silent WAVE and DRUM muted | 22–40% | 0 | 0 | 0 |
| Silent WAVE muted; repeat alternating 1/32 and 1/64, dirt 127, delay 100, filter 95 | 44–71% | 0 | 1 | 1 |

`voices_given_up` includes ordinary shared-budget stealing and overload shedding;
it is not a count of audible glitches. There are eight voices shared across all
tracks. `voice.c` prevents muted tracks from starting notes but does not apply
that rule at level 0. The inaudible WAVE unison can therefore consume rendering
time and voice slots and cause other parts to lose voices. The mute comparison
strongly identifies this as a contributor in the current song. Extreme combined
FX still triggered one preventive voice shed, so this is not proof of unlimited
headroom or a complete cure.

The I2S half deadline is about 2.9 ms (128 frames); `audio_late` reports DMA
advancing during a render. The initial 512 late halves were historical retained
counters; the table uses deltas. USB audio streaming was inactive (`uac_alt 0`,
no packets), so USB audio underruns/overruns did not explain these runs.

Immediate workaround: explicitly mute a track that is faded fully out when its
notes do not need to remain live. Do not silently change stored songs to apply
this workaround. Firmware follow-up should prioritize audible tracks over silent
ones in voice allocation, define fader-up/held-note behavior, and reduce costly
unison/FX peaks. Do not simply increase the voice budget: this can worsen CPU
load. Verify the revised allocator and rendering on TEST with the same song and
full-strip stress conditions before shipping to SHOW.

After the tests, the original current song, settings and preset banks were
restored and all eight logical objects compared byte for byte. Live FX returned
to their saved neutral state, and transport was stopped. Private backups and
raw console logs remain outside Git under the dated TEST diagnostic folder.
Physical listening was not performed by the agent. Screenshots/website work is
still pending; this investigation took priority after TEST was reconnected.

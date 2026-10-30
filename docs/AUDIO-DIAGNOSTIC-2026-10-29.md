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


## card12 repeat and mono-audio proposal

Fresh measurements on TEST / v0.1-wl-card12, source 2bd136d, on October 29.
The user explicitly clarified that "mono" means one audio channel throughout
synthesis, mixing and effects, **not** one note per synth track. Polyphony and
overlapping drum hits must remain available. This is an architectural proposal;
no mono firmware has been implemented or installed.

Before testing, CRC-validated backups captured all 20 exposed objects. The
current music differs from the earlier glo10 session: T2 FROZEN TIDE is still
level 0 / UNISON, but release is now 0, and the live master high-pass control
is +4 (not neutral). Other live controls are off. All recording arms were off.
Only temporary mix/mute, T2 voice-mode and master-filter values were changed.
Each playing condition has 2 seconds settling followed by 35 one-second samples.
The measured CPU is the smoothed render-only meter, not total processor use.

| Condition | CPU min / median / max | New late halves | Shed | Given up |
| --- | --- | --- | --- | --- |
| Original | 39 / 60 / 75% | 31 | 117 | 193 |
| Only center master filter | 33 / 52 / 71% | 3 | 2 | 125 |
| Only mute silent WAVE | 39 / 58 / 64% | 0 | 0 | 0 |
| Mute silent WAVE and center filter | 37 / 48 / 54% | 0 | 0 | 0 |
| WAVE one voice, filter centered (diagnostic only) | 35 / 52 / 64% | 0 | 0 | 28 |
| Restore original and repeat | 35 / 60 / 74% | 81 | 182 | 290 |

These are separate real-time windows, not deterministic same-phase renders.
External DIN input arrived during filter-neutral (+201 bytes), WAVE-one-voice
(+459), original-repeat (+543), and idle (+285) windows. No DIN input arrived
during the original or either muted-WAVE window. The second original is
therefore not a controlled reproduction. These counter totals must not be
treated as a precise fixed CPU price per feature. In particular, do not attribute
the entire original/filter-neutral difference to the filter alone.
Every window had zero new USB packets/underruns/overruns and zero UART drops;
USB audio was inactive throughout. Historical USB error counters do not explain
these newly measured deadline misses. "Given up" still includes ordinary voice
stealing, whereas "shed" is the overload response.

### What mono audio would actually change

Most source voices are already mono: track_render fills one part buffer.
WAVE unison still renders up to six voices regardless of output channel count.
A final L/R sum therefore cannot halve synthesis cost.

A real single-channel pipeline could remove duplicated work in mix_part /
mix_stereo, master_out's DC/lowcut/softclip, and perf_block's left/right master
filters, flanger, phaser, repeat-buffer processing and live dirt. The chorus,
delay and room/spring reverb buses are already substantially mono; only stereo
extensions such as HALL's difference and JUNO's chorus would be folded or
reworked. Preserve their sound intentionally rather than dropping one side.
At 48 kHz USB recording, uac_tap48 currently computes two FIR convolutions;
mono could compute one and duplicate the result. That cost was inactive in
these tests and must be measured separately with an actual USB stream.

Keep the DAC/USB stereo transport formats initially: compute mono once, then
copy the same sample into both hardware output slots. This preserves headphones,
codec and host compatibility while avoiding duplicate DSP. Preserve song and
preset data; pan/spread can retain stored values while being inactive in mono.
Keep safe mix gain, effect tails and musical polyphony. No measured percentage
saving is available until a genuine mono variant runs on TEST.

The CPU meter excludes nested TIMER5 input/USB service time and averages over
about 93 ms. The deadline/shed detector includes nested work; two consecutive
halves above 85% request a voice fade. Consequently a displayed 60-75% does not
prove headroom against short peaks. Screen drawing runs in the main loop,
below the audio ISR; the present comparison does not independently isolate
LCD memory/bus effects.


### Restoration and evidence limits

Private evidence: `~/Documents/FM1 Backups/TEST-cpu-audit-20261029-212119/`.
All-muted, stopped, filter-neutral baseline (15 seconds): CPU 16 / 16 / 17%,
zero new late/shed/given-up. This still runs the mixer, effect buses, master
processing and control work; it is not a measurement of stereo overhead alone.

Original per-track levels, mutes, T2 voice mode and live filter were restored;
playback resumed because it was running initially. All 19 persistent/settings/
preset objects match the pre-test backup exactly. Current runtime obj0 differs:
WAVE sequence note/tie metadata changed to rests during the session, while the
track parameter bytes remain equal. This was not an intended diagnostic edit.
External DIN input and possible panel interaction mean its cause is unproven.
Both current versions are backed up. Asked Elijah whether to retain live edits
or restore the original sequence; no full runtime restore while awaiting that
answer. Consequently do not claim the current song is byte-identically restored
or that all measurement windows played the identical musical sequence.

Mono follow-up: prototype one internal audio channel while retaining polyphony,
then compare against stereo on the same backed-up song with no live controller
input, both USB audio off and 48 kHz streaming. Measure render peaks/deadlines,
not just the average meter; audition filter, stutter, dirt, delay, chorus and
reverb for gain and tail changes. No firmware flash occurred in this audit.

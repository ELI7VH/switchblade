# Switch Blade firmware (working name VOID FOUR): handover (2026-10-16)

This file hands the work over to the next agent. Read it top to bottom before you touch anything.

## 1. What this is

Switch Blade (working name VOID FOUR; the repo folder FM1-Four, the FOR4 format and the identity FM-1_940 keep their names) is a fork of the Felucca 1.6 firmware for the M-VAVE FM-1 (a JieLi AC79 chip with a 240×240 ST7789V screen). It is plain C, built as one unity build from `firmware/src/felucca.c`, with fixed-point DSP. The fork keeps exactly four synth machines, each with 32 factory presets:

| id | Machine (display) | C ids | Source file | State |
|---|---|---|---|---|
| 0 | OB-Z | `ENG_OBX`, `ENGI_OBX`, `OBX_PRESETS` | `firmware/src/eng_obx.c` (the old ANALOG engine) | works; Phase 4 adds the SEM filter morph and slop |
| 1 | WAVE | `ENG_WAVE`, `ENGI_WAVE` | `firmware/src/eng_wave.c` (the VOID WAVE plugin port) | works; DC blocker added in Task 4.2. **One wavetable oscillator since Task 4.4** (2026-10-18, the user's decision): OSC2, MIX, the OSC2 pitch and MORPH are gone (the plugin has two oscillators: a departure); the keyframe crossfade stays. `P_E3` (MIX) and `P_E6` (OSC2) are hidden (label `-`, range 0..0), ids unchanged; **`P_E2` (was TBL2) is WARP since Task 4.5** (9..127, 64 = off; a stored 0..8 is the old TBL2 and loads as 64 through `param_fit`; a WARP is read by the slow loop, so it costs ~+45 host instr a voice and nothing when off). **Task 4.5: TYPE 6 = VOWEL** (two band-passes, CUT sweeps U O A E I, RES = Q 2..12; the LP24 ladder is unchanged, the user kept it; a mid-note switch into it clamps the ladder's state so `void_tanh1` stays in range). Other machines: OB-Z plays TYPE 6 as LP (`obx_mode`'s `f < 6` guard), ACID ignores `P_FTYPE`, DRIFT maps any value above 3 to FLAT (its HPF). The render loops lost `void_close` and the fade-in (one out-of-line slow loop), the DC clamp and the level-search divides: bit for bit the same samples |
| 2 | ACID | `ENG_ACID`, `ENGI_ACID` | `firmware/src/eng_acid.c` | **real 303 voice** (Task 5.2): `bass303.c` after Open303, float, own envelope (`ownenv`); `docs/plans/2026-10-17-acid-303.md`. Its 32 seeds are still the placeholder-era ones (Task 7.2 re-voices 12) |
| 3 | DRIFT | `ENG_DRIFT`, `ENGI_DRIFT` | `firmware/src/eng_drift.c`, `dsp_va.c` | **real Juno voice** (Task 6.1): a float port of the Ultramaster KR-106 (polyBLEP DCO, OTA ladder, 4-position HPF), `ownenv = 0`; `docs/plans/2026-10-17-drift-juno.md`. **`DRIFT_POLY 1`, `DRIFT_OS 1` by the CPU measurement (mono!), raise them if Task 8.1's PERF shows room.** Its 32 seeds are the placeholder-era ones with only `e[1]` (DTN -> PW) remapped (Task 7.2 converts the KR-106's 43 presets) |

- **Repo:** `/Users/ollie_nicklin/Documents/Projects/VOID SYNTH/FM1-Four`. It is a git repo on branch `main` (there is no `master`; the handover said `master` in error, corrected 2026-10-16), and the user has authorised committing directly.
- **Untouched originals:** `../Felucca-main` is the original tree with the VOID engine. Read it, never edit it. `../Felucca-main.bak` is older still.

## 2. Documents to read

1. **The plan:** `docs/plans/2026-10-16-fm1-four-machine.md`. It is the single source of requirements, including the global constraints, decisions D1–D9, the machine contract, the CC map, the screen mockups and every task. The user's answers are at the bottom (§ "Answers from the user").
2. **Background on donors and budgets:** `../FM-1 Four-Machine Firmware Plan.md` (the user's original design note).
3. **This-Mac test baseline and build quirks:** `docs/BASELINE.md`.
4. **MIDI behaviour, as shipped:** `docs/MIDI.md`.
5. **Per-task briefs and reports:** `.superpowers/sdd/` (git-ignored). `progress.md` is the ledger. For each finished task there is a `task-*-brief.md` and a `task-*-report.md`, with design notes and test evidence.
6. **Project rules:** `../CLAUDE.md`, the FM-1 section.

## 3. User's standing instructions (binding)

- **Process:** work subagent-driven (superpowers:subagent-driven-development). Each task gets a fresh implementer, then a task review (spec and quality). Fix Critical and Important findings and re-review. Log Minor findings in the ledger. Run one broad review at the end.
- **Persona and pace:** the user wants this "built quickly but well", by a software engineer specialising in DSP and synthesis.
- **Machine naming:** the OB machine is displayed **OB-Z**.
- **MIDI out:** USB only. The TRS jack stays input-only.
- **Web editor:** later. It is out of v0.1, so don't touch `web/`.
- **HOME knobs:** K4 is TEMPO, as confirmed.
- **Flashing:** never flash the FM-1 without the user's explicit go-ahead *in that session*, with the device connected.
- **Pending question:** "PRESETS encoder on non-HOME pages should stay within the track's machine (like HOME)". I proposed it to the user, and the user has **not answered yet**. Ask the user before you change it. Today, HOME's K2 and PRESETS stay within the machine, but PRESETS on the other pages walks all machines and can switch the machine.

## 4. Progress

| Task | Status | Commits |
|---|---|---|
| 0.1 Fork and baseline | done | e73bcfe..9d9ed5c |
| 1.1 Strip to 4 engines | done, reviewed | 4e30351..1991559 |
| 1.2 FOR4 storage format and identity FM-1_940 / v0.1 | done, reviewed | ..5115e54 |
| 1.3 Categories and 4×32 presets (generator) | done, reviewed | ..ad88835 |
| 2.1 + 2.2 CC map and MIDI OUT INT/EXT/BOTH, clock out | done, reviewed | ..5f4d4d3 |
| 3.1 + 3.2 Fonts (full A–Z L, 40 px XL digits) and big EDIT page | done, reviewed | 798733e, 210e03f, 5f23c16 |
| **3.3 + 3.4 Big HOME and page prune** | **implemented, NOT yet reviewed** | b71241d, adffc6e |
| 4.1 OB-Z: SEM morph, 4-pole, slop | done, reviewed | f539344..915487a |
| 4.2 WAVE DC blocker / 4.3 harness WAVE_PX | done, reviewed | 7c9a0ff, ce77bb8 |
| 5.0 own-envelope hooks / 5.1 FPU on | done, reviewed | 5595251, d537314 / 893f3b7 |
| 5.2 ACID: float 303 (bass303 / Open303 port) | done, reviewed | 3cd1204..3138e9d, e2e47ec |
| 5.3 Accent and slide editable on ACID (test only) | done, reviewed | e65bb4e |
| 6.1 DRIFT: the Juno voice (KR-106 port) plus `dsp_va.c` | done, reviewed (mono until 8.1) | 89d4a18..bd9721a |
| 6.2 DRIFT: the Juno chorus (OFF / I / II / I+II, KR-106 port; `engine_t.part_fx`, `dsp_chorus.c`) | implemented, NOT yet reviewed | 2ffa143.. (see `.superpowers/sdd/task-6.2-report.md`) |
| 7.1 Audition renderer (WAV + rating page) | to do | |
| 7.2 Final 128 presets, with user sign-off by ear | to do | |
| 8.1 Hardware: release build, install (**ask first**), PERF, MIDI check | to do | |
| 9 Release docs (README, CREDITS, LICENSES, CLAUDE.md, memory) | to do; the web step is deferred | |

Current state at HEAD adffc6e:
- **Image:** 344,712 B of APP_SLOT 581,564 B, so about 231 KB is free.
- **Full suite:** fails only on the known `perform: FLANGER ... PHASER ... FAIL`.
- **UI lint:** `ui_render` reports 0 findings.
- **Golden:** 141 renders, unchanged since Task 1.3.

### Next steps, in order

1. **Review 3.3 + 3.4.**
   - Make the diff package with `scripts/review-package 5f23c16 adffc6e`. The script lives in the subagent-driven-development skill directory.
   - Disclosed concerns to judge:
     - The sound panel is 80 px instead of 82, so the footer lines up with the EDIT page.
     - The editor's MENU id 23 (the old HOME view row) is now unknown, which leaves 36 menu items instead of 37.
     - Most of the pruning had already happened in earlier tasks.
   - Look at `build/ui_new/GREY/home_big.png` and `home_big_t3.png`.
2. **Phases 4 → 5 → 6 (DSP), then 7, 8, 9.**
   - Do DRIFT last: it carries the most CPU risk.
   - Before each DSP phase, write a short sub-plan in `docs/plans/` if the donor port is larger than about 300 lines. The plan rules require this.

## 5. How to build and test

```bash
cd "/Users/ollie_nicklin/Documents/Projects/VOID SYNTH/FM1-Four"
open -a Docker                                   # Docker Desktop must be running
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
[ -d /tmp/x0xvenv ] || (python3 -m venv /tmp/x0xvenv && /tmp/x0xvenv/bin/pip install Pillow fonttools numpy mido python-rtmidi)
source /tmp/x0xvenv/bin/activate
./build.sh                                       # ~1 min; prints "app ... felucca.bin N B" and "identity FM-1_940"
sh tests/run_tests.sh                            # ~5-6 min; only allowed failure: perform FLANGER/PHASER
```

Focused tests (after `./build.sh`, which generates `build/gen/`):
```bash
mkdir -p build/host
cc -O1 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/four_test tests/four_test.c -lm && build/host/four_test
cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -o build/host/regress tests/regress.c -lm && build/host/regress tests/golden.txt tests/cpu_baseline.txt
cc -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test
# UI screenshots + lint:
FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)
cc -O1 -w -ffp-contract=off "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o build/host/ui_render tests/ui_render.c -lm
mkdir -p build/ui_new/ppm build/ui_slot && build/host/ui_render build/ui_new build/ui_slot && python3 tests/ui_render.py build/ui_new build/ui_slot
```
**Always pass `-ffp-contract=off`** (as run_tests.sh does): ACID and DRIFT are float, the device has no fused multiply-add, and without the flag the host fuses FMAs and every ACID golden reads as changed. The midi_control, project and other tests compile the same way. Take the exact lines from `tests/run_tests.sh`.

### Environment quirks

- **macOS ASan:** the ASan runtime aborts in its probe, so the sanitizer stage is skipped. To check a fuzzer such as `fuzz_proj`, build it without `-fsanitize`.
- **Disk space:** the disk is nearly full (about 4.9 GB free). `build/ui_new/ppm` can be deleted after the PNGs are converted. `../Felucca-main/build` (1.3 GB) can be regenerated, but ask the user before you delete it.
- **No emcc:** the web emulator can't be built. It is out of scope anyway.

### Golden and budget rules

- **Never** use `GOLDEN_UPDATE=1` or `BUDGET_UPDATE=1` to hide a regression.
- Regenerate only after a deliberate sound change, and review the `tests/golden.txt` diff. For any changed hash on a kept engine, write the reason in the commit message.
- `tests/cpu_baseline.txt` was recorded on this Mac in Task 0.1. Its only rewrites since, each by the user's decision and each
  limited to lines that measured a placeholder: Task 4.3 (the 32 `cpu/WAVE/` lines), Task 5.2 (the 32 `cpu/ACID/` lines, e2e47ec) and
  Task 6.1 (the 32 `cpu/DRIFT/` lines, which measured OB-Z's ANALOG render, and `cpu/mix/4parts_light`, whose DRIFT part did the
  same: 2292 against the old 1787 (+28 %) once DRIFT was real, beyond the check's +25 %; `4parts_full` stays at 1705 and reads
  +15 %). Task 4.4 replaced the 32 `cpu/WAVE/` lines once more (WAVE lost its second oscillator and the loop overhead: by the user's
  decision, 251 -> 179 host instructions a voice), and added `void_render` and `void_block_slow` to `target_budget.txt`; no `cpu/mix/` line
  fell past the tolerance. `BUDGET_UPDATE` was not used. Task 6.1 also took `analog_render_f` out of `target_budget.txt` (the dead ANALOG copy in
  `eng_obx.c` and the `run_tests.sh` guard that waited for its removal are gone) and added `drift_render`.

## 6. Architecture notes you need

- **Machine contract:**
  - `engine_t.knob[4] = {P_E4 CUT, P_E5 RES, P_ED_FLT ENV, machine control}`.
  - The machine control is `P_E1` on OB-Z (DETUNE) and WAVE (POSITION), `P_E2` on ACID (DECAY), and `P_E6` on DRIFT (DRIVE).
  - Three things depend on this contract: the big EDIT page (`firmware/src/ui_big.c`, resolved via `page_id()` in params.c), the CC map (`cc_param`/`param_cc` in midi_control.c) and four_test.
  - **Keep CUT = E4 and RES = E5 in the new engines.**
- **Params:**
  - `P_COUNT` is 112. `P_MOUT` sits just before `P_E0`.
  - `P_RSV_LN..` is a reserved block (the old drum lanes), kept so the param ids don't shift.
  - Add any new common param just before `P_E0`. That changes the FOR4 layout, which is fine before release; no migration is needed.
- **Presets:**
  - Source files: `presets/four/{obx,wave,acid,drift}.py`.
  - `tools/four_presets.py` (run by build.py) validates counts, categories, names (12 characters max, from the set `" #%+-./0-9A-Z"`) and `edit[]` ranges, then writes `build/gen/four_presets.h`.
  - The arrays are unsized. `engines.c` has `_Static_assert`s for exactly 32.
  - WAVE's extras (`WAVE_PX`, `void_px_t`) are in wave.py.
  - When ACID and DRIFT get real DSP, **re-voice their 32 presets**: the seeds were written blind.
- **Categories:** `CAT_BASS LEAD KEYS BRASS STRINGS ATMOS ACID`. Counts per machine:
  - OB-Z: BRASS 8, STRINGS 8, KEYS 8, LEAD 8
  - WAVE: ATMOS 12, LEAD 10, BASS 10
  - ACID: ACID 32 (ACID exists only on this machine)
  - DRIFT: BASS 12, LEAD 10, KEYS 10
- **Sequencer:** `step_t` already has `SF_ACCENT`, `SF_SLIDE` and `ST_TIE`.
  - An accented step plays at velocity 127, and `voice.c` adds envelope to the cutoff when vel > 110.
  - A slide sets `t->slide_glide`.
  - ACID should read these.
- **MIDI OUT:**
  - All output is produced in the audio/control block (`mout_block`, `trk_note_on`), because the USB out ring is single-producer. **Never call `midi_out_event` from the UI thread.**
  - `trk_voice_on` is the voice-only half of `trk_note_on`.
- **Shared modulation:** `voice.c:~680` builds `vmod_t`: `m.cutoff` (LFO and ENV), `m.shape`, `envq15`.
  - Engines read `m->cutoff` on top of their own CUT.
  - ACID will want `ownenv` (its own MEG/VEG). The `engine_t` has `ownenv`, `amp` and `done` hooks for this.
- **Engine memory:** `ENG_MEM_PART` is the max of `VOID_MEM`, `ACID_MEM` and `DRIFT_MEM` (engines.c). Size `ACID_MEM`/`DRIFT_MEM` for the real state.
- **CPU checks:**
  - Add new render functions to `tests/target_budget.py FUNCS`.
  - Add their entries to `cpu_baseline.txt`, which is how new entries get added. Never overwrite existing lines.
    One exception, taken once: Task 4.3 rewrote the 32 `cpu/WAVE/` lines, because until then
    `tests/hostsim.c` never applied `WAVE_PX`, so those figures measured a configuration the device
    never plays. Every other line was left alone. This is not a precedent.
  - The target is a full four-track pattern at ≤ 80% of one core on hardware.

## 7. Known issues backlog (log; triage at the final review)

1. ~~**WAVE has no DC blocker.**~~ Done, Task 4.2: a one-pole blocker per voice (`eng_wave.c void_dc`)
   between the voice's level and the soft clipper; BRASS LEAD is back to REV 25. The only deliberate
   departure from the plugin in the ported signal path. Task 4.3 moved its corner from ~35 Hz
   (R 0.995) to ~10 Hz (R 0.9985, `VOID_DC_SH` 10): DC sits at 0 Hz either way, and C1 now loses
   0.4 dB instead of 3.3 dB, which 10 of WAVE's 32 presets (its BASS category) were paying.
   Measured with the blocker off and the harness corrected (4.3), 8 of the 32 presets hold over 1 %
   DC at the engine output (worst `NEURO WOBBLE` 0.054); BRASS LEAD is not one of them -- it ships
   `ftype = 2` (HP), so the 4.2 evidence for it was an artefact of the harness, not of the engine.
2. **MIDI OUT minor findings:**
   - An owed note-off goes out on the current channel. Fix by adding `mout_och[k]`.
   - The owed note-off in `mout_note` uses `mout_send` instead of `mout_try`.
   - `mout_pay` runs on INT tracks too.
   - Nested loads finalise each other (`ui.c ~902`).
3. **Behaviour change, by design:** with the default INT mode, the panel keys don't send MIDI. BOTH re-enables it. Mention this in the README.
4. **Editor protocol (web is deferred):**
   - The user-preset sentinel changed from 15 to 4.
   - MENU id 23 is gone.
   - `web/EDITOR_PROTOCOL.md` doesn't describe G_CLKOUT or MOUT.
5. **`category.c:20`:** stale comment ("PRESETS[32]"; the arrays are now unsized).
6. **Big EDIT values:** "-100" is the only value that falls back to AF_M. ENV +100 shows as "100" (decision taken; the ring still shows the sign).
7. **OB-Z carries DC of its own** (logged in Task 4.1 fix round 2; not fixed, deliberately):
   - WAVE SYNC puts about 10 % of rms as DC through the legacy low-pass (MODE 0), nothing to do
     with the morph; and the 4-pole at RES 127 adds about 4.5 % more, because the soft knee clips an
     asymmetric resonant waveform symmetrically (the pre-knee signal has 0.1 %).
   - No blocker stands in front of it: the part's reverb send is taken ahead of the master's `fx.c`
     dc_block, so the DC reaches the reverb. A one-pole blocker per voice, as WAVE has (item 1),
     is the likely fix. `test_mode_bounded` only holds the morph and the 4-pole to adding under 5 %
     past what MODE 0 already carries.

## 8. Gotchas learned so far

- The test suite reports a pass even when a sub-stage silently didn't run (ASan). Before you claim green, check that a stage you changed actually ran.
- `ui_render` findings **do** fail the suite. Keep it at 0 findings.
- Earlier "the only failure is perform" claims were once wrong because of 80 splash findings. Verify claims like that against `build/ui_new/report.txt`.
- Implementers sometimes skip RED. Ask for the RED output explicitly.
- `scripts/task-brief` matches "Task N" by regex. For dotted task ids, pass `"1\\.1"` and an explicit out-file.

## Switch Blade: rename and boot logo (UI.1, 2026-10-19)
- User-visible name is **Switch Blade**: splash, USB product string (`usb.c` STR2), console banner, MENU > INFO > FIRMWARE, editor INFO string, release package `switchblade-X.Y.fwsc`. Unchanged: installer identity `FM-1_940`, the FOR4 format, machine names, C identifiers, the folder name.
- Boot logo: `assets/switchblade_logo.jpg` -> `tools/gen_logo.py` (run by `build.py`) -> `build/gen/ui_logo.h` (236x135, <= 128 colours, index 0 black, 8 bpp + RGB565 palette, `const`). `draw_splash` blits it row by row to the LCD in its own colours (not themed). Tests: `tests/switchblade_test.py`, `tests/usb_desc_test.c`, `tests/ui_render.c` splash.

## Switch Blade: machine themes and trims (UI.2 + UI.3, 2026-10-19)
- **MENU > COLOR > MACHINE** is value 0 and the default of fresh settings: the palette follows the selected track's machine (`UI_MACHINE_PALS` in `tools/gen_ui_palettes.py`, engine order OB-Z WAVE ACID DRIFT; ACID is a light palette). Fixed palettes are values 1..10, CUSTOM is value 11 (`COLOR_CUSTOM`, only while one is stored); a fixed palette or CUSTOM pins the look and has no trim.
- Code: `gfx.c palette_set` (ids: fixed 0..9, CUSTOM `NPALETTES`, machine `PAL_MACHINE_ID(e)`; `ux.trim` = 1 + engine for a machine palette), `favorites.c palette_apply / palette_follow` (checked every frame in `ui_draw`: a track or machine change sets the palette and `ui.force`), `ui_trim.c` (the trim as data: screen-pixel rectangles per machine and page, drawn by `trim_cv` right after `cv_begin` of the header, the HOME and EDIT strips and the big footer; `trim_screen` for HOME's tab gaps). No trim in STYLE LINE.
- Stored: `settings.palette` is a fixed index or `UI_PAL_MACHINE` (= `NPALETTES`); stored id `UI_PAL_TAG + 10` (older firmware reads it as GREY). Old saved palettes load as before. The editor's COLOR list now starts with MACHINE (`web/EDITOR_PROTOCOL.md`).
- Palettes were sampled from `assets/refs/` and lifted only where `gen_ui_palettes.py`'s contrast rules failed (the list is in `.superpowers/sdd/task-ui23-report.md`).
- Tests: `ui_render` draws every screen with each machine's palette, in MACHINE mode on each machine's track and the value sweeps, and `hk_trim` checks that every trim pixel is still whole at the blit (nothing drawn over it); `ui_test` (track / machine follow, pinning, CUSTOM), `settings_test` (stored id, old ids), `theme_test` (contrast).

## Switch Blade: the machine look is the only colour scheme (UI.4 Part E, 2026-10-19)
- MENU > COLOR, the ten fixed palettes (GREY GREEN AMBER ICE VIOLET ROSE PAPER HI-CON NIGHT MONO) and CUSTOM are gone. `gfx.c palette_set(e)` takes a machine (0..3, `PAL_MACHINE_ID`); `NPALETTES` is `UI_NMACHINE` (4); `ux.trim` is always 1 + machine. `settings.palette` is always `UI_PAL_MACHINE` (0): `palette_from_stored` maps every stored id to it (older indices below 20, tagged ids, CUSTOM's), `palette_to_stored` writes `UI_PAL_TAG + 10` (older firmware reads GREY), `palette_stored_ok` still refuses garbage. CUSTOM's bytes (`favorites.factory[15][0..9, 12]`) are kept as saved and unused.
- `menu_items.c`: `MI_COLOR` removed (DISPLAY tab: STYLE LARGE ANIM LEDS SCREEN OFF SCOPE; the editor's id 0 is retired, MENU_DESC lists 35 items). Editor PALETTE_GET / PALETTE_SET are accepted and ignored (`web/EDITOR_PROTOCOL.md`).
- STYLE LINE and MENU > LARGE are layout options and stay (on the machine palettes). `gen_ui_palettes.py` emits only `UI_MACHINE_PALS` (+ REC, QR, CRASH colours); the GREY / MONO special cases (`ux.mono`, `UI_BW_*`, `ux_gray`) are deleted.
- Tests: `theme_test` (contrast of the four machine palettes in FLAT), `settings_test` (every old / tagged / CUSTOM id loads as MACHINE, the stored id), `ui_test` (`screen_on_palette` replaces the GREY / MONO "every pixel gray" checks: every pixel is a palette token, a trim colour or a blend), `ui_render` (the 10-palette sweep is gone: MACHINE mode on each track plus every machine's palette forced; audits from OB-Z).

## Switch Blade: Exo 2, LED readouts, per-machine controls, rack HOME (UI.4, 2026-10-19)
- **Type:** the UI faces are Exo 2 (S 12 / M 15 Medium, L 28 / XL 40 SemiBold; keycaps stay Inter Tight): `tools/gen_aa_font.py --preset exo2`. Exo 2's line box is 1-2 rows taller than Inter Tight's, so the preset moves each face's baseline up to Inter's rows (`aa_raster.raster_font asc_adj`: S -1, M -1, L -1, XL -2): every layout kept its baselines, `text_spacing_test` limits were widened to what Exo 2 measures (S 0.33, M 0.30, L 0.5, XL 1.1 px).
- **LEDs (`ui_led.c`):** `cv_led(x, y, size, cells, string, under, lit)`: 7-segment digits drawn with rectangles (LED_S 7 x 11, 1 px segments; LED_L 11 x 18, 2 px), unlit segments dim, a dark recess with a 2 px rounded corner, decimal points, the LED's own colours in every palette (`LED_ON / LED_HOT / LED_OFF / LED_BG`). Used by the header BPM (3 cells, always as wide; the hot colour while it is turned; no rolling digits), EDIT's readouts (OB-Z, ACID, DRIFT: the unit beside the recess; a value an LED cannot spell, "OFF", goes in the recess as text) and HOME's track and preset numbers.
- **Controls (`ui_ctl.c`, `ui_big.c`):** anti-aliased discs / arcs / strokes (4 x 4 samples where an edge falls, integers only), `ctl_card` (bevel), `ctl_brushed_c` (the TB-303's horizontal lines, seeded by the screen row), `ctl_wood` (grain seeded by screen column and 16-row band), `ctl_screw`. EDIT FLAT: OB-Z knobs with 11 ticks on bevelled cards between 8 px grained wood cheeks (the trim, `TK_WOOD`), WAVE amber glow rings (value in Exo 2; a bipolar value's arc runs from 12 o'clock), ACID black skirted knobs on brushed silver with corner screws, DRIFT four faders (travel y 66 .. 178, tan caps = the palette's 6th colour, 11 ticks) under a 3 px red band with a VCF bracket. The K pills of EDIT are gone in FLAT (the position is the knob; the accent / hot colour shows the knob turned); STYLE LINE keeps the old cards (`draw_big_card`).
- **HOME (`ui_big.c home_unit / home_cards`):** four 40 px rack units (24, 64, 104, 144; each a canvas of its own, in the unit's machine's palette, not the selected track's): ears with two screws a side, the faceplate look, the track's LED number, the badge, the category, the name (M), the preset's LED number, an activity LED (the track's meter `ts.meter` >= 3 or a MIDI IN note), the selected unit outlined (1 px white + a half-tone inside). Under them TRACK PRESET VOLUME TEMPO in four bevelled 57 x 26 cells (184 .. 211). The sound panel's level bar and the tabs' meters are gone; `trim_fill` / `trim_screen` and HOME's trim are gone (the rack carries its own looks); a trim pixel is never written twice (the #181 flash test covers HOME and EDIT on every machine).
- **Tests:** `ui_test` (`test_led`, `test_edit_controls`, `test_rack`, `screen_on_palette` with LED / wood / triangle blends), `ui_render` (cost2 lines: a forced full redraw of HOME and EDIT per machine), draw cost before / after in `.superpowers/sdd/ui4/task-ui4-report.md`.


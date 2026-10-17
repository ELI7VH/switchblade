# FM-1 Four-Machine Firmware ("VOID FOUR") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fork of Felucca 1.6 for the M-VAVE FM-1 that has exactly four synth machines (OBX, WAVE, ACID, DRIFT), 32 curated factory presets per machine, a big-type HOME and EDIT UI built on four controls per screen, the step sequencer per track, and MIDI channels 1–4 per track with a fixed CC map that works both in and out.

**Architecture:** Copy `Felucca-main/` to a new git repo `FM1-Four/`. Delete every other engine, its presets, sample data, editor screens and tests, and renumber the engine table to 0..3 behind a new storage format. The machines reuse what already exists: OBX grows out of `eng_analog.c`, WAVE is the host-tested VOID engine (`eng_void.c`), and ACID and DRIFT are new fixed-point engines. Every machine follows one contract: CUT = `P_E4`, RES = `P_E5`, ENV = the shared `P_ED_FLT`, and one machine-specific control. That single contract drives the EDIT screen, the CC map and the presets.

**Tech Stack:** Plain C99 for the JieLi AC79 (pi32v2), built in one unity build (`felucca.c`). Fixed-point DSP only. Python 3 generators. Host tests use `cc` and `tests/run_tests.sh`; UI tests use `tests/ui_render.c`. Docker builds the target.

**Source:** this plan builds on `FM-1 Four-Machine Firmware Plan.md` (machine designs, donors, budgets, licensing). Where the two disagree, this plan wins. The user's brief (2026-10-16) changes four things: no DRUM machine, a 32-preset quota per machine with fixed categories, a new 4-control HOME and EDIT, and MIDI out for external gear.

## Global Constraints

- Exactly 4 machines: `0 OBX`, `1 WAVE`, `2 ACID`, `3 DRIFT`. No DRUM, SAMPLE, SLICE, GRAIN, FM6, PHYS, NOISE or any other Felucca engine in the build.
- Exactly 32 factory presets per machine (128 in total), enforced by `_Static_assert`.
- Preset categories: `BASS`, `LEAD`, `KEYS`, `BRASS`, `STRINGS`, `ATMOS`, `ACID`. No drum or noise/FX sounds.
- ACID-category sounds live only on the ACID (303) machine.
- Every UI screen has at most 4 controls, mapped 1:1 to KNOB 1..4.
- Screen: ST7789V 240×240 RGB565. Largest drawable strip 240×124 (`CV_MAX`).
- Track N plays on MIDI channel N (1..4) by default, both in and out.
- The CC map is the same for MIDI in and MIDI out, and it is documented in `docs/MIDI.md`.
- DSP is fixed point, **except ACID and DRIFT, which are single-precision float on the FPU (override D10 / `docs/plans/2026-10-16-void-four-continuation.md`; Task 5.1 turned it on, a `double` fails the link)**. No allocation, no libc in engines (`fastmath.h` is the float maths library). Nothing may depend on the second core.
- The app image must be ≤ `APP_SLOT` = 0x8DFBC (581,564 B). RAM headroom ≥ 4096 B, pool headroom ≥ 8192 B (`tools/build.py check()`).
- Full four-track pattern load must stay ≤ 80% on one core (`song.cpu_q8`, measured on the FM-1).
- Licence: GPL-3.0-only with full source. Keep each donor's copyright notice and put its licence text in `LICENSES/`.
- Never flash the FM-1 without the user's explicit go-ahead in that session, with the device connected.
- Don't use `BUDGET_UPDATE=1` or `GOLDEN_UPDATE=1` to hide a regression. Only regenerate after a deliberate sound change, and review the diff of `golden.txt` (removing lines for deleted engines is fine; a changed hash for a kept engine needs a reason).

## Decisions taken in this plan (change them before Phase 1 if wrong)

| # | Decision | Why |
|---|---|---|
| D1 | **WAVE = the existing VOID engine** (`eng_void.c`, renamed), not a new Braids port | It is already built, host-tested and matched to the VOID WAVE plugin within 0.06 dB. It saves a whole engine. |
| D2 | **Fork into `FM1-Four/` with `git init`**; `Felucca-main/` stays untouched | `Felucca-main` holds the VOID-on-Felucca work and isn't under git. |
| D3 | **Delete removed engines** instead of hiding them behind build flags | You asked for them gone. The UI rewrite makes upstream merges impractical anyway. |
| D4 | **New storage format.** Engine ids 0..3; new magics for projects, settings and user presets; no migration from Felucca saves | Old Felucca ids (14 for VOID etc.) mean nothing here. Old saves on the device are ignored, and the unit starts from the factory project. |
| D5 | **The 4th EDIT control:** OBX **DETUNE**, WAVE **POSITION** (wavetable scan), ACID **DECAY**, DRIFT **DRIVE** | These make the biggest audible change per machine. |
| D6 | **ENV = `P_ED_FLT`** (envelope → cutoff, bipolar) on all four machines | It is shared and already wired in `voice.c:682`. On ACID it is the 303's ENV MOD. |
| D7 | **HOME K1..K4 = TRACK, PRESET, VOLUME, TEMPO** | You asked for track, volume and preset. TEMPO fills the fourth slot because it is the one global setting a player reaches for. |
| D8 | **MIDI out goes over USB only** | The firmware has no TRS MIDI TX (`midi_uart.c` is RX only). TRS out is in the open questions. |
| D9 | **Working name "VOID FOUR"**, package id `FM-1_940`, version `v0.1` | It has to match `[^_]+_\d+` (`fm1_install.py`) and be ≤ 20 characters. The installer and M-VAVE's updater use it to tell this firmware apart from Felucca. |

## The machine contract

`engine_t.knob[4]` (core.h:205) already exists and drove the old HOME cards. In this firmware it means **the EDIT screen's four controls**, in this fixed order:

| Machine | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | Voices (1 core) | Categories (32) |
|---|---|---|---|---|---|---|
| 0 OBX | CUT `P_E4` | RES `P_E5` | ENV `P_ED_FLT` | DETUNE `P_E1` | 8 | BRASS 8 · STRINGS 8 · KEYS 8 · LEAD 8 |
| 1 WAVE | CUT `P_E4` | RES `P_E5` | ENV `P_ED_FLT` | POS `P_E1` | 6 (needs FM-1 measurement) | ATMOS 12 · LEAD 10 · BASS 10 |
| 2 ACID | CUT `P_E4` | RES `P_E5` | ENV `P_ED_FLT` | DECAY `P_E2` | 1 (mono) | ACID 32 |
| 3 DRIFT | CUT `P_E4` | RES `P_E5` | ENV `P_ED_FLT` | DRIVE `P_E6` | 2 | BASS 12 · LEAD 10 · KEYS 10 |

Category totals: BASS 22, LEAD 28, KEYS 18, BRASS 8, STRINGS 8, ATMOS 12, ACID 32.

CUT is always `P_E4` and RES always `P_E5`, so the existing FILTER page (`params.c` `{P_FTYPE, P_E4, P_E5, P_E7}`) keeps working on every machine.

## The CC map (in = out)

| Control | CC | Scope |
|---|---|---|
| VOLUME | 7 | track |
| CUT (EDIT K1) | 74 | track |
| RES (EDIT K2) | 71 | track |
| ENV (EDIT K3) | 79 | track |
| MACHINE control (EDIT K4) | 70 | track |
| PAN / GLIDE / ATTACK / DECAY / RELEASE | 10 / 5 / 73 / 75 / 72 | track (unchanged from Felucca) |
| REVERB / CHORUS / DELAY send | 91 / 93 / 94 | track (unchanged from Felucca) |
| Preset | Program Change 0..31 | track |
| Transport | Clock F8, Start FA, Stop FC | global, out and in |

Each track works on its own channel: track N uses channel N. Per track, `MIDI OUT` can be:
- **INT**: plays the internal machine only (default).
- **EXT**: drives external gear; the internal voice is silent.
- **BOTH**: plays internally and sends out at the same time.

In EXT and BOTH, the track's sequencer notes, its keys, its EDIT knobs, its volume and its preset changes are sent out on its channel. That is the "clean route to external gear": point a hardware synth at channel N, set track N to EXT, and K1..K4 control its filter on CC 74/71/79/70.

## Screen designs

All type sizes are the generated Inter Tight faces. Today: `AF_S` 12 px, `AF_M` 15 px, `AF_L` 28 px (capitals A–Y only). Task 3.1 adds a full-alphabet `AF_L` and a 40 px digit face `AF_XL`.

**HOME** (240×240):
```
 0 ┌──────────────────────────────────────┐
   │ VOID FOUR        ▶ 120         ▮▮▮▯ │ header 24 (M)
24 ├─────────┬─────────┬────────┬─────────┤
   │   1     │   2     │   3    │   4     │ track tabs 44: digit L 28px,
   │  OBX    │  WAVE   │  ACID  │  DRIFT  │ machine S; the selected tab is filled,
68 ├─────────┴─────────┴────────┴─────────┤ the others show a level meter strip
   │ BRASS                       07 / 32 │ category S + preset number M
   │ ROYAL HORNS                         │ preset name L 28px (fits 12 chars → M fallback)
   │ ████████████████░░░░░░░   -6 dB     │ volume bar 14px + value M
150├─────────┬─────────┬────────┬─────────┤
   │ TRACK   │ PRESET  │ VOLUME │ TEMPO   │ the 4 controls: label S,
   │   1     │   07    │  -6    │  120    │ value L; one card per knob, 57×64
214├─────────┴─────────┴────────┴─────────┤
   │ ▪▪▪▫▪▪▫▪▪▪▫▪▪▪▫▪   step dots      │ footer 26: the selected track's 16 steps
240└──────────────────────────────────────┘
```
The dedicated encoders keep working as well: **ALGORITHM** = track, **PRESETS** = preset.

**EDIT** (240×240), four big cards in a 2×2 grid:
```
 0 ┌──────────────────────────────────────┐
   │ [1] OBX · ROYAL HORNS        EDIT   │ header 24
24 ├──────────────────┬───────────────────┤
   │ CUTOFF       K1  │ RESONANCE     K2  │ label M; K-pill S
   │      ◜‾‾‾◝       │       ◜‾‾‾◝       │ 270° arc ring, r=30
   │       84         │        42         │ value XL 40px digits
   │                  │                   │ card 118×94
118├──────────────────┼───────────────────┤
   │ ENV          K3  │ DETUNE        K4  │
   │      ◜‾‾‾◝       │       ◜‾‾‾◝       │
   │      +32         │        18         │
212├──────────────────┴───────────────────┤
   │ ▪▪▪▫▪▪▫▪▪▪▫▪▪▪▫▪   step dots      │ footer 28
240└──────────────────────────────────────┘
```
Each row of two cards is drawn as one 240×94 strip, which is under `CV_MAX`. Tapping EDIT again goes on to the deeper pages: MACHINE 1 and 2 (the 8 `edit[]` params), FILTER, VOICE. The deeper ENV/LFO/FX/SCL/ARP/SEQ pages stay as they are, minus every DRUM/FM6/SLICE page.

---

## Phase 0: Fork and baseline

### Task 0.1: Create the fork and record the baseline

**Files:**
- Create: `FM1-Four/` (a copy of `Felucca-main/`), `FM1-Four/docs/BASELINE.md`

- [ ] **Step 1: Copy and init**
```bash
cd "/Users/ollie_nicklin/Documents/Projects/VOID SYNTH"
rsync -a --exclude build/ Felucca-main/ FM1-Four/
cd FM1-Four && git init && git add -A && git commit -qm "chore: fork Felucca 1.6 + VOID engine as FM1-Four baseline"
```

- [ ] **Step 2: Build env** (quirks from the VOID port)
```bash
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
[ -d /tmp/x0xvenv ] || (python3 -m venv /tmp/x0xvenv && /tmp/x0xvenv/bin/pip install Pillow fonttools numpy mido python-rtmidi)
source /tmp/x0xvenv/bin/activate
./build.sh 2>&1 | tail -3
```
Expected: the line `image 566888 B; RAM ...`, give or take a few bytes.

- [ ] **Step 3: Run the host tests and record the failures that already exist**
```bash
sh tests/run_tests.sh 2>&1 | tee /tmp/baseline.log | grep -E "FAIL|ok$" | sort | uniq -c | tail -40
```
Expected: everything passes except the known failures on this Mac: perform FLANGER/PHASER and about 14 ANALOG/PHASE CPU-budget entries. Write the exact list into `docs/BASELINE.md`. Later tasks may not add to it.

- [ ] **Step 4: Commit** `docs/BASELINE.md`: `git commit -m "docs: record baseline test state"`

---

## Phase 1: Strip to four engine slots

Order matters: the build must be green after each task. During this phase, ACID and DRIFT are **placeholders** that reuse the ANALOG render, so the engine table, storage and UI can be finished before the new DSP exists.

### Task 1.1: New engine table, ids and placeholder machines

**Files:**
- Modify: `firmware/src/core.h:16-33` (engine flags, NENGINES), `:118-151` (ENGI_PHYS/DRUM/SAMPLE and migrations)
- Modify: `firmware/src/engines.c:24-195`
- Create: `firmware/src/eng_acid.c`, `firmware/src/eng_drift.c` (placeholders)
- Rename: `eng_analog.c` → `eng_obx.c`, `eng_void.c` → `eng_wave.c` (`git mv`)

**Interfaces produced (used by every later task):**
```c
/* core.h */
#define NENGINES 4u
#define NENG_SHOWN 4u
#define ENGI_OBX 0u
#define ENGI_WAVE 1u
#define ENGI_ACID 2u
#define ENGI_DRIFT 3u
/* engines.c */
static const engine_t *const ENGINES[NENGINES];   /* {&ENG_OBX, &ENG_WAVE, &ENG_ACID, &ENG_DRIFT} */
static const uint8_t ENGINE_ORDER[NENG_SHOWN];      /* {0, 1, 2, 3} */
```

- [ ] **Step 1: Write the failing test** `tests/four_test.c`
```c
/* four_test.c: the four-machine contract */
#include "hostsim.c"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void test_engine_table(void)
{
    static const char *const NAME[4] = {"OB-Z", "WAVE", "ACID", "DRIFT"};
    uint32_t e;
    CHECK(NENGINES == 4u, "NENGINES %u", (unsigned)NENGINES);
    for (e = 0; e < NENGINES; e++) {
        CHECK(strcmp(ENGINES[e]->name, NAME[e]) == 0, "engine %u is %s", (unsigned)e, ENGINES[e]->name);
        CHECK(ENGINES[e]->knob[0] == P_E4, "%s K1 not CUT", NAME[e]);
        CHECK(ENGINES[e]->knob[1] == P_E5, "%s K2 not RES", NAME[e]);
        CHECK(ENGINES[e]->knob[2] == P_ED_FLT, "%s K3 not ENV", NAME[e]);
    }
    CHECK(ENGINES[ENGI_OBX]->knob[3] == P_E1, "OBX K4");
    CHECK(ENGINES[ENGI_WAVE]->knob[3] == P_E1, "WAVE K4");
    CHECK(ENGINES[ENGI_ACID]->knob[3] == P_E2, "ACID K4");
    CHECK(ENGINES[ENGI_DRIFT]->knob[3] == P_E6, "DRIFT K4");
    CHECK(ENGINES[ENGI_ACID]->poly == 1u, "ACID mono");
}

int main(void)
{
    test_engine_table();
    printf(fails ? "four_test: %d FAIL\n" : "four_test: ok\n", fails);
    return fails != 0;
}
```
Register it in `tests/run_tests.sh` next to `void_test`, using the same compile line:
```sh
$CC -Ibuild/gen -Ifirmware/src -Itests -o build/host/four_test tests/four_test.c -lm && build/host/four_test || fail=1
```

- [ ] **Step 2: Run it.** `sh tests/run_tests.sh 2>&1 | grep four_test`. Expected: the compile fails on `ENGI_OBX`.

- [ ] **Step 3: Implement**
  - `core.h`: delete `FELUCCA_SLICE`, `FELUCCA_FM4` and `FELUCCA_VOID`, and replace them with the defines in **Interfaces**. Delete `ENGI_PHYS`, `ENGI_DRUM`, `ENGI_SAMPLE`, `SMP_SET_PERC`, `DRUM_KIT_E`, `drum_from_phys` and `drum_from_perc`.
  - `engines.c`: include only `dsp.c`, `eng_obx.c`, `eng_wave.c`, `eng_acid.c` and `eng_drift.c`. Set `ENGINES[] = {&ENG_OBX, &ENG_WAVE, &ENG_ACID, &ENG_DRIFT}` and `ENGINE_ORDER = {0,1,2,3}`. Make `eng_ok(e)` return `e < NENGINES`. Delete `snd_missing` (return 0) and the SLICE/BEAT patterns. Set `TRK_DEF = {{ENGI_OBX,0,0},{ENGI_WAVE,0,0},{ENGI_ACID,0,1},{ENGI_DRIFT,0,0}}` (ACID gets pattern 1 = ACID). Size `ENG_MEM_PART` from the four engines:
```c
#define ENG_MEM_PART (VOID_MEM > ACID_MEM ? (VOID_MEM > DRIFT_MEM ? VOID_MEM : DRIFT_MEM) \
                                          : (ACID_MEM > DRIFT_MEM ? ACID_MEM : DRIFT_MEM))
```
  - `eng_obx.c`: rename `ENG_ANALOG` to `ENG_OBX`, `.name = "OB-Z"`, `.knob = {P_E4, P_E5, P_ED_FLT, P_E1}`.
  - `eng_wave.c`: rename `ENG_VOID` to `ENG_WAVE`, `.name = "WAVE"`, `.knob = {P_E4, P_E5, P_ED_FLT, P_E1}`.
  - `eng_acid.c` (placeholder until Phase 5):
```c
/* eng_acid.c: ACID, the 303-style mono bass (placeholder: OBX's render until Phase 5) */
#define ACID_MEM 0u
static const engine_t ENG_ACID = {
    .name = "ACID", .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0}, {"TUNE", F_BIPCT, -64, 63, 0, 0, 0},
        {"DECY", F_PCT, 0, 127, 60, 0, 0},          {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"SQL", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = OBX_PRESETS, .npresets = NELEM(OBX_PRESETS),   /* OBX_PRESETS = the renamed ANALOG_PRESETS until Task 1.3 */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E2}, .poly = 1, .keep = 0x03,
    .note_on = 0, .render = analog_render_f,
};
```
    `N_ACID_WAVE` is `static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};`. Use `analog_render_f` only if its signature matches `render`; otherwise use the dispatcher `ENG_OBX.render` points at. `ENG_DRIFT` is the same, with edit = `OSC MIX, DTN, DRFT, VAR, CUT, RES, DRV, KTR`, `.knob = {P_E4, P_E5, P_ED_FLT, P_E6}`, `.poly = 2`, and `#define DRIFT_MEM 0u`.

- [ ] **Step 4: Delete the other engines' sources.** Use `git rm` on `eng_phase.c eng_lofi.c eng_sample.c eng_formant.c eng_trio.c eng_wheel.c eng_grain.c eng_phys.c phys_dsp.c phys_symp.c eng_drum.c drum_voice.c eng_noise.c eng_fm6.c fm6_core.c fm6_vbank.c up_fm6.c editor_fm6.c ui_fm6op.c eng_digital.c fm4_convert.c eng_slice.c slice_store.c ui_slice.c`. Then fix every compile error by deleting the dead branch, never by stubbing a whole engine back in. Call sites, from the survey:
  - `ui.c` 417-443, 1275-1484, 1760, 1842
  - `ui_graph.c` 15, 101, 263-284, 632, 689, 905, 1232-1266, 2246, 2332-2350
  - `ui_input.c` 295, 367, 531, 898, 990, 1003, 1089, 1483, 1730
  - `ui_draw.c` 875, 1061, 1162-1184
  - `ui_layer.c` 77-79, 95, 257, 545-616, 674, 863, 1092-1111, 1204, 1251
  - `ui_tools.c` 28, 371, 555, 613
  - `seq.c` 78, 96, 491-513, 972, 1055, 1083: `drum_track()` becomes `return 0` and then is deleted together with its callers
  - `voice.c` 188, 611
  - `midi_control.c` 41, 148, 219-226
  - `project.c` 272-369, 776, 1059, 1147-1250
  - `upreset.c` 79-541 (FM6 and PHYS paths)
  - `params.c` 74-79 (`N_ENGNAME = {"OB-Z","WAVE","ACID","DRIFT"}`), 151-154, 252-275, and the PAGES LANES/SLICES/OPERATOR/OP* rows
  - `editor.c` 15, 60-120, 353, 390-444, 501, 565
  - `editor_backup.c` 15-193 (the SMP and FM6 parts)
  - `storage.c` 29-33 (drop `OBJ_FM6BANK`/`OBJ_UPFM6`), `main.c` 106, `icons.c` 205-251
  - `favorites.c`, `settings_persist.c` 79-95, `category.c` (rewritten in Task 1.3)
  - `step_t.hit` and `step_t.acc` stay in the struct (the format is rewritten in 1.2); `NLANE` and `P_LN0..7` go.

- [ ] **Step 5: Delete the generators and assets.** `git rm tools/gen_fm6_patches.py tools/gen_samples.py tools/fetch_cc0.py tools/sampleio.py tools/gen_waves.py tools/fm1_sample_upload.py -r assets/samples-cc0`. Remove their entries from `tools/build.py:104-105` and the ATTRIBUTION copy at `:373`.

- [ ] **Step 6: Delete the engine tests.** `git rm tests/fm6_test.c tests/fm6bank_test.c tests/fm4_test.c tests/digital_test.c tests/fm4_div0_test.c tests/slice_test.c tests/slice_loop.py tests/fuzz_smp.c tests/sample_test.c tests/phys_test.c tests/phys_fixed.c tests/phys_ref.cpp tests/noise_test.c tests/drum_test.c tests/drumfuzz_test.c tests/fold155_test.c tests/sound14_test.c`, and remove their lines from `run_tests.sh`. In the tests that stay (`ui_test`, `ui_render`, `ratchet_test`, `detail_test`, `seq155_test`, `step15_test`, `union_test`, `project_test`, `upreset_test`, `midi_control_test`, `robust_test`, `descdump`), delete the cases that use removed engines, and change any ANALOG reference to `ENGI_OBX`. `union_test` keeps only OBX↔WAVE↔ACID↔DRIFT switches.

- [ ] **Step 7: Build and test**
```bash
./build.sh 2>&1 | tail -2 && GOLDEN_UPDATE=1 sh tests/run_tests.sh >/dev/null; sh tests/run_tests.sh 2>&1 | grep -E "FAIL|four_test"
git diff --stat tests/golden.txt   # only deletions, plus renamed ANALOG→OBX / VOID→WAVE lines with the SAME hashes
```
Expected: `four_test: ok`; no FAIL lines beyond `docs/BASELINE.md`; the image is at least 150 KiB smaller (record the number).

- [ ] **Step 8: Commit** `feat!: strip to four machine slots (OBX, WAVE, ACID, DRIFT)`

### Task 1.2: New storage format and identity

**Files:** `firmware/src/project.c:97-136`, `settings_persist.c` (`PERSIST_MAGIC`), `upreset.c:55-62`, `felucca.c:31-36`, `usb.c:354-357`, `ui_draw.c:1255`, `tools/build.py:52`, `tools/fm1_install.py` (`PORT_RE`, `LOADER_MARK` accepted as-is)

**Interfaces:**
```c
#define PROJ_MAGIC 0x34524F46u      /* "FOR4" */
#define PERSIST_MAGIC 0x34524F46u   /* settings: "FOR4" too, separate object */
#define UP_VER 0x40u                /* up_rec_t.ver for FOUR records */
#define FELUCCA_ID "FM-1_940"
#define FELUCCA_VERSION "v0.1"
```

- [ ] **Step 1: Failing tests in `tests/project_test.c`**
```c
static void test_old_felucca_project_ignored(void)
{
    static uint8_t buf[PROJ_STORE_SIZE];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x41; buf[1] = 0x4E; buf[2] = 0x55; buf[3] = 0x46;          /* "FUNA" little-endian */
    CHECK(proj_unpack(buf, sizeof buf) != 0, "a Felucca FUN10 project must be refused");
}
static void test_roundtrip_four(void)
{
    static uint8_t buf[PROJ_STORE_SIZE];
    uint32_t i;
    for (i = 0; i < NTRK; i++) { trk[i].eng_req = (uint8_t)i; trk[i].preset = (uint8_t)(31 - i); }
    proj_pack(buf);
    for (i = 0; i < NTRK; i++) { trk[i].eng_req = 0; trk[i].preset = 0; }
    CHECK(proj_unpack(buf, sizeof buf) == 0, "unpack");
    for (i = 0; i < NTRK; i++)
        CHECK(trk[i].eng_req == i && trk[i].preset == 31 - i, "track %u", (unsigned)i);
}
```
Use the file's actual pack/unpack names and return convention; `proj_pack` is at project.c:620, the unpacker next to it.

- [ ] **Step 2: Run** → FAIL (FUNA accepted).
- [ ] **Step 3: Implement**
  - Set the magics.
  - Delete the V1..V9 magics and every `*_migrate`/`legacy` path in `project.c` (272-369), `upreset.c` (`up_migrate`, `phys_legacy`) and `settings_persist.c` (PER1..3 import, 79-95).
  - `up_valid` requires `ver == UP_VER && engine < NENGINES`.
  - Identity: the defines above. USB product string `"VOID FOUR"`, splash `{"VOID FOUR", FELUCCA_VERSION, "Void Audio", "on Felucca by Hügelton"}`, `build.py` `PRODUCT = "FM-1_940"` with `--release X.Y` giving `"FM-1_94"+X+Y`.
  - Keep "Felucca" and Hügelton credits in README/CREDITS (GPL attribution).
- [ ] **Step 4: Run** `sh tests/run_tests.sh 2>&1 | grep -E "FAIL|project_test|persistence|upreset"`. Expected: ok.
- [ ] **Step 5: Commit** `feat!: FOUR storage format and firmware identity`

### Task 1.3: Categories and the 32-preset quota

**Files:** `firmware/src/category.c` (rewrite), `firmware/src/favorites.c` (rows 0..3, row 14 = MIDI LEARN kept, row 15 = prefs kept, `FAV_VOID_BIT` deleted), `tools/four_presets.py` (new), `presets/four/{obx,wave,acid,drift}.py` (new, preset source), `build/gen/four_presets.h` (generated)

**Interfaces:**
```c
enum { CAT_NONE, CAT_BASS, CAT_LEAD, CAT_KEYS, CAT_BRASS, CAT_STRINGS, CAT_ATMOS, CAT_ACID, NCAT };
static const char *const CAT_NAME[NCAT] = {"-", "BASS", "LEAD", "KEYS", "BRASS", "STRINGS", "ATMOS", "ACID"};
/* four_presets.h (generated): per machine m in OBX WAVE ACID DRIFT */
static const preset_t   <M>_PRESETS[32];
static const uint8_t    <M>_CAT[32];          /* CAT_* */
static const void_px_t  WAVE_PX[32];          /* WAVE extras (from void_presets.py) */
static uint32_t preset_cat(uint32_t e, uint32_t k);   /* category.c */
```

Preset source format, one Python dict per preset (`presets/four/obx.py`):
```python
PRESETS = [
  dict(name="ROYAL HORNS", cat="BRASS", e=[0, 12, 64, 0, 78, 34, 10, 64],   # P_E0..E7
       env=[18, 70, 96, 50], fenv=28, mono=0, fx=(0, 24, 20, 40), pat=0),
  # ... exactly 32
]
```

- [ ] **Step 1: Failing test** (append to `tests/four_test.c`, call it from `main`)
```c
static void test_presets(void)
{
    uint32_t e, k;
    for (e = 0; e < NENGINES; e++) {
        CHECK(ENGINES[e]->npresets == 32u, "%s has %u presets", ENGINES[e]->name, ENGINES[e]->npresets);
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            uint32_t c = preset_cat(e, k);
            CHECK(c >= CAT_BASS && c < NCAT, "%s/%u no category", ENGINES[e]->name, (unsigned)k);
            CHECK((c == CAT_ACID) == (e == ENGI_ACID), "%s/%s: ACID only on ACID",
                  ENGINES[e]->name, ENGINES[e]->presets[k].name);
            CHECK(strlen(ENGINES[e]->presets[k].name) <= 12u, "name too long");
        }
    }
}
```
- [ ] **Step 2: Write `tools/four_presets.py`.** It loads each `presets/four/<m>.py` and checks:
  - `len == 32`
  - every `cat` is in the allowed set for that machine (contract table above), with the per-category counts from that table
  - names are unique, ≤ 12 characters, and every character is in the new `AF_L` glyph set
  - every `e[i]` is inside the engine's `edit[i]` min..max (the generator embeds a copy of those ranges per machine)

  It then writes `build/gen/four_presets.h`, adds itself to `tools/build.py` next to `gen_void_tables.py`, and fails the build on any violation. For WAVE, it reuses the conversion code in `tools/void_presets.py` (import it) so that `WAVE_PX` is produced the same way.
- [ ] **Step 3: Seed placeholder sets** so the build is green: 32 entries per machine, using the machine's default `edit` values, named `<CAT> 01..`, with the required category counts. Phase 7 replaces them.
- [ ] **Step 4: Run** four_test → ok. Then `ui_test`'s PRESET_CAT length check is deleted (the generator enforces it now).
- [ ] **Step 5: Commit** `feat: 4x32 preset slots with BASS/LEAD/KEYS/BRASS/STRINGS/ATMOS/ACID categories`

---

## Phase 2: MIDI per track, CC map, MIDI out

### Task 2.1: The fixed CC map from the machine contract

**Files:** `firmware/src/midi_control.c:210-227`, `tests/midi_control_test.c:616`

**Interfaces:**
```c
#define CC_VOL 7u
#define CC_CUT 74u
#define CC_RES 71u
#define CC_ENV 79u
#define CC_MAC 70u
static const uint8_t CC_KNOB[4] = {CC_CUT, CC_RES, CC_ENV, CC_MAC};   /* EDIT K1..K4 */
static int32_t cc_param(const track_t *t, uint32_t cc);   /* param id, or -1 */
static int32_t param_cc(const track_t *t, uint32_t id);   /* CC number, or -1 (used by MIDI out) */
```

- [ ] **Step 1: Failing test** (replaces the `MIDI_CC_ENG` loop at midi_control_test.c:616)
```c
static void test_cc_contract(void)
{
    uint32_t e, k;
    for (e = 0; e < NENGINES; e++) {
        track_t *t = &trk[e];
        t->engine = t->eng_req = (uint8_t)e;
        for (k = 0; k < 4; k++) {
            CHECK(cc_param(t, CC_KNOB[k]) == ENGINES[e]->knob[k], "%s CC%u", ENGINES[e]->name, CC_KNOB[k]);
            CHECK(param_cc(t, ENGINES[e]->knob[k]) == CC_KNOB[k], "%s K%u out", ENGINES[e]->name, (unsigned)k + 1u);
        }
        CHECK(cc_param(t, CC_VOL) == P_LEVEL, "CC7");
        /* end to end: CC74 = 127 on channel e+1 puts CUT at its max */
        midi_event(0xB0u | e, CC_CUT, 127);
        CHECK(t->p[P_E4] == track_desc(t, P_E4)->max, "%s CC74 127", ENGINES[e]->name);
    }
}
```
- [ ] **Step 2: Run** → compile FAIL (`cc_param` undefined).
- [ ] **Step 3: Implement.**
  - Delete `MIDI_CC_ENG`, `MCC_CUT` and `MCC_RES`.
  - `cc_param`:
    - returns `ENGINES[t->engine]->knob[k]` when `cc == CC_KNOB[k]`
    - returns `P_LEVEL` for CC7
    - otherwise falls back to the remaining rows of `MIDI_CC_MAP` (5 GLIDE, 10 PAN, 72 REL, 73 ATK, 75 DEC, 91 REV, 93 CHOR, 94 DLY)
  - `param_cc` is the inverse. `midi_cc_map` calls `cc_param`, and learned CCs still win (`ml_*` unchanged).
- [ ] **Step 4: Run** → ok.
- [ ] **Step 5: Commit** `feat(midi): fixed CC map follows the 4-control contract`

### Task 2.2: MIDI OUT mode per track, sequencer and knob output

**Files:**
- `firmware/src/core.h` (new common param `P_MOUT` just before `P_E0`, so `P_COUNT` 111 → 112)
- `params.c` (`[P_MOUT] = PE("MOUT", N_MOUT, 0)` with `N_MOUT = {"INT","EXT","BOTH"}`)
- `voice.c:363,410` (`trk_note_on/off`)
- `seq.c:60-75` (route), `ui_input.c` (knob turns), `ui.c` (preset change)
- `midi_clock.c`, `usb.c:194`

**Interfaces:**
```c
enum { MOUT_INT, MOUT_EXT, MOUT_BOTH };
static void mout_note(track_t *t, uint32_t note, uint32_t vel);  /* vel 0 = note off; sends if MOUT != INT */
static void mout_param(track_t *t, uint32_t id);                 /* sends param_cc(t,id) scaled 0..127 if mapped */
static void mout_program(track_t *t);                           /* Program Change = t->preset */
static void mout_clock(uint32_t rt);                            /* 0xF8 / 0xFA / 0xFC, when G_CLKOUT is on */
```

Rules:
- Output goes out from `trk_note_on/off` (one place). Every source of notes passes through it: sequencer, arp, keys, chords, MIDI thru. `key_on`/`key_off` (seq.c:710-737) stop sending directly, so nothing is sent twice.
- Notes received over MIDI are **not** echoed back out on the same port. A `from_midi` flag is set in `midi_note_event` around the call.
- With EXT, `trk_note_on` sends the note and returns before allocating a voice.
- The CC value out is `(v - d->min) * 127 / (d->max - d->min)`, the inverse of `midi_cc_set`. Send only on change, at most once per control block per param.
- `G_CLKOUT` (MENU > MIDI > CLOCK OUT, default ON) sends F8 at 24 ppqn from the sequencer tick, FA on play and FC on stop.
- The default route stays `G_ROUTE = CH1-4`. The MENU > MIDI page shows T1..T4 CHANNEL and T1..T4 OUT (MOUT) as two 4-knob pages.

- [ ] **Step 1: Failing tests** (`tests/midi_control_test.c`; the test harness reads `midi_out_q`)
```c
static uint32_t out_n(void) { return mo_w - mo_r; }
static uint32_t out_pop(void) { return midi_out_q[mo_r++ % MQ]; }

static void test_mout(void)
{
    track_t *t = &trk[2];
    usb.config = 1; mo_r = mo_w;
    t->p[P_MOUT] = MOUT_INT;  trk_note_on(t, 48, 100);  trk_note_off(t, 48);
    CHECK(out_n() == 0, "INT sends nothing");
    t->p[P_MOUT] = MOUT_EXT;  trk_note_on(t, 48, 100);
    CHECK(out_n() == 1 && out_pop() == (0x09u | 0x92u << 8 | 48u << 16 | 100u << 24), "EXT note on ch3");
    CHECK(trk_voices_sounding(t) == 0, "EXT is silent inside");
    trk_note_off(t, 48);
    CHECK(out_n() == 1 && (out_pop() & 0xF0FF00u) == (0x80u << 8 | 48u << 16 & 0xF0FF00u), "EXT note off");
    t->p[P_E4] = track_desc(t, P_E4)->max;  mout_param(t, P_E4);
    CHECK(out_n() == 1 && out_pop() == (0x0Bu | 0xB2u << 8 | 74u << 16 | 127u << 24), "CUT -> CC74 127");
    midi_event(0x92, 48, 100);              /* MIDI in on track 3's channel */
    CHECK(out_n() == 0, "no echo of MIDI in");
}
```
`trk_voices_sounding` is a test helper: it counts voices whose `trk == t` and whose `env` stage is not idle. Write it in the test file from `voice.c`'s voice table.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** the rules above. Add `P_MOUT` to the project pack, which is just one more param byte, because `P_COUNT` is stored in the header at byte 66.
- [ ] **Step 4: Run** midi_control_test, project_test, regress → ok. `golden.txt` is unchanged, because MOUT_INT is the default.
- [ ] **Step 5: Write `docs/MIDI.md`**: the CC table above, channels, MOUT modes, clock out, Program Change, MIDI LEARN (unchanged), and a 3-step "drive an external synth from track N" recipe.
- [ ] **Step 6: Commit** `feat(midi): per-track MIDI OUT (INT/EXT/BOTH), seq+knob+PC+clock out`

---

## Phase 3: The big-type UI

### Task 3.1: Fonts: full-alphabet L and a 40 px digit face

**Files:** `tools/gen_aa_font.py:48-62`, `tests/text_spacing_test.py`, `tests/ui_test.c` (`test_large_face`)

- [ ] **Step 1:** Change `L_CHARS` to `" #%+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"`. Add `("XL", v + "@600", 40, (32, 32), [ord(c) for c in "+-.0123456789"], 0.0, PHASES_L)`.
- [ ] **Step 2:** `test_large_face`: assert that every machine name, `CAT_NAME`, every factory preset name and every EDIT label ("CUTOFF", "RESONANCE", "ENV", "DETUNE", "POSITION", "DECAY", "DRIVE", "TRACK", "PRESET", "VOLUME", "TEMPO") has full `large_face_has()` coverage. Add an `xl_face_has()` test for the formatted values -64..127.
- [ ] **Step 3:** `./build.sh && python3 tests/text_spacing_test.py build/gen/ui_fonts.h`. Expected: L and XL ≤ 0.5 px. Record the flash delta (it should be under 12 KiB).
- [ ] **Step 4: Commit** `feat(ui): full A-Z large face and 40px value digits`

### Task 3.2: EDIT screen with four big controls

**Files:**
- Create: `firmware/src/ui_big.c` (included from `ui.c` where `ui_slice.c` used to be)
- Modify: `params.c` PAGES (new first EDIT page `{"EDIT", FAM_EDIT, SC_ENGINE, GR_BIG, {0xFF...}}`, with "EDIT 1/2" renamed "MACHINE 1/2"), `ui_draw.c:1216-1239` (dispatch `GR_BIG`), `ui_input.c:1044-1052` (knobs on the EDIT page), `tests/ui_render.c` (scenes `edit_big_obx/_wave/_acid/_drift`)

**Interfaces:**
```c
static uint32_t big_id(const track_t *t, uint32_t k);          /* ENGINES[t->engine]->knob[k] */
static const char *big_label(const track_t *t, uint32_t k);    /* "CUTOFF" "RESONANCE" "ENV" + machine label */
static void draw_big_card(uint32_t k, uint32_t x, uint32_t y); /* 118x94 card into the current strip */
static void draw_edit_big(void);                               /* two 240x94 strips at y 24 and 118 */
```
The machine labels are "DETUNE", "POSITION", "DECAY" and "DRIVE". They are spelled out here; the 4-character `edit[].label` stays for the deeper pages.

Card anatomy (each element is declared with `GFX_HOOK_ALIGN` so `ui_render` checks it):
- `cv_rrect(0,0,116,92,6,T_SURF,T_BG)`
- label `AF_M` at (8,6)
- K-pill `AF_S` right-aligned at (108,6)
- 270° ring `knob(58, 54, 30, ...)` with the value in `AF_XL` centred at y 38..78, falling back to `AF_L`
- the bipolar ENV ring draws from 12 o'clock
- the hot colour `T_ACCENT` holds for 0.6 s after a turn (the existing `ui.hot_col`)

- [ ] **Step 1: Failing UI test** in `tests/ui_test.c`:
```c
static void test_edit_big(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++) {
        reset_ui(); track_select(e); press(B_EDIT); frame();
        CHECK(strcmp(PAGES[ui.page].title, "EDIT") == 0, "EDIT opens the big page");
        int16_t cut0 = TSEL->p[P_E4], k4 = TSEL->p[ENGINES[e]->knob[3]];
        turn(EN_K1, 5); frame();
        CHECK(TSEL->p[P_E4] > cut0, "%s K1 turns CUT", ENGINES[e]->name);
        turn(EN_K4, 5); frame();
        CHECK(TSEL->p[ENGINES[e]->knob[3]] != k4, "%s K4 turns its machine control", ENGINES[e]->name);
        CHECK(screen_has_text("CUTOFF") && screen_has_text("RESONANCE") && screen_has_text("ENV"), "labels");
    }
}
```
Use `ui_test.c`'s existing helpers. If `screen_has_text` doesn't exist, check the draw call log the way `test_large_face` does.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** `ui_big.c`. Knob handling on `GR_BIG` is `edit_param` with `page_desc` resolving slot k to `big_id`, so motion capture, MIDI LEARN (`ml_knob`) and `mout_param` all work without special cases.
- [ ] **Step 4: Run** `ui_test` and `ui_render` (all palettes, plus LARGE and LINE). Expected: 0 findings. Look at `build/ui_new/GREY/edit_big_*.png` (use the Read tool on the PNG) and compare against the mockup above.
- [ ] **Step 5: Commit** `feat(ui): EDIT = four big controls (CUT RES ENV + machine)`

### Task 3.3: HOME screen with track, preset, volume and tempo

**Files:** `ui_big.c` (`draw_home_big`), `ui.c:1736-1743` (`home_param`), `ui_draw.c:887-898`, `ui_input.c` (HOME knob branch), `ui.c:84-100` (HOME views: SCOPE/TRACKS/LEVELS are replaced by the one big HOME; the MENU > DISPLAY > HOME row is deleted), `tests/ui_render.c` (`home_big`, `home_big_t3`)

**Interfaces:**
```c
enum { HK_TRACK, HK_PRESET, HK_VOLUME, HK_TEMPO };
static void home_turn(uint32_t k, int32_t s);  /* K1 track_select, K2 eng_list_step (this machine's 32 + user),
                                                  K3 TSEL->p[P_LEVEL] (+mout_param), K4 song.g[G_BPM] */
static void draw_home_big(void);
```
- K2 steps only the current machine's presets plus user presets of the same machine (`eng_list_step`, already in `ui.c`). PRESETS does the same.
- Changing machine is in the EDIT layer (hold EDIT, white keys 1..4), which is rare; the default project has one machine per track.
- A preset change sends `mout_program`.

- [ ] **Step 1: Failing test:**
```c
static void test_home_big(void)
{
    reset_ui(); go_home(); frame();
    turn(EN_K1, 2); frame();  CHECK(song.sel == 2, "K1 selects track 3");
    uint8_t p0 = TSEL->preset;
    turn(EN_K2, 1); frame();  CHECK(TSEL->preset == (p0 + 1) % 32 && TSEL->engine == ENGI_ACID, "K2 next ACID preset");
    int16_t l0 = TSEL->p[P_LEVEL];
    turn(EN_K3, -3); frame(); CHECK(TSEL->p[P_LEVEL] < l0, "K3 volume");
    int16_t b0 = song.g[G_BPM];
    turn(EN_K4, 1); frame();  CHECK(song.g[G_BPM] == b0 + 1, "K4 tempo");
    turn(EN_ALGO, 1); frame(); CHECK(song.sel == 3, "ALGORITHM still selects track");
}
```
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement** to the HOME mockup. Draw strips: header, tabs 240×44, preset panel 240×82, cards 240×64, footer. **Step 4:** `ui_test` and `ui_render` show 0 findings; check the PNGs by eye.
- [ ] **Step 5: Commit** `feat(ui): HOME = track tabs, big preset name, TRACK PRESET VOLUME TEMPO`

### Task 3.4: Prune the menus and pages for four machines

**Files:** `menu_items.c`, `ui_menu.c`, `ui_layer.c`, `ui_help.c`, `params.c` PAGES

- [ ] Remove every row and page that no longer has a target: LANES, SLICES, OPERATOR*, OP*, FLT ENV for non-WAVE (keep it for WAVE), the sample upload, the FM6 bank, the HOME view selector, and the DRUM kit.
- [ ] Every remaining page must have ≤ 4 ids (it already does, since `page_t.id[4]`). Add a `ui_test` assertion that loops over `PAGES` and checks that no visible page has an id ≥ `P_COUNT` other than 0xFF.
- [ ] Run `ui_render` (0 findings). Then use the web emulator if `emcc` is available (`brew install emscripten`; ask before installing) and click through every page.
- [ ] **Commit** `refactor(ui): prune pages and menus to the four-machine set`

---

## Phase 4: OBX (extend eng_obx.c)

> Phases 4–6 are DSP. Each task's code is written during execution against the donor sources named here. Each task is checked by **measurable tests**, not by reading the code: spectra, levels, CPU counts and golden hashes. Before starting a phase, write a short sub-plan under `docs/plans/` if the donor port is bigger than ~300 lines.

### Task 4.1: SEM-style filter morph, 4-pole option and slop

**Files:** `firmware/src/eng_obx.c`, `tests/obx_test.c` (new; built like `void_test`)

**Machine params (edit[8]):**

| Slot | Param |
|---|---|
| E0 | WAVE (SAW SQR TRI PWM SYNC SUB, as ANALOG) |
| E1 | **DTN** (K4) |
| E2 | MIX |
| E3 | PW |
| E4 | CUT |
| E5 | RES |
| E6 | MODE (LP→NOTCH→HP→BP morph 0..127, with 4P above 120) |
| E7 | SLOP |

- [ ] **Step 1: Failing tests** in `tests/obx_test.c`:
  - MODE 0: render a 100 Hz saw for 1 s with CUT at 1 kHz → energy at 4 kHz is at least 20 dB below 500 Hz.
  - MODE 64 (HP-ish): the inverse.
  - MODE 32 (notch): the bin at cutoff is at least 15 dB below its neighbours.
  - SLOP 0 renders identically twice. SLOP 127: per-voice pitch spread ≤ ±6 cents, and two voices on the same note differ.
  - DTN 0 → 1 changes the render hash; DTN 127 gives two peaks 20–40 cents apart.

  Use `hostsim.c`'s offline render and its FFT helper. If it has none, add `static void fft_mag(const int32_t *x, uint32_t n, float *mag)` to the test file; float is fine on the host.
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. Base it on the existing `tsvf_coef` SVF in `eng_obx.c`: the LP/BP/HP outputs are already computed, so the morph is a crossfade of `lp`, `lp+hp` (notch), `hp` and `bp`. The 4P mode cascades the second SVF already used by `analog_render_x`. SLOP comes from `voice_t.s[]`, seeded per voice from the note-on counter, as ±slop×6 cents pitch and ±slop×2 semitones cutoff. **Step 4:** obx_test, regress (golden hashes for OBX change: regenerate and note why in the commit), cpu_baseline (OBX ≤ 1.3× the old ANALOG figure).
- [ ] **Step 5: Commit** `feat(obx): SEM morph filter, 4-pole, slop`

---

## Phase 5: ACID (new, Open303 port)

### Task 5.1: Fixed-point 303 voice

**Files:** `firmware/src/eng_acid.c` (replace the placeholder), `tools/gen_engine_tables.py` (or extend `gen_tables.py`: 303 resonance curve, exp table), `LICENSES/Open303-MIT.txt`, `tests/acid_test.c`, `tests/acid_ref.cpp` (builds Open303's float `rosic_TeeBeeFilter` on the host as the reference, the way `tools/void_ref.cpp` does for WAVE)

**Machine params:**

| Slot | Param |
|---|---|
| E0 | WAVE (SAW/SQR) |
| E1 | TUNE |
| E2 | **DECY** (K4: the filter env decay 200 ms–2.5 s) |
| E3 | ACC (accent amount) |
| E4 | CUT |
| E5 | RES |
| E6 | DRV (post-filter tanh) |
| E7 | SQL (squelch: a resonance boost tied to accent) |

ENV (K3) is `P_ED_FLT` used as the 303's ENV MOD. `ownenv = 1`: the engine runs its own MEG (exp decay) and VEG; the track ADSR is ignored except for release. Accent comes from step velocity > 110 (seq.c:920 sets 127). Slide comes from `t->slide_glide` (seq.c:977) with a fixed 60 ms ramp, as on the 303. `poly = 1`, `mono` forced. The filter is 2× oversampled with a half-band up/down pair (`dsp_va.c`).

- [ ] **Step 1: Failing tests:**
  - (a) A filter impulse response at CUT/RES on a 5×5 grid is within 1 dB of `acid_ref` (as `void_test` does).
  - (b) RES 127 self-oscillates within ±3% of the expected frequency.
  - (c) An accented step peaks at least 3 dB louder, with a higher spectral centroid than the same step unaccented.
  - (d) Over a slide step pair 48→60, the pitch moves monotonically over 50–70 ms and the envelope does not retrigger (no amplitude dip > 1 dB).
  - (e) DECY 0 vs 127 changes the filter env time constant at least 5×.
  - (f) CPU ≤ 3× ANALOG per voice (`cpu_baseline`).
- [ ] **Step 2:** FAIL. **Step 3:** Port `rosic_TeeBeeFilter`, `rosic_BlendOscillator` and the accent/slide logic from `rosic_Open303.cpp` to Q15/Q31. Use `TANH_Q15` and the new tables. Keep Robin Schmidt's MIT notice at the top. Check `fm1-x0x` (GPL-3) for how it budgets CPU on this chip. **Step 4:** All pass. Add ACID to `tests/target_budget.py` `FUNCS`.
- [ ] **Step 5: Commit** `feat(acid): fixed-point 303 voice with accent and slide`

### Task 5.2: Sequencer ACCENT/SLIDE editing is reachable on the big UI

Accent, slide and tie already exist in `step_t` (SF_ACCENT, SF_SLIDE, ST_TIE). This task only makes sure they can be edited on the SEQ STEP page in ≤ 4 controls and that the ACID pattern (pattern 1) uses them.

- [ ] Failing `ui_test`: on track 3, SEQ → step 1 → set ACCENT and SLIDE with the step page knobs → `step[0].flags == (SF_ACCENT|SF_SLIDE)`; play → `trk[2].slide_glide` is set on step 2.
- [ ] Implement only if it fails (the DETAIL page may already cover it). **Commit** `test(seq): accent/slide editable on ACID`

---

## Phase 6: DRIFT (new, Diva-like)

### Task 6.1: Shared VA DSP and DRIFT voice

**Files:** `firmware/src/dsp_va.c` (new: nonlinear one-pole, ZDF ladder solver, 2× half-band; ACID uses the half-band too), `firmware/src/eng_drift.c`, `LICENSES/DaisySP-MIT.txt` (check that it's already present), `tests/drift_test.c`

**Machine params:**

| Slot | Param |
|---|---|
| E0 | OSC MIX (saw↔pulse, with a sub under 32) |
| E1 | DTN |
| E2 | DRFT (oscillator drift) |
| E3 | VAR (per-voice component variance) |
| E4 | CUT |
| E5 | RES |
| E6 | **DRV** (K4: ladder input drive) |
| E7 | KTR |

`poly = 2`. The filter is a 4-stage ZDF ladder with `TANH_Q15` per stage, solved with one Newton step per sample, 2× oversampled. If CPU fails, fall back in this order: (1) no oversampling, (2) a cascade of 2 SVFs with tanh between stages (the risk register's fallback).

- [ ] **Step 1: Failing tests:**
  - (a) A 24 dB/oct slope: magnitude at 4× cutoff is at least 20 dB below the passband.
  - (b) DRV 0→127 raises the THD of a 200 Hz sine through an open filter from < 1% to > 10%.
  - (c) Self-oscillation at RES 127.
  - (d) DRFT 0 gives a stable pitch (two renders identical). DRFT 127 gives a slow wander ≤ ±8 cents.
  - (e) VAR gives a different cutoff per voice.
  - (f) CPU per voice is recorded in `cpu_baseline`, and 2 voices of DRIFT + 8 of OBX + 1 of ACID + 6 of WAVE ≤ the host model of the 80% target (`target_budget.py`).
- [ ] **Step 2–4:** Fail, implement, pass. **Step 5: Commit** `feat(drift): ZDF nonlinear ladder voice`

---

## Phase 7: The 128 presets

### Task 7.1: Audition renderer

**Files:** `tests/audition.c` (new, built on `hostsim.c`), `tools/audition_index.py` (new)

- [ ] `build/host/audition build/audition` renders every factory preset to a 6 s WAV: `build/audition/<MACHINE>/<nn>_<NAME>.wav`.
  - ACID presets play pattern 1 (the ACID pattern, with accent and slide).
  - BASS and LEAD presets play a 2-bar phrase.
  - KEYS, BRASS, STRINGS and ATMOS presets play a 4-note chord held 3 s and released.
- [ ] The renderer also checks each WAV for:
  - peak ≤ −1 dBFS
  - RMS between −24 and −10 dBFS (level-matched presets)
  - no DC > 1%
  - no silent output

  Any violation fails the run.
- [ ] `audition_index.py` writes `build/audition/index.html`: a table per machine with an `<audio>` player, name, category and a 1–5 rating select saved to localStorage, plus an "export ratings" button that downloads JSON.
- [ ] **Commit** `tools: preset audition renderer`

### Task 7.2: Write the 4 × 32 presets

**Files:** `presets/four/{obx,wave,acid,drift}.py`

Sound design briefs (all names ≤ 12 characters, A–Z, 0–9, space, `#+-./`):
- **OBX (32):**
  - BRASS ×8: stab, swell, horn section, synth-brass pad, filtered "Jump"-style, etc.
  - STRINGS ×8: ensemble with chorus, slow string pad, PWM strings, octave strings.
  - KEYS ×8: poly-synth keys, clav-ish pulse, bell-ish sync, EP-style tri.
  - LEAD ×8: sync lead, detuned saw lead, square lead with glide, 4-pole fat mono.
- **WAVE (32):**
  - ATMOS ×12: scanning pads (an LFO on POS), formant airs, spectral drones.
  - LEAD ×10: wavetable leads.
  - BASS ×10: Reese, growl, sub. Start from the 69 converted VOID WAVE presets (`docs/void_presets.md` lists their losses): keep the best that fit the categories, then fill the gaps.
- **ACID (32):**
  - CLASSIC ×8
  - SQUELCH ×8 (high RES/SQL)
  - DISTORTED ×8 (DRV plus the DIST send)
  - DEEP ×8 (low CUT, long DECY, square)

  All 32 are category ACID; the sub-styles are only naming.
- **DRIFT (32):**
  - BASS ×12: moog-style, plucked, sub, driven.
  - LEAD ×10: mono glide leads, screaming ladder.
  - KEYS ×10: soft analogue keys, tines-like with KTR.

Steps:
- [ ] Write each set, run `./build.sh` (the generator checks counts, categories and ranges), then run `audition`. Fix any loudness or peak failures.
- [ ] **User gate:** give the user `build/audition/index.html`. They rate the presets. Every preset rated ≤ 2 is redesigned. Repeat until the user signs off on each machine.
- [ ] Regenerate `golden.txt` for the presets only (`GOLDEN_UPDATE=1`), and commit with the user's sign-off noted: `feat(presets): 128 factory presets (OBX/WAVE/ACID/DRIFT x32)`

---

## Phase 8: Hardware

### Task 8.1: Build, size, install, measure

- [ ] `./build.sh --release 0.1`. Record the image size, RAM and pool headroom in `docs/BASELINE.md`.
- [ ] **Ask the user** before flashing. With the FM-1 connected and the user's go-ahead: `python3 tools/fm1_install.py build/felucca-0.1.fwsc`. Expected: the identity reads back `FM-1_940`.
- [ ] On the device, with the PERF meter (`song.cpu_q8`, console `perf`), play the default project (4 tracks running, each machine at its voice cap, chords on OBX and WAVE). Expected: ≤ 80%.
  - If over, reduce in this order: DRIFT oversampling, WAVE `VOID_POLY` 6→4, DRIFT voices 2→1.
  - Record the final voice caps in `four_test` assertions and in this plan.
- [ ] MIDI check with a DAW over USB:
  - Channels 1–4 each play their track.
  - CC 74/71/79/70 move K1–K4 on the right track, and turning K1–K4 sends them back.
  - EXT silences the track and its sequence goes out on its channel.
  - Clock out keeps the DAW in sync.
- [ ] Recovery: keep the FM-1 Transporter (XIAO RP2040) rig ready before the first flash.
- [ ] **Commit** the measured numbers: `chore: v0.1 hardware measurements`

## Phase 9: Release

- [ ] `README.md` rewrite (VOID FOUR, the 4 machines, controls, credits to Felucca/Hügelton and every donor), `docs/MIDI.md` (from 2.2), `CREDITS.md`, `LICENSES/`.
- [ ] Web editor (`web/`): update `descdump.c`/`test_web.mjs` to the 4 engines, delete the sample upload and FM6 pages, and add MOUT. Do this only if the user wants the web editor in v0.1 (open question 3).
- [ ] Update `CLAUDE.md` (the FM-1 section) and the memory note to point at `FM1-Four/` and this plan.

---

## Self-review against the brief

| Brief item | Where it's handled |
|---|---|
| Only 4 machines; other engines and presets removed | 1.1, 1.3 |
| 32 presets each | 1.3 (assert), 7.2 |
| Leads, basses, keys, brass and strings, atmos; acid on the 303 | contract table, 1.3 test, 7.2 |
| No drums or noises | 1.1 (DRUM/NOISE/SAMPLE deleted), category set has no DRUM/FX |
| Simplified HOME: select track 1–4, volume, preset | 3.3 |
| EDIT: Cutoff, Res, Env + a machine control | 3.2, D5 |
| Sequencer kept for each track | 1.1 keeps seq.c (drum grid removed), 5.2 |
| Tracks on MIDI channels 1–4 | 2.1/2.2 (default route CH1-4) |
| Clean route to mapping controls to external gear | 2.1 (one CC map in = out), 2.2 (MOUT EXT/BOTH, clock out), docs/MIDI.md |
| Big type, 4 controls per screen | 3.1 fonts, 3.2/3.3 layouts, 3.4 page check |

## Answers from the user (2026-10-16)

1. TRS MIDI jack stays input only; MIDI out over USB is enough.
2. The OB machine is shown as **OB-Z** (code ids stay `ENG_OBX` / `ENGI_OBX`; only the display name changes).
3. Web editor: later, not in v0.1 (Phase 9 web step deferred).
4. HOME K4 = TEMPO confirmed.
5. Execution: subagent-driven.

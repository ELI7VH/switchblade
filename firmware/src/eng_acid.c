/* SPDX-License-Identifier: GPL-3.0-only */
/* eng_acid.c: ACID, the 303-style mono bass (placeholder: OB-Z's render until Phase 5). The render reads the
 * EDIT values as OB-Z's (WAVE 0 / 1 = SAW / SQR); its state is the voice's s[] as OB-Z's: no engine memory */
#define ACID_MEM 0u
static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};
static const engine_t ENG_ACID = {
    .name = "ACID", .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0}, {"TUNE", F_BIPCT, -64, 63, 0, 0, 0},
        {"DECY", F_PCT, 0, 127, 60, 0, 0},          {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"SQL", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = OBX_PRESETS, .npresets = NELEM(OBX_PRESETS),   /* until Task 1.3 */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E2}, .poly = 1, .keep = 0x03,
    .note_on = analog_note_on, .render = analog_render_f,
};

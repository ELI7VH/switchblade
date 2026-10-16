/* SPDX-License-Identifier: GPL-3.0-only */
/* eng_drift.c: DRIFT, the Diva-like polysynth (placeholder: OB-Z's render until its phase). The render reads the
 * EDIT values as OB-Z's; its state is the voice's s[] as OB-Z's: no engine memory */
#define DRIFT_MEM 0u
static const engine_t ENG_DRIFT = {
    .name = "DRIFT", .page_title = {"OSC", "FLT"},
    .edit = {
        {"MIX", F_PCT, 0, 127, 64, 0, 0},           {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"DRFT", F_PCT, 0, 127, 20, 0, 0},          {"VAR", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = OBX_PRESETS, .npresets = NELEM(OBX_PRESETS),   /* until Task 1.3 */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E6}, .poly = 2, .keep = 0x03,
    .note_on = analog_note_on, .render = analog_render_f,
};

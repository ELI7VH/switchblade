/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real sequencer/project/UI sources: motion lifecycle and compact migration. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int motion_recording(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 23; song.sel = 0; song.rec = 1;
    seq_start(); seq_tick(t, CTL); /* first real step */
    t->p[P_REV] = 92;
    bad += check("motion records the selected armed track at its current step", !motion_capture(t, P_REV, 92) &&
        motion.count == 1u && motion.event[0].place == 0u && motion.event[0].value == 92);
    t->p[P_REV] = 110; motion_capture(t, P_REV, 110);
    bad += check("same step/parameter overwrites rather than consuming capacity", motion.count == 1u && motion.event[0].value == 110);
    bad += check("sounding values never replace the original patch base", t->p[P_REV] == 110 && motion_base_value(t, P_REV) == 23);
    project_t q; project_store_t packed;
    project_capture(&q);
    bad += check("project snapshot saves base + independent events while sounding", q.t[0].p[P_REV] == 23 && q.motion.event[0].value == 110 &&
        proj_pack(&packed, &q) && sizeof packed == PROJ_STORE_SIZE);
    seq_stop();
    bad += check("stop before another step restores the original parameter", t->p[P_REV] == 23);
    seq_start(); seq_tick(t, CTL);
    bad += check("recorded motion plays back at step zero", t->p[P_REV] == 110);
    motion_set_enabled(t, 0);
    bad += check("bypass restores base without deleting automation", t->p[P_REV] == 23 && motion_count(t) == 1u && !motion_enabled(t));
    motion_set_enabled(t, 1); motion_step(t, 0, &motion, 1);
    song.rec = 0; t->p[P_REV] = 37; motion_capture(t, P_REV, 37); seq_stop();
    bad += check("manual edits outside recording become the new base", t->p[P_REV] == 37);
    song.rec = 3; seq_start(); seq_tick(t, CTL);
    trk[1].p[P_REV] = 45; motion_capture(&trk[1], P_REV, 45);
    bad += check("only the selected track captures knob motion", motion_count(&trk[1]) == 0u);
    bad += check("transport and engine-switch parameters cannot be captured", !motion_param(P_SLEN) && !motion_param(P_AMODE) &&
        motion_set_event(t, 0, P_SLEN, 8) == 1);
    seq_stop(); motion_clear(t);
    t->p[P_REV] = 42; song.rec = 1; seq_start(); seq_tick(t, CTL);
    motion_clear(t); t->p[P_REV] = 70; motion_capture(t, P_REV, 70); seq_stop();
    bad += check("clearing then recording mid-play keeps the correct new base", t->p[P_REV] == 42);
    return bad;
}
static int motion_capacity(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t i = 0; i < MOTION_MAX; i++)          /* (128 since 1.2: two ids on each of the 64 steps) */
        bad += motion_set_event(&trk[0], i % NSTEP, i < NSTEP ? P_REV : P_DLY, (int16_t)(i % NSTEP));
    motion_store_t saved = motion;
    bad += check("full event pool refuses append without overwriting earlier events", motion_set_event(&trk[1], 0, P_REV, 90) == 2 &&
        !memcmp(&motion, &saved, sizeof saved));
    bad += check("full pool still permits a targeted overwrite", !motion_set_event(&trk[0], 12, P_REV, 100) && motion.count == MOTION_MAX);
    motion_delete_event(&trk[0], 12, P_REV);
    bad += check("deleting one event releases one slot", motion.count == MOTION_MAX - 1u && !motion_set_event(&trk[1], 0, P_REV, 90));
    saved = motion; motion_store_t invalid = motion; invalid.count = MOTION_MAX + 1;
    bad += check("invalid restore leaves the pool intact", motion_replace_track(&trk[0], &invalid) != 0 && !memcmp(&motion, &saved, sizeof saved));
    motion_clear(&trk[0]);
    bad += check("track clear keeps every other track's events", motion.count == 1u && motion.event[0].place >> STEP_BITS == 1u);
    return bad;
}
static int probability_playback(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    step_t s = {{60, 64, 0, 0}, 2, ST_NOTE, 0, 90, 0, 0, 0};
    bad += check("zero-initialized probability remains legacy 100 percent", step_chance(&s) == 100u);
    step_set_chance(&s, 0); seq_step(t, &s, 0, div_samples(2), 0);
    bad += check("zero percent suppresses the whole chord", step_chance(&s) == 0u && !t->seq_n);
    step_set_chance(&s, 100); seq_step(t, &s, 0, div_samples(2), 0);
    bad += check("100 percent plays all chord notes", t->seq_n == 2u);
    seq_release(t); step_set_chance(&s, 50); uint32_t heard = 0;
    for (uint32_t i = 0; i < 1000u; i++) { seq_step(t, &s, 0, div_samples(2), 0); heard += t->seq_n != 0; seq_release(t); }
    bad += check("chance is evaluated each repeat with one decision per step", heard > 350u && heard < 650u);
    return bad;
}
static int compact_project(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->step[3] = (step_t){{60, 67}, 2, ST_NOTE, SF_ACCENT, 110, 0x81, 0x80, 0};   /* (stale hit bits: written 0) */
    step_set_chance(&t->step[3], 25); t->p[P_FM1_LEVEL] = 80; t->p[P_ED_FLT] = -50;
    motion_set_event(t, 3, P_REV, 110);
    project_t before, after; project_store_t packed, corrupt;
    project_capture(&before);
    bad += check("FUN10 fits the retained and flash extent", sizeof(proj_slot) == 4u * PROJ_STORE_SIZE && proj_pack(&packed, &before));
    before.t[0].step[3].hit = before.t[0].step[3].acc = 0;
    before.sum = proj_sum(&before);
    bad += check("FUN10 round trip preserves signed values/FM params/probability/motion (a step's hit bits dropped)",
        proj_import(&after, &packed, sizeof packed) && !memcmp(&before, &after, sizeof before));
    corrupt = packed; corrupt.raw[112] ^= 1u;
    bad += check("FOR4 torn or corrupted payload is refused", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; corrupt.raw[68] = 255; uint32_t sum = proj_hash(corrupt.raw, sizeof corrupt - 4u);
    memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("compact parameter range is validated even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; uint32_t probability_byte = 68u + P_COUNT + 2u + 8u; corrupt.raw[probability_byte] = 127;
    sum = proj_hash(corrupt.raw, sizeof corrupt - 4u); memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("invalid probability is refused even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    project_save(1); motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    project_load(1);
    bad += check("actual project save/load restores motion, chance and operator settings", motion_count(t) == 1u &&
        step_chance(&t->step[3]) == 25u && t->p[P_FM1_LEVEL] == 80);
    return bad;
}
static int loads_and_song(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    int16_t v;
    uint32_t e0 = t->eng_req, e1 = e0 == ENGI_WAVE ? ENGI_OBX : ENGI_WAVE;
    t->p[P_REV] = 21; motion_set_event(t, 0, P_REV, 100); t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    motion_set_lock(t, 1, P_E0, motion_base_value(t, P_E0));
    apply_preset_to(t, 1);
    bad += check("1.1.5: a sound load of the same engine keeps the motion (both kinds, the engine's own E1 too), the pattern",
                 motion_count(t) == 2u && motion_lock_get(t, 1, P_E0, &v) && motion_enabled(t) && t->step[0].note[0] == 60);
    undo_swap();                                   /* (back to the sound before it: REV 21) */
    set_engine_of(t, e1);
    bad += check("  another engine: the motion on the common parameters (REV) stays, the engine's own (E1) goes, PLAY ON",
                 motion_count(t) == 1u && !motion_lock_get(t, 1, P_E0, &v) && motion_enabled(t) && t->step[0].note[0] == 60);
    undo_swap(); bad += check("sound undo restores the original motion pool and base", motion_count(t) == 2u && t->p[P_REV] == 21 &&
                              t->eng_req == e0);
    undo_swap(); bad += check("sound redo restores the loaded motion state", motion_count(t) == 1u && t->eng_req == e1);
    undo_swap(); project_save(0);
    t->p[P_REV] = 43; t->step[0].note[0] = 72; chain_config.count = 1; chain_config.row[0] = chain_row_of(0, 1);
    bad += check("song preparation imports saved motion alongside steps", chain_prepare() == 0 && chain.source[0].motion.count == 2u);
    seq_start(); seq_tick(t, CTL);
    bad += check("song plays saved automation with current instruments", chain.running && t->p[P_REV] == 100 && t->step[0].note[0] == 72);
    seq_stop(); bad += check("song stop restores current base and editable pattern", t->p[P_REV] == 43 && t->step[0].note[0] == 72);
    return bad;
}
/* WaveLoop: the ARP is SHAYPE (seq.c arp_fire over arp_shape.c; the core's parity with the toy: tests/arp_shape_test.c).
 * Here the host: the keys held as the chord, the steps on the track's clock, the notes and their gates, HOLD, ROOT,
 * the rate, recording. arp_next runs blocks until the next step plays; with GATE under 100 % and length-1 notes the
 * notes sounding then are exactly that step's */
static uint32_t arp_next(track_t *t, uint8_t *got)
{
    uint32_t idx = t->arp_idx, g = 0, i;
    while (t->arp_idx == idx && g++ < 200000u)
        arp_tick(t, CTL);
    for (i = 0; i < t->arp_n; i++)
        got[i] = arp_snd[trk_index(t)].note[i];
    return t->arp_n;
}
static int arp_seq(track_t *t, const uint8_t *want, uint32_t steps, uint32_t per)   /* the next steps play these */
{
    uint8_t got[ARP_SND];
    uint32_t s, n;
    int ok = 1;
    for (s = 0; s < steps; s++) {
        n = arp_next(t, got);
        if (n != per || memcmp(got, want + s * per, per)) {
            ok = 0;
            printf("    step %u: %u notes (%u %u), want %u %u\n", (unsigned)s, (unsigned)n, got[0], n > 1 ? got[1] : 0u,
                   want[s * per], per > 1 ? want[s * per + 1] : 0u);
        }
    }
    return ok;
}
static void arp_chord(track_t *t, const uint8_t *notes, uint32_t n)
{
    uint32_t i;
    arp_silence(t);
    arp_forget(t);                                   /* (a chord from nothing: KEY ATTACH holds none) */
    t->nheld = 0; t->arp_phys = 0;
    for (i = 0; i < n; i++)
        arp_add(t, notes[i]);
}
static int shape_arp(void)
{
    static const uint8_t CEGBD[5] = {60, 64, 67, 71, 74}, CEG[3] = {60, 64, 67};
    uint8_t got[ARP_SND];
    uint32_t i, k, c0;
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = &trk[0];
    bad += check("ARP: the power-on pattern is SHAYPE (the toy's DEFAULT_PATTERN), 4 steps, 1/8, GATE 75 %, ARP off",
                 arp_pat[0].n == 8u && arp_pat[0].note[1] == ARP_NOTE(0, 2, 1) && t->p[P_APROB] == 4 && t->p[P_ARATE] == 3 &&
                 t->p[P_AGATE] == 75 && !t->p[P_AMODE] && t->p[P_APAT] == 1);
    t->p[P_AMODE] = 1;
    arp_chord(t, CEGBD, 5);
    bad += check("  C E G B D held: pass 1 C+G E+B G+B C+B, pass 2 a place up (the toy's example)",
                 arp_seq(t, (const uint8_t[]){60, 67, 64, 71, 67, 71, 60, 71, 64, 71, 67, 74, 71, 74, 64, 74}, 8, 2));
    t->p[P_AOCT] = AD_DOWN;
    arp_chord(t, CEGBD, 5);
    bad += check("  DOWN: the second pass a place down, wrapping round the top (D+E)",
                 arp_seq(t, (const uint8_t[]){60, 67, 64, 71, 67, 71, 60, 71, 74, 64}, 5, 2));
    t->p[P_AOCT] = AD_UP; t->p[P_AORDER] = AB_OCT;
    arp_chord(t, CEG, 3);
    bad += check("  BOUND OCT over C E G: row 4 is C an octave up", arp_seq(t, (const uint8_t[]){60, 67, 64, 72}, 2, 2));
    t->p[P_AORDER] = AB_WRAP; t->p[P_AROOT] = 8;     /* ROOT G */
    arp_chord(t, CEG, 3);
    bad += check("  ROOT G over C E G: G C E, step 1 plays G and E an octave up", arp_seq(t, (const uint8_t[]){67, 76}, 1, 2));
    t->p[P_AROOT] = 0;
    /* the rate: a step of 1/8 at 120 BPM is 11025 samples, 1/16T 3675 */
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    c0 = t->arp_clk;
    arp_next(t, got);
    k = t->arp_clk - c0;
    t->p[P_ARATE] = 6;
    arp_next(t, got);
    c0 = t->arp_clk;
    arp_next(t, got);
    bad += check("  RATE: 1/8 and 1/16T steps their length (within a block)",
                 k >= 11025u && k < 11025u + CTL && t->arp_clk - c0 >= 3675u && t->arp_clk - c0 < 3675u + CTL);
    t->p[P_ARATE] = 3;
    /* gates: a note of length 2 over GATE 75 % sounds 1.5 steps: on at the next step, gone before the one after */
    fm1_irq_off(); arp_pat[0].n = 0; ash_paint(&arp_pat[0], 0, 0, 2); fm1_irq_on();
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    ok = t->arp_n == 1u && got[0] == 60;
    arp_next(t, got);
    ok &= t->arp_n == 1u && got[0] == 60;
    arp_next(t, got);
    ok &= t->arp_n == 0u;
    bad += check("  a note two steps long sounds 1.5 steps at GATE 75 %", ok);
    ash_load(&arp_pat[0], 0);
    /* let go: KEY ATTACH NOW, the notes end; NOTE (the default), they ring to their gate and the arp stops. HOLD: the
     * chord stays, the next press takes over (NOTE: at the next step) */
    t->p[P_AATT] = AA_NOW;
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    for (i = 0; i < 3u; i++) arp_remove(t, CEG[i]);
    arp_tick(t, CTL);
    bad += check("  the keys let go, ATTACH NOW: nothing sounds, nothing held", !t->nheld && !t->arp_n);
    t->p[P_AATT] = AA_NOTE;
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    for (i = 0; i < 3u; i++) arp_remove(t, CEG[i]);
    arp_tick(t, CTL);
    ok = !t->nheld && t->arp_n == 2u;
    for (k = 0; k < 11025u / CTL; k++)
        arp_tick(t, CTL);
    bad += check("  .. ATTACH NOTE: the step's notes ring to their gate, then nothing sounds, nothing new",
                 ok && !t->arp_n && !arp_att[0].n);
    t->p[P_AHOLD] = 1;
    arp_chord(t, CEG, 3);
    for (i = 0; i < 3u; i++) arp_remove(t, CEG[i]);
    ok = t->nheld == 3u && arp_seq(t, (const uint8_t[]){60, 67, 64, 60}, 2, 2);   /* (C E G: row 3 wraps to C) */
    arp_add(t, 62); arp_add(t, 65); arp_add(t, 69);
    ok &= t->nheld == 3u && arp_seq(t, (const uint8_t[]){69, 62}, 1, 2);   /* (the third step over D F A: A, D wrapped) */
    for (i = 0; i < 3u; i++) arp_remove(t, (uint32_t[]){62, 65, 69}[i]);
    bad += check("  HOLD: the chord stays after the keys; a new one takes over at the next step (ATTACH NOTE)", ok && t->nheld == 3u);
    t->p[P_AHOLD] = 0;
    t->nheld = 0; arp_tick(t, CTL);
    /* recording (WaveLoop): the chord played is written, not what the arp plays: C E G pressed into step 1, the arp
     * stepping meanwhile (SHAYPE: C+G, E+C, ..), one step holds C E G and nothing else is written */
    song.sel = 0; song.rec = 1;
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){0};
    seq_start();
    seq_tick(t, CTL);
    for (i = 0; i < 3u; i++)
        input_on(t, CEG[i], 100);
    for (k = 0; k < 3u * 5512u / CTL; k++) {           /* three steps of 1/16, the arp at 1/8 playing */
        seq_tick(t, CTL);
        arp_tick(t, CTL);
    }
    ok = t->arp_n != 0u;
    for (i = 0; i < 3u; i++)
        input_off(t, CEG[i]);
    for (i = 0, k = 0; i < NSTEP; i++)
        k += t->step[i].time == ST_NOTE && t->step[i].n;
    ok &= k == 1u && t->step[0].n == 3u && t->step[0].note[0] == 60 && t->step[0].note[1] == 64 && t->step[0].note[2] == 67 &&
          t->step[1].time == ST_TIE && t->step[2].time == ST_TIE;
    seq_stop();
    song.rec = 0;
    bad += check("  recording: the chord played (C E G) into one step, held as TIEs; not the arp's notes", ok);
    t->nheld = 0; t->p[P_AMODE] = 0; arp_tick(t, CTL);
    ash_load(&arp_pat[0], 0);
    return bad;
}
/* ------------------------------------------------ 1.1 parameter locks --- */
/* the transport on to the next step of t (its seq_idx changes) */
static void lock_next(track_t *t)
{
    uint32_t i = t->seq_idx, n = 0;
    while (t->seq_idx == i && n++ < 100000u)
        seq_tick(t, CTL);
}
/* track 1 alone: 8 NOTE steps, REV 20, swing sw */
static track_t *lock_track(int32_t sw)
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 8;
    t->p[P_SSWING] = (int16_t)sw;
    song.g[G_SWING] = 0;
    t->p[P_REV] = 20;
    for (i = 0; i < 8u; i++)
        t->step[i] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    return t;
}
/* two passes from step 0: REV at each step's start as want[]; 1 = all as wanted */
static int lock_passes(track_t *t, const int16_t *want)
{
    uint32_t i, pass;
    seq_start();
    seq_tick(t, CTL);
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < 8u; i++) {
            if (pass || i)
                lock_next(t);
            if (t->seq_idx != i || t->p[P_REV] != want[i]) {
                printf("motion:   pass %u step %u: REV %d, wanted %d\n", pass, i, t->p[P_REV], want[i]);
                return 0;
            }
        }
    return 1;
}
static int lock_playback(void)
{
    static const int16_t want[8] = {20, 20, 100, 20, 60, 60, 90, 60};   /* locks on 2, 6; automation on 4 */
    int bad = 0, ok;
    uint32_t n;
    track_t *t = lock_track(0);
    project_t q;
    ok = !motion_set_lock(t, 2, P_REV, 100) && !motion_set_event(t, 4, P_REV, 60) && !motion_set_lock(t, 6, P_REV, 90);
    bad += check("a lock is one of the shared 64 records, bit 7 of its id (MOTION_LOCK)", ok && motion.count == 3u &&
        motion.event[0].param == (P_REV | MOTION_LOCK) && motion.event[1].param == P_REV && motion_lock_count(t) == 2u &&
        motion_count(t) == 3u && motion_lock_steps(0) == ((1u << 2) | (1u << 6)));
    bad += check("lock: its value on its step only, back after it (the sound's own, or the automation held)",
        lock_passes(t, want));
    lock_next(t); lock_next(t); lock_next(t);
    project_capture(&q);
    bad += check("  a lock sounding: the sound's own value is the base, a project saves that", t->seq_idx == 2u &&
        t->p[P_REV] == 100 && motion_base_value(t, P_REV) == 20 && q.t[0].p[P_REV] == 20);
    motion_set_enabled(t, 0);
    bad += check("  AUTOMATION PLAY OFF: the sound's own value at once, the locks kept", t->p[P_REV] == 20 &&
        motion_lock_count(t) == 2u);
    motion_set_enabled(t, 1);
    lock_next(t); lock_next(t); lock_next(t); lock_next(t);
    seq_stop();
    bad += check("  STOP while a lock sounds puts the sound's own value back", t->p[P_REV] == 20);
    t = lock_track(100);
    motion_set_lock(t, 2, P_REV, 100); motion_set_event(t, 4, P_REV, 60); motion_set_lock(t, 6, P_REV, 90);
    bad += check("  with SWING 100: the same values step by step", lock_passes(t, want));
    seq_stop();
    t = lock_track(0);                                   /* the chance: a step that does not play applies no lock */
    motion_set_lock(t, 2, P_REV, 100);
    step_set_chance(&t->step[2], 0);
    {
        static const int16_t w0[8] = {20, 20, 20, 20, 20, 20, 20, 20};
        bad += check("  CHANCE 0 %: the step does not play, nor its lock", lock_passes(t, w0));
    }
    seq_stop();
    step_set_chance(&t->step[2], 100);
    {
        static const int16_t w1[8] = {20, 20, 100, 20, 20, 20, 20, 20};
        bad += check("  CHANCE 100 %: its lock", lock_passes(t, w1));
    }
    seq_stop();
    step_set_chance(&t->step[2], 50);                    /* the one roll: the lock with the notes, or neither */
    seq_start(); seq_tick(t, CTL);
    for (n = 0, ok = 1; n < 400u; n++) {
        lock_next(t);
        if (t->seq_idx == 2u)
            ok &= (t->p[P_REV] == 100) == (t->seq_n != 0u);
    }
    bad += check("  CHANCE 50 %: the lock applies exactly when the step's notes play", ok);
    seq_stop();
    t = lock_track(0);                                   /* RATCH x3: the lock over all its parts */
    motion_set_lock(t, 2, P_REV, 100);
    step_set_ratchet(&t->step[2], 3);
    seq_start(); seq_tick(t, CTL);
    lock_next(t); lock_next(t);
    {
        uint32_t v0 = vage;
        for (ok = t->seq_idx == 2u; t->seq_idx == 2u;) {
            ok &= t->p[P_REV] == 100;
            seq_tick(t, CTL);
        }
        bad += check("  RATCH x3: its 3 parts all with the lock, back at the next step", ok && vage - v0 >= 3u &&
            t->p[P_REV] == 20);
    }
    seq_stop();
    t = lock_track(0);                                   /* an engine parameter, read by the note-on */
    t->p[P_E4] = 10;
    motion_set_lock(t, 1, P_E4, 90);
    motion_set_lock(t, 1, P_E4, 300);                    /* (out of its range: refused, the lock as it was) */
    seq_start(); seq_tick(t, CTL); lock_next(t);
    bad += check("  an engine parameter: set at the step's start (before its notes), in its range", t->p[P_E4] == 90 &&
        t->seq_n == 1u);
    lock_next(t);
    bad += check("  .. and back at the next step", t->p[P_E4] == 10);
    seq_stop();
    return bad;
}
static int lock_edit(void)
{
    int bad = 0;
    int16_t v;
    track_t *t = lock_track(0);
    motion_set_event(t, 3, P_REV, 50);
    bad += check("a lock replaces an automation event of its step and id (one record, either kind)",
        !motion_set_lock(t, 3, P_REV, 70) && motion.count == 1u && motion_lock_get(t, 3, P_REV, &v) && v == 70);
    motion_set_event(t, 3, P_REV, 40);
    bad += check("  .. and back (an automation event over a lock)", motion.count == 1u && !motion_lock_count(t));
    motion_set_lock(t, 3, P_ATK, 9); motion_set_lock(t, 5, P_ATK, 9); motion_set_lock(&trk[1], 3, P_REV, 1);
    bad += check("clear a step's locks: its automation and the other steps' and tracks' stay",
        motion_clear_locks(t, 3) == 1u && motion.count == 3u && motion_lock_count(t) == 1u && motion_lock_count(&trk[1]) == 1u);
    bad += check("clear every step's locks (NSTEP)", motion_clear_locks(t, NSTEP) == 1u && !motion_lock_count(t) &&
        motion_count(t) == 1u);
    motion_delete_event(&trk[1], 3, P_REV);
    bad += check("delete an event deletes a lock too (either kind)", !motion_count(&trk[1]));
    return bad;
}
static int lock_project(void)
{
    int bad = 0;
    track_t *t = lock_track(0);
    project_t before, after;
    project_store_t a, b;
    uint32_t i, ok = 1;
    motion_set_event(t, 1, P_REV, 33);                   /* no lock: as every project before 1.1 */
    project_capture(&before);
    ok = proj_pack(&a, &before) && proj_import(&after, &a, sizeof a) && proj_pack(&b, &after) && !memcmp(&a, &b, sizeof a);
    for (i = 0; i < before.motion.count; i++)
        ok &= before.motion.event[i].param < 128u && !(before.motion.event[i].param & MOTION_LOCK);
    bad += check("a project without locks: the bytes as before (ids < 128), the same round trip", ok);
    motion_set_lock(t, 2, P_REV, 101); motion_set_lock(t, 7, P_E4, 77);
    project_capture(&before);
    ok = proj_pack(&a, &before) && proj_import(&after, &a, sizeof a) && !memcmp(&before, &after, sizeof before);
    bad += check("FUN9 round trip keeps the locks (bit 7 of the id byte), their values and steps", ok &&
        after.motion.event[1].param == (P_REV | MOTION_LOCK) && after.motion.event[2].value == 77);
    project_save(2);
    motion_clear(t);
    project_load(2);
    bad += check("  saved and loaded: the locks play again", motion_lock_count(t) == 2u && lock_passes(t,
        (const int16_t[8]){20, 33, 101, 33, 33, 33, 33, 33}));
    seq_stop();
    chain_config.count = 1; chain_config.row[0] = chain_row_of(2, 1);
    motion_clear(t); t->p[P_REV] = 20;
    bad += check("SONG: a slot's locks come with its motion", chain_prepare() == 0 && chain.source[2].motion.count == 3u);
    seq_start(); seq_tick(t, CTL);
    lock_next(t); lock_next(t);
    ok = chain.running && t->seq_idx == 2u && t->p[P_REV] == 101;
    lock_next(t);
    bad += check("  a song plays them on their step, back after it", ok && t->p[P_REV] == 33);
    seq_stop(); chain_config.count = 0; chain.armed = 0;
    return bad;
}
/* the UI: hold a step, turn a knob (ui_input.c lock_*) */
static void lk_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); }
static void lk_up(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
static int lock_ui(void)
{
    int bad = 0, ok;
    int16_t v;
    track_t *t;
    uint32_t kn;
    /* the roll: the step being entered is the one held */
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    go_home();
    open_family(FAM_SEQ);
    frame();
    for (kn = 0; kn < 27u && kb_map(t, kn) > 127u; kn++)
        ;
    lk_down(kn);                                         /* (1.2, #133: not armed, a key only plays: no step held) */
    ok = !ui.entry_open && !lock_held() && !t->step[0].n;
    turn(EN_K3, 4);
    lk_up(kn);
    bad += check("the roll, not armed: a key held writes no step and holds none; KNOB 3 locks nothing",
                 ok && !motion_lock_count(t) && !t->step[0].n && ui.cursor == 0u);
    song.rec = 1;                                        /* armed, stopped: step recording */
    lk_down(kn);
    ok = ui.entry_open && lock_held() == 1u && t->step[0].n == 1u;
    turn(EN_K3, 4);                                      /* OB-Z's HOME: E5 E6 ENV FLT E2 */
    bad += check("the roll, armed (step recording): a note key held (the entry) is the step held; KNOB 3 locks ENV FLT, TIME untouched",
        ok && lock_id(2) == P_ED_FLT && motion_lock_get(t, 0, P_ED_FLT, &v) && v == t->p[P_ED_FLT] + 4 &&
        t->step[0].time == ST_NOTE);
    lk_up(kn);
    bad += check("  let go: the note and the lock on step 1, the cursor on", ui.cursor == 1u && t->step[0].n == 1u &&
        motion_lock_count(t) == 1u && motion_lock_steps(0) == 1u);
    cursor_set(0);
    press(B_EDIT);
    bad += check("  [EDIT] alone clears the step and its locks", !t->step[0].n && !motion_lock_count(t));
    song.playing = 1; chain.running = 1;
    lk_down(kn); turn(EN_K3, 1); lk_up(kn);
    bad += check("  a song playing: STOP TO EDIT, no lock", !motion_lock_count(t));
    song.playing = 0; chain.running = 0; song.rec = 0;
    return bad;
}
/* 1.1.5: lock edits by hand are one undo (SAVE held), redo too */
static int lock_undo(void)
{
    int bad = 0;
    int16_t v;
    track_t *t;
    uint32_t kn, id;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    song.playing = 0;
    song.rec = 1;                                        /* armed, stopped: a note key held is the cursor step held */
    go_home();
    open_family(FAM_SEQ);
    frame();
    for (kn = 0; kn < 27u && kb_map(t, kn) > 127u; kn++)
        ;
    id = lock_id(1);
    cursor_set(4);
    lk_down(kn);
    turn(EN_K2, 3);
    turn(EN_K2, 2);
    lk_up(kn);
    bad += check("lock undo: a step held, KNOB 2 turned twice: one lock", id != 0xFFu && motion_lock_get(t, 4, id, &v) &&
        v == motion_base_value(t, id) + 5);
    hold(B_SAVE);
    bad += check("  SAVE held: the lock is gone (one undo for the whole turn), the step's note stays",
        !motion_lock_count(t) && t->step[4].n == 1u);
    hold(B_SAVE);
    bad += check("  held again (redo): the lock is back at its value", motion_lock_get(t, 4, id, &v) &&
        v == motion_base_value(t, id) + 5);
    frames(2000);
    cursor_set(4);
    lk_down(kn);
    press(B_EDIT);
    lk_up(kn);
    bad += check("  a step held + [EDIT] clears it", !motion_lock_count(t) && str_eq(ui.msg, "LOCKS CLEARED"));
    hold(B_SAVE);
    bad += check("  SAVE held brings it back", motion_lock_get(t, 4, id, &v) && v == motion_base_value(t, id) + 5);
    frames(2000);
    cursor_set(4);
    lk_down(kn);
    press(B_EDIT);
    lk_up(kn);
    cursor_set(4);
    lk_down(kn);
    press(B_EDIT);                                       /* (no lock there now: NO LOCKS, the undo copy kept) */
    lk_up(kn);
    hold(B_SAVE);
    bad += check("  [EDIT] on a step without locks takes no undo copy (the clear before stays undoable)",
        motion_lock_count(t) == 1u);
    song.rec = 0;
    return bad;
}
/* 1.1.5: SEQ > AUTO LIST (ui_events.c); 1.2: the AUTOMATION page, its rows: PLAY, QUANTIZE, the records (and CHANCE / RATCH / NUDGE), + ADD */
static uint32_t rows_n(void) { uint16_t rw[EV_ROWS]; return ev_list(rw); }   /* PLAY, QUANTIZE and + ADD included */
static uint32_t row_at(uint32_t r) { uint16_t rw[EV_ROWS]; return r < ev_list(rw) ? rw[r] : 0xFFFFu; }
static int auto_list(void)
{
    int bad = 0, ok;
    int16_t v;
    track_t *t;
    uint32_t id0, id1, i;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_home();
    cursor_set(4);
    go_page(GR_EVENTS);
    frame();
    id0 = ui.ev_id;
    ok = auto_cur() == EVC(EVK_TOP, 0) && str_eq(act_name(4), "CLEAR") && !act_ready();
    turn(EN_K1, 2);                                      /* (past QUANTIZE, 1.2) */
    bad += check("AUTOMATION: empty, PLAY then + ADD LOCK (on the SEQ cursor's step, the first knob of the sound page)",
        ok && cur_page()->graph == GR_EVENTS && rows_n() == 3u && auto_cur() == EVC(EVK_ADD, 0) && ui.ev_step == 4u &&
        id0 == lock_id(0) && str_eq(act_name(4), "ADD") && act_ready());
    press(B_OCTUP);
    bad += check("  OCT+: a lock on step 5 at the sound's own value, its row selected",
        motion_lock_get(t, 4, id0, &v) && v == motion_base_value(t, id0) && ui.ev_row == 2u && auto_cur() >> 8 == EVK_REC &&
        str_eq(ui.msg, "LOCK ADDED") && motion_enabled(t));
    turn(EN_K4, 3);
    bad += check("  KNOB 4: its value", motion_lock_get(t, 4, id0, &v) && v == motion_base_value(t, id0) + 3);
    turn(EN_K2, 2);
    bad += check("  KNOB 2: its step (5 -> 7), the value kept", !motion_lock_get(t, 4, id0, &v) &&
        motion_lock_get(t, 6, id0, &v) && v == motion_base_value(t, id0) + 3);
    turn(EN_K3, 1);
    id1 = MOTION_ID(&motion.event[0]);
    bad += check("  KNOB 3: the next parameter, from the sound's own value", id1 > id0 && ev_id_ok(t, id1) &&
        motion_lock_get(t, 6, id1, &v) && v == motion_base_value(t, id1) && motion_count(t) == 1u);
    press(B_OCTUP);
    bad += check("  OCT+ on the row: LOCK -> AUTO", motion_count(t) == 1u && !motion_lock_count(t) &&
        str_eq(act_name(4), "LOCK"));
    press(B_OCTUP);
    ok = motion_lock_count(t) == 1u;
    press(B_OCTUP);
    bad += check("  and back (AUTO -> LOCK -> AUTO)", ok && !motion_lock_count(t));
    turn(EN_K1, 1);
    turn(EN_K2, -2);
    bad += check("  KNOB 1: + ADD LOCK; KNOB 2 there: its step", auto_cur() == EVC(EVK_ADD, 0) && ui.ev_step == 2u);
    press(B_OCTUP);
    bad += check("  OCT+: a second lock on step 3, first in the list (by step)", motion_count(t) == 2u &&
        motion_lock_get(t, 2, ui.ev_id, &v) && rows_n() == 5u && ui.ev_row == 2u &&
        (motion.event[row_at(2) & 0xFFu].place & STEP_MASK) == 2u);
    turn(EN_K1, 5);
    press(B_OCTUP);
    bad += check("  OCT+ on + ADD LOCK where one is: ALREADY LISTED, that row selected", motion_count(t) == 2u &&
        str_eq(ui.msg, "ALREADY LISTED") && ui.ev_row == 2u);
    press(B_EDIT);
    bad += check("  EDIT: the row's record goes", motion_count(t) == 1u && !motion_lock_get(t, 2, ui.ev_id, &v) &&
        str_eq(ui.msg, "DELETED"));
    hold(B_SAVE);
    bad += check("  SAVE held: it is back", motion_count(t) == 2u);
    hold(B_SAVE);
    bad += check("  held again: gone again", motion_count(t) == 1u);
    frames(2000);
    turn(EN_K1, -5);
    turn(EN_K1, 2);                                      /* (the first record: the row after PLAY and QUANTIZE) */
    v = motion.event[0].value;
    turn(EN_K4, 2); turn(EN_K4, 2); turn(EN_K4, 2);
    ok = motion.event[0].value == v + 6;
    hold(B_SAVE);
    bad += check("  KNOB 4 turned on (three turns): one undo", ok && motion.event[0].value == v);
    chain.running = 1;
    turn(EN_K4, 1);
    bad += check("  a song playing: STOP TO EDIT", motion.event[0].value == v && str_eq(ui.msg, "STOP TO EDIT"));
    chain.running = 0;
    for (i = 0; motion.count < MOTION_MAX; i++)          /* the 128 used (track 2) */
        motion_set_event(&trk[1], i % NSTEP, i < NSTEP ? P_REV : P_DLY, 10);
    turn(EN_K1, 5);
    press(B_OCTUP);
    bad += check("  the 128 used: OCT+ adds nothing, AUTOMATION FULL", motion_count(t) == 1u &&
        str_eq(ui.msg, "AUTOMATION FULL") && !act_ready());
    motion_clear(&trk[1]);
    for (i = 0; i < P_COUNT; i++)                        /* names: an id each, none of the FM OP ENV, no reserved ids */
        if (ev_id_ok(t, i) && ((i >= P_FM1_ATK && i <= P_FM4_LEVEL) || (i >= P_RSV_LN && i <= P_RSV_LN_END) || !motion_param(i)))
            break;
    bad += check("  KNOB 3's list: what motion records and the sound has (no OP ENV, no reserved lane ids)", i == P_COUNT &&
        !ev_id_ok(&trk[3], P_RSV_LN));
    {
        char a[12], b[12];
        ev_name(t, P_ED_FLT, a); ev_name(t, P_LD_FLT, b);
        bad += check("  names: ENV FLT / LFO FLT", str_eq(a, "ENV FLT") && str_eq(b, "LFO FLT"));
    }
    ui_prefs |= PREF_LARGE;
    bad += check("  MENU > LARGE: the list layout with labels in M (as the other lists)", large_kind() == LK_LABEL);
    ui_prefs &= ~PREF_LARGE;
    return bad;
}
int main(void)
{
    int bad = motion_recording() + motion_capacity() + probability_playback() + compact_project() + loads_and_song();
    bad += shape_arp();
    bad += lock_playback() + lock_edit() + lock_project() + lock_ui() + lock_undo() + auto_list();
    printf("%s\n", bad ? "MOTION TEST FAILED" : "motion/chance/compact storage tests passed"); return bad != 0;
}

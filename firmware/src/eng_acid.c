/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: the TB-303 voice. bass303.c (a float, no-libm port of Robin Schmidt's Open303, MIT, by way of fm1-x0x's bass303) is the
 * voice; this file is its machine: the EDIT slots and ENV as the 303's pots, one mono voice per part in the engine memory,
 * the own-envelope hooks (the 303's VCA is the instrument), accent and slide from the sequencer's step. Design and the exact
 * mapping: docs/plans/2026-10-17-acid-303.md.
 *
 *   E0 WAVE -> WAVE         E1 TUNE -> TUNE (A4 440 Hz +- 1.6 semitones)   E2 DECY -> DECAY (200 ms .. 2.5 s)   E3 ACC -> ACCENT
 *   E4 CUT  -> CUTOFF       E5 RES  -> RESO                                E6 DRV  -> DRIVE (Soft)
 *   E7 SQL  -> ACCDEC + a RES offset on accented notes                     ENV (P_ED_FLT) -> ENVMOD (0 for a negative knob)
 * Accent = step velocity > 110 (seq.c sets 127). Slide = t->slide_glide (or GLIDE) onto a note that is still sounding: bass303's
 * slideToNote, no retrigger. */
#include "bass303.c"

typedef struct {                 /* the part's engine memory */
    bass303_t b;
    int16_t lvl;                 /* the VCA command amp() reported this tick, Q15 */
    int16_t ref;                 /* the one the previous render used (the ramp's start) */
    int16_t cutq;                /* the cutoff last applied, in 1/256 pot steps; -1 = none yet */
    uint8_t note;                /* the note and accent last handed to the voice: a change without a note_on is a legato step */
    uint8_t acc;
    uint8_t fresh;               /* the first render of a note: the voice's amp0 = 0 is not a fade-in */
    uint8_t pend;                /* a note_on has not yet had its modulated pitch applied */
    uint8_t og_drv;              /* the DRV pot og was worked out for */
    float og;                    /* the output gain (ACID_TRIM_DB and the drive's make-up), linear; 0 = not worked out yet */
} acid_t;

/* The level of the machine (Task 7.2a). The voice runs with VOLUME at the top of its pot (bass303.c b3_def: it was 96, the donor's
 * -14.6 dB, which left ACID about 14 dB under the other machines). Two trims follow it. ACID_TRIM_DB brings the plain presets'
 * median to -18 dBFS in the audition (the other machines' -17.3) with their peaks still under -2.7. DRIVE is a tanh that hands
 * back tanh(g x) / g, so it only ever takes level away (6 .. 10 dB for DRV 80 .. 127): ACID_DRV_MAKEUP_DB is given back at DRV
 * 127, in proportion, which brings the DISTORTED presets in with the rest. Applied to the voice's output, after the whole chain. */
#define ACID_TRIM_DB 3.0f
#define ACID_DRV_MAKEUP_DB 9.5f
/* The guard clamp (whole full scales, inclusive: |s| <= ACID_GUARD_FS). It exists so that a runaway or a NaN never reaches the integer
 * conversion, and it is sized to the mixer's int32 headroom, which this engine's output must not exceed. Accents reach 3.5 .. 4.9 FS
 * on the shipped presets (4.86 at most) and about 6.8 FS on a hand-edited worst case, so it is not a limiter: the master limiter and
 * softclip do the levelling.
 *   mix_part (fx.c) takes b = s * 32768, then x = ((b >> 2) * LEVEL_Q12[127]) >> 10, with LEVEL_Q12[127] = 9713, so
 *   x = 77,704 per full scale; it then forms x * gl with gl <= 4096 (pan 0), which has to stay below 2^31: x <= 524,287 (6.747 FS).
 *   The master forms ((mix_l + wet) >> 2) * mg with mg <= 4096 (MASTER_FULL): mix_l + wet < 2^21 = 2,097,152, and the wet bus (the
 *   sends at 127, REVERB HALL at SIZE 127 / DAMP 0, the delay's feedback up) rides on top of the four parts' dry sum.
 *   At 6 FS (the previous value): x <= 466,224, four parts 1.865 M, 232 k left for the wet bus, and four parts held at the guard with
 *   every send at 127 and HALL measured |mix + wet| = 2,353,498, 12.2 % over 2^21 (tests/acid_test.c test_wet_bus). At 5 FS:
 *   b <= 163,840, x <= 388,520 (margin 1.35 on x * gl), four parts 1.554 M, 543 k left for the wet bus. The shipped presets (4.86 FS at
 *   most) are untouched. At 8 FS (the value before that) x = 621 k and x * gl wrapped above 6.75 FS. The same figure, for the same
 *   reason, as DRIFT's chorus guard (dsp_chorus.c CHORUS_GUARD_FS). test_headroom and test_wet_bus pin both products through the real mix path. */
#define ACID_GUARD_FS 5
#define ACID_MEM (sizeof(acid_t))

static acid_t *acid_of(const track_t *t)
{
    return (acid_t *)ENG_MEM((uint32_t)(t - trk) % NPART);
}

/* a pot to the voice, only when it changed (bass303_set recomputes what depends on it) */
static void acid_pot(bass303_t *b, int i, int v)
{
    v = clamp(v, 0, b3_max[i]);
    if (b->pot[i] != v)
        bass303_set(b, i, v);
}

/* the EDIT slots and ENV as the 303's pots (the table in the plan). Run before a note and every block */
static void acid_pots(const track_t *t, acid_t *a, int accent)
{
    const int16_t *p = t->p;
    bass303_t *b = &a->b;
    int sql = clamp(p[P_E7], 0, 127), ed = p[P_ED_FLT];
    acid_pot(b, BASS303_WAVE, p[P_E0]);
    acid_pot(b, BASS303_TUNE, p[P_E1] + 64);
    acid_pot(b, BASS303_DECAY, p[P_E2]);
    acid_pot(b, BASS303_ACCENT, p[P_E3]);
    acid_pot(b, BASS303_RESO, p[P_E5] + (accent ? (sql * 24 + 63) / 127 : 0));
    acid_pot(b, BASS303_DRIVE, p[P_E6]);
    acid_pot(b, BASS303_ACCDEC, 7 + (sql * 18 + 63) / 127);
    acid_pot(b, BASS303_ENVMOD, ed <= 0 ? 0 : (ed * 127 + 31) / 63);
    if (a->og == 0.0f || a->og_drv != (uint8_t)clamp(p[P_E6], 0, 127)) {
        a->og_drv = (uint8_t)clamp(p[P_E6], 0, 127);
        a->og = fm_db2lin(ACID_TRIM_DB + ACID_DRV_MAKEUP_DB * (float)a->og_drv * (1.0f / 127.0f));
    }
}

/* the voice's pitch in Hz: the TARGET (voice.c's own linear glide is undone: the slide is bass303's slew), with the engine-wide
 * TUNE, bend, LFO and the matrix that voice.c folded into m->pitch16 / m->fine */
static float acid_hz(const bass303_t *b, int32_t pitch16, int32_t fine)
{
    float f = b->tuning * fm_exp2f((float)(pitch16 - 69 * 16) * (1.0f / 192.0f));
    return fine ? f * (1.0f + (float)fine * (1.0f / 4096.0f)) : f;
}

static void acid_note_on(track_t *t, voice_t *v)
{
    acid_t *a = acid_of(t);
    bass303_t *b = &a->b;
    int acc = v->vel > 110, sounding;
    if (b->rc1_c == 0.0f) {                              /* the region is zero: a part's first ACID note */
        bass303_init(b);
        a->cutq = -1;
    }
    acid_pots(t, a, acc);
    sounding = b->gate && b->amp_y > 1e-4f;              /* the previous note is still held and audible: a slide may glide from it */
    bass303_note_on_f(b, acid_hz(b, v->pitch16, 0), acc, (t->slide_glide || t->p[P_GLIDE]) && sounding);
    a->note = v->note;
    a->acc = (uint8_t)acc;
    a->fresh = (uint8_t)!sounding;
    a->pend = 1;
}

/* the VCA command (amp envelope plus the accent's boost of it) as Q15, [0, 32767] (voice.c does env << 9: it must not leave it) */
static float acid_vca(const bass303_t *b)
{
    return b->amp_trig ? 1.0f : b->amp_y + (b->gate ? (0.45f + 4.0f * b->accent_gain) * b->env_y : 0.0f);
}

static int32_t acid_amp(const track_t *t, const voice_t *v)
{
    acid_t *a = acid_of(t);
    float x;
    (void)v;
    if (a->b.rc1_c == 0.0f)
        return 0;
    x = acid_vca(&a->b);
    a->lvl = (int16_t)(x >= 1.0f ? 32767 : x > 0.0f ? (int32_t)(x * 32767.0f) : 0);   /* (NaN: 0) */
    return a->lvl;
}

/* the voice ends once its VCA is 0 -- gate off (1 ms or 50 ms release) or, held, after the 1.23 s decay has run out */
static int acid_done(const track_t *t, const voice_t *v)
{
    const acid_t *a = acid_of(t);
    float x;
    (void)v;
    if (a->b.rc1_c == 0.0f)
        return 1;
    x = acid_vca(&a->b) * 32767.0f;
    return !(x >= 1.0f);
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    acid_t *a = acid_of(t);
    bass303_t *b = &a->b;
    const int16_t *p = t->p;
    float buf[CTL], g0, g1, gs, f;
    int acc = v->vel > 110, sl;
    int32_t cq, mc;
    uint32_t i;
    if (b->rc1_c == 0.0f)
        return;
    if (n > CTL)
        n = CTL;
    if (!v->gate && b->gate)                             /* the key is up (an ownenv voice's stage never says so) */
        bass303_note_off(b);
    acid_pots(t, a, acc);
    f = acid_hz(b, m->pitch16 + (v->pitch16 - v->pitch_cur), m->fine);
    if (v->gate && (v->note != a->note || acc != a->acc)) {   /* legato: mono_play moved the voice to a new note, no note_on */
        sl = t->slide_glide || p[P_GLIDE];
        bass303_note_on_f(b, f, acc, 1);                 /* slideToNote: the envelopes run on */
        if (!sl)
            b->slew_y = f;                               /* no slide: the pitch jumps */
        a->note = v->note;
        a->acc = (uint8_t)acc;
        a->pend = 0;
    } else if (a->pend) {                                /* a note_on's first block: its pitch with the modulation */
        b->osc_freq = f;
        if (b->trig)
            b->slew_y = f;
        a->pend = 0;
    } else if (f != b->osc_freq) {                       /* modulation moving a steady or sliding pitch: not slewed */
        float r = f / b->osc_freq;
        b->osc_freq = f;
        b->slew_y *= r;
    }
    /* the cutoff: CUT plus what the LFO and the matrix added. voice.c's m->cutoff also holds ENV x ED_FLT and the accent's
     * boost, from the envq15 amp() reported; the 303's own filter envelope is ENVMOD, so those two are taken out again */
    mc = m->cutoff - ((m->envq15 * p[P_ED_FLT]) >> 7) - (acc ? (m->envq15 * 24) >> 7 : 0);
    cq = clamp((int32_t)p[P_E4] * 256 + mc, 0, 127 * 256);
    if (cq != a->cutq) {
        if (!(cq & 255))
            acid_pot(b, BASS303_CUTOFF, cq >> 8);
        else
            bass303_set_cutoff_f(b, (float)cq * (1.0f / 256.0f));
        a->cutq = (int16_t)cq;
    }
    bass303_render(b, buf, (int)n);
    /* the voice's amplitude (LFO, the matrix, a fade for an engine switch, the last block of an ending voice: 0) relative to the
     * level amp() reported, so the 303's own VCA is the envelope and everything else only attenuates it */
    g1 = a->lvl > 0 ? (float)m->amp1 / (float)a->lvl : 0.0f;
    g1 = g1 > 1.0f ? 1.0f : g1 < 0.0f ? 0.0f : g1;
    g0 = a->fresh ? g1 : a->ref > 0 ? (float)m->amp0 / (float)a->ref : 0.0f;
    g0 = g0 > 1.0f ? 1.0f : g0 < 0.0f ? 0.0f : g0;
    g0 *= a->og;
    gs = (g1 * a->og - g0) * (1.0f / (float)n);
    for (i = 0; i < n; i++) {
        float s;
        g0 += gs;
        s = buf[i] * g0;
        if (!(s > -(float)ACID_GUARD_FS && s < (float)ACID_GUARD_FS))   /* a runaway or a NaN never reaches the integer conversion */
            s = s >= (float)ACID_GUARD_FS ? (float)ACID_GUARD_FS : s <= -(float)ACID_GUARD_FS ? -(float)ACID_GUARD_FS : 0.0f;
        out[i] += (int32_t)(s * 32768.0f);
    }
    a->ref = a->lvl;
    a->fresh = 0;
}

static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};
static const engine_t ENG_ACID = {
    .name = "SB303", .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0}, {"TUNE", F_BIPCT, -64, 63, 0, 0, 0},
        {"DECY", F_PCT, 0, 127, 60, 0, 0},          {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"SQL", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = ACID_PRESETS, .npresets = NELEM(ACID_PRESETS),   /* presets/four/acid.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E2}, .poly = 1, .keep = 0,
    .note_on = acid_note_on, .render = acid_render,
    .mem = ACID_MEM, .ownenv = 1, .amp = acid_amp, .done = acid_done,
};

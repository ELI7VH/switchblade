/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the VOID engine (eng_void.c), same sources as the firmware (hostsim.c):
 *   build/host/void_test [DEMO_DIR]      (run_tests.sh builds and runs it; demos in build/void_demo/)
 * 1. the engine: number 1 (ENGI_WAVE, "WAVE"), its knobs, its presets.
 * 2. pitch: SINE at A4 is 440 Hz within the plugin's movement (drift +-1.5 cents, micro-LFO up to 2.5); OSC2 +12
 *    alone an octave up; the fine steps (-12c) as many cents.
 * 3. band-limited: SAW, SQUARE and SPECTRAL (its top position) at C8: what is not a harmonic below -30 dB (the
 *    linear read of a level at 4 samples per cycle of its top harmonic; the plugin aliases at ~-10 dB here).
 * 4. bounded: every table x every filter TYPE at RES 127 and DRV 127, CUT 0 / 127, C1 / C4 / C8, POS 0 / 127: no
 *    wrapped sample (a jump over full scale), no DC; a TYPE and table change while notes sound.
 * 5. DC: BRASS LEAD held for 2 s at its own POS, the engine's own output (what feeds the sends, ahead of the
 *    master's DC blocker): |mean| under 1 % of the peak. And the blocker's range against the clipper's table.
 * 6. the presets: each sounds, its loudest 0.2 s of a held chord (a pluck decays, a pad swells) near OB-Z's SAW
 *    LEAD (-18 .. +6 dB: the plugin's LP24 at a high RES loses its pass band, as a ladder does).
 * 7. the cost: each preset at WAVE's voice cap against 8 voices of OB-Z RAVE (instructions; the reference was PHASE
 *    WIRE, retired, whose cost was within 2 % of OB-Z RAVE's on the host).
 * 8. demos (DEMO_DIR): each preset playing a phrase, a POS sweep per table.
 * 9. against the plugin: eng_void.c's filter and the plugin's own Filter.cpp (build/void_ref, tools/void_ref.cpp:
 *    skipped without it) on the same saw: the level, and the harmonics within 50 dB of the loudest. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "void_ref_cfg.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("void: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define NREC (FS * 2u)
static int32_t out_buf[2 * CTL];
static int32_t rec[NREC];
static uint32_t nrec;
static void blocks(uint32_t n)
{
    uint32_t i;
    while (n--) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL && nrec < NREC; i++)
            rec[nrec++] = out_buf[2u * i];
    }
}

static void fresh(uint32_t e, uint32_t pi)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    memset(sl, 0, sizeof sl);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : e, k ? 0u : pi);
    for (k = 1; k < NPART; k++)
        trk[k].p[P_MUTE] = 1;
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    nrec = 0;
}

/* track 1 = VOID, dry, no envelope or LFO movement, a held note: the oscillators as they are */
static track_t *plain(uint32_t tb1, uint32_t tb2, uint32_t mix, uint32_t osc2, uint32_t ft)
{
    track_t *t = &trk[0];
    fresh(ENGI_WAVE, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 0;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 0;
    t->p[P_ED_FLT] = t->p[P_ED_PIT] = t->p[P_ED_SHP] = 0;
    t->p[P_LD_FLT] = t->p[P_LD_PIT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_E0] = (int16_t)tb1;
    t->p[P_E1] = 0;
    t->p[P_E2] = (int16_t)tb2;
    t->p[P_E3] = (int16_t)mix;
    t->p[P_E4] = 127;
    t->p[P_E5] = 0;
    t->p[P_E6] = (int16_t)osc2;
    t->p[P_E7] = 0;
    t->p[P_FTYPE] = (int16_t)ft;
    return t;
}

static uint32_t osc2_step(int ct)
{
    uint32_t k;
    for (k = 0; k < VOID_OSC2_N; k++)
        if (VOID_OSC2_CT[k] == ct)
            return k;
    return VOID_OSC2_ZERO;
}

/* frequency of rec[a .. b) by its rising zero crossings (interpolated) */
static double freq_of(uint32_t a, uint32_t b)
{
    double first = -1, last = -1;
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        if (rec[i - 1u] < 0 && rec[i] >= 0) {
            double x = (double)(i - 1u) + (double)(-rec[i - 1u]) / (double)(rec[i] - rec[i - 1u]);
            if (first < 0)
                first = x;
            last = x;
            n++;
        }
    return n > 1u ? (double)(n - 1u) * FS / (last - first) : 0.0;
}
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

/* ------------------------------------------------------------------ 1 --- */
static void test_engine(void)
{
    const engine_t *e = ENGINES[ENGI_WAVE];
    check("engine 1 is WAVE, two EDIT pages of four (TBL1 POS TBL2 MIX, CUT RES OSC2 DRV), presets",
          str_eq(e->name, "WAVE") && str_eq(e->edit[0].label, "TBL1") && str_eq(e->edit[4].label, "CUT") &&
          str_eq(e->edit[6].label, "OSC2") && str_eq(e->edit[7].label, "DRV") && e->npresets >= 5u && e->mem);
}

/* ------------------------------------------------------------------ 2 --- */
static void test_pitch(void)
{
    double f;
    track_t *t = plain(0, 0, 0, VOID_OSC2_ZERO, 3);
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    f = freq_of(FS / 4u, NREC);
    printf("void: SINE A4 %.3f Hz (%+.2f cents)\n", f, cents(f, 440.0));
    check("pitch: SINE at A4 is 440 Hz within the plugin's movement (+-4 cents)", fabs(cents(f, 440.0)) < 4.0);
    t = plain(0, 0, 127, osc2_step(1200), 3);
    trk_note_on(t, 57, 100);
    blocks(NREC / CTL);
    f = freq_of(FS / 4u, NREC);
    check("pitch: OSC2 +12 alone (MIX 127 = 254 / 256 of it): an octave up, 440 Hz from A3", fabs(cents(f, 440.0)) < 4.0);
    t = plain(0, 0, 127, osc2_step(-12), 3);
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    f = freq_of(FS / 4u, NREC);
    printf("void: OSC2 -12c at A4: %.3f Hz (%+.2f cents)\n", f, cents(f, 440.0));
    check("pitch: OSC2 -12c: 12 cents below (+-4)", fabs(cents(f, 440.0) + 12.0) < 4.0);
}

/* ------------------------------------------------------------------ 3 --- */
/* the share of rec[a, a + n) that is not within +-3 bins of a harmonic of f0 (Hann window, DFT on harmonics) */
static double alias_db(uint32_t a, uint32_t n, double f0)
{
    double tot = 0, harm = 0;
    uint32_t i, k;
    static double w[16384];
    for (i = 0; i < n; i++)
        w[i] = rec[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / n));
    for (k = 1; k < n / 2u; k++) {
        double re = 0, im = 0, fk = (double)k * FS / n, h = fk / f0, p;
        for (i = 0; i < n; i++) {
            re += w[i] * cos(2 * M_PI * k * i / n);
            im -= w[i] * sin(2 * M_PI * k * i / n);
        }
        p = re * re + im * im;
        tot += p;
        if (fabs(h - floor(h + 0.5)) * f0 < 3.0 * FS / n && h > 0.5)
            harm += p;
    }
    return 10 * log10((tot - harm) / tot + 1e-12);
}

static void test_bandlimit(void)
{
    static const struct { uint32_t tb; int pos; const char *nm; } C[3] = {{1, 0, "SAW"}, {2, 0, "SQUARE"}, {3, 127, "SPECTRAL top"}};
    uint32_t k;
    double worst = -999;
    char msg[160];
    for (k = 0; k < 3u; k++) {
        track_t *t = plain(C[k].tb, 0, 0, VOID_OSC2_ZERO, 3);
        double d;
        t->p[P_E1] = (int16_t)C[k].pos;
        trk_note_on(t, 108, 100);
        blocks(FS / 2u / CTL);
        d = alias_db(FS / 4u, 4096, 4186.009);
        printf("void: %s at C8: %.1f dB off the harmonics\n", C[k].nm, d);
        worst = d > worst ? d : worst;
    }
    snprintf(msg, sizeof msg, "band-limited: SAW, SQUARE, SPECTRAL (top) at C8: off-harmonic energy below -30 dB (%.1f)", worst);
    check(msg, worst < -30.0);
}

/* ------------------------------------------------------------------ 4 --- */
static void test_bounded(void)
{
    static const uint8_t NOTE[3] = {24, 60, 108};
    uint32_t tb, ft, c, nt, wraps = 0, worst_dc = 0, silent = 0, runs = 0;
    for (tb = 0; tb < VOID_NTBL; tb++)
        for (ft = 0; ft < 6u; ft++)
            for (c = 0; c < 4u; c++)
                for (nt = 0; nt < 3u; nt++) {
                    track_t *t = plain(tb, (tb + 4u) % VOID_NTBL, 64, osc2_step(700), ft);
                    int64_t sum = 0;
                    uint32_t i, dc;
                    t->p[P_E5] = 127;
                    t->p[P_E7] = 127;
                    t->p[P_E4] = (c & 1u) ? 127 : 0;
                    t->p[P_E1] = (c & 2u) ? 127 : 0;
                    trk_note_on(t, NOTE[nt], 127);
                    trk_note_on(t, NOTE[nt] + 7u, 127);
                    blocks(FS / 2u / CTL);
                    for (i = 1; i < nrec; i++) {
                        int32_t d = rec[i] - rec[i - 1u];
                        wraps += d > 6 * VOICE_FS || d < -6 * VOICE_FS;
                        if (i >= nrec / 2u)                /* settled (the master's DC blocker: the plugin's too) */
                            sum += rec[i];
                    }
                    dc = (uint32_t)llabs(sum / (int64_t)(nrec - nrec / 2u));
                    if ((c & 1u) && nt)                    /* (CUT 0 or C1: a few cycles of ~30 Hz in the window, not DC) */
                        worst_dc = dc > worst_dc ? dc : worst_dc;
                    silent += (c & 1u) && ft != 2u && ft != 4u && nrec && rec[nrec / 2u] == 0 && rec[nrec / 3u] == 0;
                    runs++;
                }
    printf("void: corners: %u runs, %u wrapped samples, worst DC %u, %u silent\n", runs, wraps, worst_dc, silent);
    check("bounded: every table x TYPE at RES and DRV 127, CUT 0 / 127, POS 0 / 127, C1 / C4 / C8: no wrapped sample",
          wraps == 0u);
    check("bounded: no DC at C4 / C8, CUT 127 (under 2 % of a voice's full scale)", worst_dc < VOICE_FS / 50u);
    {   /* TYPE and table changes while notes sound */
        track_t *t = plain(1, 3, 64, osc2_step(1200), 0);
        uint32_t i, k;
        int32_t pk = 0;
        t->p[P_E5] = 120;
        trk_note_on(t, 48, 127);
        trk_note_on(t, 55, 127);
        for (k = 0; k < 24u; k++) {
            t->p[P_FTYPE] = (int16_t)(k % 6u);
            t->p[P_E0] = (int16_t)(k % VOID_NTBL);
            t->p[P_E1] = (int16_t)(k * 11u % 128u);
            blocks(40);
        }
        for (i = 1; i < nrec; i++) {
            int32_t d = rec[i] - rec[i - 1u];
            wraps += d > 6 * VOICE_FS || d < -6 * VOICE_FS;
            pk = abs(rec[i]) > pk ? abs(rec[i]) : pk;
        }
        check("bounded: TYPE, table and POS changes while notes sound: no wrapped sample", wraps == 0u && pk > 0);
    }
}

/* ------------------------------------------------------------------ 5 --- */
/* the engine's own output for 2 s of a held note: what mix_part hands the sends, so the master's DC blocker
 * (fx.c dc_block, after the sends are taken) is not in the way. A preset's position stays where it is. */
static double engine_dc(uint32_t pi, uint32_t note)
{
    static int32_t part[CTL];
    track_t *t = &trk[0];
    int64_t sum = 0;
    int32_t pk = 0;
    uint32_t i, b;
    fresh(ENGI_WAVE, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    trk_note_on(t, note, 100);
    for (b = 0; b < 2u * FS / CTL; b++) {
        int on = mod_begin(t);                         /* (the matrix, as mix_part does) */
        spread_on = 0;
        for (i = 0; i < CTL; i++)
            part[i] = 0;
        track_render(t, part, CTL);
        if (on)
            mod_end(t);
        for (i = 0; i < CTL; i++) {
            int32_t v = part[i];
            sum += v;
            pk = (v < 0 ? -v : v) > pk ? (v < 0 ? -v : v) : pk;
        }
    }
    return pk ? fabs((double)sum / (double)(2u * FS)) / (double)pk : 0.0;
}

static void test_dc(void)
{
    uint32_t k, pi = 0;
    double r;
    char msg[160];
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++)
        if (str_eq(ENGINES[ENGI_WAVE]->presets[k].name, "BRASS LEAD"))
            pi = k;
    r = engine_dc(pi, 60);
    printf("void: BRASS LEAD held 2 s: |mean| / peak %.5f\n", r);
    snprintf(msg, sizeof msg, "DC: BRASS LEAD's engine output, |mean| under 1 %% of the peak (%.5f)", r);
    check(msg, r < 0.01);
}

/* The soft clipper reads VOID_SOFT[k] and [k + 1] for k = (|y| - VOID_CLIP_THR) >> 8. Up to Task 4.2 the clamp
 * at VOID_OUT_LEVEL kept |y| at 62216 and that index in range on its own; the DC blocker sits after it and has
 * a peak gain of 2, so its own recurrence leaves that range and void_dc clamps its return to VOID_CLIP_MAX.
 * Both halves are pinned here: the recurrence does overshoot (so the clamp is not decoration), and what
 * void_dc returns stays inside the table (so dropping the clamp fails here instead of reading past VOID_SOFT).
 * The input is held at -full scale and then at +full scale: the sequence that reaches the l1 norm of the
 * impulse response (1, then -(1 - R) R^n), which is the blocker's worst-case peak gain. */
static void test_dc_range(void)
{
    int32_t full = mulq15(110000, VOID_OUT_LEVEL), w = 0, raw = 0, wr = 0, got = 0;
    uint32_t i;
    char msg[200];
    for (i = 0; i < 3u * FS; i++) {
        int32_t x = i < 2u * FS ? -full : full;
        int32_t y = void_dc(&w, x), u;
        got = (y < 0 ? -y : y) > got ? (y < 0 ? -y : y) : got;
        u = x + (wr >> VOID_DC_SH);                    /* the same recurrence, unclamped */
        wr -= (u * (32768 - VOID_DC_R)) >> (15 - VOID_DC_SH);
        raw = (u < 0 ? -u : u) > raw ? (u < 0 ? -u : u) : raw;
    }
    printf("void: the DC blocker at +-%d: worst |y| %d unclamped, %d as returned (VOID_CLIP_MAX %d)\n",
           full, raw, got, VOID_CLIP_MAX);
    snprintf(msg, sizeof msg, "DC: the blocker's recurrence overshoots the clipper's table (|y| %d > %d)",
             raw, VOID_CLIP_MAX);
    check(msg, raw > VOID_CLIP_MAX);
    snprintf(msg, sizeof msg,
             "DC: what void_dc returns stays inside the clipper's table (%d <= VOID_CLIP_MAX %d, VOID_SOFT[%u])",
             got, VOID_CLIP_MAX, (unsigned)NELEM(VOID_SOFT));
    check(msg, got <= VOID_CLIP_MAX && (uint32_t)((VOID_CLIP_MAX - VOID_CLIP_THR) >> 8) + 1u < NELEM(VOID_SOFT));
}

/* ------------------------------------------------------------------ 6 --- */
static double phrase_rms(uint32_t e, uint32_t pi)
{
    track_t *t = &trk[0];
    double s = 0;
    uint32_t i;
    fresh(e, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    trk_note_on(t, 48, 100);
    trk_note_on(t, 55, 100);
    blocks(FS / CTL);
    trk_note_off(t, 48);
    trk_note_off(t, 55);
    {                                                  /* the loudest 0.2 s window (steps of 10 ms) */
        double best = 0;
        uint32_t w;
        for (w = 0; w + FS / 5u <= FS; w += FS / 100u) {
            s = 0;
            for (i = w; i < w + FS / 5u; i++)
                s += (double)rec[i] * rec[i];
            best = s > best ? s : best;
        }
        return sqrt(best / (FS / 5u));
    }
}

static void test_presets(void)
{
    uint32_t k, ok = 1, a = 0;
    double ref;
    for (k = 0; k < ENGINES[0]->npresets; k++)
        if (str_eq(ENGINES[0]->presets[k].name, "SAW LEAD"))
            a = k;
    ref = phrase_rms(0, a);
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        double r = phrase_rms(ENGI_WAVE, k), db = 20 * log10(r / ref + 1e-12);
        printf("void: preset %-12s %+5.1f dB against OB-Z SAW LEAD\n", ENGINES[ENGI_WAVE]->presets[k].name, db);
        ok &= db > -18.0 && db < 6.0;
    }
    check("presets: each sounds, its loudest 0.2 s within -18 .. +6 dB of OB-Z's SAW LEAD", ok);
}

/* ------------------------------------------------------------------ 7 --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static double cost(uint32_t e, uint32_t pi)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = &trk[0];
    uint32_t i, rep;
    double best = 1e30;
    fresh(e, pi);
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    for (i = 0; i < 8u; i++)
        trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}
static void test_cost(void)
{
    double a, worst = 0;
    uint32_t k, e, pw = 0, ph = 0;
    char msg[200];
    if (!instr_now()) {
        printf("void: cost: no instruction counter on this host, skipped\n");
        return;
    }
    for (e = 0; e < NENGINES; e++)                     /* OB-Z RAVE: the reference (PHASE WIRE's cost, retired) */
        if (str_eq(ENGINES[e]->name, "OB-Z"))
            for (k = 0; k < ENGINES[e]->npresets; k++)
                if (str_eq(ENGINES[e]->presets[k].name, "RAVE"))
                    ph = e, pw = k;
    a = cost(ph, pw);
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {   /* (cost() asks for 8 notes: the cap plays VOID_POLY) */
        double c = cost(ENGI_WAVE, k);
        printf("void: cost: %-12s %u voices %.0f instructions / sample (OB-Z RAVE, 8: %.0f; %.2f x)\n",
               ENGINES[ENGI_WAVE]->presets[k].name, VOID_POLY, c, a, c / a);
        worst = c / a > worst ? c / a : worst;
    }
    /* 1.75, not the 1.25 of before Task 4.1: WAVE costs what it did, but the yardstick moved. OB-Z
     * RAVE lost its drive and noise there, from 1765 instructions a sample at 8 voices to about
     * 1130, so the same WAVE presets now read 1.56 x of it instead of 1.18 x. */
    snprintf(msg, sizeof msg, "cost: any preset at its cap (%u voices) within 1.75 x 8 of OB-Z RAVE (%.2f x)", VOID_POLY, worst);
    check(msg, worst < 1.75);
}

/* ------------------------------------------------------------------ 8 --- */
static void wav16(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i, bytes = nrec * 2u;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fwrite("RIFF", 1, 4, f);
    i = 36u + bytes; fwrite(&i, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    i = 16; fwrite(&i, 4, 1, f);
    { uint16_t h[2] = {1, 1}; fwrite(h, 2, 2, f); }
    i = FS; fwrite(&i, 4, 1, f);
    i = FS * 2u; fwrite(&i, 4, 1, f);
    { uint16_t h[2] = {2, 16}; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    for (i = 0; i < nrec; i++) {
        int32_t v = rec[i];                            /* (mix_block's output is 16-bit scale, as hostsim's WAVs) */
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}
static void demos(const char *dir)
{
    static const uint8_t PHR[8] = {36, 43, 48, 36, 39, 46, 51, 39};
    uint32_t k, i, tb;
    char nm[64];
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        track_t *t = &trk[0];
        fresh(ENGI_WAVE, k);
        void_track_loaded(t);
        for (i = 0; i < (t->p[P_ATK] > 80 ? 2u : 8u); i++) {   /* (a slow attack: two long chords) */
            trk_note_on(t, PHR[i] + (t->p[P_VOICE] == V_POLY ? 12u : 0u), 100);
            if (t->p[P_VOICE] == V_POLY)
                trk_note_on(t, PHR[i] + 19u, 90);
            blocks((t->p[P_ATK] > 80 ? FS : FS / 4u) / CTL);
            trk_note_off(t, PHR[i] + (t->p[P_VOICE] == V_POLY ? 12u : 0u));
            trk_note_off(t, PHR[i] + 19u);
        }
        snprintf(nm, sizeof nm, "preset_%02u_%s", k, ENGINES[ENGI_WAVE]->presets[k].name);
        for (i = 0; nm[i]; i++)
            if (nm[i] == ' ')
                nm[i] = '_';
        wav16(dir, nm);
    }
    for (tb = 0; tb < VOID_NTBL; tb++) {
        track_t *t = plain(tb, 0, 0, VOID_OSC2_ZERO, 3);
        trk_note_on(t, 48, 100);
        for (i = 0; i < NREC / CTL; i++) {
            t->p[P_E1] = (int16_t)(i * 127u / (NREC / CTL));
            blocks(1);
        }
        snprintf(nm, sizeof nm, "sweep_%s", N_VOID_TBL[tb]);
        wav16(dir, nm);
    }
    printf("void: demos in %s\n", dir);
}

/* ------------------------------------------------------------------ 9 --- */
/* |X| at harmonic h of f0 over x[0, n) (n a whole number of cycles), and the RMS */
static double harm_mag(const double *x, uint32_t n, double f0, uint32_t h)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double a = 2 * M_PI * h * f0 * i / VOID_REF_FS;
        re += x[i] * cos(a);
        im -= x[i] * sin(a);
    }
    return 2 * sqrt(re * re + im * im) / n;
}
static double rms_of(const double *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += x[i] * x[i];
    return sqrt(s / n);
}

static void test_reference(const char *dir)
{
    static const char *const VF_N[6] = {"LP24", "BP", "HP", "LP12", "HP24", "NOTCH"};
    static float ref[VOID_REF_N];
    static double a[VOID_REF_N / 2], b[VOID_REF_N / 2];
    uint32_t k, n = VOID_REF_N / 2, have = 0, ok_lvl = 1, ok_h = 1;
    double worst_lvl = 0, worst_h = 0;
    char msg[200];
    for (k = 0; k < VOID_REF_NCFG; k++) {
        const void_ref_cfg_t *c = &VOID_REF_CFG[k];
        char path[512];
        FILE *f;
        void_flt_t fl;
        int32_t st[4] = {0, 0, 0, 0}, cut;
        uint32_t i, h, hn;
        double dl, dh = 0, top = 0;
        snprintf(path, sizeof path, "%s/%02u.f32", dir, k);
        if (!(f = fopen(path, "rb")))
            continue;
        if (fread(ref, sizeof(float), VOID_REF_N, f) != VOID_REF_N) {
            fclose(f);
            continue;
        }
        fclose(f);
        have++;
        cut = (int32_t)lround(256.0 * 127.0 * log(c->hz / 30.0) / log(16000.0 / 30.0));
        void_flt_set(&fl, (uint32_t)c->vf, clamp(cut, 0, 127 << 8), (int32_t)lround(c->res * 127),
                     (int32_t)lround(c->drv * 127), 0);
        for (i = 0; i < VOID_REF_N; i++) {
            int32_t y = void_flt_run(&fl, st, (int32_t)lround(void_ref_input(i, c->f0) * 32768.0));
            if (i >= VOID_REF_N - n) {
                a[i - (VOID_REF_N - n)] = ref[i];
                b[i - (VOID_REF_N - n)] = y / 32768.0;
            }
        }
        dl = 20 * log10(rms_of(b, n) / rms_of(a, n));
        hn = (uint32_t)(18000.0 / c->f0);
        if (hn > 40u)
            hn = 40u;
        for (h = 1; h <= hn; h++) {
            double m = harm_mag(a, n, c->f0, h);
            top = m > top ? m : top;
        }
        for (h = 1; h <= hn; h++) {
            double ma = harm_mag(a, n, c->f0, h), mb = harm_mag(b, n, c->f0, h), d;
            if (ma < top * 0.00316)                     /* within 50 dB of the loudest */
                continue;
            d = fabs(20 * log10((mb + 1e-12) / ma));
            dh = d > dh ? d : dh;
        }
        printf("void: vs plugin %-5s %5.0f Hz res %.1f drv %.1f f0 %3.0f: level %+5.2f dB, harmonics within %.2f dB\n",
               VF_N[c->vf], c->hz, c->res, c->drv, c->f0, dl, dh);
        worst_lvl = fabs(dl) > worst_lvl ? fabs(dl) : worst_lvl;
        if (c->res < 0.85f) {                           /* (self-oscillation: its own pitch, level only) */
            worst_h = dh > worst_h ? dh : worst_h;
            ok_h &= dh < 1.5;
        }
        ok_lvl &= fabs(dl) < 0.5;
    }
    if (!have) {
        printf("void: vs plugin: no reference in %s (tools/void_ref.cpp needs ../VoidWave), skipped\n", dir);
        return;
    }
    snprintf(msg, sizeof msg, "vs the plugin's Filter.cpp: %u cases, levels within 0.5 dB (worst %.2f)", have, worst_lvl);
    check(msg, ok_lvl);
    snprintf(msg, sizeof msg, "vs the plugin's Filter.cpp: harmonics within 1.5 dB below self-oscillation (worst %.2f)", worst_h);
    check(msg, ok_h);
}

static double costv(int mix, int ft, int drv, int res, int pos)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = plain(1, 1, (uint32_t)mix, VOID_OSC2_ZERO + 3, (uint32_t)ft);
    uint32_t i, rep; double best = 1e30;
    t->p[P_E7] = (int16_t)drv; t->p[P_E5] = (int16_t)res; t->p[P_E4] = 80; t->p[P_E1] = (int16_t)pos;
    for (i = 0; i < 8u; i++) trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) { uint64_t i0 = instr_now(); double ipc; blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u); best = ipc < best ? ipc : best; }
    return best;
}
static double idle_cost(void)
{
    uint32_t rep; double best = 1e30;
    fresh(0, 0);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) { uint64_t i0 = instr_now(); double ipc; blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u); best = ipc < best ? ipc : best; }
    return best;
}
int prof_main(void)
{
    double id = idle_cost();
    printf("idle %.0f\n", id);
    printf("LP12 mix0 drv0       %.0f\n", costv(0, 3, 0, 0, 0) - id);
    printf("LP12 mix0 drv0 pos40 %.0f\n", costv(0, 3, 0, 0, 40) - id);
    printf("LP12 mix64           %.0f\n", costv(64, 3, 0, 0, 0) - id);
    printf("LP24 mix0            %.0f\n", costv(0, 0, 0, 0, 0) - id);
    printf("LP24 mix64           %.0f\n", costv(64, 0, 0, 0, 0) - id);
    printf("LP24 mix64 drv40     %.0f\n", costv(64, 0, 40, 0, 0) - id);
    printf("LP12 mix64 res110    %.0f\n", costv(64, 3, 0, 110, 0) - id);
    printf("OB-Z SAW LEAD        %.0f\n", cost(0, 0) - id);
    {
        static const char *const W[][2] = {{"OB-Z", "SUB BASS"}, {"OB-Z", "RAVE"}};
        uint32_t k, e, q;
        for (k = 0; k < NELEM(W); k++)
            for (e = 0; e < NENGINES; e++)
                if (str_eq(ENGINES[e]->name, W[k][0]))
                    for (q = 0; q < ENGINES[e]->npresets; q++)
                        if (str_eq(ENGINES[e]->presets[q].name, W[k][1]))
                            printf("%-7s %-12s %.0f\n", W[k][0], W[k][1], cost(e, q) - id);
        for (q = 0; q < ENGINES[ENGI_WAVE]->npresets; q++)
            printf("WAVE    %-12s %.0f\n", ENGINES[ENGI_WAVE]->presets[q].name, cost(ENGI_WAVE, q) - id);
    }
    return 0;
}
int main(int argc, char **argv)
{
        if (getenv("VOID_PROF"))                           /* VOID_PROF=1: the cost by feature, and next to the heaviest */
        return prof_main();                            /* presets of the other engines */
    test_engine();
    test_pitch();
    test_bandlimit();
    test_bounded();
    test_dc();
    test_dc_range();
    test_presets();
    test_cost();
    test_reference(argc > 2 ? argv[2] : "build/void_ref");
    if (argc > 1)
        demos(argv[1]);
    printf(bad ? "void: %d FAILED\n" : "void: all ok\n", bad);
    return bad != 0;
}

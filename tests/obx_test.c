/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* obx_test.c: OB-Z's filter morph (MODE), its 4-pole option and SLOP, measured on one voice.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/obx_test tests/obx_test.c -lm
 *
 * Every render here drives the engine directly (obx_voice below): the engine's own note_on and
 * render with a hand-built vmod_t, so no envelope, sequencer or FX stands between the test and the
 * oscillators and the filter. Spectra come from tests/dspfft.h (Hann window, fft_mag / band_db).
 *
 * The invariant the morph has to hold: MODE 0 with SLOP 0 is the low-pass OB-Z always had, step for
 * step. test_mode0_legacy_lp pins that with hashes taken from the engine before the morph existed. */
/* the seams eng_obx.c leaves around the 4-pole's second-stage clamp. They only observe: the engine's
 * own clamp runs between them, unmodified. p4_pre sees the value before it (and can scale it, to drive
 * stage 1 past the bound so that the clamp has work to do); p4_seen sees what the clamp produced, and
 * returns it untouched. OBX_P4_IN (eng_obx.c) can be overridden on the command line (-DOBX_P4_IN=...)
 * to run this test against another bound. */
#include <stdint.h>
static int32_t p4_pre(int32_t y);
static int32_t p4_seen(int32_t x, int32_t y, const void *c, int32_t ic1, int32_t ic2);
#define OBX_P4_PRE(y) p4_pre(y)
#define OBX_P4_SEEN(x, y, c, i1, i2) p4_seen((x), (y), (c), (i1), (i2))
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "dspfft.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- render --- */
#define REN_SKIP 4096u                    /* dropped: the filter's start transient */
#define REN_N 32768u                      /* the transform length (0.74 s at 44.1 kHz) */
static int32_t ren_a[REN_SKIP + REN_N], ren_b[REN_SKIP + REN_N];
static float mag_a[REN_N / 2u], mag_b[REN_N / 2u];

/* one OB-Z voice of note-on counter `age` at `pitch16` (1/16 semitone), n samples into out[] */
static void obx_voice(const int16_t e[8], int32_t pitch16, uint32_t age, int32_t *out, uint32_t n)
{
    static track_t t;
    static voice_t v;
    vmod_t m;
    uint32_t i;
    memset(&t, 0, sizeof t);
    memset(&v, 0, sizeof v);
    for (i = 0; i < 8u; i++)
        t.p[P_E0 + i] = e[i];
    t.engine = (uint8_t)ENGI_OBX;
    v.age = age;
    v.pitch16 = pitch16;
    v.note = (uint8_t)(pitch16 / 16);
    v.vel = 100;
    v.active = 1;
    v.stage = 1;
    ENGINES[ENGI_OBX]->note_on(&t, &v);
    m.inc = pitch_inc(pitch16);
    m.pitch16 = pitch16;
    m.amp0 = m.amp1 = 32767;              /* the amplitude held open: no envelope in the way */
    m.cutoff = 0;
    m.shape = 64 << 8;
    m.envq15 = 0;
    m.fine = 0;
    memset(out, 0, n * sizeof *out);
    for (i = 0; i + CTL <= n; i += CTL)
        ENGINES[ENGI_OBX]->render(&t, &v, out + i, CTL, &m);
}

static uint64_t ren_hash(const int32_t *x, uint32_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < 4u; k++) {
            h ^= ((uint32_t)x[i] >> (8u * k)) & 0xFFu;
            h *= 0x100000001B3ull;
        }
    return h;
}

/* ------------------------------------------------------------- measuring --- */
static int32_t pitch16_of_hz(double hz)                /* the nearest 1/16 semitone */
{
    return (int32_t)floor(16.0 * (69.0 + 12.0 * log2(hz / 440.0)) + 0.5);
}
static int32_t cut_for_hz(uint32_t hz)                 /* the CUT step whose CUTOFF_HZ is nearest */
{
    uint32_t n, best = 0;
    for (n = 1; n < 128u; n++)
        if (labs((long)CUTOFF_HZ[n] - (long)hz) < labs((long)CUTOFF_HZ[best] - (long)hz))
            best = n;
    return (int32_t)best;
}
static double cents(double a, double b) { return 1200.0 * log2(a / b); }

/* the strongest peak in [lo_hz, hi_hz], its bin refined by a parabolic fit over its neighbours */
static double peak_hz(const float *mag, unsigned n, double fs, double lo_hz, double hi_hz)
{
    unsigned k, lo = (unsigned)ceil(lo_hz * n / fs), hi = (unsigned)floor(hi_hz * n / fs), best = 0;
    double d, den;
    if (hi >= n / 2u)
        hi = n / 2u - 1u;
    if (lo < 1u)
        lo = 1u;
    for (k = lo; k <= hi; k++)
        if (!best || mag[k] > mag[best])
            best = k;
    if (!best || best + 1u >= n / 2u)
        return 0.0;
    den = (double)mag[best - 1u] - 2.0 * mag[best] + mag[best + 1u];
    d = den != 0.0 ? 0.5 * ((double)mag[best - 1u] - mag[best + 1u]) / den : 0.0;
    return ((double)best + d) * fs / (double)n;
}

/* the two strongest local maxima in [lo_hz, hi_hz] that are at least `sep` bins apart, low first */
static int two_peaks(const float *mag, unsigned n, double fs, double lo_hz, double hi_hz, unsigned sep,
                     double *f1, double *f2)
{
    unsigned k, lo = (unsigned)ceil(lo_hz * n / fs), hi = (unsigned)floor(hi_hz * n / fs);
    unsigned p1 = 0, p2 = 0;
    if (hi >= n / 2u - 1u)
        hi = n / 2u - 2u;
    if (lo < 1u)
        lo = 1u;
    for (k = lo; k <= hi; k++) {                       /* the strongest local maximum */
        if (!(mag[k] > mag[k - 1u] && mag[k] >= mag[k + 1u]))
            continue;
        if (!p1 || mag[k] > mag[p1])
            p1 = k;
    }
    if (!p1)
        return 0;
    for (k = lo; k <= hi; k++) {                       /* the strongest one `sep` bins away from it */
        if (!(mag[k] > mag[k - 1u] && mag[k] >= mag[k + 1u]))
            continue;
        if (k + sep > p1 && p1 + sep > k)
            continue;
        if (!p2 || mag[k] > mag[p2])
            p2 = k;
    }
    if (!p2)
        return 0;
    {
        double a = peak_hz(mag, n, fs, (p1 - 1u) * fs / n, (p1 + 1u) * fs / n);
        double b = peak_hz(mag, n, fs, (p2 - 1u) * fs / n, (p2 + 1u) * fs / n);
        *f1 = a < b ? a : b;
        *f2 = a < b ? b : a;
    }
    return 1;
}

/* ------------------------------------------------------------ the checks --- */
/* MODE 0 / SLOP 0 is the low-pass as it was: hashes of two renders taken from the engine before the
 * morph was written (WAVE SAW through analog_osc, WAVE SUB through the SYNC / SUB render). Both use
 * E3 / E6 / E7 at 0, the one setting where the old NOIS / DRV / KTR and the new PW / MODE / SLOP
 * agree, so the two engines have to produce the same samples bit for bit. */
#define LEGACY_SAW_HASH 0xe7eab594be38dbe8ull
#define LEGACY_SUB_HASH 0x27ab739cb15431f7ull
static void test_mode0_legacy_lp(void)
{
    static const int16_t SAW[8] = {0, 7, 40, 0, 70, 90, 0, 0};
    static const int16_t SUB[8] = {6, 9, 100, 0, 80, 40, 0, 0};
    uint64_t h;
    obx_voice(SAW, 45 * 16, 1, ren_a, REN_SKIP + REN_N);
    h = ren_hash(ren_a, REN_SKIP + REN_N);
    printf("obx: MODE 0 SAW hash %016llx\n", (unsigned long long)h);
    CHECK(h == LEGACY_SAW_HASH, "MODE 0 SAW is not the legacy LP: %016llx (want %016llx)",
          (unsigned long long)h, (unsigned long long)LEGACY_SAW_HASH);
    obx_voice(SUB, 45 * 16, 1, ren_a, REN_SKIP + REN_N);
    h = ren_hash(ren_a, REN_SKIP + REN_N);
    printf("obx: MODE 0 SUB hash %016llx\n", (unsigned long long)h);
    CHECK(h == LEGACY_SUB_HASH, "MODE 0 SUB is not the legacy LP: %016llx (want %016llx)",
          (unsigned long long)h, (unsigned long long)LEGACY_SUB_HASH);
}

/* a 100 Hz saw through MODE `mode` at CUT `cut`, its spectrum in mag_a[] */
static void saw_spectrum(int32_t mode, int32_t cut, int32_t res)
{
    int16_t e[8] = {0, 0, 0, 64, 0, 0, 0, 0};
    e[4] = (int16_t)cut;
    e[5] = (int16_t)res;
    e[6] = (int16_t)mode;
    obx_voice(e, pitch16_of_hz(100.0), 1, ren_a, REN_SKIP + REN_N);
    fft_mag(ren_a + REN_SKIP, REN_N, mag_a);
}

static void test_mode_lp(void)
{
    float lo, hi;
    saw_spectrum(0, cut_for_hz(1000), 0);
    lo = band_db(mag_a, REN_N, (float)FS, 400.0f, 600.0f);
    hi = band_db(mag_a, REN_N, (float)FS, 3500.0f, 4500.0f);
    printf("obx: MODE 0   CUT %u Hz   400-600 %.1f dB   3500-4500 %.1f dB (%.1f dB down)\n",
           CUTOFF_HZ[cut_for_hz(1000)], lo, hi, lo - hi);
    CHECK(hi <= lo - 20.0f, "MODE 0 (LP): 3500-4500 only %.1f dB below 400-600", lo - hi);
}

/* The high-pass is measured at a cutoff near 11 kHz, not at the LP test's 1 kHz: a 100 Hz saw puts
 * about 13 dB more energy into 400-600 Hz than into 3500-4500 Hz to start with (its partials fall
 * as 1/n while the upper band holds eleven of them), so a two-pole high-pass needs the three
 * octaves between the bands to be well inside its stop band before the inequality can turn over. */
static void test_mode_hp(void)
{
    float lo, hi;
    saw_spectrum(64, cut_for_hz(11000), 0);
    lo = band_db(mag_a, REN_N, (float)FS, 400.0f, 600.0f);
    hi = band_db(mag_a, REN_N, (float)FS, 3500.0f, 4500.0f);
    printf("obx: MODE 64  CUT %u Hz   400-600 %.1f dB   3500-4500 %.1f dB (%.1f dB down)\n",
           CUTOFF_HZ[cut_for_hz(11000)], lo, hi, hi - lo);
    CHECK(lo <= hi - 20.0f, "MODE 64 (HP): 400-600 only %.1f dB below 3500-4500", hi - lo);
}

static void test_mode_notch(void)
{
    int32_t cut = cut_for_hz(1000);
    double fc = CUTOFF_HZ[cut];
    float at, below, above, nb;
    saw_spectrum(32, cut, 0);
    at = band_db(mag_a, REN_N, (float)FS, (float)(fc * 0.97), (float)(fc * 1.03));
    below = band_db(mag_a, REN_N, (float)FS, (float)(fc * 0.485), (float)(fc * 0.515));
    above = band_db(mag_a, REN_N, (float)FS, (float)(fc * 1.94), (float)(fc * 2.06));
    nb = 0.5f * (below + above);
    printf("obx: MODE 32  CUT %u Hz   at %.1f dB   octave below %.1f   above %.1f   (%.1f dB down)\n",
           CUTOFF_HZ[cut], at, below, above, nb - at);
    CHECK(at <= nb - 15.0f, "MODE 32 (NOTCH): the cutoff is only %.1f dB below its neighbouring octaves",
          nb - at);
}

static void test_mode_4pole(void)
{
    int32_t cut = cut_for_hz(1000);
    double fc = CUTOFF_HZ[cut];
    float two, four;
    saw_spectrum(0, cut, 0);
    two = band_db(mag_a, REN_N, (float)FS, (float)(fc * 3.5), (float)(fc * 4.5));
    saw_spectrum(127, cut, 0);
    four = band_db(mag_a, REN_N, (float)FS, (float)(fc * 3.5), (float)(fc * 4.5));
    printf("obx: MODE 127 CUT %u Hz   4x cutoff: 2-pole %.1f dB   4-pole %.1f dB (%.1f dB further down)\n",
           CUTOFF_HZ[cut], two, four, two - four);
    CHECK(four <= two - 10.0f, "MODE 127 (4-pole): only %.1f dB further down at 4x the cutoff", two - four);
}

/* ------------------------------------------ the morph between its corners --- */
/* Each corner above is a single tap of the filter. Everything between two of them runs the
 * crossfade (obx_tap's OBX_T_MIX), which no corner test, no factory preset and so neither
 * tests/golden.txt nor tests/cpu_baseline.txt ever reaches. Each leg of the morph moves one band
 * one way from end to end, so a walk of its inside has to land between the two corner figures: a
 * leg that went inert, or that steps the wrong way, fails here.
 *
 * The legs and the band each one moves, at a 1 kHz cutoff over a 100 Hz saw:
 *   0..32    LP -> NOTCH   the notch at the cutoff deepens as the high-pass weight comes in
 *   32..64   NOTCH -> HP   the fundamental falls as the low-pass weight leaves
 *   64..96   HP -> BP      the band two octaves above the cutoff falls as the band-pass takes over
 *   96..120  BP -> LP      the fundamental comes back as the morph closes onto the low-pass
 *
 * The fundamental, a decade below the cutoff, measures the passband gain the crossfade has there:
 * only the low-pass tap reaches that far down (the high-pass falls as f^2 and the band-pass as f),
 * so the figure is the low-pass weight itself and moves with it from end to end. Half an octave
 * below the cutoff would not do: there the notch the crossfade makes between lp and hp sweeps
 * down through the band on the way to HP, and the reading dips and comes back. */
static int32_t morph_cut;                              /* the CUT step the walks are measured at */
static double morph_fc;

static void morph_at(int32_t mode) { saw_spectrum(mode, morph_cut, 0); }

static double m_fund(void)                             /* the 100 Hz fundamental: the passband gain */
{
    return band_db(mag_a, REN_N, (float)FS, 80.0f, 130.0f);
}
static double m_high(void)                             /* two octaves above the cutoff */
{
    return band_db(mag_a, REN_N, (float)FS, (float)(morph_fc * 3.5), (float)(morph_fc * 4.5));
}
static double m_notch(void)                            /* the cutoff below its neighbouring octaves */
{
    double at = band_db(mag_a, REN_N, (float)FS, (float)(morph_fc * 0.97), (float)(morph_fc * 1.03));
    double below = band_db(mag_a, REN_N, (float)FS, (float)(morph_fc * 0.485), (float)(morph_fc * 0.515));
    double above = band_db(mag_a, REN_N, (float)FS, (float)(morph_fc * 1.94), (float)(morph_fc * 2.06));
    return 0.5 * (below + above) - at;
}

/* `mid` has to sit between the corners `a` and `b`, `dir` the way the figure moves with MODE */
static void morph_leg(const char *what, int32_t a, int32_t mid, int32_t b, double (*meas)(void),
                      double dir, double margin)
{
    double va, vm, vb;
    morph_at(a);
    va = meas();
    morph_at(mid);
    vm = meas();
    morph_at(b);
    vb = meas();
    printf("obx: morph %-12s MODE %3d %6.1f -> %3d %6.1f -> %3d %6.1f dB\n", what, a, va, mid, vm, b, vb);
    CHECK(dir * (vm - va) >= margin && dir * (vb - vm) >= margin,
          "MODE %d..%d (%s): %.1f -> %.1f -> %.1f dB does not move %.1f dB a step toward the next corner",
          a, b, what, va, vm, vb, margin);
}

static void test_mode_between(void)
{
    morph_cut = cut_for_hz(1000);
    morph_fc = CUTOFF_HZ[morph_cut];
    morph_leg("LP -> NOTCH", 0, 16, OBX_M_NOTCH, m_notch, +1.0, 1.0);
    morph_leg("NOTCH -> HP", OBX_M_NOTCH, 48, OBX_M_HP, m_fund, -1.0, 2.0);
    morph_leg("HP -> BP", OBX_M_HP, 80, OBX_M_BP, m_high, -1.0, 1.0);
}

/* MODE 96..120 was a flat band-pass plateau: 24 of the knob's steps did nothing. It now crossfades
 * the band-pass back to the low-pass, so the morph spans 0..120 whole and meets the 4-pole
 * low-pass above 120 where it ends. MODE 120 is then the low-pass corner exactly -- the same
 * single tap MODE 0 takes, sample for sample. */
static uint64_t saw_hash(int32_t mode, int32_t res)
{
    int16_t e[8] = {0, 0, 0, 64, 0, 0, 0, 0};
    e[4] = (int16_t)morph_cut;
    e[5] = (int16_t)res;
    e[6] = (int16_t)mode;
    obx_voice(e, pitch16_of_hz(100.0), 1, ren_b, REN_SKIP + REN_N);
    return ren_hash(ren_b, REN_SKIP + REN_N);
}

static void test_mode_bp_to_lp(void)
{
    uint64_t h[4];
    morph_leg("BP -> LP", OBX_M_BP, 108, OBX_M_4POLE - 1, m_fund, +1.0, 2.0);
    h[0] = saw_hash(OBX_M_BP, 0);
    h[1] = saw_hash(108, 0);
    h[2] = saw_hash(OBX_M_4POLE - 1, 0);
    h[3] = saw_hash(0, 0);
    printf("obx: MODE 96 %016llx  108 %016llx  120 %016llx  (MODE 0 %016llx)\n",
           (unsigned long long)h[0], (unsigned long long)h[1], (unsigned long long)h[2],
           (unsigned long long)h[3]);
    CHECK(h[0] != h[1] && h[1] != h[2], "MODE 96..120 is inert: 96 / 108 / 120 render alike");
    CHECK(h[2] == h[3], "MODE 120 is not the low-pass corner sample for sample: %016llx (MODE 0 %016llx)",
          (unsigned long long)h[2], (unsigned long long)h[3]);
}

/* ------------------------------------- the crossfade and the 4-pole, bounded --- */
/* The corner taps are measured bounded and free of DC by sound15_test (its ANALOG filter TYPE
 * section); the crossfade and the 4-pole had no counterpart. Both are walked here at RES 127: the
 * crossfade carries the widest arithmetic in the engine (obx_tap's OBX_T_MIX), and the 4-pole runs
 * two stages at the same k, so its resonant peak is the two-pole's squared.
 *
 * DC is held to the 5 % of rms sound15_test holds the corner taps to, but as an increment over the
 * same patch through MODE 0 rather than over zero. Neither path is free of offset and neither can
 * be: the morph's gain at DC is its low-pass weight, so it passes through whatever the oscillators
 * carry, and WAVE SYNC's hard-synced saw carries 10 % of rms at a low cutoff -- through the legacy
 * low-pass too, so it has nothing to do with the morph. The 4-pole, in turn, drives the soft knee
 * hard enough at RES 127 that clipping an asymmetric resonant waveform symmetrically leaves an
 * offset behind: 4.5 % of rms at the worst of these settings, where the pre-knee signal has 0.1 %
 * (probed). So what the morph and the 4-pole must not do is add DC of their own. */
#define OBX_CEIL 94866            /* the engine's own ceiling: soft_knee(.,16000) <= 32383, << 1 */
                                  /* through voice_amp at amp 32767 (x 24000 / 32768), << 1 */
#define BND_N 16384u
#define BND_DC_FLOOR 500.0        /* below this rms the DC check is skipped, and counted: a ratio of two small numbers.
                                   * 100 was tried and fails on a legitimate render -- WAVE SAW, CUT 40, note 88 sits at
                                   * rms 281 (0.3 % of the ceiling) with 225 of DC, 0.39 of its rms */
typedef struct { double mean, rms, peak; } stats_t;
static stats_t ren_stats(const int32_t *x, uint32_t n)
{
    stats_t s = {0.0, 0.0, 0.0};
    uint32_t i;
    for (i = 0; i < n; i++) {
        double v = (double)x[i];
        s.mean += v;
        s.rms += v * v;
        s.peak = fabs(v) > s.peak ? fabs(v) : s.peak;
    }
    s.mean /= (double)n;
    s.rms = sqrt(s.rms / (double)n);
    return s;
}

static stats_t bnd_render(const int16_t *wcn, int32_t mode)       /* {WAVE, CUT, note} at RES 127 */
{
    int16_t e[8] = {0, 11, 64, 64, 0, 127, 0, 0};
    e[0] = wcn[0];
    e[4] = wcn[1];
    e[6] = (int16_t)mode;
    obx_voice(e, wcn[2] * 16, 1, ren_a, REN_SKIP + BND_N);
    return ren_stats(ren_a + REN_SKIP, BND_N);
}

static void test_mode_bounded(void)
{
    static const int16_t MODES[] = {8, 16, 48, 80, 100, 108, 119, 121, 127};
    static const int16_t WAVES[] = {0, 1, 4, 5, 6};
    static const int32_t NOTES[] = {33, 60, 88};
    static const int16_t CUTS[] = {40, 90, 120};
    double worst = 0.0, dc = 0.0, quiet = 1e30;
    uint32_t w, md, nt, ct, bad = 0, n = 0, skipped = 0;
    for (w = 0; w < NELEM(WAVES); w++)
        for (ct = 0; ct < NELEM(CUTS); ct++)
            for (nt = 0; nt < NELEM(NOTES); nt++) {
                int16_t wcn[3] = {WAVES[w], CUTS[ct], (int16_t)NOTES[nt]};
                double dc0 = fabs(bnd_render(wcn, 0).mean);         /* what the legacy LP carries */
                for (md = 0; md < NELEM(MODES); md++) {
                    stats_t s = bnd_render(wcn, MODES[md]);
                    double r = s.rms >= BND_DC_FLOOR ? (fabs(s.mean) - dc0) / s.rms : 0.0;
                    n++;
                    skipped += s.rms < BND_DC_FLOOR;                /* too quiet for a DC ratio to mean anything */
                    worst = s.peak > worst ? s.peak : worst;
                    quiet = s.rms < quiet ? s.rms : quiet;
                    dc = r > dc ? r : dc;
                    if (s.peak <= (double)OBX_CEIL && r < 0.05)
                        continue;
                    if (bad++ < 4u)
                        printf("FAIL MODE %d WAVE %d CUT %d note %d: peak %.0f mean %.0f rms %.0f"
                               " (%.3f of rms past MODE 0's %.0f)\n",
                               MODES[md], WAVES[w], CUTS[ct], NOTES[nt], s.peak, s.mean, s.rms, r, dc0);
                }
            }
    printf("obx: RES 127 over the crossfade and the 4-pole (%u renders): peak %.0f of %d, DC %.3f of rms past"
           " MODE 0's, quietest rms %.0f (%u renders under rms %.0f skipped by the DC check)\n",
           n, worst, OBX_CEIL, dc, quiet, skipped, BND_DC_FLOOR);
    CHECK(!bad, "RES 127: %u of the renders are unbounded or add DC past 5 %% of rms", bad);
}

/* ------------------------------------------------- the 4-pole's second stage --- */
/* Stage 2 is fed stage 1's output, which a resonant stage 1 takes to ~185000, past dsp.c's +-150000
 * contract for tsvf_lp ("products fit in 32 bits"). eng_obx.c clamps what it hands over to
 * +-OBX_P4_IN. This pins (a) that the bound is real: for every cutoff and resonance the coefficient
 * table can produce, |a1 ic1 + a2 v3| and |a2 ic1 + a3 v3| stay inside int32 for any |in| <= OBX_P4_IN,
 * |ic| <= 150000 (the state clamp), v3 = in - ic2; and (b) the engine keeps to it: a dense sweep of a
 * full-scale square at RES 127 over every CUT and MIDI 24..99, in both render paths, with stage 2's
 * actual inner products evaluated in 64 bits at every sample. */
static struct { int32_t fed, raw, gain; double num; uint32_t clipped, over; } p4;

static int32_t p4_pre(int32_t y)
{
    int32_t ay = y < 0 ? -y : y;
    p4.raw = ay > p4.raw ? ay : p4.raw;                 /* stage 1's own output, before any stress */
    return y * (p4.gain ? p4.gain : 1);
}

static int32_t p4_seen(int32_t x, int32_t y, const void *c, int32_t ic1, int32_t ic2)
{
    const tsvf_t *f = (const tsvf_t *)c;
    int64_t v3 = (int64_t)x - ic2;                      /* x is what the engine's clamp handed over */
    int64_t n1 = (int64_t)f->a1 * ic1 + (int64_t)f->a2 * v3;
    int64_t n2 = (int64_t)f->a2 * ic1 + (int64_t)f->a3 * v3;
    double m = (double)(n1 < 0 ? -n1 : n1), m2 = (double)(n2 < 0 ? -n2 : n2);
    int32_t ax = x < 0 ? -x : x;
    m = m2 > m ? m2 : m;
    p4.fed = ax > p4.fed ? ax : p4.fed;
    p4.num = m > p4.num ? m : p4.num;
    p4.clipped += (p4.gain ? p4.gain : 1) * y != x;
    p4.over += m > 2147483647.0;
    return x;
}

/* the sweep: a full-scale square at RES 127, 4-pole, every CUT x MIDI 24..99 x 4 waves x SLOP 0 / 127.
 * ct_step thins the cutoffs for the stressed pass. */
static uint32_t p4_sweep(uint32_t ct_step)
{
    static const int16_t WAVES[] = {1, 4, 5, 6};          /* pulse, PW pulse, SYNC, SUB: the square-ish ones */
    uint32_t w, ct, note, renders = 0;
    for (w = 0; w < 2u * NELEM(WAVES); w++)               /* the second pass with SLOP 127: its cutoff error */
        for (ct = 0; ct < 128u; ct += ct_step)
            for (note = 24; note <= 99u; note++) {
                int16_t e[8] = {0, 0, 0, 64, 0, 127, 127, 0};   /* OSC 1 alone, full scale; RES 127; 4-pole */
                e[0] = WAVES[w % NELEM(WAVES)];
                e[4] = (int16_t)ct;
                e[7] = w >= NELEM(WAVES) ? 127 : 0;
                obx_voice(e, (int32_t)note * 16, note, ren_a, 3072u);
                renders++;
            }
    return renders;
}

static void test_p4_second_stage(void)
{
    double bound = 0.0, bnd1 = 0.0;
    uint32_t cut, r, renders;
    for (cut = 0; cut <= 127u * 256u; cut++)              /* every cutoff and resonance the table makes */
        for (r = 0; r < 128u; r++) {
            tsvf_t c;
            double v = (double)OBX_P4_IN + 150000.0, m1, m2;
            tsvf_coef(&c, (int32_t)cut, (int32_t)r);
            m1 = c.a1 * 150000.0 + c.a2 * v;
            m2 = c.a2 * 150000.0 + c.a3 * v;
            bnd1 = m1 > bnd1 ? m1 : bnd1;
            bound = m2 > bound ? m2 : bound;
        }
    printf("obx: 4-pole stage 2, |in| <= %d: the inner products are bounded by %.4g and %.4g of INT32_MAX"
           " (%.1f %%, %.1f %%)\n", OBX_P4_IN, bnd1, bound, 100.0 * bnd1 / 2147483647.0,
           100.0 * bound / 2147483647.0);
    CHECK(OBX_P4_IN <= 150000, "OBX_P4_IN = %d is past dsp.c's +-150000 signal contract", OBX_P4_IN);
    CHECK(bnd1 <= 2147483647.0 && bound <= 2147483647.0,
          "stage 2 with |in| <= %d can reach %.4g: past INT32_MAX", OBX_P4_IN, bound > bnd1 ? bound : bnd1);

    /* The real wrap, at the worst state the bound has to cover: CUT 32510, RES 127, same-sign state corner
     * (ic1 = +150000, ic2 = -150000, so v3 = in + 150000). At |in| = 150000, dsp.c's own contract, the
     * unclamped product is past INT32_MAX and tsvf_lp, the real one, returns the wrong v2; at OBX_P4_IN
     * (what the engine's clamp lets through) the product fits and tsvf_lp agrees with exact arithmetic. */
    {
        tsvf_t c;
        int32_t in_hi = 150000, in_ok = OBX_P4_IN, ic1, ic2, got_hi, got_ok;
        int64_t n_hi, n_ok, want_hi, want_ok;
        tsvf_coef(&c, 32510, 127);
        n_hi = (int64_t)c.a2 * 150000 + (int64_t)c.a3 * ((int64_t)in_hi + 150000);
        n_ok = (int64_t)c.a2 * 150000 + (int64_t)c.a3 * ((int64_t)in_ok + 150000);
        want_hi = -150000 + (n_hi >> 13);
        want_ok = -150000 + (n_ok >> 13);
        ic1 = 150000; ic2 = -150000; got_hi = tsvf_lp(&c, in_hi, &ic1, &ic2);
        ic1 = 150000; ic2 = -150000; got_ok = tsvf_lp(&c, in_ok, &ic1, &ic2);
        printf("obx: 4-pole stage 2 worst state (CUT 32510, RES 127, ic1 +150000, ic2 -150000): |in| 150000 -> product"
               " %.4g (%.1f %% of INT32_MAX), tsvf_lp %d vs exact %d; |in| %d -> %.4g (%.1f %%), %d vs %d\n",
               (double)n_hi, 100.0 * (double)n_hi / 2147483647.0, got_hi, (int)want_hi, in_ok, (double)n_ok,
               100.0 * (double)n_ok / 2147483647.0, got_ok, (int)want_ok);
        CHECK(n_hi > 2147483647ll, "the unclamped worst product %.4g does not exceed INT32_MAX: no wrap to witness",
              (double)n_hi);
        CHECK(got_hi != (int32_t)want_hi, "tsvf_lp did not wrap at |in| = 150000 (%d): the witness is void", got_hi);
        CHECK(n_ok <= 2147483647ll, "the worst product at OBX_P4_IN = %d is %.4g: past INT32_MAX", in_ok, (double)n_ok);
        CHECK(got_ok == (int32_t)want_ok, "tsvf_lp at OBX_P4_IN = %d gave %d, exact arithmetic %d", in_ok, got_ok,
              (int)want_ok);
    }

    /* (b1) as the engine renders: stage 1's own output goes through the clamp as it is */
    memset(&p4, 0, sizeof p4);
    renders = p4_sweep(1u);
    printf("obx: 4-pole stage 2 at RES 127, %u renders (CUT 0..127 x MIDI 24..99 x 4 waves x SLOP 0 / 127): stage 1 reaches"
           " %d, stage 2 is fed %d (%u samples clamped); inner product peaks at %.4g = %.1f %% of INT32_MAX\n",
           renders, p4.raw, p4.fed, p4.clipped, p4.num, 100.0 * p4.num / 2147483647.0);
    CHECK(p4.raw > 0, "the stage-2 seam never fired: the sweep did not reach the 4-pole");
    CHECK(p4.fed <= OBX_P4_IN, "stage 2 was fed %d, past OBX_P4_IN = %d", p4.fed, OBX_P4_IN);
    CHECK(!p4.over, "%u samples had a stage-2 inner product past INT32_MAX (int32 wrap)", p4.over);

    /* (b2) the clamp itself: stage 1's output is scaled x4 before the engine's clamp (so up to ~345000, past
     * dsp.c's contract), and what the clamp then hands stage 2 must still be inside OBX_P4_IN and the
     * inner products inside int32. Deleting or weakening the engine's clamp fails this. */
    memset(&p4, 0, sizeof p4);
    p4.gain = 4;
    renders = p4_sweep(4u);
    printf("obx: 4-pole stage 2, stage 1 scaled x4 before the clamp, %u renders: stage 1 reaches %d (x4), stage 2 is fed"
           " %d (%u samples clamped); inner product peaks at %.4g = %.1f %% of INT32_MAX\n",
           renders, p4.raw, p4.fed, p4.clipped, p4.num, 100.0 * p4.num / 2147483647.0);
    CHECK(p4.clipped > 0, "nothing was clamped under x4 stress: the engine's clamp is missing or the stress did not reach it");
    CHECK(p4.fed <= OBX_P4_IN, "the engine's clamp let %d through stage 2, past OBX_P4_IN = %d", p4.fed, OBX_P4_IN);
    CHECK(!p4.over, "%u samples had a stage-2 inner product past INT32_MAX after the clamp (int32 wrap)", p4.over);
    p4.gain = 0;
}

/* Where one leg meets the next, nothing may come between them but the morph's own granularity.
 * MODE 63 is the crossfade one step short of the high-pass and MODE 64 is the pinned high-pass tap
 * itself; MODE 63 still carries 128/4096 of the low-pass tap, and each step inside the leg moves
 * that weight by the same 128. So the step across the corner is measured against the step just
 * inside it (62 -> 63 at the high-pass; the notch and band-pass corners likewise), which has to be the same size: a clamp or a saturation that bites on one
 * side of the corner and not the other -- the +-400000 hp clamp the crossfade used to carry was
 * exactly that -- makes the crossing the larger of the two. RES 127, where the taps are widest.
 * (A fixed threshold would not do: 63 -> 64 is 15 % of the signal at a high cutoff, because there
 * the low-pass tap it drops is several times the high-pass tap that remains.) */
static double ren_diff_rel(int32_t mode_a, int32_t mode_b, int16_t cut)
{
    int16_t e[8] = {0, 11, 64, 64, 0, 127, 0, 0};
    double d = 0.0, r = 0.0;
    uint32_t i;
    e[4] = cut;
    e[6] = (int16_t)mode_a;
    obx_voice(e, 45 * 16, 1, ren_a, REN_SKIP + BND_N);
    e[6] = (int16_t)mode_b;
    obx_voice(e, 45 * 16, 1, ren_b, REN_SKIP + BND_N);
    for (i = REN_SKIP; i < REN_SKIP + BND_N; i++) {
        double x = (double)ren_a[i] - ren_b[i], y = (double)ren_b[i];
        d += x * x;
        r += y * y;
    }
    return r > 0.0 ? sqrt(d / r) : 0.0;
}

static void test_mode_corner_step(void)
{
    static const int16_t CUTS[] = {40, 90, 120};
    static const int16_t CORNERS[] = {OBX_M_NOTCH, OBX_M_HP, OBX_M_BP};   /* 32, 64, 96: the pinned taps */
    uint32_t ct, k;
    for (k = 0; k < NELEM(CORNERS); k++)
        for (ct = 0; ct < NELEM(CUTS); ct++) {
            int32_t c = CORNERS[k];
            double in = ren_diff_rel(c - 2, c - 1, CUTS[ct]);
            double over = ren_diff_rel(c - 1, c, CUTS[ct]);
            printf("obx: RES 127 CUT %3d: MODE %d -> %d moves %5.2f %% of the signal, %d -> %d %5.2f %%\n",
                   CUTS[ct], c - 2, c - 1, 100.0 * in, c - 1, c, 100.0 * over);
            CHECK(over <= in * 1.25 + 0.002,
                  "MODE %d -> %d moves %.2f %% at CUT %d where %d -> %d moves %.2f %%: the corner has a step of"
                  " its own", c - 1, c, 100.0 * over, CUTS[ct], c - 2, c - 1, 100.0 * in);
        }
}

/* SLOP 0 is inert: two different note-on counters have to give the same samples. (Two renders of
 * the same counter would only pin the renderer's own determinism.) */
static void test_slop_deterministic(void)
{
    static const int16_t E[8] = {0, 7, 64, 64, 90, 40, 0, 0};       /* SLOP 0 */
    obx_voice(E, 60 * 16, 3, ren_a, REN_SKIP + REN_N);
    obx_voice(E, 60 * 16, 11, ren_b, REN_SKIP + REN_N);
    CHECK(memcmp(ren_a, ren_b, sizeof ren_a) == 0,
          "SLOP 0: note-on 3 and note-on 11 do not render alike");
}

static void test_slop_spread(void)
{
    int16_t e[8] = {3, 0, 0, 64, 110, 0, 0, 0};                     /* WAVE SIN, OSC 1 alone */
    int32_t p16 = 84 * 16;
    double ref, f[8], worst = 0.0, moved = 0.0;
    uint64_t h[8];
    uint32_t i, k, allsame = 0;
    obx_voice(e, p16, 1, ren_a, REN_SKIP + REN_N);                 /* SLOP 0: the nominal pitch */
    fft_mag(ren_a + REN_SKIP, REN_N, mag_a);
    ref = peak_hz(mag_a, REN_N, (double)FS, 800.0, 1400.0);
    e[7] = 127;
    for (i = 0; i < 8u; i++) {
        obx_voice(e, p16, i + 1u, ren_b, REN_SKIP + REN_N);
        h[i] = ren_hash(ren_b, REN_SKIP + REN_N);
        fft_mag(ren_b + REN_SKIP, REN_N, mag_b);
        f[i] = peak_hz(mag_b, REN_N, (double)FS, 800.0, 1400.0);
        if (fabs(cents(f[i], ref)) > fabs(worst))
            worst = cents(f[i], ref);
        if (fabs(cents(f[i], ref)) > moved)
            moved = fabs(cents(f[i], ref));
    }
    for (i = 0; i < 8u; i++)
        for (k = i + 1u; k < 8u; k++)
            allsame += h[i] == h[k];
    printf("obx: SLOP 127 over 8 note-ons, %.3f Hz nominal:", ref);
    for (i = 0; i < 8u; i++)
        printf(" %+.2f", cents(f[i], ref));
    printf(" cents\n");
    CHECK(!allsame, "SLOP 127: %u of the 28 voice pairs render identically", allsame);
    CHECK(fabs(worst) <= 6.0, "SLOP 127: a voice is %+.2f cents off, past the +-6 cent spread", worst);
    CHECK(moved >= 1.0, "SLOP 127: no voice moved by as much as a cent (the widest was %.2f)", moved);
}

static void test_dtn(void)
{
    int16_t e[8] = {0, 0, 64, 64, 90, 20, 0, 0};
    uint64_t h0, h1;
    double f1 = 0.0, f2 = 0.0, sep;
    obx_voice(e, 60 * 16, 1, ren_a, REN_SKIP + REN_N);
    h0 = ren_hash(ren_a, REN_SKIP + REN_N);
    e[1] = 1;
    obx_voice(e, 60 * 16, 1, ren_a, REN_SKIP + REN_N);
    h1 = ren_hash(ren_a, REN_SKIP + REN_N);
    CHECK(h0 != h1, "DTN 0 -> 1 did not change the render");
    /* DTN is in cents (edit[1] reads "ct"), so the two oscillators of DTN 127 stand 127 cents apart */
    e[0] = 3;                                                        /* WAVE SIN: one partial each */
    e[1] = 127;
    obx_voice(e, 84 * 16, 1, ren_a, REN_SKIP + REN_N);
    fft_mag(ren_a + REN_SKIP, REN_N, mag_a);
    CHECK(two_peaks(mag_a, REN_N, (double)FS, 900.0, 1300.0, 10u, &f1, &f2), "DTN 127: no two peaks found");
    sep = f1 > 0.0 ? cents(f2, f1) : 0.0;
    printf("obx: DTN 127  peaks %.2f and %.2f Hz, %.1f cents apart\n", f1, f2, sep);
    CHECK(sep >= 115.0 && sep <= 140.0, "DTN 127: the peaks are %.1f cents apart, not DTN's 127", sep);
}

/* dspfft.h's two refusals, pinned here because Tasks 5.2 and 6.1 include the header and would
 * otherwise inherit them as traps: an n the FFT cannot transform must leave no stale magnitudes
 * behind, and a band wholly below 0 Hz must not read as the whole spectrum. */
static void test_dspfft_guards(void)
{
    unsigned i;
    for (i = 0; i < 512u; i++)
        mag_b[i] = 1234.0f;
    fprintf(stderr, "obx: dspfft refusals (the next line on stderr is the one being tested)\n");
    fft_mag(ren_a, 1000u, mag_b);                      /* 1000 is not a power of two */
    for (i = 0; i < 500u; i++)
        if (mag_b[i] != 0.0f)
            break;
    CHECK(i == 500u, "fft_mag(n = 1000) left mag[%u] at %.0f: the caller reads stale magnitudes", i,
          mag_b[i]);
    for (i = 0; i < 512u; i++)
        mag_b[i] = 1234.0f;
    fft_mag(ren_a, 2u * DSPFFT_MAX, mag_b);            /* past the scratch: the size of mag[] is unknowable */
    for (i = 0; i < 512u; i++)
        if (mag_b[i] != 1234.0f)
            break;
    CHECK(i == 512u, "fft_mag(n = 2 * DSPFFT_MAX) wrote mag[%u]: a refusal must not write past a buffer it cannot size",
          i);
    saw_spectrum(0, cut_for_hz(1000), 0);
    CHECK(band_db(mag_a, REN_N, (float)FS, -10.0f, -5.0f) == DSPFFT_FLOOR_DB,
          "band_db(-10, -5) reads %.1f dB: a band below 0 Hz is not empty",
          band_db(mag_a, REN_N, (float)FS, -10.0f, -5.0f));
    CHECK(band_db(mag_a, REN_N, (float)FS, 900.0f, 100.0f) == DSPFFT_FLOOR_DB,
          "band_db(900, 100) reads %.1f dB: hi below lo is not empty",
          band_db(mag_a, REN_N, (float)FS, 900.0f, 100.0f));
}

int main(void)
{
    test_mode0_legacy_lp();
    test_mode_lp();
    test_mode_hp();
    test_mode_notch();
    test_mode_4pole();
    test_mode_between();
    test_mode_bp_to_lp();
    test_mode_bounded();
    test_p4_second_stage();
    test_mode_corner_step();
    test_slop_deterministic();
    test_slop_spread();
    test_dtn();
    test_dspfft_guards();
    printf(fails ? "obx_test: %d FAIL\n" : "obx_test: ok\n", fails);
    return fails != 0;
}

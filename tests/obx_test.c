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

static void test_slop_deterministic(void)
{
    static const int16_t E[8] = {0, 7, 64, 64, 90, 40, 0, 0};       /* SLOP 0 */
    obx_voice(E, 60 * 16, 3, ren_a, REN_SKIP + REN_N);
    obx_voice(E, 60 * 16, 3, ren_b, REN_SKIP + REN_N);
    CHECK(memcmp(ren_a, ren_b, sizeof ren_a) == 0, "SLOP 0: two renders of the same voice differ");
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

int main(void)
{
    test_mode0_legacy_lp();
    test_mode_lp();
    test_mode_hp();
    test_mode_notch();
    test_mode_4pole();
    test_slop_deterministic();
    test_slop_spread();
    test_dtn();
    printf(fails ? "obx_test: %d FAIL\n" : "obx_test: ok\n", fails);
    return fails != 0;
}

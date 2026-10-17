/* SPDX-License-Identifier: GPL-3.0-only */
/* drift_test.c: DRIFT, the Juno voice (eng_drift.c, dsp_va.c; Ultramaster KR-106, GPL-3.0), in two tiers.
 *   cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/drift_test tests/drift_test.c -lm
 *   build/host/drift_test [build/drift_ref]
 * Tier 1 holds the lifted filter tight: dsp_va.c's va_ladder is the donor's algorithm at the donor's precision, so it must
 * agree with the donor's own kr106::VCF almost exactly. Tier 2 holds the adapted voice (the oscillator, the control rate, the
 * parameter mapping are ours) to its behaviour, through the real voice path (trk_note_on, track_render).
 *   (a) every case of drift_ref_cfg.h: max |y - y_ref| / max |y_ref| < 1e-4 against build/drift_ref/NN.f32 (tests/drift_ref.cpp);
 *   (b) 24 dB/oct: the magnitude at 4 x cutoff is >= 20 dB under the passband;
 *   (c) DRV 0 -> 127 takes the THD of a 200 Hz sine through the open filter from < 1 % to > 10 %;
 *   (d) RES 127 self-oscillates within +-3 % of the cutoff;
 *   (e) DRFT 0: two renders byte-identical; DRFT 127: a slow wander within +-8 cents;
 *   (f) VAR 0: every voice has the same cutoff; VAR 127: measurably different per voice;
 *   (g) MIX: sub at 0, saw at 64, pulse at 127, each by its spectrum;
 *   (h) P_FTYPE moves the HPF corner across its four positions;
 *   (h) also: P_FTYPE above DRIFT's four positions is the flat one, never a hidden cut;
 *   (i) CPU: the DRIFT part inside its pin (+10 % of its measured worst, -O2); the dearest legal four-track mix is measured and
 *       printed, not asserted (the 80 % claim is Task 8.1's, on the device);
 *   (j) the donor's linear ramp bandlimited: inharmonic energy at MIDI 72 / 84 / 96 below -50 dB;
 *   (k) the DCO's spectrum against the ideal waveform of the donor's amplitudes and the PW law (amplitudes, PW, polarity, MIX);
 *   (l) the auto PWM swings the pulse's 2nd harmonic; (m) KTR: 1 octave per octave at 127, none at 0, C4 the zero point;
 *   (n) the two 1.59 Hz DC blockers: -6 dB at 1.59 Hz; (o) a new note on a finished voice starts clean, a retrigger keeps the states;
 * plus the halfband pair (gain, image rejection) and a bounded / finite sweep of the corners.
 * The DRIFT_IN seam (eng_drift.c) replaces the oscillator by silence, an impulse, a sine or noise, to measure the filter alone. */
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp_va.c"
#include "drift_ref_cfg.h"
enum { INJ_OFF, INJ_SILENCE, INJ_IMPULSE, INJ_SINE, INJ_NOISE };
static int inj_mode, inj_pending;
static float inj_amp, inj_step;
static double inj_phase;
static uint32_t inj_rng = 0x2545F491u;
static float drift_in(float s)
{
    switch (inj_mode) {
    case INJ_SILENCE:
        return 0.0f;
    case INJ_IMPULSE:
        s = inj_pending ? inj_amp : 0.0f;
        inj_pending = 0;
        return s;
    case INJ_SINE:
        inj_phase += (double)inj_step;
        return inj_amp * (float)sin(inj_phase);
    case INJ_NOISE:
        inj_rng = inj_rng * 1664525u + 1013904223u;
        return inj_amp * ((float)(int32_t)inj_rng * (1.0f / 2147483648.0f));
    default:
        return s;
    }
}
#define DRIFT_IN(s) drift_in(s)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "dspfft.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("drift: %s ", ok ? "ok  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    bad += !ok;
}

#define FSF 44100.0f
#define FSD 44100.0

/* ------------------------------------------------------------ harness --- */
static track_t *T;
static void drift_fresh(void)                      /* part 0: DRIFT, POLY, dry, the amp envelope open, the engine's state zero */
{
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    inj_mode = INJ_OFF;
    inj_pending = 0;
    inj_phase = 0.0;
    inj_rng = 0x2545F491u;
    host_tracks_init();
    memset(eng_mem[0].b, 0, sizeof eng_mem[0].b);
    eng_mem_owner[0] = 0;
    host_preset(&trk[0], ENGI_DRIFT, 0);
    T = &trk[0];
    T->p[P_VOICE] = V_POLY;
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    T->p[P_LD_AMP] = T->p[P_LD_FLT] = T->p[P_LD_PIT] = T->p[P_ED_PIT] = T->p[P_LD_SHP] = T->p[P_ED_SHP] = 0;
    T->p[P_ED_FLT] = 0;
    T->p[P_GLIDE] = 0;
    T->p[P_FTYPE] = 0;
    T->p[P_ATK] = 0;
    T->p[P_SUS] = 127;
    T->slide_glide = 0;
}

/* MIX, PW, DRFT, VAR, CUT, RES, DRV, KTR */
static void pots(int mix, int pw, int drft, int var, int cut, int res, int drv, int ktr)
{
    T->p[P_E0] = (int16_t)mix;
    T->p[P_E1] = (int16_t)pw;
    T->p[P_E2] = (int16_t)drft;
    T->p[P_E3] = (int16_t)var;
    T->p[P_E4] = (int16_t)cut;
    T->p[P_E5] = (int16_t)res;
    T->p[P_E6] = (int16_t)drv;
    T->p[P_E7] = (int16_t)ktr;
}

static int32_t o_blk[CTL];
static void run(int32_t *out, unsigned n)          /* n samples (a multiple of CTL) of the part into out */
{
    unsigned b, i;
    for (b = 0; b < n / CTL; b++) {
        track_render(T, o_blk, CTL);
        if (out)
            for (i = 0; i < CTL; i++)
                out[b * CTL + i] = o_blk[i];
    }
}

static drift_vc_t *vc_of(unsigned i) { return (drift_vc_t *)ENG_MEM(0) + i; }

static const char *refdir = "build/drift_ref";

/* ------------------------------------------------------------ analysis --- */
#define NFFT 16384u
static float mag_[NFFT / 2];
static double psd_[NFFT / 2];
/* the Welch mean of |X|^2 over nseg segments of NFFT */
static void psd_of(const int32_t *x, unsigned nseg)
{
    unsigned s, k;
    for (k = 0; k < NFFT / 2u; k++)
        psd_[k] = 0.0;
    for (s = 0; s < nseg; s++) {
        fft_mag(x + (size_t)s * NFFT, NFFT, mag_);
        for (k = 0; k < NFFT / 2u; k++)
            psd_[k] += (double)mag_[k] * mag_[k] / nseg;
    }
}
static double psd_db(double lo, double hi)         /* mean power per bin of the band, dB */
{
    unsigned k0 = (unsigned)ceil(lo * NFFT / FSD), k1 = (unsigned)floor(hi * NFFT / FSD), k;
    double e = 0.0;
    if (k1 >= NFFT / 2u)
        k1 = NFFT / 2u - 1u;
    if (k0 > k1)
        k0 = k1;
    for (k = k0; k <= k1; k++)
        e += psd_[k];
    return 10.0 * log10(e / (k1 - k0 + 1u) + 1e-30);
}
/* the largest magnitude within +-3 bins of hz, dB (mag_ of the last fft_mag of n samples) */
static double peak_db(const float *mag, unsigned n, double hz)
{
    int c = (int)(hz * n / FSD + 0.5), k;
    double m = 0.0;
    for (k = c - 3; k <= c + 3; k++)
        if (k > 0 && k < (int)(n / 2u) && mag[k] > m)
            m = mag[k];
    return 20.0 * log10(m + 1e-30);
}
static double pk_hz(const float *mag, unsigned n, double lo, double hi)   /* the strongest partial, parabolic */
{
    unsigned k0 = (unsigned)(lo * n / FSD), k1 = (unsigned)(hi * n / FSD), k, kb = k0;
    double a, b, c, d;
    for (k = k0; k <= k1; k++)
        if (mag[k] > mag[kb])
            kb = k;
    a = log((double)mag[kb - 1] + 1e-30);
    b = log((double)mag[kb] + 1e-30);
    c = log((double)mag[kb + 1] + 1e-30);
    d = a - 2.0 * b + c;
    return ((double)kb + (d != 0.0 ? 0.5 * (a - c) / d : 0.0)) * FSD / n;
}

static uint32_t cut_for_hz(uint32_t hz)            /* the CUT index whose displayed frequency is nearest hz */
{
    uint32_t k, best = 0;
    for (k = 0; k < 128u; k++) {
        int32_t a = (int32_t)CUTOFF_HZ[k] - (int32_t)hz, b = (int32_t)CUTOFF_HZ[best] - (int32_t)hz;
        if ((a < 0 ? -a : a) < (b < 0 ? -b : b))
            best = k;
    }
    return best;
}

static int32_t buf[NFFT * 12u];                    /* 196608 samples, 4.5 s */
/* a note held with the seam set, rendered: skip the first `skip` samples, then n into buf */
static void play(unsigned note, unsigned skip, unsigned n)
{
    trk_note_on(T, note, 100);                     /* (VEL 100: above 110 voice.c adds the accent's cutoff boost) */
    run(0, skip);
    run(buf, n);
}

/* ------------------------------------------------------------ (a) --- */
static void test_ladder_ref(void)
{
    static float ref[DRIFT_REF_N * 4];
    static float got[DRIFT_REF_N * 4];
    int c, nok = 0, nrun = 0;
    double worst = 0.0;
    int worst_c = -1;
    for (c = 0; c < DRIFT_REF_NCFG; c++) {
        const drift_ref_cfg_t *k = &DRIFT_REF_CFG[c];
        char path[512];
        FILE *f;
        va_ladder_t l;
        float peak = 0.0f;
        size_t n = (size_t)DRIFT_REF_N * k->os, i, got_n;
        double e = 0.0, m = 0.0;
        unsigned s, j;
        snprintf(path, sizeof path, "%s/%02d.f32", refdir, c);
        if (!(f = fopen(path, "rb")))
            continue;
        got_n = fread(ref, sizeof(float), n, f);
        fclose(f);
        if (got_n != n)
            continue;
        memset(&l, 0, sizeof l);
        va_ladder_reset(&l);
        va_ladder_setup(&l, k->os, 1, FSF);
        for (s = 0; s < DRIFT_REF_N; s++) {
            va_ladder_env(&l, peak);
            va_ladder_coeffs(&l, k->hz, k->res, FSF);
            peak = 0.0f;
            for (j = 0; j < k->os; j++) {
                float x = (float)drift_ref_input(s * k->os + j, k->f0, k->os), a = x < 0.0f ? -x : x;
                peak = a > peak ? a : peak;
                got[(size_t)s * k->os + j] = va_ladder_tick(&l, x * k->drv);
            }
        }
        for (i = 0; i < n; i++) {
            double d = fabs((double)got[i] - (double)ref[i]), r = fabs((double)ref[i]);
            e = d > e ? d : e;
            m = r > m ? r : m;
        }
        nrun++;
        if (m > 0.0 && e / m < 1e-4)
            nok++;
        if (m > 0.0 && e / m > worst) {
            worst = e / m;
            worst_c = c;
        }
        if (!(m > 0.0 && e / m < 1e-4))
            printf("drift:   case %2d (%.0f Hz res %.2f drv %.0f os %d): rel err %.3g\n", c, k->hz, k->res, k->drv, k->os,
                   m > 0.0 ? e / m : 1.0);
    }
    if (!nrun) {
        printf("drift: SKIP (a) NOT CHECKED: no reference files in %s (build tests/drift_ref.cpp against the donor, see its header)\n",
               refdir);
        return;
    }
    check(nrun == DRIFT_REF_NCFG && nok == nrun,
          "(a) va_ladder against the donor's kr106::VCF: %d of %d cases (of %d) under 1e-4 of the peak, worst %.3g (case %d)", nok, nrun,
          DRIFT_REF_NCFG, worst, worst_c);
}

/* ------------------------------------------------------------ halfband --- */
static void test_halfband(void)
{
    static const double F[3] = {1000.0, 8000.0, 15000.0};
    unsigned t, n;
    char msg[200] = "";
    int ok = 1;
    double rej;
    for (t = 0; t < 3u; t++) {                     /* up then down: unity gain through the pair */
        va_hb_t u, d;
        double si = 0.0, so = 0.0;
        memset(&u, 0, sizeof u);
        memset(&d, 0, sizeof d);
        va_hb_reset(&u);
        va_hb_reset(&d);
        for (n = 0; n < 8192u; n++) {
            float x = (float)sin(2.0 * M_PI * F[t] * n / FSD), a, b, y;
            va_hb_up(&u, x, &a, &b);
            y = va_hb_down(&d, a, b);
            if (n >= 4096u) {
                si += (double)x * x;
                so += (double)y * y;
            }
        }
        {
            double g = 10.0 * log10(so / si);
            ok &= fabs(g) < 0.3;
            snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " %.0f Hz %+.2f dB", F[t], g);
        }
    }
    check(ok, "halfband up -> down is unity gain (within 0.3 dB):%s", msg);
    {                                              /* a tone at 30 kHz in the 88.2 kHz stream aliases to 14.1 kHz: the decimator must stop it */
        va_hb_t d;
        double si = 0.0, so = 0.0;
        memset(&d, 0, sizeof d);
        va_hb_reset(&d);
        for (n = 0; n < 8192u; n++) {
            float a = (float)sin(2.0 * M_PI * 30000.0 * (2 * n) / (2 * FSD)), b = (float)sin(2.0 * M_PI * 30000.0 * (2 * n + 1) / (2 * FSD)), y;
            y = va_hb_down(&d, a, b);
            if (n >= 4096u) {
                si += 0.5 * 2;
                so += (double)y * y;
            }
        }
        rej = 10.0 * log10(so / si + 1e-30);
        check(rej < -50.0, "halfband down: a 30 kHz tone at 88.2 kHz (aliasing to 14.1 kHz) is %.0f dB down (< -50)", rej);
    }
}

/* ------------------------------------------------------------ (b) --- */
static void test_slope(void)
{
    uint32_t ci = cut_for_hz(1000), fc = CUTOFF_HZ[ci];
    double pass, f4, f2;
    drift_fresh();
    pots(64, 64, 0, 0, (int)ci, 0, 0, 0);
    inj_mode = INJ_NOISE;
    inj_amp = 0.05f;
    inj_step = 0.0f;
    play(60, 4096, NFFT * 8u);
    psd_of(buf, 8);
    pass = psd_db(100.0, 200.0);
    f2 = psd_db(1.9 * fc, 2.1 * fc);
    f4 = psd_db(3.8 * fc, 4.2 * fc);
    check(pass - f4 >= 20.0 && f2 - f4 >= 19.0,
          "(b) 24 dB/oct: cutoff %u Hz, 4 x cutoff is %.1f dB under the passband (>= 20), 2 x -> 4 x drops %.1f dB (>= 19)", fc, pass - f4, f2 - f4);
}

/* ------------------------------------------------------------ (c) --- */
static double thd_at(int drv)
{
    const unsigned K = 149;                        /* 149 cycles in NFFT: 200.5 Hz, bin-centred */
    double f1, e1 = 0.0, eh = 0.0;
    unsigned h, k;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, drv, 0);
    inj_mode = INJ_SINE;
    inj_amp = 0.5f;
    inj_step = (float)(2.0 * M_PI * K / 32768.0 / DRIFT_OS);   /* per oversampled sample; 32768 base samples = K cycles */
    {
        static int32_t b2[32768];
        trk_note_on(T, 60, 100);
        run(0, 8192);
        run(b2, 32768);
        {
            static float m2[16384];
            fft_mag(b2, 32768, m2);
            f1 = K;
            for (k = (unsigned)f1 - 3; k <= (unsigned)f1 + 3; k++)
                e1 += (double)m2[k] * m2[k];
            for (h = 2; h <= 10; h++)
                for (k = K * h - 3; k <= K * h + 3; k++)
                    if (k < 16384u)
                        eh += (double)m2[k] * m2[k];
        }
    }
    return sqrt(eh / e1);
}
static void test_thd(void)
{
    double a = thd_at(0), b = thd_at(127);
    check(a < 0.01 && b > 0.10, "(c) THD of a 200 Hz sine, filter open: DRV 0 %.2f %% (< 1), DRV 127 %.1f %% (> 10)", 100.0 * a, 100.0 * b);
}

/* ------------------------------------------------------------ (d) --- */
static void test_selfosc(void)
{
    static const uint32_t HZ[3] = {300, 1000, 3000};
    unsigned i;
    char msg[240] = "";
    int ok = 1;
    for (i = 0; i < 3u; i++) {
        uint32_t ci = cut_for_hz(HZ[i]), fc = CUTOFF_HZ[ci];
        double hz, err;
        drift_fresh();
        pots(64, 64, 0, 0, (int)ci, 127, 0, 0);
        inj_mode = INJ_SILENCE;
        trk_note_on(T, 60, 100);
        run(0, 16384);
        run(buf, NFFT);
        fft_mag(buf, NFFT, mag_);
        hz = pk_hz(mag_, NFFT, 0.5 * fc, 2.0 * fc);
        err = hz / fc - 1.0;
        ok &= fabs(err) <= 0.03;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [%u Hz: %.1f Hz, %+.2f %%]", fc, hz, 100.0 * err);
    }
    check(ok, "(d) RES 127 self-oscillates within +-3 %% of the cutoff:%s", msg);
}

/* ------------------------------------------------------------ (e) --- */
/* the pitch of a held saw (filter open): the rising zero crossings of `secs` s, a window of 100 cycles each, cents against
 * `nominal` Hz into cents[]; returns the windows */
static double zc_[16384];
static unsigned pitch_track(double *cents, unsigned maxw, unsigned secs, double nominal)
{
    static int32_t blk[CTL * 64u];
    unsigned total = secs * 44100u / (CTL * 64u) * (CTL * 64u), done = 0, nz = 0, w = 0, i;
    int32_t prev = 0;
    uint64_t pos = 0;
    while (done < total) {
        run(blk, CTL * 64u);
        for (i = 0; i < CTL * 64u; i++, pos++) {
            int32_t y = blk[i];
            if (pos > 8192u && prev < 0 && y >= 0 && nz < NELEM(zc_))
                zc_[nz++] = (double)pos - 1.0 + (double)(-prev) / (double)(y - prev);
            prev = y;
        }
        done += CTL * 64u;
    }
    for (i = 0; i + 100u < nz && w < maxw; i += 100u)
        cents[w++] = 1200.0 * log2(99.0 * FSD / (zc_[i + 99u] - zc_[i]) / nominal);
    return w;
}
static void test_drift(void)
{
    static int32_t a[CTL * 400u], b[CTL * 400u];
    double cents[64], lo = 1e9, hi = -1e9, mx = 0.0;
    unsigned w, i;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(a, CTL * 400u);
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(b, CTL * 400u);
    check(!memcmp(a, b, sizeof a), "(e) DRFT 0: two renders are byte-identical");
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    w = pitch_track(cents, 64, 14, 220.0);
    for (i = 0; i < w; i++) {
        mx = fmax(mx, fabs(cents[i]));
    }
    check(w >= 20 && mx < 0.3, "(e) DRFT 0: the pitch is steady (%u windows, worst %.2f cents off 220 Hz)", w, mx);
    drift_fresh();
    pots(64, 64, 127, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    w = pitch_track(cents, 64, 14, 220.0);
    mx = 0.0;
    for (i = 0; i < w; i++) {
        lo = fmin(lo, cents[i]);
        hi = fmax(hi, cents[i]);
        mx = fmax(mx, fabs(cents[i]));
    }
    check(w >= 20 && mx <= 8.3 && hi - lo >= 0.5,
          "(e) DRFT 127: a slow wander, %.2f .. %.2f cents (within +-8, moving by >= 0.5) over %u windows of 100 cycles", lo, hi, w);
}

/* ------------------------------------------------------------ (f) --- */
/* one voice of the engine by itself: voice index i, `note`, nblk blocks, the modulation neutral (amp 1, cutoff 0, shape centred) */
static void direct_voice(unsigned i, unsigned note, unsigned nblk, int32_t *out)
{
    voice_t *v = &T->v[i];
    vmod_t m;
    unsigned b, k;
    memset(v, 0, sizeof *v);
    v->active = v->gate = 1;
    v->stage = 2;
    v->note = (uint8_t)note;
    v->vel = 100;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    memset(&m, 0, sizeof m);
    m.pitch16 = v->pitch16;
    m.inc = pitch_inc(m.pitch16);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    m.envq15 = 32767;
    ENGINES[ENGI_DRIFT]->note_on(T, v);
    for (b = 0; b < nblk; b++) {
        int32_t o[CTL];
        for (k = 0; k < CTL; k++)
            o[k] = 0;
        ENGINES[ENGI_DRIFT]->render(T, v, o, CTL, &m);
        if (out)
            for (k = 0; k < CTL; k++)
                out[b * CTL + k] = o[k];
    }
}
static void test_variance(void)
{
    double hz[2][4], lo, hi;
    unsigned var, i, part, k;
    int ok0, ok1;
    for (var = 0; var < 2u; var++) {
        k = 0;
        for (part = 0; part < 2u; part++) {            /* two parts, so four voices' offsets (they are seeded by part and voice) */
            for (i = 0; i < DRIFT_POLY; i++) {
                drift_fresh();
                pots(64, 64, 0, var ? 127 : 0, 80, 127, 0, 0);
                trk[1] = trk[0];
                memset(eng_mem[1].b, 0, sizeof eng_mem[1].b);
                T = &trk[part];
                inj_mode = INJ_SILENCE;
                direct_voice(i, 60, NFFT / CTL + 512u, buf);
                fft_mag(buf + 512 * CTL, NFFT, mag_);
                hz[var][k++] = pk_hz(mag_, NFFT, 200.0, 4000.0);
            }
        }
    }
    lo = hi = hz[0][0];
    for (i = 1; i < k; i++) {
        lo = hz[0][i] < lo ? hz[0][i] : lo;
        hi = hz[0][i] > hi ? hz[0][i] : hi;
    }
    ok0 = hi / lo - 1.0 < 0.002;
    check(ok0, "(f) VAR 0: the %u voices self-oscillate at %.1f .. %.1f Hz (the same cutoff, within 0.2 %%)", k, lo, hi);
    lo = hi = hz[1][0];
    for (i = 1; i < k; i++) {
        lo = hz[1][i] < lo ? hz[1][i] : lo;
        hi = hz[1][i] > hi ? hz[1][i] : hi;
    }
    ok1 = hi / lo - 1.0 > 0.01;
    check(ok1, "(f) VAR 127: the %u voices self-oscillate at %.1f .. %.1f Hz (%.2f %% apart, >= 1)", k, lo, hi, 100.0 * (hi / lo - 1.0));
}

/* ------------------------------------------------------------ (g) --- */
static float mag2_[NFFT];
static void test_mix(void)
{
    int k;
    unsigned mixes[3] = {0, 64, 127};
    char msg[3][220];
    int ok[3];
    for (k = 0; k < 3; k++) {
        double f1, f2, f3, f4, s1;
        drift_fresh();
        pots((int)mixes[k], 64, 0, 0, 127, 0, 0, 0);
        trk_note_on(T, 57, 100);
        run(0, 16384);
        run(buf, NFFT * 2u);
        fft_mag(buf, NFFT * 2u, mag2_);
        s1 = peak_db(mag2_, NFFT * 2u, 110.0);
        f1 = peak_db(mag2_, NFFT * 2u, 220.0);
        f2 = peak_db(mag2_, NFFT * 2u, 440.0);
        f3 = peak_db(mag2_, NFFT * 2u, 660.0);
        f4 = peak_db(mag2_, NFFT * 2u, 880.0);
        if (k == 0) {                              /* sub: a square an octave down: 110, 330, 550 Hz; nothing at 220 */
            double s3 = peak_db(mag2_, NFFT * 2u, 330.0);
            ok[k] = s1 > f1 + 20.0 && s1 > f2 + 20.0 && s1 >= s3 + 6.0 && s1 > f4 + 20.0;
            snprintf(msg[k], sizeof msg[k], "MIX 0: 110 Hz (the sub) %.1f dB, 220 %.1f, 330 %.1f, 440 %.1f", s1, f1, s3, f2);
        } else if (k == 1) {                       /* saw: 1/n harmonics, no sub */
            ok[k] = s1 < f1 - 25.0 && fabs((f2 - f1) + 6.0) < 3.0 && fabs((f3 - f1) + 9.5) < 3.0 && fabs((f4 - f1) + 12.0) < 3.5;
            snprintf(msg[k], sizeof msg[k], "MIX 64 (saw): harmonics 2 3 4 at %+.1f %+.1f %+.1f dB against the fundamental (-6 -9.5 -12), the sub %.1f dB under",
                     f2 - f1, f3 - f1, f4 - f1, f1 - s1);
        } else {                                   /* pulse at PW 64: odd harmonics only */
            ok[k] = s1 < f1 - 25.0 && f2 < f1 - 25.0 && f4 < f1 - 25.0 && fabs((f3 - f1) + 9.5) < 3.0;
            snprintf(msg[k], sizeof msg[k], "MIX 127 (pulse, PW 64): the 2nd and 4th harmonics are %.1f and %.1f dB under the fundamental (>= 25), the 3rd %+.1f (-9.5), the sub %.1f under",
                     f1 - f2, f1 - f4, f3 - f1, f1 - s1);
        }
    }
    for (k = 0; k < 3; k++)
        check(ok[k], "(g) %s", msg[k]);
}

/* ------------------------------------------------------------ (h) --- */
static void test_hpf(void)
{
    double p[4][4], ref[4];
    static const double AT[4] = {60.0, 100.0, 236.0, 754.0};
    unsigned ft, a;
    for (ft = 0; ft < 4u; ft++) {
        drift_fresh();
        pots(64, 64, 0, 0, 127, 0, 0, 0);
        T->p[P_FTYPE] = (int16_t)ft;
        inj_mode = INJ_NOISE;
        inj_amp = 0.05f;
        play(60, 4096, NFFT * 10u);
        psd_of(buf, 10);
        ref[ft] = psd_db(2000.0, 4000.0);
        for (a = 0; a < 4u; a++)
            p[ft][a] = psd_db(AT[a] * 0.93, AT[a] * 1.07) - ref[ft];
    }
    check(fabs(p[0][0]) < 1.0 && fabs(p[0][1]) < 1.0 && fabs(p[0][2]) < 1.0 && fabs(p[0][3]) < 1.0,
          "(h) P_FTYPE 0: flat (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB)", p[0][0], p[0][1], p[0][2], p[0][3]);
    check(p[1][0] > 5.0 && p[1][1] > 3.0 && p[1][3] < 3.0,
          "(h) P_FTYPE 1: the bass boost (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB)", p[1][0], p[1][1], p[1][2], p[1][3]);
    check(fabs(p[2][2] + 3.0) < 1.5 && p[2][0] < -10.0 && p[2][3] > -1.5,
          "(h) P_FTYPE 2: a 236 Hz corner (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB; -3 dB at 236)", p[2][0], p[2][1], p[2][2], p[2][3]);
    check(fabs(p[3][3] + 3.0) < 1.5 && p[3][2] < -8.0 && p[3][2] < p[2][2] - 5.0,
          "(h) P_FTYPE 3: a 754 Hz corner (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB; -3 dB at 754)", p[3][0], p[3][1], p[3][2], p[3][3]);
}

/* ------------------------------------------------------------ the corners --- */
static void test_corners(void)
{
    static const int MIXES[3] = {0, 64, 127}, RES[2] = {0, 127}, DRV[2] = {0, 127}, CUT[3] = {0, 60, 127}, KTR[2] = {0, 127};
    static const unsigned NOTES[4] = {12, 60, 108, 127};
    unsigned a, b, c, d, e, n, i, bad_n = 0, nz = 0, cases = 0;
    int32_t peak = 0;
    for (a = 0; a < 3u; a++)
        for (b = 0; b < 2u; b++)
            for (c = 0; c < 2u; c++)
                for (d = 0; d < 3u; d++)
                    for (e = 0; e < 2u; e++)
                        for (n = 0; n < 4u; n++) {
                            int32_t mx = 0;
                            drift_fresh();
                            pots(MIXES[a], 100, 127, 127, CUT[d], RES[b], DRV[c], KTR[e]);
                            T->p[P_ED_FLT] = 63;
                            trk_note_on(T, NOTES[n], 100);
                            run(buf, 4096);
                            for (i = 0; i < 4096u; i++)
                                mx = (buf[i] < 0 ? -buf[i] : buf[i]) > mx ? (buf[i] < 0 ? -buf[i] : buf[i]) : mx;
                            for (i = 0; i < DRIFT_POLY; i++) {
                                const drift_vc_t *c2 = vc_of(i);
                                int s;
                                for (s = 0; s < 4; s++)
                                    if (!(c2->l.s[s] > -1e4f && c2->l.s[s] < 1e4f))
                                        bad_n++;
                            }
                            cases++;
                            nz += mx > 0;
                            peak = mx > peak ? mx : peak;
                        }
    check(bad_n == 0 && peak <= 70000 && nz > cases / 2u,
          "corners: %u settings (MIX x RES x DRV x CUT x KTR x notes C0..G9, DRFT VAR PWM ENV on): every filter state finite, peak %d (<= 70000), %u sounded",
          cases, (int)peak, nz);
}


/* ------------------------------------------------------------ the DCO (I4, I3) --- */
/* The DCO is the donor's (KR106Oscillators.h): a linear ramp, the comparator pulse, the CD4013 sub, one polyBLEP at each edge.
 * Inharmonic energy is what a wrong ramp or an un-bandlimited step shows as: every partial of a bandlimited saw sits on a multiple
 * of the fundamental. Measured with MIX 64, the filter open, DRFT 0, a 32768-point Hann FFT; a harmonic owns its bin +-5. */
static double inharmonic_db(unsigned note)
{
    const unsigned N = 32768;
    double f0 = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0), eh = 0.0, ei = 0.0;
    unsigned k;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, note, 100);
    run(0, 16384);
    run(buf, N);
    fft_mag(buf, N, mag2_);
    for (k = (unsigned)(0.5 * f0 * N / FSD); k < N / 2u; k++) {
        double h = (double)k * FSD / N / f0, d = h - floor(h + 0.5);
        double e = (double)mag2_[k] * mag2_[k];
        if (fabs(d) * f0 * N / FSD <= 5.0 && floor(h + 0.5) >= 1.0)
            eh += e;
        else
            ei += e;
    }
    return 10.0 * log10(ei / eh + 1e-30);
}
static void test_inharmonic(void)
{
    static const unsigned NOTES[3] = {72, 84, 96};
    double d[3], worst = -1e9;
    unsigned i;
    for (i = 0; i < 3u; i++) {
        d[i] = inharmonic_db(NOTES[i]);
        worst = d[i] > worst ? d[i] : worst;
    }
    check(worst < -50.0, "(j) the donor's linear ramp, bandlimited: inharmonic energy at MIDI 72 / 84 / 96 is %.1f / %.1f / %.1f dB (all < -50)",
          d[0], d[1], d[2]);
}

/* the Hann-windowed DFT of x[0..n) evaluated exactly at hz, dB: no scalloping (a peak search over FFT bins reads a harmonic up to
 * 1.4 dB low where it falls between two bins) */
static double tone_db(const int32_t *x, unsigned n, double hz)
{
    double re = 0.0, im = 0.0;
    unsigned i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n), ph = 2.0 * M_PI * hz * (double)i / FSD;
        re += (double)x[i] * w * cos(ph);
        im -= (double)x[i] * w * sin(ph);
    }
    return 20.0 * log10(sqrt(re * re + im * im) + 1e-30);
}

/* the ideal (not bandlimited) waveform gs (2 x - 1) + gp (x < e ? -1 : 1): the magnitude of its harmonics 1..4 */
static void ideal_h(double gs, double gp, double e, double *h)
{
    const int M = 16384;
    int n, i;
    for (n = 1; n <= 4; n++) {
        double re = 0.0, im = 0.0;
        for (i = 0; i < M; i++) {
            double x = ((double)i + 0.5) / M, w = gs * (2.0 * x - 1.0) + gp * (x < e ? -1.0 : 1.0);
            re += w * cos(2.0 * M_PI * n * x);
            im -= w * sin(2.0 * M_PI * n * x);
        }
        h[n] = sqrt(re * re + im * im) / M;
    }
}
/* MIX -> the DCO's gains (the plan's table, the donor's J106 amplitudes saw 0.6 / pulse 0.5), PW -> the duty (the donor's pw range
 * 0.5 .. 0.95, J106 inverts it: the low part is 1 - pw of the cycle). The spectrum of 220 Hz at the filter open must follow it:
 * this pins the amplitudes, the PW law, the PW polarity against the saw and the MIX law, from the output. */
static void test_dco_spectrum(void)
{
    static const struct { int mix, pw; } C[] = {{64, 64}, {127, 64}, {127, 0}, {127, 40}, {96, 64}, {80, 64}, {96, 40}, {112, 20}};
    unsigned c, ok = 1;
    char msg[600] = "";
    for (c = 0; c < NELEM(C); c++) {
        double e = (double)C[c].mix, gs = (e <= 64.0 ? 1.0 : (127.0 - e) / 63.0) * 0.6, gp = (e <= 64.0 ? 0.0 : (e - 64.0) / 63.0) * 0.5;
        double pwn = C[c].pw < 64 ? (64.0 - C[c].pw) / 64.0 : 0.0, pw = 0.5 + pwn * 0.45, h[5], f[5], worst = 0.0;
        unsigned n;
        ideal_h(gs, gp, 1.0 - pw, h);
        drift_fresh();
        pots(C[c].mix, C[c].pw, 0, 0, 127, 0, 0, 0);
        trk_note_on(T, 57, 100);
        run(0, 16384);
        run(buf, NFFT * 2u);
        for (n = 1; n <= 4; n++)
            f[n] = tone_db(buf, NFFT * 2u, 220.0 * n);
        for (n = 2; n <= 4; n++) {
            double want = 20.0 * log10(h[n] / h[1] + 1e-30), got = f[n] - f[1];
            if (want > -30.0) {
                double d = fabs(got - want);
                worst = d > worst ? d : worst;
                ok &= d < 0.3;
            } else {
                ok &= got < -25.0;
            }
        }
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [MIX %d PW %d: 2nd %+.1f (%+.1f) 3rd %+.1f (%+.1f) 4th %+.1f (%+.1f)]", C[c].mix, C[c].pw,
                 f[2] - f[1], 20.0 * log10(h[2] / h[1] + 1e-30), f[3] - f[1], 20.0 * log10(h[3] / h[1] + 1e-30), f[4] - f[1],
                 20.0 * log10(h[4] / h[1] + 1e-30));
    }
    check(ok, "(k) the DCO's spectrum at 220 Hz against the ideal waveform of the donor's amplitudes / PW law (harmonics 2 3 4 re the 1st, ideal in brackets, within 0.3 dB):%s", msg);
}

/* The waveform itself, not its magnitude spectrum: the saw's charge curvature is in quadrature with its harmonics and the pulse's
 * polarity against the saw is a sign on the real part, so neither moves a harmonic's magnitude; both move the shape. The output of a
 * held 659 Hz note (filter open) folded onto one cycle (512 bins, over 32768 samples), against the ideal waveform of the donor's
 * amplitudes (gs (2 x - 1) + gp (x < e ? -1 : 1), e = 1 - pw): best circular shift and a positive gain / offset by least squares,
 * the 10 % of the cycle around each edge left out (the blep, the filter's ringing). The relative rms error of the rest. */
static double wave_err(int mix, int pw, double *gain)
{
    enum { B = 512 };
    const unsigned N = NFFT * 2u;
    const double f0 = 440.0 * pow(2.0, 7.0 / 12.0);        /* MIDI 76 = 659.255 Hz */
    double fold[B], cnt[B], ideal[B], e = (double)mix, gs = (e <= 64.0 ? 1.0 : (127.0 - e) / 63.0) * 0.6, gp = (e <= 64.0 ? 0.0 : (e - 64.0) / 63.0) * 0.5;
    double pwn = pw < 64 ? (64.0 - pw) / 64.0 : 0.0, ed = 1.0 - (0.5 + pwn * 0.45), best = 1e9;
    unsigned i, sh, b;
    drift_fresh();
    pots(mix, pw, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 76, 100);
    run(0, 16384);
    run(buf, N);
    memset(fold, 0, sizeof fold);
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < N; i++) {
        double x = fmod(f0 * (double)i / FSD, 1.0);
        b = (unsigned)(x * B) % B;
        fold[b] += (double)buf[i];
        cnt[b] += 1.0;
    }
    for (b = 0; b < B; b++) {
        double x = ((double)b + 0.5) / B;
        fold[b] /= cnt[b] > 0.0 ? cnt[b] : 1.0;
        ideal[b] = gs * (2.0 * x - 1.0) + gp * (x < ed ? -1.0 : 1.0);
    }
    for (sh = 0; sh < B; sh++) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0, n = 0, a, c, err;
        for (b = 0; b < B; b++) {
            double x = ((double)b + 0.5) / B, y = fold[(b + sh) % B], v = ideal[b];
            if (x < 0.05 || x > 0.95 || fabs(x - ed) < 0.05)
                continue;
            sx += v, sy += y, sxx += v * v, sxy += v * y, syy += y * y, n += 1.0;
        }
        a = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        c = (sy - a * sx) / n;
        err = (syy - 2.0 * a * sxy - 2.0 * c * sy + a * a * sxx + 2.0 * a * c * sx + n * c * c) / n;
        err = sqrt(err > 0.0 ? err : 0.0) / sqrt(syy / n - (sy / n) * (sy / n));
        if (a > 0.0 && err < best) {
            best = err;
            *gain = a;
        }
    }
    return best;
}
static void test_waveform(void)
{
    static const struct { int mix, pw; } C[] = {{64, 64}, {96, 20}, {96, 40}, {80, 0}};
    double g, w[4];
    unsigned c, ok = 1;
    char msg[300] = "";
    for (c = 0; c < NELEM(C); c++) {
        w[c] = wave_err(C[c].mix, C[c].pw, &g);
        ok &= w[c] < 0.012;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [MIX %d PW %d: %.2f %%]", C[c].mix, C[c].pw, 100.0 * w[c]);
    }
    check(ok, "(p) the DCO's waveform at 659 Hz against the ideal (a linear ramp, the J106-inverted pulse): relative rms error, edges left out (< 1.2 %%):%s", msg);
}

/* the auto PWM: PW >= 64 sweeps the duty by a triangle (0.8 Hz), so the 2nd harmonic of a pulse (nil at a square) swings; at PW 64 it does not */
static double h2_series(int pw, double *lo, double *hi)
{
    unsigned i;
    drift_fresh();
    pots(127, pw, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(0, 4096);
    *lo = 1e9;
    *hi = -1e9;
    for (i = 0; i < 8u; i++) {                     /* eight windows of 0.19 s, 0.37 s apart: the triangle's 1.25 s period is covered */
        double d;
        run(buf, 8192);
        run(0, 8192);
        fft_mag(buf, 8192, mag2_);
        d = peak_db(mag2_, 8192, 440.0) - peak_db(mag2_, 8192, 220.0);
        d = d < -60.0 ? -60.0 : d;                 /* (a true square has no 2nd harmonic: the floor is numerical noise, not a swing) */
        *lo = d < *lo ? d : *lo;
        *hi = d > *hi ? d : *hi;
    }
    return *hi - *lo;
}
static void test_pwm(void)
{
    double lo0, hi0, lo1, hi1, s0 = h2_series(64, &lo0, &hi0), s1 = h2_series(127, &lo1, &hi1);
    check(s0 < 1.0 && s1 > 10.0, "(l) PWM: the pulse's 2nd harmonic over 3 s swings %.1f dB at PW 64 (< 1, a fixed square) and %.1f dB at PW 127 (> 10, %.1f .. %.1f dB)",
          s0, s1, lo1, hi1);
}

/* ------------------------------------------------------------ KTR (I3) --- */
static double ktr_hz(unsigned note, int ktr)
{
    drift_fresh();
    pots(64, 64, 0, 0, (int)cut_for_hz(1000), 127, 0, ktr);
    inj_mode = INJ_SILENCE;
    trk_note_on(T, note, 100);
    run(0, 16384);
    run(buf, NFFT);
    fft_mag(buf, NFFT, mag_);
    return pk_hz(mag_, NFFT, 80.0, 9000.0);
}
static void test_ktr(void)
{
    double a127 = ktr_hz(60, 127), b127 = ktr_hz(72, 127), c127 = ktr_hz(48, 127);
    double a0 = ktr_hz(60, 0), b0 = ktr_hz(72, 0), a64 = ktr_hz(60, 64), b64 = ktr_hz(72, 64);
    int ok = fabs(b127 / a127 - 2.0) < 0.06 && fabs(a127 / c127 - 2.0) < 0.06 &&      /* KTR 127: one octave per octave */
             fabs(b0 / a0 - 1.0) < 0.03 &&                                            /* KTR 0: none */
             fabs(b64 / a64 - 1.418) < 0.05 &&                                        /* KTR 64: 64/127 of an octave per octave */
             fabs(a127 / a0 - 1.0) < 0.03;                                            /* C4 (60) is the zero point */
    check(ok, "(m) KTR: the self-oscillation of RES 127 at CUT 1 kHz: C5/C4 %.3f at KTR 127 (2), %.3f at KTR 64 (1.418), %.3f at KTR 0 (1); C4/C3 %.3f at 127 (2); "
              "C4 at 127 / at 0 %.3f (1: C4 is the zero point)", b127 / a127, b64 / a64, b0 / a0, a127 / c127, a127 / a0);
}

/* ------------------------------------------------------------ the DC blockers (I3) --- */
/* The coupling caps: a 1.59 Hz TPT high-pass after the DCO and another after the VCF (KR106Voice.h mPostOscDcG / mPostVcfDcG), so
 * -6 dB at 1.59 Hz in series (-3 dB each) against the passband. Measured by correlation over whole periods of a seam sine. */
static double lf_gain(double f)
{
    const unsigned P = (unsigned)(FSD / f + 0.5);        /* the period in samples: a multiple of CTL, so f is 44100 / P */
    const unsigned N = P * (f < 5.0 ? 3u : 30u);
    double re = 0.0, im = 0.0, fr = FSD / P;
    unsigned i;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    inj_mode = INJ_SINE;
    inj_amp = 0.05f;
    inj_step = (float)(2.0 * M_PI * fr / FSD / DRIFT_OS);
    trk_note_on(T, 60, 100);
    run(0, 44160);
    run(buf, N);
    for (i = 0; i < N; i++) {
        re += (double)buf[i] * cos(2.0 * M_PI * fr * i / FSD);
        im += (double)buf[i] * sin(2.0 * M_PI * fr * i / FSD);
    }
    return 2.0 * sqrt(re * re + im * im) / N;
}
static void test_dc(void)
{
    double lo = lf_gain(44100.0 / 27648.0), ref = lf_gain(44100.0 / 2208.0), db = 20.0 * log10(lo / ref);
    check(fabs(db + 6.0) < 0.8, "(n) the two 1.59 Hz DC blockers in series: %.2f Hz is %.2f dB against %.2f Hz (-6 +- 0.8; one blocker alone would read -3)",
          44100.0 / 27648.0, db, 44100.0 / 2208.0);
}

/* ------------------------------------------------------------ the voice start (I3) --- */
/* The donor's contract (KR106Voice.h Trigger): a retrigger of a sounding voice keeps the oscillator phase and the filter (no click);
 * a voice that has finished starts from the reset. Ours: a fresh voice (voice.c leaves env 0 only then) zeroes the whole state and
 * seeds s[0]; a sounding one is not touched. (The donor never resets its filter, since its idle voice is not skipped; ours is, so
 * a stale resonance would otherwise burst out of the next note.) */
static void test_voice_start(void)
{
    drift_vc_t ref, before;
    unsigned i;
    int ok_fresh, ok_keep, live;
    drift_fresh();
    pots(127, 64, 127, 127, 100, 127, 40, 64);
    T->p[P_REL] = 20;
    trk_note_on(T, 60, 100);
    ref = *vc_of(0);
    run(0, 22050);                                           /* loud, resonant, the filter ringing */
    live = vc_of(0)->l.s[1] != 0.0f && vc_of(0)->pos != 0.0f;
    trk_note_off(T, 60);
    for (i = 0; i < 400u && T->v[0].active; i++)
        run(0, CTL);
    live &= !T->v[0].active;
    trk_note_on(T, 60, 100);
    ok_fresh = live && !memcmp(vc_of(0), &ref, sizeof ref);
    check(ok_fresh, "(o) a new note on a finished voice starts from the reset: the state equals a never-played voice's (after %u blocks of release, state was dirty: %d)", i, live);
    run(0, 8192);
    before = *vc_of(0);
    trk_note_on(T, 64, 100);                                 /* a retrigger / steal of the sounding voice (DRIFT has one voice) */
    ok_keep = before.pos != 0.0f && before.l.s[1] != 0.0f && !memcmp(vc_of(0), &before, sizeof before);
    check(ok_keep, "(o) a retrigger of a sounding voice keeps the phase (%.3f) and the filter states (s1 %.3g): untouched", (double)before.pos, (double)before.l.s[1]);
}

/* ------------------------------------------------------------ P_FTYPE beyond DRIFT's four positions (I2) --- */
/* The track-persistent P_FTYPE takes the values other engines' filters use (4 HP24, 5 NOTCH ..): on DRIFT, whose HPF has four
 * positions and no page of its own, every value above them is the flat position, never the 754 Hz bass cut */
static void test_ftype_fallback(void)
{
    static int32_t ref[CTL * 64u], got[CTL * 64u];
    static const int V[5] = {4, 5, 7, 127, -3};
    unsigned i;
    int ok = 1;
    drift_fresh();
    pots(96, 64, 0, 0, 100, 40, 0, 64);
    T->p[P_FTYPE] = 0;
    trk_note_on(T, 40, 100);
    run(ref, CTL * 64u);
    for (i = 0; i < 5u; i++) {
        drift_fresh();
        pots(96, 64, 0, 0, 100, 40, 0, 64);
        T->p[P_FTYPE] = (int16_t)V[i];
        trk_note_on(T, 40, 100);
        run(got, CTL * 64u);
        ok &= !memcmp(ref, got, sizeof ref);
    }
    check(ok, "(h) P_FTYPE 4, 5, 7, 127 and -3 render exactly as P_FTYPE 0 (flat): no hidden bass cut from another engine's setting");
}

/* ------------------------------------------------------------ (i) --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static int32_t mix_out[2 * CTL];
/* regress.c's job_cpu: parts[k] = {engine, preset, notes} (POLY, SUS 127, they ring out), 0.5 s to settle, then 1 s counted */
static double mix_cost(const uint8_t (*parts)[3])
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    uint32_t p, i, k, nb = FS / CTL;
    uint64_t i0 = 0;
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
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    inj_mode = INJ_OFF;
    host_tracks_init();
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], parts[p][0], parts[p][1]);
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        for (i = 0; i < parts[p][2]; i++)
            trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
        if (!parts[p][2])
            trk[p].p[P_MUTE] = 1;
    }
    for (k = 0; k < nb / 2u + nb; k++) {
        if (k == nb / 2u)
            i0 = instr_now();
        mix_block(mix_out, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0.0;
}

/* What is asserted, and what is only measured. The target is a full four-track pattern at <= 80 % of one core on the device. The host
 * figure is the only instrument here (target_budget.py counts integer loops and cannot see FPU opcodes, and a divide is one
 * instruction here and several on the device), so this file cannot establish that claim and does not assert it: Task 8.1's PERF run
 * on the FM-1 is the gate. Two things it can do:
 *  - DRIFT_COST_MAX pins the DRIFT part itself (its dearest preset at the voice cap, the mix included) at +10 % of the measured worst,
 *    at -O2 (the units of regress.c and acid_test.c; another compiler or -O level moves it: re-measure and move the pin with a stated
 *    reason, never to hide a regression), as ACID's (f) does for its voice;
 *  - it MEASURES and prints the dearest legal four-track mix it can construct (the figure Task 8.1 must check on the device,
 *    recorded in docs/BASELINE.md): the engines' dearest presets, every split of the shared 8-voice budget over one part per engine
 *    (each engine within its own voice cap), the dearest total. 4parts_light and 4parts_full are printed for reference. */
#define DRIFT_COST_MAX 950.0   /* the measured worst (HOOVER, 860 at DRIFT_POLY 1, DRIFT_OS 1, -O2) + 10 % */

static uint32_t engine_cap(uint32_t e)
{
    uint32_t c = ENGINES[e]->poly;
    return c && c < NVOICE ? c : NVOICE;
}
/* the dearest preset of engine e playing v notes alone (the mix included) */
static uint32_t dearest_preset(uint32_t e, uint32_t v, double *cost)
{
    uint8_t parts[NPART][3];
    uint32_t k, best = 0;
    double worst = 0.0;
    for (k = 0; k < ENGINES[e]->npresets; k++) {
        double c;
        memset(parts, 0, sizeof parts);
        parts[0][0] = (uint8_t)e, parts[0][1] = (uint8_t)k, parts[0][2] = (uint8_t)v;
        c = mix_cost((const uint8_t(*)[3])parts);
        if (c > worst) {
            worst = c;
            best = k;
        }
    }
    *cost = worst;
    return best;
}

static void test_cost(void)
{
    uint8_t parts[NPART][3];
    double idle, full, light, worst, wmix = 0.0;
    uint32_t pre[4], k, drift_k, v[4], w[4] = {0, 0, 0, 0};
    static const uint8_t ENG[4] = {ENGI_OBX, ENGI_WAVE, ENGI_ACID, ENGI_DRIFT};
    if (!instr_now()) {
        printf("drift: SKIP (i) cost: NOT CHECKED, there is no instruction counter on this host (macOS proc_pid_rusage only); "
               "run it on a Mac, and Task 8.1 PERF is the gate on the device\n");
        return;
    }
    memset(parts, 0, sizeof parts);
    idle = mix_cost((const uint8_t(*)[3])parts);
    parts[0][0] = ENGI_WAVE, parts[0][2] = 8;
    parts[1][0] = ENGI_OBX, parts[1][2] = 8;
    parts[2][0] = ENGI_DRIFT, parts[2][2] = 4;
    parts[3][0] = ENGI_ACID, parts[3][2] = 1;
    full = mix_cost((const uint8_t(*)[3])parts);
    parts[0][2] = 3, parts[1][2] = 3, parts[2][2] = 2, parts[3][2] = 1;
    light = mix_cost((const uint8_t(*)[3])parts);
    drift_k = dearest_preset(ENGI_DRIFT, DRIFT_POLY, &worst);
    printf("drift: (i) DRIFT_POLY %d, DRIFT_OS %d: the mix alone %.0f; the DRIFT part alone, worst preset %s %.0f (%.0f per voice)\n", (int)DRIFT_POLY,
           (int)DRIFT_OS, idle, ENGINES[ENGI_DRIFT]->presets[drift_k].name, worst, (worst - idle) / DRIFT_POLY);
    check(worst <= DRIFT_COST_MAX, "(i) the DRIFT part %.0f (%s, %d voice) is within its pin %.0f (the measured worst + 10 %%, -O2)", worst,
          ENGINES[ENGI_DRIFT]->presets[drift_k].name, (int)DRIFT_POLY, DRIFT_COST_MAX);
    /* the dearest legal mix: each engine's dearest preset at its cap (for the ranking), every split of the 8 voices */
    for (k = 0; k < 4u; k++) {
        double c;
        pre[k] = dearest_preset(ENG[k], engine_cap(ENG[k]), &c);
    }
    for (v[0] = 0; v[0] <= engine_cap(ENG[0]); v[0]++)
        for (v[1] = 0; v[1] <= engine_cap(ENG[1]); v[1]++)
            for (v[2] = 0; v[2] <= engine_cap(ENG[2]); v[2]++)
                for (v[3] = 0; v[3] <= engine_cap(ENG[3]); v[3]++) {
                    double c;
                    uint32_t i, tot = v[0] + v[1] + v[2] + v[3];
                    if (tot != NVOICE)               /* the dearest has the whole budget in play (more voices never cost less) */
                        continue;
                    memset(parts, 0, sizeof parts);
                    for (i = 0; i < 4u; i++)
                        parts[i][0] = ENG[i], parts[i][1] = (uint8_t)pre[i], parts[i][2] = (uint8_t)v[i];
                    c = mix_cost((const uint8_t(*)[3])parts);
                    if (c > wmix) {
                        wmix = c;
                        for (i = 0; i < 4u; i++)
                            w[i] = v[i];
                    }
                }
    printf("drift: (i) measured, not asserted (Task 8.1 checks the device): 4parts_light %.0f, 4parts_full %.0f; the dearest legal mix (8 voices) %.0f = "
           "OB-Z %s x%u + WAVE %s x%u + ACID %s x%u + DRIFT %s x%u, one part each (idle mix %.0f)\n", light, full, wmix,
           ENGINES[ENGI_OBX]->presets[pre[0]].name, w[0], ENGINES[ENGI_WAVE]->presets[pre[1]].name, w[1], ENGINES[ENGI_ACID]->presets[pre[2]].name, w[2],
           ENGINES[ENGI_DRIFT]->presets[pre[3]].name, w[3], idle);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1)
        refdir = argv[1];
    test_ladder_ref();
    test_halfband();
    test_slope();
    test_thd();
    test_selfosc();
    test_drift();
    test_variance();
    test_mix();
    test_hpf();
    test_corners();
    test_inharmonic();
    test_dco_spectrum();
    test_waveform();
    test_pwm();
    test_ktr();
    test_dc();
    test_voice_start();
    test_ftype_fallback();
    test_cost();
    printf("drift_test: %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}

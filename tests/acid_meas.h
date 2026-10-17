/* SPDX-License-Identifier: GPL-3.0-only */
/* acid_meas.h: the measurements tests/acid_ref.c (the donor bass303, unmodified) and tests/acid_test.c (the ACID engine)
 * both make, so a reference vector and the figure compared against it come out of the same code. Host only.
 * Samples are int32 (the engine's Q15 domain: full scale 32768); spectra come from tests/dspfft.h. */
#ifndef ACID_MEAS_H
#define ACID_MEAS_H
#include <math.h>
#include <stdint.h>
#include "dspfft.h"

#define AM_FS 44100.0
#define AM_BLK 32u                       /* the engine's CTL: every note event falls on a block boundary */
#define AM_N 8192u                       /* the FFT window of the impulse responses */
#define AM_NB 21u                        /* one-third-octave bands, 100 Hz * 2^(k/3), k = 0..20 (to 10 kHz) */

static const uint8_t AM_CUT[5] = {20, 45, 70, 95, 120};
static const uint8_t AM_RES[5] = {0, 32, 64, 96, 127};
static const uint8_t AM_SO_CUT[4] = {30, 60, 90, 120};

/* the impulse measurements (tests/acid_ref.c IMPULSE build, tests/acid_test.c): the oscillator replaced by silence
 * and one impulse of AM_IMP (the B3_IN seam in bass303.c), 1.2 s into a note of C2 (the filter envelope has settled by then),
 * AM_N samples of the response kept. ENVMOD 0, DECAY 0, ACCENT 0, DRIVE 0 */
#define AM_IMP 500.0f
#define AM_IMP_AT 1650u                  /* blocks into the note (1.2 s) */

/* band k's level in dB (10 log10 of the energy; relative: the Hann window scales every band alike) */
static double am_band(const float *mag, unsigned k)
{
    double c = 100.0 * pow(2.0, k / 3.0), lo = c / pow(2.0, 1.0 / 6.0), hi = c * pow(2.0, 1.0 / 6.0);
    return (double)band_db(mag, AM_N, AM_FS, (float)lo, (float)hi);
}

/* the strongest bin in [lo, hi] Hz, parabolic fit; prom = its power over the mean power of the bins around it (dB) */
static double am_peak(const float *mag, unsigned n, double lo, double hi, double *prom)
{
    unsigned k, a = (unsigned)ceil(lo * n / AM_FS), b = (unsigned)floor(hi * n / AM_FS), best = a;
    double den, d, s = 0.0, pk;
    unsigned cnt = 0;
    for (k = a; k <= b; k++)
        if (mag[k] > mag[best])
            best = k;
    den = (double)mag[best - 1u] - 2.0 * mag[best] + mag[best + 1u];
    d = den != 0.0 ? 0.5 * ((double)mag[best - 1u] - mag[best + 1u]) / den : 0.0;
    pk = (double)mag[best] * mag[best];
    for (k = a; k <= b; k++)
        if (k + 4u < best || k > best + 4u) {
            s += (double)mag[k] * mag[k];
            cnt++;
        }
    *prom = 10.0 * log10(pk / (s / (cnt ? cnt : 1u) + 1e-30));
    return ((double)best + d) * AM_FS / (double)n;
}

/* the power-weighted spectral centroid (Hz) of the n = 4096 samples at x, 50 Hz .. 12 kHz */
static double am_centroid(const int32_t *x)
{
    static float mag[2048];
    unsigned k;
    double sw = 0.0, s = 0.0;
    fft_mag(x, 4096u, mag);
    for (k = 1; k < 2048u; k++) {
        double f = k * AM_FS / 4096.0, p = (double)mag[k] * mag[k];
        if (f < 50.0 || f > 12000.0)
            continue;
        s += f * p;
        sw += p;
    }
    return sw > 0.0 ? s / sw : 0.0;
}

static double am_rms_db(const int32_t *x, unsigned n);
/* the decay of a ring in dB per second: the rms of the 512 samples at 512 and at 1536 of an impulse response (further out the
 * Q15 quantisation of the response is the floor) */
static double am_decay(const int32_t *x)
{
    return (am_rms_db(x + 512, 512u) - am_rms_db(x + 1536, 512u)) / (1024.0 / AM_FS);
}

static double am_peak_db(const int32_t *x, unsigned n)       /* sample peak, dB re full scale (32768) */
{
    unsigned i;
    double m = 1.0;
    for (i = 0; i < n; i++)
        if (fabs((double)x[i]) > m)
            m = fabs((double)x[i]);
    return 20.0 * log10(m / 32768.0);
}

static double am_rms_db(const int32_t *x, unsigned n)        /* rms, dB re full scale */
{
    unsigned i;
    double s = 0.0;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return 10.0 * log10(s / (n ? n : 1u) / (32768.0 * 32768.0) + 1e-20);
}

/* the phrases (both programs): 4 steps of 344 blocks (0.25 s), the gate 188 blocks of it; a slide pair */
#define AM_STEP_BLK 344u
#define AM_GATE_BLK 188u
#define AM_SLIDE_AT 192u                 /* blocks: the 48 -> 60 pair, note 2 on here (0.139 s), off at AM_SLIDE_OFF */
#define AM_SLIDE_OFF 384u
#define AM_SLIDE_END 512u                /* blocks rendered (0.37 s) */
#define AM_RMS_WIN 220u                  /* 5 ms */

static int32_t am_f2i(float y)           /* the engine's float -> Q15 (acid_render does the same) */
{
    return !(y > -4.0f && y < 4.0f) ? 0 : (int32_t)(y * 32768.0f);
}
#endif

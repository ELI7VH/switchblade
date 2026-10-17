/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* dspfft.h: a plain radix-2 FFT and two band helpers for the host DSP tests (tests/obx_test.c and
 * the machine tests after it). Host only, float: none of this goes near the firmware, and the
 * header holds no test logic of its own.
 *
 *   fft_mag(x, n, mag)
 *       the magnitude spectrum of n samples (n a power of two, at most DSPFFT_MAX) into mag[n / 2].
 *       Bin k is k * fs / n Hz. A Hann window is applied first: its sidelobes fall away fast enough
 *       that a strong low partial does not set the floor of a distant band, and it scales every bin
 *       alike, so a level difference between two bins, two bands or two renders is unchanged.
 *       An n this cannot transform (zero, not a power of two, or over DSPFFT_MAX) is refused on
 *       stderr and mag[] is left as zeros rather than as whatever the caller passed in, so a wrong
 *       n reads as silence and says so instead of measuring stale or uninitialised memory.
 *
 *   band_db(mag, n, fs, lo_hz, hi_hz)
 *       the energy of the bins whose centres fall in [lo_hz, hi_hz], in dB (10 log10 of the sum of
 *       mag^2). n is the transform length, not the bin count. An empty or silent band, and any band
 *       that does not reach 0 Hz (hi_hz below zero, or below lo_hz), reads DSPFFT_FLOOR_DB.
 */
#ifndef DSPFFT_H
#define DSPFFT_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define DSPFFT_MAX 65536u                 /* the longest transform (the scratch below is static) */
#define DSPFFT_FLOOR_DB (-300.0f)

static void fft_mag(const int32_t *x, unsigned n, float *mag)
{
    static float re[DSPFFT_MAX], im[DSPFFT_MAX];
    static float twr[DSPFFT_MAX / 2u], twi[DSPFFT_MAX / 2u];
    unsigned i, j, len;
    if (!n || n > DSPFFT_MAX || (n & (n - 1u))) {              /* not a usable power of two */
        fprintf(stderr, "fft_mag: n = %u is not a power of two in 1..%u; mag[] zeroed\n", n,
                DSPFFT_MAX);
        for (i = 0; i < n / 2u; i++)                           /* not the caller's stale data */
            mag[i] = 0.0f;
        return;
    }
    for (i = 0; i < n; i++) {                                  /* Hann window into the scratch */
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n);
        re[i] = (float)((double)x[i] * w);
        im[i] = 0.0f;
    }
    for (i = 0; i < n / 2u; i++) {                             /* exp(-2 pi i k / n), k < n / 2 */
        double a = -2.0 * M_PI * (double)i / (double)n;
        twr[i] = (float)cos(a);
        twi[i] = (float)sin(a);
    }
    for (i = 1, j = 0; i < n; i++) {                           /* bit-reversal permutation */
        unsigned b = n >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j ^= b;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {                       /* the butterflies, stage by stage */
        unsigned half = len >> 1, step = n / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < half; j++) {
                float cr = twr[j * step], ci = twi[j * step];
                float pr = re[i + j + half], pi = im[i + j + half];
                float vr = pr * cr - pi * ci, vi = pr * ci + pi * cr;
                float ur = re[i + j], ui = im[i + j];
                re[i + j] = ur + vr;
                im[i + j] = ui + vi;
                re[i + j + half] = ur - vr;
                im[i + j + half] = ui - vi;
            }
    }
    for (i = 0; i < n / 2u; i++)
        mag[i] = (float)sqrt((double)re[i] * re[i] + (double)im[i] * im[i]);
}

static float band_db(const float *mag, unsigned n, float fs, float lo_hz, float hi_hz)
{
    unsigned k, lo, hi, bins = n / 2u;
    double e = 0.0, per_hz;
    /* hi_hz < 0 is its own case: lo_hz is clamped up to 0 below, so a band wholly under 0 Hz (say
     * -10 .. -5) would otherwise pass the hi_hz < lo_hz guard and read the whole spectrum */
    if (!n || !(fs > 0.0f) || hi_hz < lo_hz || hi_hz < 0.0f)
        return DSPFFT_FLOOR_DB;
    per_hz = (double)n / (double)fs;                           /* bins per Hz */
    lo = (unsigned)ceil((lo_hz < 0.0f ? 0.0 : (double)lo_hz) * per_hz);
    if (lo >= bins)
        return DSPFFT_FLOOR_DB;
    hi = (unsigned)floor((double)hi_hz * per_hz);
    if (hi >= bins)
        hi = bins - 1u;
    if (lo > hi)
        return DSPFFT_FLOOR_DB;
    for (k = lo; k <= hi; k++)
        e += (double)mag[k] * (double)mag[k];
    return e > 0.0 ? (float)(10.0 * log10(e)) : DSPFFT_FLOOR_DB;
}

#endif

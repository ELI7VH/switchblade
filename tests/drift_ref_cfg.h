/* SPDX-License-Identifier: GPL-3.0-only */
/* The cases tests/drift_ref.cpp renders through the donor's own kr106::VCF (Ultramaster KR-106, KR106VCF_OPTIMIZED.h) and
 * tests/drift_test.c through firmware/src/dsp_va.c's va_ladder, on the same input. Both include this file, so neither side can
 * drift from the other. Plain C: the C++ renderer and the C test both read it.
 *
 * A case: the cutoff in Hz and the resonance 0..1 (the VCF's own slider range), the oversampling (1, 2, 4), the input
 * gain (drv: > 1 drives the OTA stages into their tanh; 1 is the donor's level) and the saw's fundamental. The grid is 5 cutoffs
 * x 5 resonances at OS 2 (the oversampling DRIFT's DRIFT_OS 2 option plays); the shipped DRIFT_OS 1 has its own rows (a spread of
 * cutoffs and resonances, a loud input, and the resonance fade above frq 0.7); the rest cover OS 4 and the loud inputs at OS 2. The grid stops short of self-oscillation on purpose: there the output is a phase
 * accumulating oscillation, so a 1e-6 error in tan()/exp() is a frequency error, not an amplitude one (test (d) holds the
 * self-oscillation to +-3 % of its pitch instead). */
#ifndef DRIFT_REF_CFG_H
#define DRIFT_REF_CFG_H
#include <math.h>
#include <stdint.h>

#define DRIFT_REF_FS 44100.0
#define DRIFT_REF_N 16384                /* base-rate samples rendered (the buffer holds N * os) */

typedef struct { float hz, res, drv; uint8_t os; float f0; } drift_ref_cfg_t;

static const drift_ref_cfg_t DRIFT_REF_CFG[] = {
    /* the grid: cutoff x resonance, OS 2 */
    {200, 0.00f, 1, 2, 55},  {200, 0.25f, 1, 2, 55},  {200, 0.50f, 1, 2, 55},  {200, 0.70f, 1, 2, 55},  {200, 0.85f, 1, 2, 55},
    {600, 0.00f, 1, 2, 110}, {600, 0.25f, 1, 2, 110}, {600, 0.50f, 1, 2, 110}, {600, 0.70f, 1, 2, 110}, {600, 0.85f, 1, 2, 110},
    {1500, 0.00f, 1, 2, 110}, {1500, 0.25f, 1, 2, 110}, {1500, 0.50f, 1, 2, 110}, {1500, 0.70f, 1, 2, 110}, {1500, 0.85f, 1, 2, 110},
    {4000, 0.00f, 1, 2, 220}, {4000, 0.25f, 1, 2, 220}, {4000, 0.50f, 1, 2, 220}, {4000, 0.70f, 1, 2, 220}, {4000, 0.85f, 1, 2, 220},
    {9000, 0.00f, 1, 2, 220}, {9000, 0.25f, 1, 2, 220}, {9000, 0.50f, 1, 2, 220}, {9000, 0.70f, 1, 2, 220}, {9000, 0.85f, 1, 2, 220},
    /* the other oversamplings */
    {1500, 0.00f, 1, 1, 110}, {1500, 0.50f, 1, 1, 110}, {12000, 0.60f, 1, 1, 220},    /* (1x: k fades above frq 0.7) */
    /* OS 1, the oversampling DRIFT ships */
    {200, 0.70f, 1, 1, 55}, {600, 0.25f, 1, 1, 110}, {4000, 0.00f, 1, 1, 220}, {4000, 0.70f, 1, 1, 220}, {9000, 0.50f, 1, 1, 220},
    {1500, 0.30f, 6, 1, 110},
    {1500, 0.00f, 1, 4, 110}, {1500, 0.50f, 1, 4, 110}, {6000, 0.70f, 1, 4, 220},
    /* loud inputs: the OTA stages saturate */
    {1500, 0.30f, 6, 2, 110}, {600, 0.60f, 12, 2, 110}, {4000, 0.50f, 25, 2, 220},
};
#define DRIFT_REF_NCFG ((int)(sizeof DRIFT_REF_CFG / sizeof DRIFT_REF_CFG[0]))

/* the input at sample n of the oversampled stream (rate FS * os): a band-limited saw, harmonics below 18 kHz,
 * 0.5 * (2 / pi) sum sin(h w n) / h (peak about 0.5) */
static double drift_ref_input(unsigned n, double f0, int os)
{
    double s = 0, w = 2.0 * 3.141592653589793 * f0 * (double)n / (DRIFT_REF_FS * (double)os);
    unsigned h;
    for (h = 1; (double)h * f0 < 18000.0; h++)
        s += sin(w * (double)h) / (double)h;
    return 0.5 * (2.0 / 3.141592653589793) * s;
}
#endif

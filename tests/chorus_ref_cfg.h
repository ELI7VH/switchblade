/* SPDX-License-Identifier: GPL-3.0-only */
/* The cases tests/chorus_ref.cpp renders through the donor's own kr106::Chorus (Ultramaster KR-106, KR106Chorus.h, noise multipliers
 * zero) and tests/chorus_test.c through firmware/src/dsp_chorus.c, on the same input. Both include this file, so neither side can
 * drift from the other. Plain C: the C++ renderer and the C test both read it. The output of a case is N frames of L, R (float32,
 * interleaved) in build/chorus_ref/NN.f32. */
#ifndef CHORUS_REF_CFG_H
#define CHORUS_REF_CFG_H
#include <math.h>
#include <stdint.h>

#define CHORUS_REF_FS 44100.0
enum { CK_SINE, CK_SAW, CK_NOISE, CK_IMP };
#define CK_IMP_START 4096u               /* the impulse train: one impulse every CK_IMP_GAP samples from here (after the 5 ms fade-in) */
#define CK_IMP_GAP 512u

typedef struct { uint8_t mode, kind; float f0, amp; uint32_t n; } chorus_ref_cfg_t;

/* modes 1 (I), 2 (II), 3 (I+II); per mode: a long 1 kHz sine (sidebands at the LFO rate), a 220 Hz sine, a 110 Hz saw, noise, the impulse
 * train (the delay track: rate and sweep range; long enough for 4+ LFO periods of I, 5 of II, 40 of I+II) */
#define CC_MODE(m, nimp) \
    {m, CK_SINE, 1000.0f, 0.5f, 300000u}, {m, CK_SINE, 220.0f, 0.5f, 110250u}, {m, CK_SAW, 110.0f, 0.4f, 110250u}, \
    {m, CK_NOISE, 0.0f, 0.3f, 110250u}, {m, CK_IMP, 0.0f, 0.5f, nimp}
static const chorus_ref_cfg_t CHORUS_REF_CFG[] = {CC_MODE(1, 360000u), CC_MODE(2, 270000u), CC_MODE(3, 230000u)};
#define CHORUS_REF_NCFG ((int)(sizeof CHORUS_REF_CFG / sizeof CHORUS_REF_CFG[0]))

static inline float chorus_ref_input(const chorus_ref_cfg_t *k, uint32_t i)
{
    switch (k->kind) {
    case CK_SINE:
        return k->amp * (float)sin(2.0 * 3.14159265358979323846 * (double)k->f0 * (double)i / CHORUS_REF_FS);
    case CK_SAW: {
        double ph = fmod((double)k->f0 * (double)i / CHORUS_REF_FS, 1.0);
        return k->amp * (float)(2.0 * ph - 1.0);
    }
    case CK_NOISE: {
        uint32_t h = i * 2654435761u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return k->amp * ((float)(int32_t)h * (1.0f / 2147483648.0f));
    }
    default:
        return i >= CK_IMP_START && (i - CK_IMP_START) % CK_IMP_GAP == 0u ? k->amp : 0.0f;
    }
}
#endif

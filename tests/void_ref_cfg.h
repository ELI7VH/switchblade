/* SPDX-License-Identifier: GPL-3.0-only */
/* The cases tools/void_ref.cpp renders through the VOID WAVE plugin's own Filter.cpp and tests/void_test.c through
 * eng_void.c's filter, on the same input: a band-limited saw (harmonics below 18 kHz, 0.8 * (2 / pi) sum sin / h). */
#define VOID_REF_FS 44100
#define VOID_REF_N 44100                 /* samples rendered; the second half compared */
typedef struct { int vf; float hz, res, drv, f0; } void_ref_cfg_t;   /* vf: eng_void.c VF_* */
static const void_ref_cfg_t VOID_REF_CFG[] = {
    /* LP24, the factory presets' filter: cutoffs, resonance, drive */
    {0, 400, 0.0f, 0.0f, 110}, {0, 400, 0.3f, 0.0f, 110}, {0, 1200, 0.0f, 0.0f, 110}, {0, 1200, 0.5f, 0.0f, 110},
    {0, 1200, 0.8f, 0.3f, 110}, {0, 3000, 0.2f, 0.0f, 440}, {0, 5000, 0.0f, 0.0f, 110}, {0, 200, 0.9f, 0.0f, 55},
    {0, 1200, 0.0f, 0.6f, 110},
    /* the SVF types */
    {3, 1000, 0.0f, 0.0f, 110}, {3, 1000, 0.6f, 0.0f, 110}, {2, 2000, 0.2f, 0.0f, 220}, {1, 1000, 0.3f, 0.0f, 110},
    {4, 1500, 0.2f, 0.0f, 110}, {5, 1000, 0.3f, 0.0f, 110}, {3, 800, 0.9f, 0.0f, 110}, {2, 1500, 0.95f, 0.0f, 110},
    {0, 2000, 0.4f, 0.15f, 220},
};
#define VOID_REF_NCFG (sizeof VOID_REF_CFG / sizeof VOID_REF_CFG[0])
static double void_ref_input(unsigned n, double f0)
{
    double s = 0;
    unsigned h;
    for (h = 1; h * f0 < 18000.0; h++)
        s += sin(2.0 * 3.141592653589793 * h * f0 * n / VOID_REF_FS) / h;
    return 0.8 * (2.0 / 3.141592653589793) * s;
}

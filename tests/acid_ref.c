/* SPDX-License-Identifier: GPL-3.0-only */
/* acid_ref.c: the reference vectors of tests/acid_test.c, rendered by the donor, fm1-x0x's bass303.c, with the same
 * pots ACID's mapping hands it. Two builds of this file (tests/acid_ref.sh runs both), writing into DIR:
 *   plain (the donor UNMODIFIED, included from its own tree):
 *     phrase_acc.txt   "step n peak_db centroid_hz": four steps of one note, unaccented / accented / unaccented / accented
 *     phrase_slide.txt "rms k db": 5 ms rms of a 48 -> 60 slide pair, window k
 *   -DIMPULSE (the donor with ONE line inserted, `s = B3_IN(s);` ahead of its pre-filter highpass, in a temp copy: the
 *   donor has no input to inject an impulse into; the line is the same seam bass303.c carries, a no-op in the product):
 *     grid.txt     "grid CUT RES b0..b20": the one-third-octave band levels (dB) of the filter's impulse response, 5 x 5
 *     selfosc.txt  "selfosc CUT hz prom decay": RES 127: the resonance peak (Hz), its prominence (dB), the ring's decay (dB/s)
 * Everything lands on 32-sample blocks (the engine's CTL), as the ACID engine hears it. The donor's DECAY maps 200 ms .. 2 s,
 * ACID's 200 ms .. 2.5 s: every stimulus uses DECAY pot 0 (200 ms both) so the two compare like for like. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "acid_meas.h"
#ifdef IMPULSE
static int imp_pending;
static float imp_in(float s) { float r = imp_pending ? AM_IMP : 0.0f; (void)s; imp_pending = 0; return r; }
#define B3_IN(s) imp_in(s)
#endif
#include "bass303.c"

static bass303_t B;

/* the pots ACID's mapping sets, and the ones it fixes (VOLUME 96, SLIDE 21) */
static void setup(int wave, int cut, int res, int envmod, int decay, int accent, int drive)
{
    bass303_init(&B);
    bass303_set(&B, BASS303_WAVE, wave);
    bass303_set(&B, BASS303_CUTOFF, cut);
    bass303_set(&B, BASS303_RESO, res);
    bass303_set(&B, BASS303_ENVMOD, envmod);
    bass303_set(&B, BASS303_DECAY, decay);
    bass303_set(&B, BASS303_ACCENT, accent);
    bass303_set(&B, BASS303_TUNE, 64);
    bass303_set(&B, BASS303_VOLUME, 96);
    bass303_set(&B, BASS303_DRIVE, drive);
    bass303_set(&B, BASS303_DRVTYPE, BASS303_DRV_SOFT);
    bass303_set(&B, BASS303_SLIDE, 21);
    bass303_set(&B, BASS303_ACCDEC, 7);
}

static void blocks(int32_t *out, unsigned nblk)
{
    float f[AM_BLK];
    unsigned b, i;
    for (b = 0; b < nblk; b++) {
        bass303_render(&B, f, (int)AM_BLK);
        for (i = 0; i < AM_BLK; i++)
            out[b * AM_BLK + i] = am_f2i(f[i]);
    }
}

static int32_t buf[AM_SLIDE_END * AM_BLK + AM_N];
#ifdef IMPULSE
/* a note of `note`, then at block AM_IMP_AT one impulse; the AM_N samples of the response into out */
static void imp_response(int cut, int res, int note, int32_t *out)
{
    static int32_t skip[AM_IMP_AT * AM_BLK];
    setup(0, cut, res, 0, 0, 0, 0);
    bass303_note_on(&B, note, 0, 0);
    blocks(skip, AM_IMP_AT);
    imp_pending = 1;
    blocks(out, AM_N / AM_BLK);
}
#endif
static float mag[AM_N / 2u];

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/acid_ref";
    char path[512];
    FILE *f;
    unsigned c, r, k, s, w;
#ifdef IMPULSE
    snprintf(path, sizeof path, "%s/grid.txt", dir);
    f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "# acid_ref.c IMPULSE: the filter's impulse response, C2 (note 36), ENVMOD 0, DECAY 0, ACCENT 0, DRIVE 0, %u blocks in\n", AM_IMP_AT);
    for (c = 0; c < 5u; c++)
        for (r = 0; r < 5u; r++) {
            imp_response(AM_CUT[c], AM_RES[r], 36, buf);
            fft_mag(buf, AM_N, mag);
            fprintf(f, "grid %u %u", AM_CUT[c], AM_RES[r]);
            for (k = 0; k < AM_NB; k++)
                fprintf(f, " %.2f", am_band(mag, k));
            fprintf(f, "\n");
        }
    fclose(f);

    snprintf(path, sizeof path, "%s/selfosc.txt", dir);
    f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "# acid_ref.c IMPULSE: RES 127, the impulse response's peak in 60 Hz .. 8 kHz (hz), its prominence over the rest (dB), the ring's decay (dB/s)\n");
    for (c = 0; c < 4u; c++) {
        double prom, hz;
        imp_response(AM_SO_CUT[c], 127, 36, buf);
        fft_mag(buf, AM_N, mag);
        hz = am_peak(mag, AM_N, 60.0, 8000.0, &prom);
        fprintf(f, "selfosc %u %.2f %.2f %.1f\n", AM_SO_CUT[c], hz, prom, am_decay(buf));
    }
    fclose(f);
    return 0;
#else
    snprintf(path, sizeof path, "%s/phrase_acc.txt", dir);
    f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "# acid_ref.c: note 36 saw, CUT 50, RES 80, ENVMOD 81, DECAY 0, ACCENT 100; steps 0 and 2 plain, 1 and 3 accented;\n"
               "# peak = sample peak over the step (dBFS), centroid = of the 4096 samples after the onset\n");
    setup(0, 50, 80, 81, 0, 100, 0);
    for (s = 0; s < 4u; s++) {
        static int32_t sb[AM_STEP_BLK * AM_BLK];
        unsigned acc = s & 1u;
        bass303_note_on(&B, 36, (int)acc, 0);
        blocks(sb, AM_GATE_BLK);
        bass303_note_off(&B);
        blocks(sb + AM_GATE_BLK * AM_BLK, AM_STEP_BLK - AM_GATE_BLK);
        fprintf(f, "step %u %.3f %.1f\n", s, am_peak_db(sb, AM_STEP_BLK * AM_BLK), am_centroid(sb));
    }
    fclose(f);

    snprintf(path, sizeof path, "%s/phrase_slide.txt", dir);
    f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "# acid_ref.c: saw, CUT 20, RES 0, ENVMOD 0, DECAY 0: note 48, a slide to 60 at block %u, off at %u; 5 ms rms windows\n",
            AM_SLIDE_AT, AM_SLIDE_OFF);
    setup(0, 20, 0, 0, 0, 0, 0);
    bass303_note_on(&B, 48, 0, 0);
    blocks(buf, AM_SLIDE_AT);
    bass303_note_on(&B, 60, 0, 1);
    blocks(buf + AM_SLIDE_AT * AM_BLK, AM_SLIDE_OFF - AM_SLIDE_AT);
    bass303_note_off(&B);
    blocks(buf + AM_SLIDE_OFF * AM_BLK, AM_SLIDE_END - AM_SLIDE_OFF);
    for (w = 0; w + AM_RMS_WIN <= AM_SLIDE_END * AM_BLK; w += AM_RMS_WIN)
        fprintf(f, "rms %u %.2f\n", w / AM_RMS_WIN, am_rms_db(buf + w, AM_RMS_WIN));
    fclose(f);
    return 0;
#endif
}

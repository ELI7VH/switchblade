/* SPDX-License-Identifier: GPL-3.0-only */
/* The ARP's WYNDO / SHYFT vectors: SHAYPE's Windows core (windows/shaype/shaype_core.c in the WaveLoop repo, plain C,
 * built here natively) plays a set of cases, and what it plays is written as C for tests/arp_shape_test.c, which plays
 * the same cases through firmware/src/arp_shape.c and wants exactly the same notes. WYNDO and SHYFT are SHAYPE's (the
 * web toy has not got them yet), so the Windows core is their source of truth. WYNDO and SHYFT play every shape of
 * tests/arp_vectors.h (the JS core's cases) and SHAYPE's own and random ones below; UP / DOWN / PEND the latter (on the
 * JS shapes the JS vectors hold already), PEND every shape: SHAYPE's PEND climbs to the top and back in octaves and never
 * wraps (sh_build_table, waveloop a205e3a6), so the JS core's PEND WRAP cases are superseded by these (arp_shape_test.c
 * leaves them out). Every other case of tests/arp_vectors.h but RAND is replayed through the Windows core here and
 * compared: a difference is reported on stderr (and the exit status is 1), never written over.
 *   W=~/repos/waveloop; D=$(mktemp -d)
 *   for f in shaype_core.c shaype_core.h; do git -C $W show origin/main:windows/shaype/$f > $D/$f; done
 *   cc -O1 -w -Ifirmware/src -Itests -I$D -o $D/awv tools/arp_windows_vectors.c -lm
 *   $D/awv "$(git -C $W rev-parse --short origin/main) $(shasum -a 256 $D/shaype_core.c | cut -c1-64)" > tests/arp_window_vectors.h
 * A case plays as the hosts play the core's table (shaype_engine.c): the cycle sh_build_table writes, looped, a step a
 * tick; each step's row in its order (sh_step_row: one pitch once, its longest length). */
#include <stdio.h>
#include <string.h>
#include "arp_shape.c"
#include "arp_vectors.h"
#include "shaype_core.c"

#define MAXEV 40000u
static ShRow rows[SH_MAX_STEPS * 64];
static uint16_t ev[MAXEV * 3u];

/* the case through the Windows core: tick, pitch, length triples into ev; their count */
static uint32_t play(const uint16_t *pat, uint32_t npat, uint32_t steps, const uint8_t *chord, uint32_t nchord, int32_t root,
                     uint32_t dir, uint32_t bound, uint32_t passes, uint32_t *cycle)
{
    static ShPattern p;
    int raw[32], held[32], n, len;
    uint32_t i, tick, k, e = 0;
    p.count = (int)npat;
    for (i = 0; i < npat; i++) {
        p.n[i].step = (int)ARP_STEP(pat[i]);
        p.n[i].row = (int)ARP_ROW(pat[i]);
        p.n[i].length = (int)ARP_LEN(pat[i]);
    }
    for (i = 0; i < nchord; i++)
        raw[i] = chord[i];
    n = sh_root_voicing(raw, (int)nchord, root, held);
    len = sh_build_table(&p, (int)steps, held, n, (int)dir, (int)bound, rows, NULL);
    *cycle = (uint32_t)len / steps;
    for (tick = 0; tick < steps * passes; tick++) {
        const ShRow *r = &rows[tick % (uint32_t)len];
        for (k = 0; k < r->count && e < MAXEV; k++, e++) {
            ev[3u * e] = (uint16_t)tick;
            ev[3u * e + 1u] = r->pitch[k];
            ev[3u * e + 2u] = r->length[k];
        }
    }
    return e;
}

typedef struct { uint8_t steps, bound; int16_t root; uint8_t nchord, chord[16], npat; uint16_t pat[ARP_NMAX]; } shape_t;
static shape_t shapes[1024];
static uint32_t nshapes;
static void shape_add(uint32_t steps, uint32_t bound, int32_t root, const uint8_t *chord, uint32_t nchord,
                      const uint16_t *pat, uint32_t npat)
{
    shape_t s;
    uint32_t i;
    memset(&s, 0, sizeof s);
    s.steps = (uint8_t)steps;
    s.bound = (uint8_t)bound;
    s.root = (int16_t)root;
    s.nchord = (uint8_t)nchord;
    memcpy(s.chord, chord, nchord);
    s.npat = (uint8_t)npat;
    memcpy(s.pat, pat, 2u * npat);
    for (i = 0; i < nshapes; i++)
        if (!memcmp(&shapes[i], &s, sizeof s))
            return;
    shapes[nshapes++] = s;
}

static uint32_t xs = 0x5A1D0u;
static uint32_t xr(void)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return xs;
}

int main(int argc, char **argv)
{
    static const char *const DN[] = {"UP", "DOWN", "PEND", "RAND", "WYNDO", "SHYFT"};
    static const uint8_t DIRS[] = {AD_WYNDO, AD_SHYFT, AD_UP, AD_DOWN, AD_PEND};
    uint32_t k, d, i, e, cycle, njs, jsn = 0, jsdiff = 0, jspend = 0, jspendd = 0, ncase = 0, nev = 0, perdir[6] = {0};
    static char cases[1 << 22];
    size_t cl = 0;

    /* the JS core's cases (not RAND: its draws are the hosts' own) through the Windows core */
    for (k = 0; k < ARP_NVEC; k++) {
        const arp_vec_t *v = &ARP_VEC[k];
        if (v->dir == AD_RAND)
            continue;
        e = play(v->pat, v->npat, v->steps, v->chord, v->nchord, v->root, v->dir, v->bound, v->passes, &cycle);
        if (v->dir == AD_PEND && v->bound == AB_WRAP) {   /* (superseded: SHAYPE's PEND never wraps) */
            jspend++;
            jspendd += e != v->nev || memcmp(ev, v->ev, 6u * e);
            shape_add(v->steps, v->bound, v->root, v->chord, v->nchord, v->pat, v->npat);
            continue;
        }
        jsn++;
        if (e != v->nev || memcmp(ev, v->ev, 6u * e)) {
            jsdiff++;
            fprintf(stderr, "JS case %u (%s %s, %u steps): Windows plays %u notes, JS %u\n", (unsigned)k, DN[v->dir],
                    v->bound ? "OCT" : "WRAP", (unsigned)v->steps, (unsigned)e, (unsigned)v->nev);
        }
        shape_add(v->steps, v->bound, v->root, v->chord, v->nchord, v->pat, v->npat);
    }
    fprintf(stderr, "JS vs Windows: %u cases (UP DOWN PEND OCT), %u differ; PEND WRAP superseded: %u cases, %u differ\n",
            (unsigned)jsn, (unsigned)jsdiff, (unsigned)jspend, (unsigned)jspendd);
    njs = nshapes;   /* (UP DOWN are written only for the shapes below: on these the JS vectors hold already) */

    {   /* SHAYPE's own: F A C E under a 3-row group (README: F A C, then A C E); as tall as the chord (stays put); a group
         * standing on row 2; a tall group over a small chord (SHYFT climbs); one note held; every row */
        static const uint8_t FACE[4] = {53, 57, 60, 64}, CEG[3] = {60, 64, 67}, C1[1] = {60};
        static const uint16_t G3[3] = {ARP_NOTE(0, 0, 1), ARP_NOTE(1, 1, 1), ARP_NOTE(2, 2, 1)};
        static const uint16_t G4[4] = {ARP_NOTE(0, 0, 1), ARP_NOTE(1, 1, 1), ARP_NOTE(2, 2, 1), ARP_NOTE(3, 3, 2)};
        static const uint16_t G2UP[4] = {ARP_NOTE(0, 2, 1), ARP_NOTE(1, 3, 1), ARP_NOTE(2, 4, 1), ARP_NOTE(1, 2, 2)};
        static const uint16_t G8[8] = {ARP_NOTE(0, 0, 1), ARP_NOTE(1, 1, 1), ARP_NOTE(2, 2, 1), ARP_NOTE(3, 3, 1),
                                       ARP_NOTE(4, 4, 1), ARP_NOTE(5, 5, 1), ARP_NOTE(6, 6, 1), ARP_NOTE(7, 7, 1)};
        static const uint16_t G15[3] = {ARP_NOTE(0, 1, 1), ARP_NOTE(0, 5, 3), ARP_NOTE(2, 3, 1)};
        uint32_t b;
        for (b = 0; b < 2u; b++) {
            shape_add(3, b, -1, FACE, 4, G3, 3);
            shape_add(4, b, -1, FACE, 4, G4, 4);
            shape_add(3, b, -1, FACE, 4, G2UP, 4);
            shape_add(8, b, -1, CEG, 3, G8, 8);
            shape_add(8, b, -1, C1, 1, G8, 8);
            shape_add(4, b, -1, CEG, 3, G15, 3);
            shape_add(4, b, 57, FACE, 4, G15, 3);
            shape_add(3, b, -1, C1, 1, G3, 3);
        }
    }
    for (k = 0; k < 60u; k++) {   /* random ones, drawn as the user paints them, chords 1..12 notes */
        arp_pat_t p;
        uint8_t chord[16];
        uint32_t steps = 1u + xr() % (k < 40u ? 16u : 64u), nn = 1u + xr() % 12u, nc = 1u + xr() % 12u;
        int32_t root = -1;
        memset(&p, 0, sizeof p);
        for (i = 0; i < nn; i++) {
            uint32_t s = xr() % steps;
            ash_paint(&p, xr() % 8u, s, 1u + xr() % (steps - s));
        }
        for (i = 0; i < nc; i++)
            chord[i] = (uint8_t)(24u + xr() % 90u);
        if (!(xr() % 3u))
            root = chord[xr() % nc];
        shape_add(steps, xr() & 1u, root, chord, nc, p.note, p.n);
    }

    printf("/* generated by tools/arp_windows_vectors.c from SHAYPE's Windows core (windows/shaype/shaype_core.c, %s)"
           " - do not edit */\n", argc > 1 ? argv[1] : "?");
    for (d = 0; d < sizeof DIRS; d++)
        for (k = 0; k < nshapes; k++) {
            const shape_t *s = &shapes[k];
            uint32_t passes;
            if (DIRS[d] != AD_WYNDO && DIRS[d] != AD_SHYFT && DIRS[d] != AD_PEND && k < njs)
                continue;
            play(s->pat, s->npat, s->steps, s->chord, s->nchord, s->root, DIRS[d], s->bound, 1, &cycle);
            passes = 2u * cycle + 1u;
            if (passes < 4u)
                passes = 4u;
            if (passes * s->steps > 2000u)
                passes = 2000u / s->steps;
            e = play(s->pat, s->npat, s->steps, s->chord, s->nchord, s->root, DIRS[d], s->bound, passes, &cycle);
            if (e >= MAXEV)
                return fprintf(stderr, "too many notes\n"), 2;
            printf("static const uint16_t AW_EV%u[] = {", (unsigned)ncase);
            for (i = 0; i < 3u * e; i++)
                printf(i ? ", %u" : "%u", (unsigned)ev[i]);
            printf("%s};\n", e ? "" : "0");
            cl += (size_t)snprintf(cases + cl, sizeof cases - cl, "    {%u, %u, %u, %u, %d, 1u, %u, {", (unsigned)s->steps,
                                   (unsigned)passes, (unsigned)DIRS[d], (unsigned)s->bound, s->root, (unsigned)s->nchord);
            for (i = 0; i < s->nchord; i++)
                cl += (size_t)snprintf(cases + cl, sizeof cases - cl, i ? ", %u" : "%u", (unsigned)s->chord[i]);
            cl += (size_t)snprintf(cases + cl, sizeof cases - cl, "}, %u, {", (unsigned)s->npat);
            for (i = 0; i < s->npat; i++)
                cl += (size_t)snprintf(cases + cl, sizeof cases - cl, "%sAV_NOTE(%u, %u, %u)", i ? ", " : "",
                                       (unsigned)ARP_STEP(s->pat[i]), (unsigned)ARP_ROW(s->pat[i]),
                                       (unsigned)ARP_LEN(s->pat[i]));
            cl += (size_t)snprintf(cases + cl, sizeof cases - cl, "%s}, %u, AW_EV%u},\n", s->npat ? "" : "0",
                                   (unsigned)e, (unsigned)ncase);
            if (cl + 4096u > sizeof cases) {
                printf("static const arp_vec_t ARP_WVEC%u[] = {\n%s};\n", (unsigned)ncase, cases);
                return fprintf(stderr, "too many cases\n"), 2;
            }
            ncase++;
            nev += e;
            perdir[DIRS[d]]++;
        }
    printf("static const arp_vec_t ARP_WVEC[] = {\n%s};\n", cases);
    printf("#define ARP_NWVEC %uu   /* %u events; WYNDO %u, SHYFT %u, UP %u, DOWN %u, PEND %u cases */\n",
           (unsigned)ncase, (unsigned)nev, (unsigned)perdir[AD_WYNDO], (unsigned)perdir[AD_SHYFT],
           (unsigned)perdir[AD_UP], (unsigned)perdir[AD_DOWN], (unsigned)perdir[AD_PEND]);
    fprintf(stderr, "wrote %u cases, %u events\n", (unsigned)ncase, (unsigned)nev);
    return jsdiff ? 1 : 0;
}

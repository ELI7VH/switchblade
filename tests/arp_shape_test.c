/* SPDX-License-Identifier: GPL-3.0-only */
/* The ARP (SHAYPE, firmware/src/arp_shape.c) against WaveLoop's Shape Arp core: every case of tests/arp_vectors.h
 * (tools/arp_vectors.mjs ran sequence.mjs on them) played through the C core, tick by tick as seq.c arp_fire plays it,
 * must give exactly the notes, steps and lengths the JS core gave, in its order. Then the toy's own unit tests
 * (sequence.test.mjs) as C, and the editing helpers. Run by tests/run_tests.sh. */
#include <stdio.h>
#include <string.h>
#include "arp_shape.c"
#include "arp_vectors.h"

static uint32_t xs;
static uint32_t xs_next(void)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return xs;
}
static int fails;
static void check(const char *what, int ok)
{
    printf("arp: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int play_vec(uint32_t k, int verbose)
{
    const arp_vec_t *v = &ARP_VEC[k];
    arp_pat_t p;
    uint8_t held[16], pitch[ARP_ROWS], len[ARP_ROWS];
    uint32_t n, tick, e = 0, i, j;
    int32_t offset = 0;
    memset(&p, 0, sizeof p);
    p.n = v->npat;
    memcpy(p.note, v->pat, sizeof p.note);
    xs = v->seed ? v->seed : 1u;
    n = ash_voicing(v->chord, v->nchord, v->root, held);
    for (tick = 0; tick < (uint32_t)v->steps * v->passes; tick++) {
        j = ash_tick(&p, v->steps, tick, held, n, v->dir, v->bound, xs_next, &offset, pitch, len);
        for (i = 0; i < j; i++, e++) {
            const uint16_t *w = v->ev + 3u * e;
            if (e >= v->nev || w[0] != tick || w[1] != pitch[i] || w[2] != len[i]) {
                if (verbose)
                    printf("arp:   case %u tick %u: C plays %u x%u, JS %u %u x%u\n", (unsigned)k, (unsigned)tick,
                           (unsigned)pitch[i], (unsigned)len[i], e < v->nev ? (unsigned)w[0] : 0u,
                           e < v->nev ? (unsigned)w[1] : 0u, e < v->nev ? (unsigned)w[2] : 0u);
                return 0;
            }
        }
    }
    if (e != v->nev && verbose)
        printf("arp:   case %u: C played %u notes, JS %u\n", (unsigned)k, (unsigned)e, (unsigned)v->nev);
    return e == v->nev;
}

static int same(const uint8_t *a, uint32_t n, const uint8_t *b, uint32_t m) { return n == m && !memcmp(a, b, n); }

int main(void)
{
    uint32_t k, bad = 0, events = 0;
    char line[96];
    for (k = 0; k < ARP_NVEC; k++) {
        events += ARP_VEC[k].nev;
        if (!play_vec(k, bad < 5u))
            bad++;
    }
    snprintf(line, sizeof line, "JS parity: %u cases, %u notes, every one as sequence.mjs plays it", (unsigned)ARP_NVEC,
             (unsigned)events);
    check(line, !bad);

    {   /* sequence.test.mjs: requested chord shapes move through C E G B D */
        static const uint8_t C[5] = {60, 64, 67, 71, 74};
        static const uint8_t W0[8] = {60, 67, 64, 71, 67, 71, 60, 71}, W1[8] = {64, 71, 67, 74, 71, 74, 64, 74};
        arp_pat_t p;
        uint8_t pitch[ARP_ROWS], len[ARP_ROWS], got[2][8];
        uint32_t s, o, ok = 1;
        ash_load(&p, 0);
        for (o = 0; o < 2u; o++)
            for (s = 0; s < 4u; s++) {
                ok &= ash_events(&p, s, C, 5, (int32_t)o, AB_WRAP, pitch, len) == 2u;
                got[o][2 * s] = pitch[0];
                got[o][2 * s + 1] = pitch[1];
            }
        check("the toy's example: SHAYPE over C E G B D, passes 1 and 2", ok && !memcmp(got[0], W0, 8) && !memcmp(got[1], W1, 8));
    }
    {   /* boundary is independent from direction; negative positions wrap correctly */
        static const uint8_t CEG[3] = {60, 64, 67};
        static const int32_t PEND[5] = {0, 1, 2, 1, 0};
        uint32_t p, ok = 1;
        for (p = 0; p < 5u; p++)
            ok &= ash_pass_offset(p, 3, AD_PEND, xs_next) == PEND[p];
        check("WRAP / OCT, a negative place, PEND 0 1 2 1 0, DOWN -1",
              ash_resolve(CEG, 3, 3, AB_WRAP) == 60 && ash_resolve(CEG, 3, 3, AB_OCT) == 72 &&
              ash_resolve(CEG, 3, -1, AB_OCT) == 55 && ash_resolve(CEG, 0, 0, AB_WRAP) == -1 && ok &&
              ash_pass_offset(1, 3, AD_DOWN, xs_next) == -1);
    }
    {   /* painting replaces intersecting notes but keeps adjacent notes and other rows */
        arp_pat_t p = {3, 0, {ARP_NOTE(0, 0, 3), ARP_NOTE(4, 0, 1), ARP_NOTE(1, 1, 2)}};
        ash_paint(&p, 0, 1, 3);
        check("paint: a note over others in its row replaces them, the rest stay in order",
              p.n == 3u && p.note[0] == ARP_NOTE(4, 0, 1) && p.note[1] == ARP_NOTE(1, 1, 2) && p.note[2] == ARP_NOTE(1, 0, 3));
    }
    {   /* chosen root becomes the lowest pitch while other notes keep their pitch classes */
        static const uint8_t A[3] = {60, 64, 67}, WA[3] = {67, 72, 76}, B[3] = {60, 72, 74}, WB[2] = {72, 74};
        uint8_t o[16];
        check("ROOT: the root the lowest, the others up octaves, each pitch once",
              same(o, ash_voicing(A, 3, 67, o), WA, 3) && same(o, ash_voicing(B, 3, 72, o), WB, 2));
    }
    {   /* the steps cut, a full pattern, the stored form */
        arp_pat_t p = {2, 0, {ARP_NOTE(1, 0, 6), ARP_NOTE(5, 1, 1)}};
        uint32_t i, ok;
        ash_fit(&p, 4);
        check("STEPS shorter: notes past it go, a note over it is cut", p.n == 1u && p.note[0] == ARP_NOTE(1, 0, 3));
        p.n = 0;
        for (i = 0; i < ARP_NMAX; i++)
            ash_paint(&p, i & 7u, i / 8u, 1);
        ok = p.n == ARP_NMAX && !ash_paint(&p, 7, 63, 1) && ash_valid(&p);
        p.note[0] = ARP_NOTE(60, 0, 8);
        check("a full pattern refuses one more; a note past step 64 is not a pattern", ok && !ash_valid(&p));
    }
    {   /* every factory pattern fits its steps and its notes do not overlap in a row */
        uint32_t f, i, j, ok = 1;
        for (f = 0; f < ARP_NFACTORY; f++) {
            arp_pat_t p;
            ash_load(&p, f);
            ok &= ash_valid(&p) && strlen(ARP_FACTORY[f].name) <= 7u;
            for (i = 0; i < p.n; i++) {
                ok &= ARP_STEP(p.note[i]) + ARP_LEN(p.note[i]) <= ARP_FACTORY[f].steps;
                for (j = 0; j < i; j++)
                    ok &= ARP_ROW(p.note[i]) != ARP_ROW(p.note[j]) ||
                          ARP_STEP(p.note[i]) >= ARP_STEP(p.note[j]) + ARP_LEN(p.note[j]) ||
                          ARP_STEP(p.note[j]) >= ARP_STEP(p.note[i]) + ARP_LEN(p.note[i]);
            }
        }
        check("factory patterns: inside their steps, rows without overlaps, short names", ok);
    }
    printf("arp: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}

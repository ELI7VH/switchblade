/* SPDX-License-Identifier: GPL-3.0-only */
/* SHAYPE, WaveLoop's Shape Arp, as the ARP (WaveLoop build). A port of the web toy's core, web/src/arp/core/sequence.mjs
 * in the WaveLoop repo, held to it by tests/arp_shape_test.c against vectors the JS core wrote (tools/arp_vectors.mjs,
 * tests/arp_vectors.h). Nothing here touches a track, the clock or a voice: seq.c is the host (arp_fire), ui_arp.c
 * edits the pattern, project.c stores it. Built on the Mac too (the test includes this file alone).
 *
 * A pattern is notes {step, row, length} on STEPS steps (1..64) by ARP_ROWS rows. A row is a place in the held chord
 * (ash_voicing: low to high, each pitch once, a ROOT moving the notes under it up octaves). Every pass (one cycle of
 * STEPS) moves the rows by an offset (ash_pass_offset: UP, DOWN, PEND, RAND); a place past the chord wraps round the
 * held pitches (WRAP) or moves by octaves (OCT) (ash_resolve). The notes starting on a step sound together, one pitch
 * once (the longest of its lengths), for length x the step x GATE (ash_events). */
#include <stdint.h>

#define ARP_ROWS 8u
#define ARP_STEPS 64u
#define ARP_NMAX 62u                     /* notes a pattern holds: 4 + 62 x 2 bytes = project.c's 128 per track */
/* a note, packed: step 0..63 (bits 0..5), row 0..7 (6..8), length - 1 0..63 (9..14) */
#define ARP_NOTE(s, r, l) ((uint16_t)((s) | (r) << 6 | ((l) - 1u) << 9))
#define ARP_STEP(n) ((uint32_t)(n) & 63u)
#define ARP_ROW(n) (((uint32_t)(n) >> 6) & 7u)
#define ARP_LEN(n) ((((uint32_t)(n) >> 9) & 63u) + 1u)
typedef struct {
    uint8_t n;                           /* notes in use */
    uint8_t rsv;
    uint16_t note[ARP_NMAX];             /* in the order they were written (the JS core's array order) */
} arp_pat_t;

enum { AD_UP, AD_DOWN, AD_PEND, AD_RAND };   /* P_AOCT: DIR (sequence.mjs passOffset's directions) */
enum { AB_WRAP, AB_OCT };                    /* P_AORDER: BOUND */
/* RATE (P_ARATE), sequence.mjs RATES slow to fast, in steps per two beats: 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 */
static const uint8_t ARP_RATE2[8] = {1, 2, 3, 4, 6, 8, 12, 16};

/* sequence.mjs DEFAULT_PATTERN, then the factory patterns (P_APAT 1..: ARP_FACTORY[k - 1]) */
typedef struct { const char *name; uint8_t steps, bound, n; uint16_t note[16]; } arp_factory_t;   /* (loading one sets
                                                                          * STEPS and BOUND too) */
#define AN ARP_NOTE
static const arp_factory_t ARP_FACTORY[] = {
    {"SHAYPE", 4, AB_WRAP, 8, {AN(0, 0, 1), AN(0, 2, 1), AN(1, 1, 1), AN(1, 3, 1), AN(2, 2, 1), AN(2, 3, 1), AN(3, 0, 1),
                               AN(3, 3, 1)}},
    {"UP 4", 4, AB_WRAP, 4, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1)}},
    {"UPDOWN", 6, AB_WRAP, 6, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1), AN(4, 2, 1), AN(5, 1, 1)}},
    {"STABS", 8, AB_WRAP, 9, {AN(0, 0, 1), AN(0, 1, 1), AN(0, 2, 1), AN(3, 0, 1), AN(3, 1, 1), AN(3, 2, 1), AN(6, 0, 2),
                              AN(6, 1, 2), AN(6, 2, 2)}},
    {"OCT RUN", 8, AB_OCT, 8, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1), AN(4, 4, 1), AN(5, 5, 1), AN(6, 6, 1),
                               AN(7, 7, 1)}},
    {"PEDAL", 8, AB_WRAP, 8, {AN(0, 0, 1), AN(1, 2, 1), AN(2, 0, 1), AN(3, 3, 1), AN(4, 0, 1), AN(5, 2, 1), AN(6, 0, 1),
                              AN(7, 1, 1)}},
    {"DRONE", 4, AB_WRAP, 3, {AN(0, 0, 4), AN(1, 2, 1), AN(3, 1, 1)}},
};
#undef AN
#define ARP_NFACTORY (sizeof ARP_FACTORY / sizeof ARP_FACTORY[0])

static void ash_load(arp_pat_t *p, uint32_t k)   /* factory pattern k (0: SHAYPE, the toy's DEFAULT_PATTERN) */
{
    const arp_factory_t *f = &ARP_FACTORY[k < ARP_NFACTORY ? k : 0u];
    uint32_t i;
    p->n = f->n;
    p->rsv = 0;
    for (i = 0; i < ARP_NMAX; i++)
        p->note[i] = i < f->n ? f->note[i] : 0u;
}

/* passOffset(pass, size, direction, random): rnd draws a fraction of 2^32 (Math.floor(random() * size)), only where
 * the toy calls random() (RAND over two notes or more) */
static int32_t ash_pass_offset(uint32_t pass, uint32_t size, uint32_t dir, uint32_t (*rnd)(void))
{
    uint32_t p;
    if (size < 2u)
        return 0;
    if (dir == AD_DOWN)
        return -(int32_t)(pass % size);
    if (dir == AD_RAND)
        return (int32_t)(((uint64_t)rnd() * size) >> 32);
    if (dir == AD_PEND) {
        p = pass % (2u * (size - 1u));
        return (int32_t)(p < size ? p : 2u * (size - 1u) - p);
    }
    return (int32_t)(pass % size);
}

/* rootVoicing(notes, root): each note under root up octaves until it is not, then low to high, each pitch once, 0..127.
 * root < 0: none. out: up to n pitches; their count */
static uint32_t ash_voicing(const uint8_t *in, uint32_t n, int32_t root, uint8_t *out)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < n; i++) {
        int32_t v = in[i];
        if (root >= 0 && v < root)
            v += 12 * ((root - v + 11) / 12);
        if (v < 0 || v > 127)
            continue;
        for (j = 0; j < k && out[j] != v; j++)
            ;
        if (j < k)
            continue;
        for (j = k++; j > 0 && out[j - 1] > v; j--)
            out[j] = out[j - 1];
        out[j] = (uint8_t)v;
    }
    return k;
}

/* resolvePosition(held, position, boundary): held low to high, each once (ash_voicing); -1 = nothing */
static int32_t ash_resolve(const uint8_t *held, uint32_t n, int32_t pos, uint32_t bound)
{
    int32_t len = (int32_t)n, idx, pitch, oct;
    if (!n)
        return -1;
    idx = ((pos % len) + len) % len;
    oct = pos >= 0 ? pos / len : -((-pos + len - 1) / len);   /* Math.floor(position / held.length) */
    pitch = held[idx] + (bound == AB_OCT ? oct * 12 : 0);
    return pitch >= 0 && pitch <= 127 ? pitch : -1;
}

/* eventsAt(pattern, step, held, offset, boundary), merged as the hosts merge it (one pitch once, its longest length,
 * in the order its first note comes in the pattern). pitch / len: up to ARP_ROWS; their count */
static uint32_t ash_events(const arp_pat_t *p, uint32_t step, const uint8_t *held, uint32_t n, int32_t offset,
                           uint32_t bound, uint8_t *pitch, uint8_t *len)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < p->n; i++) {
        uint32_t x = p->note[i], l;
        int32_t v;
        if (ARP_STEP(x) != step)
            continue;
        if ((v = ash_resolve(held, n, (int32_t)ARP_ROW(x) + offset, bound)) < 0)
            continue;
        l = ARP_LEN(x);
        for (j = 0; j < k && pitch[j] != v; j++)
            ;
        if (j < k) {
            if (l > len[j])
                len[j] = (uint8_t)l;
        } else if (k < ARP_ROWS) {
            pitch[k] = (uint8_t)v;
            len[k++] = (uint8_t)l;
        }
    }
    return k;
}

/* one step of the hosts' loop (main.js schedule; seq.c arp_fire): step tick % steps of pass tick / steps over the held
 * chord (ash_voicing), the pass's offset worked out on its first step; the notes it starts (ash_events) */
static uint32_t ash_tick(const arp_pat_t *p, uint32_t steps, uint32_t tick, const uint8_t *held, uint32_t n, uint32_t dir,
                         uint32_t bound, uint32_t (*rnd)(void), int32_t *offset, uint8_t *pitch, uint8_t *len)
{
    uint32_t s = tick % steps;
    if (!s)
        *offset = ash_pass_offset(tick / steps, n, dir, rnd);
    return ash_events(p, s, held, n, *offset, bound, pitch, len);
}

/* the note of row r sounding on step s (it starts there or before and lasts over it): its index, -1 none */
static int32_t ash_find(const arp_pat_t *p, uint32_t row, uint32_t step)
{
    uint32_t i;
    for (i = 0; i < p->n; i++) {
        uint32_t x = p->note[i];
        if (ARP_ROW(x) == row && ARP_STEP(x) <= step && step < ARP_STEP(x) + ARP_LEN(x))
            return (int32_t)i;
    }
    return -1;
}
static void ash_drop(arp_pat_t *p, uint32_t i)   /* note i out, the order of the others kept */
{
    for (; i + 1u < p->n; i++)
        p->note[i] = p->note[i + 1u];
    p->note[--p->n] = 0;                 /* (the slot let go is 0: a stored pattern's tail always is) */
}
/* paintNote(pattern, row, step, length): the notes of the row it meets go, it goes last. 0: the pattern is full */
static int ash_paint(arp_pat_t *p, uint32_t row, uint32_t step, uint32_t len)
{
    uint32_t i = 0, end = step + len;
    while (i < p->n) {
        uint32_t x = p->note[i];
        if (ARP_ROW(x) == row && !(ARP_STEP(x) + ARP_LEN(x) <= step || ARP_STEP(x) >= end))
            ash_drop(p, i);
        else
            i++;
    }
    if (p->n >= ARP_NMAX)
        return 0;
    p->note[p->n++] = ARP_NOTE(step, row, len);
    return 1;
}
/* the steps setting got shorter (the toy's rule): notes past the end go, notes over it are cut */
static void ash_fit(arp_pat_t *p, uint32_t steps)
{
    uint32_t i = 0;
    while (i < p->n) {
        uint32_t x = p->note[i], s = ARP_STEP(x);
        if (s >= steps) {
            ash_drop(p, i);
            continue;
        }
        if (s + ARP_LEN(x) > steps)
            p->note[i] = ARP_NOTE(s, ARP_ROW(x), steps - s);
        i++;
    }
}
/* a stored pattern is one: every note inside 64 steps, no more than ARP_NMAX */
static int ash_valid(const arp_pat_t *p)
{
    uint32_t i;
    if (p->n > ARP_NMAX)
        return 0;
    for (i = 0; i < p->n; i++)
        if ((p->note[i] & 0x8000u) || ARP_STEP(p->note[i]) + ARP_LEN(p->note[i]) > ARP_STEPS)
            return 0;
    return 1;
}

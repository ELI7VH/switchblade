/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop: the sequencer feeds the ARP, and MIDI IN records like the keys (seq.c seq_chord, arp_block; ui_input.c
 * seq_entry). The real sources (ui_test.c's harness), the transport and the queues as the firmware runs them:
 *   shapeChords parity   chords on the sequencer's steps, played through the arp: every note (its step, pitch, end,
 *                        velocity) as the toy's offline form shapeChords gives it for the same chords
 *                        (tests/arp_chords_vectors.h, tools/arp_vectors.mjs --chords); the arp lets go with the chord
 *   edges                ARP off as before; live notes join the sequenced chord; HOLD; the chord's end and STOP leave
 *                        nothing sounding; ARP switched while a chord plays; the external clock's grid
 *   recording            ARP on: the chord played is recorded, not the arp's notes; MIDI live recording on the routed
 *                        track (CH1-4, channel 16 = the selected track, SEL); MIDI step recording on SEQ > STEP */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "arp_chords_vectors.h"

static int bad;
/* ------------------------------------------------------- MIDI OUT capture --- */
typedef struct { uint32_t on, off; uint8_t pitch, vel; } cap_t;
#define CAP_N 1024u
static cap_t cap[CAP_N];
static uint32_t ncap, nblk, cap_open_offs;
static void cap_reset(void)
{
    ncap = nblk = cap_open_offs = 0;
    usb.config = 1;
    mo_r = mo_w;
}
static void cap_drain(void)
{
    while (mo_r != mo_w) {
        uint32_t pkt = midi_out_q[mo_r++ % MQ], st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 127u, d2 = (pkt >> 24) & 127u;
        uint32_t k;
        if (st == 0x90u && d2) {
            if (ncap < CAP_N)
                cap[ncap++] = (cap_t){nblk * CTL, ~0u, (uint8_t)d1, (uint8_t)d2};
        } else if (st == 0x80u || st == 0x90u) {
            for (k = ncap; k--;)
                if (cap[k].pitch == d1 && cap[k].off == ~0u) {
                    cap[k].off = nblk * CTL;
                    break;
                }
            if (k == ~0u)
                cap_open_offs++;                     /* (a note-off for nothing on: never expected) */
        }
    }
}
static void blk1(void)                               /* one control block, then its MIDI OUT */
{
    events_block(CTL);
    cap_drain();
    nblk++;
}
static void blks(uint32_t n) { while (n--) blk1(); }
static uint32_t cap_hanging(void)                    /* notes on with no off */
{
    uint32_t k, n = 0;
    for (k = 0; k < ncap; k++)
        n += cap[k].off == ~0u;
    return n;
}
static uint32_t mout_holding(void)                   /* notes MIDI OUT still holds (midi_control.c mout_held) */
{
    uint32_t k, b, n = 0;
    for (k = 0; k < NTRK; k++)
        for (b = 0; b < 16u; b++)
            n += mout_held[k][b] != 0u;
    return n;
}
/* (gates(): ui_test.c's, the voices of every track with their gate on) */
static void mi(uint32_t st, uint32_t d1, uint32_t d2)   /* a USB MIDI message in (taken by the next block) */
{
    midi_enqueue((st >> 4) | st << 8 | d1 << 16 | d2 << 24, 1u);
}

/* track 1 for the arp: the sequencer at 1/16, GATE 127, no swing, QUANTIZE ON; MIDI OUT EXT (what it plays goes out,
 * captured); the arp: pattern k (ARP_FACTORY), its STEPS and BOUND, then the rest */
static track_t *setup(uint32_t pat, uint32_t dir, uint32_t rate, uint32_t gate, uint32_t len)
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    t = &trk[0];
    song.sel = 0;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_ROUTE] = 0;
    for (i = 0; i < NTRK; i++) {                     /* (the other tracks: no pattern) */
        uint32_t j;
        for (j = 0; j < NSTEP; j++)
            trk[i].step[j] = (step_t){{0}, 0, ST_REST, 0, 0};
    }
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = 2;
    t->p[P_SGATE] = 127;
    t->p[P_SSWING] = 0;
    t->p[P_SQNT] = 1;
    t->p[P_VOICE] = V_POLY;
    t->p[P_QUANT] = 0;
    t->p[P_TRANS] = 0;
    t->p[P_CHRD] = 0;
    t->p[P_MOUT] = MOUT_EXT;
    t->p[P_AMODE] = 1;
    ash_load(&arp_pat[0], pat);
    t->p[P_APROB] = ARP_FACTORY[pat].steps;
    t->p[P_AORDER] = ARP_FACTORY[pat].bound;
    t->p[P_AOCT] = (int16_t)dir;
    t->p[P_ARATE] = (int16_t)rate;
    t->p[P_AGATE] = (int16_t)gate;
    t->p[P_ASWING] = 0;
    t->p[P_AHOLD] = 0;
    t->p[P_AROOT] = 0;
    events_block(CTL);                               /* (the ISR sees ARP on) */
    cap_reset();
    return t;
}
static void chord_steps(track_t *t, const uint8_t *notes, uint32_t n, uint32_t a, uint32_t b, uint32_t vel)
{
    uint32_t i;
    n = n < 4u ? n : 4u;                             /* (a step holds 4) */
    t->step[a] = (step_t){{0}, (uint8_t)n, ST_NOTE, 0, (uint8_t)vel};
    for (i = 0; i < n; i++)
        t->step[a].note[i] = notes[i];
    for (i = a + 1u; i < b; i++)
        t->step[i] = (step_t){{0}, 0, ST_TIE, 0, 0};
}
static uint32_t factory(const char *name)
{
    uint32_t k;
    for (k = 0; k < ARP_NFACTORY; k++)
        if (!strcmp(ARP_FACTORY[k].name, name))
            return k;
    return 0;
}

/* ------------------------------------------------------- shapeChords parity --- */
static int by_tick_pitch(const void *a, const void *b)
{
    const uint32_t *x = a, *y = b;
    return x[0] != y[0] ? (int)x[0] - (int)y[0] : (int)x[1] - (int)y[1];
}
static int parity_case(uint32_t c, int verbose)
{
    const acv_t *v = &ACV[c];
    static uint32_t want[CAP_N][4], got[CAP_N][4];
    uint32_t parp, pseq, i, k, end, n;
    track_t *t = setup(factory(ACV_PAT[v->pat]), v->dir, v->rate, v->gate, v->len);
    int ok = 1;
    t->p[P_AORDER] = v->bound;                       /* (the case's, not the pattern's own) */
    t->p[P_APROB] = v->steps;
    for (i = 0; i < v->nchord; i++)
        chord_steps(t, v->chord[i].notes, v->chord[i].n, v->chord[i].start, v->chord[i].stop, v->chord[i].vel);
    pseq = div_samples(2);
    parp = arp_period(t);
    end = v->len * pseq;
    transport_req = 1;
    while (nblk * CTL < end)
        blk1();
    transport_req = 2;
    blks(4);
    for (k = 0; k < v->nev; k++) {                   /* shapeChords's notes: the end its own, or the chord's first */
        const uint16_t *e = v->ev + 4u * k;        /* (the end of the chords one after another from its step: the */
        uint32_t stop = e[2], ce = ~0u, at = ~0u, j;   /* arp lets go when nothing sounds) */
        for (i = 0; i < v->nchord; i++)
            if (v->chord[i].start * pseq <= e[0] * parp && e[0] * parp < v->chord[i].stop * pseq)
                at = v->chord[i].stop;
        for (j = 0; at != ~0u && j < v->nchord; j++)
            for (i = 0; i < v->nchord; i++)
                if (v->chord[i].start == at)
                    at = v->chord[i].stop;
        if (at != ~0u)
            ce = (uint32_t)((uint64_t)at * pseq * 96u / parp);
        want[k][0] = e[0];
        want[k][1] = e[1];
        want[k][2] = stop < ce ? stop : ce;
        want[k][3] = e[3];
    }
    for (k = 0; k < ncap; k++) {
        got[k][0] = (cap[k].on + parp / 2u) / parp;
        got[k][1] = cap[k].pitch;
        got[k][2] = cap[k].off == ~0u ? ~0u : (uint32_t)(((uint64_t)cap[k].off * 96u + parp / 2u) / parp);
        got[k][3] = cap[k].vel;
    }
    if (getenv("ACV_DEBUG") && (uint32_t)atoi(getenv("ACV_DEBUG")) == c)   /* (ACV_DEBUG=case: its first notes) */
        for (k = 0; k < 14u && k < ncap; k++)
            printf("    fw on %u (%.2f) %u off %u | js %u %u %u\n", (unsigned)cap[k].on, cap[k].on / (double)parp, cap[k].pitch,
                   (unsigned)cap[k].off, v->ev[4u * k], v->ev[4u * k + 1u], v->ev[4u * k + 2u]);
    qsort(want, v->nev, sizeof want[0], by_tick_pitch);
    qsort(got, ncap, sizeof got[0], by_tick_pitch);
    n = ncap;
    if (n != v->nev) {
        ok = 0;
        if (verbose)
            printf("    case %u (%s): the firmware played %u notes, shapeChords %u\n", (unsigned)c, ACV_PAT[v->pat],
                   (unsigned)n, (unsigned)v->nev);
    }
    for (k = 0; ok && k < n; k++) {
        int32_t d = (int32_t)got[k][2] - (int32_t)want[k][2];
        if (got[k][0] != want[k][0] || got[k][1] != want[k][1] || got[k][3] != want[k][3] || d < -3 || d > 3) {
            ok = 0;
            if (verbose)
                printf("    case %u (%s) note %u: firmware tick %u pitch %u end %u vel %u, shapeChords %u %u %u %u\n",
                       (unsigned)c, ACV_PAT[v->pat], (unsigned)k, (unsigned)got[k][0], (unsigned)got[k][1],
                       (unsigned)got[k][2], (unsigned)got[k][3], (unsigned)want[k][0], (unsigned)want[k][1],
                       (unsigned)want[k][2], (unsigned)want[k][3]);
        }
    }
    if (cap_hanging() || cap_open_offs || mout_holding() || t->arp_n)
        ok = 0;
    return ok;
}
static void parity(void)
{
    uint32_t c, fails = 0, notes = 0;
    char line[120];
    for (c = 0; c < ACV_N; c++) {
        notes += ACV[c].nev;
        fails += !parity_case(c, fails < 3u);
    }
    snprintf(line, sizeof line, "SEQ > ARP: %u cases, %u notes as shapeChords plays the same chords (step, pitch, end, vel)",
             (unsigned)ACV_N, (unsigned)notes);
    bad += check(line, !fails);
}

/* ---------------------------------------------------------------- edges --- */
static const uint8_t CEG[3] = {60, 64, 67};
static uint32_t count_pitch(uint32_t from, uint32_t to, uint32_t pitch)   /* note-ons of pitch in [from, to) samples */
{
    uint32_t k, n = 0;
    for (k = 0; k < ncap; k++)
        n += cap[k].on >= from && cap[k].on < to && cap[k].pitch == pitch;
    return n;
}
static void edges(void)
{
    track_t *t;
    uint32_t p, k, b0, ok, n;

    /* ARP off: the sequencer sounds its notes as before (voices / MIDI OUT at the step, no arp chord) */
    t = setup(0, AD_UP, 5, 75, 16);
    t->p[P_AMODE] = 0;
    events_block(CTL);
    cap_reset();
    chord_steps(t, CEG, 3, 0, 4, 90);
    transport_req = 1;
    blk1();
    ok = ncap == 3u && cap[0].on == 0u && !t->seq_arp && t->seq_n == 3u && !t->arp_n;
    blks(4u * div_samples(2) / CTL + 2u);
    ok &= cap[0].off != ~0u && ncap == 3u;
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: ARP off, the step's chord sounds as before (one note-on each, held over its TIEs)", ok &&
                 !cap_hanging());

    /* the arp plays the sequenced chord from PLAY; the chord over (a REST), nothing held: it stops, every note-off sent */
    t = setup(0, AD_UP, 5, 75, 16);
    chord_steps(t, CEG, 3, 0, 4, 90);
    transport_req = 1;
    p = div_samples(2);
    blk1();
    ok = t->seq_arp && t->seq_n == 3u && t->arp_n == 2u && ncap == 2u && cap[0].on == 0u;   /* (SHAYPE step 1: C + G) */
    while (nblk * CTL < 4u * p + CTL)
        blk1();
    ok &= !t->seq_n && !t->arp_n && !cap_hanging();
    n = ncap;
    blks(4u * p / CTL);
    ok &= ncap == n && !t->arp_n;                    /* (steps 5..8 are rests: the arp is silent) */
    bad += check("SEQ > ARP: the sequenced chord is the arp's from PLAY; when it ends, nothing sounds or hangs", ok);

    /* live notes join it: a MIDI note held (channel 1) is one more of the chord's places, gone again when let go */
    t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    chord_steps(t, CEG, 3, 0, 16, 90);
    transport_req = 1;
    blks(8u * p / CTL);
    b0 = nblk * CTL;
    mi(0x90, 72, 100);
    blks(8u * p / CTL - 2u);
    ok = count_pitch(0, b0, 72) == 0u && count_pitch(b0, nblk * CTL, 72) >= 1u && t->nheld == 1u;
    mi(0x80, 72, 0);
    blk1();
    b0 = nblk * CTL;
    while (nblk * CTL < 16u * p)
        blk1();
    transport_req = 2;
    blks(2);
    ok &= count_pitch(b0, nblk * CTL, 72) == 0u && !cap_hanging() && !mout_holding();
    bad += check("SEQ > ARP: a MIDI note held joins the sequenced chord (UP 4 reaches it), let go it leaves", ok);

    /* HOLD: a note tapped stays with the sequenced chord (UP 4 over D C E G), plays on over the rests; HOLD off ends it */
    t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    t->p[P_AHOLD] = 1;
    events_block(CTL);
    chord_steps(t, CEG, 3, 0, 8, 90);
    transport_req = 1;
    blk1();
    mi(0x90, 50, 100);                               /* D3, under the chord: its lowest place */
    blk1();
    mi(0x80, 50, 0);
    while (nblk * CTL < 8u * p)
        blk1();
    ok = t->nheld == 1u && count_pitch(0, 8u * p, 50) >= 1u && count_pitch(0, 8u * p, 60) >= 1u &&
         count_pitch(0, 8u * p, 67) >= 1u;
    while (nblk * CTL < 12u * p)                     /* the sequence rests: the latched D plays on, alone */
        blk1();
    ok &= !t->seq_n && t->nheld == 1u && count_pitch(8u * p + CTL, 12u * p, 50) >= 3u &&
          count_pitch(8u * p + CTL, 12u * p, 60) == 0u;
    t->p[P_AHOLD] = 0;
    blks(2);
    ok &= !t->nheld && !t->arp_n && !mout_holding();
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: HOLD keeps a tapped note with the sequenced chord, plays it over rests; HOLD off ends it", ok &&
                 !cap_hanging());

    /* STOP with the arp playing a sequenced chord: everything ends at once */
    t = setup(0, AD_UP, 5, 100, 16);
    chord_steps(t, CEG, 3, 0, 16, 90);
    transport_req = 1;
    blks(5u * p / CTL + 3u);
    ok = t->arp_n != 0u;
    transport_req = 2;
    blks(2);
    ok &= !t->arp_n && !t->seq_n && !cap_hanging() && !mout_holding();
    t->p[P_MOUT] = MOUT_INT;                         /* .. and the voices (the machine's sound) */
    transport_req = 1;
    blks(5u * p / CTL + 3u);
    ok &= gates() != 0u;
    transport_req = 2;
    blks(2);
    ok &= !gates() && !t->arp_n;
    bad += check("SEQ > ARP: STOP with the arp playing the sequence: its notes end (MIDI OUT and voices), none held", ok);

    /* ARP switched while the chord plays: off, the arp's notes end and the next step sounds the chord itself; on again,
     * those voices end and the arp takes it */
    t = setup(0, AD_UP, 5, 75, 16);
    for (k = 0; k < 16u; k += 2u)
        chord_steps(t, CEG, 3, k, k + 2u, 90);
    transport_req = 1;
    blks(3u * p / CTL);
    t->p[P_AMODE] = 0;
    blk1();
    ok = !t->arp_n && !t->seq_n;
    while (nblk * CTL < 4u * p + CTL)
        blk1();
    ok &= t->seq_n == 3u && !t->seq_arp && count_pitch(4u * p, 4u * p + CTL, 64) == 1u;
    t->p[P_AMODE] = 1;
    blk1();
    ok &= !t->seq_n;
    while (nblk * CTL < 6u * p + CTL)
        blk1();
    ok &= t->seq_arp && t->arp_n != 0u;
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: ARP off mid-chord, the sequence sounds itself next step; on again, the arp takes it", ok &&
                 !cap_hanging() && !mout_holding() && !cap_open_offs);
}

/* the external clock: the arp's steps on its grid, in the block the sequencer's 1/16 steps start in; steady, and with a
 * tempo that wanders (114..126 BPM: the sequencer's position is rescaled as it moves, the arp's with it) */
static void ext_clock(uint32_t wander)
{
    track_t *t = setup(0, AD_UP, 5, 75, 64);
    uint32_t i, k, ok = 1, steps_seen = 0, last = 0xFFFFu, at = 10u, apart = 0;
    song.g[G_CLOCK] = 1;
    events_block(CTL);
    chord_steps(t, (const uint8_t[]){60, 64, 67, 71}, 4, 0, 64, 90);   /* (SHAYPE over C E G B: C+G E+B G+B C+B) */
    cap_reset();
    fm1_ms = at;
    mi(0xFA, 0, 0);
    blk1();
    for (i = 0; i < 6u * 48u; i++) {                 /* 48 steps of 1/16 at about 120 BPM: a pulse every ~20.8 ms */
        at += wander ? 19u + (i / 24u) % 4u : (i % 6u == 5u ? 21u : 21u - (i % 2u));   /* (steady: 125 ms a 6) */
        mi(0xF8, 0, 0);
        while (fm1_ms < at) {
            uint32_t n0 = ncap;
            fm1_ms++;
            blk1();
            if (t->seq_idx != last) {               /* a step started in this block: the arp's with it */
                last = t->seq_idx;
                steps_seen++;
                if (!(ncap > n0 || steps_seen == 1u))
                    apart++;
            } else if (ncap > n0) {
                apart++;                             /* (an arp note away from the sequencer's step) */
            }
        }
    }
    for (k = 0, i = 0; k + 1u < ncap && i < 4u; k += 2u, i++) {
        static const uint8_t PAIRS[4][2] = {{60, 67}, {64, 71}, {67, 71}, {60, 71}};
        ok &= cap[k].pitch == PAIRS[i][0] && cap[k + 1u].pitch == PAIRS[i][1];
    }
    mi(0xFC, 0, 0);
    blks(4);
    ok &= !apart && !t->arp_n && !cap_hanging() && steps_seen >= 40u;
    if (!ok)
        printf("    steps %u, arp steps away from the sequencer's %u\n", (unsigned)steps_seen, (unsigned)apart);
    song.g[G_CLOCK] = 0;
    bad += check(wander ? "SEQ > ARP on an external clock wandering 114..126 BPM: still each arp step with its 1/16 step"
                        : "SEQ > ARP on the external clock: each arp step in the block its 1/16 step starts in, SHAYPE's notes", ok);
}

/* --------------------------------------------------------------- recording --- */
static uint32_t notes_written(const track_t *t)   /* NOTE steps with notes */
{
    uint32_t i, n = 0;
    for (i = 0; i < NSTEP; i++)
        n += t->step[i].time == ST_NOTE && t->step[i].n;
    return n;
}
static void recording(void)
{
    track_t *t;
    uint32_t p, ok;
    /* ARP on, armed: a MIDI chord (channel 1, CH1-4) held from step 3 for two steps is recorded as played; the arp's
     * notes meanwhile are not; the next pass plays the chord through the arp */
    t = setup(0, AD_UP, 3, 75, 16);
    p = div_samples(2);
    song.rec = 1;
    transport_req = 1;
    while (nblk * CTL < 2u * p + CTL)
        blk1();
    mi(0x90, 60, 100); mi(0x90, 64, 100); mi(0x90, 67, 100);
    blk1();
    while (nblk * CTL < 4u * p + p / 4u)
        blk1();
    ok = t->arp_n != 0u;
    mi(0x80, 60, 0); mi(0x80, 64, 0); mi(0x80, 67, 0);
    blk1();
    ok &= notes_written(t) == 1u && t->step[2].n == 3u && t->step[2].note[0] == 60 && t->step[2].note[1] == 64 &&
          t->step[2].note[2] == 67 && t->step[3].time == ST_TIE;
    song.rec = 0;
    while (nblk * CTL < 18u * p + p / 2u)
        blk1();
    ok &= t->seq_arp && t->seq_n == 3u && t->arp_n != 0u;
    transport_req = 2;
    blks(2);
    bad += check("REC with the ARP on: the MIDI chord played is written (one step, a TIE), not the arp's notes; it plays back "
                 "through the arp", ok && !cap_hanging());

    /* MIDI live recording on the track the channel plays: CH1-4 channel 2 -> track 2; channel 16 -> the selected track;
     * SEL: any channel -> the selected track. Quantised to the step as the keys are */
    t = setup(0, AD_UP, 3, 75, 16);
    trk[0].p[P_AMODE] = 0;
    trk[1].p[P_SLEN] = trk[2].p[P_SLEN] = 16;
    trk[1].p[P_VOICE] = trk[2].p[P_VOICE] = V_POLY;
    song.sel = 2;
    song.rec = 0xF;
    transport_req = 1;
    while (nblk * CTL < 5u * p + p / 8u)            /* just after step 6 starts */
        blk1();
    mi(0x91, 62, 100);
    mi(0x9F, 65, 100);
    blk1();
    mi(0x81, 62, 0);
    mi(0x8F, 65, 0);
    blk1();
    ok = trk[1].step[5].n == 1u && trk[1].step[5].note[0] == 62 && trk[2].step[5].n == 1u && trk[2].step[5].note[0] == 65 &&
         notes_written(&trk[0]) == 0u && notes_written(&trk[3]) == 0u;
    song.g[G_ROUTE] = ROUTE_SEL;
    while (nblk * CTL < 8u * p - p / 8u)            /* just before step 9: into step 9 (the nearest) */
        blk1();
    mi(0x98, 69, 100);
    blk1();
    mi(0x88, 69, 0);
    blk1();
    ok &= trk[2].step[8].n == 1u && trk[2].step[8].note[0] == 69 && notes_written(&trk[1]) == 1u;
    transport_req = 2;
    blks(2);
    song.rec = 0;
    song.g[G_ROUTE] = 0;
    bad += check("MIDI live recording: CH1-4 ch 2 -> T2, ch 16 -> the selected T3, SEL any channel -> T3, to the nearest step",
                 ok);
}

/* MIDI step recording on SEQ > STEP (armed, stopped): a MIDI note is a key; held together a chord, all let go moves on */
static void step_rec(void)
{
    track_t *t;
    uint32_t ok;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    t->p[P_AMODE] = 1;                               /* (the ARP on: the chord is written, as played) */
    track_defaults_steps(t);
    go_page(GR_ROLL);
    cursor_set(2);
    frame();
    press(B_REC);
    ok = song.rec == 1u && !song.playing && msg_is("KEYS WRITE STEPS");
    mi(0x9F, 60, 100);                               /* channel 16: the selected track */
    events_block(CTL);
    frame();
    ok &= t->step[2].n == 1u && t->step[2].note[0] == 60 && ui.cursor == 2u;
    mi(0x9F, 64, 100);
    mi(0x9F, 67, 100);
    events_block(CTL);
    frame();
    mi(0x8F, 60, 0);
    events_block(CTL);
    frame();
    ok &= t->step[2].n == 3u && ui.cursor == 2u;   /* (two still held: the step stays open) */
    mi(0x8F, 64, 0);
    mi(0x8F, 67, 0);
    events_block(CTL);
    frame();
    ok &= ui.cursor == 3u && t->step[2].note[1] == 64 && t->step[2].note[2] == 67;
    mi(0x90, 72, 100);                               /* a tap within one pass (CH1-4 channel 1 = T1, selected) */
    mi(0x80, 72, 0);
    events_block(CTL);
    frame();
    ok &= t->step[3].n == 1u && t->step[3].note[0] == 72 && ui.cursor == 4u;
    mi(0x91, 74, 100);                               /* channel 2 plays T2: not the selected track's step */
    events_block(CTL);
    frame();
    mi(0x81, 74, 0);
    events_block(CTL);
    frame();
    ok &= !t->step[4].n && ui.cursor == 4u;
    bad += check("MIDI step recording on STEP: ch 16 / the track's channel write the cursor step, a chord held is one step, "
                 "let go moves on; another track's channel does not", ok);
    press(B_REC);
    song.rec = 0;
}

int main(void)
{
    parity();
    edges();
    ext_clock(0);
    ext_clock(1);
    recording();
    step_rec();
    if (bad) {
        printf("ARP SEQ TEST FAILED (%d)\n", bad);
        return 1;
    }
    puts("SEQ > ARP and MIDI recording: ok");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop: the sequencer feeds the ARP, and MIDI IN records like the keys (seq.c seq_chord, arp_block; ui_input.c
 * seq_entry). The real sources (ui_test.c's harness), the transport and the queues as the firmware runs them:
 *   shapeChords parity   chords on the sequencer's steps, played through the arp: every note (its step, pitch, end,
 *                        velocity) as the toy's offline form shapeChords gives it for the same chords
 *                        (tests/arp_chords_vectors.h, tools/arp_vectors.mjs --chords); the arp lets go with the chord
 *                        (ATTACH NOW and NOTE, up to KEY ATTACH's own: attach_from; PEND WRAP: the Windows core's)
 *   KEY ATTACH           NOW / NOTE / CYCLE, MIDI, the keys and the sequencer's chords (attach)
 *   edges                ARP off as before; live notes join the sequenced chord; HOLD; the chord's end and STOP leave
 *                        nothing sounding; ARP switched while a chord plays; the external clock's grid
 *   recording            ARP on: the chord played is recorded, not the arp's notes; MIDI live recording on the routed
 *                        track (CH1-4, channel 16 = the selected track, SEL); MIDI step recording on SEQ > STEP
 *   stability            a random soak over all of it (MIDI on every routed channel, keys, ARP / HOLD / transport /
 *                        track switches, CC123): every note-off balanced, nothing left sounding or held */
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

/* KEY ATTACH the cases set up with (setup): NOW, what the release cases and the soaks were written for (every key let
 * go ends the notes at once); NOTE (the FM-1's default) and CYCLE have their own cases (attach()) */
static uint32_t setup_attach = AA_NOW;
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
    t->p[P_AXOCT] = 0;
    t->p[P_AATT] = (int16_t)setup_attach;
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
/* where KEY ATTACH parts from shapeChords: a chord following another straight on, mid-pass, with another number of notes
 * (the pass's DIR offset is the new chord's from the step it lands on, as SHAYPE's table: shapeChords keeps the pass's).
 * The arp step it lands on; ~0u none */
static uint32_t attach_from(const acv_t *v, uint32_t pseq, uint32_t parp)
{
    uint32_t i, j, at, cut = ~0u;
    for (i = 0; i < v->nchord; i++)
        for (j = 0; j < v->nchord; j++)
            if (v->chord[j].start == v->chord[i].stop && v->chord[j].n != v->chord[i].n) {
                at = (v->chord[j].start * pseq + parp - 1u) / parp;
                if (at % v->steps && at < cut)
                    cut = at;
            }
    return cut;
}
static uint32_t parity_cut, parity_cut_n;
static int parity_case(uint32_t c, int verbose)
{
    const acv_t *v = &ACV[c];
    static uint32_t want[CAP_N][4], got[CAP_N][4];
    uint32_t parp, pseq, i, k, end, n, cut, nw, ng;
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
    cut = attach_from(v, pseq, parp);
    if (cut != ~0u)
        parity_cut++;
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
        want[k][2] = setup_attach == AA_NOW && ce < stop ? ce : stop;   /* (NOTE: the chord gone, the notes ring to
                                                                         * their own ends) */
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
    for (nw = 0; nw < v->nev && want[nw][0] < cut; nw++)   /* (up to KEY ATTACH's own: attach_from) */
        ;
    for (ng = 0; ng < ncap && got[ng][0] < cut; ng++)
        ;
    parity_cut_n += v->nev - nw;
    n = ng;
    if (n != nw) {
        ok = 0;
        if (verbose)
            printf("    case %u (%s): the firmware played %u notes, shapeChords %u\n", (unsigned)c, ACV_PAT[v->pat],
                   (unsigned)n, (unsigned)nw);
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
static void parity(uint32_t mode)
{
    uint32_t c, fails = 0, notes = 0, cases = 0, pend = 0;
    char line[200];
    setup_attach = mode;
    parity_cut = parity_cut_n = 0;
    for (c = 0; c < ACV_N; c++) {
        if (ACV[c].dir == AD_PEND && ACV[c].bound == AB_WRAP) {   /* superseded: SHAYPE's PEND never wraps */
            pend++;                                                 /* (tests/arp_window_vectors.h holds PEND) */
            continue;
        }
        cases++;
        notes += ACV[c].nev;
        fails += !parity_case(c, fails < 3u);
    }
    snprintf(line, sizeof line, "SEQ > ARP, ATTACH %s: %u cases, %u notes as shapeChords plays the same chords (%u cases to a "
             "mid-pass chord of another size, %u notes after: KEY ATTACH's; %u PEND WRAP: the Windows core's)",
             mode == AA_NOW ? "NOW" : "NOTE", (unsigned)cases, (unsigned)(notes - parity_cut_n), (unsigned)parity_cut,
             (unsigned)parity_cut_n, (unsigned)pend);
    bad += check(line, !fails);
    setup_attach = AA_NOW;
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

    /* OCT (P_AXOCT, OP3 on the ARP pages): what the arp plays, octaves up or down; a note past 127 is not played */
    {
        static const uint8_t HI[3] = {110, 115, 120};
        static const int8_t O[3] = {1, -1, 3};
        uint32_t j;
        ok = 1;
        for (j = 0; j < 3u; j++) {
            t = setup(0, AD_UP, 5, 75, 16);
            t->p[P_AXOCT] = O[j];
            chord_steps(t, j < 2u ? CEG : HI, 3, 0, 4, 90);
            transport_req = 1;
            blk1();
            if (j < 2u)                              /* (SHAYPE step 1: rows 1 and 3, C + G) */
                ok &= ncap == 2u && cap[0].pitch == 60 + 12 * O[j] && cap[1].pitch == 67 + 12 * O[j];
            else                                     /* (110 + 36 and 120 + 36: past 127, neither) */
                ok &= ncap == 0u;
            transport_req = 2;
            blks(2);
            ok &= !cap_hanging();
        }
        t->p[P_AXOCT] = 0;
        bad += check("SEQ > ARP: OCT +1 / -1 moves what the arp plays an octave (C+G -> C+G up / down); past 127: silent", ok);
    }

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

/* ---------------------------------------------------------------- releases --- */
/* The FM-1 report of 2026-10-25 (firmware f673da0, fm1-builds/shaype-test/run.py): SHAYPE on track 1, STEPS 4, RATE
 * 1/8 at 120 BPM, UP, WRAP, GATE 75 %, HOLD off, MOUT BOTH, stopped, internal clock; C E G held over USB MIDI from 0 s
 * and let go at 3.40 s, in the middle of the step that started at 3.25 s (C + E). Its notes must end there, on MIDI OUT
 * and on the machine's voices, every note-on with exactly one note-off. (On the unit they came back stamped 4.005 s:
 * run.py reads what came in only after its 0.6 s sleep, so that was the reading's time, not the FM-1's.)
 * Each case: the chord in (MIDI channel 1, or the FM-1's own keys), let go at rel samples, HOLD on (then off a step
 * later) or off; then a second of nothing. Checked: the chord gone, nothing the arp played sounds in that same block
 * (arp_snd empty, no gate, MIDI OUT holding nothing, every captured note-on closed and none closed twice), no note
 * starts after it, and the voices' notes match MIDI OUT's (BOTH). Fails print why (verbose). */
static uint32_t rnd(uint32_t n);
enum { RL_MIDI, RL_KEYS };
static uint32_t key_of(const track_t *t, uint32_t note)   /* the key that plays note on t now (-1: none) */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (kb_map(t, k) == note)
            return k;
    return ~0u;
}
static uint32_t track_gates(const track_t *t, uint32_t note)   /* t's voices with their gate on (note: any if 128) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].gate && (note > 127u || t->v[i].note == note);
    return n;
}
static int rel_quiet(const track_t *t)               /* nothing the arp started still sounds or is held out there */
{
    return !t->arp_n && !track_gates(t, 128) && !mout_holding() && !cap_hanging() && !cap_open_offs;
}
static int rel_case(uint32_t rel, uint32_t mout, uint32_t hold, uint32_t via, uint32_t ext, int verbose)
{
    track_t *t = setup(0, AD_UP, 3, 75, 16);         /* SHAYPE, 1/8, UP, GATE 75 % (WRAP, 4 steps: its own) */
    uint32_t i, n0, keys = 0, ok = 1, both_bad = 0;
    const char *why = "";
    t->p[P_MOUT] = (int16_t)mout;
    t->p[P_AHOLD] = (int16_t)hold;
    song.g[G_CLOCK] = (int16_t)ext;                  /* (external: never started, the transport stays stopped) */
    events_block(CTL);
    cap_reset();
    for (i = 0; i < 3u; i++)
        if (via == RL_KEYS)
            keys |= 1u << key_of(t, CEG[i]);
        else
            mi(0x90, CEG[i], 100);
    fm1_in.notes = keys;
    while (nblk * CTL < rel) {
        blk1();
        if (mout == MOUT_BOTH) {                     /* the machine sounds exactly what MIDI OUT holds */
            uint32_t k;
            for (k = 0; k < 3u; k++)
                both_bad += !track_gates(t, CEG[k]) != !((mout_held[0][CEG[k] >> 3] >> (CEG[k] & 7u)) & 1u);
        }
    }
    if (!ncap && mout != MOUT_INT) {
        ok = 0;
        why = "the arp played nothing";
    }
    if (via == RL_KEYS)
        fm1_in.notes = 0;
    else
        for (i = 0; i < 3u; i++)
            mi(0x80, CEG[i], 0);
    blk1();
    if (!hold && ok && !rel_quiet(t)) {
        ok = 0;
        why = "the chord let go, the arp's notes still sound";
    }
    if (hold) {                                      /* latched: it plays on; HOLD off ends it at once */
        n0 = ncap;
        blks(2u * arp_period(t) / CTL + 2u);
        if (ok && (!t->nheld || (mout != MOUT_INT && !ext && ncap == n0))) {   /* (stopped external clock: latched, still) */
            ok = 0;
            why = "HOLD: the latched chord did not play on";
        }
        t->p[P_AHOLD] = 0;
        blk1();
        if (ok && !rel_quiet(t)) {
            ok = 0;
            why = "HOLD off, the arp's notes still sound";
        }
    }
    n0 = ncap;
    blks(FS / CTL);
    if (ok && (ncap != n0 || !rel_quiet(t) || t->nheld)) {
        ok = 0;
        why = "a note after the chord was gone, or one left hanging";
    }
    if (ok && both_bad) {
        ok = 0;
        why = "BOTH: the voices and MIDI OUT disagree";
    }
    if (!ok && verbose)
        printf("    release at %u samples (%.3f s), MOUT %u, HOLD %u, %s%s: %s (arp_n %u, gates %u, MIDI OUT holding %u, "
               "hanging %u, offs for nothing %u)\n", (unsigned)rel, rel / (double)FS, (unsigned)mout, (unsigned)hold,
               via == RL_KEYS ? "keys" : "MIDI", ext ? ", external clock stopped" : "", why, (unsigned)t->arp_n,
               (unsigned)track_gates(t, 128), (unsigned)mout_holding(), (unsigned)cap_hanging(), (unsigned)cap_open_offs);
    song.g[G_CLOCK] = 0;
    fm1_in.notes = 0;
    return ok;
}
static void releases(void)
{
    static const uint8_t MOUTS[3] = {MOUT_INT, MOUT_EXT, MOUT_BOTH};
    track_t *t = setup(0, AD_UP, 3, 75, 16);
    uint32_t p = arp_period(t), m, h, v, off, fails = 0, cases = 0, ok;
    /* the unit's own case first: let go at 3.40 s, in step 14 (3.25 s: C + E sounding). Their note-offs at the
     * release, on MIDI OUT and the voices */
    {
        uint32_t rel = (uint32_t)(3.40 * FS), k, ends = 0;
        ok = rel_case(rel, MOUT_BOTH, 0, RL_MIDI, 0, 1);
        for (k = 0; k < ncap; k++)
            if (cap[k].on >= 13u * p - CTL && cap[k].on < 14u * p)
                ends += (cap[k].pitch == 60 || cap[k].pitch == 64) && cap[k].off / CTL == rel / CTL + (rel % CTL != 0u);
        bad += check("ARP release (the FM-1's case): C E G let go at 3.40 s, step 14's C + E end in that block on MIDI OUT "
                     "and the voices, every note-on one note-off", ok && ends == 2u);
    }
    /* every block of a step (from its start to the next one's), MOUT BOTH, MIDI, HOLD off: on the step boundary too */
    for (off = 0; off <= p + CTL; off += CTL) {
        fails += !rel_case(p + off, MOUT_BOTH, 0, RL_MIDI, 0, fails < 3u);
        cases++;
    }
    /* the rest of the matrix at the edges of a step and its gate: INT / EXT / BOTH, HOLD on / off, MIDI / keys */
    for (m = 0; m < 3u; m++)
        for (h = 0; h < 2u; h++)
            for (v = 0; v < 2u; v++) {
                uint32_t g = p * 3u / 4u, at[8] = {0, CTL, g - CTL, g, g + CTL, p / 2u, p - CTL, p}, j;
                for (j = 0; j < 8u; j++) {
                    fails += !rel_case(5u * p + at[j], MOUTS[m], h, v, 0, fails < 3u);
                    cases++;
                }
            }
    bad += check("ARP release mid-step: every block of a step and the edges of its gate, MOUT INT / EXT / BOTH, HOLD on / "
                 "off, MIDI / the keys: the arp's notes end with the chord, no hang, no double note-off", !fails);
    printf("    (%u release cases)\n", (unsigned)cases);

    /* the external clock selected, stopped: the chord plays its first step, the gate still ends in real time; let go
     * mid-gate and after it */
    fails = 0;
    for (m = 0; m < 3u; m++)
        for (h = 0; h < 2u; h++) {
            fails += !rel_case(p / 4u, MOUTS[m], h, RL_MIDI, 1, fails < 3u);
            fails += !rel_case(2u * p + p / 3u, MOUTS[m], h, RL_MIDI, 1, fails < 3u);
        }
    bad += check("ARP release, the external clock stopped: its first step's notes end at the gate or the release, none "
                 "hang", !fails);

    /* the same pitch a key holds and the arp plays: E let go while the arp sounds E (step 2: E + C, past its first half:
     * NOW strikes a young step again with the chord left) ends that E there and then; C and G held, the arp plays on
     * without E; the gate's end of the cut E sends nothing more (one note-off) */
    for (m = 0; m < 3u; m++) {
        uint32_t k, e_on = ~0u, e_ok;
        t = setup(0, AD_UP, 3, 75, 16);
        t->p[P_MOUT] = MOUTS[m];
        events_block(CTL);
        cap_reset();
        for (k = 0; k < 3u; k++)
            mi(0x90, CEG[k], 100);
        while (nblk * CTL < p + 3u * p / 5u)         /* step 2 (E + C) sounding */
            blk1();
        ok = track_gates(t, 64) == (MOUTS[m] != MOUT_EXT) && t->arp_n == 2u;
        mi(0x80, 64, 0);
        blk1();
        ok &= !track_gates(t, 64) && !((mout_held[0][64 >> 3] >> (64 & 7)) & 1u);
        ok &= MOUTS[m] == MOUT_EXT || track_gates(t, 60) == 1u;   /* (C plays on to its gate) */
        for (k = 0; k < ncap; k++)
            if (cap[k].pitch == 64)
                e_on = k;
        e_ok = MOUTS[m] == MOUT_INT || (e_on != ~0u && cap[e_on].off == (nblk - 1u) * CTL);
        {
            uint32_t n0 = ncap;
            blks(6u * p / CTL);                      /* six more steps over C G */
            for (k = n0; k < ncap; k++)
                ok &= cap[k].pitch != 64;
            ok &= MOUTS[m] == MOUT_INT || ncap > n0;
        }
        mi(0x80, 60, 0);
        mi(0x80, 67, 0);
        blk1();
        ok &= rel_quiet(t);
        blks(FS / CTL);
        ok &= rel_quiet(t) && e_ok;
        if (!ok)
            printf("    same pitch, MOUT %u: E gates %u, arp_n %u, MIDI OUT holding %u, hanging %u, offs for nothing %u\n",
                   (unsigned)MOUTS[m], (unsigned)track_gates(t, 64), (unsigned)t->arp_n, (unsigned)mout_holding(),
                   (unsigned)cap_hanging(), (unsigned)cap_open_offs);
        bad += check(MOUTS[m] == MOUT_INT ? "ARP: a key let go while the arp sounds its pitch ends that voice; the rest play "
                                            "on, nothing hangs (MOUT INT)"
                     : MOUTS[m] == MOUT_EXT ? "ARP: .. its MIDI OUT note ends there, one note-off; the rest play on (MOUT EXT)"
                                            : "ARP: .. the voice and the MIDI OUT note together, one note-off (MOUT BOTH)",
                     ok);
    }

    fails = 0;
    /* All Notes Off (CC123) on channel 1 mid-gate, the chord still held (and latched with HOLD): the arp lets go at once,
     * MIDI OUT and the voices; the note-offs after it send nothing more */
    for (m = 0; m < 3u; m++)
        for (h = 0; h < 2u; h++) {
            t = setup(0, AD_UP, 3, 75, 16);
            t->p[P_MOUT] = MOUTS[m];
            t->p[P_AHOLD] = (int16_t)h;
            events_block(CTL);
            cap_reset();
            mi(0x90, 60, 100); mi(0x90, 64, 100); mi(0x90, 67, 100);
            while (nblk * CTL < 2u * p + p / 3u)
                blk1();
            ok = t->arp_n != 0u;
            mi(0xB0, 123, 0);
            blk1();
            ok &= rel_quiet(t) && !t->nheld;
            mi(0x80, 60, 0); mi(0x80, 64, 0); mi(0x80, 67, 0);
            blks(FS / CTL);
            ok &= rel_quiet(t);
            t->p[P_AHOLD] = 0;
            blks(2);
            fails += !ok;
            if (!ok)
                printf("    CC123, MOUT %u, HOLD %u: arp_n %u, gates %u, MIDI OUT holding %u, offs for nothing %u\n",
                       (unsigned)MOUTS[m], (unsigned)h, (unsigned)t->arp_n, (unsigned)track_gates(t, 128),
                       (unsigned)mout_holding(), (unsigned)cap_open_offs);
        }
    bad += check("ARP: All Notes Off (CC123) mid-gate, the chord held or latched: the arp's notes end at once (MIDI OUT, "
                 "voices), MOUT INT / EXT / BOTH", !fails);

    /* the external clock running, stopped (0xFC) mid-gate with the chord held, then let go; with HOLD too */
    for (h = 0; h < 2u; h++) {
        uint32_t i, at = 10u;
        t = setup(0, AD_UP, 5, 75, 64);
        t->p[P_MOUT] = MOUT_BOTH;
        t->p[P_AHOLD] = (int16_t)h;
        song.g[G_CLOCK] = 1;
        events_block(CTL);
        cap_reset();
        fm1_ms = at;
        mi(0x90, 60, 100); mi(0x90, 64, 100); mi(0x90, 67, 100);
        mi(0xFA, 0, 0);
        blk1();
        for (i = 0; i < 6u * 9u + 2u; i++) {         /* 9 1/16 steps and a bit: the stop lands inside a gate */
            at += 21u;
            mi(0xF8, 0, 0);
            while (fm1_ms < at) {
                fm1_ms++;
                blk1();
            }
        }
        ok = t->arp_n != 0u;
        mi(0xFC, 0, 0);
        blk1();
        ok &= rel_quiet(t);
        mi(0x80, 60, 0); mi(0x80, 64, 0); mi(0x80, 67, 0);
        blks(4);
        t->p[P_AHOLD] = 0;
        blks(FS / CTL);
        ok &= rel_quiet(t) && !t->nheld;
        song.g[G_CLOCK] = 0;
        bad += check(h ? "ARP, external clock: STOP mid-gate with HOLD on: the notes end; let go, HOLD off: nothing hangs"
                       : "ARP, external clock: STOP mid-gate with the chord held: the notes end; let go: nothing hangs", ok);
    }
}

/* ------------------------------------------------------------ KEY ATTACH --- */
/* KEY ATTACH (P_AATT, SHAYPE's windows/shaype/shaype_engine.c SH_ATTACH_*): UP 4 (a row a step) over C E G, DIR UP,
 * WRAP, 1/16, GATE 90 %, the transport stopped (the arp from the chord's first block). D F A B (none of C E G's pitch
 * classes, another size: the pass's offset moves with it) comes in at pass 3 step 1 (tick 13), early in the step
 * (young: 10 %) or late (60 %); every key is let go at tick 21 (pass 5 step 1). Each step's pitch is the core's for
 * the chord the mode gives that step (att_want); every note-on one note-off, nothing left sounding or held */
static const uint8_t DFAB[4] = {62, 65, 69, 71};
enum { AV_MIDI, AV_KEYS };
static uint32_t att_want(const uint8_t *c, uint32_t n, uint32_t tick, uint8_t *out)
{
    uint8_t held[16], len[ARP_ROWS];
    uint32_t vn = ash_voicing(c, n, -1, held);
    return ash_events(&arp_pat[0], tick % 4u, held, vn, ash_offset(&arp_pat[0], 4, tick / 4u, vn, AD_UP, rng), AB_WRAP,
                      out, len);
}
static void att_hold(track_t *t, const uint8_t *c, uint32_t n, uint32_t via, int on)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (via == AV_KEYS)
            fm1_in.notes = on ? fm1_in.notes | 1u << key_of(t, c[i]) : fm1_in.notes & ~(1u << key_of(t, c[i]));
        else
            mi(on ? 0x90 : 0x80, c[i], on ? 100 : 0);
}
static int by_on(const void *a, const void *b)
{
    const cap_t *x = a, *y = b;
    return x->on != y->on ? (x->on < y->on ? -1 : 1) : (int)x->pitch - (int)y->pitch;
}
static int att_case(uint32_t mode, uint32_t via, uint32_t late, int verbose)
{
    static const char *const MN[3] = {"NOW", "NOTE", "CYCLE"};
    track_t *t;
    uint32_t p, i, k, chg, lift, ok = 1, nw = 0, w[64][2], tick, pend = 0, last = 0;
    uint8_t pitch[ARP_ROWS];
    const char *why = "";
    setup_attach = mode;
    t = setup(factory("UP 4"), AD_UP, 5, 90, 16);
    setup_attach = AA_NOW;
    t->p[P_AORDER] = AB_WRAP;
    p = arp_period(t);
    chg = 13u * p + (late ? 3u * p / 5u : p / 10u);
    lift = 21u * p + p / 10u;
    for (i = 0; i < 3u; i++)
        if (via == AV_KEYS && key_of(t, CEG[i]) == ~0u)
            return 0;
    for (i = 0; i < 4u; i++)
        if (via == AV_KEYS && key_of(t, DFAB[i]) == ~0u)
            return 0;
    att_hold(t, CEG, 3, via, 1);
    while (nblk * CTL + CTL <= chg)
        blk1();
    att_hold(t, CEG, 3, via, 0);
    att_hold(t, DFAB, 4, via, 1);
    chg = nblk * CTL;                                /* (the block the change lands in) */
    while (nblk * CTL + CTL <= lift)
        blk1();
    att_hold(t, DFAB, 4, via, 0);
    lift = nblk * CTL;
    blks(FS / CTL);
    /* what each step plays: C E G to tick 13; then D F A B (NOW: at once, the young step struck again; NOTE: from tick
     * 14; CYCLE: from tick 16, the pass's top); let go at 21: NOW and NOTE stop, CYCLE plays its pass out (22, 23) */
    for (tick = 0; tick < 24u; tick++) {
        uint32_t newc = tick > 13u ? mode != AA_CYCLE || tick >= 16u : 0u, j, m;
        if (tick > 21u && mode != AA_CYCLE)
            break;
        m = newc ? att_want(DFAB, 4, tick, pitch) : att_want(CEG, 3, tick, pitch);
        for (j = 0; j < m; j++, nw++)
            w[nw][0] = tick, w[nw][1] = pitch[j];
        if (tick == 13u && mode == AA_NOW && !late)  /* (struck again with D F A B) */
            for (m = att_want(DFAB, 4, tick, pitch), j = 0; j < m; j++, nw++)
                w[nw][0] = tick, w[nw][1] = pitch[j];
    }
    qsort(cap, ncap, sizeof cap[0], by_on);
    if (ncap != nw)
        ok = 0, why = "a different number of notes";
    for (k = 0; ok && k < ncap; k++) {
        tick = (cap[k].on + CTL) / p;
        if (tick != w[k][0] || cap[k].pitch != w[k][1])
            ok = 0, why = "a step played another pitch";
        if (tick == 13u && cap[k].on < chg) {        /* the step the chord changed in: its first note */
            uint32_t cut = mode == AA_NOW && (!late || cap[k].pitch % 12u == 0u || cap[k].pitch % 12u == 4u ||
                                              cap[k].pitch % 12u == 7u);   /* (NOW, late: a key let go ends its pitch) */
            if (cut ? cap[k].off != chg : cap[k].off <= chg)
                ok = 0, why = cut ? "NOW: the struck note did not end at the change" : "the sounding note did not play on";
        }
        if (cap[k].on >= chg && cap[k].on < 14u * p - CTL && !(mode == AA_NOW && !late))
            ok = 0, why = "a note struck between the change and the next step";
        if (cap[k].on <= lift)
            pend = k;
        last = cap[k].on > last ? cap[k].on : last;
    }
    if (ok && ncap) {                                /* the step sounding when every key was let go */
        uint32_t off = cap[pend].off;
        if (mode == AA_NOW ? off != lift : off <= lift)
            ok = 0, why = mode == AA_NOW ? "NOW: let go, the note did not end at once" : "let go, the last note did not ring";
    }
    if (ok && (cap_hanging() || cap_open_offs || mout_holding() || t->arp_n || track_gates(t, 128)))
        ok = 0, why = "a note left hanging, or a note-off for nothing";
    if (!ok && verbose)
        printf("    ATTACH %s, %s, change %s: %s (%u notes, want %u; hanging %u, offs for nothing %u, arp_n %u)\n", MN[mode],
               via == AV_KEYS ? "keys" : "MIDI", late ? "late" : "young", why, (unsigned)ncap, (unsigned)nw,
               (unsigned)cap_hanging(), (unsigned)cap_open_offs, (unsigned)t->arp_n);
    fm1_in.notes = 0;
    return ok;
}
/* the sequencer's chords, CYCLE: C E G on steps 0..12, D F A B 13..21, then rests, the transport playing (arp and
 * sequencer both 1/16): the pass playing keeps C E G to tick 15, D F A B from 16; the chord over at 22, mid-pass, the
 * pass plays out (22, 23) and the arp stops, every note one note-off */
static int att_seq(int verbose)
{
    track_t *t;
    uint32_t p, k, tick, nw = 0, w[64][2], ok = 1, t0;
    uint8_t pitch[ARP_ROWS];
    setup_attach = AA_CYCLE;
    t = setup(factory("UP 4"), AD_UP, 5, 90, 32);
    setup_attach = AA_NOW;
    t->p[P_AORDER] = AB_WRAP;
    chord_steps(t, CEG, 3, 0, 13, 100);
    chord_steps(t, DFAB, 4, 13, 22, 100);
    p = arp_period(t);
    transport_req = 1;
    while (nblk * CTL < 30u * p)
        blk1();
    transport_req = 2;
    blks(FS / CTL);
    for (tick = 0; tick < 24u; tick++) {
        uint32_t j, m = tick >= 16u ? att_want(DFAB, 4, tick, pitch) : att_want(CEG, 3, tick, pitch);
        for (j = 0; j < m; j++, nw++)
            w[nw][0] = tick, w[nw][1] = pitch[j];
    }
    qsort(cap, ncap, sizeof cap[0], by_on);
    ok = ncap == nw;
    t0 = ncap ? cap[0].on : 0u;
    for (k = 0; ok && k < ncap; k++)
        ok = (cap[k].on - t0 + CTL) / p == w[k][0] && cap[k].pitch == w[k][1];
    ok &= !cap_hanging() && !cap_open_offs && !mout_holding() && !t->arp_n;
    if (!ok && verbose)
        printf("    ATTACH CYCLE, the sequencer's chords: %u notes, want %u (hanging %u, offs for nothing %u)\n",
               (unsigned)ncap, (unsigned)nw, (unsigned)cap_hanging(), (unsigned)cap_open_offs);
    return ok;
}
static void attach(void)
{
    uint32_t m, v, l, fails = 0, cases = 0;
    for (m = AA_NOW; m <= AA_CYCLE; m++)
        for (v = 0; v < 2u; v++)
            for (l = 0; l < 2u; l++) {
                fails += !att_case(m, v, l, fails < 4u);
                cases++;
            }
    bad += check("KEY ATTACH NOW / NOTE / CYCLE, MIDI and the keys: D F A B for C E G mid-pass, young and late in the step, "
                 "then every key let go: each step's notes as the mode gives them, every note-on one note-off", !fails);
    printf("    (%u KEY ATTACH cases)\n", (unsigned)cases);
    bad += check("KEY ATTACH CYCLE, the sequencer's chords: the pass keeps its chord, the next takes the new one; the chord "
                 "over mid-pass, the pass plays out, nothing hangs", att_seq(1));
    {   /* the default: a new track, and an older project's 127 (projects: tests/project_test.c), are NOTE */
        track_t *t = setup(0, AD_UP, 3, 75, 16);
        ui_power_on();
        bad += check("KEY ATTACH: NOTE by default (ATTACH on ARP 3, knob 4)", TP[P_AATT].def == AA_NOTE &&
                     trk[0].p[P_AATT] == AA_NOTE && param_fit(&TP[P_AATT], 127) == AA_NOTE && t == &trk[0]);
    }
}

/* the soak, releases only: 40 s of chords (1..4 notes, MIDI channel 1 and the keys) held for random times, so they are
 * let go anywhere in a step and its gate, RATE / GATE / PAT / DIR, HOLD, MOUT, MONO / LEGATO / POLY and the transport
 * moved now and then. After every block in which the chord is gone (HOLD off): nothing the arp played sounds on
 * track 1 (no gate, MIDI OUT holding nothing, arp_snd empty) */
static void soak_releases(void)
{
    static const uint8_t VM[3] = {V_POLY, V_MONO, V_LEGATO};
    track_t *t = setup(0, AD_UP, 3, 75, 16);
    uint8_t held[4];
    uint32_t nh = 0, k, i, viol = 0, rels = 0, until = 0, keys = 0;
    for (k = 0; k < 40u * FS / CTL; k++) {
        uint32_t r = rnd(1000);
        if (!nh && !keys && k >= until) {            /* a new chord, held for 1..1200 ms */
            uint32_t n = 1u + rnd(4);
            if (rnd(4) == 0u) {
                for (i = 0; i < n; i++)
                    keys |= 1u << rnd(16);
                fm1_in.notes = keys;
            } else
                for (i = 0; i < n; i++) {
                    uint32_t note = 48u + rnd(30), j;
                    for (j = 0; j < nh && held[j] != note; j++)
                        ;
                    if (j == nh) {
                        held[nh++] = (uint8_t)note;
                        mi(0x90, note, 1u + rnd(127));
                    }
                }
            until = k + 1u + rnd(1200u * FS / 1000u / CTL);
        } else if ((nh || keys) && k >= until) {    /* let go: all at once, or one by one over a few blocks */
            if (keys) {
                keys = 0;
                fm1_in.notes = 0;
            } else if (rnd(2)) {
                while (nh)
                    mi(0x80, held[--nh], 0);
            } else
                mi(0x80, held[--nh], 0);
            rels++;
            until = nh ? k + rnd(3) : k + rnd(400u * FS / 1000u / CTL);
        }
        if (r == 0u)
            t->p[P_ARATE] = (int16_t)rnd(8);
        else if (r == 1u)
            t->p[P_AGATE] = (int16_t)(10 + rnd(91));
        else if (r == 2u) {
            ash_load(&arp_pat[0], rnd(ARP_NFACTORY));
            t->p[P_APROB] = (int16_t)(1 + rnd(8));
            t->p[P_AOCT] = (int16_t)rnd(4);
        } else if (r == 3u && !rnd(4))
            t->p[P_AHOLD] ^= 1;
        else if (r == 4u)
            t->p[P_MOUT] = (int16_t)rnd(3);
        else if (r == 5u && !rnd(4))
            t->p[P_VOICE] = VM[rnd(3)];
        else if (r == 6u && !rnd(4))
            transport_req = song.playing ? 2u : 1u;
        fm1_ms = k * CTL * 1000u / FS;
        blk1();
        if (!t->p[P_AHOLD] && !t->nheld && !(t->seq_arp && t->seq_n)) {
            uint32_t g = track_gates(t, 128);
            if (t->arp_n || g || mout_holding()) {
                if (viol < 3u)
                    printf("    soak block %u: the chord gone, arp_n %u, gates %u, MIDI OUT holding %u (MOUT %d, VOICE %d)\n",
                           (unsigned)k, (unsigned)t->arp_n, (unsigned)g, (unsigned)mout_holding(), t->p[P_MOUT],
                           t->p[P_VOICE]);
                viol++;
            }
        }
        if (ncap > CAP_N - 64u) {
            uint32_t j = 0;
            for (i = 0; i < ncap; i++)
                if (cap[i].off == ~0u)
                    cap[j++] = cap[i];
            ncap = j;
        }
    }
    fm1_in.notes = 0;
    while (nh)
        mi(0x80, held[--nh], 0);
    t->p[P_AHOLD] = 0;
    transport_req = 2;
    blks(FS / CTL);
    viol += !rel_quiet(t) || t->nheld;
    bad += check("soak, releases: 40 s of chords let go anywhere in a step (MIDI, keys, HOLD, MOUT, RATE, GATE, PAT, VOICE, "
                 "transport): once the chord is gone nothing the arp played sounds or hangs", !viol && rels > 50u);
}

/* ------------------------------------------------------------------- soak --- */
static uint32_t rs = 0x2545F491u;
static uint32_t rnd(uint32_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs % n;
}
/* about 40 s of random play: MIDI notes on channels 1..4, 16 and 9 (bursts too), keys, the selected track, ARP / HOLD /
 * DIR / PAT switched, PLAY / STOP, ROUT CH1-4 / SEL, CC123, every track sequencing chords into its arp. Then every
 * note let go, HOLD and ARP off, the transport stopped: no gate on, MIDI OUT holding nothing, every note-on ended */
static void soak(void)
{
    static const uint8_t CHS[6] = {0, 1, 2, 3, 15, 8};
    static uint8_t on[6][128];
    uint32_t k, i, c, ev = 0, keys = 0;
    int ok;
    track_t *t;
    ui_power_on();
    memset(on, 0, sizeof on);
    cap_reset();
    for (k = 0; k < NTRK; k++) {
        t = &trk[k];
        t->p[P_MOUT] = k == 0u ? MOUT_BOTH : MOUT_INT;
        t->p[P_SUS] = 127;
        t->p[P_SLEN] = 16;
        t->p[P_AMODE] = (int16_t)(k & 1u);
        for (i = 0; i < 16u; i += 4u)
            chord_steps(t, (const uint8_t[]){(uint8_t)(48 + rnd(24)), (uint8_t)(60 + rnd(12)), (uint8_t)(64 + rnd(12))}, 3,
                        i, i + 1u + rnd(4), 60 + rnd(60));
    }
    for (k = 0; k < 40u * FS / CTL; k++) {
        uint32_t r = rnd(64);
        if (r < 10u) {                               /* MIDI */
            uint32_t ci = rnd(6), note = 36u + rnd(48);
            if (on[ci][note]) {
                mi(rnd(2) ? 0x80u | CHS[ci] : 0x90u | CHS[ci], note, 0);
                on[ci][note] = 0;
            } else {
                mi(0x90u | CHS[ci], note, 1u + rnd(127));
                on[ci][note] = 1;
            }
            ev++;
        } else if (r < 13u) {
            keys ^= 1u << rnd(27);
            fm1_in.notes = keys;
        } else if (r == 13u && !rnd(8)) {
            song.sel = (uint8_t)rnd(NTRK);
        } else if (r == 14u && !rnd(16)) {
            trk[rnd(NTRK)].p[P_AMODE] ^= 1;
        } else if (r == 15u && !rnd(16)) {
            trk[rnd(NTRK)].p[P_AHOLD] ^= 1;
        } else if (r == 16u && !rnd(32)) {
            transport_req = song.playing ? 2u : 1u;
        } else if (r == 17u && !rnd(64)) {
            song.g[G_ROUTE] ^= 1;
        } else if (r == 18u && !rnd(64)) {
            mi(0xB0u | CHS[rnd(6)], 123, 0);         /* All Notes Off (the notes stay "held" here: their offs come later) */
        } else if (r == 19u && !rnd(16)) {
            t = &trk[rnd(NTRK)];
            t->p[P_AOCT] = (int16_t)rnd(4);
            ash_load(&arp_pat[trk_index(t)], rnd(ARP_NFACTORY));
            t->p[P_APROB] = (int16_t)(1 + rnd(8));
        } else if (r == 20u && !rnd(8)) {
            for (i = 0; i < 12u; i++) {              /* a burst: a chord's worth at once */
                uint32_t ci = rnd(6), note = 36u + rnd(48);
                mi(on[ci][note] ? 0x80u | CHS[ci] : 0x90u | CHS[ci], note, on[ci][note] ? 0 : 90);
                on[ci][note] ^= 1;
            }
        }
        fm1_ms = k * CTL * 1000u / FS;
        blk1();
        if (ncap > CAP_N - 64u) {                    /* (keep the capture short: ended notes out) */
            uint32_t j = 0;
            for (i = 0; i < ncap; i++)
                if (cap[i].off == ~0u)
                    cap[j++] = cap[i];
            ncap = j;
        }
    }
    fm1_in.notes = 0;
    for (c = 0; c < 6u; c++)
        for (k = 0; k < 128u; k++)
            if (on[c][k]) {
                mi(0x80u | CHS[c], k, 0);
                on[c][k] = 0;
                blk1();
            }
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_AHOLD] = 0;
    blks(2);
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_AMODE] = 0;
    transport_req = 2;
    blks(FS / CTL);                                  /* (a second: every release over) */
    ok = !gates() && !mout_holding() && !cap_hanging() && !cap_open_offs && !midi_in_overflow;
    for (k = 0; k < NTRK; k++)
        ok &= !trk[k].nheld && !trk[k].arp_n && !trk[k].seq_n && !midi_owners[k];
    if (!ok)
        printf("    gates %u, MIDI OUT holding %u, hanging %u, offs for nothing %u\n", (unsigned)gates(),
               (unsigned)mout_holding(), (unsigned)cap_hanging(), (unsigned)cap_open_offs);
    bad += check("soak: 40 s of MIDI / keys / ARP / HOLD / transport / CC123 with the sequence feeding the arps: nothing hangs",
                 ok && ev > 1000u);
}

/* RATE changes join the next absolute transport slot, preserving the pattern's
 * step/pass. Expected boundaries use sample time, independently of arp_pos. */
static void rate_grid(void)
{
    uint32_t from, to, swing, ok = 1, cases = 0;
    for (from = 0; from < 8u; from++)
        for (to = 0; to < 8u; to++)
            for (swing = 0; swing <= 100u; swing += 50u) {
                track_t *t = setup(factory("UP 4"), AD_UP, from, 75, 16);
                uint32_t oldp = arp_period(t), p, first, pair, phase, odd, at, idx, fired = 0;
                uint64_t next;
                t->p[P_ASWING] = (int16_t)swing;
                mi(0x90, 60, 100);
                transport_req = 1;
                blk1();
                blks((7u * oldp + oldp / 3u) / CTL);
                idx = t->arp_idx;
                at = nblk * CTL;
                t->p[P_ARATE] = (int16_t)to;
                p = (uint32_t)FS * 60u / 120u * 2u / ARP_RATE2[to];
                first = p - swing * p / 250u;
                pair = 2u * p;
                phase = at % pair;
                if (from == to) { /* unchanged RATE must retain normal playback too */
                    odd = phase < first ? 1u : 0u;
                    next = at + (phase < first ? first - phase : pair - phase);
                } else if (!phase || phase == first) {
                    odd = phase == first;
                    next = at;
                } else {
                    odd = phase < first ? 1u : 0u;
                    next = at + (phase < first ? first - phase : pair - phase);
                }
                while (fired < 4u && ok) {
                    uint32_t due = (uint32_t)((next + CTL - 1u) / CTL * CTL), before = t->arp_idx;
                    at = nblk * CTL;
                    blk1();
                    if (at >= due) {
                        idx++;
                        fired++;
                        next += odd ? pair - first : first;
                        odd ^= 1u;
                    }
                    if (t->arp_idx != idx) {
                        printf("    rate %u -> %u swing %u at %u due %u: idx %u -> %u want %u\n",
                            from, to, swing, at, due, before, t->arp_idx, idx);
                        ok = 0;
                    }
                }
                mi(0x80, 60, 0); transport_req = 2; blks(2);
                ok &= !cap_hanging() && !cap_open_offs && !mout_holding();
                cases++;
            }
    bad += check("SHAYPE RATE: all 64 transitions, straight / swung transport slots, pattern/pass continues, no hanging notes", ok && cases == 192u);
}

/* A second track running the destination rate from PLAY is the external clock
 * reference. Changing track 1 must join its next slot without copying its index. */
static void rate_external(uint32_t wander)
{
    track_t *t = setup(factory("UP 4"), AD_UP, 3, 75, 64), *ref = &trk[1];
    uint32_t i, at = 10u, changed = 0, joined = 0, ok = 1, swaps = 0;
    ref->p[P_ARATE] = 5; ref->p[P_ASWING] = 0;
    song.g[G_CLOCK] = 1; events_block(CTL);
    fm1_ms = at; mi(0xFA, 0, 0); blk1();
    for (i = 0; i < 192u; i++) {
        at += wander ? 19u + (i / 24u) % 4u : (i % 6u == 5u ? 21u : 21u - (i % 2u));
        mi(0xF8, 0, 0);
        while (fm1_ms < at) {
            uint32_t a = t->arp_idx, b = ref->arp_idx;
            if (i == 49u && !changed) { t->p[P_ARATE] = 5; changed = 1; }
            fm1_ms++; blk1();
            if (changed) {
                if (t->arp_idx != a) {
                    ok &= ref->arp_idx != b && t->arp_idx == a + 1u;
                    joined = 1; swaps++;
                } else if (joined && ref->arp_idx != b) ok = 0;
            }
        }
    }
    mi(0xFC, 0, 0); blk1();
    if (!ok) printf("    external rate join wander %u, joined %u, slots %u\n",wander,joined,swaps);
    bad += check(wander ? "SHAYPE RATE: joins external grid while tempo wanders; pattern index never restarts"
                       : "SHAYPE RATE: joins external grid; pattern index never restarts", ok && joined && swaps > 10u);
}

static void rate_waiting_clock(void)
{
    track_t *t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    uint32_t i, ok = 1, idx;
    song.g[G_CLOCK] = 1; events_block(CTL);
    fm1_ms = 10; mi(0xFA, 0, 0); blk1();
    idx = t->arp_idx;
    /* No clock time has passed: RATE movement must not replay slot zero. */
    for (i = 0; i < 8u; i++) {
        t->p[P_ARATE] = (int16_t)i;
        blk1(); ok &= t->arp_idx == idx;
    }
    mi(0xFC, 0, 0); blk1();
    mi(0xFB, 0, 0); blk1(); ok &= t->arp_idx == idx;
    mi(0xFA, 0, 0); blk1(); ok &= t->arp_idx == 0u;
    mi(0xFC, 0, 0); blk1();
    bad += check("SHAYPE RATE: awaiting MIDI clock does not retrigger; Continue keeps position, Start resets", ok);
}

int main(void)
{
    rate_waiting_clock();
    rate_grid();
    rate_external(0);
    rate_external(1);
    parity(AA_NOTE);
    parity(AA_NOW);
    edges();
    ext_clock(0);
    ext_clock(1);
    recording();
    step_rec();
    releases();
    attach();
    soak_releases();
    soak();
    if (bad) {
        printf("ARP SEQ TEST FAILED (%d)\n", bad);
        return 1;
    }
    puts("SEQ > ARP, MIDI recording and the soak: ok");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca 1.2: the SEQ TOOLS layer's actions (ui_layer.c: SEQ held, on every page). Included by ui_layer.c. The white
 * keys from F3 (TL_*):
 *   F3 CLEAR  G3 REVERSE  A3 SHIFT <  B3 SHIFT >       the whole sequence
 *   C4 RANDOM D4 COOK               F4 EUCLID            new / a little changed (EUCLID: 1.5.5, below)
 *   D5 DOUBLE E5 HALF                                    LEN x2 / half (1.5.5, below)
 * (E4 and G4 .. C5 were the retired DRUM grid's BEAT and lane tools: their places stay, nothing there)
 * Each works on the selected track's steps 1..LEN (the steps past LEN stay) and is one undo: SAVE held swaps back
 * the copy taken just before it (ui.c undo, UNDO_PAT; unlike a load the track's automation stays), so after a few
 * COOKs SAVE held takes back the last one only; the layer's OCT- puts back the track as the layer opened (tl_open).
 * A step that held something and is empty after loses its locks (as EDIT's step clear); REVERSE and SHIFT move the
 * steps' automation and locks with them. RANDOM and COOK draw from libc.c rng(): no seed is kept, each press
 * differs. A sequence of ties reversed keeps its note first: N T T -> N T T at the mirrored place */
enum { TL_CLEAR, TL_REV, TL_LEFT, TL_RIGHT, TL_RANDOM, TL_COOK, TL_BEAT, TL_EUCLID, TL_LCLR, TL_LREV, TL_LFILL, TL_LRND,
       TL_DOUBLE, TL_HALF, TL_N };

/* the track as the layer opened (OCT-): in the pool (RAM's .bss is tight), taken by layer_opened */
static struct { step_t step[NSTEP]; motion_store_t mo; } tl_open __attribute__((section(".pool")));

static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }
static uint32_t tl_rnd(uint32_t n) { return n ? rng() % n : 0u; }
static int tl_gate(const step_t *s) { return s->time == ST_NOTE && s->n; }
/* place p's action is there (not the retired DRUM places) */
static int tl_cell(const track_t *t, uint32_t p)
{
    (void)t;
    return p < TL_N && (p < TL_BEAT || p == TL_EUCLID || p >= TL_DOUBLE);
}

/* the undo copy of t as it is now (ui.c load_begin, but the automation is copied and stays): one action */
static void tl_undo_take(track_t *t)
{
    uint32_t i = trk_index(t);
    undo.trk = (uint8_t)(i + 1u);
    undo.what = UNDO_PAT;
    undo.keep = 0;                                      /* (the next load or action takes its own copy) */
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    undo.pat = pat_sig[i];
    undo.from = pat_from[i];
    undo.from_h = pat_from_h[i];
    undo.patn = pat_last[i];
    motion_snapshot_track(t, &undo.motion_backup);
}

/* steps 1..len of t that are not empty (a REST); after the action the ones emptied lose their locks */
static uint64_t tl_used_of(const step_t *s, uint32_t len)
{
    uint64_t m = 0;
    uint32_t i;
    for (i = 0; i < len; i++)
        if (s[i].time != ST_REST)
            m |= (uint64_t)1 << i;
    return m;
}
static uint64_t tl_used(const track_t *t, uint32_t len) { return tl_used_of(t->step, len); }
static void tl_unlock(track_t *t, uint64_t was, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (((was >> i) & 1u) && t->step[i].time == ST_REST)
            (void)motion_clear_locks(t, i);
}

/* step j of t becomes the undo copy's step src[j] (taken just before), its automation and locks with it */
static void tl_order(track_t *t, uint32_t len, const uint8_t *src)
{
    uint8_t to[NSTEP];
    uint32_t j, k = trk_index(t), f;
    for (j = 0; j < len; j++) {
        t->step[j] = undo.step[src[j]];
        to[src[j]] = (uint8_t)j;
    }
    f = motion_guard();
    for (j = 0; j < motion.count; j++) {
        motion_event_t *e = &motion.event[j];
        if ((e->place >> 6) == k && (e->place & 63u) < len)
            e->place = (uint8_t)(k << 6 | to[e->place & 63u]);
    }
    motion_unguard(f);
}

/* REVERSE: the mirror order; a note's ties (after it) would come before it, so the note goes first again */
static void tl_reverse(track_t *t, uint32_t len)
{
    uint8_t src[NSTEP], h;
    uint32_t j, e, x;
    for (j = 0; j < len; j++)
        src[j] = (uint8_t)(len - 1u - j);
    for (j = 0; j < len; j++) {
        for (e = j; e < len && undo.step[src[e]].time == ST_TIE; e++)
            ;
        if (e == j)
            continue;
        if (e < len && undo.step[src[e]].time == ST_NOTE) {   /* ties j .. e - 1, their note at e: the note to j */
            for (h = src[e], x = e; x > j; x--)
                src[x] = src[x - 1u];
            src[j] = h;
        }
        j = e;                                          /* (past the run and its note) */
    }
    tl_order(t, len, src);
}

/* SHIFT: rotated by one step inside LEN, d = 1 later (>), len - 1 earlier (<) */
static void tl_shift(track_t *t, uint32_t len, uint32_t d)
{
    uint8_t src[NSTEP];
    uint32_t j;
    for (j = 0; j < len; j++)
        src[j] = (uint8_t)((j + len - d) % len);
    tl_order(t, len, src);
}

/* ----------------------------------------------------------- melodic --- */
static int32_t tl_in_scale(const track_t *t, int32_t n) { return (int32_t)((scale_mask(t) >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u); }
/* the next note of the scale above (d 1) or below (d -1) n */
static int32_t tl_neigh(const track_t *t, int32_t n, int32_t d)
{
    uint32_t g = 12;
    do
        n += d;
    while (g-- && !tl_in_scale(t, n));
    return clamp(n, 0, 127);
}
/* RANDOM on a melodic track: the rhythm stays (the NOTE steps, their ties, accents and chance), the notes are new: a
 * walk through the track's scale (ROOT, SCL) 9 semitones either side of the pattern's middle (an empty pattern:
 * around the root near C4, on an 8th-note rhythm with some rests). A chord keeps its shape, each note snapped
 * down into the scale */
static void tl_random_notes(track_t *t, uint32_t len)
{
    uint8_t deg[20];
    int32_t c = 0, lo, d, x;
    uint32_t i, k, g = 0, nd = 0, at;
    for (i = 0; i < len; i++)
        if (tl_gate(&t->step[i])) {
            c += t->step[i].note[0];
            g++;
        }
    if (g) {
        c /= (int32_t)g;
    } else {                                            /* no rhythm: 8ths, a few rests, always step 1 */
        c = 60 + t->p[P_ROOT] % 12;
        c -= c > 66 ? 12 : 0;
        for (i = 0; i < len; i++) {
            step_clear(&t->step[i]);
            if (i % 2u == 0u && (!i || tl_rnd(4))) {
                t->step[i].time = ST_NOTE;
                t->step[i].n = 1;
                t->step[i].vel = 96;
            }
        }
    }
    for (lo = clamp(c - 9, 12, 100), x = lo; x <= lo + 18 && nd < sizeof deg; x++)
        if (tl_in_scale(t, x))
            deg[nd++] = (uint8_t)x;
    if (!nd)
        return;
    at = nd / 2u + tl_rnd(5);
    at = at >= 2u ? at - 2u : 0u;
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        if (!tl_gate(s))
            continue;
        at = at >= nd ? nd - 1u : at;
        d = (int32_t)deg[at] - s->note[0];
        s->note[0] = deg[at];
        for (k = 1; k < s->n && k < 4u; k++) {
            x = clamp(s->note[k] + d, 1, 127);
            while (!tl_in_scale(t, x) && x > 1)
                x--;
            s->note[k] = (uint8_t)x;
        }
        if (!tl_rnd(6)) {                               /* a leap now and then, else a step or two */
            at = tl_rnd(nd);
        } else {
            x = (int32_t)at + (int32_t)tl_rnd(5) - 2;
            at = (uint32_t)(x < 0 ? -x : x >= (int32_t)nd ? 2 * ((int32_t)nd - 1) - x : x);
        }
    }
}

/* COOK on a melodic track: 15..25 % of the NOTE steps (at least one; more when they undid each other): a note to the
 * next scale note up or down (mostly) or an octave, an accent on / off, a chance of 50 / 75 % or back to 100 */
static void tl_cook_notes(track_t *t, uint32_t len, uint32_t g)
{
    uint32_t m = g * (15u + tl_rnd(11)) / 100u, i, k, r, n, sig = steps_sig(t), tries = 64;
    for (m = m ? m : 1u; tries-- && (m || steps_sig(t) == sig); m -= m != 0u) {   /* (undone again: one more) */
        step_t *s;
        n = tl_rnd(g);
        for (i = 0; i < len && (!tl_gate(&t->step[i]) || n--); i++)
            ;
        s = &t->step[i % NSTEP];
        r = tl_rnd(8);
        if (r == 6u) {
            s->flags ^= SF_ACCENT;
        } else if (r == 7u) {
            if (!step_cond(s))                          /* (1.5.5: a condition stays) */
                step_set_chance(s, step_chance(s) < 100u ? 100u : tl_rnd(2) ? 75u : 50u);
        } else {
            int32_t d = tl_rnd(2) ? 1 : -1, o = r == 5u ? d * 12 : 0;
            for (k = 0; k < s->n && k < 4u; k++)       /* (an octave out of 24 .. 108: a scale step instead) */
                if (s->note[k] + o < 24 || s->note[k] + o > 108)
                    o = 0;
            for (k = 0; k < s->n && k < 4u; k++)
                s->note[k] = (uint8_t)(o ? s->note[k] + o : tl_neigh(t, s->note[k], d));
        }
    }
}

/* ------------------------------------------------------------ EUCLID --- */
/* 1.5.5 (#186): F4 EUCLID: HITS spread as evenly as they go over a cycle of STEPS, turned by ROT, repeated through
 * 1..LEN. F4 opens it on the selected track: the undo copy is taken (one undo for all of it) and the pattern is written
 * at once (heard while playing); KNOB 1..3 HITS (0..STEPS) STEPS (1..LEN) ROT (0..STEPS - 1) rewrite it from that copy.
 * OCT+ (or F4 again, another tool, the layer let go) applies it, OCT- cancels it (the steps as they were; the layer's
 * own OCT- after that puts back the track as the layer opened). A hit keeps the step's own note (a NOTE
 * step as it is: its chord, accent, CHANCE, RATCH ..; a TIE: the note it holds), else the root (ROOT, near C4); the
 * other steps are emptied, but a TIE that holds a note on (its note's ties: the note keeps its length); the emptied
 * steps lose their locks when it is applied. The values stay for the next one (until the power goes). The track's steps
 * as it opened are its own copy (eu_from; the undo copy is the next load's, a MIDI Program Change or the editor's on
 * another track meanwhile), and it stays on the track it opened on */
static struct { uint8_t on, hits, steps, rot, trk; } eu;
static step_t eu_from[NSTEP] __attribute__((section(".pool")));   /* the steps as EUCLID opened (the pool: .bss is tight) */
static track_t *eu_track(void) { return &trk[eu.trk % NTRK]; }
static int eu_hit(uint32_t i)                           /* step i (0..) of the cycle repeated, turned by rot */
{
    uint32_t n = eu.steps ? eu.steps : 1u, p = (i + n - eu.rot % n) % n;
    return (p * eu.hits) % n < eu.hits;
}
/* the note a hit on step i plays: the NOTE step there, or the note a TIE there holds (its NOTE before it), else none */
static const step_t *eu_src(uint32_t i)
{
    uint32_t j = i;
    while (j && eu_from[j].time == ST_TIE)
        j--;
    return eu_from[j].time == ST_NOTE && eu_from[j].n && (j == i || eu_from[i].time == ST_TIE) ? &eu_from[j] : 0;
}
static void eu_render(track_t *t)                       /* (t: ignored, EUCLID's own track) */
{
    t = eu_track();
    uint32_t len = tl_len(t), i;
    memcpy(t->step, eu_from, sizeof t->step);
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        const step_t *o = eu_src(i);
        if (eu_hit(i)) {
            if (o != &eu_from[i]) {                   /* (a REST, an empty step, a TIE: a NOTE of its own) */
                int32_t r = 60 + t->p[P_ROOT] % 12;
                step_clear(s);
                s->time = ST_NOTE;
                if (o)
                    memcpy(s->note, o->note, sizeof s->note), s->n = o->n;
                else
                    s->note[0] = (uint8_t)(r > 66 ? r - 12 : r), s->n = 1;
            }
        } else if (!(s->time == ST_TIE && i && (t->step[i - 1u].time == ST_TIE || (t->step[i - 1u].time == ST_NOTE &&
                                                                                    t->step[i - 1u].n)))) {
            step_clear(s);
        }
    }
}
static void eu_fit(const track_t *t)                    /* the values inside LEN */
{
    uint32_t len = tl_len(t);
    if (!eu.steps || eu.steps > len)
        eu.steps = (uint8_t)(len < 16u ? len : 16u);
    if (eu.hits > eu.steps)
        eu.hits = eu.steps;
    eu.rot = (uint8_t)(eu.rot % eu.steps);
}
static void eu_say(void)                               /* "EUCLID 5/16" ("+2": turned) */
{
    char b[24], n[4];
    str_cpy(b, "EUCLID ", sizeof b);
    fmt_int(n, eu.hits);
    str_cpy(b + str_len(b), n, sizeof b - str_len(b));
    str_cpy(b + str_len(b), "/", sizeof b - str_len(b));
    fmt_int(n, eu.steps);
    str_cpy(b + str_len(b), n, sizeof b - str_len(b));
    if (eu.rot) {
        str_cpy(b + str_len(b), " +", sizeof b - str_len(b));
        fmt_int(n, eu.rot);
        str_cpy(b + str_len(b), n, sizeof b - str_len(b));
    }
    ui_message(b);
}
static void eu_open(track_t *t)
{
    tl_undo_take(t);
    memcpy(eu_from, t->step, sizeof eu_from);
    eu.trk = (uint8_t)trk_index(t);
    if (!eu.steps) {                                    /* (the first since the power came: 4 / 16) */
        eu.hits = 4;
        eu.rot = 0;
    }
    eu_fit(t);
    eu.on = 1;
    eu_render(t);
    eu_say();
}
static void eu_knob(track_t *t, uint32_t k, int32_t s)
{
    t = eu_track();
    if (k == 0u)
        eu.hits = (uint8_t)clamp((int32_t)eu.hits + s, 0, eu.steps);
    else if (k == 1u)
        eu.steps = (uint8_t)clamp((int32_t)eu.steps + s, 1, (int32_t)tl_len(t));
    else if (k == 2u)
        eu.rot = (uint8_t)(((int32_t)eu.rot + s % (int32_t)eu.steps + (int32_t)eu.steps) % (int32_t)eu.steps);
    else
        return;
    eu_fit(t);
    eu_render(t);
    eu_say();
}
/* OCT+ (apply: the emptied steps lose their locks) or OCT- (cancel: the steps of the copy back) */
static void eu_close(track_t *t, int apply)
{
    uint32_t len;
    if (!eu.on)
        return;
    t = eu_track();
    len = tl_len(t);
    eu.on = 0;
    if (apply) {
        tl_unlock(t, tl_used_of(eu_from, len), len);
        return;
    }
    memcpy(t->step, eu_from, sizeof t->step);
}

/* ------------------------------------------------------- DOUBLE, HALF --- */
/* 1.5.5 (#85): D5 DOUBLE: steps 1..LEN once more after them (LEN + 1 .. 2 x LEN, at most the 64 steps: from LEN 33 on
 * only the first 64 - LEN steps fit) and LEN x2, the steps whole (notes, ties, hits, accents, CHANCE / conditions, RATCH,
 * NUDGE, VEL) with their automation and locks (a copy each; what was stored past LEN where the copy goes is replaced).
 * A note tied over the loop's end ties into the copy as it tied into step 1. E5 HALF: LEN / 2 (rounded down), the steps
 * of the second half cleared with their automation and locks (a note tied into it ends at the new LEN). Both one undo
 * (SAVE held: the steps, LEN and the automation back). DOUBLE at 64 steps and HALF at 1 do nothing (said why); DOUBLE
 * with too few of the 128 records left for the copies: AUTOMATION FULL, nothing done */
static motion_store_t tl_mo __attribute__((section(".pool")));   /* the track's records as they will be */
/* tl_mo = track t's records with steps lo..hi-1 dropped, those of steps 0..cn-1 copied by `by` steps (cn 0: none);
 * 0 = they do not fit the MOTION_MAX */
static int tl_motion_plan(track_t *t, uint32_t lo, uint32_t hi, uint32_t cn, uint32_t by)
{
    uint32_t k = trk_index(t), i, f, n = 0, ok = 1;
    f = motion_guard();
    memset(&tl_mo, 0, sizeof tl_mo);
    tl_mo.on = motion.on & (uint8_t)(1u << k);
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        uint32_t s = e->place & 63u;
        if ((e->place >> 6) != k || (s >= lo && s < hi))
            continue;
        if (n < MOTION_MAX) tl_mo.event[n] = *e;
        n++;
        if (s < cn) {
            if (n < MOTION_MAX) {
                tl_mo.event[n] = *e;
                tl_mo.event[n].place = (uint8_t)(k << 6 | (s + by));
            }
            n++;
        }
    }
    if (motion.count - motion_count(t) + n > MOTION_MAX)
        ok = 0;
    tl_mo.count = (uint8_t)(n < MOTION_MAX ? n : MOTION_MAX);
    motion_unguard(f);
    return ok;
}
static uint32_t tl_nl(uint32_t len) { return 2u * len < NSTEP ? 2u * len : NSTEP; }   /* DOUBLE's LEN */
/* 0 = it cannot (said why): before the undo copy is taken */
static int tl_resize_ok(track_t *t, uint32_t p, uint32_t len)
{
    if (p == TL_DOUBLE && len >= NSTEP) {
        ui_message("64 STEPS: NO ROOM");
        return 0;
    }
    if (p == TL_HALF && len < 2u) {
        ui_message("1 STEP: NO HALF");
        return 0;
    }
    if (p == TL_DOUBLE && !tl_motion_plan(t, len, tl_nl(len), tl_nl(len) - len, len)) {
        ui_message("AUTOMATION FULL");
        return 0;
    }
    return 1;
}
static uint32_t tl_double(track_t *t, uint32_t len)      /* the new LEN */
{
    uint32_t nl = tl_nl(len), i;
    for (i = len; i < nl; i++)
        t->step[i] = t->step[i - len];
    (void)tl_motion_plan(t, len, nl, nl - len, len);
    (void)motion_replace_track(t, &tl_mo);
    t->p[P_SLEN] = (int16_t)nl;
    return nl;
}
static uint32_t tl_half(track_t *t, uint32_t len)
{
    uint32_t h = len / 2u, i;
    for (i = h; i < len; i++)
        step_clear(&t->step[i]);
    (void)tl_motion_plan(t, h, len, 0, 0);
    (void)motion_replace_track(t, &tl_mo);
    t->p[P_SLEN] = (int16_t)h;
    return h;
}

/* ------------------------------------------------------------ actions --- */
/* the action of place p on track t: its message; 0 = nothing done (said why) */
static const char *tl_do(track_t *t, uint32_t p)
{
    static char b[24];
    uint32_t len = tl_len(t), i, n = 0;
    uint64_t was = tl_used(t, len);
    const char *msg = 0;
    if ((p == TL_DOUBLE || p == TL_HALF) && !tl_resize_ok(t, p, len))
        return 0;
    if (p == TL_COOK) {                                 /* (nothing there: nothing to cook, no undo copy) */
        for (i = 0; i < len; i++)
            n += (uint32_t)tl_gate(&t->step[i]);
        if (!n) {
            ui_message("NOTHING TO COOK");
            return 0;
        }
    }
    tl_undo_take(t);
    switch (p) {
    case TL_CLEAR:
        for (i = 0; i < len; i++)
            step_clear(&t->step[i]);
        msg = "SEQUENCE CLEARED";
        break;
    case TL_REV: tl_reverse(t, len); msg = "REVERSED"; break;
    case TL_LEFT: tl_shift(t, len, len - 1u); msg = "SHIFTED LEFT"; break;
    case TL_RIGHT: tl_shift(t, len, 1u); msg = "SHIFTED RIGHT"; break;
    case TL_RANDOM:
        tl_random_notes(t, len);
        msg = "RANDOM NOTES";
        break;
    case TL_COOK:
        tl_cook_notes(t, len, n);
        msg = "COOKED";
        break;
    case TL_DOUBLE:
    case TL_HALF:
        str_cpy(b, p == TL_DOUBLE ? "DOUBLED: " : "HALVED: ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)(p == TL_DOUBLE ? tl_double(t, len) : tl_half(t, len)));
        str_cpy(b + str_len(b), " STEPS", sizeof b - str_len(b));
        return b;                                       /* (nothing emptied inside the new LEN: no unlock) */
    default:                                            /* (not a place with an action: tl_cell) */
        return 0;
    }
    tl_unlock(t, was, len);
    return msg;
}

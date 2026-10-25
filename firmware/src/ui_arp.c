/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop: the ARP's pages (params.c ARP, ARP 2, ARP 3; GR_ARP), SHAYPE's pattern of the selected track (seq.c
 * arp_pat, arp_shape.c) as a grid (ui_graph.c graph_arp): steps across, rows up, the cursor step framed, the step
 * playing lit, 16 steps at a time (the cursor's 16).
 *   PRESETS     the cursor, round the pattern's STEPS
 *   the keys    F3 G3 A3 B3 C4 D4 E4 F4 (the white keys from the left) are rows 1..8 at the cursor step: a tap writes a
 *               note there, or takes away the note that is there; the black keys between them are silent here, the
 *               keys above play as everywhere (the chord the arp plays)
 *   a row key held + any knob   that row's note at the cursor: its length (a note the press wrote, or one already there)
 *   EDIT        the notes starting on the cursor step go
 *   ARP 3's PAT a factory pattern, loaded as it is turned to (its STEPS and BOUND too); an edit makes it USER
 *   OP1 + OCT+ / OCT-   ZOOM in / out (the toy's x2 / /2, ui_layer.c op_shift: OP1 is the button Switch Blade calls FX)
 * Every edit is the main loop's, with the audio ISR held off while the pattern changes (arp_fire reads it). */
static const uint8_t ARP_ROWKEY[ARP_ROWS] = {0, 2, 4, 6, 7, 9, 11, 12};   /* row r's key (from F: F3 G3 A3 B3 C4 D4 E4 F4) */

static int arp_keys_on(void) { return !ui.home && !ui.menu && !ui.confirm && !ui.layer && cur_page()->graph == GR_ARP; }
static uint32_t arp_cur(void) { return ui.arp.cur % arp_steps(TSEL); }   /* (STEPS may have shrunk under it) */
/* the step the selected track's arp plays now, -1 none (no chord, ARP off) */
static int32_t arp_playing(void)
{
    const track_t *t = TSEL;
    if (!t->p[P_AMODE] || !arp_fed(t) || t->arp_idx == 0xFFFFFFFFu)   /* (keys, MIDI or the sequencer's chord) */
        return -1;
    return (int32_t)(t->arp_idx % arp_steps(t));
}
/* the key LEDs on an ARP page: the rows with a note sounding over the cursor step (their keys), the held ones */
static uint32_t arp_leds(void)
{
    const arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, m = 0, c = arp_cur();
    for (r = 0; r < ARP_ROWS; r++)
        if (ash_find(p, r, c) >= 0 || ((ui.arp.down >> r) & 1u))
            m |= 1u << ARP_ROWKEY[r];
    return m;
}
static void arp_edited(void)                     /* the pattern is the user's now: PAT USER */
{
    TSEL->p[P_APAT] = 0;
    ui.force = 1;
}
static void arp_cursor_move(int32_t s)
{
    int32_t n = (int32_t)arp_steps(TSEL);
    ui.arp.cur = (uint8_t)((((int32_t)arp_cur() + s) % n + n) % n);
}
/* PAT turned (edit_param): factory pattern v - 1 with its STEPS and BOUND; USER (0) leaves the pattern as it is */
static void arp_factory(track_t *t, uint32_t v)
{
    const arp_factory_t *f;
    if (!v || v > ARP_NFACTORY)
        return;
    f = &ARP_FACTORY[v - 1u];
    fm1_irq_off();
    ash_load(&arp_pat[trk_index(t)], v - 1u);
    t->p[P_APROB] = f->steps;
    t->p[P_AORDER] = f->bound;
    fm1_irq_on();
    ui.force = 1;
    ui_say("PATTERN ", f->name);
}
/* STEPS turned (edit_param): the toy's rule for a shorter grid, notes past the end go, notes over it are cut */
static void arp_steps_set(track_t *t)
{
    fm1_irq_off();
    ash_fit(&arp_pat[trk_index(t)], arp_steps(t));
    fm1_irq_on();
}
/* EDIT on an ARP page: the notes starting on the cursor step go */
static void arp_clear_step(void)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t i = 0, c = arp_cur(), n = 0;
    fm1_irq_off();
    while (i < p->n)
        if (ARP_STEP(p->note[i]) == c) {
            ash_drop(p, i);
            n++;
        } else {
            i++;
        }
    fm1_irq_on();
    if (n)
        arp_edited();
    ui_message(n ? "STEP CLEARED" : "NOTHING THERE");
}
/* the row keys (ui_input, song.grid 4): pressed (notes: this pass's new keys) a note is written at the cursor, or the
 * one there is marked to go; let go without a knob turned, it goes */
static void arp_keys(uint32_t notes)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, bit, c = arp_cur();
    int32_t i;
    for (r = 0; r < ARP_ROWS; r++) {
        bit = 1u << r;
        if ((notes >> ARP_ROWKEY[r]) & 1u) {
            ui.arp.down |= (uint8_t)bit;
            ui.arp.moved &= (uint8_t)~bit;
            ui.arp.made &= (uint8_t)~bit;
            if (ash_find(p, r, c) < 0) {
                int ok;
                fm1_irq_off();
                ok = ash_paint(p, r, c, 1);
                fm1_irq_on();
                if (!ok) {
                    ui_message("PATTERN FULL");
                    continue;
                }
                ui.arp.made |= (uint8_t)bit;
                arp_edited();
            }
        } else if ((ui.arp.down & bit) && !((fm1_in.notes >> ARP_ROWKEY[r]) & 1u)) {
            ui.arp.down &= (uint8_t)~bit;
            if (!((ui.arp.made | ui.arp.moved) & bit) && (i = ash_find(p, r, c)) >= 0) {
                fm1_irq_off();
                ash_drop(p, (uint32_t)i);
                fm1_irq_on();
                arp_edited();
            }
            ui.arp.made &= (uint8_t)~bit;
            ui.arp.moved &= (uint8_t)~bit;
        }
    }
}
/* a knob turned with row keys held: their notes at the cursor longer or shorter (1 .. to the end of STEPS). 1: taken */
static int arp_len_turn(int32_t s)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, c = arp_cur(), steps = arp_steps(TSEL), len = 0;
    int32_t i;
    if (!arp_keys_on() || !ui.arp.down)
        return 0;
    for (r = 0; r < ARP_ROWS; r++) {
        uint32_t x, st;
        if (!((ui.arp.down >> r) & 1u) || (i = ash_find(p, r, c)) < 0)
            continue;
        x = p->note[i];
        st = ARP_STEP(x);
        len = (uint32_t)clamp((int32_t)ARP_LEN(x) + s, 1, (int32_t)(steps - st));
        fm1_irq_off();
        ash_paint(p, r, st, len);                    /* (the notes of the row it now reaches go: the toy's paint) */
        fm1_irq_on();
    }
    ui.arp.moved |= ui.arp.down;
    if (len) {
        char b[8];
        fmt_int(b, (int32_t)len);
        ui_say("LENGTH ", b);
        arp_edited();
    }
    return 1;
}
/* ZOOM's refusals (arp_shape.c AZ_*): the screen says why. NORATE: only 1/16T in (no 1/32T) and 1/4T out (no 1/2T) */
static const char *const AZ_MSG[] = {"", "1/32 IS THE FASTEST", "1/2 IS THE SLOWEST", "", "64 STEPS MAX", "STEPS ARE ODD",
                                     "NOTE BETWEEN STEPS"};
/* OP1 + OCT+ (in) / OCT- (out) on an ARP page (ui_layer.c op_shift): the toy's subdivide x2 / /2 (ash_subdivide) on the
 * selected track's pattern. STEPS and RATE double (or halve), every note's step and length with them: it sounds the
 * same, zoomed. The cursor stays on its place in the music (its step x2 or /2); an arp playing goes on from where it is
 * (its pass, the step it is in and how far into it). When it can't, nothing changes and the screen says why. Main loop
 * only, the ISR held off while the pattern, STEPS and RATE change together (a few dozen notes at most) */
static void arp_zoom(int in)
{
    track_t *t = TSEL;
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t was = arp_steps(t), steps = was, rate = (uint32_t)clamp(t->p[P_ARATE], 0, 7), code, s, pass, per;
    char b[24];
    fm1_irq_off();
    code = ash_subdivide(p, &steps, &rate, in);
    if (code == AZ_OK) {
        t->p[P_APROB] = (int16_t)steps;
        t->p[P_ARATE] = (int16_t)rate;
        if (t->arp_idx != 0xFFFFFFFFu && t->arp_pos < 0xFFFFFFFu) {   /* (playing, not a fresh chord's first step) */
            pass = t->arp_idx / was;
            s = t->arp_idx % was;
            per = arp_period(t);                     /* (the new RATE's step) */
            if (in) {
                s *= 2u;
                if (t->arp_pos >= per) {             /* (in the old step's second half: the new step after it) */
                    s++;
                    t->arp_pos -= per;
                }
            } else {
                if (s & 1u)                          /* (the second of a pair: half way into the joined step) */
                    t->arp_pos += per / 2u;
                s /= 2u;
            }
            t->arp_idx = pass * steps + s;
        }
    }
    fm1_irq_on();
    if (code != AZ_OK) {
        ui_message(code == AZ_NORATE ? (in ? "NO 1/32T RATE" : "NO 1/2T RATE") : AZ_MSG[code]);
        return;
    }
    ui.arp.cur = (uint8_t)(in ? (ui.arp.cur % was) * 2u : (ui.arp.cur % was) / 2u);
    fmt_int(b, (int32_t)steps);
    str_cpy(b + str_len(b), " STEPS ", sizeof b - str_len(b));
    str_cpy(b + str_len(b), N_ARATE[rate], sizeof b - str_len(b));
    ui_say("ZOOM ", b);                              /* "ZOOM 32 STEPS 1/32" */
    arp_edited();
}

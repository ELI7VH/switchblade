/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM's STEP page: 16 white keys are steps; the first eight black keys pick
 * a kit lane. A held step edits before it toggles, as the SHAYPE grid does.
 * Storage is the existing FOR4 hit/acc bytes; exact older GM notes stay intact. */
static const uint8_t DG_WHITE[16] = {0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26};
static const uint8_t DG_BLACK[8] = {1,3,5,8,10,13,15,17};
static struct { uint16_t down, edited; uint8_t bank, lane, track; } dg;
static int drum_grid_on(void)
{
    return !ui.home && !ui.menu && !ui.confirm && !ui.layer && !ui.uboot &&
           TSEL->eng_req == ENGI_DRUM && cur_page()->graph == GR_ROLL && !detail_on();
}
static uint32_t dg_lane(void) { return drum_st[song.sel].focus % DV_NLANE; }
static int dg_has(const step_t *s, uint32_t lane)
{
    uint32_t i;
    if (s->time != ST_NOTE) return 0;
    if ((s->hit >> lane) & 1u) return 1;
    for (i = 0; i < s->n && i < 4u; i++)
        if (drum_lane(s->note[i]) == lane) return 1;
    return 0;
}
static void dg_remove(step_t *s, uint32_t lane)
{
    uint32_t i, n = 0;
    s->hit &= (uint8_t)~(1u << lane);
    s->acc &= s->hit;
    for (i = 0; i < s->n && i < 4u; i++)
        if (drum_lane(s->note[i]) != lane) s->note[n++] = s->note[i];
    s->n = (uint8_t)n;
    if (!s->n && !s->hit) s->time = ST_REST;
}
static void dg_add(step_t *s, uint32_t lane)
{
    if (s->time != ST_NOTE) s->n = s->hit = s->acc = 0;
    s->time = ST_NOTE;
    s->hit |= (uint8_t)(1u << lane);
}
static void dg_keys(uint32_t pressed)
{
    uint32_t i, held = fm1_in.notes & ~kb_layer, base = ui.bank * 16u, lane = dg_lane();
    if (chain_busy()) { dg.down = dg.edited = 0; if (pressed) ui_message("STOP TO EDIT"); return; }
    if (dg.track != song.sel || dg.bank != ui.bank || dg.lane != lane) dg.down = dg.edited = 0;
    dg.track = song.sel; dg.bank = ui.bank; dg.lane = (uint8_t)lane;
    for (i = 0; i < 8u; i++)
        if ((pressed >> DG_BLACK[i]) & 1u) {
            fm1_irq_off(); drum_focus_set(TSEL, i); fm1_irq_on();
            dg.down = dg.edited = 0; dg.lane = (uint8_t)i; lane = i;
            ui.force = 1;
        }
    for (i = 0; i < 16u && base + i < (uint32_t)TSEL->p[P_SLEN]; i++) {
        uint32_t bit = 1u << i;
        if ((pressed >> DG_WHITE[i]) & 1u) {
            dg.down |= (uint16_t)bit; dg.edited &= (uint16_t)~bit;
            cursor_set((int32_t)(base + i)); follow_hold(); ui.force = 1;
        }
        if ((dg.down & bit) && !((held >> DG_WHITE[i]) & 1u)) {
            if (!(dg.edited & bit)) {
                step_t *s = &TSEL->step[base + i];
                step_undo_take(TSEL, 0);
                fm1_irq_off();
                if (dg_has(s, lane)) dg_remove(s, lane); else dg_add(s, lane);
                TSEL->seq_active = 1;
                fm1_irq_on(); motion_undo_done(TSEL);
            }
            dg.down &= (uint16_t)~bit; dg.edited &= (uint16_t)~bit; ui.force = 1;
        }
    }
}
static void dg_turn(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t i, lane = dg_lane();
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (dg.down) {
        if (slot < 2u) return;                      /* keep the held steps on their lane and page */
        step_undo_take(t, 0x8000u | slot);
        fm1_irq_off();
        for (i = 0; i < 16u; i++) if ((dg.down >> i) & 1u) {
            step_t *s = &t->step[dg.bank * 16u + i];
            /* Keep exact GM variants (909 crash/toms); new hits use the lane's own note. */
            if (!dg_has(s, lane)) dg_add(s, lane);
            if (slot == 2u) s->vel = (uint8_t)clamp((s->vel ? s->vel : 96) + steps, 1, 127);
            else {
                uint32_t j;
                /* An accent is a lane bit. Canonicalise this lane only when explicitly editing its accent. */
                for (j = 0; j < s->n; j++) if (drum_lane(s->note[j]) == lane) break;
                if (j < s->n) { /* legacy steps share velocity/accent; retain their exact pitches */
                    if (steps > 0) s->flags |= SF_ACCENT; else s->flags &= (uint8_t)~SF_ACCENT;
                } else if (steps > 0) s->acc |= (uint8_t)(1u << lane);
                else s->acc &= (uint8_t)~(1u << lane);
            }
        }
        dg.edited |= dg.down; t->seq_active = 1;
        fm1_irq_on(); motion_undo_done(t);
    } else if (slot == 0u) {
        fm1_irq_off(); drum_focus_set(t, (uint32_t)clamp((int32_t)lane + steps, 0, 7)); fm1_irq_on();
    } else if (slot == 1u) {
        uint32_t bank = (uint32_t)clamp((int32_t)ui.bank + steps, 0, (t->p[P_SLEN] - 1) / 16);
        cursor_set((int32_t)(bank * 16u));
    } else {
        uint32_t id = slot == 2u ? P_SLEN : P_SSWING;
        step_undo_take(t, 0x8100u | slot);
        t->p[id] = (int16_t)param_turn(&TP[id], t->p[id], steps);
        if (ui.cursor >= (uint32_t)t->p[P_SLEN]) cursor_set(t->p[P_SLEN] - 1);
        motion_undo_done(t);
    }
    follow_hold(); ui.force = 1;
}
static void dg_clear(void)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    step_undo_take(TSEL, 0);
    fm1_irq_off(); dg_remove(&TSEL->step[ui.cursor], dg_lane()); fm1_irq_on();
    dg.edited |= dg.down;
    motion_undo_done(TSEL); ui.force = 1; ui_message("HIT CLEARED");
}
static uint32_t dg_leds(void)
{
    uint32_t i, m = 1u << DG_BLACK[dg_lane()], base = ui.bank * 16u;
    for (i = 0; i < 16u && base + i < (uint32_t)TSEL->p[P_SLEN]; i++)
        if (dg_has(&seq_steps(TSEL)[base + i], dg_lane()) || ((dg.down >> i) & 1u) ||
            (song.playing && TSEL->seq_idx == base + i)) m |= 1u << DG_WHITE[i];
    return m;
}

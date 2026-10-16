/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_big.c: VOID FOUR's EDIT page (params.c "EDIT", GR_BIG; included by ui_draw.c). The machine's four controls,
 * engine_t.knob[] (KNOB 1..4 = CUT RES ENV + the machine's own: params.c page_id, so the knobs, MIDI LEARN, the locks
 * and MIDI OUT need no case of their own), as four big cards in a 2 x 2 grid:
 *
 *    0  header (ui_draw.c draw_head: head_big): the track's cushion, the machine, the sound, EDIT, the battery
 *   24  KNOB 1 CUTOFF    | KNOB 2 RESONANCE     two 240 x 94 strips (under CV_MAX), each two 116 x 92 cards
 *  118  KNOB 3 ENV       | KNOB 4 <machine's>   (3 px in from the screen's edges, 2 px apart)
 *  212  footer (draw_foot): the selected track's 16 steps, 28 px
 *
 * A card: the label in M (S when M cannot hold it beside the pill; never abbreviated), the K pill (the keycap: the
 * accent while the knob is hot), a 270 degree ring (KNOB_XL, r 34, 3 px: the value's arc; ENV, bipolar, from 12
 * o'clock), the value centred in the ring in XL (40 px figures), else L, else M: the first face whose every glyph is
 * in it and whose ink lies inside the ring (big_fits), the unit (S) in the ring's opening under it. The knob just
 * turned: value, arc and pill in the accent (ui.hot_col / hot_t, ~0.6 s); MONO: its label inverted too. A control
 * MOTION drives: its label in the theme colour. The value snaps (no rolling digits). Each strip is redrawn only
 * when what it shows changed (big_sig) */
#ifndef BIG_HOOK
#define BIG_HOOK(k, label, val, face) ((void)0)        /* host: what card k drew (its face: 0 XL, 1 L, 2 M) */
#endif
#define BIG_Y0 H_HEAD                                   /* the first strip; the second at BIG_Y0 + BIG_SH */
#define BIG_SH 94                                       /* a strip: two cards */
#define BIG_CW 116                                      /* a card */
#define BIG_CH 92
#define BIG_CX(k) (3 + 118 * (int32_t)((k) & 1u))       /* its left in the strip, its top: row 1 */
#define BIG_FOOT_Y (BIG_Y0 + 2 * BIG_SH)                /* the footer: 212 .. 239 */
#define BIG_FOOT_H (240 - BIG_FOOT_Y)
#define BIG_PY 6                                        /* the K pill's top (card rows 6 .. 18), the label on its line */
#define BIG_RX 24                                       /* the ring's box in the card: 24 .. 91 x 22 .. 89 */
#define BIG_RY 22
#define BIG_RD (2 * KNOB_XL_R)
#define BIG_FIT (KNOB_XL_R - KNOB_XL_W - 2)             /* the value's ink inside this radius: 2 px clear of the ring */
#define BIG_UY 76                                       /* the unit's line (S capitals rows 79 .. 87) */

static const char *const BIG_MLABEL[NENGINES] = {"DETUNE", "POSITION", "DECAY", "DRIVE"};   /* knob[3] spelled out
                                                         * (the 4-letter edit[].label stays the MACHINE pages') */
static uint32_t big_sig[2];

static uint32_t big_id(const track_t *t, uint32_t k) { return ENGINES[t->eng_req % NENGINES]->knob[k & 3u]; }
static const char *big_label(const track_t *t, uint32_t k)
{
    static const char *const L[3] = {"CUTOFF", "RESONANCE", "ENV"};
    return k < 3u ? L[k] : BIG_MLABEL[t->eng_req % NENGINES];
}

/* s in f, its ink centred across and its capitals' band (cap_y .. + cap_h of the line) up and down on a point: every
 * glyph's ink box inside a circle of r round it (the ring's inside; the boxes, not the string's: a sign's or a
 * point's ink is low or short, a corner of the string's box is empty) */
static int big_fits(const aafont_t *f, const char *s, int32_t cap_y, int32_t cap_h, int32_t r)
{
    int32_t b[4], mid2, cy2 = 2 * cap_y + cap_h, pen = 0;
    uint32_t prev = 0;
    text_ink(f, s, b);
    if (b[2] <= b[0])
        return 0;
    mid2 = b[0] + b[2];                                 /* (doubled: the half pixels kept) */
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        const aag_t *g;
        if (prev)
            pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        if (g->bw) {
            int32_t x0 = 2 * (((pen + 8) >> 4) + g->bx) - mid2, x1 = x0 + 2 * g->bw;
            int32_t y0 = 2 * g->by - cy2, y1 = y0 + 2 * g->bh;
            int32_t dx = (x0 < 0 ? -x0 : x0) > (x1 < 0 ? -x1 : x1) ? (x0 < 0 ? -x0 : x0) : (x1 < 0 ? -x1 : x1);
            int32_t dy = (y0 < 0 ? -y0 : y0) > (y1 < 0 ? -y1 : y1) ? (y0 < 0 ? -y0 : y0) : (y1 < 0 ? -y1 : y1);
            if (dx * dx + dy * dy > 4 * r * r)
                return 0;
        }
        pen += g->adv;
        prev = c;
    }
    return 1;
}
/* a value as the big cards show it (param_format's, for these cards only): a cutoff of 10 kHz or more in whole kHz
 * ("16", unit kHz: "16.0" fits no ring in L) */
static void big_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    param_format(d, v, val, unit);
    fmt_named = 0;
    if (d->fmt == F_CUTOFF && CUTOFF_HZ[v & 127] >= 10000u)
        fmt_int(val, (int32_t)((CUTOFF_HZ[v & 127] + 500u) / 1000u));
}
/* the face of a value: 0 XL, 1 L, 2 M */
static int big_face_of(const char *v)
{
    if (xl_face_has(v) && big_fits(&AF_XL, v, AF_XL_CAP_Y, AF_XL_CAP_H, BIG_FIT))
        return 0;
    if (large_face_has(v) && big_fits(&AF_L, v, AF_L_CAP_Y, AF_L_CAP_H, BIG_FIT))
        return 1;
    return 2;
}

/* card k (116 x 92) into the strip being drawn, its top-left at (x, y) */
static void draw_big_card(uint32_t k, uint32_t x, uint32_t y)
{
    const track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), k, &vp);
    const char *label = big_label(t, k), *unit = "";
    char val[12];
    int hot = k == ui.hot_col && ui.hot_t, mot, face;
    uint16_t vc = VAL(k), lc, fill = hot ? T_ACCENT : T_KEY;
    int32_t px, lx = (int32_t)x + 8, ly, lw, room, v;
    const aafont_t *lf = &AF_M, *vf;
    cv_rrect((int32_t)x, (int32_t)y, BIG_CW, BIG_CH, 6, T_SURF, T_BG);
    if (!d || !vp)
        return;
    big_format(d, *vp, val, &unit);
    card_mot_of(vp);
    mot = card_mot_next;
    card_mot_next = 0;
    card_mot = (uint8_t)((card_mot & ~(1u << k)) | (uint32_t)mot << k);
    lc = hot ? T_ACCENT : mot ? T_THEME : T_MID;
    {   /* the knob: its keycap at the right */
        px = (int32_t)x + BIG_CW - 7 - kc_w(KC_K1 + k);
        cv_keycap(px, (int32_t)y + BIG_PY, KC_K1 + k, fill, T_INK, T_SURF);
    }
    room = px - 5 - lx;
    if (ink_w(lf, label) > room)                        /* M too wide beside the pill: S, on the same line */
        lf = &AF_S;
    ly = (int32_t)y + BIG_PY + (lf == &AF_M ? CAP_IN(M, KC_H) : CAP_IN(S, KC_H));
    lw = ink_w(lf, label);
    GFX_HOOK_ALIGN(0, (int32_t)y + BIG_PY, 0, (int32_t)y + BIG_PY + KC_H, AL_V, "big card label on its pill's line");
    if (hot && T_ACCENT == T_MID) {                     /* MONO: the label inverted, the chip 2 px round its ink */
        int32_t b[4], cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
        text_ink(lf, label, b);
        cv_rrect(lx + b[0] - 2, cy - 3, lw + 4, chh + 6, 3, T_ACCENT, T_SURF);
        cv_text_on(lx, ly, lf, label, T_INK, T_ACCENT);
    } else {
        cv_text_fit(lx, ly, lf, label, lc, T_SURF, room);
    }
    v = d->fmt == F_ENUM ? enum_rank(d, *vp) : *vp;
    knob((int32_t)x + BIG_RX, (int32_t)y + BIG_RY, KNOB_XL_R, KNOB_XL_COV, KNOB_XL_ANG, v, d->min, d->max, vc);
    face = big_face_of(val);
    if (face == 2 && val[0] == '+' && big_face_of(val + 1) < 2) {   /* the '+' all that kept it from L / XL ("+100"):
                                                                     * dropped ('-' always stays) */
        str_cpy(val, val + 1, sizeof val);
        face = big_face_of(val);
    }
    vf = face == 0 ? &AF_XL : face == 1 ? &AF_L : &AF_M;
    BIG_HOOK(k, label, val, face);
    GFX_HOOK_ALIGN((int32_t)x + BIG_RX, (int32_t)y + BIG_RY, (int32_t)x + BIG_RX + BIG_RD,
                   (int32_t)y + BIG_RY + BIG_RD, AL_HV, "big card value centred in its ring");
    cv_text_in((int32_t)x + BIG_RX, (int32_t)y + BIG_RY +
               (face == 0 ? CAP_IN(XL, BIG_RD) : face == 1 ? CAP_IN(L, BIG_RD) : CAP_IN(M, BIG_RD)),
               BIG_RD, vf, val, vc, T_SURF);
    if (unit[0]) {                                      /* in the ring's opening, under the value */
        GFX_HOOK_ALIGN((int32_t)x + BIG_RX, 0, (int32_t)x + BIG_RX + BIG_RD, 0, AL_H, "big card unit centred");
        cv_text_in((int32_t)x + BIG_RX, (int32_t)y + BIG_UY, BIG_RD, &AF_S, unit, T_MID, T_SURF);
    }
}

/* what card k shows, as a number: its value (the ring's) and its text (an engine's desc hook can name a value by
 * another parameter), its label, hot / motion state */
static uint32_t big_card_sig(uint32_t k)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), k, &vp);
    uint32_t id = big_id(TSEL, k), h = id * 7919u + (k == ui.hot_col && ui.hot_t) * 104729u +
               (uint32_t)(id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u)) * 15485863u;
    if (d && vp) {
        char val[12];
        const char *unit;
        big_format(d, *vp, val, &unit);
        h = str_hash(str_hash(h + (uint32_t)(*vp + 1000) * 131u + (uint32_t)d->min * 31u + (uint32_t)d->max, val), unit);
    }
    return h;
}

/* the page's two strips (draw_columns), each when it changed */
static void draw_edit_big(void)
{
    uint32_t r, k;
    (void)motion_mask(song.sel, card_mot_mask);
    for (k = 0; k < 4u; k++)                            /* (no rolling digits here: a roll the last page left, done) */
        ui.roll[k].from[0] = 0;
    for (r = 0; r < 2u; r++) {
        uint32_t sig = str_hash(TSEL->eng_req * 977u + song.sel * 31u + ux.gen * 7u + ux.style + 1u, "EDIT");
        for (k = 2u * r; k < 2u * r + 2u; k++)
            sig = sig * 2654435761u + big_card_sig(k);
        if (!ui.force && sig == big_sig[r])
            continue;
        big_sig[r] = sig;
        cv_begin(240, BIG_SH, T_BG);
        for (k = 2u * r; k < 2u * r + 2u; k++)
            draw_big_card(k, (uint32_t)BIG_CX(k), 1u);
        if (ux.style) {                                 /* LINE: the dividers (no SURF): between the cards, under the
                                                         * header, between the strips, above the footer */
            cv_rect(BIG_CX(1) - 2, 1, 1, BIG_CH, T_RULE);
            cv_rect(BIG_CX(0), r ? BIG_SH - 1 : 0, BIG_CX(1) + BIG_CW - BIG_CX(0), 1, T_RULE);
            if (!r)
                cv_rect(BIG_CX(0), BIG_SH - 1, BIG_CX(1) + BIG_CW - BIG_CX(0), 1, T_RULE);
        }
        cv_blit(0, BIG_Y0 + r * BIG_SH);
    }
}

/* the header's left on EDIT (draw_head, no message or layer showing): from x, the machine (M, THEME) and the sound
 * (M, TEXT; ellipsised), "EDIT" (S, MID) ending at r */
static void head_big(int32_t x, int32_t r)
{
    char pn[16];
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    int32_t ew = text_w(&AF_S, "EDIT");
    sound_name(TSEL, pn);
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    x = cv_text(x, HEAD_MY, &AF_M, e->name, T_THEME) + 7;
    if (pn[0]) {
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_free_text(x, HEAD_MY, &AF_M, pn, T_TEXT, T_BG, r - ew - 8 - x);
    }
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(r - ew, HEAD_SY, &AF_S, "EDIT", T_MID);
}

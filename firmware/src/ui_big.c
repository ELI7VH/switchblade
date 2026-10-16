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

/* ------------------------------------------------------------------ HOME --- */
/* VOID FOUR's HOME (ui_draw.c draw_columns, ui.home without a layer: big_home). Its knobs are TRACK PRESET VOLUME
 * TEMPO (ui.c HK_*, ui_input.c home_turn); the dedicated encoders stay: ALGORITHM the track, PRESETS the sound.
 *
 *    0  header (draw_head): the track's cushion, the transport and the BPM, the battery
 *   24  the four tracks as tabs (each 57 x 42 on its card column, its own canvas): the track's number in L, the
 *       machine in S; the selected tab THEME (its ink INK), the others SURF with the track's output meter at the right
 *       (MIXER's meter data, ts.meter); a muted track's tab DIM (its meter empty)
 *   68  the sound (240 x 80): the category (S) and the number in the machine's list ("07 / 32"; a user preset: USER,
 *       "SLOT 07") on one line, the name in L (M when L cannot hold it or lacks a glyph), the track's LEVEL as a bar
 *       and its dB (param_format's: "-6.0 dB")
 *  148  KNOB 1..4 (240 x 64, four 57 x 62 cards): the label (S), the value in L (M when too wide), its unit (S) under it;
 *       the knob just turned: its value and label in the accent (MONO: the label inverted); TEMPO on an external
 *       clock DIM ("EXT": the clock sets it); VOLUME driven by MOTION: its label in the theme colour
 *  212  footer (draw_foot, as EDIT's): the selected track's 16 steps, 28 px
 * LINE (no SURF): 1 px rules under the header, between the tabs (draw_gaps), under them, under the sound, between the
 * cards and above the footer, as EDIT's. Each strip (a tab) is drawn again only when what it shows changed (home_sig) */
#ifndef HOME_HOOK
#define HOME_HOOK(cat, num, name, face) ((void)0)       /* host: the sound panel's text, its name's face (1 L, 2 M) */
#endif
#define HOME_TY H_HEAD                                  /* the tabs: 24 .. 67 */
#define HOME_TH 44
#define HOME_PY (HOME_TY + HOME_TH)                     /* the sound: 68 .. 147 */
#define HOME_PH 80
#define HOME_CY (HOME_PY + HOME_PH)                     /* the cards: 148 .. 211 */
#define HOME_CH 64
typedef char home_fits[HOME_CY + HOME_CH == BIG_FOOT_Y ? 1 : -1];   /* (EDIT's footer: the steps do not move) */
#define HT_H 42                                         /* a tab (rows 1 .. 42 of its canvas) */
#define HT_DY 5                                         /* its number's capitals (L): rows 5 .. 25 */
#define HT_NY 30                                        /* the machine's (S): rows 30 .. 38; both centred on the tab */
#define HT_MX 50                                        /* the meter: 3 x 34 at x 50 (the text centred on the tab: */
#define HT_MW 3                                         /* its widest, DRIFT, ends at x 44), rows 5 .. 38 */
#define HT_MH 34
#define HP_VAL (8 + 1 + 6)                              /* the panel's "value unit": param_format's value (<= 5 chars, 8 with
                                                         * margin) + " " + its unit (<= 5 chars) + 0; the append is bounded too */
#define HP_X0 11                                        /* the sound's text: x 11 .. 229 */
#define HP_X1 229
#define HP_AY 6                                         /* its first line's band: rows 6 .. 21 (category S, number M) */
#define HP_AH 16
#define HP_NY 27                                        /* the name's capitals (L): rows 27 .. 47 */
#define HP_BY 57                                        /* the LEVEL bar: rows 57 .. 70 */
#define HP_BH 14
#define HC_H 62                                         /* a card (rows 1 .. 62) */
#define HC_LY 8                                         /* its label's capitals (S): rows 8 .. 16 */
#define HC_VY 22                                        /* its value's band (L capitals): rows 22 .. 42 */
#define HC_UY 48                                        /* its unit's capitals (S): rows 48 .. 56 */
static uint32_t home_sig[NTRK + 2u];                    /* the tabs, the sound, the cards */

/* tab c (57 x 44) */
static void home_tab(uint32_t c)
{
    track_t *t = &trk[c];
    uint32_t sel = c == song.sel, mute = t->p[P_MUTE] != 0, lit = (uint32_t)midi_lit(c), sig;
    int32_t m = mute ? 0 : meter_px(t->peak);
    uint16_t bg = sel ? T_THEME : T_SURF;
    uint16_t dc = sel ? (mute ? ux_mix(T_THEME, T_INK, 55) : T_INK) : mute ? T_DIM : lit ? T_ACCENT : T_TEXT;
    uint16_t nc = sel ? (mute ? ux_mix(T_THEME, T_INK, 55) : T_INK) : mute ? T_DIM : T_MID;
    const char *en = ENGINES[t->eng_req % NENGINES]->name;
    char d[2] = {(char)('1' + c), 0};
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;                            /* (falls as the MIXER's: ~2 dB a frame) */
    ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
    sig = str_hash(1u + sel + mute * 2u + lit * 4u + ux.gen * 8u + ux.style * 7919u + (sel ? 0u : ts.meter[c]) * 131u, en);
    if (!ui.force && sig == home_sig[c])
        return;
    home_sig[c] = sig;
    cv_begin(CARD_W, HOME_TH, T_BG);
    cv_rrect(0, 1, CARD_W, HT_H, 6, bg, T_BG);
    if (ux.style) {                                     /* LINE: the rules over and under the tab */
        cv_rect(0, 0, CARD_W, 1, T_RULE);
        cv_rect(0, HOME_TH - 1, CARD_W, 1, T_RULE);
    }
    GFX_HOOK_ALIGN(0, 0, CARD_W, 0, AL_H, "home tab number centred");
    cv_text_in(0, HT_DY - AF_L_CAP_Y, CARD_W, &AF_L, d, dc, bg);
    GFX_HOOK_ALIGN(0, 0, CARD_W, 0, AL_H, "home tab machine centred");
    cv_text_in(0, HT_NY - AF_S_CAP_Y, CARD_W, &AF_S, en, nc, bg);
    if (!sel) {                                         /* the output meter (MIXER's), bottom up */
        int32_t h = ts.meter[c] * HT_MH / (TS_MH - 2);
        cv_rrect(HT_MX, HT_DY, HT_MW, HT_MH, 1, T_RAISE, bg);
        if (h)
            cv_rrect(HT_MX, HT_DY + HT_MH - h, HT_MW, h, h >= 3 ? 1 : 0, T_MID, T_RAISE);
    }
    cv_blit((uint32_t)CARD_X(c), HOME_TY);
}

/* the sound's category, its number ("07 / 32"; a user preset's slot) and name */
static void home_sound(char *cat, char *num, char *name)
{
    uint32_t total, cur = eng_list_pos(&total), u = user_of(TSEL), np = preset_shown(TSEL->eng_req % NENGINES);
    sound_name(TSEL, name);
    if (u < UP_SLOTS) {
        str_cpy(cat, "USER", 16);
        str_cpy(num, "SLOT ", 16);
        num[5] = (char)('0' + (u + 1u) / 10u % 10u);
        num[6] = (char)('0' + (u + 1u) % 10u);
        num[7] = 0;
    } else {
        str_cpy(cat, CAT_NAME[track_cat(TSEL) % NCAT], 16);
        num[0] = (char)('0' + (cur + 1u) / 10u % 10u);
        num[1] = (char)('0' + (cur + 1u) % 10u);
        str_cpy(num + 2, " / ", 14);
        fmt_int(num + 5, (int32_t)np);
    }
    (void)total;
}

/* the sound panel (240 x 80) */
static void home_panel(void)
{
    char cat[16], num[16], name[16], val[HP_VAL];
    const char *unit = "";
    int16_t *vp;
    const param_desc_t *d = home_param(HK_VOLUME, &vp);
    int hot = ui.hot_col == HK_VOLUME && ui.hot_t, face, mute = TSEL->p[P_MUTE] != 0;
    int32_t fw = text_w(&AF_M, "-00.0 dB"), bw, fx, v = *vp;
    uint32_t sig;
    home_sound(cat, num, name);
    param_format(d, v, val, &unit);
    fmt_named = 0;
    if (unit[0]) {                                      /* "value unit", bounded: val is HP_VAL, whatever the pair */
        unsigned n = str_len(val);
        if (n + 2u < HP_VAL) {
            val[n++] = ' ';
            str_cpy(val + n, unit, HP_VAL - n);
        }
    }
    face = large_face_has(name) && ink_w(&AF_L, name) <= HP_X1 - HP_X0 ? 1 : 2;
    HOME_HOOK(cat, num, name, face);
    sig = str_hash(str_hash(str_hash(str_hash(ux.gen * 31u + ux.style * 7u + (uint32_t)hot * 3u + (uint32_t)mute * 5u +
                                              (uint32_t)v * 131u, cat), num), name), val);
    if (!ui.force && sig == home_sig[NTRK])
        return;
    home_sig[NTRK] = sig;
    cv_begin(240, HOME_PH, T_BG);
    cv_rrect(3, 1, 234, HOME_PH - 2, 6, T_SURF, T_BG);
    if (ux.style)                                       /* LINE: the rule under it */
        cv_rect(3, HOME_PH - 1, 234, 1, T_RULE);
    GFX_HOOK_ALIGN(0, HP_AY, 0, HP_AY + HP_AH, AL_V, "home sound line on its band");
    cv_text_fit(HP_X0, HP_AY + CAP_IN(S, HP_AH), &AF_S, cat, T_THEME, T_SURF, 110);
    GFX_HOOK_ALIGN(0, HP_AY, 0, HP_AY + HP_AH, AL_V, "home sound line on its band");
    cv_text_r(HP_X1, HP_AY + CAP_IN(M, HP_AH), &AF_M, num, T_MID, T_SURF);
    if (face == 1) {
        cv_text_on(HP_X0, HP_NY - AF_L_CAP_Y, &AF_L, name, T_TEXT, T_SURF);
    } else {
        GFX_HOOK_ALIGN(0, HP_NY, 0, HP_NY + AF_L_CAP_H, AL_V, "home sound name (M) on L's band");
        cv_free_text(HP_X0, HP_NY + CAP_IN(M, AF_L_CAP_H), &AF_M, name, T_TEXT, T_SURF, HP_X1 - HP_X0);
    }
    bw = HP_X1 - fw - 10 - HP_X0;                       /* the LEVEL: a bar, its dB right of it */
    fx = v * bw / 127;
    cv_rrect(HP_X0, HP_BY, bw, HP_BH, 3, T_RAISE, T_SURF);
    if (fx > 0)
        cv_rrect(HP_X0, HP_BY, fx < 6 ? 6 : fx, HP_BH, 3, mute ? T_DIM : hot ? T_ACCENT : T_THEME, T_RAISE);
    GFX_HOOK_ALIGN(0, HP_BY, 0, HP_BY + HP_BH, AL_V, "home level value on its bar");
    cv_text_r(HP_X1, HP_BY + CAP_IN(M, HP_BH), &AF_M, val, mute ? T_DIM : hot ? T_ACCENT : T_TEXT, T_SURF);
    cv_blit(0, HOME_PY);
}

/* what card k shows: its label, value, unit, the value's colour */
static void home_card_of(uint32_t k, char *val, const char **unit, uint16_t *vc)
{
    const track_t *t = TSEL;
    *vc = VAL(k);
    *unit = "";
    if (k == HK_TRACK) {
        val[0] = (char)('1' + song.sel);
        val[1] = 0;
        *unit = ENGINES[t->eng_req % NENGINES]->name;
    } else if (k == HK_PRESET) {
        uint32_t u = user_of(t), total, cur = eng_list_pos(&total);
        uint32_t n = u < UP_SLOTS ? u + 1u : cur + 1u;
        uint32_t i = 0;
        if (u < UP_SLOTS)
            val[i++] = 'U';
        val[i++] = (char)('0' + n / 10u % 10u);
        val[i++] = (char)('0' + n % 10u);
        val[i] = 0;
        *unit = u < UP_SLOTS ? "USER" : "/ 32";
    } else if (k == HK_VOLUME) {
        int32_t lv = t->p[P_LEVEL];
        if (lv > 0) {                                   /* whole dB, as MIXER's knob (the panel: param_format's) */
            int32_t d = LEVEL_DB_X10[lv & 127];
            fmt_int(val, (d + (d < 0 ? -5 : 5)) / 10);
            *unit = "dB";
        } else {
            str_cpy(val, "OFF", 12);
        }
        if (t->p[P_MUTE])
            *vc = T_DIM;
    } else {
        fmt_int(val, song.g[G_BPM]);
        *unit = "BPM";
        if (song.g[G_CLOCK]) {                          /* an external clock sets it: read-only */
            *unit = "EXT";
            *vc = T_DIM;
        }
    }
}

/* the four cards (240 x 64) */
static void home_cards(void)
{
    static const char *const L[4] = {"TRACK", "PRESET", "VOLUME", "TEMPO"};
    char val[4][12];
    const char *unit[4];
    uint16_t vc[4];
    uint32_t k, sig = ux.gen * 31u + ux.style * 7u, mot = (card_mot_mask[P_LEVEL / 32u] >> (P_LEVEL % 32u)) & 1u;
    for (k = 0; k < 4u; k++) {
        home_card_of(k, val[k], &unit[k], &vc[k]);
        sig = str_hash(str_hash(sig * 2654435761u + vc[k] * 131u + (k == ui.hot_col && ui.hot_t) * 7u, val[k]), unit[k]);
    }
    sig += mot * 104729u;
    card_mot = (uint8_t)(mot << HK_VOLUME);
    if (!ui.force && sig == home_sig[NTRK + 1u])
        return;
    home_sig[NTRK + 1u] = sig;
    cv_begin(240, HOME_CH, T_BG);
    for (k = 0; k < 4u; k++) {
        int32_t x = CARD_X(k), lw = ink_w(&AF_S, L[k]);
        int hot = k == ui.hot_col && ui.hot_t, face;
        uint16_t lc = hot ? T_ACCENT : k == HK_VOLUME && mot ? T_THEME : T_MID;
        cv_rrect(x, 1, CARD_W, HC_H, 6, T_SURF, T_BG);
        GFX_HOOK_ALIGN(x, 0, x + CARD_W, 0, AL_H, "home card label centred");
        if (hot && T_ACCENT == T_MID) {                 /* MONO: the label inverted, the chip 2 px round its ink */
            int32_t b[4], ix = x + ink_in(&AF_S, L[k], CARD_W);
            text_ink(&AF_S, L[k], b);
            cv_rrect(ix + b[0] - 2, HC_LY - 3, lw + 4, AF_S_CAP_H + 6, 3, T_ACCENT, T_SURF);
            cv_text_on(ix, HC_LY - AF_S_CAP_Y, &AF_S, L[k], T_INK, T_ACCENT);
        } else {
            cv_text_in(x, HC_LY - AF_S_CAP_Y, CARD_W, &AF_S, L[k], lc, T_SURF);
        }
        face = large_face_has(val[k]) && ink_w(&AF_L, val[k]) <= CARD_W - 6 ? 1 : 2;
        BIG_HOOK(k, L[k], val[k], face);
        GFX_HOOK_ALIGN(x, HC_VY, x + CARD_W, HC_VY + AF_L_CAP_H, AL_HV, "home card value centred");
        cv_text_in(x, HC_VY + (face == 1 ? -AF_L_CAP_Y : CAP_IN(M, AF_L_CAP_H)), CARD_W, face == 1 ? &AF_L : &AF_M,
                   val[k], vc[k], T_SURF);
        if (unit[k][0]) {
            GFX_HOOK_ALIGN(x, 0, x + CARD_W, 0, AL_H, "home card unit centred");
            cv_text_in(x, HC_UY - AF_S_CAP_Y, CARD_W, &AF_S, unit[k], T_MID, T_SURF);
        }
    }
    if (ux.style) {                                     /* LINE: the rules between the cards, above the footer */
        for (k = 0; k < 3u; k++)
            cv_rect(CARD_X(k) + CARD_W, 1, 1, HC_H, T_RULE);
        cv_rect(CARD_X(0), HOME_CH - 1, CARD_X(3) + CARD_W - CARD_X(0), 1, T_RULE);
    }
    cv_blit(0, HOME_CY);
}

/* the BG beside the tabs; LINE: between two tabs a rule down, and the rules over and under the tabs across (each pixel
 * written once: no flash, #181) */
static void home_gaps(void)
{
    uint32_t i, y = HOME_TY, h = HOME_TH;
    lcd_fill(0, y, (uint32_t)CARD_X(0), h, T_BG);
    for (i = 0; i < 4u; i++) {                          /* right of each tab */
        uint32_t x = (uint32_t)(CARD_X(i) + CARD_W), w = i < 3u ? (uint32_t)CARD_X(i + 1u) - x : 240u - x;
        if (ux.style && i < 3u) {
            lcd_fill(x, y, w, 1, T_RULE);
            lcd_fill(x, y + h - 1u, w, 1, T_RULE);
            lcd_rule(x, y + 1u, 1, h - 2u);
            lcd_fill(x + 1u, y + 1u, w - 1u, h - 2u, T_BG);
        } else {
            lcd_fill(x, y, w, h, T_BG);
        }
    }
}

static void draw_home_big(void)
{
    uint32_t c;
    (void)motion_mask(song.sel, card_mot_mask);
    for (c = 0; c < 4u; c++)                            /* (no rolling digits here: a roll the last page left, done) */
        ui.roll[c].from[0] = 0;
    if (ui.force) {                                     /* the BG beside the tabs (LINE: the dividers); the meters anew */
        home_gaps();
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    for (c = 0; c < NTRK; c++)
        home_tab(c);
    home_panel();
    home_cards();
}

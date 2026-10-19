/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_ctl.c: the drawing primitives of the machines' controls (Switch Blade UI.4): anti-aliased discs, rings and arcs, a thick
 * line, a bevelled card, the brushed silver of the TB-303 and the wood of the OB-6. Everything is computed as it is drawn
 * (no bitmaps): coverage by 4 x 4 samples where an edge falls, integers only (no libm, no division in the sample loops).
 * Coordinates of discs and arcs are in 1/8 px (x8); angles in degrees clockwise from 12 o'clock. Drawn on the canvas (cv_oy
 * honoured, everything clipped to it); colours are tokens of the palette or blends of two (the on-palette test of ui_test.c
 * counts the textures' end colours as tokens) */

/* ---- colours the textures end at: wood (OB-Z's cheeks) between a dark and a light grain ---- */
#define WOOD_LO 0x59C4u                                 /* #5A3A20 */
#define WOOD_HI 0x92A5u                                 /* #8E5A2A */

static uint16_t ctl_blend(uint16_t a, uint16_t b, uint32_t t)   /* a toward b, t 0..16 */
{
    uint32_t r = ((uint32_t)(a >> 11) * (16u - t) + (uint32_t)(b >> 11) * t + 8u) >> 4;
    uint32_t g = ((uint32_t)((a >> 5) & 63u) * (16u - t) + (uint32_t)((b >> 5) & 63u) * t + 8u) >> 4;
    uint32_t l = ((uint32_t)(a & 31u) * (16u - t) + (uint32_t)(b & 31u) * t + 8u) >> 4;
    return (uint16_t)(r << 11 | g << 5 | l);
}
/* colour c over the canvas pixel (x, y), a 0..16 of it */
static void cv_mix(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint32_t i;
    y += cv_oy;
    if (!a || (uint32_t)x >= cv_w || (uint32_t)y >= cv_h)
        return;
    i = (uint32_t)y * cv_w + (uint32_t)x;
    cv_px[i] = a >= 16u ? swap16(c) : swap16(ctl_blend(swap16(cv_px[i]), c, a));
}

/* ---- sin / cos in Q10 (1024 = 1.0), degrees: a table of a quarter turn ---- */
static const uint16_t CTL_SIN[91] = {
    0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213, 230, 248, 265, 282, 299, 316, 333, 350, 367, 384, 400,
    416, 433, 449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630, 644, 658, 672, 685, 698, 711, 724, 737,
    749, 761, 773, 784, 796, 807, 818, 828, 839, 849, 859, 868, 878, 887, 896, 904, 912, 920, 928, 935, 943, 949, 956,
    962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1021, 1022, 1023, 1023, 1024};
static int32_t ctl_sin(int32_t d)
{
    d %= 360;
    if (d < 0)
        d += 360;
    return d <= 90 ? CTL_SIN[d] : d <= 180 ? CTL_SIN[180 - d] : d <= 270 ? -(int32_t)CTL_SIN[d - 180] : -(int32_t)CTL_SIN[360 - d];
}
static int32_t ctl_cos(int32_t d) { return ctl_sin(d + 90); }

/* the 16 samples of a pixel: offsets in 1/8 px from its centre */
static const int8_t CTL_SO[4] = {-3, -1, 1, 3};

/* a filled disc of radius r8 at (cx8, cy8), colour c over what is there */
static void cv_disc(int32_t cx8, int32_t cy8, int32_t r8, uint16_t c)
{
    int32_t x0 = (cx8 - r8) / 8 - 1, x1 = (cx8 + r8) / 8 + 1, y0 = (cy8 - r8) / 8 - 1, y1 = (cy8 + r8) / 8 + 1, x, y;
    int32_t inner = r8 > 6 ? (r8 - 6) * (r8 - 6) : -1, outer = (r8 + 6) * (r8 + 6);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 8 + 4 - cx8, dy = y * 8 + 4 - cy8, d2 = dx * dx + dy * dy, n = 0, i, j;
            if (d2 >= outer)
                continue;
            if (d2 <= inner) {
                cv_mix(x, y, c, 16u);
                continue;
            }
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    int32_t sx = dx + CTL_SO[i], sy = dy + CTL_SO[j];
                    n += sx * sx + sy * sy <= r8 * r8;
                }
            cv_mix(x, y, c, (uint32_t)n);
        }
}
/* an annulus between ri8 and ro8, or (a1 > a0) the part of it clockwise from the angle a0 to a1 (a1 - a0 up to 360; a sector
 * is tested at the pixel's centre, its ends are not anti-aliased) */
static void cv_arc(int32_t cx8, int32_t cy8, int32_t ro8, int32_t ri8, int32_t a0, int32_t a1, uint16_t c)
{
    int32_t x0 = (cx8 - ro8) / 8 - 1, x1 = (cx8 + ro8) / 8 + 1, y0 = (cy8 - ro8) / 8 - 1, y1 = (cy8 + ro8) / 8 + 1, x, y;
    int32_t ox = (ro8 + 6) * (ro8 + 6), ix = ri8 > 6 ? (ri8 - 6) * (ri8 - 6) : -1;
    int32_t ix2 = (ri8 + 6) * (ri8 + 6), ox2 = ro8 > 6 ? (ro8 - 6) * (ro8 - 6) : -1;
    int32_t ux0 = ctl_sin(a0), uy0 = -ctl_cos(a0), ux1 = ctl_sin(a1), uy1 = -ctl_cos(a1);    /* the edges' directions */
    int32_t wide = a1 - a0 > 180, full = a1 - a0 >= 360;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t dx = x * 8 + 4 - cx8, dy = y * 8 + 4 - cy8, d2 = dx * dx + dy * dy, n = 0, i, j;
            if (d2 >= ox || d2 <= ix)
                continue;
            if (!full) {
                int32_t za = ux0 * dy - uy0 * dx, zb = ux1 * dy - uy1 * dx;     /* z(u0, p), z(u1, p): the sign says which side */
                if (wide ? (zb >= 0 && za <= 0) : !(za >= 0 && zb <= 0))     /* (wide: not in the wedge from u1 round to u0) */
                    continue;
            }
            if (d2 < ox2 && d2 > ix2) {
                cv_mix(x, y, c, 16u);
                continue;
            }
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    int32_t sx = dx + CTL_SO[i], sy = dy + CTL_SO[j], s2 = sx * sx + sy * sy;
                    n += s2 <= ro8 * ro8 && s2 >= ri8 * ri8;
                }
            cv_mix(x, y, c, (uint32_t)n);
        }
}
/* a line of width 2 r8 from (x0, y0) to (x1, y1) (x8, y8): discs every half pixel */
static void cv_stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r8, uint16_t c)
{
    int32_t dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy), k;
    n = n / 4 + 1;
    for (k = 0; k <= n; k++)
        cv_disc(x0 + dx * k / n, y0 + dy * k / n, r8, c);
}
/* the point at angle a (degrees clockwise from up), radius r8 from (cx8, cy8): x8, y8 */
static void ctl_pt(int32_t cx8, int32_t cy8, int32_t r8, int32_t a, int32_t *x, int32_t *y)
{
    *x = cx8 + r8 * ctl_sin(a) / 1024;
    *y = cy8 - r8 * ctl_cos(a) / 1024;
}

/* a card with its bevel: the fill, a 1 px highlight along the top and the left, a shadow along the bottom and the right (the
 * straight parts of the edges: the rounded corners keep the fill's) */
static void ctl_card(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t fill, uint16_t under)
{
    uint16_t hi = ux_mix(fill, T_TEXT, 14), sh = ux_mix(fill, under, 60);
    cv_rrect(x, y, w, h, r, fill, under);
    cv_rect(x + r, y, w - 2 * r, 1, hi);
    cv_rect(x, y + r, 1, h - 2 * r, hi);
    cv_rect(x + r, y + h - 1, w - 2 * r, 1, sh);
    cv_rect(x + w - 1, y + r, 1, h - 2 * r, sh);
}
/* a screw: a disc with a slot, at its centre (x, y) */
static void ctl_screw(int32_t x, int32_t y, uint16_t base, uint16_t ink)
{
    cv_disc(x * 8 + 4, y * 8 + 4, 28, ux_mix(base, ink, 40));
    cv_disc(x * 8 + 4, y * 8 + 4, 22, base);
    cv_rect(x - 2, y, 5, 1, ink);
}

/* a small integer hash of a row / column (the textures' fixed seed) */
static uint32_t ctl_hash(uint32_t v)
{
    v = (v ^ 61u) ^ (v >> 16);
    v *= 9u;
    v ^= v >> 4;
    v *= 0x27D4EB2Du;
    v ^= v >> 15;
    return v;
}
/* brushed silver over the rows y0 .. y0 + h - 1 of the screen, x0 .. x0 + w - 1 of the canvas (the TB-303's panel): a fixed
 * pattern of horizontal lines, each a shade of BG toward SURF (the same rows every time: the screen row is the seed) */
static void ctl_brushed(int32_t x0, int32_t w, int32_t screen_y0, int32_t h)
{
    uint16_t sh[4];
    int32_t r;
    sh[0] = T_BG;
    sh[1] = ctl_blend(T_BG, T_SURF, 3);
    sh[2] = ctl_blend(T_BG, T_SURF, 6);
    sh[3] = ctl_blend(T_BG, T_SURF, 10);
    for (r = 0; r < h; r++) {
        uint32_t v = ctl_hash(0x303u + (uint32_t)(screen_y0 + r) * 7919u);
        uint32_t k = v & 15u;
        cv_rect(x0, r, w, 1, sh[k < 6u ? 0 : k < 10u ? 1 : k < 14u ? 2 : 3]);
        if ((v >> 8) % 5u == 0u)                         /* a streak: part of the row a shade lighter / darker */
            cv_rect(x0 + (int32_t)((v >> 12) % (uint32_t)(w / 2)), r, w / 3, 1, sh[1 + (v >> 20) % 3u]);
    }
}
/* wood over a canvas rectangle: vertical grain, each column a shade between WOOD_LO and WOOD_HI, changing every 16 rows (the
 * screen row and column are the seed) */
static void ctl_wood(int32_t x, int32_t y, int32_t w, int32_t h, int32_t screen_x, int32_t screen_y)
{
    int32_t cx, ry;
    for (cx = 0; cx < w; cx++)
        for (ry = 0; ry < h;) {
            int32_t band = (screen_y + ry) >> 4, len = 16 - ((screen_y + ry) & 15);
            uint32_t v = ctl_hash((uint32_t)(screen_x + cx) * 31u + (uint32_t)band * 977u + 5u) % 6u;
            if (len > h - ry)
                len = h - ry;
            cv_rect(x + cx, y + ry, 1, len, ctl_blend(WOOD_LO, WOOD_HI, 3u + v * 2u));
            ry += len;
        }
}

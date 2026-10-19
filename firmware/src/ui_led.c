/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_led.c: 7-segment LED readouts (Switch Blade UI.4). Drawn with rectangles, no tables of bitmaps: each digit is up to
 * seven segments, every one drawn dim first (the unlit segments of a real display), then the lit ones over them, on a dark
 * recess with a 2 px rounded corner. The colours are the LED's own and the same in every palette (an LED is an LED):
 * lit red, unlit nearly black red, the recess darker still.
 *
 *   LED_S  digit 7 x 11, segments 1 px   the header's BPM, EDIT's readouts, HOME's preset number     recess 15 tall
 *   LED_L  digit 11 x 18, segments 2 px  HOME's track number                                         recess 24 tall
 *
 * The segments keep a pixel of air at every corner (a horizontal one is inset by the vertical's width), so the eight
 * segments read as a digit at 11 px on the 240 x 240 panel. Digits 0 .. 9, '-' (the middle segment), blank (all dim).
 * A readout is `n` cells (led_cells: what a string needs), the text right aligned in them (a short number leaves its left cells dim: "  7" is a dim 8, a dim
 * 8 and a lit 7). LEDs switch instantly (no rolling digits). Drawn on the canvas at (x, y) = the recess's top left */
#define LED_ON 0xF9A5u                                  /* #FF3428 lit segments */
#define LED_OFF 0x2041u                                 /* #220908 unlit segments */
#define LED_BG 0x0800u                                  /* #0E0303 the recess */
#define LED_HOT 0xFD95u                                 /* #FFB4A8 lit segments of a readout being changed (the header's BPM) */
enum { LED_S, LED_L, LED_NSZ };
typedef struct { uint8_t w, h, t, adv, padx, pady; } led_sz_t;
static const led_sz_t LED_SZ[LED_NSZ] = {{7, 11, 1, 9, 3, 2}, {11, 18, 2, 14, 4, 3}};
#define LED_A 1u
#define LED_B 2u
#define LED_C 4u
#define LED_D 8u
#define LED_E 16u
#define LED_F 32u
#define LED_G 64u
static const uint8_t LED_DIGIT[10] = {
    LED_A | LED_B | LED_C | LED_D | LED_E | LED_F, LED_B | LED_C, LED_A | LED_B | LED_G | LED_E | LED_D,
    LED_A | LED_B | LED_G | LED_C | LED_D, LED_F | LED_G | LED_B | LED_C, LED_A | LED_F | LED_G | LED_C | LED_D,
    LED_A | LED_F | LED_G | LED_E | LED_D | LED_C, LED_A | LED_B | LED_C,
    LED_A | LED_B | LED_C | LED_D | LED_E | LED_F | LED_G, LED_A | LED_B | LED_C | LED_D | LED_F | LED_G};
/* the lit segments of a character: a digit, '-' (G), anything else none */
static uint32_t led_mask(char ch)
{
    return ch >= '0' && ch <= '9' ? LED_DIGIT[ch - '0'] : ch == '-' ? LED_G : 0u;
}
static int32_t led_w(uint32_t sz, uint32_t n)           /* a readout of n cells: the recess's width */
{
    const led_sz_t *z = &LED_SZ[sz % LED_NSZ];
    return 2 * z->padx + (int32_t)n * z->w + (int32_t)(n - 1u) * (z->adv - z->w);
}
static int32_t led_h(uint32_t sz) { return 2 * LED_SZ[sz % LED_NSZ].pady + LED_SZ[sz % LED_NSZ].h; }
/* one digit's segments in colour c (a mask), its cell's top left at (x, y) */
static void led_segs(int32_t x, int32_t y, uint32_t sz, uint32_t mask, uint16_t c)
{
    const led_sz_t *z = &LED_SZ[sz % LED_NSZ];
    int32_t t = z->t, w = z->w, h = z->h, hv = (h - 3 * t) / 2;   /* a vertical's length */
    if (mask & LED_A) cv_rect(x + t, y, w - 2 * t, t, c);
    if (mask & LED_G) cv_rect(x + t, y + t + hv, w - 2 * t, t, c);
    if (mask & LED_D) cv_rect(x + t, y + h - t, w - 2 * t, t, c);
    if (mask & LED_F) cv_rect(x, y + t, t, hv, c);
    if (mask & LED_B) cv_rect(x + w - t, y + t, t, hv, c);
    if (mask & LED_E) cv_rect(x, y + 2 * t + hv, t, hv, c);
    if (mask & LED_C) cv_rect(x + w - t, y + 2 * t + hv, t, hv, c);
}
/* the cells a string needs: its characters but '.' (the decimal point of the cell before it) and '+' (not drawn) */
static uint32_t led_cells(const char *s)
{
    uint32_t n = 0;
    for (; *s; s++)
        n += *s != '.' && *s != '+';
    return n;
}
/* a readout: the recess at (x, y), n cells, s right aligned (a string longer than n shows its last n characters; a '.' lights
 * the decimal point of the digit before it, every cell has one, unlit, at its lower right); `under` is what lies behind the
 * recess's corners, `lit` the lit segments' colour (LED_ON; LED_HOT while it is being changed). Returns the recess's width */
static int32_t cv_led(int32_t x, int32_t y, uint32_t sz, uint32_t n, const char *s, uint16_t under, uint16_t lit)
{
    const led_sz_t *z = &LED_SZ[sz % LED_NSZ];
    char ch[12];
    uint8_t dp[12];
    uint32_t len = 0, i;
    int32_t w = led_w(sz, n);
    for (; *s && len < 11u; s++) {
        if (*s == '+')
            continue;
        if (*s == '.' && len) {
            dp[len - 1u] = 1;
            continue;
        }
        ch[len] = *s;
        dp[len++] = 0;
    }
    cv_rrect(x, y, w, led_h(sz), 2, LED_BG, under);
    for (i = 0; i < n; i++) {
        int32_t cx = x + z->padx + (int32_t)i * z->adv, cy = y + z->pady;
        int32_t k = (int32_t)len - (int32_t)n + (int32_t)i;           /* the character in this cell */
        led_segs(cx, cy, sz, 127u, LED_OFF);
        cv_rect(cx + z->w, cy + z->h - z->t, z->t, z->t, k >= 0 && dp[k] ? lit : LED_OFF);
        if (k >= 0)
            led_segs(cx, cy, sz, led_mask(ch[k]), lit);
    }
    return w;
}

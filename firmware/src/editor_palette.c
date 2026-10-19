/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* CUSTOM, the user's palette, for the editor (1.6, #244; EDITOR_PROTOCOL.md "CUSTOM palette"; INFO 45 01 5). Switch Blade has one
 * colour scheme (the machine's, gfx.c UI_MACHINE_PALS) and no CUSTOM: the two commands stay so an editor that sends them gets
 * no error. PALETTE_GET answers flags 0 (none stored, none in use) and the five colours in use (the machine palette on
 * screen: BG SURF TEXT THEME ACCENT, each as three bytes: red 0..31, green 0..63, blue 0..31 of RGB565). PALETTE_SET
 * (preview, store, remove, drop the preview) is accepted, changes nothing, and answers rc 0 with that body */
enum { ED_PALETTE_GET = 80, ED_PALETTE_SET };
#define ED_PAL_N 5u
static void ed_palette_body(void)
{
    const uint16_t *tok = &ux.bg;                      /* (BG SURF TEXT THEME ACCENT in a row) */
    uint32_t i;
    ed_b(0);
    ed_b(ED_PAL_N);
    for (i = 0; i < ED_PAL_N; i++) {
        uint32_t c = tok[i];
        ed_b(c >> 11); ed_b((c >> 5) & 63u); ed_b(c & 31u);
    }
}
static int ed_palette_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    (void)a; (void)n;
    if (cmd == ED_PALETTE_GET) {
        ed_palette_body();
        return 1;
    }
    if (cmd != ED_PALETTE_SET)
        return 0;
    ed_b(0);                                           /* accepted, ignored */
    ed_palette_body();
    return 1;
}

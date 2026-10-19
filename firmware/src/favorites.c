/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
#define FAV_ROWS 4u              /* the engines with a row of favourites: the four machines */
/* Stable engine/preset references; fixed capacity independent of optional engines. */
typedef struct {
    uint8_t factory[16][32]; /* engine 0..3, preset 0..255 (rows 4..13 unused; 14: the MIDI LEARN map, 15: settings) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
/* where engine e's preset k is starred: its row and bit, 0 = none */
static uint8_t *favorite_at(uint32_t e, uint32_t *k)
{
    return e < FAV_ROWS && *k < 256u ? favorites.factory[e] : 0;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    {
        uint8_t *r = engine < NENGINES ? favorite_at(engine, &preset) : 0;
        return r && ((r[preset / 8u] >> (preset % 8u)) & 1u);
    }
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    uint32_t k = preset;
    if (engine > NENGINES || (engine == NENGINES ? preset >= UP_SLOTS : !favorite_at(engine, &k)))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == NENGINES) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        uint8_t *b = &favorite_at(engine, &preset)[preset / 8u];
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}

/* MENU > DISPLAY > COLOR's 11th value, CUSTOM (1.6, #244, Discussion #239): a palette of the user's, five RGB565 colours (BG
 * SURF TEXT THEME ACCENT, 16 bits little endian) in favorites.factory[15][0..9], flags in [12] (bit 0: one is stored, bit 1:
 * it is the palette in use), set from the editor (PALETTE_SET, editor_palette.c) and saved with the settings. settings.palette
 * stays a built-in palette (the one in use before, or after): firmware before 1.6 shows that and keeps the bytes. The value
 * is in COLOR only while one is stored. Zero (every older setting) = none */
#define ui_cpal (&favorites.factory[15][0])
#define ui_cpal_flags (favorites.factory[15][12])
#define CPAL_STORED 1u
#define CPAL_ACTIVE 2u
static uint32_t cpal_on(void) { return (ui_cpal_flags & (CPAL_STORED | CPAL_ACTIVE)) == (CPAL_STORED | CPAL_ACTIVE); }
/* COLOR's values: 0 MACHINE (the palette follows the selected track's machine, with its trim: gfx.c UI_MACHINE_PALS,
 * ui_trim.c), 1 .. NPALETTES the fixed palettes, then CUSTOM while one is stored. settings.palette is a fixed palette's
 * index or UI_PAL_MACHINE; a fixed one pins the look (and there is no trim) */
#define COLOR_CUSTOM (NPALETTES + 1u)
static uint32_t palette_n(void) { return 1u + NPALETTES + ((ui_cpal_flags & CPAL_STORED) ? 1u : 0u); }
static uint32_t palette_cur(void)
{
    return cpal_on() ? COLOR_CUSTOM : settings.palette == UI_PAL_MACHINE ? 0u : 1u + settings.palette % NPALETTES;
}
static const char *palette_name(uint32_t v)
{
    return v == 0 ? "MACHINE" : v == COLOR_CUSTOM ? "CUSTOM" : UI_PALETTES[(v - 1u) % NPALETTES].name;
}
static uint8_t trim_machine = 0xFF;                    /* the machine the MACHINE palette was set for (palette_follow) */
/* the palette in use into gfx.c (the settings changed under it: a start, an import, a restore, a STYLE change, a new
 * machine on the selected track in MACHINE mode) */
static void palette_apply(void)
{
    if (cpal_on()) {
        palette_custom_load(ui_cpal);
        palette_set(NPALETTES);
    } else if (settings.palette == UI_PAL_MACHINE) {
        trim_machine = (uint8_t)(TSEL->eng_req % NENGINES);
        palette_set(PAL_MACHINE_ID(trim_machine));
    } else {
        palette_set(settings.palette % NPALETTES);
    }
}
/* MACHINE mode: another machine on the selected track (a track change, a machine change, a project or sound load) sets
 * its palette and trim; one full redraw, as a palette change in the menu */
static int palette_follow(void)
{
    if (cpal_on() || settings.palette != UI_PAL_MACHINE || trim_machine == (uint8_t)(TSEL->eng_req % NENGINES))
        return 0;
    palette_apply();
    return 1;
}

/* the five colours into the settings' bytes (stored; not yet the palette in use: palette_choose / palette_apply) */
static void cpal_store(const uint16_t *c)
{
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        ui_cpal[2u * i] = (uint8_t)c[i];
        ui_cpal[2u * i + 1u] = (uint8_t)(c[i] >> 8);
    }
    ui_cpal_flags |= CPAL_STORED;
}

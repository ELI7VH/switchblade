/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop: the set list, twelve songs (as the WaveLoop Melodee fork's living set list, resume_sounds.c song_step / song_dupe,
 * and the WaveLoop Kit's set list). A song is a whole project (FOR4, what a PROJECT slot holds: every track's sound, steps,
 * the song chain, the automation, the name) in its own storage object (storage.c OBJ_SONG0 + k, A/B at 0xA0000 + 8 KiB k),
 * beside the four PROJECT slots, which stay as they are.
 *   - The music is always one of the songs (sl_cur), unless the set list is full and the music is new (SL_NONE).
 *   - The song playing keeps itself: when the autosave writes the music (stopped, quiet, changed: project.c autosave_poll),
 *     the song's object gets the same bytes (sl_autosaved). RESTORE LAST OFF writes neither.
 *   - Power-on: the autosave brings the music back (RESTORE LAST), and with it the song it was (project_t.rsv, header byte
 *     67): sl_boot. Nothing restored (or an autosave of before the set list): the music is the first free song, unsaved
 *     until it changes (an empty song stays empty).
 *   - HOME, stopped (ui_input.c home_oct): OCT- / OCT+ the previous / next song that holds one (round the list); the song
 *     left is saved first when it changed (sl_keep), the one arrived at is the music, and the autosave holds it at once
 *     (so power-on comes back to it). OCT+ held: a copy of this song in the next free one (it is the music then, unnamed).
 *   - HOME's bottom line names it: SONG 3/12 and its name (ui_draw.c draw_foot).
 * The order is the slots' (rearranging them: later). Main loop only; every flash write while stopped (transport_busy) */
#define SL_NONE 0xFFu
static uint8_t sl_cur = SL_NONE;
static uint32_t sl_sig;                  /* autosave_sig() of the music as its song was loaded or saved last (0: unsaved) */
static uint16_t sl_used;                 /* bit k: song k holds a song (its object loads as a project) */

/* song k's object into proj_scratch (proj_wire: its bytes); 1 = a song */
static int sl_fetch(uint32_t k)
{
#if FELUCCA_FLASH
    int n;
    if (!flash_ok || k >= SL_N)
        return 0;
    proj_wire_gen++;
    n = st_load(OBJ_SONG0 + k, &proj_wire, sizeof proj_wire);
    return n > 0 && proj_import(&proj_scratch, &proj_wire, (int)n);
#else
    (void)k;
    return 0;
#endif
}
static void sl_scan(void)                /* which songs hold one */
{
    uint32_t k;
    sl_used = 0;
    for (k = 0; k < SL_N; k++)
        if (sl_fetch(k))
            sl_used |= (uint16_t)(1u << k);
}
static int sl_has(uint32_t k) { return k < SL_N && ((sl_used >> k) & 1u); }
static uint32_t sl_free_from(uint32_t k) /* the first free song from k on, round the list; SL_NONE none */
{
    uint32_t i;
    for (i = 0; i < SL_N; i++)
        if (!sl_has((k + i) % SL_N))
            return (k + i) % SL_N;
    return SL_NONE;
}

/* the music -> song k (the bytes the autosave would write); 0 written */
static int sl_write(uint32_t k)
{
#if FELUCCA_FLASH
    if (!flash_ok || k >= SL_N)
        return 1;
    project_capture(&proj_scratch);
    proj_scratch.rsv = (uint8_t)(k + 1u);
    proj_scratch.sum = proj_sum(&proj_scratch);
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_SONG0 + k, &proj_wire, sizeof proj_wire) != 0)
        return 1;
    sl_used |= (uint16_t)(1u << k);
    sl_sig = autosave_sig();
    return 0;
#else
    (void)k;
    return 1;
#endif
}
/* the autosave wrote the music (sig, its bytes in proj_wire, its song sl_cur): the song gets them too */
static void sl_autosaved(uint32_t sig)
{
#if FELUCCA_FLASH
    if (sl_cur >= SL_N || sig == sl_sig)
        return;
    if (st_save(OBJ_SONG0 + sl_cur, &proj_wire, sizeof proj_wire) == 0) {
        sl_used |= (uint16_t)(1u << sl_cur);
        sl_sig = sig;
    }
#else
    (void)sig;
#endif
}
/* the autosave holds the music now (a song stepped to: power-on comes back to it); RESTORE LAST OFF: none */
static int sl_autosave_now(void)
{
#if FELUCCA_FLASH
    if (!flash_ok) return 1;
    if (ui_prefs & PREF_RESTORE_OFF) return 0;
    project_capture(&proj_scratch);
    proj_wire_gen++;
    if (proj_pack(&proj_wire, &proj_scratch) && st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) == 0) {
        as.saved = as.seen = autosave_sig();
        as.last = as.t = fm1_ms;
        as.wrote = 1;
        as.err = 0;
        return 0;
    }
    as.err = 1;
#endif
    return 1;
}

/* power-on (project.c autosave_boot): song (1..SL_N) the restored music was, 0 none */
static void sl_boot(uint32_t song, int restored)
{
    sl_scan();
    sl_cur = SL_NONE;
    sl_sig = 0;
    if (song >= 1u && song <= SL_N) {
        sl_cur = (uint8_t)(song - 1u);
        if (sl_has(sl_cur))
            sl_sig = as.saved;                           /* (the autosave and the song hold the same: written together) */
        return;
    }
    sl_cur = (uint8_t)sl_free_from(0);                   /* new music, or an autosave of before the set list: a free song */
    if (!restored)
        sl_sig = autosave_sig();                         /* (the power-on sounds: kept only once they change) */
}

/* the song left: saved when it changed since it was loaded or saved (else its object holds it, or it never was one);
 * 0 = done */
static int sl_keep(void)
{
    if (sl_cur >= SL_N || autosave_sig() == sl_sig)
        return 0;
    if (sl_write(sl_cur)) {
        ui_message("SONG SAVE ERROR");
        return 1;
    }
    return 0;
}

/* "SONG 3/12" (and its name, if any) -> b (24 bytes); "SONG --/12" when the music is no song */
static void sl_line(char *b, const char **name)
{
    str_cpy(b, "SONG ", 24);
    if (sl_cur < SL_N) {
        fmt_int(b + 5, (int32_t)sl_cur + 1);
    } else {
        str_cpy(b + 5, "--", 4);
    }
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)SL_N);
    *name = proj_name;
}
static void sl_say(const char *what)                  /* the top bar: "SONG 3/12 NAME" (what: in the name's place) */
{
    char b[24], t[24];
    const char *nm;
    sl_line(b, &nm);
    t[0] = ' ';
    str_cpy(t + 1, what[0] ? what : nm, sizeof t - 1u);
    ui_say(b, t[1] ? t : "");
}

/* HOME, OCT- / OCT+ (ui_input.c home_oct): the previous / next song that holds one, round the list */
static void song_step(int dir)
{
    uint32_t i, k = SL_NONE, from;
    if (transport_busy()) {
        ui_message("STOP TO CHANGE SONG");
        return;
    }
    if (sl_keep())
        return;
    from = sl_cur < SL_N ? sl_cur : dir > 0 ? SL_N - 1u : 0u;
    for (i = 1; i <= SL_N; i++) {
        uint32_t j = dir > 0 ? (from + i) % SL_N : (from + SL_N - i % SL_N) % SL_N;
        if (sl_has(j) && j != sl_cur) {
            k = j;
            break;
        }
    }
    if (k >= SL_N) {
        ui_message(sl_used ? "NO OTHER SONG" : "NO SONGS YET");
        return;
    }
    if (!sl_fetch(k)) {
        sl_used &= (uint16_t)~(1u << k);
        ui_message("SONG LOAD ERROR");
        return;
    }
    proj_bound(&proj_scratch);
    if (project_restore_runtime(&proj_scratch))
        return;                                          /* (it said why) */
    sl_cur = (uint8_t)k;
    sl_sig = autosave_sig();
    sl_autosave_now();
    sl_say("");
    ui.force = 1;
}
/* HOME, OCT+ held: a copy of this song (tweaks, patterns, all of it) in the next free song, which is the music then,
 * unnamed; the song copied is kept first */
static void song_dupe(void)
{
    uint32_t k;
    if (transport_busy()) {
        ui_message("STOP TO CHANGE SONG");
        return;
    }
    if (sl_keep())
        return;
    if (sl_cur < SL_N && !sl_has(sl_cur) && sl_write(sl_cur)) {   /* (a new song never saved: it is one first) */
        ui_message("SONG SAVE ERROR");
        return;
    }
    k = sl_free_from(sl_cur < SL_N ? sl_cur + 1u : 0u);
    if (k >= SL_N) {
        ui_message("SET LIST FULL");
        return;
    }
    {
        char was[PROJ_NAME_LEN + 1u];
        str_cpy(was, proj_name, sizeof was);
        proj_name[0] = 0;                                /* (the copy unnamed: its number tells it apart) */
        if (sl_write(k)) {
            str_cpy(proj_name, was, sizeof proj_name);
            ui_message("SONG SAVE ERROR");
            return;
        }
    }
    sl_cur = (uint8_t)k;
    proj_cur = PROJ_NO_SLOT;                             /* (a song, no PROJECT slot's) */
    pat_from_set(0);
    sl_autosave_now();
    sl_say("COPY");
    ui.force = 1;
}

/* The current setlist song's name, not a project-slot rename. Keep the draft on
 * failure; commit the song first, then its startup snapshot. Main loop, quiet. */
static int song_name_save(const char *name)
{
    char was[PROJ_NAME_LEN + 1u]; uint8_t prior = sl_cur;
    if (!autosave_quiet()) return 1;
    if (sl_cur >= SL_N) sl_cur = (uint8_t)sl_free_from(0);
    if (sl_cur >= SL_N) { ui_message("SET LIST FULL"); return 1; }
    str_cpy(was,proj_name,sizeof was); str_cpy(proj_name,name,sizeof proj_name);
    if (sl_write(sl_cur)) {
        str_cpy(proj_name,was,sizeof proj_name); sl_cur = prior;
        ui_message("SONG SAVE ERROR"); return 1;
    }
    if (sl_autosave_now()) { ui_message("SONG RESUME ERROR"); return 1; }
    ui.force = 1;
    return 0;
}

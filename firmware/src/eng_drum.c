/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WaveLoop: DRUM, Switch Blade's fifth machine. A kit of eight drums, ported from the WaveLoop Kit (the Felucca fork,
 * branch codex/standalone-minilab-midi): its DRUM engine (eng_drum.c), the drum voices (drum_voice.c, the revoiced 808
 * and CR78 of 08913fe / b3e1f84, the eight Yama-bruh FM kits of bdba4a5: drum_yama.c, MIT) and every drum shaped on its
 * own (drum_control.c, 0bf4181), as the WaveLoop Melodee fork brought them in (fd4e003).
 *
 * Notes: a note plays its General MIDI drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other
 * notes as their octave of 36..47), so the Oxygen's pads (channel 4, GM notes from 36) play the kit. The FM-1's keys
 * play the GM map with the kick on the first C (seq.c kb_map: drum_key; OCT -/+ move it).
 * The eight drums (lanes): KICK SNARE CLAP HATCL HATOP TOM RIM BELL; a kit may play another piece in a lane (CR78's
 * maracas on CLAP, congas, claves, a cymbal: drum_lane_name). A lane is mono: a hit takes over its lane's voice, the
 * eight lanes ring together; a closed hat chokes the open one. The voices come from the shared budget (voice.c), up to
 * DV_NLANE on the track: drums are released at once (a hit ends itself, a key-up does nothing), so a synth's new note
 * takes a ringing drum's voice before any held note's.
 *
 * KIT (E0): 808 CR78 909, the Yama-bruh kits (YB ...), Felucca's own (STD) and the model kits 10 66 55 77. Each drum has
 * its own PITCH (+-12 semitones), TONE, DECAY, SNAP (the drum's extra: the snare's snappiness, the hats' sizzle, the
 * toms' bend, the clap's spread, ...), LEVEL (100: as designed), PAN and SEND (its reverb, on top of the track's REV).
 * They are E1..E7: the track's p[P_E1..P_E7] always hold the PICKED drum's values (drum_st.focus), so every page,
 * knob, MIDI CC, lock and the knob card shape the picked drum with no code of their own. A live hit (a key, a pad, a
 * MIDI note; never the sequencer) picks its drum (drum_note_on -> drum_hit, the main loop: drum_pick); the values of
 * the drum let go are kept, its own where they differ from the kit's (drum_st.mask: stored only when edited), and the
 * new drum's come into p[]. A sound load gives every drum the kit's values (drum_track_loaded).
 *
 * PAN and SEND: a drum with a PAN or SEND of its own puts that part of its sound beside the kit's sum (drum_render:
 * drum_side, drum_rs), and fx.c drum_part_fx (engine_t.part_fx) places it: the side as mix_stereo's (L - R) / 2 with the
 * track's PAN law, the send into the reverb bus at the track's LEVEL. The kit's sum (track_render's block) goes through
 * DIST, INSERT and the SLICER as on every machine; a drum's pan and its own send pass them by (as SPREAD's side does).
 * With no PAN or SEND anywhere the kit is one bus, the same code as before.
 *
 * What applies: the hit is the envelope (engine_t.ownenv: drum_amp is the velocity, drum_done ends the voice when its
 * drum has rung out or another voice took its lane). VOICE, GLIDE, the ADSR, ENV DEST, LFO DEST PIT / FLT / SHP and the
 * matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the sends, DIST, INSERT, the SLICER, LFO DEST AMP and velocity do.
 * The ARP does not play a DRUM track (seq.c events_block: ARP is off there).
 *
 * State: the eight lanes of a part in its engine memory (engines.c eng_mem, DRUM_MEM); the per-drum values in drum_st
 * (the project and the autosave keep them: project.c drum_pack).
 *
 * KIT 909 (DK_909): the 9W9 TR-909 voices (x0x/drum909.c: Charles Vestal's fm1-x0x 201e5c5, GPL-3.0; 9W9 by
 * athousanddetails, after ER-99 by Matthew Cieplak), as the WaveLoop Melodee fork carries them: circuit-modelled BD SD
 * LT MT HT RS CP, synthesized CH OH CR RD (oscillators and noise, no sample ROM). Float (the FPU, -mfprev1). Eleven
 * drums on the eight lanes: a note strikes its 909 drum (drum909_ins: the toms by the note, 49 52 55 57 the crash, 51
 * 53 56 59 the ride, on the BELL lane, named RIDE), each drum a voice of its own (d9_part_t.owner: the three toms, the
 * ride and the crash ring together), a closed hat chokes the open one (drum909_trigger). A lane's PITCH TONE DECAY SNAP
 * set its drums' 909 parameters at the hit (d9_values); LEVEL, PAN and SEND act on the sound as for every kit. The
 * part's 909 noise is drawn once a block (d9_voice_render: the first 909 voice that renders; drum_block_end clears it). */
#include "drum_voice.c"
#include "x0x/drum909.c"                         /* KIT 909 (GPL-3.0: keep the notices in x0x/) */

/* KIT values (E0), the order the knob shows them; each the Kit's model kit (DV_KIT: 0 80, 1 10, 2 66, 3 55, 4 77,
 * 5 CR78), Yama-bruh bank (0..7) or Felucca's own (STD) */
enum { DK_808, DK_CR78, DK_909, DK_YSTD, DK_YELEC, DK_YPOWER, DK_YBRUSH, DK_YORCH, DK_YSYNTH, DK_YLATIN, DK_YLOFI,
       DK_STD, DK_10, DK_66, DK_55, DK_77, DK_COUNT };   /* (909 came in after CR78: drum_unpack's version 1 moves the
                                                         * kits saved before it, project.c) */
static const char *const N_DRUM_KIT[DK_COUNT] = {"808", "CR78", "909", "YB STD", "YB ELEC", "YB PWR", "YB BRSH",
                                                 "YB ORCH", "YB SYN", "YB LAT", "YB LOFI", "STD", "10", "66", "55", "77"};
#define DKP_STD 0u                               /* drum_kit_of: Felucca's own */
#define DKP_MODEL 0x10u                          /* + the DV_KIT index */
#define DKP_YAMA 0x20u                           /* + the Yama-bruh bank */
#define DKP_909 0x30u                            /* the 909 (x0x/drum909.c): no lane voices */
static const uint8_t DK_PLAYS[DK_COUNT] = {
    DKP_MODEL + 0, DKP_MODEL + 5, DKP_909, DKP_YAMA + 0, DKP_YAMA + 1, DKP_YAMA + 2, DKP_YAMA + 3, DKP_YAMA + 4, DKP_YAMA + 5,
    DKP_YAMA + 6, DKP_YAMA + 7, DKP_STD, DKP_MODEL + 1, DKP_MODEL + 2, DKP_MODEL + 3, DKP_MODEL + 4,
};
static uint32_t drum_kit_plays(int32_t v) { return DK_PLAYS[v >= 0 && v < DK_COUNT ? v : 0]; }
static int drum_is909(const struct track *t) { return t->p[P_E0] == DK_909; }
/* the model kits' pieces where a lane plays another than its own (DV_KIT order): 1 TOM -> CONGA, 2 RIM -> CLAVE,
 * 4 BELL -> CYM, 8 CLAP -> MARACAS */
static const uint8_t DK_SWAP[DV_NKIT] = {0, 4, 1, 0, 2 | 4, 1 | 2 | 8};
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};

/* General MIDI notes 35..81 -> the drum (DVT_*) and semitones from its designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
static uint32_t drum_gm_note(uint32_t note) { return note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u; }
/* the drum a note strikes on kit kv (DVT_*, or a model kit's / Yama-bruh bank's lane: DV_KTYPE, DV_YTYPE) and its semitones */
static uint32_t drum_gm(int32_t kv, uint32_t note, int32_t *st)
{
    uint32_t n = drum_gm_note(note), t = (uint32_t)DRUM_GM[n - 35u][0], kit = drum_kit_plays(kv);
    *st = DRUM_GM[n - 35u][1];
    if (kit >= DKP_YAMA)
        return DV_YTYPE(kit - DKP_YAMA, t == DVT_CYM ? DV_YCYM : DV_TYPE_LANE[t]);
    if (kit >= DKP_MODEL)
        return DV_KTYPE(kit - DKP_MODEL + 1u, DV_TYPE_LANE[t]);
    return t;                                            /* (STD: the kick is PUNCH) */
}
/* the lane a note strikes (whatever the kit: a kit plays its own piece inside the lane) */
static uint32_t drum_lane(uint32_t note) { return DV_TYPE_LANE[DRUM_GM[drum_gm_note(note) - 35u][0]]; }
/* the GM note of each lane's own drum (the knob card's, the help's) */
static const uint8_t DRUM_LANE_NOTE[DV_NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the FM-1's key k (0 = F3) on a DRUM track: the GM map, the first C (key 7) the kick (36) at OCT 0 */
static __attribute__((noinline)) uint32_t drum_key(uint32_t k, int32_t oct) { return (uint32_t)clamp(29 + 12 * oct + (int32_t)k, 0, 127); }

/* the lane's name as the track's KIT plays it (5 characters at most) */
static uint32_t drum_swaps(int32_t kv)
{
    uint32_t kit = drum_kit_plays(kv);
    return kit >= DKP_MODEL && kit < DKP_YAMA ? DK_SWAP[kit - DKP_MODEL] : 0u;
}
static const char *drum_lane_name(const struct track *t, uint32_t l)
{
    static const char *const N[DV_NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    uint32_t s = drum_swaps(t->p[P_E0]);
    l &= DV_NLANE - 1u;
    if (l == DV_CLAP && (s & 8u))
        return "MARAC";
    if (l == DV_TOM && (s & 1u))
        return "CONGA";
    if (l == DV_RIM && (s & 2u))
        return "CLAVE";
    if (l == DV_BELL && drum_is909(t))
        return "RIDE";                                   /* (the 909's ride and crash: drum909_ins) */
    return (s & 4u) && l == DV_BELL ? "CYM" : N[l];
}

/* ------------------------------------------------------------ each drum its own --- */
/* the per-drum controls: E1..E7 (PITCH TONE DECAY SNAP LEVEL PAN SEND), DC_N of them */
#define DC_N 7u
enum { DC_PITCH, DC_TONE, DC_DECAY, DC_SNAP, DC_LEVEL, DC_PAN, DC_SEND };
typedef struct {
    int8_t kit[DC_N];                    /* the kit's values: every drum without its own (the sound loaded) */
    uint8_t set;                         /* kit[] holds them (0: never loaded: every drum the picked one's, p[]) */
    uint8_t focus;                       /* the picked drum (lane): its values are in p[P_E1..P_E7] */
    uint8_t mask[DV_NLANE];              /* bit c: drum l has its own value c, v[l][c] (not the picked one's: in p[]) */
    int8_t v[DV_NLANE][DC_N];
} drum_st_t;
static drum_st_t drum_st[NTRK];
static volatile uint8_t drum_hit[NTRK];  /* the audio ISR: a live hit's lane + 1 (the main loop picks it: drum_pick) */
static uint8_t drum_seq;                 /* seq.c seq_step: the sequencer plays (its hits pick nothing) */

/* drum l's value c on track t (the picked drum: p[], which the matrix and the locks move too) */
static int32_t drum_val(const struct track *t, uint32_t l, uint32_t c)
{
    const drum_st_t *d = &drum_st[(uint32_t)(t - trk) % NTRK];
    if (l == d->focus || !d->set)
        return t->p[P_E1 + c];
    return (d->mask[l] >> c) & 1u ? d->v[l][c] : d->kit[c];
}
/* every drum to the kit's values (p[P_E1..P_E7], the sound just loaded); the picked drum stays picked */
static void drum_track_loaded(struct track *t)
{
    drum_st_t *d = &drum_st[(uint32_t)(t - trk) % NTRK];
    uint32_t c;
    for (c = 0; c < DC_N; c++)
        d->kit[c] = (int8_t)t->p[P_E1 + c];
    d->set = 1;
    memset(d->mask, 0, sizeof d->mask);
    memset(d->v, 0, sizeof d->v);
    d->focus &= DV_NLANE - 1u;
}
/* the picked drum's values (p[]) into its own (where they differ from the kit's), then lane l's into p[]: l is picked.
 * The main loop, the audio ISR off (it reads both; a MIDI CC writes p[] there) */
static void drum_focus_set(struct track *t, uint32_t l)
{
    drum_st_t *d = &drum_st[(uint32_t)(t - trk) % NTRK];
    uint32_t c, f;
    if (!d->set)
        drum_track_loaded(t);
    f = d->focus & (DV_NLANE - 1u);
    l &= DV_NLANE - 1u;
    for (c = 0; c < DC_N; c++) {
        int8_t x = (int8_t)t->p[P_E1 + c];
        d->v[f][c] = x != d->kit[c] ? x : 0;
        d->mask[f] = (uint8_t)(x != d->kit[c] ? d->mask[f] | 1u << c : d->mask[f] & ~(1u << c));
    }
    d->focus = (uint8_t)l;
    for (c = 0; c < DC_N; c++)
        t->p[P_E1 + c] = (int16_t)((d->mask[l] >> c) & 1u ? d->v[l][c] : d->kit[c]);
    d->mask[l] = 0;                                      /* (the picked drum's own values are p[]) */
}
/* the state of track k to keep (project.c, the undo copy), the picked drum's values (p: its track's P_*) folded in, as
 * drum_focus_set leaves them */
static void drum_state_fold(uint32_t k, const int16_t *p, drum_st_t *out)
{
    const drum_st_t *d = &drum_st[k % NTRK];
    uint32_t c, f = d->focus & (DV_NLANE - 1u);
    *out = *d;
    for (c = 0; c < DC_N; c++) {
        int8_t x = (int8_t)p[P_E1 + c];
        out->v[f][c] = x != d->kit[c] ? x : 0;
        out->mask[f] = (uint8_t)(x != d->kit[c] ? out->mask[f] | 1u << c : out->mask[f] & ~(1u << c));
    }
}
static void drum_state_of(const struct track *t, drum_st_t *out) { drum_state_fold((uint32_t)(t - trk), t->p, out); }
/* .. and back (its picked drum's values come from p[], which the same copy holds) */
static void drum_state_set(struct track *t, const drum_st_t *in)
{
    drum_st_t *d = &drum_st[(uint32_t)(t - trk) % NTRK];
    *d = *in;
    d->focus &= DV_NLANE - 1u;
    d->mask[d->focus] = 0;
}

/* stored with a DRUM track (project.c, in its ARP pattern's place: a DRUM track plays no ARP): 'D', version 2, then the
 * state; DRUM_PACKED bytes. Version 1: saved before KIT 909 came in after CR78 (the kits from YB STD on one lower:
 * project.c moves them, drum_kit_v1) */
#define DRUM_MAGIC 'D'
#define DRUM_PACKED (2u + 1u + DC_N + DV_NLANE + DV_NLANE * DC_N)
static void drum_pack(const drum_st_t *d, uint8_t *b)
{
    uint32_t l, c, k = 0;
    b[k++] = DRUM_MAGIC;
    b[k++] = 2u;
    b[k++] = d->focus;
    for (c = 0; c < DC_N; c++)
        b[k++] = (uint8_t)d->kit[c];
    for (l = 0; l < DV_NLANE; l++)
        b[k++] = d->mask[l];
    for (l = 0; l < DV_NLANE; l++)
        for (c = 0; c < DC_N; c++)
            b[k++] = (uint8_t)d->v[l][c];
}
/* b is a stored state (into d): its version (1, 2), else 0 */
static int drum_unpack(drum_st_t *d, const uint8_t *b)
{
    uint32_t l, c, k = 3;
    if (b[0] != DRUM_MAGIC || b[1] < 1u || b[1] > 2u || b[2] >= DV_NLANE)
        return 0;
    d->focus = b[2];
    d->set = 1;
    for (c = 0; c < DC_N; c++)
        d->kit[c] = (int8_t)b[k++];
    for (l = 0; l < DV_NLANE; l++)
        d->mask[l] = (uint8_t)(b[k++] & ((1u << DC_N) - 1u));
    for (l = 0; l < DV_NLANE; l++)
        for (c = 0; c < DC_N; c++)
            d->v[l][c] = (int8_t)b[k++];
    d->mask[d->focus] = 0;
    return b[1];
}
/* a DRUM track of a version-1 state (saved before KIT 909): its kit and its sound's index as they are now */
static void drum_kit_v1(struct track *t)
{
    if (t->p[P_E0] >= DK_909 && t->p[P_E0] < DK_COUNT - 1)
        t->p[P_E0]++;
    if (t->preset >= DK_909 && t->preset < DK_COUNT - 1)
        t->preset++;
}

/* ------------------------------------------------------------------ the voices --- */
typedef struct {
    dv_param_t key;              /* the parameters the coefficients were set up with */
    dv_coef_t c;
    dv_voice_t v;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (its type) */
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
    uint8_t pad;
} drum_lane_t;
/* the metal source (six squares: the hats, the cymbal) of the lanes whose drums use it on some kit: CLAP (CR78's
 * maracas), HATCL, HATOP, BELL (a cymbal); -1 none (such a drum there would be silent: no kit puts one there) */
#define DRUM_NMB 4u
static const int8_t DRUM_MB[DV_NLANE] = {-1, -1, 0, 1, 2, -1, -1, 3};
/* KIT 909: the part's 909 (its eleven drums), each drum's voice and this block's noise */
typedef struct {
    drum909_t d;
    float *nz;                   /* this block's noise (d9_noise_block), 0: none drawn yet (drum_block_end) */
    uint8_t owner[D9_NUM];       /* the voice playing each 909 drum: index + 1, 0 = none */
    uint8_t ready;               /* d is set up (drum909_init; the region starts zeroed: eng_mem_claim) */
} d9_part_t;
typedef struct {
    drum_lane_t lane[DV_NLANE];  /* (first: drum_mb_of) */
    dv_metal_t mb[DRUM_NMB];
    d9_part_t d9;                /* (its own room beside the lanes: a kit switched, or locked per step, between the 909
                                  * and another lets both ring out) */
} drum_part_t;
#define DRUM_MEM (sizeof(drum_part_t))           /* the part's engine memory: its eight lanes and the 909 (the largest) */
static d9_part_t *d9_of(const struct track *t)
{
    uint32_t p = (uint32_t)(t - trk);
    d9_part_t *k;
    if (p >= NPART)
        return 0;
    k = &((drum_part_t *)ENG_MEM(p))->d9;
    if (!k->ready) {
        drum909_init(&k->d);
        k->ready = 1;
    }
    return k;
}
/* the 909 drum a note strikes (Melodee's drum909_ins): 49 52 55 57 the crash, 51 53 56 59 the ride; by its lane else, the
 * toms by the note (below 45 LT, below 48 MT, HT) */
static uint32_t drum909_ins(uint32_t note)
{
    if (note == 49u || note == 52u || note == 55u || note == 57u)
        return D9_CR;
    if (note == 51u || note == 53u || note == 59u || note == 56u)
        return D9_RD;
    switch (drum_lane(note)) {
    case DV_KICK: return D9_BD;
    case DV_SNARE: return D9_SD;
    case DV_CLAP: return D9_CP;
    case DV_HATC: return D9_CH;
    case DV_HATO: return D9_OH;
    case DV_RIM: return D9_RS;
    default: return note < 45u ? D9_LT : note < 48u ? D9_MT : D9_HT;
    }
}
/* 909 drum ins still sounding */
static int drum909_live(const d9_part_t *k, uint32_t ins)
{
    if (ins <= D9_SD)
        return k->d.bt[ins].mute > 0;
    if (ins <= D9_HT)
        return k->d.tom[ins - D9_LT].mute > 0;
    if (ins == D9_RS)
        return k->d.rim.mute > 0;
    if (ins == D9_CP)
        return k->d.clap.mute > 0;
    return k->d.smp[ins - D9_CH].mute > 0;
}
/* the end of a DRUM part's block (fx.c drum_part_fx): the next block draws its own noise */
static void drum_block_end(const struct track *t)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p < NPART)
        ((drum_part_t *)ENG_MEM(p))->d9.nz = 0;
}
static drum_lane_t *drum_kit_of(const struct track *t)
{
    uint32_t p = (uint32_t)(t - trk);
    return p < NPART ? ((drum_part_t *)ENG_MEM(p))->lane : 0;
}
static dv_metal_t *drum_mb_of(drum_lane_t *L, uint32_t lane)
{
    return DRUM_MB[lane & (DV_NLANE - 1u)] < 0 ? 0 : &((drum_part_t *)(void *)(L - lane))->mb[DRUM_MB[lane & (DV_NLANE - 1u)]];
}
/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(const struct track *t, const voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* KIT 909: drum ins's parameters for this hit from its lane's values (Melodee's drum909_values, on Switch Blade's
 * per-drum controls), then the hit. Each 909 parameter starts at its 9W9 default (the panel's pots); PITCH moves the
 * drum's tuning (+-12 semitones: the kick's sweep, the snare's two shells, the toms, the rim's and clap's filters, the
 * metal's oscillators); DECAY its decays (x2 per 24 steps from 64); TONE (from 64) the kick's click and the toms' stick
 * (ATTACK), the snare's noise (TONE), the rim's, clap's and metal's TUNE pot (their colour); SNAP (from 64) the snare's
 * SNAPPY, the kick's pitch sweep (P.DPTH, above 64), the toms' bend, the clap's spread, the rim's noise, the metal's
 * DRIVE (its sizzle). Velocity: the 909's own (its accent 2, velocity depth 1); LEVEL, PAN, SEND: d9_voice_render */
static __attribute__((noinline)) void d9_values(const struct track *t, d9_part_t *k, uint32_t ins, uint32_t lane,
                                                uint32_t vel)
{
    drum909_t *d = &k->d;
    int32_t tone = drum_val(t, lane, DC_TONE) - 64, snap = drum_val(t, lane, DC_SNAP) - 64, j;
    float ratio = fm_exp2f((float)clamp(drum_val(t, lane, DC_PITCH), -12, 12) * (1.0f / 12.0f));
    float decay = fm_exp2f((float)(drum_val(t, lane, DC_DECAY) - 64) * (1.0f / 24.0f));
    for (j = 0; j < d9_nspec[ins]; j++) {
        const d9_pspec_t *sp = &d9_specs[ins][j];
        int32_t value = sp->ui.def;
        if (sp->field == D9_F_ATTACK || sp->field == D9_F_TONE || (ins >= D9_RS && sp->field == D9_F_TUNE))
            value += tone;
        if (sp->field == D9_F_SNAPPY || sp->field == D9_F_PDEPTH || (ins >= D9_CH && sp->field == D9_F_DRIVE))
            value += snap;
        d9_set_panel(d, (int)ins, (int)j, value, 1);     /* (deferred: the filters and shapes once, below) */
    }
    if (ins <= D9_HT) {
        static const float DEC[5] = {1380.0f, 320.0f, 1700.0f, 1050.0f, 1100.0f};
        d->bt[ins].decay = DEC[ins] * decay;
        if (ins == D9_SD) {
            d->bt[ins].tune *= ratio;
            d->bt[ins].tune2 = 325.0f * ratio;
            d->bt[ins].noise_decay = 1200.0f * fm_exp2f((float)tone * (1.0f / 24.0f)) * decay;
        } else if (ins != D9_BD) {
            d->bt[ins].tune *= ratio;
            d->bt[ins].sweep_depth = 1.0f + 0.12f * fm_exp2f((float)snap * (1.0f / 32.0f));   /* the toms' bend */
        }
        d9_shape_prep(&d->bt[ins].shape, d->bt[ins].drive, d->bt[ins].dist_type);
    } else if (ins == D9_RS) {
        d->rim.tune *= ratio;
        d9_rim_retune(&d->rim);
        d9_shape_prep(&d->rim.shape, d->rim.drive, d->rim.dist_type);
    } else if (ins == D9_CP) {
        d->clap.tune *= ratio;
        d->clap.tail_decay *= decay;
        d->clap.spread = 12.0f * fm_exp2f((float)snap * (1.0f / 48.0f));
        d9_clap_retune(&d->clap);
        d9_shape_prep(&d->clap.shape, d->clap.drive, d->clap.dist_type);
    } else {
        d9_smp_t *m = &d->smp[ins - D9_CH];
        m->pitch *= ratio;
        m->decay *= decay;
        d9_smp_rate(m);
        d9_shape_prep(&m->shape, m->drive, m->dist_type);
    }
    drum909_trigger(d, (int)ins, (float)vel * (1.0f / 127.0f));
    if (ins == D9_SD) {                                  /* (the trigger pins the snare's decay and the rim's: theirs again) */
        d->bt[1].decay = 340.0f * decay;
        d9_env_exp(&d->bt[1].amp, 0.00001f, d->bt[1].decay * D9_MS);
        d->bt[1].mute = d9_ceil((d->bt[1].noise_decay + d->bt[1].decay + 60.0f) * D9_MS);
    } else if (ins == D9_RS) {
        d->rim.decay = 200.0f * decay;
        d->rim.noise_mix = 0.35f * fm_exp2f((float)snap * (1.0f / 32.0f));
        d9_env_exp(&d->rim.amp, 0.00001f, d->rim.decay * D9_MS);
        d->rim.mute = d9_ceil((d->rim.decay + 20.0f) * D9_MS);
    } else if (ins == D9_BD) {
        d->bt[0].bd_base *= ratio;
        d->bt[0].bd_df *= ratio;
        d->bt[0].pitch.v *= ratio;
    }
}
static void d9_note_on(struct track *t, voice_t *v, uint32_t lane)
{
    d9_part_t *k = d9_of(t);
    uint32_t i = (uint32_t)(v - t->v), ins = drum909_ins(v->note), j;
    if (!k)
        return;
    for (j = 0; j < D9_NUM; j++)                         /* this voice played another 909 drum: it stops there */
        if (k->owner[j] == i + 1u)
            k->owner[j] = 0;
    k->owner[ins] = (uint8_t)(i + 1u);                   /* (its last voice, if another, ends: drum_done) */
    v->s[0] = (int32_t)lane;
    v->s[1] = (int32_t)ins + 1;                          /* a 909 voice (0: a lane's) */
    v->env_out = 32767;                                  /* the hit starts at its level (the velocity is the 909's) */
    d9_values(t, k, ins, lane, v->vel);
}

static void drum_note_on(struct track *t, voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    if (drum_is909(t)) {                                 /* KIT 909: its own drums */
        d9_note_on(t, v, lane);
        if (!drum_seq)
            drum_hit[(uint32_t)(t - trk) % NTRK] = (uint8_t)(lane + 1u);
        return;
    }
    v->s[1] = 0;
    role = drum_gm(t->p[P_E0], v->note, &st);
    L = &K[lane];
    if (L->role != role || L->v.type != dv_run_type(role)) {   /* another drum on this lane: from rest */
        dv_init(&L->v, role);
        L->key.type = 0xFF;
        L->role = (uint8_t)role;
    }
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_done) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    dv_trigger(&L->v);
    if (lane == DV_HATC && K[DV_HATO].v.live)            /* a closed hat chokes the open one */
        dv_choke(&K[DV_HATO].v);
    if (!drum_seq)                                       /* a live hit picks its drum (the main loop: drum_pick) */
        drum_hit[(uint32_t)(t - trk) % NTRK] = (uint8_t)(lane + 1u);
}
/* the hit is the envelope: the velocity (the accent shapes the drum itself: drum_render), as the Kit's ADSR held at full
 * scaled it (mulq15(32767, vel 258): the Kit's level to the bit) */
static int32_t drum_amp(const struct track *t, const voice_t *v)
{
    (void)t;
    return v->s[1] ? 32767 : mulq15(32767, v->vel * 258);   /* (KIT 909: the velocity is in the 909's hit) */
}
/* the voice is free once its drum has rung out (-84 dB; the 909's: its mute count, -100 dB), or another voice took its
 * lane (its 909 drum) */
static int drum_done(const struct track *t, const voice_t *v)
{
    const drum_lane_t *L;
    if (v->s[1]) {
        uint32_t p = (uint32_t)(t - trk), ins = (uint32_t)v->s[1] - 1u;
        const d9_part_t *k = p < NPART ? &((const drum_part_t *)ENG_MEM(p))->d9 : 0;
        return !k || !k->ready || ins >= D9_NUM || k->owner[ins] != (uint32_t)(v - t->v) + 1u || !drum909_live(k, ins);
    }
    L = drum_lane_of(t, v);
    return !L || (!L->v.live && !L->v.trig);
}

/* a drum's PAN and SEND (drum_render) for fx.c drum_part_fx: the side (L - R) / 2 of the drums with a PAN, their sends,
 * this block; drum_bus bit 0 / 1 when either holds something (the part rendering now: one at a time) */
static int32_t drum_side[CTL], drum_rs[CTL];
static uint8_t drum_bus;
static __attribute__((noinline)) void drum_place(const int32_t *o, uint32_t n, int32_t pan, int32_t snd)
{
    uint32_t i;
    if (pan) {                                           /* the track's PAN law: the far side down, the near one as it is */
        int32_t gs = -pan * 32;                          /* (L - R) / 2 per unit, Q12 */
        if (!(drum_bus & 1u))
            for (i = 0; i < CTL; i++)
                drum_side[i] = 0;
        drum_bus |= 1u;
        for (i = 0; i < n; i++)
            drum_side[i] += (o[i] * gs) >> 12;
    }
    if (snd) {
        int32_t g = snd * 258;
        if (!(drum_bus & 2u))
            for (i = 0; i < CTL; i++)
                drum_rs[i] = 0;
        drum_bus |= 2u;
        for (i = 0; i < n; i++)
            drum_rs[i] += mulq15(o[i], g);
    }
}

/* KIT 909: one block of the voice's 909 drum (drum909_render_voice: float), at its lane's LEVEL (100: 9W9's level), into
 * the 32-bit path every drum takes (the knee, the voice's ramp, PAN and SEND as drum_render's) */
static __attribute__((noinline)) void d9_voice_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t p = (uint32_t)(t - trk), ins = (uint32_t)v->s[1] - 1u, lane = (uint32_t)v->s[0] & (DV_NLANE - 1u), i;
    d9_part_t *k = p < NPART ? &((drum_part_t *)ENG_MEM(p))->d9 : 0;
    float f[CTL], g;
    int32_t y[CTL], pan, snd;
    if (!k || !k->ready || ins >= D9_NUM || k->owner[ins] != (uint32_t)(v - t->v) + 1u)
        return;
    if (n > CTL)
        n = CTL;
    if (!k->nz)                                          /* the part's noise, once a block (the first 909 voice) */
        k->nz = d9_noise_block(&k->d, CTL);
    for (i = 0; i < n; i++)
        f[i] = 0.0f;
    drum909_render_voice(&k->d, (int)ins, k->nz, f, (int)n);
    g = (float)clamp(drum_val(t, lane, DC_LEVEL), 0, 127) * 120.0f;   /* (Melodee's 12000 at LEVEL 100) */
    for (i = 0; i < n; i++)
        y[i] = (int32_t)(fm_clampf(f[i], -2.0f, 2.0f) * g);
    pan = drum_val(t, lane, DC_PAN);
    snd = drum_val(t, lane, DC_SEND);
    if (pan) {
        int32_t gm = 4096 - (pan < 0 ? -pan : pan) * 32;
        for (i = 0; i < n; i++) {
            int32_t s = y[i];
            y[i] = voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
            out[i] += (y[i] * gm) >> 12;
        }
    } else {
        for (i = 0; i < n; i++) {
            int32_t s = y[i];
            y[i] = voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
            out[i] += y[i];
        }
    }
    if (pan | snd)
        drum_place(y, n, pan, snd);
}

static __attribute__((noinline)) void drum_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    drum_lane_t *L = drum_lane_of(t, v);
    dv_param_t k;
    dv_metal_t *M;
    int32_t y[CTL], mb[CTL], acc, pan, snd;
    uint32_t i, r, lane;
    if (!L)
        return;
    if (n > CTL)
        n = CTL;
    lane = (uint32_t)v->s[0] & (DV_NLANE - 1u);
    M = drum_mb_of(L, lane);
    acc = clamp(100 * v->vel * 4, 0, 65536);             /* the accent at full velocity (the Kit's ACC 100), Q16 */
    r = L->role;
    dv_default(&k, r);                                   /* the drum's values move it from its design (64) */
    k.decay = (uint8_t)clamp(k.decay + drum_val(t, lane, DC_DECAY) - 64, 0, 127);
    k.tone = (uint8_t)clamp(k.tone + drum_val(t, lane, DC_TONE) - 64, 0, 127);
    k.extra = (uint8_t)clamp(k.extra + drum_val(t, lane, DC_SNAP) - 64, 0, 127);
    k.level = (uint8_t)clamp(drum_val(t, lane, DC_LEVEL), 0, 127);
    k.accent = (uint8_t)clamp(acc >> 9, 0, 127);
    k.tune = (int16_t)(L->st * 16 + clamp(drum_val(t, lane, DC_PITCH), -12, 12) * 16);
    if (((const uint32_t *)&k)[0] != ((const uint32_t *)&L->key)[0] ||
        ((const uint32_t *)&k)[1] != ((const uint32_t *)&L->key)[1]) {
        dv_setup(&L->c, &k);                             /* (a value moved) */
        L->key = k;
        if (M && dv_uses_metal(L->c.type))
            dv_metal_tune(M, &L->c);
    }
    if (M && dv_uses_metal(L->c.type) && (L->v.live || L->v.trig))
        dv_metal_run(M, mb, n);
    dv_run(&L->c, &L->v, M ? mb : 0, y, n);
    pan = drum_val(t, lane, DC_PAN);
    snd = drum_val(t, lane, DC_SEND);
    if (pan) {                                           /* the kit's sum gets its mid ((L + R) / 2), drum_place its side */
        int32_t gm = 4096 - (pan < 0 ? -pan : pan) * 32;
        for (i = 0; i < n; i++) {
            int32_t s = y[i];
            y[i] = voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
            out[i] += (y[i] * gm) >> 12;
        }
    } else {
        for (i = 0; i < n; i++) {                        /* x1.25 and the knee below, in 32 bits */
            int32_t s = y[i];
            y[i] = voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
            out[i] += y[i];
        }
    }
    if (pan | snd)
        drum_place(y, n, pan, snd);
}
/* engine_t.render: a lane's drum (drum_render, as before KIT 909) or a 909 drum (d9_voice_render) */
static void drum_render_voice(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    if (v->s[1])
        d9_voice_render(t, v, out, n, m);
    else
        drum_render(t, v, out, n, m);
}

/* {KIT, PITCH, TONE, DECAY, SNAP, LEVEL, PAN, SEND}; every kit suggests the BEAT pattern (GM notes) */
#define DRUM_E(kit) {(kit), 0, 64, 64, 64, 100, 0, 0}
static const preset_t DRUM_PRESETS[] = {
    {"808", DRUM_E(DK_808), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},   /* (a new project's T4: engines.c TRK_DEF) */
    {"CR78", DRUM_E(DK_CR78), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"909", DRUM_E(DK_909), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},   /* (x0x/drum909.c: synthesized, float) */
    {"YB STANDARD", DRUM_E(DK_YSTD), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB ELECTRO", DRUM_E(DK_YELEC), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB POWER", DRUM_E(DK_YPOWER), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB BRUSH", DRUM_E(DK_YBRUSH), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB ORCH", DRUM_E(DK_YORCH), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB SYNTH", DRUM_E(DK_YSYNTH), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB LATIN", DRUM_E(DK_YLATIN), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"YB LOFI", DRUM_E(DK_YLOFI), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"FELUCCA KIT", DRUM_E(DK_STD), {0, 100, 127, 100}, 0, FX(0, 0, 0, 20), PAT(10)},
    {"KIT 10", DRUM_E(DK_10), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"KIT 66", DRUM_E(DK_66), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"KIT 55", DRUM_E(DK_55), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
    {"KIT 77", DRUM_E(DK_77), {0, 100, 127, 100}, 0, FX(0, 0, 0, 0), PAT(10)},
};
#undef DRUM_E

static int drum_part_fx(struct track *t, int32_t *b, int32_t *z, uint32_t n);   /* (fx.c: the mix's buses) */
static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "MIX"},
    .edit = {
        {"KIT", F_ENUM, 0, DK_COUNT - 1, DK_808, N_DRUM_KIT, 0},
        {"PITCH", F_SEMI, -12, 12, 0, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"LEVEL", F_PCT, 0, 127, 100, 0, 0},
        {"PAN", F_BIPCT, -64, 63, 0, 0, 0},
        {"SEND", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .knob = {P_E1, P_E2, P_E3, P_E4},                    /* HOME: PITCH TONE DECAY SNAP of the picked drum */
    .poly = DV_NLANE,
    .note_on = drum_note_on,
    .render = drum_render_voice,
    .mem = DRUM_MEM,
    .ownenv = 1,
    .amp = drum_amp,
    .done = drum_done,
    .part_fx = drum_part_fx,
};

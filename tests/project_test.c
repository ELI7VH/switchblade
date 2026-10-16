/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 4 ("FUN4", 8-byte
 * steps: before the drum grid), a format 3 ("FUN3", 57 parameters per track: before the modulation matrix), a
 * format 2 ("FUN2", 53: before the SLICER) and a format 1 ("FUN1") project, built byte for byte as the
 * firmware stored them, convert to today's format: every old value at its parameter, the parameters
 * added since at their defaults (SLICER OFF, every matrix slot OFF), steps, globals, selection, the engine bytes
 * kept; damaged ones are refused. Global id 24 (the drum part's MIDI channel then, REVERB TYPE now) loads as ROOM
 * from every format before FUN7. FM1-Four: only engines 0..3 (OB-Z WAVE ACID DRIFT) load; a project with a track of
 * another engine number is refused in every format. The retired conversions (the GM drum part, PHYS, DIGITAL, SAMPLE
 * PERC, the drum grid) are gone: a track 4 of before 1.0 loads as stored, a step's grid bytes are cleared, the FM6
 * patches' area is written 0 and not read.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"
#include "old_pack.h"                         /* (FUN9 / FUN8 / FUN7 as firmware before 1.2 wrote them) */

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {ENGI_DRIFT, ENGI_OBX, ENGI_ACID, 0};   /* (track 4 of before 1.0: 0, the drum part) */
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* today's steps n are the old steps o (8 bytes each), with no hits */
static int steps_same(const step_t *n, const step8_t *o)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (memcmp(n[k].note, o[k].note, 4) || n[k].n != o[k].n || n[k].time != o[k].time ||
            n[k].flags != o[k].flags || n[k].vel != o[k].vel || n[k].hit || n[k].acc)
            return 0;
    return 1;
}

/* today's project q as format 4 stored it (8-byte steps; the hits dropped) */
static void to_v4(project_v4_t *v, const project_t *q)
{
    uint32_t t, k;
    memset(v, 0, sizeof *v);
    v->magic = PROJ_MAGIC_V4;
    v->size = sizeof *v;
    memcpy(v->g, q->g, sizeof v->g);
    v->sel = q->sel;
    v->parts = q->parts;
    v->phys = q->phys;
    for (t = 0; t < NTRK; t++) {
        for (k = 0; k < 61u; k++) v->t[t].p[k] = q->t[t].p[k];
        for (k = 0; k < 8u; k++) v->t[t].p[61u + k] = q->t[t].p[P_E0 + k];
        v->t[t].engine = q->t[t].engine;
        v->t[t].preset = q->t[t].preset;
        for (k = 0; k < NSTEP; k++) {
            memcpy(v->t[t].step[k].note, q->t[t].step[k].note, 4);
            v->t[t].step[k].n = q->t[t].step[k].n;
            v->t[t].step[k].time = q->t[t].step[k].time;
            v->t[t].step[k].flags = q->t[t].step[k].flags;
            v->t[t].step[k].vel = q->t[t].step[k].vel;
        }
    }
    v->sum = proj_hash(v, sizeof *v - 4u);
}

/* track t of the converted project has the old values where they belong */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step);
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == oldv(t, k);
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == 0;                     /* every matrix slot OFF */
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    return ok;
}

/* a format 3 track (57 parameters: the SLICER at 45..48, P_E0 49) */
static int16_t oldv3(uint32_t t, uint32_t k) { return (int16_t)(t * 200u + k * 5u + 3u); }
static void fill_v3_track(proj_trk_v3_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V3; k++)
        d->p[k] = oldv3(t, k);
    d->engine = (uint8_t)t;                     /* (0..3: OB-Z WAVE ACID DRIFT) */
    d->preset = (uint8_t)(t + 1u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(40u + (k * 7u + t) % 40u);
        s->n = (uint8_t)((k + t) % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)((k + 1u) & 3u);
        s->vel = (uint8_t)(70u + t);
    }
}
static int track_v3_ok(const proj_trk_t *n, const proj_trk_v3_t *o, uint32_t t)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step);
    for (k = 0; k <= P_SLDEPTH; k++)
        ok &= n->p[k] == oldv3(t, k);           /* up to the SLICER: the same ids */
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == TP[k].def && TP[k].def == 0;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv3(t, 49u + k);
    return ok;
}

int main(void)
{
    static project_v4_t v4;
    static project_v3_t v3;
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static union {
        project_t v5;
        project_v4_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_RSV_LN &&
                 P_RSV_LN_END + 1 == P_LSYNC && P_LSYNC + 3 == P_SQNT && P_SQNT + 1 == P_SPRD && P_SPRD + 1 == P_ITYPE &&
                 P_ITYPE + 4 == P_IMIX && P_IMIX + 1 == P_FTYPE && P_FTYPE + 1 == P_ESYNC && P_ESYNC + 1 == P_E0 && P_E0 == 103 &&
                 P_COUNT == PROJ_NP_V3 + 54u && PROJ_NP_V3 == PROJ_NP_V2 + 4u);
    bad += check("FUN10 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3840u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN6: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE ? 0 : (int16_t)(500 + i));
    bad += check("FUN2 -> FUN6: globals (id 24, the old drum channel: ROOM) and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t);   /* (track 4, the drum part then: as stored) */
    bad += check("FUN2 -> FUN6: every parameter mapped, SLICER and matrix OFF (4 tracks)", ok);

    bad += check("FUN2 -> FUN6: engine bytes kept (DRIFT 3, OB-Z 0, ACID 2, 0)",
                 q.t[0].engine == ENGI_DRIFT && q.t[1].engine == ENGI_OBX && q.t[2].engine == ENGI_ACID &&
                 q.t[3].engine == 0 && str_eq(ENGINES[ENGI_DRIFT]->name, "DRIFT") && NENGINES == 4);
    {   /* an engine byte this firmware does not have (FUN2's WHEEL 7, its last 7): the project is refused */
        static const uint8_t RETIRED[] = {4, 7, NENGINES + 9u, 255};
        ok = 1;
        for (i = 0; i < sizeof RETIRED; i++) {
            v2.t[2].engine = RETIRED[i];
            v2.sum = proj_hash(&v2, sizeof v2 - 4u);
            memcpy(&buf, &v2, sizeof v2);
            ok &= !proj_import(&q2, &buf, (int)sizeof v2);
        }
        v2.t[2].engine = OLD_ENG[2];
        v2.sum = proj_hash(&v2, sizeof v2 - 4u);
        bad += check("FUN2 with a track of engine 4, 7, 13 or 255: refused", ok);
    }

    /* format 3 (1.0, four parts), as written before the modulation matrix */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < G_COUNT; i++)
        v3.g[i] = (int16_t)(600 + i);
    v3.sel = 3;
    v3.parts = NPART;
    for (t = 0; t < NTRK; t++)
        fill_v3_track(&v3.t[t], t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q, &buf, (int)sizeof v3) && proj_ok(&q) && q.magic == PROJ_MAGIC && q.sel == 3 &&
         q.parts == NPART;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE ? 0 : (int16_t)(600 + i));
    for (t = 0; t < NTRK; t++)
        ok &= track_v3_ok(&q.t[t], &v3.t[t], t);
    bad += check("FUN3 -> FUN6: every parameter kept, the matrix OFF, steps, globals (24: ROOM)", ok);

    /* a FUN3 written before 1.0 (parts 0: track 4 the drum part, engine byte 0): no conversion now, as stored */
    v3.parts = 0;
    v3.t[3] = v3.t[0];
    v3.t[3].engine = 0;
    v3.t[3].preset = 0;
    v3.g[G_DRLVL] = 90;
    v3.g[G_DRREV] = 20;
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) && track_v3_ok(&q2.t[3], &v3.t[3], 0) &&
         q2.t[3].p[P_LEVEL] == oldv3(0, P_LEVEL) && q2.t[3].p[P_REV] == oldv3(0, P_REV) &&
         !memcmp(&q2.t[0], &q.t[0], sizeof q.t[0]);
    bad += check("FUN3 before 1.0: track 4 (the drum part then) as stored, its own LEVEL / REV", ok);

    /* a FUN6 round trip: stored as is (a matrix slot set; the last engine, DRIFT 3) */
    q.t[1].engine = ENGI_DRIFT;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("FUN6 -> FUN6: as stored (a matrix slot, engine 3)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == ENGI_DRIFT &&
                 q2.t[2].p[P_M2AMT] == -17);
    buf.v5.t[1].engine = NENGINES;
    buf.v5.sum = proj_sum(&buf.v5);
    bad += check("  the same with engine 4 (a valid sum): refused", !proj_import(&q2, &buf, (int)sizeof q));

    {
        project_v5_t old;
        memset(&old, 0, sizeof old);
        memcpy(old.g, q.g, sizeof old.g); old.sel = q.sel; old.parts = q.parts; old.phys = q.phys;
        for (uint32_t k = 0; k < NTRK; k++) {
            for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = q.t[k].p[j];
            for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = q.t[k].p[P_E0 + j];
            old.t[k].engine = q.t[k].engine; old.t[k].preset = q.t[k].preset;
            for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &q.t[k].step[j], sizeof(step10_t));
        }
        old.t[1].step[2].hit = 0x11; old.t[1].step[2].acc = 0x01;   /* (the retired grid's bytes: cleared on load) */
        old.magic = PROJ_MAGIC_V5;
        old.size = sizeof old;
        old.sum = proj_hash(&old, sizeof old - 4u);
        bad += check("FUN5 -> FUN6: sounds and steps kept (the grid's hits cleared), no chain",
            proj_import(&q2, &old, sizeof old) && !memcmp(q2.t, q.t, sizeof q.t) &&
            !q2.chain.count && chain_valid(&q2.chain));
        old.t[3].engine = NENGINES;
        old.sum = proj_hash(&old, sizeof old - 4u);
        bad += check("FUN5 with a track of engine 4: refused", !proj_import(&q2, &old, sizeof old));
        old.t[3].engine = q.t[3].engine;
        old.sum = proj_hash(&old, sizeof old - 4u) ^ 1u;
        bad += check("FUN5: damaged checksum refused", !proj_import(&q2, &old, sizeof old));
        q.chain.count = 2;
        q.chain.row[0] = chain_row_of(0, 4);
        q.chain.row[1] = chain_row_of(3, 16);
        q.sum = proj_sum(&q);
        bad += check("FUN6: chain round trip", proj_import(&q2, &q, sizeof q) &&
            !memcmp(&q.chain, &q2.chain, sizeof q.chain));
        q.chain.row[0].repeat = 0;
        q.sum = proj_sum(&q);
        bad += check("FUN6: invalid row refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));
        chain_defaults(&q.chain);
        q.sum = proj_sum(&q);
    }

    /* format 4 (1.0 development builds): 8-byte steps */
    to_v4(&v4, &q);
    bad += check("FUN4 image is 2680 bytes (as stored)", sizeof v4 == 2680u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == q.sel &&
         !memcmp(q2.g, q.g, sizeof q.g);
    for (t = 0; t < NTRK; t++)
        ok &= !memcmp(q2.t[t].p, q.t[t].p, sizeof q.t[t].p) && q2.t[t].engine == q.t[t].engine &&
              steps_same(q2.t[t].step, v4.t[t].step);
    bad += check("FUN4 -> FUN6: parameters, engines, globals; steps as they were, no hits", ok);
    v4.t[1].engine = NENGINES;
    v4.sum = proj_hash(&v4, sizeof v4 - 4u);
    memcpy(&buf, &v4, sizeof v4);
    bad += check("FUN4 with a track of engine 4: refused", !proj_import(&q2, &buf, (int)sizeof v4));
    v4.t[1].engine = q.t[1].engine;
    v4.sum = proj_hash(&v4, sizeof v4 - 4u);
    v3.t[0].p[7]++;
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V3;
    bad += check("FUN6 size with a FUN3 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V2;
    bad += check("FUN6 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0) && q.g[5] == 705 &&
         q.parts == NPART;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == PROJ_DEF_SOUND && q.t[t].engine == trk_def_engine(t) && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN6: track 1 mapped, tracks 2..4 defaults", ok);

    {   /* the FUN7 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("FUN10 name at the end of the reserved tail, the retired FM6 area before it, after the data (16 spare)",
                     PROJ_NAME_OFF == 3824u && PROJ_FM6_OFF == 3312u && 68u + NTRK * (P_COUNT + 2u + NSTEP * 9u) +
                     sizeof(chain_config_t) + sizeof(motion_store_t) + 16u == PROJ_FM6_OFF);
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN7 no name: the tail all zero (as firmware before wrote it)", ok && zero);
        bad += check("FUN7 of before names (zero tail): loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN7 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN7 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
        bad += check("FUN6..FUN1 imports: no name", !q2.name[0] && !q.name[0]);
    }

    {   /* FUN8..FUN10: the retired FM6 patches' area (PROJ_FM6_OFF, 4 x 128 bytes) is written 0 and not read; FUN8 /
         * FUN7 of before load their music and name */
        project_t a, c;
        static project_store_t st, slot;
        static uint8_t v7[PROJ_STORE_V7];
        uint32_t i, k, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < NTRK; k++) {
            a.t[k].engine = (uint8_t)k;                 /* OB-Z WAVE ACID DRIFT */
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == 0x46554E41u && proj_import(&c, &st, sizeof st) &&
             !memcmp(&c, &a, sizeof c);
        for (k = 0; k < NTRK * PROJ_FM6_PACKED; k++)
            ok &= st.raw[PROJ_FM6_OFF + k] == 0u;
        bad += check("FUN10 round trip: the four engines, the name; the retired FM6 area all 0", ok &&
                     !memcmp(c.name, "FM SONG", 7));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN10: a byte there changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        sum = proj_hash(st.raw, PROJ_STORE_SIZE - 4u);
        memcpy(st.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        bad += check("  with its hash right: loads, the byte not read", proj_import(&c, &st, sizeof st) &&
                     !memcmp(&c, &a, sizeof c));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        {   /* FUN8 as 1.0.x wrote it (3584 bytes, 91 parameters: E0..E7 at 83..90, the patches at 3056): the reserved
             * block (the retired lane levels) 127, E0..E7 and their motion at today's P_E0.., the name kept */
            static uint8_t v8[3584];
            uint32_t pos = 68u, pos9 = 68u, j;
            project_t e = a;
            e.t[3].engine = ENGI_DRIFT; e.t[3].p[P_E3] = 30;
            e.motion.count = 2; e.motion.on = 8;
            e.motion.event[0] = (motion_event_t){3u << 6 | 2u, P_E3, 99};
            e.motion.event[1] = (motion_event_t){3u << 6 | 4u, P_REV, 50};
            {   /* FUN10 holds parameter locks (bit 7 of the id): on a send and on an engine value */
                project_t f = e, g;
                f.motion.count = 4;
                f.motion.event[2] = (motion_event_t){3u << 6 | 5u, (uint8_t)(P_DIST | MOTION_LOCK), 40};
                f.motion.event[3] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
                f.sum = proj_sum(&f);
                ok = proj_pack(&st, &f) && proj_import(&g, &st, sizeof st) && !memcmp(&f.motion, &g.motion, sizeof f.motion);
                bad += check("FUN10 round trip: locks on a send (P_DIST) and an engine value (P_E5) kept", ok &&
                             st.raw[68u + NTRK * (P_COUNT + 2u + NSTEP * 9u) + sizeof(chain_config_t) + 4u + 6u + 1u] ==
                             (P_DIST | MOTION_LOCK) && g.motion.event[3].param == (P_E5 | MOTION_LOCK));
            }
            e.motion.count = 3;   /* a FUN8 of a 1.1 development build (91 parameters) could hold a lock: its E5 then */
            e.motion.event[2] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
            e.sum = proj_sum(&e);
            ok = old_pack(v8, &e, OLD_FUN8, 91u) == sizeof v8;   /* (its E3 at 86, its E5 lock at 88) */
            ok = ok && proj_import(&c, v8, sizeof v8);
            for (k = 0; ok && k < NTRK; k++)
                for (j = P_RSV_LN; j <= P_RSV_LN_END; j++) ok &= c.t[k].p[j] == 127;
            bad += check("FUN8 of 1.0.x (91 parameters): the reserved block 127, E0..E7 at P_E0, the name kept", ok &&
                         c.t[3].engine == ENGI_DRIFT && c.t[3].p[P_E3] == 30 && c.t[2].p[P_E7] == 4 &&
                         !memcmp(c.name, "FM SONG", 7));
            bad += check("  its motion: E3 (86 then) at P_E3, REV kept", c.motion.count == 3u &&
                         c.motion.event[0].param == P_E3 && c.motion.event[0].value == 99 && c.motion.event[1].param == P_REV);
            bad += check("  a lock on its E5 (88 then): at P_E5, still a lock", c.motion.event[2].param == (P_E5 | MOTION_LOCK) &&
                         c.motion.event[2].value == -3);
            bad += check("  one byte short: refused", !proj_import(&c, v8, sizeof v8 - 1u));
            memset(&slot, 0xA5, sizeof slot);
            memcpy(slot.raw, v8, sizeof v8);
            bad += check("  in a retained (longer) slot: loads", proj_import(&c, &slot, sizeof slot) && c.t[3].p[P_E3] == 30);
            e.t[1].engine = NENGINES;                   /* (a retired engine's number: 1.x's DIGITAL was 1, SAMPLE 4) */
            e.sum = proj_sum(&e);
            bad += check("  a FUN8 with a track of engine 4: refused", old_pack(v8, &e, OLD_FUN8, 91u) == sizeof v8 &&
                         !proj_import(&c, v8, sizeof v8));
        }
        /* the same music as FUN7 (3388 bytes, 91 parameters, no patches): the name at 3372 */
        ok = old_pack(v7, &a, OLD_FUN7, 91u) == sizeof v7 && proj_import(&c, v7, sizeof v7);
        bad += check("FUN7 -> FUN10: the music and name kept",
                     ok && c.t[2].engine == ENGI_ACID && c.t[2].p[P_E7] == 4 && !memcmp(c.name, "FM SONG", 7));
        {   /* REVERB TYPE (id 24) of a FUN7 holding the old drum channel (10): ROOM; SPRING (1) kept */
            int16_t g24 = 10;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok = proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 0 && proj_ok(&c);
            g24 = 1;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok &= proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 1;
            bad += check("FUN7 REVERB TYPE above SPRING (an old drum channel): ROOM", ok);
        }
        ok = proj_import(&c, v7, sizeof v7 - 1u);
        bad += check("FUN7 one byte short: refused", !ok);
        /* the retained cache after an update: slot 1's FUN7 record, longer slot, garbage after it */
        memset(&slot, 0xA5, sizeof slot);
        memcpy(slot.raw, v7, sizeof v7);
        bad += check("a retained 3840-byte slot holding a FUN7 record loads it", proj_import(&c, &slot, sizeof slot) &&
                     c.t[2].p[P_E7] == 4);
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN10: a step out of range packs inside it (note, velocity 127; the grid bytes 0): it loads",
                     ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                     !c.t[0].step[3].hit && !c.t[0].step[3].acc);
    }

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}

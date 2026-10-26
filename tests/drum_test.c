/* SPDX-License-Identifier: GPL-3.0-only */
/* drum_test.c (WaveLoop): DRUM, Switch Blade's fifth machine (firmware/src/eng_drum.c, drum_voice.c, drum_yama.c), through
 * the real voice and mix paths (hostsim.c): every kit's every drum sounds and ends; the 808 and the CR78 are not the same
 * kit; the GM map (the Oxygen's pads) and the FM-1's keys (the first C the kick); each drum shaped on its own (a live hit
 * picks it, the sequencer's do not; an edit moves that drum only, and is stored only where it differs from the kit); a
 * drum's PAN places it (the rest of the kit in the middle) and its SEND reaches the reverb with the track's REV at 0;
 * nothing set: the kit as one bus, L = R; the ARP never plays a DRUM track; drums and synths share the eight voices (a
 * synth note takes a ringing drum's voice, never a held synth note's). Each render runs in a child process (fork) so
 * every one starts from the same state. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int fails;
static int check(const char *what, int ok)
{
    printf("drum: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    return ok;
}

typedef struct {
    double el, er;               /* energy of the left / right channel */
    int32_t peak;
    uint32_t zc;                 /* zero crossings of the left channel (a pitch's rough measure) */
    uint64_t h;                  /* FNV of the left channel */
    double rev;                  /* the reverb bus's input, summed |x| */
    uint32_t busy_end;           /* voices still active at the end */
} rr_t;

/* the drum track: T4, preset pi (808 = 0) */
static void drum_init(uint32_t pi)
{
    host_tracks_init();
    host_preset(&trk[3], ENGI_DRUM, pi);
    song.sel = 3;
}

/* blocks of the mix; note (0: none) struck at the start, at velocity vel, released after one block (a pad) */
static void render(rr_t *r, uint32_t note, uint32_t vel, uint32_t blocks)
{
    int32_t o[2 * CTL];
    uint32_t b, i, prev = 0;
    int pl = 0;
    memset(r, 0, sizeof *r);
    r->h = 1469598103934665603ull;
    if (note)
        trk_note_on(&trk[3], note, vel);
    for (b = 0; b < blocks; b++) {
        if (b == 1 && note)
            trk_note_off(&trk[3], note);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t l = o[2 * i], rr = o[2 * i + 1], a = l < 0 ? -l : l;
            r->el += (double)l * l;
            r->er += (double)rr * rr;
            if (a > r->peak)
                r->peak = a;
            if ((l < 0) != pl && (l > 64 || l < -64)) {
                r->zc++;
                pl = l < 0;
            }
            r->h = (r->h ^ (uint32_t)l) * 1099511628211ull;
            r->rev += fabs((double)send_r[i]);
        }
        (void)prev;
    }
    r->busy_end = busy_now();
}

/* the child runs fn and sends its result back */
static rr_t in_child(void (*fn)(rr_t *, uint32_t), uint32_t arg)
{
    int fd[2];
    rr_t r;
    pid_t pid;
    memset(&r, 0, sizeof r);
    if (pipe(fd))
        return r;
    fflush(stdout);
    pid = fork();
    if (!pid) {
        close(fd[0]);
        fn(&r, arg);
        if (write(fd[1], &r, sizeof r) != (ssize_t)sizeof r)
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != (ssize_t)sizeof r)
        memset(&r, 0, sizeof r);
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r;
}

/* ------------------------------------------------------------------------------------------------ the kits --- */
static uint32_t g_kit;
static void hit_lane(rr_t *r, uint32_t lane)
{
    drum_init(g_kit);
    render(r, DRUM_LANE_NOTE[lane], 110, (uint32_t)(12.0 * FS / CTL));  /* 12 s: the longest ring (a cymbal, -84 dB) ends */
}
static void test_kits(void)
{
    uint32_t k, l, sounds = 0, ends = 0, n = ENGINES[ENGI_DRUM]->npresets;
    uint64_t h808[DV_NLANE], hcr[DV_NLANE];
    uint32_t differ = 0;
    for (k = 0; k < n; k++) {
        g_kit = k;
        for (l = 0; l < DV_NLANE; l++) {
            rr_t r = in_child(hit_lane, l);
            sounds += r.peak > 300;
            ends += r.busy_end == 0u;
            if (r.peak <= 300 || r.busy_end)
                printf("drum:   %s lane %u (%s): peak %d, voices left %u\n", ENGINES[ENGI_DRUM]->presets[k].name, (unsigned)l,
                       drum_lane_name(&trk[3], l), (int)r.peak, (unsigned)r.busy_end);
            if (k == 0u)
                h808[l] = r.h;
            if (k == 1u)
                hcr[l] = r.h;
        }
    }
    for (l = 0; l < DV_NLANE; l++)
        differ += h808[l] != hcr[l];
    check("every kit's every drum sounds (its GM note, a pad's hit) and ends, its voice freed", sounds == n * DV_NLANE &&
          ends == n * DV_NLANE);
    check("the 808 and the CR78 are different kits (every drum)", differ == DV_NLANE);
}

/* ------------------------------------------------------------------------------------------- notes, keys --- */
static void test_map(void)
{
    static const struct { uint8_t note, lane; } M[] = {{36, DV_KICK}, {35, DV_KICK}, {38, DV_SNARE}, {40, DV_SNARE},
        {39, DV_CLAP}, {42, DV_HATC}, {44, DV_HATC}, {46, DV_HATO}, {45, DV_TOM}, {50, DV_TOM}, {37, DV_RIM}, {56, DV_BELL},
        {49, DV_BELL}};
    uint32_t i, ok = 1;
    for (i = 0; i < NELEM(M); i++)
        ok &= drum_lane(M[i].note) == M[i].lane;
    check("GM notes (the Oxygen's pads on channel 4): 36 / 35 KICK, 38 / 40 SNARE, 39 CLAP, 42 / 44 HATCL, 46 HATOP, toms, 37 RIM, 56 / 49 BELL", ok);
    drum_init(0);
    song.octave = 0;
    ok = kb_map(&trk[3], 7) == 36u && kb_map(&trk[3], 9) == 38u && kb_map(&trk[3], 13) == 42u;
    song.octave = 1;
    ok &= kb_map(&trk[3], 7) == 48u;
    song.octave = 0;
    host_preset(&trk[3], ENGI_OBX, 0);
    ok &= kb_map(&trk[3], 7) == 60u;
    check("the FM-1's keys on a DRUM track: the GM map, the first C the kick (36), D the snare, F# the hat; OCT moves it; a synth as before", ok);
}

/* ------------------------------------------------------------------------------------- each drum its own --- */
static void kick_pitched(rr_t *r, uint32_t lane)
{
    drum_init(0);
    drum_focus_set(&trk[3], DV_KICK);
    trk[3].p[P_E1] = 12;                                 /* the kick an octave up */
    if (lane != DV_KICK)
        drum_focus_set(&trk[3], lane);                   /* (picking another drum keeps the kick's) */
    render(r, DRUM_LANE_NOTE[lane], 110, (uint32_t)(0.5 * FS / CTL));
}
static void plain(rr_t *r, uint32_t lane)
{
    drum_init(0);
    render(r, DRUM_LANE_NOTE[lane], 110, (uint32_t)(0.5 * FS / CTL));
}
static void test_each_its_own(void)
{
    rr_t k0 = in_child(plain, DV_KICK), k1 = in_child(kick_pitched, DV_KICK);
    rr_t s0 = in_child(plain, DV_SNARE), s1 = in_child(kick_pitched, DV_SNARE);
    drum_st_t d;
    uint32_t ok;
    check("PITCH +12 on the picked kick: it sounds higher", k1.zc > k0.zc * 3 / 2);
    check("  the snare, picked after, sounds as it did (bit for bit)", s1.h == s0.h && s0.peak > 300);
    drum_init(0);
    drum_focus_set(&trk[3], DV_KICK);
    trk[3].p[P_E1] = 12;
    trk[3].p[P_E3] = 64;                                 /* (the kit's DECAY: not the kick's own) */
    drum_focus_set(&trk[3], DV_SNARE);
    ok = drum_st[3].focus == DV_SNARE && trk[3].p[P_E1] == 0 && drum_st[3].mask[DV_KICK] == 1u && drum_st[3].v[DV_KICK][DC_PITCH] == 12;
    check("  stored only where edited: the kick keeps its PITCH (mask bit 0), its DECAY is the kit's; the snare's values in p[]", ok);
    drum_focus_set(&trk[3], DV_KICK);
    ok = trk[3].p[P_E1] == 12 && drum_val(&trk[3], DV_SNARE, DC_PITCH) == 0 && drum_val(&trk[3], DV_KICK, DC_PITCH) == 12;
    check("  picked again, the kick's PITCH comes back into p[] (the pages, the CCs, the knob card)", ok);
    drum_state_of(&trk[3], &d);
    ok = d.mask[DV_KICK] == 1u && d.v[DV_KICK][DC_PITCH] == 12 && d.focus == DV_KICK;
    host_preset(&trk[3], ENGI_DRUM, 1);                  /* another kit: every drum the kit's values */
    ok &= drum_st[3].mask[DV_KICK] == 0u && drum_st[3].set && drum_val(&trk[3], DV_SNARE, DC_LEVEL) == 100;
    drum_state_set(&trk[3], &d);
    trk[3].p[P_E1] = 12;                                 /* (p[] is the caller's: a project's, an undo's copy, the kick's) */
    ok &= drum_val(&trk[3], DV_KICK, DC_PITCH) == 12 && drum_val(&trk[3], DV_SNARE, DC_PITCH) == 0;
    check("  the state round trips (drum_state_of / set: projects, undo); a sound load gives every drum the kit's values", ok);
}

static void live_and_seq(rr_t *r, uint32_t live)
{
    drum_init(0);
    if (live)
        input_on(&trk[3], 38, 100);                      /* (a key, a pad, a MIDI note) */
    else {
        drum_seq = 1;                                    /* (as seq.c seq_step plays a step) */
        trk_note_on(&trk[3], 38, 100);
        drum_seq = 0;
    }
    r->peak = drum_hit[3];
}
static void test_pick(void)
{
    rr_t a = in_child(live_and_seq, 1), b = in_child(live_and_seq, 0);
    check("a live hit picks its drum (drum_hit: the snare), the sequencer's picks none", a.peak == DV_SNARE + 1 && b.peak == 0);
}

/* ------------------------------------------------------------------------------------------- PAN, SEND --- */
static void panned(rr_t *r, uint32_t what)
{
    drum_init(0);
    trk[3].p[P_REV] = 0;
    drum_focus_set(&trk[3], DV_SNARE);
    if (what == 1u)
        trk[3].p[P_E6] = -64;                            /* the snare hard left */
    if (what == 2u)
        trk[3].p[P_E7] = 100;                            /* its own reverb */
    drum_focus_set(&trk[3], DV_KICK);                    /* (the kick picked: the snare keeps its own) */
    render(r, what == 3u ? 36u : 38u, 110, (uint32_t)(0.5 * FS / CTL));
}
static void test_pan_send(void)
{
    rr_t c = in_child(panned, 0), l = in_child(panned, 1), s = in_child(panned, 2), k = in_child(panned, 3);
    check("nothing set: the kit is one bus, L = R, no reverb with the track's REV at 0", c.el == c.er && c.el > 0 && c.rev == 0);
    check("a drum's PAN hard left: its sound on the left only (the rest of the kit stays in the middle)",
          l.el > 0 && l.er < l.el * 0.001 && fabs(l.el - c.el) < c.el * 0.6);
    check("a drum's SEND: its reverb with the track's REV at 0", s.rev > 0 && s.el >= c.el);
    check("  the kick, which has neither, is centred and dry", k.el == k.er && k.rev == 0);
}

/* ---------------------------------------------------------------------------------------------------- ARP --- */
static void arp_on(rr_t *r, uint32_t unused)
{
    int32_t o[2 * CTL];
    (void)unused;
    drum_init(0);
    trk[3].p[P_AMODE] = 1;
    mix_block(o, CTL);                                   /* (events_block: ARP off on a DRUM track) */
    input_on(&trk[3], 36, 100);
    r->peak = trk[3].p[P_AMODE];
    r->zc = trk[3].nheld;
    r->busy_end = busy_now();
}
static void test_arp(void)
{
    rr_t r = in_child(arp_on, 0);
    check("the ARP does not play a DRUM track: ARP goes off, a key hits the drum at once (no held chord)",
          r.peak == 0 && r.zc == 0 && r.busy_end == 1u);
}

/* ----------------------------------------------------------------------------------------- the voice pool --- */
static void shared(rr_t *r, uint32_t unused)
{
    int32_t o[2 * CTL];
    uint32_t i, b, held = 0;
    (void)unused;
    drum_init(0);
    host_preset(&trk[0], ENGI_OBX, 0);
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], 48u + 4u * i, 100);          /* a synth chord held */
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[3], DRUM_LANE_NOTE[i], 110);     /* four drums ringing (struck, let go) */
    mix_block(o, CTL);
    for (i = 0; i < 4u; i++)
        trk_note_off(&trk[3], DRUM_LANE_NOTE[i]);
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    r->zc = busy_now();
    for (i = 0; i < 2u; i++)
        trk_note_on(&trk[0], 72u + i, 100);               /* two more synth notes: the budget is full */
    for (b = 0; b < 4u; b++)
        mix_block(o, CTL);
    for (i = 0; i < NVOICE; i++)
        held += trk[0].v[i].active && trk[0].v[i].gate;
    r->peak = (int32_t)held;
    r->busy_end = busy_now();
}
static void test_pool(void)
{
    rr_t r = in_child(shared, 0);
    check("drums share the eight voices: a held 4-note chord and 4 drums fill them; 2 more synth notes take 2 drums' voices, every held note stays",
          r.zc == 8u && r.peak == 6 && r.busy_end <= 8u);
}

/* ------------------------------------------------------------------------------------------------ KIT 909 --- */
/* the 909's drums by a note each: BD SD LT MT HT RS CP CH OH CR RD (drum909_ins) */
static const uint8_t N909[D9_NUM] = {36, 38, 41, 45, 50, 37, 39, 42, 46, 49, 51};
static uint32_t g_pre;
static void hit909(rr_t *r, uint32_t note)
{
    drum_init(DK_909);
    render(r, note, 110, (uint32_t)(4.0 * FS / CTL));
}
static void hit909_shaped(rr_t *r, uint32_t what)
{
    drum_init(DK_909);
    drum_focus_set(&trk[3], what >= 4u ? DV_SNARE : DV_KICK);
    if (what == 1u)
        trk[3].p[P_E1] = 12;                             /* PITCH +12 */
    if (what == 2u)
        trk[3].p[P_E3] = 127;                            /* DECAY */
    if (what == 3u)
        trk[3].p[P_E5] = 50;                             /* LEVEL 50 */
    if (what == 5u)
        trk[3].p[P_E4] = 127;                            /* the snare's SNAP */
    if (what == 6u)
        trk[3].p[P_E2] = 0;                              /* the snare's TONE */
    drum_focus_set(&trk[3], DV_HATC);                    /* (another drum picked: they keep their own) */
    render(r, what >= 4u ? 38u : 36u, 110, (uint32_t)(1.5 * FS / CTL));
}
static void toms_together(rr_t *r, uint32_t unused)
{
    int32_t o[2 * CTL];
    (void)unused;
    drum_init(DK_909);
    trk_note_on(&trk[3], 41, 110);
    trk_note_on(&trk[3], 50, 110);
    trk_note_on(&trk[3], 51, 110);                       /* the ride */
    trk_note_on(&trk[3], 49, 110);                       /* the crash: the same lane, its own voice */
    mix_block(o, CTL);
    trk_note_off(&trk[3], 41);
    trk_note_off(&trk[3], 50);
    mix_block(o, CTL);
    r->zc = busy_now();
}
static void oh_choked(rr_t *r, uint32_t choke)
{
    int32_t o[2 * CTL];
    uint32_t b, i;
    drum_init(DK_909);
    trk_note_on(&trk[3], 46, 110);
    for (b = 0; b < 40u; b++)
        mix_block(o, CTL);
    if (choke)
        trk_note_on(&trk[3], 42, 1);                      /* a closed hat, as soft as it goes */
    for (b = 0; b < 200u; b++) {
        mix_block(o, CTL);
        if (b >= 20u)                                    /* from 15 ms after */
            for (i = 0; i < CTL; i++)
                r->el += (double)o[2 * i] * o[2 * i];
    }
}
static void test_909(void)
{
    uint32_t i, j, sounds = 0, ends = 0, distinct = 1, bounded = 1;
    rr_t r[D9_NUM], a, b;
    uint64_t h808k;
    for (i = 0; i < D9_NUM; i++) {
        r[i] = in_child(hit909, N909[i]);
        sounds += r[i].peak > 300;
        ends += r[i].busy_end == 0u;
        bounded &= r[i].peak < 32767 * 4;
        for (j = 0; j < i; j++)
            distinct &= r[i].h != r[j].h;
        printf("drum:   909 %-2s (note %u): peak %6d, rms %8.1f%s\n", (const char *[]){"BD", "SD", "LT", "MT", "HT", "RS", "CP",
               "CH", "OH", "CR", "RD"}[i], (unsigned)N909[i], (int)r[i].peak, sqrt(r[i].el / (4.0 * FS)),
               r[i].busy_end ? " (voice left)" : "");
    }
    check("KIT 909: every one of its eleven drums sounds (BD SD LT MT HT RS CP CH OH CR RD) and ends, its voice freed",
          sounds == D9_NUM && ends == D9_NUM);
    check("  each drum its own sound (eleven different renders), inside the mixer's limits", distinct && bounded);
    g_kit = 0;
    a = in_child(hit_lane, DV_KICK);
    h808k = a.h;
    check("  the 909's kick is not the 808's; its level within 12 dB of the 808 kick's peak", r[0].h != h808k &&
          r[0].peak > a.peak / 4 && r[0].peak < a.peak * 4);
    printf("drum:   (808 kick peak %d, 909 kick %d)\n", (int)a.peak, (int)r[0].peak);
    a = in_child(toms_together, 0);
    check("  the toms, the ride and the crash ring together (a voice each): four voices", a.zc == 4u);
    drum_init(DK_909);
    check("  the lane names: RIDE on the BELL lane (the knob card, EDIT's header); the others the kit's",
          !strcmp(drum_lane_name(&trk[3], DV_BELL), "RIDE") && !strcmp(drum_lane_name(&trk[3], DV_KICK), "KICK") &&
          !strcmp(drum_lane_name(&trk[3], DV_TOM), "TOM"));
    a = in_child(oh_choked, 0), b = in_child(oh_choked, 1);
    check("  a closed hat chokes the open one (its tail after the closed hit < 1 % of the open hat's alone)",
          a.el > 0 && b.el < a.el * 0.01);
    {
        rr_t k0 = in_child(hit909_shaped, 0), kp = in_child(hit909_shaped, 1), kd = in_child(hit909_shaped, 2);
        rr_t kl = in_child(hit909_shaped, 3), s0 = in_child(hit909_shaped, 4), ss = in_child(hit909_shaped, 5);
        rr_t st = in_child(hit909_shaped, 6);
        check("  the kick's PITCH +12: it sounds higher", kp.zc > k0.zc * 3 / 2);
        check("  the kick's DECAY 127: it rings longer (more energy)", kd.el > k0.el * 1.5);
        check("  the kick's LEVEL 50: quieter, -4 .. -9 dB (its energy; 100 sits on the drums' knee, as every kit's)",
              kl.el < k0.el * 0.4 && kl.el > k0.el * 0.125);
        printf("drum:   (the kick's energy at LEVEL 50: %.1f dB)\n", 10.0 * log10(kl.el / k0.el));
        check("  the snare's SNAP 127 (SNAPPY) and TONE 0 (its noise) each change it; the kick's values left it alone",
              ss.h != s0.h && st.h != s0.h && ss.el > s0.el);
    }
    (void)g_pre;
}

/* -------------------------------------------------------------------------------------- the cost (CPU) --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
/* the dearest 909 drums first (each measured alone, over the idle mix, the dearest half: SD 303, the toms 330, BD 268, OH
 * CR RD 219, RS 218, CH 219 (its mean below OH's), CP 192); the 808's pattern plays the same notes (its lanes) */
static const uint8_t DENSE[D9_NUM] = {38, 41, 45, 50, 36, 46, 49, 51, 37, 42, 39};
typedef struct { double mean, worst; } cost_t;
static uint32_t g_drift;                                 /* JUNO's FAT SAW BASS (the dearest DRIFT preset) */
/* T4: the kit, its N dearest drums struck on every 16th at 120 BPM (every 4th accented: 127, else 100; let go after a
 * block, as pads); T1..T3: JUNO FAT SAW BASS holding s notes over the three (3 + 3 + 2 at most: DRIFT_POLY), POLY, SUS
 * 127. 0.5 s to settle, then 2 s measured in halves (HALF_FRAMES): the mean and the dearest half, instructions a sample */
static cost_t mix_run(uint32_t kit, uint32_t nd, uint32_t s)
{
    static const uint8_t NOTES[3][3] = {{48, 52, 55}, {60, 64, 67}, {72, 76, 79}};
    int32_t o[2 * CTL];
    uint32_t b, i, p, nb = (uint32_t)(2.5 * FS / CTL), bh = HALF_FRAMES / CTL, sixt = (uint32_t)(FS * 60.0 / 120.0 / 4.0);
    uint64_t t0 = 0, th = 0, sum = 0, worst = 0;
    uint32_t pos = 0, step = 0, nh = 0;
    cost_t c = {0, 0};
    host_tracks_init();
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    host_preset(&trk[3], ENGI_DRUM, kit);
    for (p = 0; p < 3u; p++) {
        uint32_t k = s > 3u * p ? (s - 3u * p > 3u ? 3u : s - 3u * p) : 0u;
        if (p == 2u && k > 2u)
            k = 2u;
        host_preset(&trk[p], ENGI_DRIFT, g_drift);
        trk[p].p[P_RSV_LN] = 0;                          /* (CHOR OFF, as drift_test's mixes) */
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        if (!k)
            trk[p].p[P_MUTE] = 1;
        for (i = 0; i < k; i++)
            trk_note_on(&trk[p], NOTES[p][i], 100);
    }
    if (!nd)
        trk[3].p[P_MUTE] = 1;
    for (b = 0; b < nb; b++) {
        if (nd && pos >= step * sixt) {                  /* a 16th */
            uint32_t vel = step % 4u ? 100u : 127u;
            for (i = 0; i < nd; i++)
                trk_note_on(&trk[3], DENSE[i], vel);
            step++;
        }
        if (b == (uint32_t)(0.5 * FS / CTL))
            t0 = th = instr_now();
        mix_block(o, CTL);
        if (nd && pos >= (step - 1u) * sixt && pos < (step - 1u) * sixt + CTL)
            for (i = 0; i < nd; i++)
                trk_note_off(&trk[3], DENSE[i]);
        pos += CTL;
        if (t0 && (b + 1u) % bh == 0u) {                 /* a half's end */
            uint64_t now = instr_now(), d = now - th;
            th = now;
            if (nh++) {                                  /* (the first one: partial) */
                sum += d;
                if (d > worst)
                    worst = d;
            }
        }
    }
    if (nh > 1u) {
        c.mean = (double)sum / ((nh - 1u) * HALF_FRAMES);
        c.worst = (double)worst / HALF_FRAMES;
    }
    (void)t0;
    return c;
}
static void cost_child(rr_t *r, uint32_t arg)            /* arg: kit << 8 | drums << 4 | synth voices */
{
    cost_t c = mix_run(arg >> 8, (arg >> 4) & 15u, arg & 15u);
    r->el = c.mean;
    r->er = c.worst;
    r->busy_end = busy_now();
}
static cost_t cost_of(uint32_t kit, uint32_t nd, uint32_t s)
{
    rr_t r = in_child(cost_child, kit << 8 | nd << 4 | s);
    cost_t c = {r.el, r.er};
    return c;
}
#define DEV_PCT 0.017                                    /* the device estimate: 1.7 % of the deadline per 100 (reverb_test.c) */
static void test_cost(void)
{
    uint32_t k, n, kits[2] = {DK_808, DK_909};
    cost_t idle, alone[2], all[2], w[2] = {{0, 0}, {0, 0}}, syn;
    uint32_t wn[2] = {0, 0};
    if (!instr_now()) {
        printf("drum: SKIP the cost: no instruction counter on this host (macOS proc_pid_rusage only)\n");
        return;
    }
    for (k = 0; k < ENGINES[ENGI_DRIFT]->npresets; k++)
        if (!strcmp(ENGINES[ENGI_DRIFT]->presets[k].name, "FAT SAW BASS"))
            g_drift = k;
    idle = cost_of(DK_808, 0, 0);
    syn = cost_of(DK_808, 0, 8);
    printf("drum: cost, host instructions a sample (-O2), the mean / the dearest half (%u samples); the device estimate "
           "%.1f %% of the deadline per 100\n", (unsigned)HALF_FRAMES, DEV_PCT * 100.0);
    printf("drum:   idle mix %.0f / %.0f; JUNO FAT SAW BASS x8 (3 + 3 + 2), no drums: %.0f / %.0f (~%.0f %%)\n", idle.mean,
           idle.worst, syn.mean, syn.worst, syn.worst * DEV_PCT);
    for (k = 0; k < 2u; k++) {
        alone[k] = cost_of(kits[k], 8, 0);
        all[k] = cost_of(kits[k], D9_NUM, 0);
        printf("drum:   %s alone, 8 drums on every 16th: %.0f / %.0f; all eleven notes: %.0f / %.0f (~%.0f %%)\n",
               N_DRUM_KIT[kits[k]], alone[k].mean, alone[k].worst, all[k].mean, all[k].worst, all[k].worst * DEV_PCT);
    }
    for (n = 0; n <= 8u; n++) {                          /* the shared budget: n drums + 8 - n JUNO voices */
        cost_t c[2];
        for (k = 0; k < 2u; k++) {
            c[k] = cost_of(kits[k], n, 8u - n);
            if (c[k].worst > w[k].worst) {
                w[k] = c[k];
                wn[k] = n;
            }
        }
        printf("drum:   %u drums + %u JUNO voices: 808 %.0f / %.0f (~%.0f %%)   909 %.0f / %.0f (~%.0f %%)\n", (unsigned)n,
               (unsigned)(8u - n), c[0].mean, c[0].worst, c[0].worst * DEV_PCT, c[1].mean, c[1].worst, c[1].worst * DEV_PCT);
    }
    printf("drum:   the dearest: 808 %u drums %.0f (~%.0f %%), 909 %u drums %.0f (~%.0f %%); JUNO alone %.0f (~%.0f %%)\n",
           (unsigned)wn[0], w[0].worst, w[0].worst * DEV_PCT, (unsigned)wn[1], w[1].worst, w[1].worst * DEV_PCT, syn.worst,
           syn.worst * DEV_PCT);
    check("cost: KIT 909's dearest mix (n drums + 8 - n JUNO voices) within the four-track worst the firmware already "
          "carries (drift_test's 3864 probe + 10 %)", w[1].worst <= 3864.0 * 1.1);
}

int main(void)
{
    test_kits();
    test_map();
    test_each_its_own();
    test_pick();
    test_pan_send();
    test_arp();
    test_pool();
    test_909();
    test_cost();
    printf(fails ? "DRUM TEST FAILED (%d)\n" : "drum: all ok\n", fails);
    return fails ? 1 : 0;
}

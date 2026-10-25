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

int main(void)
{
    test_kits();
    test_map();
    test_each_its_own();
    test_pick();
    test_pan_send();
    test_arp();
    test_pool();
    printf(fails ? "DRUM TEST FAILED (%d)\n" : "drum: all ok\n", fails);
    return fails ? 1 : 0;
}

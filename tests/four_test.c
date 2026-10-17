/* four_test.c: the four-machine contract */
#define ENGINES_QUAL
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void test_engine_table(void)
{
    static const char *const NAME[4] = {"OB-Z", "WAVE", "ACID", "DRIFT"};
    uint32_t e;
    CHECK(NENGINES == 4u, "NENGINES %u", (unsigned)NENGINES);
    for (e = 0; e < NENGINES; e++) {
        CHECK(strcmp(ENGINES[e]->name, NAME[e]) == 0, "engine %u is %s", (unsigned)e, ENGINES[e]->name);
        CHECK(ENGINES[e]->knob[0] == P_E4, "%s K1 not CUT", NAME[e]);
        CHECK(ENGINES[e]->knob[1] == P_E5, "%s K2 not RES", NAME[e]);
        CHECK(ENGINES[e]->knob[2] == P_ED_FLT, "%s K3 not ENV", NAME[e]);
    }
    CHECK(ENGINES[ENGI_OBX]->knob[3] == P_E1, "OBX K4");
    CHECK(ENGINES[ENGI_WAVE]->knob[3] == P_E1, "WAVE K4");
    CHECK(ENGINES[ENGI_ACID]->knob[3] == P_E2, "ACID K4");
    CHECK(ENGINES[ENGI_DRIFT]->knob[3] == P_E6, "DRIFT K4");
    CHECK(ENGINES[ENGI_ACID]->poly == 1u, "ACID mono");
}

static void test_presets(void)
{
    uint32_t e, k;
    for (e = 0; e < NENGINES; e++) {
        CHECK(ENGINES[e]->npresets == 32u, "%s has %u presets", ENGINES[e]->name, ENGINES[e]->npresets);
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            uint32_t c = preset_cat(e, k);
            CHECK(c >= CAT_BASS && c < NCAT, "%s/%u no category", ENGINES[e]->name, (unsigned)k);
            CHECK((c == CAT_ACID) == (e == ENGI_ACID), "%s/%s: ACID only on ACID",
                  ENGINES[e]->name, ENGINES[e]->presets[k].name);
            CHECK(strlen(ENGINES[e]->presets[k].name) <= 12u, "name too long");
        }
    }
    CHECK(NELEM(WAVE_PX) == 32u, "WAVE_PX %u", (unsigned)NELEM(WAVE_PX));
    CHECK(NELEM(N_VOID_OSC2) == VOID_OSC2_N, "N_VOID_OSC2 has %u names for %u values", (unsigned)NELEM(N_VOID_OSC2),
          (unsigned)VOID_OSC2_N);
}

/* an engine that runs its own envelope (ACID): amp is constant, the voice ends when done says so, not when the ADSR would */
static int own_done, own_renders;
static int32_t own_amp0, own_amp1;
static int32_t own_amp(const track_t *t, const voice_t *v) { (void)t; (void)v; return 0x4000; }
static int own_isdone(const track_t *t, const voice_t *v) { (void)t; (void)v; return own_done; }
static void own_note_on(track_t *t, voice_t *v) { (void)t; (void)v; }
static void own_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n;
    own_renders++;
    own_amp0 = m->amp0;
    own_amp1 = m->amp1;
}

static void test_ownenv(void)
{
    static engine_t stub;
    const engine_t *saved = ENGINES[ENGI_ACID];
    track_t *t = &trk[0];
    int32_t o[CTL];
    uint32_t b, r0;
    stub = *ENGINES[ENGI_OBX];
    stub.ownenv = 1;
    stub.amp = own_amp;
    stub.done = own_isdone;
    stub.note_on = own_note_on;
    stub.render = own_render;
    stub.mem = 0;
    ENGINES[ENGI_ACID] = &stub;
    host_tracks_init();
    host_preset(t, ENGI_OBX, 5);
    t->engine = t->eng_req = ENGI_ACID;
    t->p[P_ATK] = 0;
    t->p[P_REL] = 0;                                    /* the fastest release: the ADSR would end it in a few blocks */
    t->p[P_LD_AMP] = 0;
    own_done = own_renders = 0;
    trk_note_on(t, 60, 100);
    for (b = 0; b < 8; b++)
        track_render(t, o, CTL);
    CHECK(own_renders == 8 && own_amp1 == 0x4000 && own_amp0 == 0x4000, "ownenv: amp %d/%d renders %d", (int)own_amp0, (int)own_amp1, own_renders);
    trk_note_off(t, 60);
    for (b = 0; b < 2000; b++)                          /* ~1.4 s past the note-off */
        track_render(t, o, CTL);
    CHECK(t->v[0].active && own_amp1 == 0x4000, "ownenv: ended with the track ADSR (active %d amp %d)", t->v[0].active, (int)own_amp1);
    own_done = 1;
    r0 = (uint32_t)own_renders;
    track_render(t, o, CTL);
    CHECK(!t->v[0].active && (uint32_t)own_renders == r0, "ownenv: not freed when done said so");
    ENGINES[ENGI_ACID] = saved;
}

int main(void)
{
    test_engine_table();
    test_presets();
    test_ownenv();
    printf(fails ? "four_test: %d FAIL\n" : "four_test: ok\n", fails);
    return fails != 0;
}

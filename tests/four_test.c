/* four_test.c: the four-machine contract */
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

int main(void)
{
    test_engine_table();
    printf(fails ? "four_test: %d FAIL\n" : "four_test: ok\n", fails);
    return fails != 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"length: line %d\n",__LINE__); return 1; } count++; } while (0)
int main(void)
{
    static const int lengths[] = {2,4,8,16,32,48,64,128};
    int i, count=0;
    const param_desc_t *d=&TP[P_SLEN];
    for(i=0;i<8;i++) {
        CHECK(param_turn(d,lengths[i],1)==lengths[i<7?i+1:7]);
        CHECK(param_turn(d,lengths[i],-1)==lengths[i?i-1:0]);
    }
    CHECK(param_turn(d,17,0)==17);
    CHECK(param_turn(d,17,1)==32);
    CHECK(param_turn(d,17,-1)==16);
    CHECK(param_turn(d,16,100)==128);
    CHECK(param_turn(d,16,-100)==2);
    ui_power_on(); go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);
    TSEL->p[P_SLEN]=16; turn(EN_K1,1);
    CHECK(TSEL->p[P_SLEN]==32);
    turn(EN_K1,1); CHECK(TSEL->p[P_SLEN]==48);
    turn(EN_K1,-1); CHECK(TSEL->p[P_SLEN]==32);
    btn_up(B_SEQ); frame();
    song.sel=3; set_engine_of(TSEL,ENGI_DRUM); frame(); go_page(GR_ROLL); frame();
    TSEL->p[P_SLEN]=16;
    dg_turn(2,1); CHECK(TSEL->p[P_SLEN]==32);
    dg_turn(2,1); CHECK(TSEL->p[P_SLEN]==48);
    dg_turn(2,1); CHECK(TSEL->p[P_SLEN]==64);
    dg_turn(2,-7); CHECK(TSEL->p[P_SLEN]==2);
    TSEL->p[P_SDIV]=2; TSEL->p[P_SSWING]=23;
    TSEL->step[0].hit=3;
    dg_turn(3,-2); CHECK(TSEL->p[P_SDIV]==1); /* 16 -> 8T -> 8 */
    CHECK(abs((int)div_samples(1)-2*(int)div_samples(2))<=1); /* integer sample rounding */
    CHECK(TSEL->p[P_SSWING]==23 && TSEL->step[0].hit==3);
    dg_turn(3,-1); CHECK(TSEL->p[P_SDIV]==0); /* quarter */
    CHECK(abs((int)div_samples(0)-4*(int)div_samples(2))<=3);
    dg_turn(3,-100); CHECK(TSEL->p[P_SDIV]==9); /* 4BAR */
    dg_turn(3,100); CHECK(TSEL->p[P_SDIV]==3); /* 1/32 */
    dg_turn(3,-1); CHECK(TSEL->p[P_SDIV]==5); /* 16T */
    btn_down(B_SEQ); frames(500);
    TSEL->p[P_SSWING]=23; turn(EN_K3,1);
    CHECK(TSEL->p[P_SSWING]==24);
    btn_up(B_SEQ); frame();
    printf("sequence length: %d checks passed\n",count); return 0;
}

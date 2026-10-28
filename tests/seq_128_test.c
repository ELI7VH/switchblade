/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"128: line %d: %s\n",__LINE__,#x); return 1; } count++; } while(0)
int main(void)
{
    static project_t p,q;
    static project_store_t wire;
    int count=0; uint32_t i, before;
    ui_power_on(); song.sel=3; set_engine_of(TSEL,ENGI_DRUM); frame(); go_page(GR_ROLL); frame();
    track_t *t=TSEL; track_defaults_steps(t); t->p[P_SLEN]=64;
    dg_turn(2,1); CHECK(t->p[P_SLEN]==128);
    dg_turn(1,7); CHECK(ui.bank==7 && ui.cursor==112);
    tap_key(DG_WHITE[15]); CHECK(t->step[127].hit==1 && !t->step[63].hit);
    key_down(DG_WHITE[15]); frame(); dg_turn(3,1); key_up(DG_WHITE[15]); frame();
    CHECK(t->step[127].acc==1 && t->p[P_SDIV]==2);
    CHECK(motion_set_lock(t,127,P_LEVEL,87)==0);
    CHECK(motion_count(t)==1 && motion.event[0].place==511);
    CHECK(motion_lock_steps_from(3,64)==((uint64_t)1<<63));
    motion_restore(t); motion_step(t,127,&motion,1); CHECK(t->p[P_LEVEL]==87); motion_restore(t);
    project_capture(&p); CHECK(proj_pack(&wire,&p));
    CHECK(proj_import(&q,&wire,sizeof wire));
    CHECK(q.t[3].p[P_SLEN]==128 && q.t[3].step[127].hit==1 && q.motion.event[0].place==511);
    CHECK(project_restore_runtime(&q)==0);
    t=TSEL; motion_clear(t); track_defaults_steps(t); t->p[P_SLEN]=128; t->p[P_SDIV]=2; t->p[P_SQNT]=1;
    for(i=0;i<128;i++) {t->step[i].time=ST_NOTE; t->step[i].hit=1;}
    seq_start(); before=vage;
    for(i=0;i<128u*div_samples(2)/CTL;i++)seq_tick(t,CTL);
    CHECK(vage-before==128); seq_stop();
    /* Exact final-step recording address does not alias step 63 or another track. */
    track_defaults_steps(t); t->seq_idx=127; t->seq_pos=0; t->rh_n=0;
    rec_note(t,36,90); CHECK(t->step[127].hit==1 && !t->step[63].hit);
    /* Synthetic old-format fixture, produced by the unmodified 31d0af6 firmware. */
    { unsigned char old[3840]; FILE *f=fopen("tests/fixtures/for4-64.bin","rb");
      CHECK(f && fread(old,1,sizeof old,f)==sizeof old); fclose(f);
      CHECK(proj_import(&q,old,sizeof old));
      CHECK(q.t[3].p[P_SLEN]==64 && q.t[3].step[63].hit==17 && q.t[3].step[63].acc==1);
      CHECK(q.motion.count==1 && q.motion.event[0].place==447 && q.motion.event[0].value==87);
      for(i=64;i<128;i++) CHECK(q.t[3].step[i].time==ST_REST && !q.t[3].step[i].hit);
      CHECK(proj_pack(&wire,&q) && proj_import(&p,&wire,sizeof wire));
      CHECK(p.t[3].step[63].hit==17 && p.motion.event[0].place==447);
    }
    /* Melodic editing above 64 must address the selected step, including DETAIL and locks. */
    ui_power_on(); t=TSEL; t->p[P_SLEN]=128; track_defaults_steps(t);
    t->step[127]=(step_t){{60},1,ST_NOTE,0,96};
    go_page(GR_ROLL); cursor_set(127); frame();
    CHECK(ui.bank==7 && ui.cursor==127);
    CHECK(motion_set_lock(t,127,P_LEVEL,87)==0);
    lock_clear(128); CHECK(!motion_lock_count(t));
    sd_turn(2,1); CHECK(step_nudge(&t->step[127])==1 && step_nudge(&t->step[63])==0);
    sd_reset(); CHECK(step_nudge(&t->step[127])==0);
    /* A 64-step phrase doubles across the old boundary with notes and automation intact. */
    t->p[P_SLEN]=64; t->step[0]=(step_t){{60},1,ST_NOTE,0,96};
    CHECK(motion_set_lock(t,0,P_LEVEL,87)==0);
    CHECK(tl_do(t,TL_DOUBLE)!=0 && t->p[P_SLEN]==128 && t->step[64].note[0]==60);
    {int16_t v=0;CHECK(motion_lock_get(t,64,P_LEVEL,&v) && v==87);}
    /* Chain preparation uses all 128 steps and widened automation addresses. */
    project_capture(&p); CHECK(proj_pack(&proj_slot[0],&p));
    chain_config.count=2; chain_config.row[0]=chain_row_of(0,1); chain_config.row[1]=chain_row_of(0,1);
    CHECK(chain_prepare()==0);
    CHECK(chain.source[0].step[0][64].note[0]==60 && chain.source[0].timing[0][0]==128);
    seq_stop();
    t->p[P_SDIV]=9; project_capture(&p); CHECK(proj_pack(&proj_slot[0],&p));
    {char name[13]; uint32_t lengths[NTRK]; CHECK(project_info(0,name,lengths,0,0)); CHECK(lengths[0]==98304u);}
    printf("128 steps: %d checks passed\n",count);return 0;
}

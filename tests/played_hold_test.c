/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"hold:%d %s\n",__LINE__,#x); return 1;} count++; } while(0)
static track_t *setup(void) {
 ui_power_on(); track_t *t=&trk[0]; track_defaults_steps(t);
 t->p[P_SLEN]=128;t->p[P_VOICE]=V_POLY;t->p[P_SQNT]=1;
 song.rec=1;seq_start();seq_tick(t,CTL);return t;
}
int main(void) {
 int count=0;track_t *t=setup();uint32_t p=div_samples(t->p[P_SDIV]);
 /* Release after 0.3 steps; onset stays snapped, GATE cannot round release. */
 t->seq_pos=p/5;rec_note(t,60,90);seq_tick(t,p*3/10);rec_release(t,60);
 uint32_t q=step_hold_q8(&t->step[0]);CHECK(q>=76&&q<=78);CHECK(t->step[0].time==ST_NOTE&&t->p[P_SQNT]);
 song.rec=0;t->p[P_SGATE]=1;seq_step(t,&t->step[0],0,p,SEQ_ROLLED);
 CHECK(t->seq_n==1&&t->seq_timed&&!t->seq_hold);uint32_t gate=t->seq_off;
 CHECK(gate>p/4&&gate<p/3);song.playing=0;seq_tick(t,gate-1);CHECK(t->seq_n==1);seq_tick(t,1);CHECK(!t->seq_n);
 /* A 1.3-step hold survives a tie and releases within it. */
 t=setup();p=div_samples(t->p[P_SDIV]);rec_note(t,60,90);seq_tick(t,p);seq_tick(t,p*3/10);rec_release(t,60);
 q=step_hold_q8(&t->step[0]);CHECK(q>=332&&q<=334);CHECK(t->step[1].time==ST_TIE);
 song.rec=0;seq_step(t,&t->step[0],0,p,SEQ_ROLLED);gate=t->seq_off;
 song.playing=0;seq_tick(t,p);uint32_t left=t->seq_off;seq_step(t,&t->step[1],1,p,SEQ_ROLLED);
 CHECK(t->seq_n&&t->seq_off==left&&left==gate-p);seq_tick(t,left);CHECK(!t->seq_n);
 /* Late half-step onset snaps forward, but elapsed hold remains unchanged. */
 t=setup();p=div_samples(t->p[P_SDIV]);t->seq_pos=p*3/4;rec_note(t,62,90);seq_tick(t,p/2);rec_release(t,62);
 CHECK(t->step[1].n==1&&step_hold_q8(&t->step[1])==128);
 /* Last release ends a chord; STOP finalizes a still-held note. */
 t=setup();p=div_samples(t->p[P_SDIV]);rec_note(t,60,90);rec_note(t,64,90);seq_tick(t,p/4);rec_release(t,60);
 CHECK(t->rh_n==1);seq_tick(t,p/4);rec_release(t,64);CHECK(step_hold_q8(&t->step[0])==128);
 rec_note(t,67,90);seq_tick(t,p/4);seq_stop();CHECK(step_hold_q8(&t->step[t->rh_start])==64&&!t->rh_n);
 /* Same size FOR6 persists holds; FOR5 importer drops old synth mask bytes. */
 project_t before,after;project_store_t wire;project_capture(&before);CHECK(proj_pack(&wire,&before));
 CHECK(proj_import(&after,&wire,sizeof wire)&&step_hold_q8(&after.t[0].step[0])==64);
 uint32_t magic=PROJ_PREV_MAGIC;memcpy(wire.raw,&magic,4);uint32_t hash=proj_hash(wire.raw,sizeof wire-4);memcpy(wire.raw+sizeof wire-4,&hash,4);
 CHECK(proj_import(&after,&wire,sizeof wire)&&!step_hold_q8(&after.t[0].step[0]));
 CHECK(note_set_len(t,0,2)==2&&!step_hold_q8(&t->step[0]));
 /* Swing: a hold longer than the short step must not be cut by the next REST. */
 t=setup();p=div_samples(t->p[P_SDIV]);t->p[P_SSWING]=50;t->seq_idx=1;t->seq_pos=0;
 rec_note(t,60,90);seq_tick(t,p*9/10);rec_release(t,60);
 CHECK(step_hold_q8(&t->step[1])>=229&&t->step[2].time==ST_TIE);
 /* Pattern wrap and full-length holds preserve a bounded release. */
 t=setup();p=div_samples(t->p[P_SDIV]);t->seq_idx=127;t->seq_pos=0;
 rec_note(t,60,90);seq_tick(t,p);seq_tick(t,p/4);rec_release(t,60);
 CHECK(t->step[0].time==ST_TIE&&step_hold_q8(&t->step[127])==320);
 t=setup();p=div_samples(t->p[P_SDIV]);rec_note(t,60,90);
 for(unsigned n=0;n<130;n++) seq_tick(t,p);
 CHECK(!t->rh_n&&step_hold_q8(&t->step[0])<=32768&&step_hold_q8(&t->step[0])>=32512);
 /* Drum masks, arp chord timers, and tempo-relative playback remain independent. */
 t=setup();rec_note(t,60,90);seq_tick(t,p/4);rec_release(t,60);
 step_set_hold_q8(&t->step[0],333);t->p[P_AMODE]=1;seq_step(t,&t->step[0],0,p*2,SEQ_ROLLED);
 CHECK(t->seq_arp&&t->seq_timed&&!t->seq_hold&&t->seq_off==((uint64_t)p*2*333+128)/256);
 t=setup();set_engine_of(t,ENGI_DRUM);t->engine=ENGI_DRUM;rec_note(t,42,120);seq_tick(t,p/3);rec_release(t,42);CHECK(t->step[0].hit==(1u<<drum_lane(42))&&t->step[0].acc==t->step[0].hit&&!t->rh_n);
 /* Repeated control blocks do not accumulate rounding drift at slow divisions. */
 t=setup();p=div_samples(t->p[P_SDIV]);rec_note(t,60,90);
 for(unsigned n=0;n<10000;n++)seq_tick(t,CTL);
 rec_release(t,60);CHECK(step_hold_q8(&t->step[0])==((uint64_t)10000*CTL*256+p/2)/p);
 /* Changing between synth and drum cannot reinterpret duration bytes as hits. */
 t=setup();step_set_hold_q8(&t->step[0],333);t->eng_req=ENGI_DRUM;
 for(unsigned n=0;n<8;n++){t->xf=0;engine_block(t);}
 CHECK(t->engine==ENGI_DRUM&&!t->step[0].hit&&!t->step[0].acc&&t->step_drum);
 t->step[0].hit=8;t->step[0].acc=8;t->eng_req=0;
 for(unsigned n=0;n<8;n++){t->xf=0;engine_block(t);}
 CHECK(!t->step[0].hit&&!t->step[0].acc&&!t->step_drum);
 printf("played holds: %d checks passed\n",count);return 0;
}

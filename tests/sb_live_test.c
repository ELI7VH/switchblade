/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"sb-live %d: %s\n",__LINE__,#x);return 1;}count++;}while(0)
static void reset_live(void) {
 ui_power_on();sb_repeat=sb_dirt=sb_delay=0;sb_filter=0;sb_echo_send=sb_echo_mix=0;sb_echo_tail=0;
 sb_dirt_wet=0;sb_dirt_phase=0;sb_dirt_last[0]=sb_dirt_last[1]=0;
 memset(&pf,0,sizeof pf);pf.src=pf.next=PF_N;pf.lc=PF_TOP;
 perf_held=perf_latched=0;perf_kill=0;memset((void*)perf_k,0,sizeof perf_k);sl_lent=0;
}
static void cc(uint32_t n,uint32_t v){midi_event(0xB0,15,n,v);}
int main(void) {
 int count=0;uint32_t i,b,j,k;static int32_t l[CTL],r[CTL];static project_t before,after;
 reset_live();project_capture(&before);
 CHECK(!perf_begin(CTL));
 cc(20,1);CHECK(sb_repeat_fx()==PF_R8);CHECK(wl_io[5]==KC_LIVE && wl_io[6]==1);
 cc(20,81);CHECK(sb_repeat_fx()==PF_R16);
 cc(20,113);CHECK(sb_repeat_fx()==PF_R32 && perf_len(PF_R32)==beat_samples()/8u);
 cc(20,124);CHECK(perf_len(PF_R32)==beat_samples()/16u);
 cc(20,0);CHECK(sb_repeat_fx()==PF_N);
 cc(23,0);CHECK(sb_filter==-100);cc(23,63);CHECK(!sb_filter);cc(23,64);CHECK(!sb_filter);cc(23,127);CHECK(sb_filter==100);
 cc(23,64);project_capture(&after);CHECK(!memcmp(&before,&after,sizeof before));
 song.g[G_ROUTE]=ROUTE_TRACK;for(i=0;i<NTRK;i++)mch_set(i,MCH_OFF);
 cc(21,50);CHECK(sb_dirt==50);cc(21,0);CHECK(!sb_dirt); /* master CC bypasses track routing */
 reset_live();cc(20,1);
 for(b=0;b<800;b++){perf_begin(CTL);for(i=0;i<CTL;i++)l[i]=r[i]=(int32_t)(10000*sin((b*CTL+i)*.11));perf_block(l,r,CTL);}
 CHECK(pf.src==PF_R8 && pf.mode==BM_LOOP && pf.state);
 cc(20,124);for(b=0;b<100;b++){perf_begin(CTL);perf_block(l,r,CTL);}CHECK(pf.src==PF_R32 && pf.len==beat_samples()/16u);
 cc(20,0);for(b=0;b<100;b++){perf_begin(CTL);perf_block(l,r,CTL);}CHECK(!sl_lent && pf.mode==BM_NONE);
 for(k=0;k<2;k++)for(j=0;j<4;j++) {
   double dry=0,wet=0;reset_live();sb_dirt=(uint8_t)(j==0?1:j==1?43:j==2?90:127);
   for(b=0;b<300;b++) {
    for(i=0;i<CTL;i++){l[i]=(int32_t)((k?12000:1000)*sin((b*CTL+i)*.071));r[i]=(int32_t)((k?10000:800)*sin((b*CTL+i)*.093));if(b>40)dry+=(double)l[i]*l[i]+(double)r[i]*r[i];}
    sb_dirt_block(l,r,CTL);if(b>40)for(i=0;i<CTL;i++)wet+=(double)l[i]*l[i]+(double)r[i]*r[i];
   }
   double db=10*log10(wet/dry);printf("sb-live: dirt %u, input %u: %.4f dB\n",sb_dirt,k,db);CHECK(fabs(db)<.15);
 }
 reset_live();sb_dirt=127;memset(l,0,sizeof l);memset(r,0,sizeof r);sb_dirt_block(l,r,CTL);for(i=0;i<CTL;i++)CHECK(!l[i]&&!r[i]);
 reset_live();cc(22,127);for(b=0;b<20;b++)perf_begin(CTL);CHECK(sb_echo_send>0 && sb_echo_mix>0);
 cc(22,0);for(b=0;b<30;b++)perf_begin(CTL);CHECK(!sb_echo_send && sb_echo_tail>0 && sb_echo_mix>0);
 cc(20,127);cc(21,127);cc(23,127);cc(121,0);CHECK(!sb_repeat&&!sb_dirt&&!sb_delay&&!sb_filter);
 printf("sb-live: %d checks passed\n",count);return 0;
}

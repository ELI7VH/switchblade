/* SPDX-License-Identifier: GPL-3.0-only */
/* Signal-level order checks through the same mixed-block finish as the device. */
#define SB_LIVE_TEST_NO_MAIN 1
#include "sb_live_test.c"
static int32_t result[2][128 * CTL * 2];
static void reset_chain(void)
{
 reset_live(); for(uint32_t t=0;t<NTRK;t++)pf.mg[t]=32768; memset(&fx,0,sizeof fx); memset(dly_buf,0,sizeof dly_buf);
 memset(cho_buf,0,sizeof cho_buf); rev_clear();
 dc_l=dc_r=dce_l=dce_r=0;lim_env=LIM_T;fx_lowcut=0;fx_usb_fixed=0;
 song.master_q12=MASTER_FULL;song.g[G_BPM]=120;song.g[G_DFDBK]=0;
 song.g[G_DTIME]=2;song.g[G_DMIX]=0;
}
static void feed(uint32_t b,int signal,int32_t *out)
{
 uint32_t i;int perf=perf_begin(CTL);
 for(i=0;i<CTL;i++) {
  mix_l[i]=signal?(int32_t)(9000*sin((b*CTL+i)*.77)):0;
  mix_r[i]=signal?(int32_t)(7000*sin((b*CTL+i)*.53)):0;
  send_c[i]=send_d[i]=send_r[i]=0;
 }
 mix_finish(out,CTL,perf);
}
static int frozen_dirt(void)
{
 int count=0;uint32_t run,b;static int32_t out[CTL*2];
 for(run=0;run<2;run++) {
  reset_chain();sb_dirt=127;sb_repeat=124;
  for(b=0;b<256;b++) {
   if(run && b==128)sb_dirt=0;
   feed(b,1,out);
   if(b==127)CHECK(pf.state && pf.w==32768);
   if(b>=128)memcpy(result[run]+(b-128)*CTL*2,out,sizeof out);
  }
 }
 CHECK(!memcmp(result[0],result[1],sizeof result[0]));
 puts("sb-chain: changing dirt after a stutter capture leaves the captured sound unchanged");return 0;
}
static int stutter_feeds_delay(void)
{
 int count=0;uint32_t b,i,dl;int nonzero=0;static int32_t out[CTL*2];
 reset_chain();sb_repeat=124;sb_delay=100;dl=delay_samples();
 for(b=0;b<100;b++)feed(b,1,out);
 CHECK(pf.state && pf.w==32768);CHECK(104u*CTL<dl);
 for(b=100;b<104;b++) {
  uint32_t at=fx.dly_w;
  feed(b,0,out);
  for(i=0;i<CTL;i++)nonzero|=dly_buf[(at+i)&(DLY_LEN-1u)]!=0;
 }
 CHECK(nonzero);
 puts("sb-chain: frozen stutter feeds the delay even after fresh input becomes silent");return 0;
}
static double echo_behind_frozen_silence(int filter)
{
 uint32_t b,i,dl,start,end;double energy=0;static int32_t out[CTL*2];
 reset_chain();sb_delay=100;sb_filter=(int8_t)filter;dl=delay_samples();
 for(b=0;b<100;b++)feed(b,0,out); /* live send and filter settled */
 for(;b<104;b++)feed(b,1,out); /* short high-frequency burst */
 sb_repeat=124;
 start=100u*CTL+dl;end=start+2048u;
 for(;b*CTL<end;b++) {
  feed(b,0,out);
  if(b*CTL>=start)for(i=0;i<CTL*2;i++)energy+=(double)out[i]*out[i];
 }
 return energy;
}
static int release_and_headroom(void)
{
 int count=0;uint32_t b,i;static int32_t out[CTL*2];
 reset_chain();sb_repeat=124;
 for(b=0;b<100;b++)feed(b,1,out);
 CHECK(sb_chain_prepared && pf.state);
 sb_repeat=0;
 for(;b<200;b++)feed(b,0,out);

 CHECK(!sb_chain_prepared && !pf.busy && !sl_lent);
 reset_chain();sb_repeat=127;sb_dirt=127;sb_delay=127;sb_filter=100;
 for(b=0;b<2000;b++) {
  int perf=perf_begin(CTL);
  for(i=0;i<CTL;i++) {mix_l[i]=(i&1)?65536:-65536;mix_r[i]=-mix_l[i];send_c[i]=send_d[i]=send_r[i]=0;}
  mix_finish(out,CTL,perf);
  for(i=0;i<CTL*2;i++)CHECK(out[i]>=-32767 && out[i]<=32767);
 }
 reset_chain();sb_repeat=127;sb_dirt=127;sb_delay=127;sb_filter=-100;
 for(b=0;b<2000;b++) {feed(b,0,out);for(i=0;i<CTL*2;i++)CHECK(!out[i]);}
 puts("sb-chain: release returns to idle; full strip stays bounded and silence stays silent");return 0;
}

int main(void)
{
 int count=0;double open,filtered;
 if(frozen_dirt() || stutter_feeds_delay() || release_and_headroom())return 1;
 open=echo_behind_frozen_silence(0);filtered=echo_behind_frozen_silence(-100);
 printf("sb-chain: echo after a silent stutter: open %.0f, filtered %.0f\n",open,filtered);
 CHECK(open>1000000);CHECK(filtered>0 && filtered<open*.25);
 puts("sb-chain: delay return survives a silent stutter; final filter processes the echo");
 puts("sb-chain: bitcrush > stutter > delay > filter verified");return 0;
}

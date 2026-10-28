/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-sb-live dirt: soft saturation plus symmetric bit reduction/sample hold.
 * Block energy matching is applied after the wet/dry blend. No saved master or
 * track gain changes. Silent/degenerate blocks fall back to dry, without a DC
 * floor or unbounded makeup. Scratch is shared main audio work, never recursive. */
static int32_t sb_dirt_wet, sb_dirt_last[2];
static uint32_t sb_dirt_phase;
static int32_t sb_dry[2][CTL] __attribute__((section(".pool")));
static __attribute__((noinline)) void sb_dirt_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i,c,peak=1,step,hold=1u+sb_dirt/18u;
    int32_t *b[2]={l,r}; float de=0.0f,we=0.0f;
    int32_t target=(int32_t)sb_dirt*258;
    for(i=0;i<n;i++)for(c=0;c<2;c++) {
        int32_t x=b[c][i];uint32_t a=(uint32_t)(x<0?-x:x);
        sb_dry[c][i]=x;if(a>peak)peak=a;
        de+=(float)x*(float)x;
    }
    step=peak>>(11u-(uint32_t)sb_dirt*6u/127u);if(!step)step=1;
    for(i=0;i<n;i++) {
        sb_dirt_wet+=clamp(target-sb_dirt_wet,-256,256);
        if(++sb_dirt_phase>=hold) {
            sb_dirt_phase=0;
            for(c=0;c<2;c++) {
                int32_t x=clamp(b[c][i],-262143,262143);
                int32_t drive=256+(int32_t)sb_dirt*14;
                int32_t y=softclip((x*drive)>>8);
                int32_t a=y<0?-y:y;
                a=((a+(int32_t)step/2)/(int32_t)step)*(int32_t)step;
                sb_dirt_last[c]=y<0?-a:a;
            }
        }
        for(c=0;c<2;c++) {
            int32_t y=sb_dry[c][i]+(int32_t)(((int64_t)(sb_dirt_last[c]-sb_dry[c][i])*sb_dirt_wet)>>15);
            b[c][i]=y;we+=(float)y*(float)y;
        }
    }
    if(de<1.0f || we<1.0f) {for(i=0;i<n;i++)for(c=0;c<2;c++)b[c][i]=sb_dry[c][i];return;}
    {float g=fm_sqrtf(de/we);if(g>8.0f)g=8.0f;
     for(i=0;i<n;i++)for(c=0;c<2;c++)b[c][i]=(int32_t)((float)b[c][i]*g);}
}

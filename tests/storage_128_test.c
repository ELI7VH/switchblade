/* SPDX-License-Identifier: GPL-3.0-only */
#define main storage_base_main
#include "storage_test.c"
#undef main
int main(void)
{
    static uint8_t a[6400], b[6400], got[6400], snapshot[4][4096];
    uint32_t off[4], k, c, cut; int ok=1;
    memset(nor,255,sizeof nor);
    for(k=0;k<sizeof a;k++){a[k]=(uint8_t)(k*13u); b[k]=(uint8_t)(k*17u+1u);}
    for(k=0;k<17;k++)for(c=0;c<2;c++) {
        uint32_t obj=k<4?OBJ_PROJECT0+k:k==4?OBJ_AUTOSAVE:OBJ_SONG0+k-5;
        uint32_t x=st_extra(obj,c);
        ok &= x>=0xB8000 && x<0xDA000;
        for(uint32_t j=0;j<k*2+c;j++) {
            uint32_t q=j/2, other=q<4?OBJ_PROJECT0+q:q==4?OBJ_AUTOSAVE:OBJ_SONG0+q-5;
            ok &= x!=st_extra(other,j%2);
        }
    }
    if(!ok)return 1;
    if(st_save(OBJ_SONG0,a,sizeof a))return 2;
    off[0]=st_sector(OBJ_SONG0,0);off[1]=st_sector(OBJ_SONG0,1);
    off[2]=st_extra(OBJ_SONG0,0);off[3]=st_extra(OBJ_SONG0,1);
    for(k=0;k<4;k++)memcpy(snapshot[k],nor+off[k],4096);
    for(cut=0;cut<sizeof b+sizeof(st_hdr_t);cut++) {
        for(k=0;k<4;k++)memcpy(nor+off[k],snapshot[k],4096);
        fail_bytes=(int)cut;
        if(st_save(OBJ_SONG0,b,sizeof b)==0)return 3;
        fail_bytes=-1;
        if(st_load(OBJ_SONG0,got,sizeof got)!=sizeof a || memcmp(a,got,sizeof a))return 4;
    }
    if(st_save(OBJ_SONG0,b,sizeof b) || st_load(OBJ_SONG0,got,sizeof got)!=sizeof b || memcmp(b,got,sizeof b))return 5;
    /* Corrupt the new extension: CRC must fall back to the complete old copy. */
    nor[st_extra(OBJ_SONG0,1)]^=1;
    if(st_load(OBJ_SONG0,got,sizeof got)!=sizeof a || memcmp(a,got,sizeof a))return 6;
    if(st_save(OBJ_SETTINGS,b,sizeof b)==0 || st_save(OBJ_UPRESET0,b,sizeof b)==0)return 7;
    printf("extended storage: 34 unique extension sectors; %u power cuts preserve old data; CRC fallback passes\n",cut);
    return 0;
}

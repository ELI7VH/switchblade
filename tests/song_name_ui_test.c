/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"name-ui:%d %s\n",__LINE__,#x);return 1;}count++;}while(0)
int main(void) {
 int count=0;ui_power_on();song.playing=0;sl_cur=0;proj_name[0]=0;go_page(GR_SONGNAME);frame();
 CHECK(name_on()&&nm.kind==NK_SONG&&!nm.dirty&&!nm.saved_ok);
 for(unsigned i=0;i<NPAGES;i++)if(PAGES[i].graph==GR_SLOTS)CHECK(!page_navigable(i));
 turn(EN_K2,1);CHECK(nm.dirty&&str_eq(nm.s,"A"));uint32_t edited=nm.edited;
 frames(2000);turn(EN_K2,1);CHECK(nm.edited>edited&&str_eq(nm.s,"B"));
 frames(2000);CHECK(nm.dirty&&nm.edited>edited);edited=nm.edited;
 frames(1200);CHECK(nm.dirty&&nm.edited>edited&&str_eq(nm.s,"B")); /* RAM-only test refuses flash; draft stays */
 song.playing=1;edited=nm.edited;frames(3200);CHECK(nm.dirty&&nm.edited==edited&&name_on());song.playing=0;
 nm.dirty=0;press(B_SAVE);CHECK(!name_on()&&cur_page()->graph==GR_TOOLS);
 go_page(GR_SONGNAME);frame();
 FILE *f=fopen("build/song-name.ppm","wb");fprintf(f,"P6\n240 240\n255\n");
 for(unsigned n=0;n<240*240;n++){uint16_t c=host_screen[n];unsigned char b[3]={(c>>11)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(b,1,3,f);}fclose(f);
 printf("song naming UI: %d checks passed\n",count);return 0;
}

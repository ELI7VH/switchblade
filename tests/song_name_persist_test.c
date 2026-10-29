/* SPDX-License-Identifier: GPL-3.0-only */
#define PERSISTENCE_TEST_NO_MAIN 1
#include "persistence_test.c"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"song-name:%d %s\n",__LINE__,#x);return 1;}count++; }while(0)
int main(void) {
 int count=0;project_t q;project_store_t raw;
 reset();sl_cur=0;sl_used=0;song.playing=0;fm1_in.notes=fm1_in.buttons=0;
 for(unsigned t=0;t<NTRK;t++)for(unsigned v=0;v<NVOICE;v++)trk[t].v[v].active=0;
 trk[0].p[P_SLEN]=128;trk[0].step[127]=(step_t){{72},1,ST_NOTE,0,96};
 CHECK(!song_name_save("LIVE SET"));CHECK(sl_has(0)&&str_eq(proj_name,"LIVE SET"));
 CHECK(st_load(OBJ_SONG0,&raw,sizeof raw)==sizeof raw&&proj_import(&q,&raw,sizeof raw));
 CHECK(str_eq(q.name,"LIVE SET")&&q.t[0].p[P_SLEN]==128&&q.t[0].step[127].note[0]==72);
 CHECK(st_load(OBJ_AUTOSAVE,&raw,sizeof raw)==sizeof raw&&proj_import(&q,&raw,sizeof raw));CHECK(q.rsv==1&&str_eq(q.name,"LIVE SET"));
 uint32_t prior=erases;song.playing=1;CHECK(song_name_save("WAIT")&&erases==prior&&str_eq(proj_name,"LIVE SET"));song.playing=0;
 erase_error=1;CHECK(song_name_save("RETRY")&&str_eq(proj_name,"LIVE SET"));erase_error=0;
 CHECK(!song_name_save("RETRY")&&str_eq(proj_name,"RETRY"));
 song_dupe();CHECK(sl_cur==1&&sl_has(0)&&sl_has(1)&&!proj_name[0]);
 CHECK(!song_name_save("SECOND"));song_step(-1);CHECK(sl_cur==0&&str_eq(proj_name,"RETRY"));
 song_step(1);CHECK(sl_cur==1&&str_eq(proj_name,"SECOND"));
 CHECK(project_used(0)==0&&project_used(1)==0); /* no dependency on A-D */
 printf("song naming persistence: %d checks passed\n",count);return 0;
}

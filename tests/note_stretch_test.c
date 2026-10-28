/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"stretch: %d: %s\n",__LINE__,#x);return 1;}count++;}while(0)
static void start(uint32_t cursor) {
 ui_power_on(); TSEL->p[P_SLEN]=128; TSEL->p[P_VOICE]=V_POLY; track_defaults_steps(TSEL);
 go_page(GR_ROLL); song.rec=1; cursor_set(cursor); frame();
}
static void midi_hit(uint32_t st,uint32_t note,uint32_t vel) {
 midi_enqueue((st>>4)|st<<8|note<<16|vel<<24,1);events_block(CTL);frame();
}
int main(void) {
 int count=0; track_t *t;
 start(3);t=TSEL;key_down(7);frame(); CHECK(ui.entry_open && ui.cursor==3);
 turn(EN_K1,3);CHECK(ui.cursor==3 && note_len(t,3)==4);
 CHECK(t->step[4].time==ST_TIE && t->step[6].time==ST_TIE && !motion.count);
 key_up(7);frame();CHECK(ui.cursor==7 && !ui.entry_open);
 turn(EN_K1,1);CHECK(ui.cursor==8); /* no key held: K1 still navigates */
 start(14);t=TSEL;key_down(7);key_down(11);frame();turn(EN_K1,4);
 CHECK(t->step[14].n==2 && note_len(t,14)==5 && ui.cursor==14);
 key_up(7);frame();CHECK(ui.cursor==14);key_up(11);frame();CHECK(ui.cursor==19 && ui.bank==1);
 start(3);t=TSEL;key_down(7);frame();turn(EN_K1,5);turn(EN_K1,-3);
 CHECK(note_len(t,3)==3 && t->step[6].time==ST_REST && t->step[8].time==ST_REST);
 key_up(7);frame();CHECK(ui.cursor==6);
 start(125);t=TSEL;key_down(7);frame();turn(EN_K1,20);
 CHECK(note_len(t,125)==3 && ui.cursor==125);key_up(7);frame();CHECK(ui.cursor==0);
 start(3);t=TSEL;t->step[6]=(step_t){{72},1,ST_NOTE,0,96};key_down(7);frame();turn(EN_K1,20);
 CHECK(note_len(t,3)==3 && t->step[6].note[0]==72);key_up(7);frame();CHECK(ui.cursor==6);
 start(3);t=TSEL;key_down(7);frame();key_up(7);frame();CHECK(ui.cursor==4);
 /* External MIDI holds use the same anchor and release advance. */
 start(64);t=TSEL;midi_hit(0x90,60,90);midi_hit(0x90,64,90);
 CHECK(ui.entry_open && t->step[64].n==2);
 turn(EN_K1,3);CHECK(ui.cursor==64 && note_len(t,64)==4);
 midi_hit(0x80,60,0);CHECK(ui.cursor==64 && ui.entry_open);
 midi_hit(0x80,64,0);CHECK(ui.cursor==68 && !ui.entry_open);
 start(0);ui_prefs2|=PREF_CHORD_ADD>>16;key_down(7);frame();turn(EN_K1,2);
 key_up(7);frame();CHECK(ui.cursor==3); /* explicit length gesture advances even in ADD mode */
 /* Unarmed keys and drum hits never become stretched synth notes. */
 start(0);song.rec=0;key_down(7);frame();turn(EN_K1,1);CHECK(ui.cursor==1 && !TSEL->step[0].n);key_up(7);frame();
 start(0);song.sel=3;set_engine_of(TSEL,ENGI_DRUM);frame();go_page(GR_ROLL);song.rec=8;frame();
 key_down(DG_WHITE[0]);frame();turn(EN_K1,1);CHECK(!entry_len_on());key_up(DG_WHITE[0]);frame();
 printf("note stretch: %d checks passed\n",count);return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* Real panel, sequencer and FOR4 paths: lane editing, overdubbing, gates and storage. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "drum seq: line %d: %s\n", __LINE__, #x); return 1; } count++; } while (0)
int main(void)
{
    uint32_t count = 0, i, n;
    static project_t p, q;
    static project_store_t wire;
    ui_power_on(); song.sel = 3; set_engine_of(TSEL, ENGI_DRUM); frame();
    track_t *t = TSEL;
    track_defaults_steps(t); t->p[P_SLEN] = 64; t->p[P_SDIV] = 2;
    go_page(GR_ROLL); frame();
    REQUIRE(drum_grid_on() && song.grid == 5u);
    for (i = 0; i < 8u; i++) { tap_key(DG_BLACK[i]); tap_key(DG_WHITE[0]); }
    REQUIRE(t->step[0].hit == 255 && t->step[0].n == 0);
    REQUIRE(dg_lane() == 7);
    n = vage;
    keyboard_block();
    REQUIRE(vage == n);                         /* panel steps never audition/record pitches */
    seq_step(t, &t->step[0], 0, div_samples(2), 0);
    REQUIRE(t->seq_n == 8 && vage - n == 8);
    seq_release(t); REQUIRE(t->seq_n == 0);
    tap_key(DG_BLACK[0]); tap_key(DG_WHITE[0]);
    REQUIRE(t->step[0].hit == 254);             /* remove only the kick */
    tap_key(DG_WHITE[0]);
    key_down(DG_WHITE[0]); frame(); REQUIRE(mf_busy()); dg_turn(3, 1); key_up(DG_WHITE[0]); frame();
    REQUIRE(t->step[0].hit == 255 && t->step[0].acc == 1);
    key_down(DG_WHITE[0]); frame(); dg_turn(2, -20); key_up(DG_WHITE[0]); frame();
    REQUIRE(t->step[0].hit == 255 && t->step[0].vel == 76);
    dg_turn(1, 3); tap_key(DG_WHITE[15]);
    REQUIRE(ui.bank == 3 && ui.cursor == 63 && t->step[63].hit == 1);
    dg_turn(2, -2); REQUIRE(t->p[P_SLEN] == 32 && ui.bank == 1 && ui.cursor == 31);
    cursor_set(16);
    t->step[16].n = 2; t->step[16].note[0] = 49; t->step[16].note[1] = 38; t->step[16].time = ST_NOTE;
    tap_key(DG_BLACK[7]); tap_key(DG_WHITE[0]);
    REQUIRE(t->step[16].n == 1 && t->step[16].note[0] == 38); /* old crash removes only BELL */
    t->p[P_E0] = DK_909;
    project_capture(&p);
    REQUIRE(proj_pack(&wire, &p) && proj_import(&q, &wire, sizeof wire));
    REQUIRE(q.t[3].step[0].hit == 255 && q.t[3].step[0].acc == 1 && q.t[3].step[0].vel == 76);
    REQUIRE(project_restore_runtime(&q) == 0 && trk[3].step[0].hit == 255);
    t = &trk[3];
    /* Old note-only FOR4s round trip and play identically. */
    track_defaults_steps(t); t->p[P_SLEN] = 16; t->p[P_SDIV] = 2; t->p[P_AMODE] = 0;
    t->step[0] = (step_t){{36,38,49,48},4,ST_NOTE,0,90};
    project_capture(&p); REQUIRE(proj_pack(&wire,&p) && proj_import(&q,&wire,sizeof wire));
    REQUIRE(q.t[3].step[0].n == 4 && !q.t[3].step[0].hit && q.t[3].step[0].note[2] == 49);
    n = vage; seq_step(t,&t->step[0],0,div_samples(2),0); REQUIRE(vage - n == 4); seq_release(t);
    /* Canonical note plus mask cannot retrigger twice. */
    t->step[0].hit = 1; n = vage; seq_step(t,&t->step[0],0,div_samples(2),0);
    REQUIRE(vage - n == 4); seq_release(t);
    /* Eight pads overdub the same quantised step; holding them creates no ties. */
    track_defaults_steps(t); t->p[P_SLEN] = 16; t->p[P_SDIV] = 2;
    t->seq_idx = 0; t->seq_pos = 0; t->rh_n = 0;
    for (i = 0; i < 8u; i++) rec_note(t,DRUM_LANE_NOTE[i],i == 0 ? 127 : 85);
    REQUIRE(t->step[0].hit == 255 && t->step[0].acc == 1 && t->rh_n == 0);
    REQUIRE(!(t->step[0].flags & SF_ACCENT) && t->step[0].vel == 85);
    rec_hold(t,1,16); REQUIRE(t->step[1].time != ST_TIE);
    /* Failed chance, ratchets and gates use the normal transport. */
    step_set_chance(&t->step[0],0); n = vage;
    seq_step(t,&t->step[0],0,div_samples(2),0); REQUIRE(vage == n);
    step_set_chance(&t->step[0],100); step_set_ratchet(&t->step[0],4);
    seq_step(t,&t->step[0],0,div_samples(2),0); REQUIRE(t->rat_left == 3 && t->seq_n == 8);
    seq_stop(); REQUIRE(t->seq_n == 0);
    /* Transport loops the full mask, with one trigger per beat and no UI-focus theft. */
    track_defaults_steps(t); t->p[P_SLEN] = 16; t->p[P_SDIV] = 2; t->p[P_SSWING] = 0;
    t->p[P_SQNT] = 1; t->p[P_SGATE] = 64;
    for (i = 0; i < 16; i++) { t->step[i].time = ST_NOTE; t->step[i].hit = 3; }
    song.rec = 0;
    seq_start(); n = vage;
    for (i = 0; i < 16u * div_samples(2) / CTL; i++) seq_tick(t,CTL);
    REQUIRE(vage - n == 32); seq_stop();
    /* Notes overdubbed ahead of the playhead are not hit again when it catches up. */
    track_defaults_steps(t); t->p[P_SLEN] = 16; t->p[P_SDIV] = 2;
    t->seq_idx = 0; t->seq_pos = div_samples(2) * 3u / 4u;
    t->nd_fired = t->nd_late = t->rskip_n = 0;
    for (i = 0; i < 8; i++) rec_note(t,DRUM_LANE_NOTE[i],90);
    REQUIRE(t->step[1].hit == 255 && t->rskip_n == 8);
    n = vage; seq_play(t,1,div_samples(2)); REQUIRE(vage == n && !t->rskip_n);
    seq_play(t,1,div_samples(2)); REQUIRE(vage - n == 8); seq_release(t);
    /* Failed lanes do not destroy another lane's probability/ratchet on a re-toggle. */
    step_set_chance(&t->step[0], 50); dg_add(&t->step[0],0); dg_remove(&t->step[0],0); dg_add(&t->step[0],0);
    REQUIRE(step_chance(&t->step[0]) == 50);
    /* A changed drum bank does not replace the rhythm. */
    n = t->step[0].hit; apply_preset_to(t, DK_CR78); REQUIRE(t->step[0].hit == n);
    /* Synth STEP is still the piano roll with ordinary step entry. */
    song.sel = 0; go_page(GR_ROLL); frame(); REQUIRE(!drum_grid_on() && song.grid == 0);
    printf("drum sequencer: %u checks passed\n",count);
    return 0;
}

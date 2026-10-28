/* SPDX-License-Identifier: GPL-3.0-only */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"
int main(void) {
 static project_t a,b;uint32_t n;uint8_t extra=0;
 reset();project_capture(&a);
 midi_event(0xB0,15,20,127);midi_event(0xB0,15,21,71);midi_event(0xB0,15,22,43);midi_event(0xB0,15,23,0);
 n=request(82,0,0);
 if(n!=12 || host_wire[5]!=1 || host_wire[6]!=127 || host_wire[7]!=71 || host_wire[8]!=43 ||
    (int32_t)(host_wire[9]|host_wire[10]<<7)-8192!=-100)return 1;
 project_capture(&b);if(memcmp(&a,&b,sizeof a))return 2;
 if(request(82,&extra,1))return 3;
 puts("sb-live protocol: actual master values, strict read-only request, project unchanged");return 0;
}

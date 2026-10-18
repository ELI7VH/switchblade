// SPDX-License-Identifier: GPL-3.0-only
// The host reference of DRIFT's chorus: the Ultramaster KR-106's own kr106::Chorus (GPL-3.0, kayrockscreenprinting), unmodified, run over
// tests/chorus_ref_cfg.h's cases and written as build/chorus_ref/NN.f32 (float32, N frames of L, R). tests/chorus_test.c holds
// firmware/src/dsp_chorus.c to these.
//   c++ -std=c++17 -O2 -ffp-contract=off -w -I"../ultramaster_kr106-main/Source/DSP" -Itests -o build/host/chorus_ref tests/chorus_ref.cpp
//   mkdir -p build/chorus_ref && build/host/chorus_ref build/chorus_ref
// The injected noise (pink floor, leakage, rail ripple, the clock-reset clicks and their ring) is the donor's own to switch off
// (mAnalogMul, mMainsMul, mClockMul = 0): the port does not model it, so the reference does not either. -ffp-contract=off: the
// FM-1 has no fused multiply-add and the reference must not fuse either.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "KR106Chorus.h"
#include "chorus_ref_cfg.h"

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : "build/chorus_ref";
  for (int c = 0; c < CHORUS_REF_NCFG; c++)
  {
    const chorus_ref_cfg_t &k = CHORUS_REF_CFG[c];
    kr106::Chorus ch;
    ch.mAnalogMul = 0.f;
    ch.mMainsMul = 0.f;
    ch.mClockMul = 0.f;
    ch.Init((float)CHORUS_REF_FS);
    ch.SetMode(k.mode);
    std::vector<float> out((size_t)k.n * 2);
    for (uint32_t i = 0; i < k.n; i++)
    {
      float l, r;
      ch.Process(chorus_ref_input(&k, i), l, r);
      out[2 * (size_t)i] = l;
      out[2 * (size_t)i + 1] = r;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%02d.f32", dir, c);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "chorus_ref: cannot write %s\n", path); return 1; }
    fwrite(out.data(), sizeof(float), out.size(), f);
    fclose(f);
  }
  printf("chorus_ref: %d cases in %s\n", CHORUS_REF_NCFG, dir);
  return 0;
}

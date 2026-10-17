// SPDX-License-Identifier: GPL-3.0-only
// The host reference of DRIFT's filter: the Ultramaster KR-106's own kr106::VCF (GPL-3.0, kayrockscreenprinting), unmodified,
// run over tests/drift_ref_cfg.h's cases and written as build/drift_ref/NN.f32 (float32, N * os samples, at the oversampled
// rate). tests/drift_test.c (a) holds firmware/src/dsp_va.c to these.
//   c++ -std=c++17 -O2 -ffp-contract=off -w -I"../ultramaster_kr106-main/Source/DSP" -Itests -o build/host/drift_ref tests/drift_ref.cpp
//   mkdir -p build/drift_ref && build/host/drift_ref build/drift_ref
// -ffp-contract=off on both sides: the FM-1 has no fused multiply-add, so the reference must not fuse either or the two
// would not agree. The drive order is the voice's (KR106Voice.h): per base sample TrackInputEnv(peak of the previous base
// sample's oscillator output), UpdateCoeffs, then ProcessSample once per oversampled sample.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "KR106VCF_OPTIMIZED.h"
#include "drift_ref_cfg.h"

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : "build/drift_ref";
  for (int c = 0; c < DRIFT_REF_NCFG; c++)
  {
    const drift_ref_cfg_t &k = DRIFT_REF_CFG[c];
    kr106::VCF vcf;
    vcf.mJ106Res = true;
    vcf.SetSampleRate((float)DRIFT_REF_FS);
    vcf.SetOversample(k.os);
    vcf.Reset();
    const float frq = k.hz / ((float)DRIFT_REF_FS * 0.5f);
    std::vector<float> out((size_t)DRIFT_REF_N * k.os);
    float peak = 0.f;
    for (int n = 0; n < DRIFT_REF_N; n++)
    {
      vcf.TrackInputEnv(peak);
      vcf.UpdateCoeffs(frq, k.res);
      peak = 0.f;
      for (int j = 0; j < k.os; j++)
      {
        float x = (float)drift_ref_input((unsigned)(n * k.os + j), k.f0, k.os);
        float a = fabsf(x);
        if (a > peak) peak = a;
        out[(size_t)n * k.os + j] = vcf.ProcessSample(x * k.drv);
      }
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%02d.f32", dir, c);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "drift_ref: cannot write %s\n", path); return 1; }
    fwrite(out.data(), sizeof(float), out.size(), f);
    fclose(f);
  }
  printf("drift_ref: %d cases in %s\n", DRIFT_REF_NCFG, dir);
  return 0;
}

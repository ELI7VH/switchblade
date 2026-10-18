/* SPDX-License-Identifier: GPL-3.0-only */
/* STUB (the RED commit of Task 6.2): the API the tests are written against, a pass-through. Replaced by the port in the next commit. */
#ifndef DSP_CHORUS_C
#define DSP_CHORUS_C
#include <stdint.h>
#include "fastmath.h"
#define CHORUS_TRIM 0.6567f              /* 1 / sqrt(kDry^2 + kWet^2) */
#define CHORUS_GUARD_FS 6
#define CHORUS_LEN 256u
typedef struct { float pad; } chorus_t;
static int chorus_block(chorus_t *c, uint32_t mode, const float *in, float *l, float *r, uint32_t n)
{
    uint32_t i;
    (void)c, (void)mode;
    for (i = 0; i < n; i++)
        l[i] = r[i] = in[i];
    return 0;
}
static int chorus_part(chorus_t *c, int32_t mode, int32_t *b, int32_t *z, uint32_t n)
{
    (void)c, (void)mode, (void)b, (void)z, (void)n;
    return 0;
}
#endif

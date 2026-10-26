# x0x native fp32 and 909 sources

Imported from Charles Vestal's fm1-x0x, commit
`201e5c5c1ac056a028a006bd1eabb23ac1bb174d`:
https://github.com/charlesvestal/fm1-x0x/tree/201e5c5c1ac056a028a006bd1eabb23ac1bb174d

GPL-3.0-only. x0x credits 9W9 by athousanddetails, derived from ER99 by
Matthew Cieplak, for the 909 models. Preserve the original notices in these files.
`fastmath.h` contains x0x's pi32v2-friendly math substrate. The build enables
`-mfprev1`; both cores disable floating-point exception traps while retaining
stack protection, matching x0x's arithmetic requirements.

Melodee changes the instrument identifiers, integrates its shared eight-voice
allocator, adds per-sound and per-hit tuning/decay, and replaces the sampled
metallic voices with oscillator/noise synthesis. The latter is a timbral
approximation, not a reproduction of the original 909 sample ROM. Generated
tables contain mathematical parameter curves and tanh values only.

## WaveLoop Switch Blade

Copied from the WaveLoop Melodee fork (`waveloop/main`, aaaf5e4) into Switch Blade's DRUM machine as its KIT 909
(`firmware/src/eng_drum.c`): `drum909.c`, `drum909.h`, `drum909_dsp.h`, `x0x_param.h` as they are. `fastmath.h` is not
copied again: `firmware/src/fastmath.h` is the same x0x substrate (ACID's and DRIFT's maths). The tables come from
`tools/gen_909_tables.py` (Melodee's, plus the tanh table Melodee borrows from its Prophet engine: Switch Blade has none),
the tanh table in RAM (.data). The build already had `-mfprev1 -ffp-contract=off`; `hal/fm1_irq.h` now turns the FPU's
exception traps off as well (EMU_CON bits 16..20, as here). Switch Blade runs one core: the 909 renders in the audio
interrupt like every voice, per voice and per 32-sample block (`d9_voice_render`), never in its per-sample loop.

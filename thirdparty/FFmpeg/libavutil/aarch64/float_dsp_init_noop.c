// No-op stub for macOS ARM64 where NEON assembly can't be compiled (ELF .S syntax).
// Generic C float DSP code paths are used instead.
#include "libavutil/float_dsp.h"
void ff_float_dsp_init_aarch64(AVFloatDSPContext *fdsp) { (void)fdsp; }

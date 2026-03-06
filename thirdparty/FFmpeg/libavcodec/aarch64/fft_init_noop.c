// No-op stub for macOS ARM64 where NEON assembly can't be compiled (ELF .S syntax).
// Generic C FFT code paths are used instead.
#include "libavcodec/fft.h"
void ff_fft_init_aarch64(FFTContext *s) { (void)s; }

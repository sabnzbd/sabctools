#include "common.h"

// slightly faster version which improves the worst case scenario significantly; since worst case doesn't happen often, overall speedup is relatively minor
// requires PSHUFB (SSSE3) instruction, but will use POPCNT (SSE4.2 (or AMD's ABM, but Phenom doesn't support SSSE3 so doesn't matter)) if available (these only seem to give minor speedups, so considered optional)
#ifdef __SSSE3__
#include "encoder_sse_base.h"

void encoder_ssse3_init() {
    _do_encode = &do_encode_simd< do_encode_sse<ISA_LEVEL_SSSE3> >;
    encoder_sse_lut<ISA_LEVEL_SSSE3>();
}
#else

void encoder_sse2_init();

void encoder_ssse3_init() {
    encoder_sse2_init();
}

#endif


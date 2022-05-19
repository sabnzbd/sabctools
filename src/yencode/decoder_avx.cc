#include "common.h"

#if defined(__AVX__) && defined(__POPCNT__)
#include "decoder_common.h"
#include "decoder_sse_base.h"
void decoder_set_avx_funcs() {
    decoder_sse_init();
    decoder_init_lut(lookups->eqFix, lookups->compact);
    _do_decode = &do_decode_simd<false, false, sizeof(__m128i)*2, do_decode_sse<false, false, ISA_LEVEL_SSE4_POPCNT> >;
    _do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i)*2, do_decode_sse<true, false, ISA_LEVEL_SSE4_POPCNT> >;
    _do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i)*2, do_decode_sse<true, true, ISA_LEVEL_SSE4_POPCNT> >;
    _decode_simd_level = ISA_LEVEL_AVX;
}
#else

void decoder_set_ssse3_funcs();

void decoder_set_avx_funcs() {
    decoder_set_ssse3_funcs();
}

#endif

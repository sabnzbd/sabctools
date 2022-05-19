#include "common.h"

#if defined(__AVX2__) && !defined(YENC_DISABLE_AVX256)
#include "decoder_common.h"
#include "decoder_avx2_base.h"
void decoder_set_avx2_funcs() {
    ALIGN_ALLOC(lookups, sizeof(*lookups), 16);
    decoder_init_lut(lookups->eqFix, lookups->compact);
    _do_decode = &do_decode_simd<false, false, sizeof(__m256i)*2, do_decode_avx2<false, false, ISA_LEVEL_AVX2> >;
    _do_decode_raw = &do_decode_simd<true, false, sizeof(__m256i)*2, do_decode_avx2<true, false, ISA_LEVEL_AVX2> >;
    _do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m256i)*2, do_decode_avx2<true, true, ISA_LEVEL_AVX2> >;
    _decode_simd_level = ISA_LEVEL_AVX2;
}
#else

void decoder_set_avx_funcs();

void decoder_set_avx2_funcs() {
    decoder_set_avx_funcs();
}

#endif

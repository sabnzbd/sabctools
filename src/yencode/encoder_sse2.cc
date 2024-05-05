#include "common.h"

#ifdef __SSE2__
#include "encoder_sse_base.h"

void encoder_sse2_init() {
	RapidYenc::_do_encode = &do_encode_simd< do_encode_sse<ISA_LEVEL_SSE2> >;
	encoder_sse_lut<ISA_LEVEL_SSE2>();
	RapidYenc::_encode_isa = ISA_LEVEL_SSE2;
}
#else
void encoder_sse2_init() {}
#endif


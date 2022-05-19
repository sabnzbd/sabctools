#ifndef __YENC_DECODER_H
#define __YENC_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif


// the last state that the decoder was in (i.e. last few characters processed)
// the state is needed for incremental decoders as its behavior is affected by what it processed last
// acronyms: CR = carriage return (\r), LF = line feed (\n), EQ = equals char, DT = dot char (.)
typedef enum {
    YDEC_STATE_CRLF, // default
    YDEC_STATE_EQ,
    YDEC_STATE_CR,
    YDEC_STATE_NONE,
    YDEC_STATE_CRLFDT,
    YDEC_STATE_CRLFDTCR,
    YDEC_STATE_CRLFEQ // may actually be "\r\n.=" in raw decoder
} YencDecoderState;

// end result for incremental processing (whether the end of the yEnc data was reached)
typedef enum {
    YDEC_END_NONE,    // end not reached
    YDEC_END_CONTROL, // \r\n=y sequence found, src points to byte after 'y'
    YDEC_END_ARTICLE  // \r\n.\r\n sequence found, src points to byte after last '\n'
} YencDecoderEnd;

#include "hedley.h"

extern YencDecoderEnd
(*_do_decode)(const unsigned char *HEDLEY_RESTRICT *, unsigned char *HEDLEY_RESTRICT *, size_t, YencDecoderState *);

extern YencDecoderEnd
(*_do_decode_raw)(const unsigned char *HEDLEY_RESTRICT *, unsigned char *HEDLEY_RESTRICT *, size_t, YencDecoderState *);

extern YencDecoderEnd
(*_do_decode_end_raw)(const unsigned char *HEDLEY_RESTRICT *, unsigned char *HEDLEY_RESTRICT *, size_t,
                      YencDecoderState *);

extern int _decode_simd_level;

static inline size_t
do_decode(int isRaw, const unsigned char *HEDLEY_RESTRICT src, unsigned char *HEDLEY_RESTRICT dest, size_t len,
          YencDecoderState *state) {
    unsigned char *ds = dest;
    (*(isRaw ? _do_decode_raw : _do_decode))(&src, &ds, len, state);
    return ds - dest;
}

static inline YencDecoderEnd
do_decode_end(const unsigned char *HEDLEY_RESTRICT *src, unsigned char *HEDLEY_RESTRICT *dest, size_t len,
              YencDecoderState *state) {
    return _do_decode_end_raw(src, dest, len, state);
}

void decoder_init();

const char* simd_detected();


#ifdef __cplusplus
}
#endif
#endif

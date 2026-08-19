/*  Copyright (c) 2026, Alliance for Open Media
    All rights reserved. */
/*
    Redistribution and use in source and binary forms, with or without
    modification, are permitted (subject to the limitations in the
    disclaimer below) provided that the following conditions are met:

    - Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    - Neither the name of the Alliance for Open Media nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

    NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
    GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
    HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
    BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
    OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
    OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2008 Gregory Maxwell
   Written by Jean-Marc Valin and Gregory Maxwell */
/**
   @file celt.h
   @brief Contains all the functions for encoding and decoding audio
 */

/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CELT_H
#define CELT_H

#include "oac_types.h"
#include "oac_defines.h"
#include "oac_custom.h"
#include "entenc.h"
#include "entdec.h"
#include "arch.h"
#include "kiss_fft.h"

#ifdef ENABLE_DEEP_PLC
# include "lpcnet.h"
#endif

#define CELTEncoder OacCustomEncoder
#define CELTDecoder OacCustomDecoder
#define CELTMode OacCustomMode

/** Maximum supported ambisonics order. */
#define OAC_MAX_AMBISONICS_ORDER 5
#define OAC_MAX_AMBISONICS_CHANNELS ((OAC_MAX_AMBISONICS_ORDER+1)*(OAC_MAX_AMBISONICS_ORDER+1))
#define OAC_MAX_CHANNELS 255
/* Check that OAC_MAX_CHANNELS is large enough */
typedef char oac_assert_max_channels_sufficient[(OAC_MAX_CHANNELS >= OAC_MAX_AMBISONICS_CHANNELS) ? 1 : -1];
#define LEAK_BANDS 19

typedef struct {
    int valid;
    float tonality;
    float tonality_slope;
    float noisiness;
    float activity;
    float music_prob;
    float music_prob_min;
    float music_prob_max;
    int bandwidth;
    float activity_probability;
    float max_pitch_ratio;
    /* Store as Q6 char to save space. */
    unsigned char leak_boost[LEAK_BANDS];
} AnalysisInfo;

typedef struct {
    int signalType;
    int offset;
} SILKInfo;

#define celt_check_mode_ptr_ptr(ptr) ((ptr) + ((ptr) - (const CELTMode**)(ptr)))

#define celt_check_analysis_ptr(ptr) ((ptr) + ((ptr) - (const AnalysisInfo*)(ptr)))

#define celt_check_silkinfo_ptr(ptr) ((ptr) + ((ptr) - (const SILKInfo*)(ptr)))

#define celt_check_glog_ptr(ptr) ((ptr) + ((ptr) - (celt_glog*)(ptr)))

#define celt_check_sig_ptr(ptr) ((ptr) + ((ptr) - (celt_sig*)(ptr)))
#define celt_check_const_sig_ptr(ptr) ((ptr) + ((ptr) - (const celt_sig*)(ptr)))

/* Encoder/decoder Requests */


#define CELT_SET_PREDICTION_REQUEST    10002
/** Controls the use of interframe prediction.
    0=Independent frames
    1=Short term interframe prediction allowed
    2=Long term prediction allowed
 */
#define CELT_SET_PREDICTION(x) CELT_SET_PREDICTION_REQUEST, oac_check_int(x)

#define CELT_SET_INPUT_CLIPPING_REQUEST    10004
#define CELT_SET_INPUT_CLIPPING(x) CELT_SET_INPUT_CLIPPING_REQUEST, oac_check_int(x)

#define CELT_GET_AND_CLEAR_ERROR_REQUEST   10007
#define CELT_GET_AND_CLEAR_ERROR(x) CELT_GET_AND_CLEAR_ERROR_REQUEST, oac_check_int_ptr(x)

#define CELT_SET_CHANNELS_REQUEST    10008
#define CELT_SET_CHANNELS(x) CELT_SET_CHANNELS_REQUEST, oac_check_int(x)


/* Internal */
#define CELT_SET_START_BAND_REQUEST    10010
#define CELT_SET_START_BAND(x) CELT_SET_START_BAND_REQUEST, oac_check_int(x)

#define CELT_SET_END_BAND_REQUEST    10012
#define CELT_SET_END_BAND(x) CELT_SET_END_BAND_REQUEST, oac_check_int(x)

#define CELT_GET_MODE_REQUEST    10015
/** Get the CELTMode used by an encoder or decoder */
#define CELT_GET_MODE(x) CELT_GET_MODE_REQUEST, celt_check_mode_ptr_ptr(x)

#define CELT_SET_SIGNALLING_REQUEST    10016
#define CELT_SET_SIGNALLING(x) CELT_SET_SIGNALLING_REQUEST, oac_check_int(x)

#define CELT_SET_TONALITY_REQUEST    10018
#define CELT_SET_TONALITY(x) CELT_SET_TONALITY_REQUEST, oac_check_int(x)
#define CELT_SET_TONALITY_SLOPE_REQUEST    10020
#define CELT_SET_TONALITY_SLOPE(x) CELT_SET_TONALITY_SLOPE_REQUEST, oac_check_int(x)

#define CELT_SET_ANALYSIS_REQUEST    10022
#define CELT_SET_ANALYSIS(x) CELT_SET_ANALYSIS_REQUEST, celt_check_analysis_ptr(x)

#define OAC_SET_LFE_REQUEST    10024
#define OAC_SET_LFE(x) OAC_SET_LFE_REQUEST, oac_check_int(x)

#define OAC_SET_ENERGY_MASK_REQUEST    10026
#define OAC_SET_ENERGY_MASK(x) OAC_SET_ENERGY_MASK_REQUEST, celt_check_glog_ptr(x)

#define CELT_SET_SILK_INFO_REQUEST    10028
#define CELT_SET_SILK_INFO(x) CELT_SET_SILK_INFO_REQUEST, celt_check_silkinfo_ptr(x)

#define CELT_SET_PREEMPHASIS_REQUEST    10030
#define CELT_SET_PREEMPHASIS(x) CELT_SET_PREEMPHASIS_REQUEST, celt_check_const_sig_ptr(x)

#define CELT_GET_PREEMPHASIS_REQUEST    10031
#define CELT_GET_PREEMPHASIS(x) CELT_GET_PREEMPHASIS_REQUEST, celt_check_sig_ptr(x)

#define CELT_SET_TDAC_MEM_REQUEST       10032
#define CELT_SET_TDAC_MEM(x) CELT_SET_TDAC_MEM_REQUEST, celt_check_const_sig_ptr(x)

#define CELT_GET_TDAC_MEM_REQUEST       10033
#define CELT_GET_TDAC_MEM(x) CELT_GET_TDAC_MEM_REQUEST, celt_check_sig_ptr(x)


static OAC_INLINE oac_int32 oaci_bits_to_bitrate(oac_int32 bits, oac_int32 Fs, oac_int32 frame_size) {
    return bits*(6*Fs/frame_size)/6;
}

static OAC_INLINE oac_int32 oaci_bitrate_to_bits(oac_int32 bitrate, oac_int32 Fs, oac_int32 frame_size) {
    return bitrate*6/(6*Fs/frame_size);
}

/* Encoder stuff */

int oaci_celt_encoder_get_size(int channels);

int oaci_celt_encode_with_ec(OacCustomEncoder * OAC_RESTRICT st, const oac_res * pcm, int frame_size,
    unsigned char *compressed, int nbCompressedBytes, ec_enc *enc);

int oaci_celt_encoder_init(CELTEncoder *st, oac_int32 sampling_rate, int channels,
    int arch, int format);



/* Decoder stuff */

int oaci_celt_decoder_get_size(int channels);


int oaci_celt_decoder_init(CELTDecoder *st, oac_int32 sampling_rate, int channels, int format);

int oaci_celt_decode_with_ec_dred(CELTDecoder * OAC_RESTRICT st, const unsigned char *data,
    int len, oac_res * OAC_RESTRICT pcm, int frame_size, ec_dec *dec, int accum
#ifdef ENABLE_DEEP_PLC
    , LPCNetPLCState *lpcnet
#endif
    );

int oaci_celt_decode_with_ec(OacCustomDecoder * OAC_RESTRICT st, const unsigned char *data,
    int len, oac_res * OAC_RESTRICT pcm, int frame_size, ec_dec *dec, int accum);

#define celt_encoder_ctl oac_custom_encoder_ctl
#define celt_decoder_ctl oac_custom_decoder_ctl


#if defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API)
# define OAC_CUSTOM_NOSTATIC
#else
# define OAC_CUSTOM_NOSTATIC static OAC_INLINE
#endif

static const unsigned char trim_icdf[11] = {119, 109, 85, 53, 30, 14, 6, 3, 2, 1, 0};
/* Probs: NONE: 6.25%, LIGHT: 6.25%, NORMAL: 50%, AGGRESSIVE: 37.5% */
static const unsigned char spread_icdf[4] = {30, 28, 12, 0};

static const unsigned char tapset_icdf[3] = {3, 2, 0};

#if defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API)
static const unsigned char toOacTable[20] = {
    0xE0, 0xE8, 0xF0, 0xF8,
    0xC0, 0xC8, 0xD0, 0xD8,
    0xA0, 0xA8, 0xB0, 0xB8,
    0x00, 0x00, 0x00, 0x00,
    0x80, 0x88, 0x90, 0x98,
};

static const unsigned char fromOacTable[16] = {
    0x80, 0x88, 0x90, 0x98,
    0x40, 0x48, 0x50, 0x58,
    0x20, 0x28, 0x30, 0x38,
    0x00, 0x08, 0x10, 0x18
};

static OAC_INLINE int oaci_toOac(unsigned char c) {
    int ret = 0;
    if (c < 0xA0)
        ret = toOacTable[c>>3];
    if (ret == 0)
        return -1;
    else
        return ret|(c&0x7);
}

static OAC_INLINE int oaci_fromOac(unsigned char c) {
    if (c < 0x80)
        return -1;
    else
        return fromOacTable[(c>>3) - 16]|(c&0x7);
}
#endif /* CUSTOM_MODES */

#define COMBFILTER_MAXPERIOD 1024
#define COMBFILTER_MINPERIOD 15

extern const signed char oaci_tf_select_table[4][8];

#if defined(ENABLE_HARDENING) || defined(ENABLE_ASSERTIONS)
void oaci_validate_celt_decoder(CELTDecoder *st);
# define VALIDATE_CELT_DECODER(st) oaci_validate_celt_decoder(st)
#else
# define VALIDATE_CELT_DECODER(st)
#endif

int oaci_resampling_factor(oac_int32 rate);

void oaci_celt_preemphasis(const oac_res * OAC_RESTRICT pcmp, celt_sig * OAC_RESTRICT inp,
    int N, int CC, int upsample, const oac_val16 *coef, celt_sig *mem, int clip);

void oaci_comb_filter(oac_val32 *y, oac_val32 *x, int T0, int T1, int N,
    oac_val16 g0, oac_val16 g1, int tapset0, int tapset1,
    const celt_coef *window, int overlap, int arch);

void oaci_init_caps(const CELTMode *m, int *cap, int LM, int C);

#ifdef RESYNTH
void oaci_deemphasis(celt_sig *in[], oac_res *pcm, int N, int C, int downsample, const oac_val16 *coef, celt_sig *mem,
    int accum);
void oaci_celt_synthesis(const CELTMode *mode, celt_norm *X, celt_sig * out_syn[],
    celt_glog *oldBandE, int start, int effEnd, int C, int CC, int isTransient,
    int LM, int downsample, int silence, int arch);
#endif

#ifdef ENABLE_QEXT
# define QEXT_SCALE(x) ((qext_scale)*(x))
# define QEXT_SCALE2(x, qext_scale) ((qext_scale)*(x))
#else
# define QEXT_SCALE(x) (x)
# define QEXT_SCALE2(x, qext_scale) (x)
#endif

#endif /* CELT_H */

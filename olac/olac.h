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

#ifndef OLAC_H
# define OLAC_H

#include <math.h>
#include "arch.h"

#define FRAME_SIZE 960
#define PRED_ORDER 63
#define OVERLAP_SIZE 120
#define HALF_OVERLAP (OVERLAP_SIZE/2)
#define PREEMPH (0.85f)
#define PREEMPH_Q15 (27853)
#define INV_PREEMPH_Q15 (38550)
#define PREEMPH_MOD (2 * (32767 / (32768 - PREEMPH_Q15)) + 1)
#define MAX_SHIFT 24
#define MAX_CHANNELS 2
#define OLAC_COEF_SHIFT 20
#define OLAC_COEF_SCALE (1<<OLAC_COEF_SHIFT)
#define RC_SHIFT 20
#define RC_SCALE (1<<RC_SHIFT)
#define SIGNAL_BITS 13
#define COEF_BITS 13

extern oac_int32 lift_p[];
extern oac_int32 lift_q[];

extern oac_int8 rc_center[PRED_ORDER];

extern oac_int8 rc_scale[PRED_ORDER];

#define MUL(a,b) ((int)floor(.5f+(a)*(b)))

#define OLAC_ABS(x) (((oac_uint32)(x) ^ (oac_uint32)(-((x) < 0))) + (oac_uint32)((x) < 0))
#define OLAC_SHL32(a, b) ((oac_int32)((oac_uint32)(a)<<(b)))
#define OLAC_SHR32(a, b) ((oac_int32)(a)>>(b))
#define OLAC_PSHR32(a, b) (((oac_int32)(a)+(1L<<(b)>>1))>>(b))
#define OLAC_SHR64(a, b) ((oac_int64)(a)>>(b))
#define OLAC_PSHR64(a, b) (((oac_int64)(a)+(1LL<<(b)>>1))>>(b))
#define OLAC_MUL_P31(a, b) (((oac_int64)(a)*(oac_int64)(b)+(1LL<<30)) >> 31)
#define OLAC_MUL16_16_Q15(a, b) (((oac_int32)(a)*(oac_int32)(b)) >> 15)
#define OLAC_MUL16_16_P15(a, b) (((oac_int32)(a)*(oac_int32)(b)+(1<<14)) >> 15)
#define OLAC_MUL16_32_P15(a, b) (((oac_int64)(a)*(oac_int64)(b)+(1<<14)) >> 15)

#define MOD(a, b) (((a)%(b)+(b))%(b))

typedef struct {
    int nb_channels;
    oac_int32 sampling_rate;
    int last_ctz;
    oac_uint32 rng;
    oac_int32 mem_modulo[MAX_CHANNELS];
    oac_int32 last_last_sample[MAX_CHANNELS];
    oac_int32 pmem[MAX_CHANNELS];
    oac_int32 tdac_mem[MAX_CHANNELS][HALF_OVERLAP];
} OlacEncoder;


typedef struct {
    int nb_channels;
    oac_int32 sampling_rate;
    int last_ctz;
    oac_uint32 rng;
    oac_int32 dmem[MAX_CHANNELS];
    oac_int32 untdac_mem[MAX_CHANNELS][HALF_OVERLAP];
} OlacDecoder;


float olac_burg_analysis(              /* O    returns residual energy                                     */
    float          A[],                /* O    prediction coefficients (length order)                      */
    float          refl[],             /* O    reflection coefficients (length order)                      */
    int           *optimal_order,      /* O    optimal order of the filter */
    const float    x[],                /* I    input signal, length: nb_subfr*(D+L_sub)                    */
    const float    minInvGain,         /* I    minimum inverse prediction gain                             */
    const int      subfr_length,       /* I    input signal subframe length (incl. D preceding samples)    */
    const int      nb_subfr,           /* I    number of subframes stacked in x                            */
    const int      D                   /* I    order                                                       */
);

oac_int16 rc_factor(oac_int32 rc);

int aks_downshift(const oac_int32 *aks, oac_int16 *aks16, int order);

void olac_aks_from_rc(oac_int32 *aks, const oac_int32 *rc, int order);


int olac_encoder_init(OlacEncoder *st, int channels, oac_int32 sampling_rate);

oac_int32 olac_encode(OlacEncoder *st, const oac_int32 *pcm, int frame_size, unsigned char *data, int nbCompressedBytes);

int olac_decoder_init(OlacDecoder *st, int channels, oac_int32 sampling_rate);

oac_int32 olac_decode(OlacDecoder *st, const unsigned char *data, int len, oac_int32 *pcm, int frame_size);




#endif /* OLAC_H */

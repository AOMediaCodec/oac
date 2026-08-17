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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include <stdio.h>
#include "olac.h"
#include "os_support.h"
#include "entenc.h"

static void tdac(oac_int32 *pcm, int len, oac_int32 *mem) {
    oac_int32 tmp[FRAME_SIZE];
    int i;

    /* 1. Output the left fold computed during the PREVIOUS frame */
    for (i = 0; i < HALF_OVERLAP; i++) {
        tmp[i] = mem[HALF_OVERLAP - i - 1];
    }

    /* 2. Shift the flat top */
    for (i = HALF_OVERLAP; i < len - HALF_OVERLAP; i++) {
        tmp[i] = pcm[i - HALF_OVERLAP];
    }

    /* 3. Process CURRENT overlap with stable lifting */
    for (i = 0; i < HALF_OVERLAP; i++) {
        oac_int32 a = pcm[len - i - 1];            /* Inner sample (y) */
        oac_int32 b = pcm[len - OVERLAP_SIZE + i]; /* Outer sample (x) */

        /* Forward stable lifting */
        a += OLAC_MUL_P31(lift_p[i], b);
        b += OLAC_MUL_P31(lift_q[i], a);
        a += OLAC_MUL_P31(lift_p[i], b);

        /* 'a' perfectly maps to the right fold for the current frame */
        tmp[i + len - HALF_OVERLAP] = a;

        /* 'b' maps to the left fold. Negate it to perfectly align
           with the next frame's reverse-read logic. */
        mem[i] = -b;
    }

    for (i = 0; i < len; i++) {
        pcm[i] = tmp[i];
    }
}

static void olac_preemphasis(oac_int32 *pcm, int len, oac_int32 *_mem) {
    int i;
    oac_int32 mem = *_mem;
    for (i=0;i<len;i++) {
        oac_int32 tmp = mem;
        mem = pcm[i];
        pcm[i] -= OLAC_MUL16_32_P15(PREEMPH_Q15, tmp);
    }
    *_mem = mem;
}

static void pred_filter(oac_int32 *out, const oac_int32 *in, int len, const oac_int32 *rc, int order, int last_ctz, int curr_ctz) {
    oac_int32 aks[PRED_ORDER*(PRED_ORDER+1)/2];
    int i;
    oac_int32 mem[PRED_ORDER] = {0};
    olac_aks_from_rc(aks, rc, order);
    for (i=0;i<len;i++) {
        int j;
        int offset;
        oac_int64 pred = 0;
        if (i==HALF_OVERLAP && last_ctz != curr_ctz) {
            if (last_ctz > curr_ctz) {
                for (j=0;j<order;j++) mem[j] = OLAC_SHL32(mem[j], last_ctz-curr_ctz);
            } else {
                for (j=0;j<order;j++) mem[j] = OLAC_SHR32(mem[j], curr_ctz-last_ctz);
            }
        }
        offset = order*(order-1)/2;
        if (i < order) offset = i*(i-1)/2;
        for (j=0;j<order;j++) {
            pred += mem[j]*(oac_int64)aks[offset + j];
        }
        out[i] = in[i] - OLAC_PSHR64(pred, COEF_SHIFT);
        for (j=order-2;j>=0;j--) {
            mem[j+1] = mem[j];
        }
        mem[0] = in[i];
    }
}

static int rc_map(int x, int p, int N) {
    if (x >= p) {
        if (x <= 2 * p) {
            /* Inside symmetric window, right side (maps to even) */
            return 2 * (x - p);
        } else {
            /* Outside symmetric window, tail on the right */
            return x;
        }
    } else {
        if (x >= 2 * p - N) {
            /* Inside symmetric window, left side (maps to odd) */
            return 2 * (p - x) - 1;
        } else {
            /* Outside symmetric window, tail on the left */
            return N - x;
        }
    }
}

static void encode_unary(ec_enc *enc, int x);

static void quantize_coefs(ec_enc *enc, float *float_rc, oac_int32 *rc, int order, float gain) {
    int i;
    int q0;
    int res = 9./sqrt(1e-5+gain);
    q0 = EC_ILOG((int)res);
    q0 = IMIN(15, q0);
    oaci_ec_enc_bits(enc, order, 6);
    oaci_ec_enc_bits(enc, q0, 4);
    res = 1<<q0;
    for (i=0;i<order;i++) {
        int q, m, s;
        q = floor(.5+res*float_rc[i]);
        m = rc_map(q+res, ((rc_center[i]+64)*res+32)>>6, 2*res);
        s = EC_ILOG(IMAX(1, res*rc_scale[i]>>8));
        encode_unary(enc, m>>s);
        if (s > 0) oaci_ec_enc_bits(enc, m&((1<<s)-1), s);
        rc[i] = (oac_int64)q*RC_SCALE/res;
        res = OLAC_MUL16_16_P15(rc_factor(rc[i]), res);
        if (res < 1) res = 1;
    }
}

static int predict_impl(ec_enc *enc, oac_int32 *residual, float *float_rc, const oac_int32 *pcm, int len, int order, int start, int end, int last_ctz, int curr_ctz) {
    int i;
    float gain;
    float dummy[PRED_ORDER];
    float pcm_float[FRAME_SIZE];
    oac_int32 rc[PRED_ORDER];
    int optimal_order;
    for (i=0;i<len;i++) pcm_float[i] = pcm[i];
    if (last_ctz != curr_ctz) {
        if (last_ctz > curr_ctz) {
            for (i=0;i<HALF_OVERLAP;i++) pcm_float[i] *= 1<<(last_ctz-curr_ctz);
        } else {
            for (i=HALF_OVERLAP;i<len;i++) pcm_float[i] *= 1<<(curr_ctz-last_ctz);
        }
    }
    gain = olac_burg_analysis(dummy, float_rc, &optimal_order, pcm_float+start, 3e-5, end-start, 1, order);
    for (i=optimal_order;i<order;i++) float_rc[i] = 0;
    order = optimal_order;

    quantize_coefs(enc, float_rc, rc, order, gain);
    pred_filter(residual, pcm, len, rc, order, last_ctz, curr_ctz);
    return order;
}


static void predict(ec_enc *enc, oac_int32 *residual, const oac_int32 *pcm, int len, int last_ctz, int curr_ctz) {
    float rc[PRED_ORDER];
#if 0
    ec_enc bak1, bak2;
    bak1 = *enc;
#endif
    predict_impl(enc, residual, rc, pcm, len, PRED_ORDER, 0, len, last_ctz, curr_ctz);
#if 0
    /* FIXME: Copy the bytes too. */
    bak2 = *enc;
    *enc = bak1;
    int res1[FRAME_SIZE], res2[FRAME_SIZE];
    int i;
    float cumul=0;
    int split_pos=0;
    float best=1e9;
    predict_impl(enc, res1, rc, pcm, len, PRED_ORDER/2, 0, len/4, last_ctz, curr_ctz);
    predict_impl(enc, res2, rc, pcm, len, PRED_ORDER/2, 3*len/4, len, last_ctz, curr_ctz);
    for (i=0;i<len;i++) {
        if (cumul<best) {
            best = cumul;
            split_pos = i;
        }
        cumul += (int)OLAC_ABS(res1[i]) - (int)OLAC_ABS(res2[i]);
        //printf("%f ", cumul);
    }
    //printf("%d %f ", split_pos, best);
    if (split_pos < PRED_ORDER) split_pos = PRED_ORDER;
    if (split_pos > len-PRED_ORDER) split_pos = len-PRED_ORDER;
    //printf("\n");
    predict_impl(enc, res1, rc, pcm, len, PRED_ORDER/2, 0, split_pos, last_ctz, curr_ctz);
    predict_impl(enc, res2, rc, pcm, len, PRED_ORDER/2, split_pos, len, last_ctz, curr_ctz);
    split_pos = 0;
    best = 1e9;
    cumul=0;
    for (i=0;i<len;i++) {
        if (cumul<best) {
            best = cumul;
            split_pos = i;
        }
        cumul += (int)OLAC_ABS(res1[i]) - (int)OLAC_ABS(res2[i]);
        //printf("%f ", cumul);
    }
    //printf("%d %f\n", split_pos, best);
    if (split_pos < PRED_ORDER) split_pos = PRED_ORDER;
    if (split_pos > len-PRED_ORDER) split_pos = len-PRED_ORDER;
    *enc = bak1;
    predict_impl(enc, res1, rc, pcm, len, PRED_ORDER/2, 0, split_pos, last_ctz, curr_ctz);
    predict_impl(enc, res2, rc, pcm, len, PRED_ORDER/2, split_pos, len, last_ctz, curr_ctz);
    for (i=split_pos;i<len;i++) {
        res1[i] = res2[i];
    }
    cumul = 0;
    for (i=0;i<len;i++) {
        cumul += (int)OLAC_ABS(res1[i]) - (int)OLAC_ABS(residual[i]);
    }
    if (cumul < 0) {
        for (i=0;i<len;i++) {
            residual[i] = res1[i];
        }
    } else {
        *enc = bak2;
    }
#endif
}

static void find_residual_params(const oac_int32 *residual, int len, int *s_ptr, int *t_ptr, int *split_ptr) {
    int s, i;
    oac_int32 res[FRAME_SIZE];
    oac_int32 L[MAX_SHIFT+1] = {0};
    oac_int32 R[MAX_SHIFT+1] = {0};
    oac_int32 min_bits = 2000000000;
    int best_s = 0, best_t = 0, best_split = 0;

    for (i=0; i<len; i++) {
        res[i] = 2*OLAC_ABS(residual[i]) - (residual[i]<0);
    }
    /* 1. Calculate bits and initial right-side totals (entire frame) */
    for (s=0; s<=MAX_SHIFT; s++) {
        for (i=0; i<len; i++) {
            oac_int32 b;
            int s2 = s - 1 + (i<4) + (i==0);
            if (s > 0) {
                b = (res[i]>>s2) + s2 + 1;
                b = IMIN(b, 500000);
            } else {
                b = 500000*(res[i] != 0);
            }
            R[s] += b;
        }
    }

    /* 2. Establish NO SPLIT baseline */
    for (s=0; s<=MAX_SHIFT; s++) {
        if (R[s] + 1 < min_bits) {
            min_bits = R[s] + 1;
            best_s = s;
            best_t = s;
            best_split = -1;
        }
    }

    /* 3. Evaluate SPLITS dynamically */
    for (i=0; i<len-1; i++) {
        int min_L = 2000000000, local_s = 0;
        int min_R = 2000000000, local_t = 0;

        /* Shift one element, and track the independent minimums */
        for (s=0; s<=MAX_SHIFT; s++) {
            oac_int32 b;
            int s2 = s - 1 + (i<4) + (i==0);
            if (s > 0) {
                b = (res[i]>>s2) + s2 + 1;
                b = IMIN(b, 500000);
            } else {
                b = 500000*(res[i] != 0);
            }
            L[s] += b;
            R[s] -= b;

            if (L[s] < min_L) {
                min_L = L[s];
                local_s = s;
            }
            if (R[s] < min_R) {
                min_R = R[s];
                local_t = s;
            }
        }

        /* The penalty is now a constant 12 (or whatever your static overhead is) */
        int split_bits = min_L + min_R + 12;

        if (split_bits < min_bits && (i+1)%8==0) {
            min_bits = split_bits;
            best_s = local_s;
            best_t = local_t;
            best_split = i+1;
        }
    }

    min_bits += OLAC_ABS(best_s-best_t);
    *s_ptr = best_s;
    *t_ptr = best_t;
    *split_ptr = best_split;
}

static oac_int32 interleave(oac_int32 x) {
    return 2*OLAC_ABS(x) - (x<0);
}

static void encode_unary(ec_enc *enc, int x) {
    while (x>0) {
        oaci_ec_enc_bits(enc, 1, 1);
        x--;
    }
    oaci_ec_enc_bits(enc, 0, 1);
}

static void encode_golomb_rice(ec_enc *enc, oac_int32 x, int s) {
    x = interleave(x);
    encode_unary(enc, x>>s);
    if (s > 0) oaci_ec_enc_bits(enc, x&((1<<s)-1), s);
}

static float compute_chan_pred_gain(const oac_int32 *residual, int len, const oac_int32 *ref) {
    double xy=0, yy=0;
    int i;
    for (i=0;i<len;i++) {
        xy += (double)residual[i]*ref[i];
        yy += (double)ref[i]*ref[i];
    }
    return xy/(yy+1);
}

static void code_residual(ec_enc *enc, oac_int32 *residual, int len, const oac_int32 *ref) {
    int best_s, best_t;
    int split_pos;
    int i;
    if (ref != NULL) {
        int qgain;
        float gain = compute_chan_pred_gain(residual, len, ref);
        gain = MAX32(-2.f, MIN32(2.f, gain));
        qgain = floor(.5 + gain*16.);
        /* Optional fine-tuning of gain. */
        if (0) {
            int d;
            oac_int64 best_error=1000000000000LL;
            int best_d=0;
            for (d=-2;d<=2;d++) {
                oac_int64 error=0;
                gain = (qgain+d)/16.;
                for (i=0;i<len;i++) {
                    error += OLAC_ABS(residual[i] - MUL(gain, ref[i]));
                }
                if (error < best_error) {
                    best_error = error;
                    best_d = d;
                }
            }
            qgain += best_d;
        }
        encode_golomb_rice(enc, qgain-5, 3);
        for (i=0;i<len;i++) residual[i] -= OLAC_PSHR64(qgain * (oac_int64)ref[i], 4);
    }
    find_residual_params(residual, len, &best_s, &best_t, &split_pos);
    oaci_ec_enc_uint(enc, best_s, MAX_SHIFT+1);
    encode_unary(enc, interleave(best_t - best_s));
    if (best_t != best_s) {
        oaci_ec_enc_uint(enc, split_pos/8-1, len/8-1);
    }
    if (best_s == best_t) split_pos = len;
    if (best_s > 0) {
        for (i = 0; i < split_pos; i++) {
            encode_golomb_rice(enc, residual[i], best_s - 1 + (i<4) + (i==0));
        }
    }
    if (best_t > 0) {
        for (i = split_pos; i < len; i++) {
            encode_golomb_rice(enc, residual[i], best_t - 1 + (i<4) + (i==0));
        }
    }
}

int olac_encoder_init(OlacEncoder *st, int channels, int sampling_rate) {
    st->nb_channels = channels;
    st->sampling_rate = sampling_rate;
    st->last_ctz = 0;
    OAC_CLEAR(st->tdac_mem, channels);
    OAC_CLEAR(st->pmem, channels);
    OAC_CLEAR(st->last_last_sample, channels);
    OAC_CLEAR(st->mem_modulo, channels);
    return OAC_OK;
}

oac_int32 olac_encode(OlacEncoder *st, const oac_int32 *pcm, int frame_size, unsigned char *data, int nbCompressedBytes) {
    ec_enc enc;
    int c, i;
    oac_uint32 lsb_mask=0;
    int ctz=0;
    oac_int32 sig[FRAME_SIZE];
    oac_int32 residual[FRAME_SIZE];
    oac_int32 ref[FRAME_SIZE];
    oaci_ec_enc_init(&enc, data, nbCompressedBytes);
    for (i=0;i<frame_size*st->nb_channels;i++) lsb_mask |= pcm[i];
    while ((lsb_mask & 1) == 0 && ctz < 24) {
        ctz++;
        lsb_mask >>= 1;
    }
    oaci_ec_enc_uint(&enc, ctz, 25);
    for (c=0;c<st->nb_channels;c++) {
        for (i=0;i<frame_size;i++) {
            sig[i] = OLAC_SHR32(pcm[i*st->nb_channels+c], ctz);
        }
        if (ctz > st->last_ctz) st->pmem[c] = OLAC_SHR32(st->pmem[c], ctz - st->last_ctz);
        else if (ctz < st->last_ctz) st->pmem[c] = OLAC_SHL32(st->pmem[c], st->last_ctz - ctz);
        oaci_ec_enc_uint(&enc, st->mem_modulo[c], PREEMPH_MOD);
        if (frame_size > OVERLAP_SIZE) {
            st->mem_modulo[c] = MOD(sig[frame_size-OVERLAP_SIZE-1],PREEMPH_MOD);
        } else {
            st->mem_modulo[c] = MOD(st->last_last_sample[c],PREEMPH_MOD);
        }
        st->last_last_sample[c] = sig[frame_size-1];
        olac_preemphasis(sig, frame_size, &st->pmem[c]);
        tdac(sig, frame_size, st->tdac_mem[c]);
        predict(&enc, residual, sig, frame_size, st->last_ctz, ctz);
        code_residual(&enc, residual, frame_size, (c==0) ? NULL : ref);
        OAC_COPY(ref, residual, frame_size);
    }
    nbCompressedBytes = IMIN(nbCompressedBytes, (oaci_ec_tell(&enc)+7)>>3);
    oaci_ec_enc_shrink(&enc, nbCompressedBytes);
    oaci_ec_enc_done(&enc);
    st->last_ctz = ctz;
    return enc.error ? OAC_BUFFER_TOO_SMALL : nbCompressedBytes;
}

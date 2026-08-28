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
#include "entdec.h"


static void untdac(oac_int32 *pcm, int len, oac_int32 *mem) {
    oac_int32 tmp[FRAME_SIZE];
    int i;

    for (i = 0; i < HALF_OVERLAP; i++) {
        oac_int32 a = mem[i];
        oac_int32 b = -pcm[HALF_OVERLAP - i - 1]; /* Negate to restore the 'b' state */

        /* Perfect inverse lifting */
        a -= OLAC_MUL_P31(lift_p[i], b);
        b -= OLAC_MUL_P31(lift_q[i], a);
        a -= OLAC_MUL_P31(lift_p[i], b);

        /* Un-swap the variables to their original PCM positions */
        tmp[i] = b;
        tmp[OVERLAP_SIZE - i - 1] = a;
    }

    for (i = 0; i < HALF_OVERLAP; i++) {
        mem[i] = pcm[len - HALF_OVERLAP + i];
    }

    for (i = HALF_OVERLAP; i < len - HALF_OVERLAP; i++) {
        tmp[i + HALF_OVERLAP] = pcm[i];
    }

    for (i = 0; i < len; i++) {
        pcm[i] = tmp[i];
    }
}

static void olac_deemphasis(oac_int32 *pcm, int len, oac_int32 *_mem) {
    int i;
    oac_int32 mem = *_mem;
    for (i=0;i<len;i++) {
        pcm[i] += OLAC_MUL16_32_P15(PREEMPH_Q15, mem);
        mem = pcm[i];
    }
    *_mem = mem;
}

static void syn_filter(oac_int32 *out, const oac_int32 *in, int len, const oac_int32 *rc, int order, int last_ctz, int curr_ctz) {
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
        out[i] = in[i] + OLAC_PSHR64(pred, OLAC_COEF_SHIFT);
        for (j=order-2;j>=0;j--) {
            mem[j+1] = mem[j];
        }
        mem[0] = out[i];
    }
}

int rc_unmap(int y, int p, int N) {
    int m = IMIN(p, N - p);

    if (y <= 2 * m) {
        /* Reconstruct from the symmetric alternating window */
        if (y % 2 == 0) {
            return p + (y / 2);
        } else {
            return p - ((y + 1) / 2);
        }
    } else {
        /* Reconstruct from the sequential tail */
        if (2 * p < N) {
            return y;
        } else {
            return N - y;
        }
    }
}

static int decode_unary(ec_dec *dec);

static int unquantize_coefs(ec_dec *dec, oac_int32 *rc) {
    int i;
    int order;
    int q0;
    int res;
    order = oaci_ec_dec_bits(dec, 6);
    q0 = oaci_ec_dec_bits(dec, 4);
    res = 1<<q0;
    for (i=0;i<order;i++) {
        int q, m, s;
        s = EC_ILOG(IMAX(1, res*rc_scale[i]>>8));
        m = OLAC_SHL32(decode_unary(dec), s);
        if (s > 0) m += oaci_ec_dec_bits(dec, s);
        if (m > 2*res) return -1;
        q = rc_unmap(m, ((rc_center[i]+64)*res+32)>>6, 2*res) - res;
        rc[i] = (oac_int64)q*RC_SCALE/res;
        res = OLAC_MUL16_16_P15(rc_factor(rc[i]), res);
        if (res < 1) res = 1;
    }
    return order;
}

static oac_int32 deinterleave(oac_int32 x) {
    if (x&1) return -(x/2) - 1;
    else return x/2;
}

static int decode_unary(ec_dec *dec) {
    int x=0;
    while (oaci_ec_dec_bits(dec, 1) == 1) x++;
    return x;
}

static oac_int32 decode_golomb_rice(ec_dec *dec, int s) {
    oac_int32 x;
    x = OLAC_SHL32(decode_unary(dec), s);
    if (s > 0) x += oaci_ec_dec_bits(dec, s);
    x = deinterleave(x);
    return x;
}

static int decode_residual(ec_dec *dec, oac_int32 *residual, int len, oac_int32 *ref) {
    int best_s, best_t;
    int i;
    int split_pos=0;
    int qgain = 0;
    if (ref != NULL) {
        qgain = decode_golomb_rice(dec, 3)+5;
    }

    best_s = oaci_ec_dec_uint(dec, MAX_SHIFT+1);
    best_t = deinterleave(decode_unary(dec)) + best_s;
    if (best_t < 0 || best_t > MAX_SHIFT) return 1;
    if (best_t != best_s) {
        split_pos = 8 + 8*oaci_ec_dec_uint(dec, len/8-1);
    }
    OAC_CLEAR(residual, len);
    if (best_s > 0) {
        for (i = 0; i < split_pos; i++) {
            residual[i] = decode_golomb_rice(dec, best_s - 1 + (i<4) + (i==0));
        }
    }
    if (best_t > 0) {
        for (i = split_pos; i < len; i++) {
            residual[i] = decode_golomb_rice(dec, best_t - 1 + (i<4) + (i==0));
        }
    }
    if (ref != NULL) {
        for (i=0;i<len;i++) residual[i] += OLAC_PSHR64(qgain * (oac_int64)ref[i], 4);
    }
    return 0;
}

static int decode_lsbs(ec_dec *dec, oac_int32 *sig, int frame_size, int shift) {
    int s, i;
    oac_int32 lsbs[FRAME_SIZE]={0};
    for (s=0;s<shift;s++) {
        for (i=0;i<frame_size;i++) {
            lsbs[i] |= oaci_ec_dec_bits(dec, 1 ) << (shift - s - 1);
        }
    }
    for (i=0;i<frame_size;i++) {
        sig[i] = (sig[i] << shift) | lsbs[i];
    }
    return 0;
}

int olac_decoder_init(OlacDecoder *st, int channels, int sampling_rate) {
    st->last_ctz = 0;
    st->nb_channels = channels;
    st->sampling_rate = sampling_rate;
    OAC_CLEAR(st->untdac_mem, channels);
    OAC_CLEAR(st->dmem, channels);
    return OAC_OK;
}

oac_int32 olac_decode(OlacDecoder *st, const unsigned char *data, int len, oac_int32 *pcm, int frame_size) {
    ec_dec dec;
    int c;
    int ctz;
    oac_int32 rc[PRED_ORDER];
    oac_int32 sig[FRAME_SIZE];
    int order;
    oac_int32 residual[FRAME_SIZE];
    oac_int32 ref[FRAME_SIZE];
    oaci_ec_dec_init(&dec, (unsigned char*)data, len);
    ctz = oaci_ec_dec_uint(&dec, 25);
    for (c=0;c<st->nb_channels;c++) {
        int i;
        int modulo, offset;
        int shift;
        modulo = oaci_ec_dec_uint(&dec, PREEMPH_MOD);
        shift = oaci_ec_dec_bits(&dec, 4);
        order = unquantize_coefs(&dec, rc);
        if (order < 0) return OAC_INVALID_PACKET;
        if (decode_residual(&dec, residual, frame_size, (c==0) ? NULL : ref)) return OAC_INVALID_PACKET;
        syn_filter(sig, residual, frame_size, rc, order, st->last_ctz, ctz);
        decode_lsbs(&dec, sig, frame_size, shift);
        untdac(sig, frame_size, st->untdac_mem[c]);
        offset = (st->dmem[c] - modulo) % PREEMPH_MOD;
        if (offset < 0) offset += PREEMPH_MOD;
        if (offset > PREEMPH_MOD / 2) offset -= PREEMPH_MOD;
        st->dmem[c] -= offset;
        olac_deemphasis(sig, OVERLAP_SIZE, &st->dmem[c]);
        if (ctz > st->last_ctz) st->dmem[c] = OLAC_SHR32(st->dmem[c], ctz - st->last_ctz);
        else if (ctz < st->last_ctz) st->dmem[c] = OLAC_SHL32(st->dmem[c], st->last_ctz - ctz);
        olac_deemphasis(&sig[OVERLAP_SIZE], frame_size-OVERLAP_SIZE, &st->dmem[c]);
        for (i=0;i<OVERLAP_SIZE;i++) {
            pcm[i*st->nb_channels+c] = OLAC_SHL32(sig[i], st->last_ctz);
        }
        for (;i<frame_size;i++) {
            pcm[i*st->nb_channels+c] = OLAC_SHL32(sig[i], ctz);
        }
        OAC_COPY(ref, residual, frame_size);
    }
    st->last_ctz = ctz;
    st->rng = dec.rng;
    return OAC_OK;
}

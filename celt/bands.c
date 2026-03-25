/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2008-2009 Gregory Maxwell
   Written by Jean-Marc Valin and Gregory Maxwell */
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include "bands.h"
#include "modes.h"
#include "vq.h"
#include "cwrs.h"
#include "stack_alloc.h"
#include "os_support.h"
#include "mathops.h"
#include "rate.h"
#include "quant_bands.h"
#include "pitch.h"

int oaci_hysteresis_decision(oac_val16 val, const oac_val16 *thresholds, const oac_val16 *hysteresis, int N, int prev) {
    int i;
    for (i = 0; i < N; i++) {
        if (val < thresholds[i])
            break;
    }
    if (i > prev && val < thresholds[prev] + hysteresis[prev])
        i = prev;
    if (i < prev && val > thresholds[prev - 1] - hysteresis[prev - 1])
        i = prev;
    return i;
}

oac_uint32 oaci_celt_lcg_rand(oac_uint32 seed) {
    return 1664525*seed + 1013904223;
}

/* This is a cos() approximation designed to be bit-exact on any platform. Bit exactness
   with this approximation is important because it has an impact on the bit allocation */
oac_int16 oaci_bitexact_cos(oac_int16 x) {
    oac_int32 tmp;
    oac_int16 x2;
    tmp = (4096 + ((oac_int32)(x)*(x)))>>13;
    celt_sig_assert(tmp <= 32767);
    x2 = tmp;
    x2 = (32767 - x2) + FRAC_MUL16(x2, (-7651 + FRAC_MUL16(x2, (8277 + FRAC_MUL16(-626, x2)))));
    celt_sig_assert(x2 <= 32766);
    return 1 + x2;
}

int oaci_bitexact_log2tan(int isin, int icos) {
    int lc;
    int ls;
    lc = EC_ILOG(icos);
    ls = EC_ILOG(isin);
    icos <<= 15 - lc;
    isin <<= 15 - ls;
    return (ls - lc)*(1<<11)
           + FRAC_MUL16(isin, FRAC_MUL16(isin, -2597) + 7932)
           - FRAC_MUL16(icos, FRAC_MUL16(icos, -2597) + 7932);
}

#ifdef FIXED_POINT
/* Compute the amplitude (sqrt energy) in each of the bands */
void oaci_compute_band_energies(const CELTMode *m, const celt_sig *X, celt_ener *bandE, int end, int C, int LM, int arch) {
    int i, c, N;
    const oac_int16 *eBands = m->eBands;
    (void)arch;
    N = m->shortMdctSize<<LM;
    c = 0; do {
        for (i = 0; i < end; i++) {
            int j;
            oac_val32 maxval = 0;
            oac_val32 sum = 0;

            maxval = celt_maxabs32(&X[c*N + (eBands[i]<<LM)], (eBands[i + 1] - eBands[i])<<LM);
            if (maxval > 0) {
                int shift = IMAX(0,
                30 - oaci_celt_ilog2(maxval + (maxval>>14) + 1) - ((((m->logN[i] + 7)>>BITRES) + LM + 1)>>1));
                j = eBands[i]<<LM; do {
                    oac_val32 x = SHL32(X[j + c*N], shift);
                    sum = ADD32(sum, MULT32_32_Q31(x, x));
                } while (++j < eBands[i + 1]<<LM);
                bandE[i + c*m->nbEBands] = MAX32(maxval, PSHR32(oaci_celt_sqrt32(SHR32(sum, 1)), shift));
            } else {
                bandE[i + c*m->nbEBands] = EPSILON;
            }
        }
    } while (++c < C);
}

/* Normalise each band such that the energy is one. */
void oaci_normalise_bands(const CELTMode *m, const celt_sig * OAC_RESTRICT freq, celt_norm * OAC_RESTRICT X,
                     const celt_ener *bandE, int end, int C, int M) {
    int i, c, N;
    const oac_int16 *eBands = m->eBands;
    N = M*m->shortMdctSize;
    c = 0; do {
        i = 0; do {
            int j, shift;
            oac_val32 E;
            oac_val32 g;
            E = bandE[i + c*m->nbEBands];
            /* For very low energies, we need this to make sure not to prevent energy rounding from
               blowing up the normalized signal. */
            if (E < 10) E += EPSILON;
            shift = 30 - oaci_celt_zlog2(E);
            E = SHL32(E, shift);
            g = oaci_celt_rcp_norm32(E);
            j = M*eBands[i]; do {
                X[j + c*N] = PSHR32(MULT32_32_Q31(g, SHL32(freq[j + c*N], shift)), 30 - NORM_SHIFT);
            } while (++j < M*eBands[i + 1]);
        } while (++i < end);
    } while (++c < C);
}

#else /* FIXED_POINT */
/* Compute the amplitude (sqrt energy) in each of the bands */
void oaci_compute_band_energies(const CELTMode *m, const celt_sig *X, celt_ener *bandE, int end, int C, int LM, int arch) {
    int i, c, N;
    const oac_int16 *eBands = m->eBands;
    N = m->shortMdctSize<<LM;
    c = 0; do {
        for (i = 0; i < end; i++) {
            oac_val32 sum;
            sum = 1e-27f + oaci_celt_inner_prod(&X[c*N + (eBands[i]<<LM)], &X[c*N + (eBands[i]<<LM)],
                (eBands[i + 1] - eBands[i])<<LM, arch);
            bandE[i + c*m->nbEBands] = oaci_celt_sqrt(sum);
            /*printf ("%f ", bandE[i+c*m->nbEBands]);*/
        }
    } while (++c < C);
    /*printf ("\n");*/
}

/* Normalise each band such that the energy is one. */
void oaci_normalise_bands(const CELTMode *m, const celt_sig * OAC_RESTRICT freq, celt_norm * OAC_RESTRICT X,
                     const celt_ener *bandE, int end, int C, int M) {
    int i, c, N;
    const oac_int16 *eBands = m->eBands;
    N = M*m->shortMdctSize;
    c = 0; do {
        for (i = 0; i < end; i++) {
            int j;
            oac_val16 g = 1.f/(1e-27f + bandE[i + c*m->nbEBands]);
            for (j = M*eBands[i]; j < M*eBands[i + 1]; j++)
                X[j + c*N] = freq[j + c*N]*g;
        }
    } while (++c < C);
}

#endif /* FIXED_POINT */

/* De-normalise the energy to produce the synthesis from the unit-energy bands */
void oaci_denormalise_bands(const CELTMode *m, const celt_norm * OAC_RESTRICT X,
                       celt_sig * OAC_RESTRICT freq, const celt_glog *bandLogE, int start,
                       int end, int M, int downsample, int silence) {
    int i, N;
    int bound;
    celt_sig * OAC_RESTRICT f;
    const celt_norm * OAC_RESTRICT x;
    const oac_int16 *eBands = m->eBands;
    N = M*m->shortMdctSize;
    bound = M*eBands[end];
    if (downsample != 1)
        bound = IMIN(bound, N/downsample);
    if (silence) {
        bound = 0;
        start = end = 0;
    }
    f = freq;
    x = X + M*eBands[start];
    if (start != 0) {
        for (i = 0; i < M*eBands[start]; i++)
            *f++ = 0;
    } else {
        f += M*eBands[start];
    }
    for (i = start; i < end; i++) {
        int j, band_end;
        oac_val32 g;
        celt_glog lg;
#ifdef FIXED_POINT
        int shift;
#endif
        j = M*eBands[i];
        band_end = M*eBands[i + 1];
        lg = ADD32(bandLogE[i], SHL32((oac_val32)oaci_eMeans[i], DB_SHIFT - 4));
#ifndef FIXED_POINT
        g = celt_exp2_db(MIN32(32.f, lg));
#else
        /* Handle the integer part of the log energy */
        shift = 17 - (lg>>DB_SHIFT);
        if (shift >= 31) {
            shift = 0;
            g = 0;
        } else {
            /* Handle the fractional part. */
            g = SHL32(oaci_celt_exp2_db_frac((lg&((1<<DB_SHIFT) - 1))), 2);
        }
        /* Handle extreme gains with negative shift. */
        if (shift < 0) {
            /* To avoid overflow, we're
               capping the gain here, which is equivalent to a cap of 18 on lg.
               This shouldn't trigger unless the bitstream is already corrupted. */
            g = 2147483647;
            shift = 0;
        }
#endif
        do {
            *f++ = PSHR32(MULT32_32_Q31(SHL32(*x, 30 - NORM_SHIFT), g), shift);
            x++;
        } while (++j < band_end);
    }
    celt_assert(start <= end);
    OAC_CLEAR(&freq[bound], N - bound);
}

/* This prevents energy collapse for transients with multiple short MDCTs */
void oaci_anti_collapse(const CELTMode *m, celt_norm *X_, unsigned char *collapse_masks, int LM, int C, int size,
                   int start, int end, const celt_glog *logE, const celt_glog *prev1logE,
                   const celt_glog *prev2logE, const int *pulses, oac_uint32 seed, int encode, int arch) {
    int c, i, j, k;
    for (i = start; i < end; i++) {
        int N0;
        oac_val16 thresh, sqrt_1;
        int depth;
#ifdef FIXED_POINT
        int shift;
        oac_val32 thresh32;
#endif

        N0 = m->eBands[i + 1] - m->eBands[i];
        /* depth in 1/8 bits */
        celt_sig_assert(pulses[i] >= 0);
        depth = oaci_celt_udiv(1 + pulses[i], (m->eBands[i + 1] - m->eBands[i]))>>LM;

#ifdef FIXED_POINT
        thresh32 = SHR32(oaci_celt_exp2(-SHL16(depth, 10 - BITRES)), 1);
        thresh = MULT16_32_Q15(QCONST16(0.5f, 15), MIN32(32767, thresh32));
        {
            oac_val32 t;
            t = N0<<LM;
            shift = oaci_celt_ilog2(t)>>1;
            t = SHL32(t, (7 - shift)<<1);
            sqrt_1 = oaci_celt_rsqrt_norm(t);
        }
#else
        thresh = .5f*oaci_celt_exp2(-.125f*depth);
        sqrt_1 = oaci_celt_rsqrt(N0<<LM);
#endif

        c = 0; do {
            celt_norm *X;
            celt_glog prev1;
            celt_glog prev2;
            oac_val32 Ediff;
            celt_norm r;
            int renormalize = 0;
            prev1 = prev1logE[c*m->nbEBands + i];
            prev2 = prev2logE[c*m->nbEBands + i];
            if (!encode && C == 1) {
                prev1 = MAXG(prev1, prev1logE[m->nbEBands + i]);
                prev2 = MAXG(prev2, prev2logE[m->nbEBands + i]);
            }
            Ediff = logE[c*m->nbEBands + i] - MING(prev1, prev2);
            Ediff = MAX32(0, Ediff);

#ifdef FIXED_POINT
            if (Ediff < GCONST(16.f)) {
                oac_val32 r32 = SHR32(celt_exp2_db(-Ediff), 1);
                r = 2*MIN16(16383, r32);
            } else {
                r = 0;
            }
            if (LM == 3)
                r = MULT16_16_Q14(23170, MIN32(23169, r));
            r = SHR16(MIN16(thresh, r), 1);
            r = VSHR32(MULT16_16_Q15(sqrt_1, r), shift + 14 - NORM_SHIFT);
#else
            /* r needs to be multiplied by 2 or 2*sqrt(2) depending on LM because
               short blocks don't have the same energy as long */
            r = 2.f*celt_exp2_db(-Ediff);
            if (LM == 3)
                r *= 1.41421356f;
            r = MIN16(thresh, r);
            r = r*sqrt_1;
#endif
            X = X_ + c*size + (m->eBands[i]<<LM);
            for (k = 0; k < 1<<LM; k++) {
                /* Detect collapse */
                if (!(collapse_masks[i*C + c]&1<<k)) {
                    /* Fill with noise */
                    for (j = 0; j < N0; j++) {
                        seed = oaci_celt_lcg_rand(seed);
                        X[(j<<LM) + k] = (seed&0x8000 ? r : -r);
                    }
                    renormalize = 1;
                }
            }
            /* We just added some energy, so we need to renormalise */
            if (renormalize)
                oaci_renormalise_vector(X, N0<<LM, Q31ONE, arch);
        } while (++c < C);
    }
}

/* Compute the weights to use for optimizing normalized distortion across
   channels. We use the amplitude to weight square distortion, which means
   that we use the square root of the value we would have been using if we
   wanted to minimize the MSE in the non-normalized domain. This roughly
   corresponds to some quick-and-dirty perceptual experiments I ran to
   measure inter-aural masking (there doesn't seem to be any published data
   on the topic). */
static void oaci_compute_channel_weights(celt_ener Ex, celt_ener Ey, oac_val16 w[2]) {
    celt_ener minE;
#ifdef FIXED_POINT
    int shift;
#endif
    minE = MIN32(Ex, Ey);
    /* Adjustment to make the weights a bit more conservative. */
    Ex = ADD32(Ex, minE/3);
    Ey = ADD32(Ey, minE/3);
#ifdef FIXED_POINT
    shift = oaci_celt_ilog2(EPSILON + MAX32(Ex, Ey)) - 14;
#endif
    w[0] = VSHR32(Ex, shift);
    w[1] = VSHR32(Ey, shift);
}

static void oaci_intensity_stereo(const CELTMode *m, celt_norm * OAC_RESTRICT X, const celt_norm * OAC_RESTRICT Y,
                             const celt_ener *bandE, int bandID, int N) {
    int i = bandID;
    int j;
    oac_val16 a1, a2;
    oac_val16 left, right;
    oac_val16 norm;
#ifdef FIXED_POINT
    int shift = oaci_celt_zlog2(MAX32(bandE[i], bandE[i + m->nbEBands])) - 13;
#endif
    left = VSHR32(bandE[i], shift);
    right = VSHR32(bandE[i + m->nbEBands], shift);
    norm = EPSILON + oaci_celt_sqrt(EPSILON + MULT16_16(left, left) + MULT16_16(right, right));
#ifdef FIXED_POINT
    left = MIN32(left, norm - 1);
    right = MIN32(right, norm - 1);
#endif
    a1 = DIV32_16(SHL32(EXTEND32(left), 15), norm);
    a2 = DIV32_16(SHL32(EXTEND32(right), 15), norm);
    for (j = 0; j < N; j++) {
        X[j] = ADD32(MULT16_32_Q15(a1, X[j]), MULT16_32_Q15(a2, Y[j]));
        /* Side is not encoded, no need to calculate */
    }
}

static void oaci_stereo_split(celt_norm * OAC_RESTRICT X, celt_norm * OAC_RESTRICT Y, int N) {
    int j;
    for (j = 0; j < N; j++) {
        oac_val32 r, l;
        l = MULT32_32_Q31(QCONST32(.70710678f, 31), X[j]);
        r = MULT32_32_Q31(QCONST32(.70710678f, 31), Y[j]);
        X[j] = ADD32(l, r);
        Y[j] = SUB32(r, l);
    }
}

static void oaci_stereo_merge(celt_norm * OAC_RESTRICT X, celt_norm * OAC_RESTRICT Y, oac_val32 mid, int N, int arch) {
    int j;
    oac_val32 xp = 0, side = 0;
    oac_val32 El, Er;
#ifdef FIXED_POINT
    int kl, kr;
#endif
    oac_val32 t, lgain, rgain;

    /* Compute the norm of X+Y and X-Y as |X|^2 + |Y|^2 +/- sum(xy) */
    xp = oaci_celt_inner_prod_norm_shift(Y, X, N, arch);
    side = oaci_celt_inner_prod_norm_shift(Y, Y, N, arch);
    /* Compensating for the mid normalization */
    xp = MULT32_32_Q31(mid, xp);
    /* mid and side are in Q15, not Q14 like X and Y */
    El = SHR32(MULT32_32_Q31(mid, mid), 3) + side - 2*xp;
    Er = SHR32(MULT32_32_Q31(mid, mid), 3) + side + 2*xp;
    if (Er < QCONST32(6e-4f, 28) || El < QCONST32(6e-4f, 28)) {
        OAC_COPY(Y, X, N);
        return;
    }

#ifdef FIXED_POINT
    kl = oaci_celt_ilog2(El)>>1;
    kr = oaci_celt_ilog2(Er)>>1;
#endif
    t = VSHR32(El, (kl<<1) - 29);
    lgain = oaci_celt_rsqrt_norm32(t);
    t = VSHR32(Er, (kr<<1) - 29);
    rgain = oaci_celt_rsqrt_norm32(t);

#ifdef FIXED_POINT
    if (kl < 7)
        kl = 7;
    if (kr < 7)
        kr = 7;
#endif

    for (j = 0; j < N; j++) {
        celt_norm r, l;
        /* Apply mid scaling (side is already scaled) */
        l = MULT32_32_Q31(mid, X[j]);
        r = Y[j];
        X[j] = VSHR32(MULT32_32_Q31(lgain, SUB32(l, r)), kl - 15);
        Y[j] = VSHR32(MULT32_32_Q31(rgain, ADD32(l, r)), kr - 15);
    }
}

/* Decide whether we should spread the pulses in the current frame */
int oaci_spreading_decision(const CELTMode *m, const celt_norm *X, int *average,
                       int last_decision, int *hf_average, int *tapset_decision, int update_hf,
                       int end, int C, int M, const int *spread_weight) {
    int i, c, N0;
    int sum = 0, nbBands = 0;
    const oac_int16 * OAC_RESTRICT eBands = m->eBands;
    int decision;
    int hf_sum = 0;

    celt_assert(end > 0);

    N0 = M*m->shortMdctSize;

    if (M*(eBands[end] - eBands[end - 1]) <= 8)
        return SPREAD_NONE;
    c = 0; do {
        for (i = 0; i < end; i++) {
            int j, N, tmp = 0;
            int tcount[3] = {0, 0, 0};
            const celt_norm * OAC_RESTRICT x = X + M*eBands[i] + c*N0;
            N = M*(eBands[i + 1] - eBands[i]);
            if (N <= 8)
                continue;
            /* Compute rough CDF of |x[j]| */
            for (j = 0; j < N; j++) {
                oac_val32 x2N; /* Q13 */

                x2N = MULT16_16(MULT16_16_Q15(SHR32(x[j], NORM_SHIFT - 14), SHR32(x[j], NORM_SHIFT - 14)), N);
                if (x2N < QCONST16(0.25f, 13))
                    tcount[0]++;
                if (x2N < QCONST16(0.0625f, 13))
                    tcount[1]++;
                if (x2N < QCONST16(0.015625f, 13))
                    tcount[2]++;
            }

            /* Only include four last bands (8 kHz and up) */
            if (i > m->nbEBands - 4)
                hf_sum += oaci_celt_udiv(32*(tcount[1] + tcount[0]), N);
            tmp = (2*tcount[2] >= N) + (2*tcount[1] >= N) + (2*tcount[0] >= N);
            sum += tmp*spread_weight[i];
            nbBands += spread_weight[i];
        }
    } while (++c < C);

    if (update_hf) {
        if (hf_sum)
            hf_sum = oaci_celt_udiv(hf_sum, C*(4 - m->nbEBands + end));
        *hf_average = (*hf_average + hf_sum)>>1;
        hf_sum = *hf_average;
        if (*tapset_decision == 2)
            hf_sum += 4;
        else if (*tapset_decision == 0)
            hf_sum -= 4;
        if (hf_sum > 22)
            *tapset_decision = 2;
        else if (hf_sum > 18)
            *tapset_decision = 1;
        else
            *tapset_decision = 0;
    }
    /*printf("%d %d %d\n", hf_sum, *hf_average, *tapset_decision);*/
    celt_assert(nbBands > 0); /* end has to be non-zero */
    celt_assert(sum >= 0);
    sum = oaci_celt_udiv((oac_int32)sum<<8, nbBands);
    /* Recursive averaging */
    sum = (sum + *average)>>1;
    *average = sum;
    /* Hysteresis */
    sum = (3*sum + (((3 - last_decision)<<7) + 64) + 2)>>2;
    if (sum < 80) {
        decision = SPREAD_AGGRESSIVE;
    } else if (sum < 256) {
        decision = SPREAD_NORMAL;
    } else if (sum < 384) {
        decision = SPREAD_LIGHT;
    } else {
        decision = SPREAD_NONE;
    }
#ifdef FUZZING
    decision = rand()&0x3;
    *tapset_decision = rand()%3;
#endif
    return decision;
}

/* Indexing table for converting from natural Hadamard to ordery Hadamard
   This is essentially a bit-reversed Gray, on top of which we've added
   an inversion of the order because we want the DC at the end rather than
   the beginning. The lines are for N=2, 4, 8, 16 */
static const int ordery_table[] = {
    1,  0,
    3,  0,  2,  1,
    7,  0,  4,  3,  6,  1,  5,  2,
    15,  0,  8,  7, 12,  3, 11,  4, 14,  1,  9,  6, 13,  2, 10,  5,
};

static void oaci_deinterleave_hadamard(celt_norm *X, int N0, int stride, int hadamard) {
    int i, j;
    VARDECL(celt_norm, tmp);
    int N;
    SAVE_STACK;
    N = N0*stride;
    ALLOC(tmp, N, celt_norm);
    celt_assert(stride > 0);
    if (hadamard) {
        const int *ordery = ordery_table + stride - 2;
        for (i = 0; i < stride; i++) {
            for (j = 0; j < N0; j++)
                tmp[ordery[i]*N0 + j] = X[j*stride + i];
        }
    } else {
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[i*N0 + j] = X[j*stride + i];
    }
    OAC_COPY(X, tmp, N);
    RESTORE_STACK;
}

static void oaci_interleave_hadamard(celt_norm *X, int N0, int stride, int hadamard) {
    int i, j;
    VARDECL(celt_norm, tmp);
    int N;
    SAVE_STACK;
    N = N0*stride;
    ALLOC(tmp, N, celt_norm);
    if (hadamard) {
        const int *ordery = ordery_table + stride - 2;
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[j*stride + i] = X[ordery[i]*N0 + j];
    } else {
        for (i = 0; i < stride; i++)
            for (j = 0; j < N0; j++)
                tmp[j*stride + i] = X[i*N0 + j];
    }
    OAC_COPY(X, tmp, N);
    RESTORE_STACK;
}

void oaci_haar1(celt_norm *X, int N0, int stride) {
    int i, j;
    N0 >>= 1;
    for (i = 0; i < stride; i++)
        for (j = 0; j < N0; j++) {
            oac_val32 tmp1, tmp2;
            tmp1 = MULT32_32_Q31(QCONST32(.70710678f, 31), X[stride*2*j + i]);
            tmp2 = MULT32_32_Q31(QCONST32(.70710678f, 31), X[stride*(2*j + 1) + i]);
            X[stride*2*j + i] = ADD32(tmp1, tmp2);
            X[stride*(2*j + 1) + i] = SUB32(tmp1, tmp2);
        }
}

static int oaci_compute_qn(int N, int b, int offset, int pulse_cap, int stereo) {
    static const oac_int16 exp2_table8[8] =
    {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
    int qn, qb;
    int N2 = 2*N - 1;
    if (stereo && N == 2)
        N2--;
    /* The upper limit ensures that in a stereo split with itheta==16384, we'll
        always have enough bits left over to code at least one pulse in the
        side; otherwise it would collapse, since it doesn't get folded. */
    qb = oaci_celt_sudiv(b + N2*offset, N2);
    qb = IMIN(b - pulse_cap - (4<<BITRES), qb);

    qb = IMIN(8<<BITRES, qb);

    if (qb < (1<<BITRES>>1)) {
        qn = 1;
    } else {
        qn = exp2_table8[qb&0x7]>>(14 - (qb>>BITRES));
        qn = (qn + 1)>>1<<1;
    }
    celt_assert(qn <= 256);
    return qn;
}

struct band_ctx {
    int encode;
    int resynth;
    const CELTMode *m;
    int i;
    int intensity;
    int spread;
    int tf_change;
    ec_ctx *ec;
    oac_int32 total_bits;
    const celt_ener *bandE;
    oac_uint32 seed;
    int arch;
    int theta_round;
    int disable_inv;
    int avoid_split_noise;
};

struct split_ctx {
    int inv;
    int imid;
    int iside;
    int delta;
    int itheta;
    int itheta_q30;
};

static void oaci_compute_theta(struct band_ctx *ctx, struct split_ctx *sctx,
                          celt_norm *X, celt_norm *Y, int N, int *b, int B, int B0,
                          int LM,
                          int stereo, int *fill) {
    int qn;
    int itheta = 0;
    int itheta_q30 = 0;
    int delta;
    int imid, iside;
    int qalloc;
    int pulse_cap;
    int offset;
    oac_int32 tell;
    int inv = 0;
    int encode;
    const CELTMode *m;
    int i;
    int intensity;
    ec_ctx *ec;
    const celt_ener *bandE;

    encode = ctx->encode;
    m = ctx->m;
    i = ctx->i;
    intensity = ctx->intensity;
    ec = ctx->ec;
    bandE = ctx->bandE;

    /* Decide on the resolution to give to the split parameter theta */
    pulse_cap = m->logN[i] + LM*(1<<BITRES);
    offset = (pulse_cap>>1) - (stereo && N == 2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
    qn = oaci_compute_qn(N, *b, offset, pulse_cap, stereo);
    if (stereo && i >= intensity)
        qn = 1;
    if (encode) {
        /* theta is the atan() of the ratio between the (normalized)
           side and mid. With just that parameter, we can re-scale both
           mid and side because we know that 1) they have unit norm and
           2) they are orthogonal. */
        itheta_q30 = oaci_stereo_itheta(X, Y, stereo, N, ctx->arch);
        itheta = itheta_q30>>16;
    }
    tell = oaci_ec_tell_frac(ec);
    if (qn != 1) {
        if (encode) {
            if (!stereo || ctx->theta_round == 0) {
                itheta = (itheta*(oac_int32)qn + 8192)>>14;
                if (!stereo && ctx->avoid_split_noise && itheta > 0 && itheta < qn) {
                    /* Check if the selected value of theta will cause the bit allocation
                       to inject noise on one side. If so, make sure the energy of that side
                       is zero. */
                    int unquantized = oaci_celt_udiv((oac_int32)itheta*16384, qn);
                    imid = oaci_bitexact_cos((oac_int16)unquantized);
                    iside = oaci_bitexact_cos((oac_int16)(16384 - unquantized));
                    delta = FRAC_MUL16((N - 1)<<7, oaci_bitexact_log2tan(iside, imid));
                    if (delta > *b)
                        itheta = qn;
                    else if (delta < -*b)
                        itheta = 0;
                }
            } else {
                int down;
                /* Bias quantization towards itheta=0 and itheta=16384. */
                int bias = itheta > 8192 ? 32767/qn : -32767/qn;
                down = IMIN(qn - 1, IMAX(0, (itheta*(oac_int32)qn + bias)>>14));
                if (ctx->theta_round < 0)
                    itheta = down;
                else
                    itheta = down + 1;
            }
        }
        /* Entropy coding of the angle. We use a uniform pdf for the
           time split, a step for stereo, and a triangular one for the rest. */
        if (stereo && N > 2) {
            int p0 = 3;
            int x = itheta;
            int x0 = qn/2;
            int ft = p0*(x0 + 1) + x0;
            /* Use a probability of p0 up to itheta=8192 and then use 1 after */
            if (encode) {
                oaci_ec_encode(ec, x <= x0?p0*x:(x - 1 - x0) + (x0 + 1)*p0, x <= x0?p0*(x + 1):(x - x0) + (x0 + 1)*p0, ft);
            } else {
                int fs;
                fs = oaci_ec_decode(ec, ft);
                if (fs < (x0 + 1)*p0)
                    x = fs/p0;
                else
                    x = x0 + 1 + (fs - (x0 + 1)*p0);
                oaci_ec_dec_update(ec, x <= x0?p0*x:(x - 1 - x0) + (x0 + 1)*p0, x <= x0?p0*(x + 1):(x - x0) + (x0 + 1)*p0,
                ft);
                itheta = x;
            }
        } else if (B0 > 1 || stereo) {
            /* Uniform pdf */
            if (encode)
                oaci_ec_enc_uint(ec, itheta, qn + 1);
            else
                itheta = oaci_ec_dec_uint(ec, qn + 1);
        } else {
            int fs = 1, ft;
            ft = ((qn>>1) + 1)*((qn>>1) + 1);
            if (encode) {
                int fl;

                fs = itheta <= (qn>>1) ? itheta + 1 : qn + 1 - itheta;
                fl = itheta <= (qn>>1) ? itheta*(itheta + 1)>>1 :
                     ft - ((qn + 1 - itheta)*(qn + 2 - itheta)>>1);

                oaci_ec_encode(ec, fl, fl + fs, ft);
            } else {
                /* Triangular pdf */
                int fl = 0;
                int fm;
                fm = oaci_ec_decode(ec, ft);

                if (fm < ((qn>>1)*((qn>>1) + 1)>>1)) {
                    itheta = (oaci_isqrt32(8*(oac_uint32)fm + 1) - 1)>>1;
                    fs = itheta + 1;
                    fl = itheta*(itheta + 1)>>1;
                } else {
                    itheta = (2*(qn + 1)
                              - oaci_isqrt32(8*(oac_uint32)(ft - fm - 1) + 1))>>1;
                    fs = qn + 1 - itheta;
                    fl = ft - ((qn + 1 - itheta)*(qn + 2 - itheta)>>1);
                }

                oaci_ec_dec_update(ec, fl, fl + fs, ft);
            }
        }
        celt_assert(itheta >= 0);
        itheta = oaci_celt_udiv((oac_int32)itheta*16384, qn);
        itheta_q30 = (oac_int32)itheta<<16;
        if (encode && stereo) {
            if (itheta == 0)
                oaci_intensity_stereo(m, X, Y, bandE, i, N);
            else
                oaci_stereo_split(X, Y, N);
        }
        /* NOTE: Renormalising X and Y *may* help fixed-point a bit at very high rate.
                 Let's do that at higher complexity */
    } else if (stereo) {
        if (encode) {
            inv = itheta > 8192 && !ctx->disable_inv;
            if (inv) {
                int j;
                for (j = 0; j < N; j++)
                    Y[j] = -Y[j];
            }
            oaci_intensity_stereo(m, X, Y, bandE, i, N);
        }
        if (*b > 2<<BITRES && ctx->total_bits - tell > 2<<BITRES) {
            if (encode)
                oaci_ec_enc_bit_logp(ec, inv, 2);
            else
                inv = oaci_ec_dec_bit_logp(ec, 2);
        } else
            inv = 0;
        /* inv flag override to avoid problems with downmixing. */
        if (ctx->disable_inv)
            inv = 0;
        itheta = 0;
        itheta_q30 = 0;
    }
    qalloc = oaci_ec_tell_frac(ec) - tell;
    *b -= qalloc;

    if (itheta == 0) {
        imid = 32767;
        iside = 0;
        *fill &= (1<<B) - 1;
        delta = -16384;
    } else if (itheta == 16384) {
        imid = 0;
        iside = 32767;
        *fill &= ((1<<B) - 1)<<B;
        delta = 16384;
    } else {
        imid = oaci_bitexact_cos((oac_int16)itheta);
        iside = oaci_bitexact_cos((oac_int16)(16384 - itheta));
        /* This is the mid vs side allocation that minimizes squared error
           in that band. */
        delta = FRAC_MUL16((N - 1)<<7, oaci_bitexact_log2tan(iside, imid));
    }

    sctx->inv = inv;
    sctx->imid = imid;
    sctx->iside = iside;
    sctx->delta = delta;
    sctx->itheta = itheta;
    sctx->itheta_q30 = itheta_q30;
}
static unsigned oaci_quant_band_n1(struct band_ctx *ctx, celt_norm *X, celt_norm *Y,
                              celt_norm *lowband_out) {
    int c;
    int stereo;
    celt_norm *x = X;
    int encode;
    ec_ctx *ec;

    encode = ctx->encode;
    ec = ctx->ec;

    stereo = Y != NULL;
    c = 0; do {
        int sign = 0;
        if (ctx->total_bits - oaci_ec_tell_frac(ec) > 1<<BITRES) {
            if (encode) {
                sign = x[0] < 0;
                oaci_ec_enc_bits(ec, sign, 1);
            } else {
                sign = oaci_ec_dec_bits(ec, 1);
            }
        }
        if (ctx->resynth)
            x[0] = sign ? -NORM_SCALING : NORM_SCALING;
        x = Y;
    } while (++c < 1 + stereo);
    if (lowband_out)
        lowband_out[0] = SHR32(X[0], 4);
    return 1;
}

/* This function is responsible for encoding and decoding a mono partition.
   It can split the band in two and transmit the energy difference with
   the two half-bands. It can be called recursively so bands can end up being
   split in 8 parts. */
static unsigned oaci_quant_partition(struct band_ctx *ctx, celt_norm *X,
                                int N, int b, int B, celt_norm *lowband,
                                int LM,
                                oac_val32 gain, int fill) {
    const unsigned char *cache;
    int q;
    int curr_bits;
    int imid = 0, iside = 0;
    int B0 = B;
    oac_val32 mid = 0, side = 0;
    unsigned cm = 0;
    celt_norm *Y = NULL;
    int encode;
    const CELTMode *m;
    int i;
    int spread;
    ec_ctx *ec;
    oac_int32 tell;

    encode = ctx->encode;
    m = ctx->m;
    i = ctx->i;
    spread = ctx->spread;
    ec = ctx->ec;

    /* If we need 1.5 more bit than we can produce, split the band in two. */
    cache = m->cache.bits + m->cache.index[(LM + 1)*m->nbEBands + i];
    if (LM != -1 && b > cache[cache[0]] + 12 && N > 2) {
        int mbits, sbits, delta;
        int itheta;
        struct split_ctx sctx;
        celt_norm *next_lowband2 = NULL;
        oac_int32 rebalance;

        N >>= 1;
        Y = X + N;
        LM -= 1;
        if (B == 1)
            fill = (fill&1)|(fill<<1);
        B = (B + 1)>>1;

        oaci_compute_theta(ctx, &sctx, X, Y, N, &b, B, B0, LM, 0, &fill);
        imid = sctx.imid;
        iside = sctx.iside;
        delta = sctx.delta;
        itheta = sctx.itheta;
        (void)imid;
        (void)iside;
#ifdef FIXED_POINT
        mid = oaci_celt_cos_norm32(sctx.itheta_q30);
        side = oaci_celt_cos_norm32((1<<30) - sctx.itheta_q30);
#else
        mid = oaci_celt_cos_norm2(sctx.itheta_q30*(1.f/(1<<30)));
        side = oaci_celt_cos_norm2(1.f - sctx.itheta_q30*(1.f/(1<<30)));
#endif

        /* Give more bits to low-energy MDCTs than they would otherwise deserve */
        if (B0 > 1 && (itheta&0x3fff)) {
            if (itheta > 8192)
                /* Rough approximation for pre-echo masking */
                delta -= delta>>(4 - LM);
            else
                /* Corresponds to a forward-masking slope of 1.5 dB per 10 ms */
                delta = IMIN(0, delta + (N<<BITRES>>(5 - LM)));
        }
        mbits = IMAX(0, IMIN(b, (b - delta)/2));
        sbits = b - mbits;

        if (lowband)
            next_lowband2 = lowband + N; /* >32-bit split case */

        tell = oaci_ec_tell_frac(ec);
        if (mbits >= sbits) {
            cm = oaci_quant_partition(ctx, X, N, mbits, B, lowband, LM,
               MULT32_32_Q31(gain, mid), fill);
            rebalance = mbits - (oaci_ec_tell_frac(ec) - tell);
            if (rebalance > 3<<BITRES && itheta != 0)
                sbits += rebalance - (3<<BITRES);
            cm |= oaci_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM,
               MULT32_32_Q31(gain, side), fill>>B)<<(B0>>1);
        } else {
            cm = oaci_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM,
               MULT32_32_Q31(gain, side), fill>>B)<<(B0>>1);
            rebalance = sbits - (oaci_ec_tell_frac(ec) - tell);
            if (rebalance > 3<<BITRES && itheta != 16384)
                mbits += rebalance - (3<<BITRES);
            cm |= oaci_quant_partition(ctx, X, N, mbits, B, lowband, LM,
               MULT32_32_Q31(gain, mid), fill);
        }
    } else {
        oac_int32 remaining_bits;
        /* This is the basic no-split case */
        q = oaci_bits2pulses(m, i, LM, b);
        curr_bits = oaci_pulses2bits(m, i, LM, q);
        remaining_bits = ctx->total_bits - oaci_ec_tell_frac(ec) - 1;
        remaining_bits -= curr_bits;

        /* Ensures we can never bust the budget */
        while (remaining_bits < 0 && q > 0) {
            remaining_bits += curr_bits;
            q--;
            curr_bits = oaci_pulses2bits(m, i, LM, q);
            remaining_bits -= curr_bits;
        }

        if (q != 0) {
            int K = oaci_get_pulses(q);

            /* Finally do the actual quantization */
            if (encode) {
                cm = oaci_alg_quant(X, N, K, spread, B, ec, gain, ctx->resynth,
                           ctx->arch);
            } else {
                cm = oaci_alg_unquant(X, N, K, spread, B, ec, gain);
            }
        } else {
            /* If there's no pulse, fill the band anyway */
            int j;
            if (ctx->resynth) {
                unsigned cm_mask;
                /* B can be as large as 16, so this shift might overflow an int on a
                   16-bit platform; use a long to get defined behavior.*/
                cm_mask = (unsigned)(1UL<<B) - 1;
                fill &= cm_mask;
                if (!fill) {
                    OAC_CLEAR(X, N);
                } else {
                    if (lowband == NULL) {
                        /* Noise */
                        for (j = 0; j < N; j++) {
                            ctx->seed = oaci_celt_lcg_rand(ctx->seed);
                            X[j] = SHL32((celt_norm)((oac_int32)ctx->seed>>20), NORM_SHIFT - 14);
                        }
                        cm = cm_mask;
                    } else {
                        /* Folded spectrum */
                        for (j = 0; j < N; j++) {
                            oac_val16 tmp;
                            ctx->seed = oaci_celt_lcg_rand(ctx->seed);
                            /* About 48 dB below the "normal" folding level */
                            tmp = QCONST16(1.0f/256, NORM_SHIFT - 4);
                            tmp = (ctx->seed)&0x8000 ? tmp : -tmp;
                            X[j] = lowband[j] + tmp;
                        }
                        cm = fill;
                    }
                    oaci_renormalise_vector(X, N, gain, ctx->arch);
                }
            }
        }
    }

    return cm;
}

unsigned oaci_cubic_quant_partition(struct band_ctx *ctx, celt_norm *X, int N, int b, int B, ec_ctx *ec, int LM,
                               oac_val32 gain, int resynth, int encode) {
    oac_int32 remaining_bits = ctx->total_bits - oaci_ec_tell_frac(ctx->ec) - 1;
    celt_assert(LM >= 0);
    b = IMIN(b, remaining_bits);
    /* As long as we have at least two bits of depth, split all the way to LM=0 (not -1 like PVQ). */
    if (LM == 0 || b <= 2*N<<BITRES) {
        int res, ret;
        b = IMIN(b + ((N - 1)<<BITRES)/2, remaining_bits);
        /* Resolution left after taking into account coding the cube face. */
        res = (b - (1<<BITRES) - ctx->m->logN[ctx->i] - (LM<<BITRES) - 1)/(N - 1)>>BITRES;
        res = IMIN(14, IMAX(0, res));
        if (encode) ret = oaci_cubic_quant(X, N, res, B, ec, gain, resynth);
        else ret = oaci_cubic_unquant(X, N, res, B, ec, gain);
        return ret;
    } else {
        celt_norm *Y;
        oac_int32 itheta_q30;
        oac_val32 g1, g2;
        oac_int32 theta_res;
        oac_int32 qtheta;
        int delta;
        int b1, b2;
        int cm;
        int N0;
        N0 = N;
        N >>= 1;
        Y = X + N;
        LM -= 1;
        B = (B + 1)>>1;
        theta_res = IMIN(16, (b>>BITRES)/(N0 - 1) + 1);
        if (encode) {
            itheta_q30 = oaci_stereo_itheta(X, Y, 0, N, ctx->arch);
            qtheta = (itheta_q30 + (1<<(29 - theta_res)))>>(30 - theta_res);
            oaci_ec_enc_uint(ec, qtheta, (1<<theta_res) + 1);
        } else {
            qtheta = oaci_ec_dec_uint(ec, (1<<theta_res) + 1);
        }
        itheta_q30 = qtheta<<(30 - theta_res);
        b -= theta_res<<BITRES;
        delta = (N0 - 1)*23*((itheta_q30>>16) - 8192)>>(17 - BITRES);

#ifdef FIXED_POINT
        g1 = oaci_celt_cos_norm32(itheta_q30);
        g2 = oaci_celt_cos_norm32((1<<30) - itheta_q30);
#else
        g1 = oaci_celt_cos_norm2(itheta_q30*(1.f/(1<<30)));
        g2 = oaci_celt_cos_norm2(1.f - itheta_q30*(1.f/(1<<30)));
#endif
        if (itheta_q30 == 0) {
            b1 = b;
            b2 = 0;
        } else if (itheta_q30 == 1073741824) {
            b1 = 0;
            b2 = b;
        } else {
            b1 = IMIN(b, IMAX(0, (b - delta)/2));
            b2 = b - b1;
        }
        cm  = oaci_cubic_quant_partition(ctx, X, N, b1, B, ec, LM, MULT32_32_Q31(gain, g1), resynth, encode);
        cm |= oaci_cubic_quant_partition(ctx, Y, N, b2, B, ec, LM, MULT32_32_Q31(gain, g2), resynth, encode);
        return cm;
    }
}

/* This function is responsible for encoding and decoding a band for the mono case. */
static unsigned oaci_quant_band(struct band_ctx *ctx, celt_norm *X,
                           int N, int b, int B, celt_norm *lowband,
                           int LM, celt_norm *lowband_out,
                           oac_val32 gain, celt_norm *lowband_scratch, int fill) {
    int N0 = N;
    int N_B = N;
    int N_B0;
    int B0 = B;
    int time_divide = 0;
    int recombine = 0;
    int longBlocks;
    unsigned cm = 0;
    int k;
    int encode;
    int tf_change;

    encode = ctx->encode;
    tf_change = ctx->tf_change;

    longBlocks = B0 == 1;

    N_B = oaci_celt_udiv(N_B, B);

    /* Special case for one sample */
    if (N == 1) {
        return oaci_quant_band_n1(ctx, X, NULL, lowband_out);
    }

    if (tf_change > 0)
        recombine = tf_change;
    /* Band recombining to increase frequency resolution */

    if (lowband_scratch && lowband && (recombine || ((N_B&1) == 0 && tf_change < 0) || B0 > 1)) {
        OAC_COPY(lowband_scratch, lowband, N);
        lowband = lowband_scratch;
    }

    for (k = 0; k < recombine; k++) {
        static const unsigned char bit_interleave_table[16] = {
            0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3
        };
        if (encode)
            oaci_haar1(X, N>>k, 1<<k);
        if (lowband)
            oaci_haar1(lowband, N>>k, 1<<k);
        fill = bit_interleave_table[fill&0xF]|bit_interleave_table[fill>>4]<<2;
    }
    B >>= recombine;
    N_B <<= recombine;

    /* Increasing the time resolution */
    while ((N_B&1) == 0 && tf_change < 0) {
        if (encode)
            oaci_haar1(X, N_B, B);
        if (lowband)
            oaci_haar1(lowband, N_B, B);
        fill |= fill<<B;
        B <<= 1;
        N_B >>= 1;
        time_divide++;
        tf_change++;
    }
    B0 = B;
    N_B0 = N_B;

    /* Reorganize the samples in time order instead of frequency order */
    if (B0 > 1) {
        if (encode)
            oaci_deinterleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
        if (lowband)
            oaci_deinterleave_hadamard(lowband, N_B>>recombine, B0<<recombine, longBlocks);
    }

    cm = oaci_quant_partition(ctx, X, N, b, B, lowband, LM, gain, fill);

    /* This code is used by the decoder and by the resynthesis-enabled encoder */
    if (ctx->resynth) {
        /* Undo the sample reorganization going from time order to frequency order */
        if (B0 > 1)
            oaci_interleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);

        /* Undo time-freq changes that we did earlier */
        N_B = N_B0;
        B = B0;
        for (k = 0; k < time_divide; k++) {
            B >>= 1;
            N_B <<= 1;
            cm |= cm>>B;
            oaci_haar1(X, N_B, B);
        }

        for (k = 0; k < recombine; k++) {
            static const unsigned char bit_deinterleave_table[16] = {
                0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
                0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
            };
            cm = bit_deinterleave_table[cm];
            oaci_haar1(X, N0>>k, 1<<k);
        }
        B <<= recombine;

        /* Scale output for later folding */
        if (lowband_out) {
            int j;
            oac_val16 n;
            n = oaci_celt_sqrt(SHL32(EXTEND32(N0), 22));
            for (j = 0; j < N0; j++)
                lowband_out[j] = MULT16_32_Q15(n, X[j]);
        }
        cm &= (1<<B) - 1;
    }
    return cm;
}

#ifdef FIXED_POINT
# define MIN_STEREO_ENERGY 2
#else
# define MIN_STEREO_ENERGY 1e-10f
#endif

/* This function is responsible for encoding and decoding a band for the stereo case. */
static unsigned oaci_quant_band_stereo(struct band_ctx *ctx, celt_norm *X, celt_norm *Y,
                                  int N, int b, int B, celt_norm *lowband,
                                  int LM, celt_norm *lowband_out,
                                  celt_norm *lowband_scratch, int fill) {
    int imid = 0, iside = 0;
    int inv = 0;
    oac_val32 mid = 0, side = 0;
    unsigned cm = 0;
    int mbits, sbits, delta;
    int itheta;
    struct split_ctx sctx;
    int orig_fill;
    int encode;
    ec_ctx *ec;

    encode = ctx->encode;
    ec = ctx->ec;

    /* Special case for one sample */
    if (N == 1) {
        return oaci_quant_band_n1(ctx, X, Y, lowband_out);
    }

    orig_fill = fill;

    if (encode) {
        if (ctx->bandE[ctx->i] < MIN_STEREO_ENERGY || ctx->bandE[ctx->m->nbEBands + ctx->i] < MIN_STEREO_ENERGY) {
            if (ctx->bandE[ctx->i] > ctx->bandE[ctx->m->nbEBands + ctx->i]) OAC_COPY(Y, X, N);
            else OAC_COPY(X, Y, N);
        }
    }
    oaci_compute_theta(ctx, &sctx, X, Y, N, &b, B, B, LM, 1, &fill);
    inv = sctx.inv;
    imid = sctx.imid;
    iside = sctx.iside;
    delta = sctx.delta;
    itheta = sctx.itheta;
    (void)imid;
    (void)iside;
#ifdef FIXED_POINT
    mid = oaci_celt_cos_norm32(sctx.itheta_q30);
    side = oaci_celt_cos_norm32((1<<30) - sctx.itheta_q30);
#else
    mid = oaci_celt_cos_norm2(sctx.itheta_q30*(1.f/(1<<30)));
    side = oaci_celt_cos_norm2(1.f - sctx.itheta_q30*(1.f/(1<<30)));
#endif

    /* This is a special case for N=2 that only works for stereo and takes
       advantage of the fact that mid and side are orthogonal to encode
       the side with just one bit. */
    if (N == 2) {
        int c;
        int sign = 0;
        celt_norm *x2, *y2;
        mbits = b;
        sbits = 0;
        /* Only need one bit for the side. */
        if (itheta != 0 && itheta != 16384)
            sbits = 1<<BITRES;
        mbits -= sbits;
        c = itheta > 8192;

        x2 = c ? Y : X;
        y2 = c ? X : Y;
        if (sbits) {
            if (encode) {
                /* Here we only need to encode a sign for the side. */
                /* FIXME: Need to increase fixed-point precision? */
                sign = MULT32_32_Q31(x2[0], y2[1]) - MULT32_32_Q31(x2[1], y2[0]) < 0;
                oaci_ec_enc_bits(ec, sign, 1);
            } else {
                sign = oaci_ec_dec_bits(ec, 1);
            }
        }
        sign = 1 - 2*sign;
        /* We use orig_fill here because we want to fold the side, but if
           itheta==16384, we'll have cleared the low bits of fill. */
        cm = oaci_quant_band(ctx, x2, N, mbits, B, lowband, LM, lowband_out, Q31ONE,
            lowband_scratch, orig_fill);
        /* We don't split N=2 bands, so cm is either 1 or 0 (for a fold-collapse),
           and there's no need to worry about mixing with the other channel. */
        y2[0] = -sign*x2[1];
        y2[1] = sign*x2[0];
        if (ctx->resynth) {
            celt_norm tmp;
            X[0] = MULT32_32_Q31(mid, X[0]);
            X[1] = MULT32_32_Q31(mid, X[1]);
            Y[0] = MULT32_32_Q31(side, Y[0]);
            Y[1] = MULT32_32_Q31(side, Y[1]);
            tmp = X[0];
            X[0] = SUB32(tmp, Y[0]);
            Y[0] = ADD32(tmp, Y[0]);
            tmp = X[1];
            X[1] = SUB32(tmp, Y[1]);
            Y[1] = ADD32(tmp, Y[1]);
        }
    } else {
        /* "Normal" split code */
        oac_int32 tell;
        oac_int32 rebalance;

        mbits = IMAX(0, IMIN(b, (b - delta)/2));
        sbits = b - mbits;
        tell = oaci_ec_tell_frac(ec);

        if (mbits >= sbits) {
            /* In stereo mode, we do not apply a scaling to the mid because we need the normalized
               mid for folding later. */
            cm = oaci_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, Q31ONE,
               lowband_scratch, fill);
            rebalance = mbits - (oaci_ec_tell_frac(ec) - tell);
            if (rebalance > 3<<BITRES && itheta != 0)
                sbits += rebalance - (3<<BITRES);

            /* For a stereo split, the high bits of fill are always zero, so no
               folding will be done to the side. */
            cm |= oaci_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
        } else {
            /* For a stereo split, the high bits of fill are always zero, so no
               folding will be done to the side. */
            cm = oaci_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill>>B);
            rebalance = sbits - (oaci_ec_tell_frac(ec) - tell);
            if (rebalance > 3<<BITRES && itheta != 16384)
                mbits += rebalance - (3<<BITRES);
            /* In stereo mode, we do not apply a scaling to the mid because we need the normalized
               mid for folding later. */
            cm |= oaci_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, Q31ONE,
               lowband_scratch, fill);
        }
    }


    /* This code is used by the decoder and by the resynthesis-enabled encoder */
    if (ctx->resynth) {
        if (N != 2)
            oaci_stereo_merge(X, Y, mid, N, ctx->arch);
        if (inv) {
            int j;
            for (j = 0; j < N; j++)
                Y[j] = -Y[j];
        }
    }
    return cm;
}

static void oaci_special_hybrid_folding(const CELTMode *m, celt_norm *norm, celt_norm *norm2, int start, int M,
                                   int dual_stereo) {
    int n1, n2;
    const oac_int16 * OAC_RESTRICT eBands = m->eBands;
    n1 = M*(eBands[start + 1] - eBands[start]);
    n2 = M*(eBands[start + 2] - eBands[start + 1]);
    /* Duplicate enough of the first band folding data to be able to fold the second band.
       Copies no data for CELT-only mode. */
    OAC_COPY(&norm[n1], &norm[2*n1 - n2], n2 - n1);
    if (dual_stereo)
        OAC_COPY(&norm2[n1], &norm2[2*n1 - n2], n2 - n1);
}

static void quant_all_bands_twoch(int encode, const CELTMode *m, int start, int end,
                     celt_norm *X_, celt_norm *Y_, unsigned char *collapse_masks,
                     const celt_ener *bandE, int *pulses, int shortBlocks, int spread,
                     int dual_stereo, int intensity, int *tf_res, oac_int32 total_bits,
                     oac_int32 balance, ec_ctx *ec, int LM, int codedBands,
                     oac_uint32 *seed, int complexity, int arch, int disable_inv) {
    int i;
    oac_int32 remaining_bits;
    const oac_int16 * OAC_RESTRICT eBands = m->eBands;
    celt_norm * OAC_RESTRICT norm, * OAC_RESTRICT norm2;
    VARDECL(celt_norm, _norm);
    VARDECL(celt_norm, _lowband_scratch);
    VARDECL(celt_norm, X_save);
    VARDECL(celt_norm, Y_save);
    VARDECL(celt_norm, X_save2);
    VARDECL(celt_norm, Y_save2);
    VARDECL(celt_norm, norm_save2);
    VARDECL(unsigned char, bytes_save);
    int resynth_alloc;
    celt_norm *lowband_scratch;
    int B;
    int M;
    int lowband_offset;
    int update_lowband = 1;
    int C = Y_ != NULL ? 2 : 1;
    int norm_offset;
    int theta_rdo = encode && Y_ != NULL && !dual_stereo && complexity >= 8;
#ifdef RESYNTH
    int resynth = 1;
#else
    int resynth = !encode || theta_rdo;
#endif
    struct band_ctx ctx;
    SAVE_STACK;

    M = 1<<LM;
    B = shortBlocks ? M : 1;
    norm_offset = M*eBands[start];
    /* No need to allocate norm for the last band because we don't need an
       output in that band. */
    ALLOC(_norm, C*(M*eBands[m->nbEBands - 1] - norm_offset), celt_norm);
    norm = _norm;
    norm2 = norm + M*eBands[m->nbEBands - 1] - norm_offset;

    /* For decoding, we can use the last band as scratch space because we don't need that
       scratch space for the last band and we don't care about the data there until we're
       decoding the last band. */
    if (encode && resynth)
        resynth_alloc = M*(eBands[m->nbEBands] - eBands[m->nbEBands - 1]);
    else
        resynth_alloc = ALLOC_NONE;
    ALLOC(_lowband_scratch, resynth_alloc, celt_norm);
    if (encode && resynth)
        lowband_scratch = _lowband_scratch;
    else
        lowband_scratch = X_ + M*eBands[m->effEBands - 1];
    ALLOC(X_save, resynth_alloc, celt_norm);
    ALLOC(Y_save, resynth_alloc, celt_norm);
    ALLOC(X_save2, resynth_alloc, celt_norm);
    ALLOC(Y_save2, resynth_alloc, celt_norm);
    ALLOC(norm_save2, resynth_alloc, celt_norm);

    lowband_offset = 0;
    ctx.bandE = bandE;
    ctx.ec = ec;
    ctx.encode = encode;
    ctx.intensity = intensity;
    ctx.m = m;
    ctx.seed = *seed;
    ctx.spread = spread;
    ctx.arch = arch;
    ctx.disable_inv = disable_inv;
    ctx.resynth = resynth;
    ctx.theta_round = 0;
    ALLOC(bytes_save, theta_rdo ? 1275 : ALLOC_NONE, unsigned char);

    /* Avoid injecting noise in the first band on transients. */
    ctx.avoid_split_noise = B > 1;
    for (i = start; i < end; i++) {
        oac_int32 tell;
        int b;
        int N;
        oac_int32 curr_balance;
        int effective_lowband = -1;
        celt_norm * OAC_RESTRICT X, * OAC_RESTRICT Y;
        int tf_change = 0;
        unsigned x_cm;
        unsigned y_cm;
        int last;

        ctx.i = i;
        last = (i == end - 1);

        X = X_ + M*eBands[i];
        if (Y_ != NULL)
            Y = Y_ + M*eBands[i];
        else
            Y = NULL;
        N = M*eBands[i + 1] - M*eBands[i];
        celt_assert(N > 0);
        tell = oaci_ec_tell_frac(ec);

        /* Compute how many bits we want to allocate to this band */
        if (i != start)
            balance -= tell;
        remaining_bits = total_bits - tell;
        ctx.total_bits = total_bits;
        if (i <= codedBands - 1) {
            curr_balance = oaci_celt_sudiv(balance, IMIN(3, codedBands - i));
            b = IMAX(0, IMIN(16383, IMIN(remaining_bits, pulses[i] + curr_balance)));
        } else {
            b = 0;
        }

        if (resynth && (M*eBands[i] - N >= M*eBands[start] || i == start + 1)
            && (update_lowband || lowband_offset == 0))
            lowband_offset = i;
        if (i == start + 1)
            oaci_special_hybrid_folding(m, norm, norm2, start, M, dual_stereo);

        tf_change = tf_res[i];
        ctx.tf_change = tf_change;
        if (i >= m->effEBands) {
            X = norm;
            if (Y_ != NULL)
                Y = norm;
            lowband_scratch = NULL;
        }
        if (last && !theta_rdo)
            lowband_scratch = NULL;

        /* Get a conservative estimate of the collapse_mask's for the bands we're
           going to be folding from. */
        if (lowband_offset != 0 && (spread != SPREAD_AGGRESSIVE || B > 1 || tf_change < 0)) {
            int fold_start;
            int fold_end;
            int fold_i;
            /* This ensures we never repeat spectral content within one band */
            effective_lowband = IMAX(0, M*eBands[lowband_offset] - norm_offset - N);
            fold_start = lowband_offset;
            while (M*eBands[--fold_start] > effective_lowband + norm_offset) ;
            fold_end = lowband_offset - 1;
            while (++fold_end < i && M*eBands[fold_end] < effective_lowband + norm_offset + N) ;
            x_cm = y_cm = 0;
            fold_i = fold_start; do {
                x_cm |= collapse_masks[fold_i*C + 0];
                y_cm |= collapse_masks[fold_i*C + C - 1];
            } while (++fold_i < fold_end);
        }
        /* Otherwise, we'll be using the LCG to fold, so all blocks will (almost
           always) be non-zero. */
        else
            x_cm = y_cm = (1<<B) - 1;

        if (dual_stereo && i == intensity) {
            int j;

            /* Switch off dual stereo to do intensity. */
            dual_stereo = 0;
            if (resynth)
                for (j = 0; j < M*eBands[i] - norm_offset; j++)
                    norm[j] = HALF32(norm[j] + norm2[j]);
        }
        if (dual_stereo) {
            x_cm = oaci_quant_band(&ctx, X, N, b/2, B,
               effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
               last?NULL:norm + M*eBands[i] - norm_offset, Q31ONE, lowband_scratch, x_cm);
            y_cm = oaci_quant_band(&ctx, Y, N, b/2, B,
               effective_lowband != -1 ? norm2 + effective_lowband : NULL, LM,
               last?NULL:norm2 + M*eBands[i] - norm_offset, Q31ONE, lowband_scratch, y_cm);
        } else {
            if (Y != NULL) {
                if (theta_rdo && i < intensity) {
                    ec_ctx ec_save, ec_save2;
                    struct band_ctx ctx_save, ctx_save2;
                    oac_val32 dist0, dist1;
                    unsigned cm, cm2;
                    int nstart_bytes, nend_bytes, save_bytes;
                    unsigned char *bytes_buf;
                    oac_val16 w[2];
                    oaci_compute_channel_weights(bandE[i], bandE[i + m->nbEBands], w);
                    /* Make a copy. */
                    cm = x_cm|y_cm;
                    ec_save = *ec;
                    ctx_save = ctx;
                    OAC_COPY(X_save, X, N);
                    OAC_COPY(Y_save, Y, N);
                    /* Encode and round down. */
                    ctx.theta_round = -1;
                    x_cm = oaci_quant_band_stereo(&ctx, X, Y, N, b, B,
                     effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
                     last?NULL:norm + M*eBands[i] - norm_offset, lowband_scratch, cm);
                    dist0 = MULT16_32_Q15(w[0], oaci_celt_inner_prod_norm_shift(X_save, X, N, arch)) + MULT16_32_Q15(w[1],
                    oaci_celt_inner_prod_norm_shift(Y_save, Y, N, arch));

                    /* Save first result. */
                    cm2 = x_cm;
                    ec_save2 = *ec;
                    ctx_save2 = ctx;
                    OAC_COPY(X_save2, X, N);
                    OAC_COPY(Y_save2, Y, N);
                    if (!last)
                        OAC_COPY(norm_save2, norm + M*eBands[i] - norm_offset, N);
                    nstart_bytes = ec_save.offs;
                    nend_bytes = ec_save.storage;
                    bytes_buf = ec_save.buf + nstart_bytes;
                    save_bytes = nend_bytes - nstart_bytes;
                    OAC_COPY(bytes_save, bytes_buf, save_bytes);

                    /* Restore */
                    *ec = ec_save;
                    ctx = ctx_save;
                    OAC_COPY(X, X_save, N);
                    OAC_COPY(Y, Y_save, N);
                    if (i == start + 1)
                        oaci_special_hybrid_folding(m, norm, norm2, start, M, dual_stereo);
                    /* Encode and round up. */
                    ctx.theta_round = 1;
                    x_cm = oaci_quant_band_stereo(&ctx, X, Y, N, b, B,
                     effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
                     last?NULL:norm + M*eBands[i] - norm_offset, lowband_scratch, cm);
                    dist1 = MULT16_32_Q15(w[0], oaci_celt_inner_prod_norm_shift(X_save, X, N, arch)) + MULT16_32_Q15(w[1],
                    oaci_celt_inner_prod_norm_shift(Y_save, Y, N, arch));
                    if (dist0 >= dist1) {
                        x_cm = cm2;
                        *ec = ec_save2;
                        ctx = ctx_save2;
                        OAC_COPY(X, X_save2, N);
                        OAC_COPY(Y, Y_save2, N);
                        if (!last)
                            OAC_COPY(norm + M*eBands[i] - norm_offset, norm_save2, N);
                        OAC_COPY(bytes_buf, bytes_save, save_bytes);
                    }
                } else {
                    ctx.theta_round = 0;
                    x_cm = oaci_quant_band_stereo(&ctx, X, Y, N, b, B,
                     effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
                     last?NULL:norm + M*eBands[i] - norm_offset, lowband_scratch, x_cm|y_cm);
                }
            } else {
                x_cm = oaci_quant_band(&ctx, X, N, b, B,
                  effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
                  last?NULL:norm + M*eBands[i] - norm_offset, Q31ONE, lowband_scratch, x_cm|y_cm);
            }
            y_cm = x_cm;
        }
        collapse_masks[i*C + 0] = (unsigned char)x_cm;
        collapse_masks[i*C + C - 1] = (unsigned char)y_cm;
        balance += pulses[i] + tell;

        /* Update the folding position only as long as we have 1 bit/sample depth. */
        update_lowband = b > (N<<BITRES);
        /* We only need to avoid noise on a split for the first band. After that, we
           have folding. */
        ctx.avoid_split_noise = 0;
    }
    *seed = ctx.seed;

    RESTORE_STACK;
}
/* Multi-channel multi-mono: each channel encoded independently per band.
   collapse_masks are stored interleaved as [band*C + channel] so that
   oaci_anti_collapse can read them correctly. */
static void quant_all_bands_multi(int encode, const CELTMode *m, int start, int end,
                     celt_norm *X_, int C, unsigned char *collapse_masks,
                     const celt_ener *bandE, int *pulses, int shortBlocks, int spread,
                     int *tf_res, oac_int32 total_bits,
                     oac_int32 balance, ec_ctx *ec, int LM, int codedBands,
                     oac_uint32 *seed, int arch, int disable_inv) {
    int i, c;
    oac_int32 remaining_bits;
    const oac_int16 * OAC_RESTRICT eBands = m->eBands;
    int B;
    int M;
    int frame_size;
    int norm_offset;
    int norm_size;
    int lowband_offset;
    int update_lowband = 1;
    int resynth_alloc;
    struct band_ctx ctx;
    VARDECL(celt_norm, _norm);
    VARDECL(celt_norm, _lowband_scratch);
#ifdef RESYNTH
    int resynth = 1;
#else
    int resynth = !encode;
#endif
    SAVE_STACK;
    M = 1<<LM;
    B = shortBlocks ? M : 1;
    frame_size = M*m->shortMdctSize;
    norm_offset = M*eBands[start];
    norm_size = M*eBands[m->nbEBands - 1] - norm_offset;
    /* One norm array per channel for spectral folding */
    ALLOC(_norm, C*norm_size, celt_norm);
    if (encode && resynth)
        resynth_alloc = M*(eBands[m->nbEBands] - eBands[m->nbEBands - 1]);
    else
        resynth_alloc = ALLOC_NONE;
    ALLOC(_lowband_scratch, resynth_alloc, celt_norm);
    ctx.ec = ec;
    ctx.encode = encode;
    ctx.intensity = 0;
    ctx.m = m;
    ctx.seed = *seed;
    ctx.spread = spread;
    ctx.arch = arch;
    ctx.disable_inv = disable_inv;
    ctx.resynth = resynth;
    ctx.theta_round = 0;
    ctx.avoid_split_noise = B > 1;
    lowband_offset = 0;
    for (i = start; i < end; i++) {
        oac_int32 tell;
        int b;
        int band_N;
        oac_int32 curr_balance;
        int effective_lowband = -1;
        int tf_change;
        int last = (i == end - 1);
        ctx.i = i;
        band_N = M*eBands[i + 1] - M*eBands[i];
        celt_assert(band_N > 0);
        tell = oaci_ec_tell_frac(ec);
        if (i != start)
            balance -= tell;
        remaining_bits = total_bits - tell;
        if (i <= codedBands - 1) {
            curr_balance = oaci_celt_sudiv(balance, IMIN(3, codedBands - i));
            b = IMAX(0, IMIN(16383, IMIN(remaining_bits, pulses[i] + curr_balance)));
        } else {
            b = 0;
        }
        /* Update lowband offset for spectral folding */
        if (resynth && (M*eBands[i] - band_N >= M*eBands[start] || i == start + 1)
            && (update_lowband || lowband_offset == 0))
            lowband_offset = i;
        /* Handle hybrid mode band boundary */
        if (i == start + 1) {
            int n1 = M*(eBands[start + 1] - eBands[start]);
            int n2 = M*(eBands[start + 2] - eBands[start + 1]);
            for (c = 0; c < C; c++) {
                celt_norm *norm_c = _norm + c*norm_size;
                OAC_COPY(&norm_c[n1], &norm_c[2*n1 - n2], n2 - n1);
            }
        }
        tf_change = tf_res[i];
        ctx.tf_change = tf_change;
        /* Encode each channel independently for this band */
        {
            oac_int32 remaining_per_chan = remaining_bits / C;
            for (c = 0; c < C; c++) {
                celt_norm *X_c;
                celt_norm *norm_c = _norm + c*norm_size;
                celt_norm *lowband_ptr = NULL;
                celt_norm *lowband_out;
                celt_norm *lb_scratch;
                unsigned x_cm;
                int chan_b = b / C;
                /* Each channel's PVQ budget cap: current tell + per-channel share */
                ctx.total_bits = oaci_ec_tell_frac(ec) + remaining_per_chan;
                ctx.bandE = bandE + c*m->nbEBands;
            X_c = X_ + c*frame_size + M*eBands[i];
            lowband_out = last ? NULL : norm_c + M*eBands[i] - norm_offset;
            /* Setup lowband for folding */
            if (lowband_offset != 0 && (spread != SPREAD_AGGRESSIVE || B > 1 || tf_change < 0)) {
                int fold_start, fold_end, fold_i;
                effective_lowband = IMAX(0, M*eBands[lowband_offset] - norm_offset - band_N);
                fold_start = lowband_offset;
                while (M*eBands[--fold_start] > effective_lowband + norm_offset) ;
                fold_end = lowband_offset - 1;
                while (++fold_end < i && M*eBands[fold_end] < effective_lowband + norm_offset + band_N) ;
                x_cm = 0;
                fold_i = fold_start; do {
                    x_cm |= collapse_masks[fold_i*C + c];
                } while (++fold_i < fold_end);
                lowband_ptr = norm_c + effective_lowband;
            } else {
                x_cm = (1<<B) - 1;
            }
            if (i >= m->effEBands) {
                X_c = norm_c;
                lb_scratch = NULL;
            } else if (encode && resynth) {
                lb_scratch = _lowband_scratch;
            } else {
                lb_scratch = X_ + c*frame_size + M*eBands[m->effEBands - 1];
            }
            if (last)
                lb_scratch = NULL;
            x_cm = oaci_quant_band(&ctx, X_c, band_N, chan_b, B,
                lowband_ptr, LM, lowband_out, Q31ONE, lb_scratch, x_cm);
            collapse_masks[i*C + c] = (unsigned char)x_cm;
            }
        }
        balance += pulses[i] + tell;
        update_lowband = b > (band_N<<BITRES);
        ctx.avoid_split_noise = 0;
    }
    *seed = ctx.seed;
    RESTORE_STACK;
}
void oaci_quant_all_bands(int encode, const CELTMode *m, int start, int end,
                     celt_norm *X_, int C, unsigned char *collapse_masks,
                     const celt_ener *bandE, int *pulses, int shortBlocks, int spread,
                     int dual_stereo, int intensity, int *tf_res, oac_int32 total_bits,
                     oac_int32 balance, ec_ctx *ec, int LM, int codedBands,
                     oac_uint32 *seed, int complexity, int arch, int disable_inv) {
    if (C <= 2) {
        int M = 1<<LM;
        int frame_size = M*m->shortMdctSize;
        celt_norm *Y_ = (C == 2) ? X_ + frame_size : NULL;
        quant_all_bands_twoch(encode, m, start, end, X_, Y_, collapse_masks,
            bandE, pulses, shortBlocks, spread, dual_stereo, intensity,
            tf_res, total_bits, balance, ec, LM, codedBands,
            seed, complexity, arch, disable_inv);
    } else {
        quant_all_bands_multi(encode, m, start, end, X_, C, collapse_masks,
            bandE, pulses, shortBlocks, spread,
            tf_res, total_bits, balance, ec, LM, codedBands,
            seed, arch, disable_inv);
    }
}

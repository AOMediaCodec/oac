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

#ifndef BANDS_H
#define BANDS_H

#include "arch.h"
#include "modes.h"
#include "entenc.h"
#include "entdec.h"
#include "rate.h"

oac_int16 oaci_bitexact_cos(oac_int16 x);
int oaci_bitexact_log2tan(int isin, int icos);

/** Compute the amplitude (sqrt energy) in each of the bands
 * @param m Mode data
 * @param X Spectrum
 * @param bandE Square root of the energy for each band (returned)
 */
void oaci_compute_band_energies(const CELTMode *m, const celt_sig *X, celt_ener *bandE, int end, int C, int LM, int arch);

/*void compute_noise_energies(const CELTMode *m, const celt_sig *X, const oac_val16 *tonality, celt_ener *bandE);*/

/** Normalise each band of X such that the energy in each band is
    equal to 1
 * @param m Mode data
 * @param X Spectrum (returned normalised)
 * @param bandE Square root of the energy for each band
 */
void oaci_normalise_bands(const CELTMode *m, const celt_sig * OAC_RESTRICT freq, celt_norm * OAC_RESTRICT X,
    const celt_ener *bandE, int end, int C, int M);

/** Denormalise each band of X to restore full amplitude
 * @param m Mode data
 * @param X Spectrum (returned de-normalised)
 * @param bandE Square root of the energy for each band
 */
void oaci_denormalise_bands(const CELTMode *m, const celt_norm * OAC_RESTRICT X,
    celt_sig * OAC_RESTRICT freq, const celt_glog *bandE, int start,
    int end, int M, int downsample, int silence);

#define SPREAD_NONE       (0)
#define SPREAD_LIGHT      (1)
#define SPREAD_NORMAL     (2)
#define SPREAD_AGGRESSIVE (3)

int oaci_spreading_decision(const CELTMode *m, const celt_norm *X, int *average,
    int last_decision, int *hf_average, int *tapset_decision, int update_hf,
    int end, int C, int M, const int *spread_weight);

#ifdef MEASURE_NORM_MSE
void measure_norm_mse(const CELTMode *m, float *X, float *X0, float *bandE, float *bandE0, int M, int N, int C);
#endif

void oaci_haar1(celt_norm *X, int N0, int stride);

/** Quantisation/encoding of the residual spectrum
 * @param encode flag that indicates whether we're encoding (1) or decoding (0)
 * @param m Mode data
 * @param start First band to process
 * @param end Last band to process + 1
 * @param X Residual (normalised), all C channels contiguous (channel stride = M*shortMdctSize)
 * @param C Number of channels
 * @param collapse_masks Anti-collapse tracking mask (C*nbEBands, interleaved as [band*C + channel])
 * @param bandE Square root of the energy for each band (C*nbEBands, stride nbEBands per channel)
 * @param pulses Bit allocation (per band) for PVQ
 * @param shortBlocks Zero for long blocks, non-zero for short blocks
 * @param spread Amount of spreading to use
 * @param dual_stereo Zero for MS stereo, non-zero for dual stereo (C<=2 only)
 * @param intensity First band to use intensity stereo (C<=2 only)
 * @param tf_res Time-frequency resolution change
 * @param total_bits Total number of bits that can be used for the frame (including the ones already spent)
 * @param balance Number of unallocated bits
 * @param en Entropy coder state
 * @param LM log2() of the number of 2.5 subframes in the frame
 * @param codedBands Last band to receive bits + 1
 * @param seed Random generator seed
 * @param arch Run-time architecture (see oac_select_arch())
 */
void oaci_quant_all_bands(int encode, const CELTMode *m, int start, int end,
    celt_norm * X, int C, unsigned char *collapse_masks,
    const celt_ener *bandE, int *pulses, int shortBlocks, int spread,
    int dual_stereo, int intensity, int *tf_res, oac_int32 total_bits,
    oac_int32 balance, ec_ctx *ec, int M, int codedBands, oac_uint32 *seed,
    int complexity, int arch, int disable_inv);

void oaci_anti_collapse(const CELTMode *m, celt_norm *X_,
    unsigned char *collapse_masks, int LM, int C, int size, int start,
    int end, const celt_glog *logE, const celt_glog *prev1logE,
    const celt_glog *prev2logE, const int *pulses, oac_uint32 seed,
    int encode, int arch);

oac_uint32 oaci_celt_lcg_rand(oac_uint32 seed);

int oaci_hysteresis_decision(oac_val16 val, const oac_val16 *thresholds, const oac_val16 *hysteresis, int N, int prev);

#endif /* BANDS_H */

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

#ifndef SEMAC_H
#define SEMAC_H

#include "arch.h"
#include "modes.h"
#include "entenc.h"
#include "entdec.h"

/** SEMAC transport order is now configured at runtime via encoder controls.
    The compile-time define is kept as documentation but no longer used. */

/* Spatial whitening (level restored at the decoder via the envelope):
   - defined: whiten input beams to unit per-band energy BEFORE forming the transport.
   - not defined (default): form the transport, then normalize its per-band overall level. */
/* #define SEMAC_SPATIAL_NORMALIZATION */

/* Energy-weighted per-transport-channel bit allocation (experimental).
 * When SEMAC_ENERGY_ALLOC is defined, SEMAC biases the per-channel PVQ bit split in
 * quant_all_bands_multi toward higher-energy transport channels: each channel gets
 * +1 bit/dimension per doubling of amplitude (6.02 dB) above the mean channel
 * (per-band energy-weighted law). Weights are derived from the bit-exact coded transport
 * band energies (oldBandE), so the decoder reproduces them with no extra side-info. On by default. */
#define SEMAC_ENERGY_ALLOC

/* By default the allocation weights are derived from the coded transport band energies
 * (oldBandE) directly -- the channels' actual coded amplitude. Define SEMAC_ALLOC_FROM_ENV to
 * instead derive them by projecting the spatial energy envelope (a diagonal approximation).
 * Both are bit-exact on encoder and decoder. */
/* #define SEMAC_ALLOC_FROM_ENV */

/* Partial-weighting scalar lambda on the per-channel allocation offset. The offset is
 * lambda * (log2(a_c) - per-band mean), where a_c is the channel amplitude (= sqrt energy):
 *   lambda = 0.0 -> equal split
 *   lambda = 0.5 -> half-amplitude
 *   lambda = 1.0 -> amplitude law (1 bit/dim per 6.02 dB amplitude doubling)   [default]
 * Higher lambda steers more bits to the louder channels. */
#ifndef SEMAC_ALLOC_LAMBDA
#define SEMAC_ALLOC_LAMBDA 1.0f
#endif

/** SEMAC transport (coding) domains. Mirrors the public OAC_SEMAC_DOMAIN_*
    values; extend with new domains as they are added. */
typedef enum {
   SEMAC_DOMAIN_SHD  = 0,   /**< Spherical harmonic (ambisonics) domain. */
   SEMAC_DOMAIN_BEAM = 1    /**< Beam-projection domain (full input order only). */
} SemacTransportDomain;

/* Number of fine quantization bits for spatial beam energies in SEMAC mode.
   With 3 bits, resolution improves from 6 dB (coarse only) to 0.75 dB
   (6/2³ = 0.75 dB), meeting the sub-1 dB target for ambisonics spatial coding. */
#define SPATIAL_FINE_BITS 3

/** Maximum number of beams supported (5th order = 36 beams). */
#define SEMAC_MAX_BEAMS 36

/** Maximum number of CELT energy bands (standard 48 kHz mode). Per-band SEMAC weight
    buffers are sized to this; the oaci_semac_compute_bit_weights_* functions assert nbEBands <= this. */
#define SEMAC_MAX_BANDS 21

/** Return the ambisonics order for a given channel count.
    Channel count must be a perfect square: 1, 4, 9, 16, 25, 36. */
static OAC_INLINE int oaci_semac_get_order(int channels)
{
   int order = 0;
   while ((order + 1) * (order + 1) < channels)
      order++;
   return order;
}

/** Return the number of channels for a given ambisonics order: (order+1)^2. */
static OAC_INLINE int oaci_semac_get_channels(int order)
{
   return (order + 1) * (order + 1);
}

/** Get the SPFB from_shd projection matrix (ambisonics -> beams) for the given order.
    Returns NULL if the order is unsupported. */
const float *oaci_semac_get_from_shd_matrix(int order);

/** Get the SPFB to_shd projection matrix (beams -> ambisonics) for the given order.
    Returns NULL if the order is unsupported. */
const float *oaci_semac_get_to_shd_matrix(int order);

/** Apply a square projection matrix (from_shd or to_shd): transform N channels
    to N channels. The same matmul serves both directions; pass the from_shd
    matrix for SHD->beams or the to_shd matrix for beams->SHD.
    in/out layout: x[channel * num_bins + bin]; matrix row-major [channels x channels]. */
void oaci_semac_project(const float *proj,
   const celt_sig *freq_in, celt_sig *freq_out,
   int channels, int num_bins);

/** Encoder-side step 1: compute spatial beam band energies from the full
    ambisonics MDCT spectrum. Transforms ambisonics -> beams (SPFB from_shd
    projection) and runs compute_band_energies + amp2Log2 on the beams. Does not
    modify freq.
    @param mode         CELT mode
    @param shdFreq         Input MDCT spectrum, layout freq[c*N + k] (read-only)
    @param beamFreq     Output beam-domain MDCT, layout beamFreq[b*N + k]
                        [num_beams * N]. Reused by oaci_semac_create_transport_channels.
    @param spatialBandE Output linear band energies [num_beams * nbEBands]
    @param spatialLogE  Output log-domain band energies [num_beams * nbEBands]
    @param input_order  Ambisonics order of the input signal
    @param effEnd       Number of effective bands
    @param end          Number of bands
    @param C            Number of input channels
    @param LM           Frame size selector
    @param arch         Architecture flags */
void oaci_semac_compute_spatial_energy(const CELTMode *mode,
   const celt_sig *shdFreq, celt_sig *beamFreq,
   celt_ener *spatialBandE, celt_glog *spatialLogE,
   int input_order, int effEnd, int end, int C, int LM, int arch);

/** Encoder-side step 2: create the transport channels (written into outFreq). When
    SEMAC_SPATIAL_NORMALIZATION is defined the input beams (beamFreq) are first normalized to
    unit per-band energy (restored at the decoder from the spatial envelope); otherwise the
    transport is formed from the input ambisonics (shdFreq) and only its per-band level is
    normalized. SEMAC_DOMAIN_SHD yields ambisonics transport channels; SEMAC_DOMAIN_BEAM
    downmixes to coding_channels transport beams (identity at full order).
    @param mode            CELT mode
    @param shdFreq         Input ambisonics MDCT, layout [c*N + k] (read-only)
    @param beamFreq        Beam-domain MDCT from oaci_semac_compute_spatial_energy (read-only)
    @param outFreq         Output transport channels, layout [c*N + k] (may alias shdFreq)
    @param spatialBandE    Per-beam linear band energies [num_beams * nbEBands]
    @param input_order     Ambisonics order of the input signal
    @param effEnd          Number of effective bands
    @param coding_channels Number of transport channels
    @param transport_domain SEMAC_DOMAIN_* selecting the coding domain
    @param LM              Frame size selector */
void oaci_semac_create_transport_channels(const CELTMode *mode,
   const celt_sig *shdFreq, const celt_sig *beamFreq, celt_sig *outFreq,
   const celt_ener *spatialBandE,
   int input_order, int effEnd, int coding_channels,
   int transport_domain, int LM);

/** Decoder-side: reconstruct full ambisonics from the coded transport channels + spatial
    energies. Operates in the normalized spectrum + band energy domain, before synthesis.
    @param mode             CELT mode
    @param X                Normalized spectrum, coding_channels * N (input). On output:
                            full-order output_channels * N normalized spectrum.
    @param oldBandE         Decoded transport-channel band log-energies (input, read-only): used
                            to denormalize the transport. NOT modified, so it stays the transport
                            energy for inter-frame prediction and the bit allocation.
    @param outLogE          Output: full-order output-channel band log-energies for synthesis
                            [output_channels * nbEBands].
    @param spatialLogE      Decoded spatial beam band energies [num_beams * nbEBands]
    @param coding_channels  Number of waveform-coded (transport) channels
    @param output_channels  Number of output channels (full ambisonics)
    @param input_order      Ambisonics order of the original signal
    @param transport_domain SEMAC_DOMAIN_* selecting the coding domain
    @param start            First band to process
    @param effEnd           Number of effective bands
    @param end              Number of bands
    @param LM               Frame size selector
    @param arch             Architecture flags */
void oaci_semac_reconstruct(const CELTMode *mode,
   celt_norm *X, celt_glog *oldBandE,
   celt_glog *outLogE,
   const celt_glog *spatialLogE,
   int coding_channels, int output_channels,
   int input_order, int transport_domain,
   int start, int effEnd, int end,
   int LM, int arch);

/** Compute Q6 PER-BAND per-transport-channel bit-allocation offsets (signed,
    per-dimension BITRES) from the BIT-EXACT coded transport band energies (oldBandE).
    Layout offsets_q6[c*nbEBands + i]. Length must be C*nbEBands. Deterministic and
    called identically on encoder and decoder so the per-channel PVQ split stays
    bit-exact. eMeans is per-band (constant across channels), so it cancels in the
    per-band mean and oldBandE serves directly as log2-amplitude. Logarithmic curve
    (+1 bit/dim per amplitude doubling above the per-band mean). This is the default
    weight source (see SEMAC_ALLOC_FROM_ENV for the envelope-projection alternative).
    @param oldBandE   coded transport band energies, layout [c*nbEBands + i]
    @param C          number of transport (coding) channels
    @param nbEBands   bands per channel
    @param start,end  coded band range
    @param offsets_q6 output array of length C*nbEBands (signed per-dim BITRES, Q6) */
void oaci_semac_compute_bit_weights_from_tc(const celt_glog *oldBandE, int C, int nbEBands,
   int start, int end, oac_int32 *offsets_q6);

/** Like oaci_semac_compute_bit_weights_from_tc but derives the offsets from the
    spatial energy envelope (a diagonal projection of the per-beam energies) instead of
    the coded transport energies. Beam transport domain uses the envelope directly per
    band; SHD projects each band's beam energies through the to_shd matrix. Selected via
    SEMAC_ALLOC_FROM_ENV.
    @param oldSpatialBandE per-beam envelope, layout [beam*nbEBands + band]
    @param num_beams       number of beams in the envelope (full input order)
    @param C               number of transport (coding) channels
    @param input_order     ambisonics input order (selects the to_shd matrix)
    @param transport_domain SEMAC_DOMAIN_SHD or SEMAC_DOMAIN_BEAM
    @param nbEBands        bands per channel
    @param start,end       coded band range
    @param offsets_q6      output array of length C*nbEBands (signed per-dim BITRES, Q6) */
void oaci_semac_compute_bit_weights_from_env(const celt_glog *oldSpatialBandE, int num_beams,
   int C, int input_order, int transport_domain, int nbEBands,
   int start, int end, oac_int32 *offsets_q6);

#endif /* SEMAC_H */

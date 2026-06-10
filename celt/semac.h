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

/* When defined, enables spatial normalization before ambisonics encoding */
#define SEMAC_SPATIAL_NORMALIZATION

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
    @param freq         Input MDCT spectrum, layout freq[c*N + k] (read-only)
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
   const celt_sig *freq, celt_sig *beamFreq,
   celt_ener *spatialBandE, celt_glog *spatialLogE,
   int input_order, int effEnd, int end, int C, int LM, int arch);

/** Encoder-side step 2: create the transport channels (written into freq) from
    the beams. When SEMAC_SPATIAL_NORMALIZATION is defined the beams are first
    normalized to unit per-band energy (restored at the decoder from the spatial
    envelope); this is independent of the transport domain. SEMAC_DOMAIN_SHD
    demixes the beams back to ambisonics; SEMAC_DOMAIN_BEAM downmixes them to
    coding_channels transport beams (identity at full order).
    @param mode            CELT mode
    @param beamFreq        Beam-domain MDCT from oaci_semac_compute_spatial_energy
    @param freq            Output transport channels, layout freq[c*N + k]
    @param spatialBandE    Per-beam linear band energies [num_beams * nbEBands]
    @param input_order     Ambisonics order of the input signal
    @param effEnd          Number of effective bands
    @param C               Number of channels (SHD demix path)
    @param coding_channels Number of transport channels (beam domain)
    @param transport_domain SEMAC_DOMAIN_* selecting the coding domain
    @param LM              Frame size selector */
void oaci_semac_create_transport_channels(const CELTMode *mode,
   const celt_sig *beamFreq, celt_sig *freq,
   const celt_ener *spatialBandE,
   int input_order, int effEnd, int C, int coding_channels,
   int transport_domain, int LM);

/** Decoder-side: reconstruct full ambisonics from coded channels + spatial energies.
    Operates in the normalized spectrum + band energy domain, before synthesis.
    @param mode             CELT mode
    @param X                Normalized spectrum, coding_channels * N. On output: output_channels * N.
    @param oldBandE         Band energies for coded channels. On output: band energies for output channels.
    @param spatialLogE      Decoded spatial beam band energies [num_beams * nbEBands]
    @param coding_channels  Number of waveform-coded channels
    @param output_channels  Number of output channels (full ambisonics)
    @param input_order      Ambisonics order of the original signal
    @param start            First band to process
    @param effEnd           Number of effective bands
    @param end              Number of bands
    @param LM               Frame size selector
    @param arch             Architecture flags */
void oaci_semac_reconstruct(const CELTMode *mode,
   celt_norm *X, celt_glog *oldBandE,
   const celt_glog *spatialLogE,
   int coding_channels, int output_channels,
   int input_order, int transport_domain,
   int start, int effEnd, int end,
   int LM, int arch);

#endif /* SEMAC_H */

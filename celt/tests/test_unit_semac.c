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

#ifdef ENABLE_SEMAC
#include <stdio.h>
#include <math.h>
#if defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API)
#define CELT_ENCODER_C
#define CELT_DECODER_C
#endif
#include "celt.h"
#include "semac.h"

static int ret = 0;

static void test_allocation(void) {
    /* Excluded edges, equal energy, amplitude scaling, signed half ties,
       and a fractional mean. Inputs are on the shared Q15 grid. */
    static const oac_int32 logs[24] = {
         983040, 0,  32768,  32, 42, -983040,
         983040, 0,      0, -32,  0, -983040,
         983040, 0, -32768,   0,  0, -983040,
         983040, 0,      0,   0,  0, -983040
    };
    static const double centered[24] = {
        0, 0,  32768,  32,  31.5, 0,
        0, 0,      0, -32, -10.5, 0,
        0, 0, -32768,   0, -10.5, 0,
        0, 0,      0,   0, -10.5, 0
    };
    celt_glog energies[24];
    oac_int32 offsets[24];
    int i;
    for (i = 0; i < 24; i++) {
#ifdef FIXED_POINT
        energies[i] = logs[i] * (1 << (DB_SHIFT - 15));
#else
        energies[i] = logs[i] * (1.f/32768);
#endif
        offsets[i] = 42;
    }
    oaci_semac_clamp_transport_energy(energies, 4, 6, 0, 6);
    if (energies[0] != GCONST(28.f) || energies[5] != -GCONST(28.f)) {
        fprintf(stderr, "SEMAC coarse energy saturation mismatch\n");
        ret = 1;
    }
    oaci_semac_compute_bit_weights_from_tc(energies, 4, 6, 1, 5, offsets);
    for (i = 0; i < 24; i++) {
        /* Known mean-centered values give an oracle without a second allocator. */
        double scaled = centered[i] * SEMAC_ALLOC_LAMBDA_Q15
            * ((1 << BITRES)*64) / 1073741824.;
        oac_int32 expected = (oac_int32)floor(fabs(scaled) + .5);
        if (scaled < 0) expected = -expected;
        if (offsets[i] != expected) {
            fprintf(stderr, "SEMAC allocation mismatch at index %d: %d != %d\n",
                i, offsets[i], expected);
            ret = 1;
        }
    }
}

#if defined(SEMAC_ENERGY_ALLOC) \
    && (defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API))
static void test_packet_loss(int domain) {
    int err, frame, i, len;
    const int channels = 9, frame_size = 960;
    const int order = domain == SEMAC_DOMAIN_SHD ? 1 : 2;
    OacCustomMode *mode = oac_custom_mode_create(48000, 960, &err);
    OacCustomEncoder *enc;
    OacCustomDecoder *dec;
    oac_int16 pcm[960*9], output[960*9];
    unsigned char packet[512];
    if (!mode) { ret = 1; return; }
    enc = oac_custom_encoder_create(mode, channels, OAC_FORMAT_AMBISONICS, &err);
    dec = oac_custom_decoder_create(mode, channels, OAC_FORMAT_AMBISONICS, &err);
    if (!enc || !dec) {
        ret = 1;
        goto cleanup;
    }
    if (oac_custom_encoder_ctl(enc, OAC_SET_ENABLE_SEMAC(1)) ||
        oac_custom_encoder_ctl(enc, OAC_SET_SEMAC_TRANSPORT_DOMAIN(domain)) ||
        oac_custom_encoder_ctl(enc, OAC_SET_SEMAC_TRANSPORT_ORDER(order))) {
        ret = 1;
        goto cleanup;
    }
    for (frame = 0; frame < 6; frame++) {
        oac_uint32 encoder_range, decoder_range;
        for (i = 0; i < frame_size*channels; i++)
            pcm[i] = (oac_int16)((i*151 + frame*71)%8000 - 4000);
        len = oac_custom_encode(enc, pcm, frame_size, packet, sizeof(packet));
        if (len < 0) { ret = 1; break; }
        /* Consecutive losses alter decoder history; the next packet must resync. */
        if (frame == 2 || frame == 3) {
            if (oac_custom_decode(dec, NULL, 0, output, frame_size) != frame_size) ret = 1;
        } else {
            if (oac_custom_decode(dec, packet, len, output, frame_size) != frame_size ||
                oac_custom_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&encoder_range)) ||
                oac_custom_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&decoder_range))) {
                ret = 1;
                break;
            }
            if (encoder_range != decoder_range) {
                fprintf(stderr, "SEMAC packet range mismatch domain=%d frame=%d\n",
                    domain, frame);
                ret = 1;
                break;
            }
        }
    }
cleanup:
    oac_custom_encoder_destroy(enc);
    oac_custom_decoder_destroy(dec);
    oac_custom_mode_destroy(mode);
}
#endif
#endif

int main(void) {
#ifdef ENABLE_SEMAC
    test_allocation();
#if defined(SEMAC_ENERGY_ALLOC) \
    && (defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API))
    test_packet_loss(SEMAC_DOMAIN_SHD);
    test_packet_loss(SEMAC_DOMAIN_BEAM);
#endif
    return ret;
#else
    return 0;
#endif
}

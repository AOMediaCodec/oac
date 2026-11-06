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

/* Tests for the lossless (OLAC) coding paths of the Oac encoder and decoder:
   strict lossless, lossless with lossy fallback (including constant bit-rate),
   lossless frames in the lossy applications when they are cheaper, switching
   applications, and the decoder's behaviour around OLAC frames (rate and
   channel conversion, mono/stereo splices, corrupt packets, multi-frame
   packets, packet loss). */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
# include <unistd.h>
#else
# include <process.h>
# define getpid _getpid
#endif
#include "oac.h"
#include "oac_multistream.h"
#include "test_oac_common.h"

#define FS 48000
#define LOOKAHEAD (FS/400)
#define MAX_FRAME 960
#define MAX_PACKET 65536

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

/* --enable-fuzzing makes the encoder take random coding decisions: the lossy
   applications may pick SILK at any time and CELT may declare a frame silent
   (which turns its prefilter off), so the checks that depend on those
   decisions are skipped in such builds. */
static int fuzzing;

static oac_int32 rand_int24(void) {
    return (oac_int32)(fast_rand()&0xFFFFFF) - 0x800000;
}

/* A signal with enough variety to exercise the coder: silence, extremes, DC,
   tones, a pattern and noise, in 1/8 segments, at the given depth. */
static void fill_signal(oac_int32 *buf, int nb_samples, int channels, int depth) {
    int i, c;
    int seg = nb_samples/8;
    oac_int32 max = (1<<(depth - 1)) - 1;
    for (i = 0; i < nb_samples; i++) {
        for (c = 0; c < channels; c++) {
            oac_int32 v;
            int s = seg ? i/seg : 0;
            switch (s) {
                case 0: v = 0; break;
                case 1: v = (i&1) ? max : -max - 1; break;
                case 2: v = max/3 - c*max/5; break;
                case 3: v = (oac_int32)floor(.5 + .6*max*sin(2*M_PI*(440 + 110*c)*i/(double)FS)); break;
                case 4: v = (oac_int32)floor(.5 + .01*max*sin(2*M_PI*(3000 + 500*c)*i/(double)FS)); break;
                case 5: v = (((i*7 + c*13)&127) - 64)*(max>>7); break;
                default: v = (oac_int32)(fast_rand()%(2*(oac_uint32)max + 2)) - max - 1; break;
            }
            buf[i*channels + c] = v*(1<<(24 - depth));
        }
    }
}

/* A frame is reconstructed exactly when the packet holding its first 17.5 ms,
   the one holding its last 2.5 ms and the one before (whose last sample is
   the de-emphasis memory the first one starts from) are all lossless. */
static int exact_expected(const unsigned char *lossless, int f) {
    return (f == 0 || lossless[f - 1]) && lossless[f] && lossless[f + 1];
}

/* Encodes nb_frames of in[] (plus one to flush the look-ahead) with enc,
   decodes with dec, checks the range coder states match and records in
   lossless[] which packets were coded losslessly. max_bytes is the output
   buffer size handed to the encoder. The frames that should come out exactly
   (see above) are checked to. Returns the number of lossless packets. */
static int roundtrip24(OacEncoder *enc, OacDecoder *dec, const oac_int32 *in, oac_int32 *out,
                       int frame_size, int nb_frames, int channels, int max_bytes,
                       unsigned char *lossless) {
    unsigned char *packet = malloc(MAX_PACKET);
    int f, nb_lossless = 0;
    oac_test_assert(packet != NULL);
    for (f = 0; f < nb_frames + 1; f++) {
        oac_uint32 enc_rng, dec_rng;
        const oac_int32 *frame = f < nb_frames ? in + f*frame_size*channels : in;
        int len = oac_encode24(enc, frame, frame_size, packet, max_bytes);
        oac_test_assert(len > 0 && len <= max_bytes);
        lossless[f] = oac_packet_is_lossless(packet, len);
        oac_test_assert(lossless[f] == 0 || lossless[f] == 1);
        nb_lossless += lossless[f];
        oac_test_assert(oac_decode24(dec, packet, len, out + f*frame_size*channels, frame_size, 0) == frame_size);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&enc_rng)) == OAC_OK);
        oac_test_assert(oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&dec_rng)) == OAC_OK);
        oac_test_assert(enc_rng == dec_rng);
    }
    for (f = 0; f < nb_frames; f++) {
        if (exact_expected(lossless, f)) {
            oac_test_assert(memcmp(in + f*frame_size*channels, out + (LOOKAHEAD + f*frame_size)*channels,
                                   sizeof(*in)*frame_size*channels) == 0);
        }
    }
    free(packet);
    return nb_lossless;
}

/* OAC_APPLICATION_LOSSLESS without fallback: bit-exact for every API and
   every frame size up to 20 ms, in VBR and CBR, regardless of the settings
   that only make sense for lossy coding; errors on a buffer too small. */
static void test_strict_lossless(void) {
    static const int frame_sizes[4] = {120, 240, 480, 960};
    int err, channels, fs, i, f;
    const int nb_frames = 24;
    oac_int32 *in = malloc(sizeof(*in)*2*MAX_FRAME*(nb_frames + 1));
    oac_int32 *out = malloc(sizeof(*out)*2*MAX_FRAME*(nb_frames + 1));
    unsigned char *packet = malloc(MAX_PACKET);
    unsigned char *packet2 = malloc(MAX_PACKET);
    unsigned char lossless[25];
    oac_test_assert(in && out && packet && packet2);

    fprintf(stdout, "  Strict lossless\n");
    /* 48 kHz mono or stereo only, no multi-frame packets. */
    for (i = 0; i < 5; i++) {
        static const oac_int32 rates[5] = {8000, 12000, 16000, 24000, 96000};
        oac_test_assert(oac_encoder_create(rates[i], 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err) == NULL);
        oac_test_assert(err == OAC_BAD_ARG);
    }
    oac_test_assert(oac_encoder_create(FS, 3, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err) == NULL);
    oac_test_assert(err == OAC_BAD_ARG);
    oac_test_assert(oac_encoder_create(FS, 1, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_LOSSLESS, &err) == NULL);
    oac_test_assert(err == OAC_BAD_ARG);
    {
        OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        oac_int32 lookahead;
        oac_test_assert(enc != NULL && err == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&lookahead)) == OAC_OK && lookahead == LOOKAHEAD);
        memset(in, 0, sizeof(*in)*2*2880);
        oac_test_assert(oac_encode24(enc, in, 1920, packet, MAX_PACKET) == OAC_BAD_ARG);
        oac_test_assert(oac_encode24(enc, in, 2880, packet, MAX_PACKET) == OAC_BAD_ARG);
        oac_encoder_destroy(enc);
    }

    for (channels = 1; channels <= 2; channels++) {
        for (fs = 0; fs < 4; fs++) {
            OacEncoder *enc = oac_encoder_create(FS, channels, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
            OacDecoder *dec = oac_decoder_create(FS, channels, OAC_FORMAT_STANDARD, &err);
            int frame_size = frame_sizes[fs];
            int len0, len1;
            oac_test_assert(enc != NULL && dec != NULL);
            /* None of these may change the output. */
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(6000)) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(OAC_BANDWIDTH_NARROWBAND)) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_FORCE_CHANNELS(1)) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_DTX(1)) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(fast_rand()%11)) == OAC_OK);
            fill_signal(in, nb_frames*frame_size, channels, 24);
            oac_test_assert(roundtrip24(enc, dec, in, out, frame_size, nb_frames, channels, MAX_PACKET, lossless)
                            == nb_frames + 1);
            /* CBR pads every packet to the same (worst-case) size. */
            oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_VBR(0)) == OAC_OK);
            len0 = oac_encode24(enc, in, frame_size, packet, MAX_PACKET);
            len1 = oac_encode24(enc, in + 7*frame_size*channels, frame_size, packet, MAX_PACKET);
            oac_test_assert(len0 > 0 && len0 == len1 && oac_packet_is_lossless(packet, len1) == 1);
            oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(roundtrip24(enc, dec, in, out, frame_size, nb_frames, channels, MAX_PACKET, lossless)
                            == nb_frames + 1);
            oac_encoder_destroy(enc);
            oac_decoder_destroy(dec);
        }
    }

    /* 16-bit and float APIs. */
    {
        OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        OacDecoder *dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
        oac_int16 *in16 = malloc(sizeof(*in16)*2*MAX_FRAME*(nb_frames + 1));
        oac_int16 *out16 = malloc(sizeof(*out16)*2*MAX_FRAME*(nb_frames + 1));
        oac_test_assert(enc && dec && in16 && out16);
        fill_signal(in, nb_frames*960, 2, 16);
        for (i = 0; i < nb_frames*960*2; i++) in16[i] = (oac_int16)(in[i]/256);
        for (f = 0; f < nb_frames + 1; f++) {
            const oac_int16 *frame = f < nb_frames ? in16 + f*960*2 : in16;
            int len = oac_encode(enc, frame, 960, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
            oac_test_assert(oac_decode(dec, packet, len, out16 + f*960*2, 960, 0) == 960);
        }
        oac_test_assert(memcmp(in16, out16 + LOOKAHEAD*2, sizeof(*in16)*nb_frames*960*2) == 0);
#ifndef DISABLE_FLOAT_API
        {
            float *inf = malloc(sizeof(*inf)*2*MAX_FRAME*(nb_frames + 1));
            float *outf = malloc(sizeof(*outf)*2*MAX_FRAME*(nb_frames + 1));
            oac_test_assert(inf && outf);
            oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
            /* 16-bit data through the float API, with OAC_SET_LSB_DEPTH(16)
               telling the encoder there is nothing below. */
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LSB_DEPTH(16)) == OAC_OK);
            for (i = 0; i < nb_frames*960*2; i++) inf[i] = in16[i]*(1.f/32768.f);
            for (f = 0; f < nb_frames + 1; f++) {
                const float *frame = f < nb_frames ? inf + f*960*2 : inf;
                int len = oac_encode_float(enc, frame, 960, packet, MAX_PACKET);
                oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
                oac_test_assert(oac_decode_float(dec, packet, len, outf + f*960*2, 960, 0) == 960);
            }
            for (i = 0; i < nb_frames*960*2; i++)
                oac_test_assert(outf[LOOKAHEAD*2 + i] == inf[i]);
            free(inf);
            free(outf);
        }
#endif
        free(in16);
        free(out16);
        oac_encoder_destroy(enc);
        oac_decoder_destroy(dec);
    }

    /* A buffer too small for the frame fails without changing the state, so
       the frame can be retried with a larger one and give the same packet as
       an encoder that never failed. A buffer of a few dozen bytes is the case
       that matters: its equivalent rate is below anything the stereo-width
       reduction of the lossy modes would tolerate. */
    {
        OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        OacEncoder *ref = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        oac_test_assert(enc && ref);
        for (i = 0; i < 8*960*2; i++) in[i] = rand_int24();
        for (f = 0; f < 8; f++) {
            int len = oac_encode24(ref, in + f*960*2, 960, packet, MAX_PACKET);
            int len2, small;
            oac_test_assert(len > 50);
            oac_test_assert(oac_encode24(enc, in + f*960*2, 960, packet2, 50) == OAC_BUFFER_TOO_SMALL);
            for (small = 1; small < len; small += 1 + len/5)
                oac_test_assert(oac_encode24(enc, in + f*960*2, 960, packet2, small) == OAC_BUFFER_TOO_SMALL);
            len2 = oac_encode24(enc, in + f*960*2, 960, packet2, len);
            oac_test_assert(len2 == len && memcmp(packet, packet2, len) == 0);
        }
        oac_encoder_destroy(enc);
        oac_encoder_destroy(ref);
    }

    /* A stereo signal simple enough to fit a tiny buffer must come out with
       its full width, and a loud frame at a coarse depth followed by a quiet
       one at full depth (the predictor's memory is rescaled by 2^12 at the
       boundary) must stay exact. */
    {
        OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        OacDecoder *dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
        oac_test_assert(enc && dec);
        for (i = 0; i < 4*960; i++) {
            oac_int32 v = (i%960 == 60) ? (1<<22) : 0;
            in[2*i] = v;
            in[2*i + 1] = -v;
        }
        oac_test_assert(roundtrip24(enc, dec, in, out, 960, 4, 2, 65, lossless) == 5);
        oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
        for (i = 0; i < 960*2; i++) in[i] = ((i&1) ? 200 : -200)*(1<<12);
        for (i = 960*2; i < 4*960*2; i++) in[i] = (oac_int32)(fast_rand()%201) - 100;
        oac_test_assert(roundtrip24(enc, dec, in, out, 960, 4, 2, MAX_PACKET, lossless) == 5);
        oac_encoder_destroy(enc);
        oac_decoder_destroy(dec);
    }
    free(in);
    free(out);
    free(packet);
    free(packet2);
}

/* OAC_APPLICATION_LOSSLESS with OAC_SET_LOSSY_FALLBACK(1): lossless when it
   fits the buffer, CELT otherwise, with seamless switches. */
static void test_lossy_fallback(void) {
    int err, i, f, channels;
    const int nb_frames = 40;
    const int frame_size = 960;
    oac_int32 *in = malloc(sizeof(*in)*2*frame_size*(nb_frames + 1));
    oac_int32 *out = malloc(sizeof(*out)*2*frame_size*(nb_frames + 1));
    unsigned char *packet = malloc(MAX_PACKET);
    unsigned char noisy[40], lossless[41];
    oac_test_assert(in && out && packet);

    fprintf(stdout, "  Lossy fallback\n");
    for (channels = 1; channels <= 2; channels++) {
        OacEncoder *enc = oac_encoder_create(FS, channels, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        OacDecoder *dec = oac_decoder_create(FS, channels, OAC_FORMAT_STANDARD, &err);
        oac_int32 value;
        int len;
        oac_test_assert(enc && dec);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOSSY_FALLBACK(&value)) == OAC_OK && value == 0);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(2)) == OAC_BAD_ARG);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(1)) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOSSY_FALLBACK(&value)) == OAC_OK && value == 1);

        /* Alternate 4 frames of quiet tone (lossless in a few hundred bytes)
           with 4 frames of full-scale noise (not even close). A tone frame
           must be coded losslessly unless it follows noise (its overlap with
           the noise is part of it) or starts the stream (no history to
           predict from); a noise frame never can be. */
        for (f = 0; f < nb_frames; f++) {
            noisy[f] = (f/4)&1;
            for (i = 0; i < frame_size*channels; i++) {
                int n = f*frame_size + i/channels;
                in[f*frame_size*channels + i] = noisy[f] ? rand_int24()
                    : (oac_int32)floor(.5 + 2000*256*sin(2*M_PI*(220 + 110*(i%channels))*n/(double)FS));
            }
        }
        /* With the buffer as the limit; the bit-rate is not one (it is what
           the CELT frames get). */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000*channels)) == OAC_OK);
        roundtrip24(enc, dec, in, out, frame_size, nb_frames, channels, 1000*channels, lossless);
        for (f = 1; f < nb_frames; f++) {
            if (!noisy[f] && !noisy[f - 1]) oac_test_assert(lossless[f] == 1);
            if (noisy[f]) oac_test_assert(lossless[f] == 0);
        }

        /* Constant bit-rate: every packet has exactly the target size. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(400000*channels)) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_VBR(0)) == OAC_OK);
        for (f = 0; f < nb_frames; f++) {
            len = oac_encode24(enc, in + f*frame_size*channels, frame_size, packet, MAX_PACKET);
            oac_test_assert(len == 1000*channels);
            lossless[f] = oac_packet_is_lossless(packet, len);
            oac_test_assert(oac_decode24(dec, packet, len, out + f*frame_size*channels, frame_size, 0) == frame_size);
        }
        for (f = 1; f < nb_frames; f++) {
            if (!noisy[f] && !noisy[f - 1]) oac_test_assert(lossless[f] == 1);
            if (noisy[f]) oac_test_assert(lossless[f] == 0);
            if (f < nb_frames - 1 && exact_expected(lossless, f)) {
                oac_test_assert(memcmp(in + f*frame_size*channels, out + (LOOKAHEAD + f*frame_size)*channels,
                                       sizeof(*in)*frame_size*channels) == 0);
            }
        }

        /* Back to strict: the noise frames no longer fit. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_VBR(1)) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(0)) == OAC_OK);
        len = oac_encode24(enc, in, frame_size, packet, 2000*channels);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        len = oac_encode24(enc, in + frame_size*channels, frame_size, packet, 1000*channels);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        len = oac_encode24(enc, in + 4*frame_size*channels, frame_size, packet, 1000*channels);
        oac_test_assert(len == OAC_BUFFER_TOO_SMALL);

        /* Switching from CELT to OLAC on loud high-frequency material: the
           overlap handed over is pre-emphasised and folded, so it exceeds
           24 bits and must not be clipped on the way. The first OLAC packet
           then reproduces the end of the CELT frame about as well as CELT did
           (within 1/16 of full scale here), instead of a click twice full
           scale. */
        if (!fuzzing) {
            oac_int32 err_max = 0;
            oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(1)) == OAC_OK);
            oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(256000*channels)) == OAC_OK);
            for (i = 0; i < frame_size*channels; i++) {
                int n = i/channels;
                in[i] = (oac_int32)floor(.5 + 8000000*cos(2*M_PI*16000*n/(double)FS)) + ((int)(fast_rand()&63) - 32);
                in[frame_size*channels + i] = 0;
            }
            len = oac_encode24(enc, in, frame_size, packet, 600*channels);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode24(dec, packet, len, out, frame_size, 0) == frame_size);
            len = oac_encode24(enc, in + frame_size*channels, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
            oac_test_assert(oac_decode24(dec, packet, len, out + frame_size*channels, frame_size, 0) == frame_size);
            for (i = 0; i < LOOKAHEAD*channels; i++) {
                oac_int32 e = abs(out[frame_size*channels + i] - in[(frame_size - LOOKAHEAD)*channels + i]);
                if (e > err_max) err_max = e;
            }
            oac_test_assert(err_max < (1<<19));
        }
        oac_encoder_destroy(enc);
        oac_decoder_destroy(dec);
    }
    free(in);
    free(out);
    free(packet);
}

/* The lossy applications code a frame losslessly when that is cheaper than
   the lossy target, i.e. the smaller of the output buffer and the bit-rate,
   except right after a CELT frame that used the comb prefilter. */
static void test_opportunistic(void) {
    static const int apps[3] = {OAC_APPLICATION_AUDIO, OAC_APPLICATION_RESTRICTED_LOWDELAY,
                                OAC_APPLICATION_RESTRICTED_CELT};
    int err, i, f, a;
    const int frame_size = 960;
    oac_int16 *noise = malloc(sizeof(*noise)*frame_size);
    oac_int16 *pitchy = malloc(sizeof(*pitchy)*frame_size);
    oac_int16 *silence = calloc(frame_size, sizeof(*silence));
    oac_int16 *out = malloc(sizeof(*out)*frame_size);
    unsigned char *packet = malloc(MAX_PACKET);
    oac_test_assert(noise && pitchy && silence && out && packet);

    fprintf(stdout, "  Opportunistic lossless\n");
    for (i = 0; i < frame_size; i++) {
        /* A loud, very periodic signal that CELT's comb prefilter will pick
           up. The other signal is quiet noise (about 800 bytes with OLAC),
           drawn afresh for every frame so that there is no periodicity for
           the prefilter to find. */
        pitchy[i] = (oac_int16)floor(.5 + 8000*sin(2*M_PI*200*i/(double)FS)
                                    + 4000*sin(2*M_PI*400*i/(double)FS) + 2000*sin(2*M_PI*600*i/(double)FS));
    }
#define NEW_NOISE() do { for (i = 0; i < frame_size; i++) noise[i] = (oac_int16)(fast_rand()%33) - 16; } while (0)
    for (a = fuzzing ? 1 : 0; a < 3; a++) {
        OacEncoder *enc = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, apps[a], &err);
        OacDecoder *dec = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
        int len, len_olac;
        oac_test_assert(enc && dec);
        /* Digital silence is left to CELT, which codes it in fewer bytes. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000)) == OAC_OK);
        for (f = 0; f < 3; f++) {
            len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && len < 16 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        /* The noise is not at 64 kb/s (160 bytes), however large the buffer... */
        for (f = 0; f < 3; f++) {
            NEW_NOISE();
            len = oac_encode(enc, noise, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        /* ...but is at 512 kb/s (1280 bytes)... */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(512000)) == OAC_OK);
        for (f = 0; f < 3; f++) {
            NEW_NOISE();
            len = oac_encode(enc, noise, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 400 && len <= 1280 && oac_packet_is_lossless(packet, len) == 1);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        len_olac = len;
        /* ...unless the buffer is smaller than that. */
        NEW_NOISE();
        len = oac_encode(enc, noise, frame_size, packet, 3*len_olac/4);
        oac_test_assert(len > 0 && len <= 3*len_olac/4 && oac_packet_is_lossless(packet, len) == 0);
        oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);

        /* After CELT codes the pitchy signal (with its prefilter), the first
           quiet frame has to be CELT too; the next one can be lossless. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000)) == OAC_OK);
        for (f = 0; f < 4; f++) {
            len = oac_encode(enc, pitchy, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(OAC_BITRATE_MAX)) == OAC_OK);
        NEW_NOISE();
        len = oac_encode(enc, noise, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && (fuzzing || oac_packet_is_lossless(packet, len) == 0));
        oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        NEW_NOISE();
        len = oac_encode(enc, noise, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        /* Digital silence goes back to CELT, and once the high-pass filter's
           response to what came before has died out, the output is silent as
           well. */
        for (f = 0; f < 50; f++) {
            len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        for (i = 0; i < frame_size; i++)
            oac_test_assert(out[i] == 0);
        oac_encoder_destroy(enc);
        oac_decoder_destroy(dec);
    }

    /* The input is rounded to the depth of the API before OLAC sees it, so
       the high-pass filter's output does not cost 24 bits per sample. */
    {
        OacEncoder *enc = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_RESTRICTED_LOWDELAY, &err);
        int len;
        oac_test_assert(enc != NULL);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(OAC_BITRATE_MAX)) == OAC_OK);
        for (f = 0; f < 4; f++) {
            for (i = 0; i < frame_size; i++)
                noise[i] = (oac_int16)(fast_rand()%201) - 100;
            len = oac_encode(enc, noise, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
            /* 8 bits of noise take about 10 bits per sample, not 24. */
            oac_test_assert(len < frame_size*14/8);
        }
        oac_encoder_destroy(enc);
    }

    /* Without the high-pass filter, a lossy application that codes every
       frame losslessly is bit-exact, after its own look-ahead. */
    if (!fuzzing) {
        OacEncoder *enc = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
        OacDecoder *dec = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
        oac_int16 *in = malloc(sizeof(*in)*5*frame_size);
        oac_int16 *all_out = malloc(sizeof(*all_out)*5*frame_size);
        oac_int32 lookahead, value;
        oac_test_assert(enc && dec && in && all_out);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_DC_REJECT(&value)) == OAC_OK && value == 1);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_DC_REJECT(0)) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(OAC_BITRATE_MAX)) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&lookahead)) == OAC_OK);
        /* A DC step, which the filter would not have left alone. */
        for (i = 0; i < 5*frame_size; i++)
            in[i] = i < 1000 ? 10000 : -7777;
        for (f = 0; f < 5; f++) {
            int len = oac_encode(enc, in + f*frame_size, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
            oac_test_assert(oac_decode(dec, packet, len, all_out + f*frame_size, frame_size, 0) == frame_size);
        }
        oac_test_assert(memcmp(in, all_out + lookahead, sizeof(*in)*4*frame_size) == 0);
        free(in);
        free(all_out);
        oac_encoder_destroy(enc);
        oac_decoder_destroy(dec);
    }
    free(noise);
    free(pitchy);
    free(silence);
    free(out);
    free(packet);
}

/* Lossless with fallback: a silent frame that CELT has to code right after a
   loud lossless frame still carries the last 2.5 ms of that frame, so CELT
   must not take it for digital silence. */
static void test_fallback_into_silence(void) {
    OacEncoder *enc;
    OacDecoder *dec;
    unsigned char *packet = malloc(MAX_PACKET);
    oac_int32 *in = calloc(4*MAX_FRAME, sizeof(*in));
    oac_int32 *out = calloc(4*MAX_FRAME, sizeof(*out));
    double sig = 0, noise = 0;
    int err, f, i;
    oac_test_assert(packet != NULL && in != NULL && out != NULL);

    fprintf(stdout, "  Lossy fallback into silence\n");
    enc = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    oac_test_assert(err == OAC_OK && enc != NULL);
    dec = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
    oac_test_assert(err == OAC_OK && dec != NULL);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(1)) == OAC_OK);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(96000)) == OAC_OK);
    /* Frame 1 is loud, the others are digital silence. */
    for (i = MAX_FRAME; i < 2*MAX_FRAME; i++) {
        in[i] = (oac_int32)floor(.5 + 256*(9000*sin(2*M_PI*330.*i/FS) + 9000*sin(2*M_PI*1234.*i/FS)))
              + (rand_int24()>>4);
    }
    for (f = 0; f < 4; f++) {
        /* The buffer of frame 2 is too small for OLAC to code the end of
           frame 1, so CELT does. */
        int len = oac_encode24(enc, in + f*MAX_FRAME, MAX_FRAME, packet, f == 2 ? 240 : MAX_PACKET);
        oac_test_assert(len > 0);
        oac_test_assert(oac_packet_is_lossless(packet, len) == (f != 2));
        oac_test_assert(oac_decode24(dec, packet, len, out + f*MAX_FRAME, MAX_FRAME, 0) == MAX_FRAME);
    }
    for (i = 2*MAX_FRAME - LOOKAHEAD; i < 2*MAX_FRAME; i++) {
        double d = (double)out[i + LOOKAHEAD] - in[i];
        sig += (double)in[i]*in[i];
        noise += d*d;
    }
    /* CELT's own error there is around -28 dB; a dropped hand-over is 0 dB. */
    if (!fuzzing)
        oac_test_assert(noise < .01*sig);
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);
    free(packet);
    free(in);
    free(out);
}

/* A lossless packet that is too short to hold its residual is rejected instead
   of being decoded as noise. One that only lacks raw LSBs (the last bits
   written, which end up right after the first few bytes of the payload) still
   decodes, to within those LSBs. */
static void test_truncated_lossless_packet(void) {
    OacEncoder *enc;
    OacDecoder *dec, *dec_cut;
    unsigned char *packet = malloc(MAX_PACKET);
    oac_int32 *in = malloc(sizeof(*in)*MAX_FRAME);
    oac_int32 *out = malloc(sizeof(*out)*MAX_FRAME);
    oac_int32 *out_cut = malloc(sizeof(*out_cut)*MAX_FRAME);
    int err, f, i;
    oac_test_assert(packet != NULL && in != NULL && out != NULL && out_cut != NULL);

    fprintf(stdout, "  Truncated lossless packets\n");
    enc = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    oac_test_assert(err == OAC_OK && enc != NULL);
    dec = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
    oac_test_assert(err == OAC_OK && dec != NULL);
    dec_cut = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
    oac_test_assert(err == OAC_OK && dec_cut != NULL);
    for (f = 0; f < 8; f++) {
        int len;
        for (i = 0; i < MAX_FRAME; i++)
            in[i] = rand_int24();
        len = oac_encode24(enc, in, MAX_FRAME, packet, MAX_PACKET);
        oac_test_assert(len > 2000 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode24(dec, packet, len, out, MAX_FRAME, 0) == MAX_FRAME);
        /* Full-scale noise has a dozen planes of raw LSBs, of 120 bytes
           each: without 100 of those bytes, only the last plane suffers. */
        memmove(packet + 9, packet + 109, len - 109);
        oac_test_assert(oac_decode24(dec_cut, packet, len - 100, out_cut, MAX_FRAME, 0) == MAX_FRAME);
        for (i = 0; i < MAX_FRAME; i++)
            oac_test_assert(abs(out_cut[i] - out[i]) < 256);
    }
    /* The first bytes of that last packet cannot hold a residual of 11 bits
       or more per sample. */
    oac_test_assert(oac_decode24(dec_cut, packet, 8, out_cut, MAX_FRAME, 0) == OAC_INVALID_PACKET);
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);
    oac_decoder_destroy(dec_cut);
    free(packet);
    free(in);
    free(out);
    free(out_cut);
}

/* Decoder: OLAC frames decoded at a lower rate or with a different channel
   count, and packet loss right after an OLAC frame, which must not burst (the
   CELT PLC would otherwise use whatever energies it last saw). */
static void test_decoder(void) {
    int err, i, f;
    const int frame_size = 960;
    OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    OacDecoder *dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
    OacDecoder *dec_mono = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
    OacDecoder *dec_24k = oac_decoder_create(24000, 2, OAC_FORMAT_STANDARD, &err);
    oac_int32 *in = malloc(sizeof(*in)*2*frame_size);
    oac_int32 *out = malloc(sizeof(*out)*2*frame_size);
    unsigned char *packet = malloc(MAX_PACKET);
    oac_test_assert(enc && dec && dec_mono && dec_24k && in && out && packet);

    fprintf(stdout, "  Decoder\n");
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_LOSSY_FALLBACK(1)) == OAC_OK);
    /* Loud CELT frames (noise that does not fit in 200 bytes)... */
    for (f = 0; f < 4; f++) {
        int len;
        for (i = 0; i < frame_size*2; i++) in[i] = rand_int24();
        len = oac_encode24(enc, in, frame_size, packet, 200);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
        oac_test_assert(oac_decode24(dec, packet, len, out, frame_size, 0) == frame_size);
        oac_test_assert(oac_decode24(dec_mono, packet, len, out, frame_size, 0) == frame_size);
        oac_test_assert(oac_decode24(dec_24k, packet, len, out, frame_size/2, 0) == frame_size/2);
    }
    /* ...then quiet OLAC frames... */
    for (f = 0; f < 4; f++) {
        int len;
        for (i = 0; i < frame_size*2; i++) in[i] = (oac_int32)(fast_rand()&255) - 128;
        len = oac_encode24(enc, in, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode24(dec, packet, len, out, frame_size, 0) == frame_size);
        if (f > 0) {
            for (i = 0; i < frame_size*2; i++)
                oac_test_assert(abs(out[i]) <= 128);
        }
        oac_test_assert(oac_decode24(dec_mono, packet, len, out, frame_size, 0) == frame_size);
        oac_test_assert(oac_decode24(dec_24k, packet, len, out, frame_size/2, 0) == frame_size/2);
    }
    /* ...then losses: nothing louder than the quiet signal may come out. */
    for (f = 0; f < 4; f++) {
        oac_test_assert(oac_decode24(dec, NULL, 0, out, frame_size, 0) == frame_size);
        for (i = 0; i < frame_size*2; i++)
            oac_test_assert(abs(out[i]) <= 256);
        oac_test_assert(oac_decode24(dec_mono, NULL, 0, out, frame_size, 0) == frame_size);
        oac_test_assert(oac_decode24(dec_24k, NULL, 0, out, frame_size/2, 0) == frame_size/2);
    }
    free(in);
    free(out);
    free(packet);
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);
    oac_decoder_destroy(dec_mono);
    oac_decoder_destroy(dec_24k);
}

/* What the decoder returns for sample n of a stream it decodes from the start
   is input sample n - LOOKAHEAD (or 0 before that). */
static oac_int32 expected_sample(const oac_int32 *in, int n, int channels, int c) {
    return n < LOOKAHEAD ? 0 : in[(n - LOOKAHEAD)*channels + c];
}

/* The average of two channels, the way the decoder computes it. */
static oac_int32 average(oac_int32 a, oac_int32 b) {
    return (oac_int32)floor((a + b + 1)/2.);
}

/* Decoders with another channel count than the stream (exact: the average
   or a copy), mono and stereo streams following each other, and a corrupt
   packet. The latter two break the chain of memories between packets: the
   next packet cannot come out right, and the one after it only in part, so
   the output is checked from the third one on. */
static void test_channels(void) {
    int err, i, f, c;
    const int frame_size = 960, nb_frames = 6;
    OacEncoder *enc1 = oac_encoder_create(FS, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    OacEncoder *enc2 = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    OacDecoder *dec1 = oac_decoder_create(FS, 1, OAC_FORMAT_STANDARD, &err);
    OacDecoder *dec2 = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
    oac_int32 *in1 = malloc(sizeof(*in1)*frame_size*nb_frames);
    oac_int32 *in2 = malloc(sizeof(*in2)*2*frame_size*nb_frames);
    oac_int32 *out1 = malloc(sizeof(*out1)*frame_size*nb_frames);
    oac_int32 *out2 = malloc(sizeof(*out2)*2*frame_size*nb_frames);
    unsigned char *packet = malloc(MAX_PACKET);
    int len;
    oac_test_assert(enc1 && enc2 && dec1 && dec2 && in1 && in2 && out1 && out2 && packet);

    fprintf(stdout, "  Channel counts\n");
    fill_signal(in2, nb_frames*frame_size, 2, 24);
    for (i = 0; i < nb_frames*frame_size; i++) in1[i] = in2[2*i + 1];
    /* The stereo stream in the mono decoder and vice versa. */
    for (f = 0; f < nb_frames; f++) {
        len = oac_encode24(enc2, in2 + f*frame_size*2, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode24(dec1, packet, len, out1 + f*frame_size, frame_size, 0) == frame_size);
        len = oac_encode24(enc1, in1 + f*frame_size, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode24(dec2, packet, len, out2 + f*frame_size*2, frame_size, 0) == frame_size);
    }
    for (i = 0; i < nb_frames*frame_size; i++) {
        oac_test_assert(out1[i] == average(expected_sample(in2, i, 2, 0), expected_sample(in2, i, 2, 1)));
        for (c = 0; c < 2; c++)
            oac_test_assert(out2[2*i + c] == expected_sample(in1, i, 1, 0));
    }

    /* Then each decoder gets the other stream, from a fresh encoder. */
    oac_test_assert(oac_encoder_ctl(enc1, OAC_RESET_STATE) == OAC_OK);
    oac_test_assert(oac_encoder_ctl(enc2, OAC_RESET_STATE) == OAC_OK);
    for (f = 0; f < nb_frames; f++) {
        len = oac_encode24(enc1, in1 + f*frame_size, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0);
        oac_test_assert(oac_decode24(dec1, packet, len, out1 + f*frame_size, frame_size, 0) == frame_size);
        len = oac_encode24(enc2, in2 + f*frame_size*2, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0);
        oac_test_assert(oac_decode24(dec2, packet, len, out2 + f*frame_size*2, frame_size, 0) == frame_size);
    }
    for (i = 2*frame_size; i < nb_frames*frame_size; i++) {
        oac_test_assert(out1[i] == expected_sample(in1, i, 1, 0));
        for (c = 0; c < 2; c++)
            oac_test_assert(out2[2*i + c] == expected_sample(in2, i, 2, c));
    }

    /* A corrupt packet is rejected and resets the decoder. */
    oac_test_assert(oac_encoder_ctl(enc2, OAC_RESET_STATE) == OAC_OK);
    oac_test_assert(oac_decoder_ctl(dec2, OAC_RESET_STATE) == OAC_OK);
    for (f = 0; f < nb_frames; f++) {
        len = oac_encode24(enc2, in2 + f*frame_size*2, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0);
        if (f == 1) {
            /* Keep the ToC, fill the payload with ones: that cannot be parsed. */
            memset(packet + 1, 0xFF, 63);
            oac_test_assert(oac_decode24(dec2, packet, 64, out2 + f*frame_size*2, frame_size, 0) == OAC_INVALID_PACKET);
            /* Then ignore it, like a lost packet. */
            oac_test_assert(oac_decode24(dec2, NULL, 0, out2 + f*frame_size*2, frame_size, 0) == frame_size);
        } else {
            oac_test_assert(oac_decode24(dec2, packet, len, out2 + f*frame_size*2, frame_size, 0) == frame_size);
        }
    }
    for (i = 4*frame_size; i < nb_frames*frame_size; i++) {
        for (c = 0; c < 2; c++)
            oac_test_assert(out2[2*i + c] == expected_sample(in2, i, 2, c));
    }

    free(in1);
    free(in2);
    free(out1);
    free(out2);
    free(packet);
    oac_encoder_destroy(enc1);
    oac_encoder_destroy(enc2);
    oac_decoder_destroy(dec1);
    oac_decoder_destroy(dec2);
}

/* Decoders at other rates than 48 kHz get the nearest 48 kHz sample until
   OLAC gets a resampler (so this will become approximate), and multi-frame
   packets, as the repacketizer makes them, decode like the frames they hold. */
static void test_rates_and_multiframe(void) {
    static const oac_int32 rates[5] = {24000, 16000, 12000, 8000, 96000};
    int err, i, f, c, r;
    const int frame_size = 960, nb_frames = 4;
    OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    OacDecoder *dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
    OacRepacketizer *rp = oac_repacketizer_create();
    oac_int32 *in = malloc(sizeof(*in)*2*frame_size*nb_frames);
    oac_int32 *out = malloc(sizeof(*out)*2*2*frame_size*nb_frames);
    unsigned char *packets = malloc(MAX_PACKET*nb_frames);
    unsigned char *packet = malloc(MAX_PACKET);
    int len[4];
    oac_test_assert(enc && dec && rp && in && out && packets && packet);

    fprintf(stdout, "  Other rates and multi-frame packets\n");
    fill_signal(in, nb_frames*frame_size, 2, 24);
    for (f = 0; f < nb_frames; f++) {
        len[f] = oac_encode24(enc, in + f*frame_size*2, frame_size, packets + f*MAX_PACKET, MAX_PACKET);
        oac_test_assert(len[f] > 0 && oac_packet_is_lossless(packets + f*MAX_PACKET, len[f]) == 1);
    }
    for (r = 0; r < 5; r++) {
        OacDecoder *dec_r = oac_decoder_create(rates[r], 2, OAC_FORMAT_STANDARD, &err);
        int frame_size_r = frame_size*rates[r]/FS;
        if (dec_r == NULL) continue; /* 96 kHz needs ENABLE_QEXT. */
        for (f = 0; f < nb_frames; f++) {
            oac_test_assert(oac_decode24(dec_r, packets + f*MAX_PACKET, len[f], out + f*frame_size_r*2, frame_size_r, 0)
                            == frame_size_r);
        }
        for (i = 0; i < nb_frames*frame_size_r; i++) {
            for (c = 0; c < 2; c++)
                oac_test_assert(out[2*i + c] == expected_sample(in, (int)((oac_int64)i*FS/rates[r]), 2, c));
        }
        oac_decoder_destroy(dec_r);
    }
    /* Three 20 ms packets into one 60 ms packet. */
    for (f = 0; f < 3; f++)
        oac_test_assert(oac_repacketizer_cat(rp, packets + f*MAX_PACKET, len[f]) == OAC_OK);
    len[0] = oac_repacketizer_out(rp, packet, MAX_PACKET);
    oac_test_assert(len[0] > 0 && oac_packet_is_lossless(packet, len[0]) == 1);
    oac_test_assert(oac_packet_get_nb_frames(packet, len[0]) == 3);
    oac_test_assert(oac_decode24(dec, packet, len[0], out, 3*frame_size, 0) == 3*frame_size);
    oac_test_assert(oac_decode24(dec, packets + 3*MAX_PACKET, len[3], out + 3*frame_size*2, frame_size, 0) == frame_size);
    for (i = 0; i < nb_frames*frame_size; i++) {
        for (c = 0; c < 2; c++)
            oac_test_assert(out[2*i + c] == expected_sample(in, i, 2, c));
    }

    free(in);
    free(out);
    free(packets);
    free(packet);
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);
    oac_repacketizer_destroy(rp);
}

/* OAC_SET_APPLICATION to and from OAC_APPLICATION_LOSSLESS, which like any
   application change is only allowed before the first frame, and only where
   lossless coding is possible. */
static void test_application(void) {
    int err, i, f;
    const int frame_size = 960, nb_frames = 4;
    OacEncoder *enc;
    OacDecoder *dec;
    OacMSEncoder *ms;
    oac_int32 *in = malloc(sizeof(*in)*2*frame_size*nb_frames);
    oac_int32 *out = malloc(sizeof(*out)*2*frame_size*nb_frames);
    unsigned char *packet = malloc(MAX_PACKET);
    unsigned char mapping[2] = {0, 1};
    oac_int32 value;
    int len;
    oac_test_assert(in && out && packet);

    fprintf(stdout, "  Application changes\n");
    /* An AUDIO encoder made lossless is a lossless encoder... */
    enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
    dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
    oac_test_assert(enc && dec);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_LOSSLESS)) == OAC_OK);
    oac_test_assert(oac_encoder_ctl(enc, OAC_GET_APPLICATION(&value)) == OAC_OK && value == OAC_APPLICATION_LOSSLESS);
    oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&value)) == OAC_OK && value == LOOKAHEAD);
    fill_signal(in, nb_frames*frame_size, 2, 24);
    for (f = 0; f < nb_frames; f++) {
        len = oac_encode24(enc, in + f*frame_size*2, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode24(dec, packet, len, out + f*frame_size*2, frame_size, 0) == frame_size);
    }
    for (i = 0; i < nb_frames*frame_size*2; i++)
        oac_test_assert(out[i] == expected_sample(in, i/2, 2, i&1));
    /* ...which, like any encoder, can no longer change application. */
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_AUDIO)) == OAC_BAD_ARG);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_LOSSLESS)) == OAC_OK);
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);

    /* A lossless encoder made AUDIO codes like one. */
    enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
    dec = oac_decoder_create(FS, 2, OAC_FORMAT_STANDARD, &err);
    oac_test_assert(enc && dec);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_AUDIO)) == OAC_OK);
    oac_test_assert(oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&value)) == OAC_OK && value == LOOKAHEAD + FS/250);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000)) == OAC_OK);
    for (f = 0; f < nb_frames; f++) {
        for (i = 0; i < frame_size*2; i++) in[i] = rand_int24();
        len = oac_encode24(enc, in, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
        oac_test_assert(oac_decode24(dec, packet, len, out, frame_size, 0) == frame_size);
    }
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);

    /* Not where lossless coding cannot run, nor through the multistream API. */
    enc = oac_encoder_create(24000, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
    oac_test_assert(enc != NULL);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_LOSSLESS)) == OAC_BAD_ARG);
    oac_test_assert(oac_encoder_ctl(enc, OAC_GET_APPLICATION(&value)) == OAC_OK && value == OAC_APPLICATION_AUDIO);
    oac_encoder_destroy(enc);
    enc = oac_encoder_create(FS, 4, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_AUDIO, &err);
    oac_test_assert(enc != NULL);
    oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_LOSSLESS)) == OAC_BAD_ARG);
    oac_encoder_destroy(enc);
    ms = oac_multistream_encoder_create(FS, 2, 1, 1, mapping, OAC_APPLICATION_LOSSLESS, &err);
    oac_test_assert(ms == NULL && err == OAC_BAD_ARG);
    ms = oac_multistream_encoder_create(FS, 2, 1, 1, mapping, OAC_APPLICATION_AUDIO, &err);
    oac_test_assert(ms != NULL);
    oac_test_assert(oac_multistream_encoder_ctl(ms, OAC_SET_APPLICATION(OAC_APPLICATION_LOSSLESS)) == OAC_BAD_ARG);
    oac_test_assert(oac_multistream_encoder_ctl(ms, OAC_SET_APPLICATION(OAC_APPLICATION_AUDIO)) == OAC_OK);
    oac_multistream_encoder_destroy(ms);

    free(in);
    free(out);
    free(packet);
}

int main(int argc, char **argv) {
    const char *env_seed = getenv("SEED");
    if (argc > 1)
        iseed = atoi(argv[1]);
    else if (env_seed)
        iseed = atoi(env_seed);
    else
        iseed = (oac_uint32)time(NULL)^(((oac_uint32)getpid()&65535)<<16);
    Rw = Rz = iseed;
    fuzzing = strstr(oac_get_version_string(), "-fuzzing") != NULL;
    fprintf(stdout, "Testing %s lossless coding. Random seed: %u (%.4X)\n",
            oac_get_version_string(), iseed, fast_rand()%65535);

    test_strict_lossless();
    test_lossy_fallback();
    test_fallback_into_silence();
    test_opportunistic();
    test_decoder();
    test_truncated_lossless_packet();
    test_channels();
    test_rates_and_multiframe();
    test_application();
    fprintf(stdout, "All lossless tests passed.\n");
    return 0;
}

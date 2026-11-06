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
   lossless frames in the lossy applications when they are cheaper, and the
   decoder's behaviour around OLAC frames (rate and channel conversion, packet
   loss). */

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
    /* 48 kHz mono or stereo only, no multi-frame packets, no application change. */
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
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_APPLICATION(OAC_APPLICATION_AUDIO)) == OAC_BAD_ARG);
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
       an encoder that never failed. */
    {
        OacEncoder *enc = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        OacEncoder *ref = oac_encoder_create(FS, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_LOSSLESS, &err);
        oac_test_assert(enc && ref);
        for (i = 0; i < 8*960*2; i++) in[i] = rand_int24();
        for (f = 0; f < 8; f++) {
            int len = oac_encode24(ref, in + f*960*2, 960, packet, MAX_PACKET);
            int len2, small;
            oac_test_assert(len > 3);
            for (small = 1; small < len; small += 1 + len/5)
                oac_test_assert(oac_encode24(enc, in + f*960*2, 960, packet2, small) == OAC_BUFFER_TOO_SMALL);
            len2 = oac_encode24(enc, in + f*960*2, 960, packet2, len);
            oac_test_assert(len2 == len && memcmp(packet, packet2, len) == 0);
        }
        oac_encoder_destroy(enc);
        oac_encoder_destroy(ref);
    }
    free(in);
    free(out);
    free(packet);
    free(packet2);
}

/* OAC_APPLICATION_LOSSLESS with OAC_SET_LOSSY_FALLBACK(1): lossless when it
   fits the buffer and the bit-rate, CELT otherwise, with seamless switches. */
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
        /* With the buffer as the limit... */
        roundtrip24(enc, dec, in, out, frame_size, nb_frames, channels, 1000*channels, lossless);
        for (f = 1; f < nb_frames; f++) {
            if (!noisy[f] && !noisy[f - 1]) oac_test_assert(lossless[f] == 1);
            if (noisy[f]) oac_test_assert(lossless[f] == 0);
        }
        /* ...and with the bit-rate as the limit: 400 kb/s per channel is
           1000 bytes per 20 ms frame per channel. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(400000*channels)) == OAC_OK);
        roundtrip24(enc, dec, in, out, frame_size, nb_frames, channels, MAX_PACKET, lossless);
        for (f = 1; f < nb_frames; f++) {
            if (!noisy[f] && !noisy[f - 1]) oac_test_assert(lossless[f] == 1);
            if (noisy[f]) oac_test_assert(lossless[f] == 0);
        }

        /* Constant bit-rate: every packet has exactly the target size. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_RESET_STATE) == OAC_OK);
        oac_test_assert(oac_decoder_ctl(dec, OAC_RESET_STATE) == OAC_OK);
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
        /* Silence is cheaper than any lossy target. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000)) == OAC_OK);
        for (f = 0; f < 3; f++) {
            len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && len < 16 && oac_packet_is_lossless(packet, len) == 1);
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
           silent frame has to be CELT too; the next one can be lossless. */
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(64000)) == OAC_OK);
        for (f = 0; f < 4; f++) {
            len = oac_encode(enc, pitchy, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 0);
            oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        }
        oac_test_assert(oac_encoder_ctl(enc, OAC_SET_BITRATE(OAC_BITRATE_MAX)) == OAC_OK);
        len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && (fuzzing || oac_packet_is_lossless(packet, len) == 0));
        oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
        oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
        oac_test_assert(oac_decode(dec, packet, len, out, frame_size, 0) == frame_size);
        /* Once the high-pass filter's response to the pitchy signal has died
           out, the output is silent as well. */
        for (f = 0; f < 50; f++) {
            len = oac_encode(enc, silence, frame_size, packet, MAX_PACKET);
            oac_test_assert(len > 0 && oac_packet_is_lossless(packet, len) == 1);
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
    free(noise);
    free(pitchy);
    free(silence);
    free(out);
    free(packet);
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
    test_opportunistic();
    test_decoder();
    fprintf(stdout, "All lossless tests passed.\n");
    return 0;
}

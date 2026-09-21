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

/* Copyright (c) 2011-2013 Xiph.Org Foundation
   Written by Gregory Maxwell */
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

/* This tests the API presented by the liboac system.
   It does not attempt to extensively exercise the codec internals.
   The strategy here is to simply the API interface invariants:
   That sane options are accepted, insane options are rejected,
   and that nothing blows up. In particular we don't actually test
   that settings are heeded by the codec (though we do check that
   get after set returns a sane value when it should). Other
   tests check the actual codec behavior.
   In cases where its reasonable to do so we test exhaustively,
   but its not reasonable to do so in all cases.
   Although these tests are simple they found several library bugs
   when they were initially developed. */

/* These tests are more sensitive if compiled with -DVALGRIND and
   run inside valgrind. Malloc failure testing requires glibc. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "arch.h"
#include "oac_multistream.h"
#include "oac.h"
#include "test_oac_common.h"

#ifdef VALGRIND
# include <valgrind/memcheck.h>
# define VG_UNDEF(x, y) VALGRIND_MAKE_MEM_UNDEFINED((x), (y))
# define VG_CHECK(x, y) VALGRIND_CHECK_MEM_IS_DEFINED((x), (y))
#else
# define VG_UNDEF(x, y)
# define VG_CHECK(x, y)
#endif

#if defined(HAVE___MALLOC_HOOK)
# define MALLOC_FAIL
# include "os_support.h"
# include <malloc.h>

static const oac_int32 oac_apps[3] = {OAC_APPLICATION_VOIP,
                                      OAC_APPLICATION_AUDIO, OAC_APPLICATION_RESTRICTED_LOWDELAY};

void *malloc_hook(__attribute__((unused)) size_t size,
                  __attribute__((unused)) const void *caller) {
    return 0;
}
#endif

oac_int32 *null_int_ptr = (oac_int32 *)NULL;
oac_uint32 *null_uint_ptr = (oac_uint32 *)NULL;

static const oac_int32 oac_rates[5] = {48000, 24000, 16000, 12000, 8000};

/* Scratch buffer for the encoder/decoder API tests. It only ever has to hold
   a single encoded frame at a sane bitrate. */
#define TEST_PACKET_MAX 1500

/* Largest size that the frame length code represents in one and in two bytes. */
#define TEST_SIZE_1B_MAX 191
#define TEST_SIZE_2B_MAX 8383
/* Sweep bound for the parser tests: crosses both tier boundaries. */
#define TEST_SIZE_SWEEP 8500
/* Packet buffer for the parser tests. The parser never reads the payload, so
   this only has to be large enough for the headers the tests write. */
#define TEST_PACKET_BUF 4096
/* Arbitrary packet length used where the tests only need "some plausible,
   non-degenerate packet length". */
#define TEST_PKT_LEN 1024
/* Frame size for the jumbo-packet test. Above 32767 so that it also catches a
   regression to the old oac_int16 size[] truncation. */
#define TEST_JUMBO_FRAME 100000
/* Largest channel count the ToC can signal. Mirrors the library's internal
   TEST_MAX_CHANNELS, which this test cannot see: it only links the public API. */
#define TEST_MAX_CHANNELS 256

/* Test-local reference implementation of the frame length code, written from
   the specification rather than reusing the library's. test_oac_api only links
   against the public API, so oaci_encode_size() is not available here. Returns
   the number of bytes written. */
static int ref_put_size(unsigned char *p, oac_int32 size) {
    if (size <= TEST_SIZE_1B_MAX) {
        p[0] = (unsigned char)size;
        return 1;
    } else if (size <= TEST_SIZE_2B_MAX) {
        oac_int32 v = size - 192;
        p[0] = (unsigned char)(192 + (v&0x1F));
        p[1] = (unsigned char)(v>>5);
        return 2;
    } else {
        oac_int32 v = size - 8384;
        p[0] = (unsigned char)(224 + (v&0x1F));
        p[1] = (unsigned char)((v>>5)&0xFF);
        p[2] = (unsigned char)(v>>13);
        return 3;
    }
}

/* Number of bytes the reference encoder above uses for a given size. */
static int ref_size_bytes(oac_int32 size) {
    if (size <= TEST_SIZE_1B_MAX) return 1;
    if (size <= TEST_SIZE_2B_MAX) return 2;
    return 3;
}

/* Independent reference implementation of the frame-count rules, used to
   cross-check oac_packet_get_nb_frames().  The frame count is D[m+F]/D[m],
   where D is the list of frame durations in 2.5 ms units and m is the index
   of the base frame duration signalled by the config field.  The combination
   is invalid when m+F runs off the list or when the division is not exact. */
static int ref_nb_frames(int toc, int ext) {
    static const int dur_list[8] = {1, 2, 4, 8, 16, 24, 32, 48};
    static const int silk_dur[4] = {4, 8, 16, 24};
    static const int hybrid_dur[2] = {4, 8};
    static const int celt_dur[4] = {1, 2, 4, 8};
    int config, base, m, p, F;
    if (!(toc&0x02)) return 1;
    config = toc>>3;
    if (config < 12) base = silk_dur[config&3];
    else if (config < 16) base = hybrid_dur[config&1];
    else base = celt_dur[config&3];
    for (m = 0; m < 8; m++)
        if (dur_list[m] == base) break;
    F = (ext>>4)&0x07;
    p = m + F;
    if (p > 7 || (dur_list[p] % dur_list[m]) != 0) return OAC_INVALID_PACKET;
    return dur_list[p]/dur_list[m];
}

/* Reference for the configuration checks the public accessors now apply: they
   have to reject anything the decoder would reject, so this sweep needs to know
   which ToC bytes describe a configuration that can exist at all. The packet is
   assumed to be exactly two bytes long, which makes the escape form of the
   channel count (S=1, C=7) truncated and therefore invalid. */
static int ref_toc2_valid(int toc, int ext) {
    int config, S, channels, ambisonics;
    config = toc>>3;
    S = (toc>>2)&0x01;
    ambisonics = (toc&0x02) && (ext&0x08);
    if (!(toc&0x02)) {
        channels = S + 1;
    } else if (ambisonics) {
        int order = 2*(ext&0x07) + S;
        channels = (order + 1)*(order + 1);
    } else if ((ext&0x07) == 7 && S == 1) {
        return 0;
    } else {
        channels = 2*(ext&0x07) + S + 1;
    }
    /* SILK (configs 0-11) and hybrid (configs 12-15) only ever code one or two
       standard channels (never ambisonics); CELT (16-31) codes any number. */
    return !(config < 16 && (channels > 2 || ambisonics));
}

/* Inverse of the above: the F value that packs exactly nb_frames frames of the
   duration signalled by config, or -1 when that count is not representable. */
static int ref_F_for_count(int config, int nb_frames) {
    int F;
    for (F = 0; F < 8; F++)
        if (ref_nb_frames((config<<3)|0x02, F<<4) == nb_frames) return F;
    return -1;
}

/* Test-local ToC writer, again written from the specification rather than
   reusing the library's oaci_write_toc(). It always sets X, so a single-frame
   one- or two-channel packet comes out in the legal-but-non-canonical
   two-byte form. Set force_escape to use the escape byte for a channel count
   that C and S could have carried on their own, which is also legal but not
   canonical. Returns the number of header bytes written. */
static int ref_put_toc(unsigned char *p, int config, int F, int vbr, int padding,
                       int ambisonics, int channels, int force_escape) {
    int S, C, escape;
    escape = !ambisonics && (channels > 15 || force_escape);
    if (ambisonics) {
        int order = 0;
        while ((order + 1)*(order + 1) < channels) order++;
        S = order&0x01;
        C = order>>1;
    } else if (escape) {
        S = 1;
        C = 7;
    } else {
        S = (channels - 1)&0x01;
        C = (channels - 1)>>1;
    }
    p[0] = (unsigned char)((config<<3) | (S<<2) | 0x02 | (padding ? 0x01 : 0));
    p[1] = (unsigned char)((vbr ? 0x80 : 0) | (F<<4) | (ambisonics ? 0x08 : 0) | C);
    if (escape) {
        p[2] = (unsigned char)(channels - 1);
        return 3;
    }
    return 2;
}

oac_int32 test_dec_api(void) {
    oac_uint32 dec_final_range;
    OacDecoder *dec;
    OacDecoder *dec2;
    oac_int32 i, j, cfgs;
    unsigned char packet[TEST_PACKET_MAX];
#ifndef DISABLE_FLOAT_API
    float fbuf[960*2];
#endif
    short sbuf[960*2];
    int c, err;

    cfgs = 0;
    /*First test invalid configurations which should fail*/
    fprintf(stdout, "\n  Decoder basic API tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
    for (c = 0; c < 4; c++) {
        i = oac_decoder_get_size(c, OAC_FORMAT_STANDARD);
        if (((c == 1 || c == 2) && (i <= 2048 || i > 1<<18)) || ((c != 1 && c != 2) && i != 0)) test_failed();
        fprintf(stdout, "    oac_decoder_get_size(%d)=%d ...............%s OK.\n", c, i, i > 0?"":"....");
        cfgs++;
    }

    /*Test with unsupported sample rates*/
    for (c = 0; c < 4; c++) {
        for (i = -7; i <= 96000; i++) {
            int fs;
            if ((i == 8000 || i == 12000 || i == 16000 || i == 24000 || i == 48000
#ifdef ENABLE_QEXT
                 || i == 96000
#endif
                 ) && (c == 1 || c == 2)) continue;
            switch (i) {
                case (-5): fs = -8000; break;
                case (-6): fs = INT32_MAX; break;
                case (-7): fs = INT32_MIN; break;
                default: fs = i;
            }
            err = OAC_OK;
            VG_UNDEF(&err, sizeof(err));
            dec = oac_decoder_create(fs, c, OAC_FORMAT_STANDARD, &err);
            if (err != OAC_BAD_ARG || dec != NULL) test_failed();
            cfgs++;
            dec = oac_decoder_create(fs, c, OAC_FORMAT_STANDARD, 0);
            if (dec != NULL) test_failed();
            cfgs++;
            dec = (OacDecoder*)malloc(oac_decoder_get_size(2, OAC_FORMAT_STANDARD));
            if (dec == NULL) test_failed();
            err = oac_decoder_init(dec, fs, c, OAC_FORMAT_STANDARD);
            if (err != OAC_BAD_ARG) test_failed();
            cfgs++;
            free(dec);
        }
    }

    VG_UNDEF(&err, sizeof(err));
    dec = oac_decoder_create(48000, 2, OAC_FORMAT_STANDARD, &err);
    if (err != OAC_OK || dec == NULL) test_failed();
    VG_CHECK(dec, oac_decoder_get_size(2, OAC_FORMAT_STANDARD));
    cfgs++;

    fprintf(stdout, "    oac_decoder_create() ........................ OK.\n");
    fprintf(stdout, "    oac_decoder_init() .......................... OK.\n");

    err = oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(null_uint_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    VG_UNDEF(&dec_final_range, sizeof(dec_final_range));
    err = oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&dec_final_range));
    if (err != OAC_OK) test_failed();
    VG_CHECK(&dec_final_range, sizeof(dec_final_range));
    fprintf(stdout, "    OAC_GET_FINAL_RANGE ......................... OK.\n");
    cfgs++;

    err = oac_decoder_ctl(dec, OAC_UNIMPLEMENTED);
    if (err != OAC_UNIMPLEMENTED) test_failed();
    fprintf(stdout, "    OAC_UNIMPLEMENTED ........................... OK.\n");
    cfgs++;

    err = oac_decoder_ctl(dec, OAC_GET_BANDWIDTH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_BANDWIDTH(&i));
    if (err != OAC_OK || i != 0) test_failed();
    fprintf(stdout, "    OAC_GET_BANDWIDTH ........................... OK.\n");
    cfgs++;

    err = oac_decoder_ctl(dec, OAC_GET_SAMPLE_RATE(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_SAMPLE_RATE(&i));
    if (err != OAC_OK || i != 48000) test_failed();
    fprintf(stdout, "    OAC_GET_SAMPLE_RATE ......................... OK.\n");
    cfgs++;

    /*GET_PITCH has different execution paths depending on the previously decoded frame.*/
    err = oac_decoder_ctl(dec, OAC_GET_PITCH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    VG_UNDEF(packet, sizeof(packet));
    packet[0] = 63<<2; packet[1] = packet[2] = 0;
    if (oac_decode(dec, packet, 3, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    /* Two 10 ms frames: X=1, F=1 steps the 10 ms frame up to a 20 ms packet. */
    packet[0] = 0x02;
    packet[1] = 1<<4;
    if (oac_decode(dec, packet, 2, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_PITCH ............................... OK.\n");

    err = oac_decoder_ctl(dec, OAC_GET_LAST_PACKET_DURATION(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_LAST_PACKET_DURATION(&i));
    if (err != OAC_OK || i != 960) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_LAST_PACKET_DURATION ................ OK.\n");

    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_GAIN(&i));
    VG_CHECK(&i, sizeof(i));
    if (err != OAC_OK || i != 0) test_failed();
    cfgs++;
    err = oac_decoder_ctl(dec, OAC_GET_GAIN(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_decoder_ctl(dec, OAC_SET_GAIN(-32769));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_decoder_ctl(dec, OAC_SET_GAIN(32768));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_decoder_ctl(dec, OAC_SET_GAIN(-15));
    if (err != OAC_OK) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_decoder_ctl(dec, OAC_GET_GAIN(&i));
    VG_CHECK(&i, sizeof(i));
    if (err != OAC_OK || i != -15) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_SET_GAIN ................................ OK.\n");
    fprintf(stdout, "    OAC_GET_GAIN ................................ OK.\n");

    /*Reset the decoder*/
    dec2 = (OacDecoder*)malloc(oac_decoder_get_size(2, OAC_FORMAT_STANDARD));
    memcpy(dec2, dec, oac_decoder_get_size(2, OAC_FORMAT_STANDARD));
    if (oac_decoder_ctl(dec, OAC_RESET_STATE) != OAC_OK) test_failed();
    if (memcmp(dec2, dec, oac_decoder_get_size(2, OAC_FORMAT_STANDARD)) == 0) test_failed();
    free(dec2);
    fprintf(stdout, "    OAC_RESET_STATE ............................. OK.\n");
    cfgs++;

    VG_UNDEF(packet, sizeof(packet));
    packet[0] = 0;
    if (oac_decoder_get_nb_samples(dec, packet, 1) != 480) test_failed();
    if (oac_packet_get_nb_samples(packet, 1, 48000) != 480) test_failed();
    if (oac_packet_get_nb_samples(packet, 1, 96000) != 960) test_failed();
    if (oac_packet_get_nb_samples(packet, 1, 32000) != 320) test_failed();
    if (oac_packet_get_nb_samples(packet, 1, 8000) != 80) test_failed();
    packet[0] = 3;
    if (oac_packet_get_nb_samples(packet, 1, 24000) != OAC_INVALID_PACKET) test_failed();
    /* Config 2 is a 40 ms frame; F=1 would step it to 60 ms, i.e. 1.5 frames,
       which is not representable and must be rejected. */
    packet[0] = (2<<3)|0x02;
    packet[1] = 1<<4;
    if (oac_packet_get_nb_samples(packet, 0, 24000) != OAC_BAD_ARG) test_failed();
    if (oac_packet_get_nb_samples(packet, 2, 48000) != OAC_INVALID_PACKET) test_failed();
    if (oac_decoder_get_nb_samples(dec, packet, 2) != OAC_INVALID_PACKET) test_failed();
    fprintf(stdout, "    oac_{packet,decoder}_get_nb_samples() ....... OK.\n");
    cfgs += 9;

    if (OAC_BAD_ARG != oac_packet_get_nb_frames(packet, 0)) test_failed();
    for (i = 0; i < 256; i++) {
        packet[0] = i;
        /* Without the extended ToC byte the packet holds exactly one frame;
           with X set but the byte missing the packet is truncated. */
        if (((i&0x02) ? OAC_INVALID_PACKET : 1) != oac_packet_get_nb_frames(packet, 1)) test_failed();
        cfgs++;
        for (j = 0; j < 256; j++) {
            int want;
            packet[1] = j;
            want = ref_toc2_valid(i, j) ? ref_nb_frames(i, j) : OAC_INVALID_PACKET;
            if (want != oac_packet_get_nb_frames(packet, 2)) test_failed();
            cfgs++;
        }
    }
    fprintf(stdout, "    oac_packet_get_nb_frames() .................. OK.\n");

    /* These two sweeps cover every possible main ToC byte, including the S, X
       and P bits. The accessors validate the framing now, so each case has to
       be a packet that actually parses: an all-zero extended byte (one frame,
       S+1 channels) and an all-zero padding length keep every config legal. */
    for (i = 0; i < 256; i++) {
        int bw;
        memset(packet, 0, 8);
        packet[0] = i;
        bw = packet[0]>>4;
        bw = OAC_BANDWIDTH_NARROWBAND + (((((bw&7)*9)&(63 - (bw&8))) + 2 + 12*((bw&8) != 0))>>4);
        if (bw != oac_packet_get_bandwidth(packet, 8)) test_failed();
        cfgs++;
    }
    fprintf(stdout, "    oac_packet_get_bandwidth() .................. OK.\n");

    for (i = 0; i < 256; i++) {
        int fp3s, rate;
        memset(packet, 0, 8);
        packet[0] = i;
        fp3s = packet[0]>>3;
        fp3s = ((((3 - (fp3s&3))*13&119) + 9)>>2)*((fp3s > 13)*(3 - ((fp3s&3) == 3)) + 1)*25;
        for (rate = 0; rate < 5; rate++) {
            if ((oac_rates[rate]*3/fp3s) != oac_packet_get_samples_per_frame(packet, 8, oac_rates[rate])) test_failed();
            cfgs++;
        }
    }
    fprintf(stdout, "    oac_packet_get_samples_per_frame() .......... OK.\n");

    /* Both must refuse a packet oac_decode() would refuse, rather than
       reporting a property of something unusable. A SILK ToC claiming four
       channels is rejected by oaci_validate_config(). */
    memset(packet, 0, 8);
    packet[0] = (1<<3)|0x02;    /* config 1: SILK NB 20 ms, X=1 */
    packet[1] = 0x01;           /* A=0, C=1, S=0 -> 3 channels */
    if (oac_packet_get_bandwidth(packet, 8) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_samples_per_frame(packet, 8, 48000) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_nb_channels(packet, 8) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_nb_frames(packet, 8) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_format(packet, 8) != OAC_INVALID_PACKET) test_failed();
    cfgs += 5;
    /* The same configuration in CELT is fine. */
    packet[0] = (31<<3)|0x02;   /* config 31: CELT FB 20 ms, X=1 */
    if (oac_packet_get_nb_channels(packet, 8) != 3) test_failed();
    if (oac_packet_get_bandwidth(packet, 8) != OAC_BANDWIDTH_FULLBAND) test_failed();
    cfgs += 2;
    /* Truncated headers are rejected, and no data at all is OAC_BAD_ARG. */
    if (oac_packet_get_bandwidth(packet, 1) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_samples_per_frame(packet, 1, 48000) != OAC_INVALID_PACKET) test_failed();
    if (oac_packet_get_bandwidth(packet, 0) != OAC_BAD_ARG) test_failed();
    if (oac_packet_get_samples_per_frame(packet, 0, 48000) != OAC_BAD_ARG) test_failed();
    cfgs += 4;
    fprintf(stdout, "    ToC accessor validation ..................... OK.\n");

    /* Config 2 is a 40 ms frame; F=1 would step it to 60 ms, i.e. 1.5 frames,
       which is not representable and must be rejected. */
    packet[0] = (2<<3)|0x02;
    packet[1] = 1<<4;
    for (j = 2; j < 51; j++) packet[j] = 0;
    VG_UNDEF(sbuf, sizeof(sbuf));
    if (oac_decode(dec, packet, 51, sbuf, 960, 0) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    packet[0] = (63<<2);
    packet[1] = packet[2] = 0;
    if (oac_decode(dec, packet, -1, sbuf, 960, 0) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_decode(dec, packet, 3, sbuf, 60, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
    cfgs++;
    if (oac_decode(dec, packet, 3, sbuf, 480, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
    cfgs++;
    if (oac_decode(dec, packet, 3, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_decode() ................................ OK.\n");
#ifndef DISABLE_FLOAT_API
    VG_UNDEF(fbuf, sizeof(fbuf));
    if (oac_decode_float(dec, packet, 3, fbuf, 960, 0) != 960) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_decode_float() .......................... OK.\n");
#endif

#if 0
    /*These tests are disabled because the library crashes with null states*/
    if (oac_decoder_ctl(0, OAC_RESET_STATE)         != OAC_INVALID_STATE)test_failed();
    if (oac_decoder_init(0, 48000, 1, OAC_FORMAT_STANDARD)                 != OAC_INVALID_STATE)test_failed();
    if (oac_decode(0, packet, 1, outbuf, 2880, 0)        != OAC_INVALID_STATE)test_failed();
    if (oac_decode_float(0, packet, 1, 0, 2880, 0)       != OAC_INVALID_STATE)test_failed();
    if (oac_decoder_get_nb_samples(0, packet, 1)      != OAC_INVALID_STATE)test_failed();
    if (oac_packet_get_nb_frames(NULL, 1)            != OAC_BAD_ARG)test_failed();
    if (oac_packet_get_bandwidth(NULL, 1)           != OAC_BAD_ARG)test_failed();
    if (oac_packet_get_samples_per_frame(NULL, 1, 48000) != OAC_BAD_ARG) test_failed();
#endif
    oac_decoder_destroy(dec);
    cfgs++;
    fprintf(stdout, "                   All decoder interface tests passed\n");
    fprintf(stdout, "                             (%6d API invocations)\n", cfgs);
    return cfgs;
}

oac_int32 test_msdec_api(void) {
    oac_uint32 dec_final_range;
    OacMSDecoder *dec;
    OacDecoder *streamdec;
    oac_int32 i, j, cfgs;
    unsigned char packet[TEST_PACKET_MAX];
    unsigned char mapping[256];
#ifndef DISABLE_FLOAT_API
    float fbuf[960*2];
#endif
    short sbuf[960*2];
    int a, b, c, err;

    mapping[0] = 0;
    mapping[1] = 1;
    for (i = 2; i < 256; i++) VG_UNDEF(&mapping[i], sizeof(unsigned char));

    cfgs = 0;
    /*First test invalid configurations which should fail*/
    fprintf(stdout, "\n  Multistream decoder basic API tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
    for (a = -1; a < 4; a++) {
        for (b = -1; b < 4; b++) {
            i = oac_multistream_decoder_get_size(a, b);
            if (((a > 0 && b <= a && b >= 0) && (i <= 2048 || i > ((1<<18)*a)))
                || ((a < 1 || b > a || b < 0) && i != 0)) test_failed();
            fprintf(stdout, "    oac_multistream_decoder_get_size(%2d,%2d)=%d %sOK.\n", a, b, i, i > 0?"":"... ");
            cfgs++;
        }
    }

    /*Test with unsupported sample rates*/
    for (c = 1; c < 3; c++) {
        for (i = -7; i <= 96000; i++) {
            int fs;
            if ((i == 8000 || i == 12000 || i == 16000 || i == 24000 || i == 48000
#ifdef ENABLE_QEXT
                 || i == 96000
#endif
                 ) && (c == 1 || c == 2)) continue;
            switch (i) {
                case (-5): fs = -8000; break;
                case (-6): fs = INT32_MAX; break;
                case (-7): fs = INT32_MIN; break;
                default: fs = i;
            }
            err = OAC_OK;
            VG_UNDEF(&err, sizeof(err));
            dec = oac_multistream_decoder_create(fs, c, 1, c - 1, mapping, &err);
            if (err != OAC_BAD_ARG || dec != NULL) test_failed();
            cfgs++;
            dec = oac_multistream_decoder_create(fs, c, 1, c - 1, mapping, 0);
            if (dec != NULL) test_failed();
            cfgs++;
            dec = (OacMSDecoder*)malloc(oac_multistream_decoder_get_size(1, 1));
            if (dec == NULL) test_failed();
            err = oac_multistream_decoder_init(dec, fs, c, 1, c - 1, mapping);
            if (err != OAC_BAD_ARG) test_failed();
            cfgs++;
            free(dec);
        }
    }

    for (c = 0; c < 2; c++) {
        int *ret_err;
        ret_err = c?0:&err;

        mapping[0] = 0;
        mapping[1] = 1;
        for (i = 2; i < 256; i++) VG_UNDEF(&mapping[i], sizeof(unsigned char));

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 2, 1, 0, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        mapping[0] = mapping[1] = 0;
        dec = oac_multistream_decoder_create(48000, 2, 1, 0, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_OK) || dec == NULL) test_failed();
        cfgs++;
        oac_multistream_decoder_destroy(dec);
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 1, 4, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_OK) || dec == NULL) test_failed();
        cfgs++;

        err = oac_multistream_decoder_init(dec, 48000, 1, 0, 0, mapping);
        if (err != OAC_BAD_ARG) test_failed();
        cfgs++;

        err = oac_multistream_decoder_init(dec, 48000, 1, 1, -1, mapping);
        if (err != OAC_BAD_ARG) test_failed();
        cfgs++;

        oac_multistream_decoder_destroy(dec);
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 2, 1, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_OK) || dec == NULL) test_failed();
        cfgs++;
        oac_multistream_decoder_destroy(dec);
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 255, 255, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, -1, 1, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 0, 1, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 1, -1, 2, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 1, -1, -1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 256, 255, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        dec = oac_multistream_decoder_create(48000, 256, 255, 0, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        mapping[0] = 255;
        mapping[1] = 1;
        mapping[2] = 2;
        dec = oac_multistream_decoder_create(48000, 3, 2, 0, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        mapping[0] = 0;
        mapping[1] = 0;
        mapping[2] = 0;
        dec = oac_multistream_decoder_create(48000, 3, 2, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_OK) || dec == NULL) test_failed();
        cfgs++;
        oac_multistream_decoder_destroy(dec);
        cfgs++;

        VG_UNDEF(ret_err, sizeof(*ret_err));
        mapping[0] = 0;
        mapping[1] = 255;
        mapping[2] = 1;
        mapping[3] = 2;
        mapping[4] = 3;
        dec = oac_multistream_decoder_create(48001, 5, 4, 1, mapping, ret_err);
        if (ret_err) {
            VG_CHECK(ret_err, sizeof(*ret_err));
        }
        if ((ret_err && *ret_err != OAC_BAD_ARG) || dec != NULL) test_failed();
        cfgs++;
    }

    VG_UNDEF(&err, sizeof(err));
    mapping[0] = 0;
    mapping[1] = 255;
    mapping[2] = 1;
    mapping[3] = 2;
    dec = oac_multistream_decoder_create(48000, 4, 2, 1, mapping, &err);
    VG_CHECK(&err, sizeof(err));
    if (err != OAC_OK || dec == NULL) test_failed();
    cfgs++;

    fprintf(stdout, "    oac_multistream_decoder_create() ............ OK.\n");
    fprintf(stdout, "    oac_multistream_decoder_init() .............. OK.\n");

    VG_UNDEF(&dec_final_range, sizeof(dec_final_range));
    err = oac_multistream_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&dec_final_range));
    if (err != OAC_OK) test_failed();
    VG_CHECK(&dec_final_range, sizeof(dec_final_range));
    fprintf(stdout, "    OAC_GET_FINAL_RANGE ......................... OK.\n");
    cfgs++;

    streamdec = 0;
    VG_UNDEF(&streamdec, sizeof(streamdec));
    err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(-1, &streamdec));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(1, &streamdec));
    if (err != OAC_OK || streamdec == NULL) test_failed();
    VG_CHECK(streamdec, oac_decoder_get_size(1, OAC_FORMAT_STANDARD));
    cfgs++;
    err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(2, &streamdec));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(0, &streamdec));
    if (err != OAC_OK || streamdec == NULL) test_failed();
    VG_CHECK(streamdec, oac_decoder_get_size(1, OAC_FORMAT_STANDARD));
    fprintf(stdout, "    OAC_MULTISTREAM_GET_DECODER_STATE ........... OK.\n");
    cfgs++;

    for (j = 0; j < 2; j++) {
        OacDecoder *od;
        err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(j, &od));
        if (err != OAC_OK) test_failed();
        VG_UNDEF(&i, sizeof(i));
        err = oac_decoder_ctl(od, OAC_GET_GAIN(&i));
        VG_CHECK(&i, sizeof(i));
        if (err != OAC_OK || i != 0) test_failed();
        cfgs++;
    }
    err = oac_multistream_decoder_ctl(dec, OAC_SET_GAIN(15));
    if (err != OAC_OK) test_failed();
    fprintf(stdout, "    OAC_SET_GAIN ................................ OK.\n");
    for (j = 0; j < 2; j++) {
        OacDecoder *od;
        err = oac_multistream_decoder_ctl(dec, OAC_MULTISTREAM_GET_DECODER_STATE(j, &od));
        if (err != OAC_OK) test_failed();
        VG_UNDEF(&i, sizeof(i));
        err = oac_decoder_ctl(od, OAC_GET_GAIN(&i));
        VG_CHECK(&i, sizeof(i));
        if (err != OAC_OK || i != 15) test_failed();
        cfgs++;
    }
    fprintf(stdout, "    OAC_GET_GAIN ................................ OK.\n");

    VG_UNDEF(&i, sizeof(i));
    err = oac_multistream_decoder_ctl(dec, OAC_GET_BANDWIDTH(&i));
    if (err != OAC_OK || i != 0) test_failed();
    fprintf(stdout, "    OAC_GET_BANDWIDTH ........................... OK.\n");
    cfgs++;

    err = oac_multistream_decoder_ctl(dec, OAC_UNIMPLEMENTED);
    if (err != OAC_UNIMPLEMENTED) test_failed();
    fprintf(stdout, "    OAC_UNIMPLEMENTED ........................... OK.\n");
    cfgs++;

#if 0
    /*Currently unimplemented for multistream*/
    /*GET_PITCH has different execution paths depending on the previously decoded frame.*/
    err = oac_multistream_decoder_ctl(dec, OAC_GET_PITCH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_multistream_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    VG_UNDEF(packet, sizeof(packet));
    packet[0] = 63<<2; packet[1] = packet[2] = 0;
    if (oac_multistream_decode(dec, packet, 3, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_multistream_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    packet[0] = 1;
    if (oac_multistream_decode(dec, packet, 1, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    err = oac_multistream_decoder_ctl(dec, OAC_GET_PITCH(&i));
    if (err != OAC_OK || i > 0 || i < -1) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_PITCH ............................... OK.\n");
#endif

    /*Reset the decoder*/
    if (oac_multistream_decoder_ctl(dec, OAC_RESET_STATE) != OAC_OK) test_failed();
    fprintf(stdout, "    OAC_RESET_STATE ............................. OK.\n");
    cfgs++;

    oac_multistream_decoder_destroy(dec);
    cfgs++;
    VG_UNDEF(&err, sizeof(err));
    dec = oac_multistream_decoder_create(48000, 2, 1, 1, mapping, &err);
    if (err != OAC_OK || dec == NULL) test_failed();
    cfgs++;

    /* Config 2 is a 40 ms frame; F=1 would step it to 60 ms, i.e. 1.5 frames,
       which is not representable and must be rejected. */
    packet[0] = (2<<3)|0x02;
    packet[1] = 1<<4;
    for (j = 2; j < 51; j++) packet[j] = 0;
    VG_UNDEF(sbuf, sizeof(sbuf));
    if (oac_multistream_decode(dec, packet, 51, sbuf, 960, 0) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    packet[0] = (63<<2);
    packet[1] = packet[2] = 0;
    if (oac_multistream_decode(dec, packet, -1, sbuf, 960, 0) != OAC_BAD_ARG) {
        printf("%d\n", oac_multistream_decode(dec, packet, -1, sbuf, 960, 0)); test_failed();
    }
    cfgs++;
    if (oac_multistream_decode(dec, packet, 3, sbuf, -960, 0) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_multistream_decode(dec, packet, 3, sbuf, 60, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
    cfgs++;
    if (oac_multistream_decode(dec, packet, 3, sbuf, 480, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
    cfgs++;
    if (oac_multistream_decode(dec, packet, 3, sbuf, 960, 0) != 960) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_multistream_decode() .................... OK.\n");
#ifndef DISABLE_FLOAT_API
    VG_UNDEF(fbuf, sizeof(fbuf));
    if (oac_multistream_decode_float(dec, packet, 3, fbuf, 960, 0) != 960) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_multistream_decode_float() .............. OK.\n");
#endif

#if 0
    /*These tests are disabled because the library crashes with null states*/
    if (oac_multistream_decoder_ctl(0, OAC_RESET_STATE)         != OAC_INVALID_STATE)test_failed();
    if (oac_multistream_decoder_init(0, 48000, 1)                 != OAC_INVALID_STATE)test_failed();
    if (oac_multistream_decode(0, packet, 1, outbuf, 2880, 0)        != OAC_INVALID_STATE)test_failed();
    if (oac_multistream_decode_float(0, packet, 1, 0, 2880, 0)       != OAC_INVALID_STATE)test_failed();
    if (oac_multistream_decoder_get_nb_samples(0, packet, 1)      != OAC_INVALID_STATE)test_failed();
#endif
    oac_multistream_decoder_destroy(dec);
    cfgs++;
    fprintf(stdout, "       All multistream decoder interface tests passed\n");
    fprintf(stdout, "                             (%6d API invocations)\n", cfgs);
    return cfgs;
}

#ifdef VALGRIND
# define UNDEFINE_FOR_PARSE  toc = -1; \
        frames[0] = (unsigned char *)0; \
        frames[1] = (unsigned char *)0; \
        payload_offset = -1; \
        VG_UNDEF(&toc, sizeof(toc)); \
        VG_UNDEF(frames, sizeof(frames)); \
        VG_UNDEF(&payload_offset, sizeof(payload_offset));
#else
# define UNDEFINE_FOR_PARSE  toc = -1; \
        frames[0] = (unsigned char *)0; \
        frames[1] = (unsigned char *)0; \
        payload_offset = -1;
#endif

/* This test exercises the heck out of the liboac parser.
   It is much larger than the parser itself in part because
   it tries to hit a lot of corner cases that could never
   fail with the liboac code, but might be problematic for
   other implementations. */
oac_int32 test_parse(void) {
    oac_int32 i, j, jj, sz;
    unsigned char packet[TEST_PACKET_BUF];
    oac_int32 cfgs, cfgs_total;
    unsigned char toc;
    const unsigned char *frames[OAC_MAX_FRAMES_PER_PACKET];
    oac_int32 size[OAC_MAX_FRAMES_PER_PACKET];
    int payload_offset, ret, nb;
    int config, S, F, count, hdr;

    fprintf(stdout, "\n  Packet header parsing tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
    memset(packet, 0, sizeof(packet));
    packet[0] = 63<<2;
    if (oac_packet_parse(packet, 1, &toc, frames, 0, &payload_offset) != OAC_BAD_ARG) test_failed();
    if (oac_packet_parse(packet, -1, &toc, frames, size, &payload_offset) != OAC_BAD_ARG) test_failed();
    if (oac_packet_parse(packet, 0, &toc, frames, size, &payload_offset) != OAC_INVALID_PACKET) test_failed();
    cfgs_total = cfgs = 3;

    /*X=0: exactly one frame of S+1 channels, whose length is implicit*/
    for (i = 0; i < 64; i++) {
        packet[0] = i<<2;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 1) test_failed();
        if (size[0] != 3) test_failed();
        if (frames[0] != packet + 1) test_failed();
        if (payload_offset != 1) test_failed();
        if (toc != packet[0]) test_failed();
    }
    fprintf(stdout, "    X=0 (%2d cases) ............................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=0, the largest representable implicit length and one past it*/
    for (i = 0; i < 64; i++) {
        packet[0] = i<<2;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, OAC_SIZE_MAX + 1, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 1) test_failed();
        if (size[0] != OAC_SIZE_MAX) test_failed();
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, OAC_SIZE_MAX + 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    fprintf(stdout, "    X=0 size limit (%2d cases) ................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=1 but the extended ToC byte is missing*/
    for (i = 0; i < 64; i++) {
        packet[0] = (i<<2) | 0x02;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 1, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    /*...and the same for the channel escape byte*/
    for (config = 16; config < 32; config++) {
        hdr = ref_put_toc(packet, config, 0, 0, 0, 0, 64, 0);
        if (hdr != 3) test_failed();
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    fprintf(stdout, "    truncated ToC (%2d cases) .................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=0, P=1: a single frame preceded by a 255-chained padding length*/
    for (i = 0; i < 64; i++) {
        packet[0] = (i<<2) | 0x01;
        for (jj = 0; jj < 600; jj += 7) {
            int pos;
            for (pos = 0; pos < jj/254; pos++) packet[1 + pos] = 255;
            packet[1 + pos] = jj%254;
            pos++;
            /*Coding more padding than the packet has room for*/
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 1 + pos + jj - 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 1 + pos + jj + TEST_PKT_LEN, &toc, frames, size,
                                       &payload_offset);
            cfgs++;
            if (ret != 1) test_failed();
            if (size[0] != TEST_PKT_LEN) test_failed();
            if (payload_offset != 1 + pos) test_failed();
        }
    }
    fprintf(stdout, "    X=0 padding (%2d cases) .................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=1, V=0: every frame the same size, with the count taken from F*/
    for (i = 0; i < 64; i++) {
        config = i>>1;
        S = i&1;
        for (F = 0; F < 8; F++) {
            packet[0] = (config<<3) | (S<<2) | 0x02;
            packet[1] = F<<4;
            count = ref_nb_frames(packet[0], packet[1]);
            for (sz = 0; sz < 200; sz++) {
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, 2 + sz, &toc, frames, size, &payload_offset);
                cfgs++;
                if (count < 0 || sz%count != 0) {
                    if (ret != OAC_INVALID_PACKET) test_failed();
                } else {
                    if (ret != count) test_failed();
                    for (jj = 0; jj < count; jj++) if (size[jj] != sz/count) test_failed();
                    if (frames[0] != packet + 2) test_failed();
                    if (payload_offset != 2) test_failed();
                    if (toc != packet[0]) test_failed();
                }
            }
        }
    }
    fprintf(stdout, "    X=1 CBR (%6d cases) ..................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*Super jumbo packets. The frame size here is deliberately above 32767,
      which the old oac_int16 size[] silently truncated.*/
    for (i = 0; i < 64; i++) {
        config = i>>1;
        S = i&1;
        for (F = 0; F < 8; F++) {
            packet[0] = (config<<3) | (S<<2) | 0x02;
            packet[1] = F<<4;
            count = ref_nb_frames(packet[0], packet[1]);
            if (count < 0) continue;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, (oac_int32)TEST_JUMBO_FRAME*count + 2, &toc, frames,
                                       size, &payload_offset);
            cfgs++;
            if (ret != count) test_failed();
            for (jj = 0; jj < count; jj++) if (size[jj] != TEST_JUMBO_FRAME) test_failed();
        }
    }
    fprintf(stdout, "    X=1 CBR jumbo (%2d cases) .................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=1, V=1: an explicit length for every frame but the last*/
    for (i = 0; i < 64; i++) {
        config = i>>1;
        S = i&1;
        for (F = 0; F < 8; F++) {
            packet[0] = (config<<3) | (S<<2) | 0x02;
            packet[1] = 0x80 | (F<<4);
            count = ref_nb_frames(packet[0], packet[1]);
            if (count < 0) {
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, TEST_PKT_LEN, &toc, frames, size, &payload_offset);
                cfgs++;
                if (ret != OAC_INVALID_PACKET) test_failed();
                continue;
            }
            /*The cheapest encoding: every length field zero, no payload*/
            for (jj = 2; jj < 2 + count; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + count - 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != count) test_failed();
            for (jj = 0; jj < count; jj++) if (size[jj] != 0) test_failed();
            if (toc != packet[0]) test_failed();
            if (count < 2) continue;
            /*One length field short of the packet*/
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + count - 2, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*A three-byte length code, one byte too short...*/
            nb = ref_put_size(&packet[2], TEST_SIZE_2B_MAX + 1);
            for (jj = 2 + nb; jj < 2 + nb + count - 2; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + (count - 2) + TEST_SIZE_2B_MAX, &toc, frames,
                                       size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*...and exactly long enough*/
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + (count - 2) + TEST_SIZE_2B_MAX + 1, &toc,
                                       frames, size, &payload_offset);
            cfgs++;
            if (ret != count) test_failed();
            if (size[0] != TEST_SIZE_2B_MAX + 1) test_failed();
            for (jj = 1; jj < count; jj++) if (size[jj] != 0) test_failed();
            if (toc != packet[0]) test_failed();
            /*Quasi-CBR use of VBR framing, spanning all three length tiers*/
            for (sz = 0; sz < 5; sz++) {
                const int tsz[5] = {50, 201, 403, 1472, 20400};
                int pos = 0;
                int as = (tsz[sz] - count - 2)/count;
                if (as < 0) continue;
                for (jj = 0; jj < count - 1; jj++) pos += ref_put_size(&packet[2 + pos], as);
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, tsz[sz], &toc, frames, size, &payload_offset);
                cfgs++;
                if (ret != count) test_failed();
                for (jj = 0; jj < count - 1; jj++) if (size[jj] != as) test_failed();
                if (size[count - 1] != tsz[sz] - 2 - pos - as*(count - 1)) test_failed();
                if (toc != packet[0]) test_failed();
            }
        }
    }
    fprintf(stdout, "    X=1 VBR (%6d cases) ..................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*X=1, P=1: the padding length comes before the frame length fields and the
      padding itself goes at the very end*/
    for (i = 0; i < 64; i++) {
        config = i>>1;
        S = i&1;
        for (F = 0; F < 8; F++) {
            packet[0] = (config<<3) | (S<<2) | 0x03;
            packet[1] = F<<4;
            count = ref_nb_frames(packet[0], packet[1]);
            if (count < 0) continue;
            for (jj = 0; jj < 520; jj += 37) {
                int pos;
                for (pos = 0; pos < jj/254; pos++) packet[2 + pos] = 255;
                packet[2 + pos] = jj%254;
                pos++;
                /*Coding more padding than the packet has room for*/
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, 2 + pos + jj - 1, &toc, frames, size,
                                           &payload_offset);
                cfgs++;
                if (ret != OAC_INVALID_PACKET) test_failed();
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, 2 + pos + jj + count, &toc, frames, size,
                                           &payload_offset);
                cfgs++;
                if (ret != count) test_failed();
                for (sz = 0; sz < count; sz++) if (size[sz] != 1) test_failed();
                if (payload_offset != 2 + pos) test_failed();
            }
        }
    }
    fprintf(stdout, "    X=1 padding (%6d cases) ................. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*Standard multichannel. C and S resolve 1..15 channels directly and
      C=7,S=1 escapes to an explicit count in a third ToC byte. SILK and hybrid
      only ever code one or two channels, so anything above that is rejected.*/
    for (config = 0; config < 32; config++) {
        int max_channels = config < 16 ? 2 : TEST_MAX_CHANNELS;
        for (j = 1; j <= TEST_MAX_CHANNELS; j++) {
            hdr = ref_put_toc(packet, config, 0, 0, 0, 0, j, 0);
            if (hdr != (j > 15 ? 3 : 2)) test_failed();
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, hdr + 7, &toc, frames, size, &payload_offset);
            cfgs++;
            /*The accessors apply the same policy as the decoder, so they
              answer only for the packets oac_packet_parse() accepts.*/
            if (j <= max_channels) {
                if (oac_packet_get_nb_channels(packet, hdr) != j) test_failed();
                if (oac_packet_get_format(packet, hdr) != OAC_FORMAT_STANDARD) test_failed();
            } else {
                if (oac_packet_get_nb_channels(packet, hdr) != OAC_INVALID_PACKET) test_failed();
                if (oac_packet_get_format(packet, hdr) != OAC_INVALID_PACKET) test_failed();
            }
            cfgs += 2;
            if (j <= max_channels) {
                if (ret != 1) test_failed();
                if (size[0] != 7) test_failed();
                if (payload_offset != hdr) test_failed();
            } else if (ret != OAC_INVALID_PACKET) test_failed();
        }
        /*The escape byte may also carry 1..15: legal, just not canonical.*/
        for (j = 1; j <= 15; j++) {
            hdr = ref_put_toc(packet, config, 0, 0, 0, 0, j, 1);
            if (hdr != 3) test_failed();
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, hdr + 7, &toc, frames, size, &payload_offset);
            cfgs++;
            if (oac_packet_get_nb_channels(packet, hdr)
                != (j <= max_channels ? j : OAC_INVALID_PACKET)) test_failed();
            cfgs++;
            if (j <= max_channels) {
                if (ret != 1) test_failed();
                if (size[0] != 7) test_failed();
                if (payload_offset != hdr) test_failed();
            } else if (ret != OAC_INVALID_PACKET) test_failed();
        }
    }
    fprintf(stdout, "    channel signalling (%2d cases) ............. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /*Ambisonics: 2*C+S is the order, so orders 0..15 are all representable and
      the escape byte is never needed. Ambisonics is CELT-only, so SILK and
      hybrid configs (0..15) are rejected even at order 0.*/
    for (config = 0; config < 32; config++) {
        for (j = 0; j <= 15; j++) {
            hdr = ref_put_toc(packet, config, 0, 0, 0, 1, (j + 1)*(j + 1), 0);
            if (hdr != 2) test_failed();
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, hdr + 7, &toc, frames, size, &payload_offset);
            cfgs++;
            if (config >= 16) {
                if (oac_packet_get_nb_channels(packet, hdr) != (j + 1)*(j + 1)) test_failed();
                if (oac_packet_get_format(packet, hdr) != OAC_FORMAT_AMBISONICS) test_failed();
                cfgs += 2;
                if (ret != 1) test_failed();
                if (size[0] != 7) test_failed();
            } else {
                if (oac_packet_get_nb_channels(packet, hdr) != OAC_INVALID_PACKET) test_failed();
                if (oac_packet_get_format(packet, hdr) != OAC_INVALID_PACKET) test_failed();
                cfgs += 2;
                if (ret != OAC_INVALID_PACKET) test_failed();
            }
        }
    }
    fprintf(stdout, "    ambisonics signalling (%2d cases) .......... OK.\n", cfgs);
    cfgs_total += cfgs;

    fprintf(stdout, "    oac_packet_parse ............................ OK.\n");
    fprintf(stdout, "                      All packet parsing tests passed\n");
    fprintf(stdout, "                          (%d API invocations)\n", cfgs_total);
    return cfgs_total;
}

/* This is a helper macro for the encoder tests.
   The encoder api tests all have a pattern of set-must-fail, set-must-fail,
   set-must-pass, get-and-compare, set-must-pass, get-and-compare. */
#define CHECK_SETGET(setcall, getcall, badv, badv2, goodv, goodv2, sok, gok) \
        i = (badv); \
        if (oac_encoder_ctl(enc, setcall) == OAC_OK) test_failed(); \
        i = (badv2); \
        if (oac_encoder_ctl(enc, setcall) == OAC_OK) test_failed(); \
        j = i = (goodv); \
        if (oac_encoder_ctl(enc, setcall) != OAC_OK) test_failed(); \
        i = -12345; \
        VG_UNDEF(&i, sizeof(i)); \
        err = oac_encoder_ctl(enc, getcall); \
        if (err != OAC_OK || i != j) test_failed(); \
        j = i = (goodv2); \
        if (oac_encoder_ctl(enc, setcall) != OAC_OK) test_failed(); \
        fprintf(stdout, sok); \
        i = -12345; \
        VG_UNDEF(&i, sizeof(i)); \
        err = oac_encoder_ctl(enc, getcall); \
        if (err != OAC_OK || i != j) test_failed(); \
        fprintf(stdout, gok); \
        cfgs += 6;

oac_int32 test_enc_api(void) {
    oac_uint32 enc_final_range;
    OacEncoder *enc;
    oac_int32 i, j;
    unsigned char packet[TEST_PACKET_MAX];
#ifndef DISABLE_FLOAT_API
    float fbuf[960*2];
#endif
    short sbuf[960*2];
    int c, err, cfgs;

    cfgs = 0;
    /*First test invalid configurations which should fail*/
    fprintf(stdout, "\n  Encoder basic API tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
    for (c = 0; c < 4; c++) {
        i = oac_encoder_get_size(c, OAC_FORMAT_STANDARD);
#ifdef RESYNTH
        /* RESYNTH builds allocate per-channel state sized for TEST_MAX_CHANNELS, so the
           encoder allocation greatly exceeds the normal upper bound. Skip the size
           range check; just verify the zero/non-zero pattern across configurations. */
        if (((c == 1 || c == 2) && i <= 0) || ((c != 1 && c != 2) && i != 0)) test_failed();
#else
        if (((c == 1 || c == 2) && (i <= 2048 || i > 1<<18)) || ((c != 1 && c != 2) && i != 0)) test_failed();
#endif
        fprintf(stdout, "    oac_encoder_get_size(%d)=%d ...............%s OK.\n", c, i, i > 0?"":"....");
        cfgs++;
    }

    /*Test with unsupported sample rates, channel counts*/
    for (c = 0; c < 4; c++) {
        for (i = -7; i <= 96000; i++) {
            int fs;
            if ((i == 8000 || i == 12000 || i == 16000 || i == 24000 || i == 48000
#ifdef ENABLE_QEXT
                 || i == 96000
#endif
                 ) && (c == 1 || c == 2)) continue;
            switch (i) {
                case (-5): fs = -8000; break;
                case (-6): fs = INT32_MAX; break;
                case (-7): fs = INT32_MIN; break;
                default: fs = i;
            }
            err = OAC_OK;
            VG_UNDEF(&err, sizeof(err));
            enc = oac_encoder_create(fs, c, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP, &err);
            if (err != OAC_BAD_ARG || enc != NULL) test_failed();
            cfgs++;
            enc = oac_encoder_create(fs, c, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP, 0);
            if (enc != NULL) test_failed();
            cfgs++;
            oac_encoder_destroy(enc);
            enc = (OacEncoder*)malloc(oac_encoder_get_size(2, OAC_FORMAT_STANDARD));
            if (enc == NULL) test_failed();
            err = oac_encoder_init(enc, fs, c, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP);
            if (err != OAC_BAD_ARG) test_failed();
            cfgs++;
            free(enc);
        }
    }

    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_AUTO, NULL);
    if (enc != NULL) test_failed();
    cfgs++;

    VG_UNDEF(&err, sizeof(err));
    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_AUTO, &err);
    if (err != OAC_BAD_ARG || enc != NULL) test_failed();
    cfgs++;

    VG_UNDEF(&err, sizeof(err));
    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP, NULL);
    if (enc == NULL) test_failed();
    oac_encoder_destroy(enc);
    cfgs++;

    VG_UNDEF(&err, sizeof(err));
    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err != OAC_OK || enc == NULL) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&i));
    if (err != OAC_OK || i < 0 || i > 32766) test_failed();
    cfgs++;
    oac_encoder_destroy(enc);

    VG_UNDEF(&err, sizeof(err));
    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || enc == NULL) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&i));
    if (err != OAC_OK || i < 0 || i > 32766) test_failed();
    oac_encoder_destroy(enc);
    cfgs++;

    VG_UNDEF(&err, sizeof(err));
    enc = oac_encoder_create(48000, 2, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP, &err);
    if (err != OAC_OK || enc == NULL) test_failed();
    cfgs++;

    fprintf(stdout, "    oac_encoder_create() ........................ OK.\n");
    fprintf(stdout, "    oac_encoder_init() .......................... OK.\n");

    i = -12345;
    VG_UNDEF(&i, sizeof(i));
    err = oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(&i));
    if (err != OAC_OK || i < 0 || i > 32766) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_LOOKAHEAD(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_LOOKAHEAD ........................... OK.\n");

    err = oac_encoder_ctl(enc, OAC_GET_SAMPLE_RATE(&i));
    if (err != OAC_OK || i != 48000) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_SAMPLE_RATE(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_SAMPLE_RATE ......................... OK.\n");

    if (oac_encoder_ctl(enc, OAC_UNIMPLEMENTED) != OAC_UNIMPLEMENTED) test_failed();
    fprintf(stdout, "    OAC_UNIMPLEMENTED ........................... OK.\n");
    cfgs++;

    err = oac_encoder_ctl(enc, OAC_GET_APPLICATION(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_APPLICATION(i), OAC_GET_APPLICATION(&i), -1, OAC_AUTO,
     OAC_APPLICATION_AUDIO, OAC_APPLICATION_RESTRICTED_LOWDELAY,
     "    OAC_SET_APPLICATION ......................... OK.\n",
     "    OAC_GET_APPLICATION ......................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_BITRATE(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_encoder_ctl(enc, OAC_SET_BITRATE(1073741832)) != OAC_OK) test_failed();
    cfgs++;
    VG_UNDEF(&i, sizeof(i));
    if (oac_encoder_ctl(enc, OAC_GET_BITRATE(&i)) != OAC_OK) test_failed();
    if (i > 1700000 || i < 256000) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_BITRATE(i), OAC_GET_BITRATE(&i), -12345, 0,
     500, 256000,
     "    OAC_SET_BITRATE ............................. OK.\n",
     "    OAC_GET_BITRATE ............................. OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_FORCE_CHANNELS(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_FORCE_CHANNELS(i), OAC_GET_FORCE_CHANNELS(&i), -1, 3,
     1, OAC_AUTO,
     "    OAC_SET_FORCE_CHANNELS ...................... OK.\n",
     "    OAC_GET_FORCE_CHANNELS ...................... OK.\n")

    i = -2;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) == OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_FULLBAND + 1;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) == OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_NARROWBAND;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_FULLBAND;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_WIDEBAND;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_MEDIUMBAND;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_SET_BANDWIDTH ........................... OK.\n");
    /*We don't test if the bandwidth has actually changed.
       because the change may be delayed until the encoder is advanced.*/
    i = -12345;
    VG_UNDEF(&i, sizeof(i));
    err = oac_encoder_ctl(enc, OAC_GET_BANDWIDTH(&i));
    if (err != OAC_OK || (i != OAC_BANDWIDTH_NARROWBAND
                          && i != OAC_BANDWIDTH_MEDIUMBAND && i != OAC_BANDWIDTH_WIDEBAND
                          && i != OAC_BANDWIDTH_FULLBAND && i != OAC_AUTO)) test_failed();
    cfgs++;
    if (oac_encoder_ctl(enc, OAC_SET_BANDWIDTH(OAC_AUTO)) != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_BANDWIDTH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_BANDWIDTH ........................... OK.\n");

    i = -2;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) == OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_FULLBAND + 1;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) == OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_NARROWBAND;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_FULLBAND;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_WIDEBAND;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    i = OAC_BANDWIDTH_MEDIUMBAND;
    if (oac_encoder_ctl(enc, OAC_SET_MAX_BANDWIDTH(i)) != OAC_OK) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_SET_MAX_BANDWIDTH ....................... OK.\n");
    /*We don't test if the bandwidth has actually changed.
       because the change may be delayed until the encoder is advanced.*/
    i = -12345;
    VG_UNDEF(&i, sizeof(i));
    err = oac_encoder_ctl(enc, OAC_GET_MAX_BANDWIDTH(&i));
    if (err != OAC_OK || (i != OAC_BANDWIDTH_NARROWBAND
                          && i != OAC_BANDWIDTH_MEDIUMBAND && i != OAC_BANDWIDTH_WIDEBAND
                          && i != OAC_BANDWIDTH_FULLBAND)) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_MAX_BANDWIDTH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_MAX_BANDWIDTH ....................... OK.\n");

    err = oac_encoder_ctl(enc, OAC_GET_DTX(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_DTX(i), OAC_GET_DTX(&i), -1, 2,
     1, 0,
     "    OAC_SET_DTX ................................. OK.\n",
     "    OAC_GET_DTX ................................. OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_COMPLEXITY(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_COMPLEXITY(i), OAC_GET_COMPLEXITY(&i), -1, 11,
     0, 10,
     "    OAC_SET_COMPLEXITY .......................... OK.\n",
     "    OAC_GET_COMPLEXITY .......................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_INBAND_FEC(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_INBAND_FEC(i), OAC_GET_INBAND_FEC(&i), -1, 3,
     1, 0,
     "    OAC_SET_INBAND_FEC .......................... OK.\n",
     "    OAC_GET_INBAND_FEC .......................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_PACKET_LOSS_PERC(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_PACKET_LOSS_PERC(i), OAC_GET_PACKET_LOSS_PERC(&i), -1, 101,
     100, 0,
     "    OAC_SET_PACKET_LOSS_PERC .................... OK.\n",
     "    OAC_GET_PACKET_LOSS_PERC .................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_VBR(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_VBR(i), OAC_GET_VBR(&i), -1, 2,
     1, 0,
     "    OAC_SET_VBR ................................. OK.\n",
     "    OAC_GET_VBR ................................. OK.\n")

/*   err=oac_encoder_ctl(enc,OAC_GET_VOICE_RATIO(null_int_ptr));
   if(err!=OAC_BAD_ARG)test_failed();
   cfgs++;
   CHECK_SETGET(OAC_SET_VOICE_RATIO(i),OAC_GET_VOICE_RATIO(&i),-2,101,
     0,50,
     "    OAC_SET_VOICE_RATIO ......................... OK.\n",
     "    OAC_GET_VOICE_RATIO ......................... OK.\n")*/

    err = oac_encoder_ctl(enc, OAC_GET_VBR_CONSTRAINT(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_VBR_CONSTRAINT(i), OAC_GET_VBR_CONSTRAINT(&i), -1, 2,
     1, 0,
     "    OAC_SET_VBR_CONSTRAINT ...................... OK.\n",
     "    OAC_GET_VBR_CONSTRAINT ...................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_SIGNAL(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_SIGNAL(i), OAC_GET_SIGNAL(&i), -12345, 0x7FFFFFFF,
     OAC_SIGNAL_MUSIC, OAC_AUTO,
     "    OAC_SET_SIGNAL .............................. OK.\n",
     "    OAC_GET_SIGNAL .............................. OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_LSB_DEPTH(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_LSB_DEPTH(i), OAC_GET_LSB_DEPTH(&i), 7, 25, 16, 24,
     "    OAC_SET_LSB_DEPTH ........................... OK.\n",
     "    OAC_GET_LSB_DEPTH ........................... OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_PREDICTION_DISABLED(&i));
    if (i != 0) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_GET_PREDICTION_DISABLED(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_PREDICTION_DISABLED(i), OAC_GET_PREDICTION_DISABLED(&i), -1, 2, 1, 0,
     "    OAC_SET_PREDICTION_DISABLED ................. OK.\n",
     "    OAC_GET_PREDICTION_DISABLED ................. OK.\n")

    err = oac_encoder_ctl(enc, OAC_GET_EXPERT_FRAME_DURATION(null_int_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_2_5_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_5_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_10_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_20_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_40_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_60_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_80_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    /* 100 ms is no longer a representable packet duration. */
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(5009));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    err = oac_encoder_ctl(enc, OAC_SET_EXPERT_FRAME_DURATION(OAC_FRAMESIZE_120_MS));
    if (err != OAC_OK) test_failed();
    cfgs++;
    CHECK_SETGET(OAC_SET_EXPERT_FRAME_DURATION(i), OAC_GET_EXPERT_FRAME_DURATION(&i), 0, -1,
         OAC_FRAMESIZE_60_MS, OAC_FRAMESIZE_ARG,
     "    OAC_SET_EXPERT_FRAME_DURATION ............... OK.\n",
     "    OAC_GET_EXPERT_FRAME_DURATION ............... OK.\n")

    /*OAC_SET_FORCE_MODE is not tested here because it's not a public API, however the encoder tests use it*/

    err = oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(null_uint_ptr));
    if (err != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&enc_final_range)) != OAC_OK) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_GET_FINAL_RANGE ......................... OK.\n");

    /*Reset the encoder*/
    if (oac_encoder_ctl(enc, OAC_RESET_STATE) != OAC_OK) test_failed();
    cfgs++;
    fprintf(stdout, "    OAC_RESET_STATE ............................. OK.\n");

    memset(sbuf, 0, sizeof(short)*2*960);
    VG_UNDEF(packet, sizeof(packet));
    i = oac_encode(enc, sbuf, 960, packet, sizeof(packet));
    if (i < 1 || (i > (oac_int32)sizeof(packet))) test_failed();
    VG_CHECK(packet, i);
    cfgs++;
    fprintf(stdout, "    oac_encode() ................................ OK.\n");
#ifndef DISABLE_FLOAT_API
    memset(fbuf, 0, sizeof(float)*2*960);
    VG_UNDEF(packet, sizeof(packet));
    i = oac_encode_float(enc, fbuf, 960, packet, sizeof(packet));
    if (i < 1 || (i > (oac_int32)sizeof(packet))) test_failed();
    VG_CHECK(packet, i);
    cfgs++;
    fprintf(stdout, "    oac_encode_float() .......................... OK.\n");
#endif

#if 0
    /*These tests are disabled because the library crashes with null states*/
    if (oac_encoder_ctl(0, OAC_RESET_STATE)               != OAC_INVALID_STATE)test_failed();
    if (oac_encoder_init(0, 48000, 1, OAC_FORMAT_STANDARD, OAC_APPLICATION_VOIP) != OAC_INVALID_STATE) test_failed();
    if (oac_encode(0, sbuf, 960, packet, sizeof(packet))      != OAC_INVALID_STATE)test_failed();
    if (oac_encode_float(0, fbuf, 960, packet, sizeof(packet)) != OAC_INVALID_STATE) test_failed();
#endif
    oac_encoder_destroy(enc);
    cfgs++;
    fprintf(stdout, "                   All encoder interface tests passed\n");
    fprintf(stdout, "                             (%d API invocations)\n", cfgs);
    return cfgs;
}

/* Largest input payload the repacketizer test feeds in. */
#define TEST_REPACK_MAX 1350
/* Worst case output: OAC_MAX_FRAMES_PER_PACKET frames, each of which may need
   a three-byte length code, plus the TOC and frame count bytes. */
#define max_out ((TEST_REPACK_MAX + 3)*OAC_MAX_FRAMES_PER_PACKET + 2)
int test_repacketizer_api(void) {
    int ret, cfgs, i, j, k;
    OacRepacketizer *rp;
    unsigned char *packet;
    unsigned char *po;
    cfgs = 0;
    fprintf(stdout, "\n  Repacketizer tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");

    packet = (unsigned char *)malloc(max_out);
    if (packet == NULL) test_failed();
    memset(packet, 0, max_out);
    po = (unsigned char *)malloc(max_out + 256);
    if (po == NULL) test_failed();

    i = oac_repacketizer_get_size();
    if (i <= 0) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_repacketizer_get_size()=%d ............. OK.\n", i);

    rp = (OacRepacketizer*)malloc(i);
    rp = oac_repacketizer_init(rp);
    if (rp == NULL) test_failed();
    cfgs++;
    free(rp);
    fprintf(stdout, "    oac_repacketizer_init ....................... OK.\n");

    rp = oac_repacketizer_create();
    if (rp == NULL) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_repacketizer_create ..................... OK.\n");

    if (oac_repacketizer_get_nb_frames(rp) != 0) test_failed();
    cfgs++;
    fprintf(stdout, "    oac_repacketizer_get_nb_frames .............. OK.\n");

    /*Packets the repacketizer must refuse to take in*/
    VG_UNDEF(packet, 4);
    if (oac_repacketizer_cat(rp, packet, 0) != OAC_INVALID_PACKET) test_failed(); /* Zero len */
    cfgs++;
    packet[0] = 0x02;
    if (oac_repacketizer_cat(rp, packet, 1) != OAC_INVALID_PACKET) test_failed(); /* No extended ToC byte */
    cfgs++;
    packet[0] = (2<<3)|0x02;
    packet[1] = 1<<4;
    if (oac_repacketizer_cat(rp, packet, 100) != OAC_INVALID_PACKET) test_failed(); /* 40 ms stepped once is 1.5 frames */
    cfgs++;
    packet[0] = (31<<3)|0x02;
    packet[1] = 7<<4;
    if (oac_repacketizer_cat(rp, packet, 100) != OAC_INVALID_PACKET) test_failed(); /* 20 ms stepped seven times runs off the list */
    cfgs++;
    packet[0] = 0x02;
    packet[1] = 1<<4;
    if (oac_repacketizer_cat(rp, packet, 5) != OAC_INVALID_PACKET) test_failed(); /* CBR payload not a multiple of the count */
    cfgs++;
    packet[0] = 0x01;
    if (oac_repacketizer_cat(rp, packet, 1) != OAC_INVALID_PACKET) test_failed(); /* No padding length byte */
    cfgs++;
    packet[0] = 0;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = 1<<2;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_INVALID_PACKET) test_failed(); /* Change in TOC */
    cfgs++;

    /*CBR in, CBR out, over every ToC and every representable frame count*/
    oac_repacketizer_init(rp);
    for (j = 0; j < 32; j++) {
        /* TOC types, test half with stereo */
        int maxi;
        packet[0] = ((j<<1) + (j&1))<<2;
        maxi = 960/oac_packet_get_samples_per_frame(packet, 1, 8000);
        for (i = 1; i <= maxi; i++) {
            /* Number of CBR frames in the input packets */
            int maxp, in_toc, F;
            F = ref_F_for_count(j, i);
            /* Not every count is representable from every frame size: a 60 ms
               packet cannot be built out of 40 ms frames, and so on. */
            if (F < 0) continue;
            in_toc = i > 1 ? 2 : 1;
            packet[0] = (j<<3) | ((j&1)<<2) | (i > 1 ? 0x02 : 0);
            packet[1] = F<<4;
            maxp = 960/(i*oac_packet_get_samples_per_frame(packet, 2, 8000));
            for (k = 0; k <= TEST_REPACK_MAX; k += 3) {
                /*Payload size*/
                oac_int32 cnt, rcnt;
                if (k%i != 0) continue; /* Only testing CBR here, payload must be a multiple of the count */
                for (cnt = 0; cnt < maxp + 2; cnt++) {
                    if (cnt > 0) {
                        ret = oac_repacketizer_cat(rp, packet, k + in_toc);
                        /* Only the 120 ms limit can reject now: there is no
                           longer a per-frame byte limit at this scale. */
                        if ((cnt <= maxp)?ret != OAC_OK:ret != OAC_INVALID_PACKET) test_failed();
                        cfgs++;
                    }
                    rcnt = cnt < maxp?cnt:maxp;
                    if (oac_repacketizer_get_nb_frames(rp) != rcnt*i) test_failed();
                    cfgs++;
                    ret = oac_repacketizer_out_range(rp, 0, rcnt*i, po, max_out);
                    if (rcnt > 0) {
                        int len, out_toc, out_F;
                        out_F = ref_F_for_count(j, rcnt*i);
                        if (out_F < 0) {
                            /* Holding these frames is fine, emitting them in one
                               packet is not: the restriction is on out_range. */
                            if (ret != OAC_BAD_ARG) test_failed();
                            cfgs++;
                            continue;
                        }
                        out_toc = (rcnt*i) > 1 ? 2 : 1;
                        len = k*rcnt + out_toc;
                        if (ret != len) test_failed();
                        if (out_toc == 1) {
                            if ((po[0]&0x02) != 0) test_failed();                 /* No extended ToC */
                        } else {
                            if ((po[0]&0x02) == 0) test_failed();                 /* Extended ToC */
                            if (((po[1]>>4)&0x07) != out_F) test_failed();        /* Frame count */
                            if ((po[1]&0x80) != 0) test_failed();                 /* CBR */
                        }
                        cfgs++;
                        if (oac_repacketizer_out(rp, po, len) != len) test_failed();
                        cfgs++;
                        if (oac_packet_unpad(po, len) != len) test_failed();
                        cfgs++;
                        if (oac_packet_pad(po, len, len + 1) != OAC_OK) test_failed();
                        cfgs++;
                        if (oac_packet_pad(po, len + 1, len + 256) != OAC_OK) test_failed();
                        cfgs++;
                        if (oac_packet_unpad(po, len + 256) != len) test_failed();
                        cfgs++;
                        if (oac_multistream_packet_unpad(po, len, 1) != len) test_failed();
                        cfgs++;
                        if (oac_multistream_packet_pad(po, len, len + 1, 1) != OAC_OK) test_failed();
                        cfgs++;
                        if (oac_multistream_packet_pad(po, len + 1, len + 256, 1) != OAC_OK) test_failed();
                        cfgs++;
                        if (oac_multistream_packet_unpad(po, len + 256, 1) != len) test_failed();
                        cfgs++;
                        if (oac_repacketizer_out(rp, po, len - 1) != OAC_BUFFER_TOO_SMALL) test_failed();
                        cfgs++;
                        if (len > 1) {
                            if (oac_repacketizer_out(rp, po, 1) != OAC_BUFFER_TOO_SMALL) test_failed();
                            cfgs++;
                        }
                        if (oac_repacketizer_out(rp, po, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
                        cfgs++;
                    } else if (ret != OAC_BAD_ARG) test_failed();                /* Empty range */
                }
                oac_repacketizer_init(rp);
            }
        }
    }

    /*Mixed frame counts in, CBR out. Config 1 is a 20 ms frame, and 20 ms is
      the only base duration from which a three-frame packet is representable.*/
    oac_repacketizer_init(rp);
    packet[0] = 1<<3;
    if (oac_repacketizer_cat(rp, packet, 5) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = (1<<3)|0x02;
    packet[1] = 1<<4;
    if (oac_repacketizer_cat(rp, packet, 10) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    /*Three 4-byte frames: two ToC bytes, CBR, F=2 for a 60 ms packet.*/
    if ((i != (2 + 12)) || ((po[0]&0x02) == 0) || (((po[1]>>4)&0x07) != 2)
        || ((po[1]&0x80) != 0)) test_failed();
    cfgs++;
    i = oac_repacketizer_out_range(rp, 0, 1, po, max_out);
    if (i != 5 || (po[0]&0x02) != 0) test_failed();
    cfgs++;
    i = oac_repacketizer_out_range(rp, 1, 2, po, max_out);
    if (i != 5 || (po[0]&0x02) != 0) test_failed();
    cfgs++;
    /*Four frames is representable from 20 ms, five is not.*/
    packet[0] = 1<<3;
    if (oac_repacketizer_cat(rp, packet, 5) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_out_range(rp, 0, 4, po, max_out) != 2 + 16) test_failed();
    cfgs++;
    if (oac_repacketizer_cat(rp, packet, 5) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_out_range(rp, 0, 5, po, max_out) != OAC_BAD_ARG) test_failed();
    cfgs++;

    /*Mixed frame counts in, VBR out*/
    oac_repacketizer_init(rp);
    packet[0] = (1<<3)|0x02;
    packet[1] = 1<<4;
    if (oac_repacketizer_cat(rp, packet, 10) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = 1<<3;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    /*Frames of 4, 4 and 2 bytes, so VBR with two explicit lengths.*/
    if ((i != (2 + 1 + 1 + 4 + 4 + 2)) || ((po[0]&0x02) == 0) || (((po[1]>>4)&0x07) != 2)
        || ((po[1]&0x80) == 0)) test_failed();
    cfgs++;

    /*VBR in, VBR out*/
    oac_repacketizer_init(rp);
    packet[0] = (1<<3)|0x02;
    packet[1] = 0x80|(1<<4);
    packet[2] = 4;
    if (oac_repacketizer_cat(rp, packet, 8) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_cat(rp, packet, 8) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    /*Frames of 4, 1, 4 and 1 bytes: F=3 for an 80 ms packet.*/
    if ((i != (2 + 1 + 1 + 1 + 4 + 1 + 4 + 1)) || (((po[1]>>4)&0x07) != 3)
        || ((po[1]&0x80) == 0)) test_failed();
    cfgs++;

    /*VBR in, CBR out*/
    oac_repacketizer_init(rp);
    packet[0] = (1<<3)|0x02;
    packet[1] = 0x80|(1<<4);
    packet[2] = 4;
    if (oac_repacketizer_cat(rp, packet, 11) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_cat(rp, packet, 11) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    /*All four frames are 4 bytes, so the explicit lengths go away again.*/
    if ((i != (2 + 16)) || (((po[1]>>4)&0x07) != 3) || ((po[1]&0x80) != 0)) test_failed();
    cfgs++;

    /*Single frames of every size in, VBR out*/
    for (j = 0; j < 32; j++) {
        /* TOC types, test half with stereo */
        int maxi, sum, rcnt;
        packet[0] = ((j<<1) + (j&1))<<2;
        maxi = 960/oac_packet_get_samples_per_frame(packet, 1, 8000);
        sum = 0;
        rcnt = 0;
        oac_repacketizer_init(rp);
        for (i = 1; i <= maxi + 2; i++) {
            int len, out_toc, out_F;
            ret = oac_repacketizer_cat(rp, packet, i);
            if (rcnt < maxi) {
                if (ret != OAC_OK) test_failed();
                rcnt++;
                sum += i - 1;
            } else if (ret != OAC_INVALID_PACKET) test_failed();
            cfgs++;
            out_F = ref_F_for_count(j, rcnt);
            if (out_F < 0) {
                if (oac_repacketizer_out(rp, po, max_out) != OAC_BAD_ARG) test_failed();
                cfgs++;
                continue;
            }
            /*Every frame has a different size, so the output is always VBR and
              carries an explicit length for all but the last frame.*/
            out_toc = rcnt > 1 ? 2 : 1;
            len = sum + out_toc + (rcnt > 1 ? rcnt - 1 : 0);
            if (oac_repacketizer_out(rp, po, max_out) != len) test_failed();
            if (out_toc == 1) {
                if ((po[0]&0x02) != 0) test_failed();
            } else {
                if ((po[0]&0x02) == 0) test_failed();
                if (((po[1]>>4)&0x07) != out_F) test_failed();
                if ((po[1]&0x80) == 0) test_failed();
            }
            cfgs++;
            if (oac_repacketizer_out(rp, po, len) != len) test_failed();
            cfgs++;
            if (oac_packet_unpad(po, len) != len) test_failed();
            cfgs++;
            if (oac_packet_pad(po, len, len + 1) != OAC_OK) test_failed();
            cfgs++;
            if (oac_packet_pad(po, len + 1, len + 256) != OAC_OK) test_failed();
            cfgs++;
            if (oac_packet_unpad(po, len + 256) != len) test_failed();
            cfgs++;
            if (oac_multistream_packet_unpad(po, len, 1) != len) test_failed();
            cfgs++;
            if (oac_multistream_packet_pad(po, len, len + 1, 1) != OAC_OK) test_failed();
            cfgs++;
            if (oac_multistream_packet_pad(po, len + 1, len + 256, 1) != OAC_OK) test_failed();
            cfgs++;
            if (oac_multistream_packet_unpad(po, len + 256, 1) != len) test_failed();
            cfgs++;
            if (oac_repacketizer_out(rp, po, len - 1) != OAC_BUFFER_TOO_SMALL) test_failed();
            cfgs++;
            if (len > 1) {
                if (oac_repacketizer_out(rp, po, 1) != OAC_BUFFER_TOO_SMALL) test_failed();
                cfgs++;
            }
            if (oac_repacketizer_out(rp, po, 0) != OAC_BUFFER_TOO_SMALL) test_failed();
            cfgs++;
        }
    }

    po[0] = 'O';
    po[1] = 'p';
    if (oac_packet_pad(po, 4, 4) != OAC_OK) test_failed();
    cfgs++;
    if (oac_multistream_packet_pad(po, 4, 4, 1) != OAC_OK) test_failed();
    cfgs++;
    if (oac_packet_pad(po, 4, 5) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    if (oac_multistream_packet_pad(po, 4, 5, 1) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    if (oac_packet_pad(po, 0, 5) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_multistream_packet_pad(po, 0, 5, 1) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_packet_unpad(po, 0) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_multistream_packet_unpad(po, 0, 1) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_packet_unpad(po, 4) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    if (oac_multistream_packet_unpad(po, 4, 1) != OAC_INVALID_PACKET) test_failed();
    cfgs++;
    po[0] = 0;
    po[1] = 0;
    po[2] = 0;
    if (oac_packet_pad(po, 5, 4) != OAC_BAD_ARG) test_failed();
    cfgs++;
    if (oac_multistream_packet_pad(po, 5, 4, 1) != OAC_BAD_ARG) test_failed();
    cfgs++;

    fprintf(stdout, "    oac_repacketizer_cat ........................ OK.\n");
    fprintf(stdout, "    oac_repacketizer_out ........................ OK.\n");
    fprintf(stdout, "    oac_repacketizer_out_range .................. OK.\n");
    fprintf(stdout, "    oac_packet_pad .............................. OK.\n");
    fprintf(stdout, "    oac_packet_unpad ............................ OK.\n");
    fprintf(stdout, "    oac_multistream_packet_pad .................. OK.\n");
    fprintf(stdout, "    oac_multistream_packet_unpad ................ OK.\n");

    oac_repacketizer_destroy(rp);
    cfgs++;
    free(packet);
    free(po);
    fprintf(stdout, "                        All repacketizer tests passed\n");
    fprintf(stdout, "                            (%7d API invocations)\n", cfgs);

    return cfgs;
}

#ifdef MALLOC_FAIL
/* GLIBC 2.14 declares __malloc_hook as deprecated, generating a warning
 * under GCC. However, this is the cleanest way to test malloc failure
 * handling in our codebase, and the lack of thread safety isn't an
 * issue here. We therefore disable the warning for this function.
 */
# if OAC_GNUC_PREREQ(4, 6)
/* Save the current warning settings */
#  pragma GCC diagnostic push
# endif
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"

typedef void *(*mhook)(size_t __size, __const void *);
#endif

int test_malloc_fail(void) {
#ifdef MALLOC_FAIL
    OacDecoder *dec;
    OacEncoder *enc;
    OacRepacketizer *rp;
    unsigned char mapping[256] = {0, 1};
    OacMSDecoder *msdec;
    OacMSEncoder *msenc;
    int rate, c, app, cfgs, err, useerr;
    int *ep;
    mhook orig_malloc;
    cfgs = 0;
#endif
    fprintf(stdout, "\n  malloc() failure tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
#ifdef MALLOC_FAIL
    orig_malloc = __malloc_hook;
    __malloc_hook = malloc_hook;
    ep = (int *)oac_alloc(sizeof(int));
    if (ep != NULL) {
        if (ep) free(ep);
        __malloc_hook = orig_malloc;
#endif
    fprintf(stdout, "    oac_decoder_create() ................... SKIPPED.\n");
    fprintf(stdout, "    oac_encoder_create() ................... SKIPPED.\n");
    fprintf(stdout, "    oac_repacketizer_create() .............. SKIPPED.\n");
    fprintf(stdout, "    oac_multistream_decoder_create() ....... SKIPPED.\n");
    fprintf(stdout, "    oac_multistream_encoder_create() ....... SKIPPED.\n");
    fprintf(stdout, "(Test only supported with GLIBC and without valgrind)\n");
    return 0;
#ifdef MALLOC_FAIL
}
for (useerr = 0; useerr < 2; useerr++) {
    ep = useerr?&err:0;
    for (rate = 0; rate < 5; rate++) {
        for (c = 1; c < 3; c++) {
            err = 1;
            if (useerr) {
                VG_UNDEF(&err, sizeof(err));
            }
            dec = oac_decoder_create(oac_rates[rate], c, OAC_FORMAT_STANDARD, ep);
            if (dec != NULL || (useerr && err != OAC_ALLOC_FAIL)) {
                __malloc_hook = orig_malloc;
                test_failed();
            }
            cfgs++;
            msdec = oac_multistream_decoder_create(oac_rates[rate], c, 1, c - 1, mapping, ep);
            if (msdec != NULL || (useerr && err != OAC_ALLOC_FAIL)) {
                __malloc_hook = orig_malloc;
                test_failed();
            }
            cfgs++;
            for (app = 0; app < 3; app++) {
                if (useerr) {
                    VG_UNDEF(&err, sizeof(err));
                }
                enc = oac_encoder_create(oac_rates[rate], c, OAC_FORMAT_STANDARD, oac_apps[app], ep);
                if (enc != NULL || (useerr && err != OAC_ALLOC_FAIL)) {
                    __malloc_hook = orig_malloc;
                    test_failed();
                }
                cfgs++;
                msenc = oac_multistream_encoder_create(oac_rates[rate], c, 1, c - 1, mapping, oac_apps[app], ep);
                if (msenc != NULL || (useerr && err != OAC_ALLOC_FAIL)) {
                    __malloc_hook = orig_malloc;
                    test_failed();
                }
                cfgs++;
            }
        }
    }
}
rp = oac_repacketizer_create();
if (rp != NULL) {
    __malloc_hook = orig_malloc;
    test_failed();
}
cfgs++;
__malloc_hook = orig_malloc;
fprintf(stdout, "    oac_decoder_create() ........................ OK.\n");
fprintf(stdout, "    oac_encoder_create() ........................ OK.\n");
fprintf(stdout, "    oac_repacketizer_create() ................... OK.\n");
fprintf(stdout, "    oac_multistream_decoder_create() ............ OK.\n");
fprintf(stdout, "    oac_multistream_encoder_create() ............ OK.\n");
fprintf(stdout, "                      All malloc failure tests passed\n");
fprintf(stdout, "                                 (%2d API invocations)\n", cfgs);
return cfgs;
#endif
}

#ifdef MALLOC_FAIL
# if __GNUC_PREREQ(4, 6)
#  pragma GCC diagnostic pop /* restore -Wdeprecated-declarations */
# endif
#endif

/* Exhaustively checks the frame length code. The reference encoder above is
   written from the specification, so this validates the library's parser
   against an independent implementation rather than against itself. */
oac_int32 test_frame_length_code(void) {
    oac_int32 s, cfgs;
    unsigned char *pkt;
    unsigned char toc;
    const unsigned char *frames[OAC_MAX_FRAMES_PER_PACKET];
    oac_int32 size[OAC_MAX_FRAMES_PER_PACKET];
    int payload_offset, nb, ret;

    cfgs = 0;
    fprintf(stdout, "\n  Frame length code tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");

    /* The parser never reads the payload, so a single buffer large enough for
       the longest packet we claim to have is enough for every size. */
    pkt = (unsigned char *)calloc((size_t)OAC_SIZE_MAX + 8, 1);
    if (pkt == NULL) test_failed();

    /* Every size in [0, OAC_SIZE_MAX] must survive a reference-encode followed
       by a library parse, and must be rejected when the packet is one byte
       short of what the length claims. */
    /* Two frames, VBR, so the length of the first one is explicit. Config 31 is
       a 20 ms CELT frame and F=1 steps it up to a 40 ms two-frame packet. */
    pkt[0] = (31<<3)|(1<<2)|0x02;
    pkt[1] = 0x80|(1<<4);
    for (s = 0; s <= OAC_SIZE_MAX; s++) {
        nb = ref_put_size(&pkt[2], s);
        if (nb != (s < 192 ? 1 : (s < 8384 ? 2 : 3))) test_failed();
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(pkt, 2 + nb + s, &toc, frames, size, &payload_offset);
        if (ret != 2) test_failed();
        if (size[0] != s || size[1] != 0) test_failed();
        if (frames[0] != pkt + 2 + nb) test_failed();
        if (frames[1] != frames[0] + s) test_failed();
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(pkt, 2 + nb + s - 1, &toc, frames, size, &payload_offset);
        if (ret != OAC_INVALID_PACKET) test_failed();
        cfgs += 2;
    }
    fprintf(stdout, "    exhaustive parse 0..%d ................. OK.\n", OAC_SIZE_MAX);

    /* Now the other direction: the length bytes the library writes must match
       the reference at every tier boundary. The repacketizer is the only public
       way to make it emit an explicit length. */
    {
        const oac_int32 boundaries[9] = {0, 191, 192, 223, 224, 8383, 8384, 100000, OAC_SIZE_MAX};
        unsigned char *out;
        unsigned char ref[3];
        OacRepacketizer *rp;
        oac_int32 out_len;
        int i;

        out = (unsigned char *)calloc((size_t)OAC_SIZE_MAX + 16, 1);
        if (out == NULL) test_failed();
        rp = oac_repacketizer_create();
        if (rp == NULL) test_failed();

        for (i = 0; i < 9; i++) {
            s = boundaries[i];
            /* Two single-frame packets of different sizes, so the output is VBR
               and has to signal the length of the first one explicitly. A
               two-frame packet always carries the extended ToC byte, so the
               length field starts at offset 2. The same buffer backs both
               frames; the repacketizer only keeps pointers into it. */
            pkt[0] = (31<<3)|(1<<2);
            oac_repacketizer_init(rp);
            if (oac_repacketizer_cat(rp, pkt, 1 + s) != OAC_OK) test_failed();
            if (oac_repacketizer_cat(rp, pkt, 1 + 1) != OAC_OK) test_failed();
            out_len = oac_repacketizer_out(rp, out, OAC_SIZE_MAX + 16);
            nb = ref_put_size(ref, s);
            if (out_len != 2 + nb + s + 1) test_failed();
            if (memcmp(out + 2, ref, nb) != 0) test_failed();
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(out, out_len, &toc, frames, size, &payload_offset);
            if (ret != 2) test_failed();
            if (size[0] != s || size[1] != 1) test_failed();
            cfgs += 4;
        }
        oac_repacketizer_destroy(rp);
        free(out);
    }
    fprintf(stdout, "    length bytes written at tier boundaries ..... OK.\n");

    /* Round trip real payloads of assorted sizes through the repacketizer, in
       both directions and across all three length tiers. Two equal frames are
       re-coded as code 1, which drops the explicit length; anything else keeps
       code 2. Getting that accounting wrong is what would overrun a caller's
       buffer. */
    {
        const oac_int32 sizes[8] = {1, 50, 191, 192, 300, 8383, 8384, 20000};
        const oac_int32 cap = 2*20000 + 16;
        unsigned char *in;
        unsigned char *out;
        OacRepacketizer *rp;
        int i, j;

        in = (unsigned char *)malloc(cap);
        out = (unsigned char *)malloc(cap);
        if (in == NULL || out == NULL) test_failed();
        rp = oac_repacketizer_create();
        if (rp == NULL) test_failed();

        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
                oac_int32 s0 = sizes[i];
                oac_int32 s1 = sizes[j];
                oac_int32 k, len, expect;

                /* Build a two-frame VBR packet by hand: 20 ms CELT-only stereo,
                   with F=1 stepping the 20 ms frame up to a 40 ms packet. */
                in[0] = (31<<3)|(1<<2)|0x02;
                in[1] = 0x80|(1<<4);
                len = 2;
                len += ref_put_size(in + len, s0);
                for (k = 0; k < s0; k++) in[len + k] = (unsigned char)(k + i);
                len += s0;
                for (k = 0; k < s1; k++) in[len + k] = (unsigned char)(k + j + 7);
                len += s1;

                if (oac_repacketizer_init(rp) == NULL) test_failed();
                if (oac_repacketizer_cat(rp, in, len) != OAC_OK) test_failed();
                if (oac_repacketizer_get_nb_frames(rp) != 2) test_failed();
                ret = oac_repacketizer_out(rp, out, cap);
                expect = (s0 == s1) ? 2 + s0 + s1 : 2 + ref_size_bytes(s0) + s0 + s1;
                if (ret != expect) test_failed();

                UNDEFINE_FOR_PARSE
                    if (oac_packet_parse(out, ret, &toc, frames, size, &payload_offset) != 2) test_failed();
                if (size[0] != s0 || size[1] != s1) test_failed();
                if (memcmp(frames[0], in + 2 + ref_size_bytes(s0), s0) != 0) test_failed();
                if (memcmp(frames[1], in + len - s1, s1) != 0) test_failed();

                /* Splitting back into single frames must also work. */
                if (oac_repacketizer_out_range(rp, 0, 1, out, cap) != 1 + s0) test_failed();
                if (oac_repacketizer_out_range(rp, 1, 2, out, cap) != 1 + s1) test_failed();
                cfgs += 6;
            }
        }
        oac_repacketizer_destroy(rp);
        free(in);
        free(out);
    }
    fprintf(stdout, "    repacketizer round trip across tiers ........ OK.\n");


    free(pkt);
    fprintf(stdout, "                    All frame length code tests passed\n");
    fprintf(stdout, "                          (%d API invocations)\n", cfgs);
    return cfgs;
}



/* Room kept aside for the reference bitstream of one configuration. */
#define TEST_REF_FRAMES 6
#define TEST_REF_FRAME_BYTES 65536

/* Encoding the same audio at the same bitrate must give the exact same
   bitstream no matter how much spare room the caller left in the output
   buffer. Before the length signalling change, max_data_bytes could never
   exceed 1276, so a number of derived rate computations silently assumed a
   small value; with big buffers they overflow and quietly change the coding
   decisions (bandwidth collapse, redundancy turning itself off, ...).
   Comparing whole packets rather than just the length catches all of those. */
oac_int32 test_encoder_buffer_independence(void) {
    const int fsz[4] = {120, 480, 960, 2880};
    const oac_int32 rates[3] = {24000, 64000, 256000};
    const oac_int32 bufs[3] = {8000, 100000, 4000000};
    const int apps[2] = {OAC_APPLICATION_AUDIO, OAC_APPLICATION_VOIP};
    /* Sweeping the bitrate walks the encoder through SILK/hybrid/CELT and the
       mode-switching redundancy, which a steady-state encode never reaches. */
    const oac_int32 sweep[TEST_REF_FRAMES] = {12000, 40000, 128000, 12000, 96000, 16000};
    oac_int32 cfgs;
    int fi, ri, ai, ch, bi, f, i, pass;
    unsigned int seed;
    short *pcm;
    short *out;
    unsigned char *ref;
    int ref_len[TEST_REF_FRAMES];

    cfgs = 0;
    fprintf(stdout, "\n  Encoder output buffer independence tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");

    pcm = (short *)malloc(sizeof(short)*2880*2);
    out = (short *)malloc(sizeof(short)*5760*2);
    ref = (unsigned char *)malloc((size_t)TEST_REF_FRAMES*TEST_REF_FRAME_BYTES);
    if (pcm == NULL || out == NULL || ref == NULL) test_failed();

    /* pass 0: steady state at a fixed bitrate. pass 1: bitrate sweep, which
       walks through SILK/hybrid/CELT and the mode-switch redundancy. pass 2:
       same sweep with FEC on. FEC has to be a separate pass rather than part of
       pass 1, because it biases the encoder towards SILK hard enough that the
       mode transitions never happen. */
    for (pass = 0; pass < 3; pass++)
    for (fi = 0; fi < 4; fi++)
    for (ri = 0; ri < 3; ri++)
    for (ai = 0; ai < 2; ai++)
    for (ch = 1; ch <= 2; ch++) {
        if (pass > 0 && (fi != 2 || ri != 0)) continue; /* sweep only needs 20 ms */
        for (bi = 0; bi < 3; bi++) {
            OacEncoder *enc;
            OacDecoder *dec;
            unsigned char *data;
            int err;

            /* --enable-fuzzing makes the encoder take random coding decisions,
               and oac_select_arch() randomly downgrades the SIMD path on every
               create, so the three runs would diverge for reasons that have
               nothing to do with the output buffer. Restarting the generator
               from the same seed before each run gives all three the same
               sequence of random decisions, so any remaining difference really
               is caused by the buffer size. Builds that never call rand() are
               unaffected. */
            srand((unsigned)((((pass*4 + fi)*3 + ri)*2 + ai)*2 + ch));

            enc = oac_encoder_create(48000, ch, OAC_FORMAT_STANDARD, apps[ai], &err);
            if (err != OAC_OK || enc == NULL) test_failed();
            dec = oac_decoder_create(48000, ch, OAC_FORMAT_STANDARD, &err);
            if (err != OAC_OK || dec == NULL) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_BITRATE(rates[ri])) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_VBR(1)) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(3)) != OAC_OK) test_failed();
            if (pass == 2) {
                if (oac_encoder_ctl(enc, OAC_SET_INBAND_FEC(1)) != OAC_OK) test_failed();
                if (oac_encoder_ctl(enc, OAC_SET_PACKET_LOSS_PERC(20)) != OAC_OK) test_failed();
            }
            data = (unsigned char *)malloc(bufs[bi]);
            if (data == NULL) test_failed();
            seed = 1234u;
            for (f = 0; f < TEST_REF_FRAMES; f++) {
                oac_uint32 erange, drange;
                int dlen, len;
                for (i = 0; i < fsz[fi]*ch; i++) {
                    seed = 1664525u*seed + 1013904223u;
                    pcm[i] = (short)((int)(seed>>20) - 2048);
                }
                if (pass > 0 && oac_encoder_ctl(enc, OAC_SET_BITRATE(sweep[f])) != OAC_OK)
                    test_failed();
                len = oac_encode(enc, pcm, fsz[fi], data, bufs[bi]);
                if (len < 0 || len > bufs[bi]) test_failed();
                if (len > TEST_REF_FRAME_BYTES) test_failed();
                if (oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&erange)) != OAC_OK) test_failed();
                if (bi == 0) {
                    ref_len[f] = len;
                    memcpy(ref + (size_t)f*TEST_REF_FRAME_BYTES, data, len);
                } else {
                    /* The only difference between the runs is the buffer size. */
                    if (len != ref_len[f]) test_failed();
                    if (memcmp(ref + (size_t)f*TEST_REF_FRAME_BYTES, data, len) != 0) test_failed();
                }
                dlen = oac_decode(dec, data, len, out, 5760, 0);
                if (dlen != fsz[fi]) test_failed();
                if (oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&drange)) != OAC_OK) test_failed();
                if (erange != drange) test_failed();
                cfgs += 4;
            }
            free(data);
            oac_encoder_destroy(enc);
            oac_decoder_destroy(dec);
        }
    }
    free(pcm);
    free(out);
    free(ref);
    fprintf(stdout, "    encoder ignores spare output room ........... OK.\n");

    /* Frame sizes above 20 ms are coded as several 20 ms frames in one packet.
       That path sizes a scratch buffer on the stack; if it were sized from the
       caller's output buffer rather than from what the frame encoder can
       actually be asked for, simply offering a large buffer would crash the
       encoder. 9 MB is above the usual 8 MB stack limit, which is the point. */
    {
        /* 40, 60, 80 and 120 ms. There is no 100 ms: the packet duration has to
           be an integer number of frames of one of the eight legal sizes. */
        const int big_fsz[4] = {1920, 2880, 3840, 5760};
        const oac_int32 bufsize = 9000000;
        unsigned char *data;
        int vbr;

        pcm = (short *)malloc(sizeof(short)*5760*2);
        out = (short *)malloc(sizeof(short)*5760*2);
        data = (unsigned char *)malloc(bufsize);
        if (pcm == NULL || out == NULL || data == NULL) test_failed();
        for (fi = 0; fi < 4; fi++)
        for (ch = 1; ch <= 2; ch++)
        for (vbr = 0; vbr <= 1; vbr++) {
            OacEncoder *enc;
            OacDecoder *dec;
            int err;

            enc = oac_encoder_create(48000, ch, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
            if (err != OAC_OK || enc == NULL) test_failed();
            dec = oac_decoder_create(48000, ch, OAC_FORMAT_STANDARD, &err);
            if (err != OAC_OK || dec == NULL) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_BITRATE(96000)) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_VBR(vbr)) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(3)) != OAC_OK) test_failed();
            seed = 777u;
            for (f = 0; f < 2; f++) {
                oac_uint32 erange, drange;
                oac_int32 len;
                int dlen;
                for (i = 0; i < big_fsz[fi]*ch; i++) {
                    seed = 1664525u*seed + 1013904223u;
                    pcm[i] = (short)((int)(seed>>20) - 2048);
                }
                len = oac_encode(enc, pcm, big_fsz[fi], data, bufsize);
                if (len < 0 || len > bufsize) test_failed();
                if (oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&erange)) != OAC_OK) test_failed();
                dlen = oac_decode(dec, data, len, out, 5760, 0);
                if (dlen != big_fsz[fi]) test_failed();
                if (oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&drange)) != OAC_OK) test_failed();
                if (erange != drange) test_failed();
                cfgs += 4;
            }
            oac_encoder_destroy(enc);
            oac_decoder_destroy(dec);
        }
        free(pcm);
        free(out);
        free(data);
    }
    fprintf(stdout, "    large buffers do not blow the stack ......... OK.\n");

    /* An ambisonics packet always spends two bytes on the ToC where a mono or
       stereo packet spends one, and the frame encoder has to reserve exactly
       that much up front. Under-reserving shows up only as a write one byte
       past the limit the encoder was given, so encode into a buffer with a
       guard byte immediately after that limit. Also verify max_data_bytes =
       1..4 (buffer-too-small and low-bitrate PLC paths) in CBR and VBR, and
       verify that OAC_APPLICATION_RESTRICTED_SILK is rejected for all
       ambisonics orders (including order 0). */
    {
        int order;

        pcm = (short *)malloc(sizeof(short)*960*36);
        out = (short *)malloc(sizeof(short)*960*36);
        if (pcm == NULL || out == NULL) test_failed();
        for (order = 0; order <= 5; order++) {
            OacEncoder *enc;
            OacDecoder *dec;
            unsigned char *data;
            int err, channels, budget, cap;

            channels = (order + 1)*(order + 1);
            enc = oac_encoder_create(48000, channels, OAC_FORMAT_AMBISONICS,
                                     OAC_APPLICATION_RESTRICTED_SILK, &err);
            if (err != OAC_BAD_ARG || enc != NULL) test_failed();
            cfgs++;
            enc = oac_encoder_create(48000, channels, OAC_FORMAT_AMBISONICS,
                                     OAC_APPLICATION_AUDIO, &err);
            if (err != OAC_OK || enc == NULL) test_failed();
            dec = oac_decoder_create(48000, channels, OAC_FORMAT_AMBISONICS, &err);
            if (err != OAC_OK || dec == NULL) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_BITRATE(32000*channels)) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_VBR(0)) != OAC_OK) test_failed();
            if (oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(3)) != OAC_OK) test_failed();
            /* 20 ms at the requested bitrate, which is what CBR will aim for. */
            budget = 32000*channels/400;
            data = (unsigned char *)malloc(budget + 1);
            if (data == NULL) test_failed();
            seed = 4321u;
            for (f = 0; f < 2; f++) {
                oac_uint32 erange, drange;
                oac_int32 len;
                int dlen;
                for (i = 0; i < 960*channels; i++) {
                    seed = 1664525u*seed + 1013904223u;
                    pcm[i] = (short)((int)(seed>>20) - 2048);
                }
                data[budget] = 0xA5;
                len = oac_encode(enc, pcm, 960, data, budget);
                if (len < 0 || len > budget) test_failed();
                if (data[budget] != 0xA5) test_failed();
                if (oac_packet_get_format(data, len) != OAC_FORMAT_AMBISONICS) test_failed();
                if (oac_packet_get_nb_channels(data, len) != channels) test_failed();
                if (oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&erange)) != OAC_OK) test_failed();
                dlen = oac_decode(dec, data, len, out, 960, 0);
                if (dlen != 960) test_failed();
                if (oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&drange)) != OAC_OK) test_failed();
                if (erange != drange) test_failed();
                cfgs += 6;
            }
            /* Test tiny output buffers (max_data_bytes = 1..4) in both CBR and VBR */
            for (f = 0; f <= 1; f++) {
                if (oac_encoder_ctl(enc, OAC_SET_VBR(f)) != OAC_OK) test_failed();
                for (cap = 1; cap <= 4; cap++) {
                    oac_int32 len;
                    data[cap] = 0xA5;
                    len = oac_encode(enc, pcm, 960, data, cap);
                    if (data[cap] != 0xA5) test_failed();
                    if (cap < 2) {
                        if (len != OAC_BUFFER_TOO_SMALL) test_failed();
                    } else {
                        if (len < 2 || len > cap) test_failed();
                        if (oac_packet_get_format(data, len) != OAC_FORMAT_AMBISONICS) test_failed();
                        if (oac_packet_get_nb_channels(data, len) != channels) test_failed();
                        if (oac_decode(dec, data, len, out, 960, 0) != 960) test_failed();
                    }
                    cfgs += 3;
                }
            }
            free(data);
            oac_encoder_destroy(enc);
            oac_decoder_destroy(dec);
        }
        free(pcm);
        free(out);
    }
    fprintf(stdout, "    ambisonics ToC reserve ..................... OK.\n");

    fprintf(stdout, "              All encoder buffer independence tests passed\n");
    fprintf(stdout, "                          (%d API invocations)\n", cfgs);
    return cfgs;
}

int main(int _argc, char **_argv) {
    oac_int32 total;
    const char * oversion;
    if (_argc > 1) {
        fprintf(stderr, "Usage: %s\n", _argv[0]);
        return 1;
    }
    iseed = 0;

    oversion = oac_get_version_string();
    if (!oversion) test_failed();
    fprintf(stderr, "Testing the %s API deterministically\n", oversion);
    if (oac_strerror(-32768) == NULL) test_failed();
    if (oac_strerror(32767) == NULL) test_failed();
    if (strlen(oac_strerror(0)) < 1) test_failed();
    total = 4;

    total += test_dec_api();
    total += test_msdec_api();
    total += test_parse();
    total += test_frame_length_code();
    total += test_enc_api();
    total += test_encoder_buffer_independence();
    total += test_repacketizer_api();
    total += test_malloc_fail();

    fprintf(stderr, "\nAll API tests passed.\nThe liboac API was invoked %d times.\n", total);

    return 0;
}

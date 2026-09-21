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
    if (oac_decoder_get_size(256, OAC_FORMAT_AMBISONICS) <= 0) test_failed();
    if (oac_decoder_get_size(289, OAC_FORMAT_AMBISONICS) != 0) test_failed();
    cfgs += 2;

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
    packet[0] = (1<<3);
    if (oac_decode(dec, packet, 1, sbuf, 960, 0) != 960) test_failed();
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
    packet[0] = 2;
    if (oac_packet_get_nb_samples(packet, 1, 24000) != OAC_INVALID_PACKET) test_failed();
    packet[0] = (63<<2)|2;
    packet[1] = (7<<4);
    if (oac_packet_get_nb_samples(packet, 0, 24000) != OAC_BAD_ARG) test_failed();
    if (oac_packet_get_nb_samples(packet, 2, 48000) != OAC_INVALID_PACKET) test_failed();
    if (oac_decoder_get_nb_samples(dec, packet, 2) != OAC_INVALID_PACKET) test_failed();
    fprintf(stdout, "    oac_{packet,decoder}_get_nb_samples() ....... OK.\n");
    cfgs += 9;

    if (OAC_BAD_ARG != oac_packet_get_nb_frames(packet, 0)) test_failed();
    {
        static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
        for (i = 0; i < 256; i++) {
            int x_bit = (i >> 1) & 1;
            int s_bit = (i >> 2) & 1;
            int is_celt = (i & 0x80) != 0;
            int base_dur = oac_packet_get_samples_per_frame((const unsigned char *)&i, 48000) / 120;
            int base_idx = 0;
            while (base_idx < 8 && ref_dur[base_idx] != base_dur) base_idx++;
            packet[0] = (unsigned char)i;
            if ((x_bit ? OAC_INVALID_PACKET : 1) != oac_packet_get_nb_frames(packet, 1)) test_failed();
            cfgs++;
            for (j = 0; j < 256; j++) {
                int expect_frames;
                packet[1] = (unsigned char)j;
                if (!x_bit) {
                    expect_frames = 1;
                } else {
                    int f_inc = (j >> 4) & 7;
                    int a_bit = (j >> 3) & 1;
                    int c_val = j & 7;
                    int target_idx = base_idx + f_inc;
                    if (target_idx > 7 || (ref_dur[target_idx] % base_dur) != 0) {
                        expect_frames = OAC_INVALID_PACKET;
                    } else if (!a_bit && c_val == 7 && s_bit == 1) {
                        /* C=7, S=1 requires 3rd ToC byte (len >= 3) */
                        expect_frames = OAC_INVALID_PACKET;
                    } else if ((a_bit || c_val > 0) && !is_celt) {
                        /* Ambisonics and >2 channels require CELT mode */
                        expect_frames = OAC_INVALID_PACKET;
                    } else {
                        expect_frames = ref_dur[target_idx] / base_dur;
                    }
                }
                if (expect_frames != oac_packet_get_nb_frames(packet, 2)) test_failed();
                cfgs++;
            }
        }
    }
    fprintf(stdout, "    oac_packet_get_nb_frames() .................. OK.\n");

    for (i = 0; i < 256; i++) {
        int bw;
        packet[0] = i;
        bw = packet[0]>>4;
        bw = OAC_BANDWIDTH_NARROWBAND + (((((bw&7)*9)&(63 - (bw&8))) + 2 + 12*((bw&8) != 0))>>4);
        if (bw != oac_packet_get_bandwidth(packet)) test_failed();
        cfgs++;
    }
    fprintf(stdout, "    oac_packet_get_bandwidth() .................. OK.\n");

    for (i = 0; i < 256; i++) {
        int fp3s, rate;
        packet[0] = i;
        fp3s = packet[0]>>3;
        fp3s = ((((3 - (fp3s&3))*13&119) + 9)>>2)*((fp3s > 13)*(3 - ((fp3s&3) == 3)) + 1)*25;
        for (rate = 0; rate < 5; rate++) {
            if ((oac_rates[rate]*3/fp3s) != oac_packet_get_samples_per_frame(packet, oac_rates[rate])) test_failed();
            cfgs++;
        }
    }
    fprintf(stdout, "    oac_packet_get_samples_per_frame() .......... OK.\n");

    packet[0] = (63<<2) + 2;
    packet[1] = (7<<4);
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
    if (oac_packet_get_bandwidth(NULL)              != OAC_BAD_ARG)test_failed();
    if (oac_packet_get_samples_per_frame(NULL, 48000) != OAC_BAD_ARG) test_failed();
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

    packet[0] = (63<<2) + 2;
    packet[1] = (7<<4);
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

/* Helper to map a base frame duration (in 2.5 ms units) to its index in oaci_frame_dur[8]. */
static int test_dur_to_idx(int dur_units) {
    static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
    int k;
    for (k = 0; k < 8; k++) {
        if (ref_dur[k] == dur_units) return k;
    }
    return -1;
}

static int test_frames_to_F(int base_dur_units, int nb_frames) {
    static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
    int base_idx = test_dur_to_idx(base_dur_units);
    int target_idx = test_dur_to_idx(base_dur_units * nb_frames);
    if (base_idx < 0 || target_idx < 0 || target_idx < base_idx) return -1;
    (void)ref_dur;
    return target_idx - base_idx;
}

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
    fprintf(stdout, "\n  Packet header parsing tests\n");
    fprintf(stdout, "  ---------------------------------------------------\n");
    memset(packet, 0, sizeof(packet));
    packet[0] = 63<<2;
    if (oac_packet_parse(packet, 1, &toc, frames, 0, &payload_offset) != OAC_BAD_ARG) test_failed();
    cfgs_total = cfgs = 1;
    /* Single frame (X=0, P=0) */
    for (i = 0; i < 64; i++) {
        packet[0] = i<<2;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 1) test_failed();
        if (size[0] != 3) test_failed();
        if (frames[0] != packet + 1) test_failed();
        if (oac_packet_get_format(packet, 4) != OAC_FORMAT_STANDARD) test_failed();
        if (oac_packet_get_nb_channels(packet, 4) != (i & 1) + 1) test_failed();
    }
    fprintf(stdout, "    X=0 single frame (%2d cases) ................. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /* Single frame (X=0), the largest representable implicit length and one past it */
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
    fprintf(stdout, "    X=0 size limit (%2d cases) .................. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /* Extended ToC (X=1, V=0), two frames of the same size */
    for (i = 0; i < 64; i++) {
        int base_dur = oac_packet_get_samples_per_frame((const unsigned char[]){ (unsigned char)(i<<2) }, 48000) / 120;
        int f_inc = test_frames_to_F(base_dur, 2);
        if (f_inc <= 0) test_failed(); /* 2 * base_dur is always in {2, 4, 8, 16, 32, 48} */
        packet[0] = (i<<2) + 2;
        packet[1] = (unsigned char)(f_inc << 4);
        for (jj = 1; jj <= 2*TEST_PKT_LEN + 4; jj++) {
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, jj, &toc, frames, size, &payload_offset);
            cfgs++;
            if (jj >= 2 && ((jj - 2)&1) == 0) {
                /* Must pass if the payload length (jj - 2) is even. */
                if (ret != 2) test_failed();
                if (size[0] != size[1] || size[0] != ((jj - 2)>>1)) test_failed();
                if (frames[0] != packet + 2) test_failed();
                if (frames[1] != frames[0] + size[0]) test_failed();
                if ((toc>>2) != i) test_failed();
            } else if (ret != OAC_INVALID_PACKET) test_failed();
        }
        /*The largest representable pair of implicit lengths must be accepted.*/
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2*(oac_int32)OAC_SIZE_MAX + 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 2) test_failed();
        if (size[0] != OAC_SIZE_MAX || size[1] != OAC_SIZE_MAX) test_failed();
        /*One byte per frame more must not be.*/
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2*((oac_int32)OAC_SIZE_MAX + 1) + 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    fprintf(stdout, "    X=1 V=0 2-frame CBR (%6d cases) .......... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        int base_dur = oac_packet_get_samples_per_frame((const unsigned char[]){ (unsigned char)(i<<2) }, 48000) / 120;
        int f_inc = test_frames_to_F(base_dur, 2);
        /* Extended ToC (X=1, V=1) 2 frames, length code overflow */
        packet[0] = (i<<2) + 2;
        packet[1] = (unsigned char)(0x80 | (f_inc << 4));
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
        /* a two-byte length code truncated by the end of the packet */
        packet[2] = 192;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 3, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
        /* a three-byte length code truncated by the end of the packet */
        packet[2] = 224;
        packet[3] = 0;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
        for (j = 0; j < TEST_SIZE_SWEEP; j++) {
            nb = ref_put_size(&packet[2], j);
            /* one too short */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + j - 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /* the second frame one byte past what can be represented */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + j + (oac_int32)OAC_SIZE_MAX + 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /* second zero */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + j, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != 2) test_failed();
            if (size[0] != j || size[1] != 0) test_failed();
            if (frames[1] != frames[0] + size[0]) test_failed();
            if ((toc>>2) != i) test_failed();
            /* normal */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, (j<<1) + nb + 3, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != 2) test_failed();
            if (size[0] != j || size[1] != j + 1) test_failed();
            if (frames[1] != frames[0] + size[0]) test_failed();
            if ((toc>>2) != i) test_failed();
        }
    }
    fprintf(stdout, "    X=1 V=1 2-frame VBR (%6d cases) .......... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        packet[0] = (i<<2) + 2;
        /* Extended ToC truncated at 1 byte */
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 1, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    fprintf(stdout, "    X=1 truncation (%2d cases) ................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /* Invalid F increments (target_idx > 7 or non-integer division like 40ms -> 60ms) */
    for (i = 0; i < 64; i++) {
        static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
        int base_dur = oac_packet_get_samples_per_frame((const unsigned char[]){ (unsigned char)(i<<2) }, 48000) / 120;
        int base_idx = test_dur_to_idx(base_dur);
        for (jj = 0; jj < 8; jj++) {
            int target_idx = base_idx + jj;
            if (target_idx <= 7 && (ref_dur[target_idx] % base_dur) == 0) continue;
            packet[0] = (i<<2) + 2;
            packet[1] = (unsigned char)(jj << 4); /* CBR */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, TEST_PKT_LEN, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            packet[1] = (unsigned char)(0x80 | (jj << 4)); /* VBR */
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, TEST_PKT_LEN, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
        }
    }
    fprintf(stdout, "    X=1 invalid F (%3d cases) ................... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        packet[0] = (i<<2) + 2;
        /* Extended ToC, F=0 (1 frame), CBR */
        packet[1] = 0;
        for (j = 0; j < TEST_SIZE_SWEEP; j++) {
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, j + 2, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != 1) test_failed();
            if (size[0] != j) test_failed();
            if ((toc>>2) != i) test_failed();
        }
        /*The largest representable implicit length must be accepted...*/
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, (oac_int32)OAC_SIZE_MAX + 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 1) test_failed();
        if (size[0] != OAC_SIZE_MAX) test_failed();
        /*...and one byte more must not.*/
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, (oac_int32)OAC_SIZE_MAX + 3, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
    }
    fprintf(stdout, "    X=1 F=0 CBR (%6d cases) .................. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
        int frame_samp;
        int base_dur, base_idx, f_inc;
        /* Extended ToC, F>0 CBR */
        packet[0] = (i<<2) + 2;
        frame_samp = oac_packet_get_samples_per_frame(packet, 48000);
        base_dur = frame_samp / 120;
        base_idx = test_dur_to_idx(base_dur);
        for (f_inc = 1; base_idx + f_inc < 8; f_inc++) {
            if ((ref_dur[base_idx + f_inc] % base_dur) != 0) continue;
            j = ref_dur[base_idx + f_inc] / base_dur;
            packet[1] = (unsigned char)(f_inc << 4);
            for (sz = 2; sz < ((j + 2)*TEST_PKT_LEN); sz++) {
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, sz, &toc, frames, size, &payload_offset);
                cfgs++;
                if ((sz - 2)%j == 0) {
                    if (ret != j) test_failed();
                    for (jj = 1; jj < ret; jj++) if (frames[jj] != frames[jj - 1] + size[jj - 1]) test_failed();
                    if ((toc>>2) != i) test_failed();
                } else if (ret != OAC_INVALID_PACKET) test_failed();
            }
        }
        /*Super jumbo packets. The frame size here is deliberately above 32767,
          which the old oac_int16 size[] silently truncated.*/
        j = 5760 / frame_samp;
        f_inc = test_frames_to_F(base_dur, j);
        packet[1] = (unsigned char)(f_inc << 4);
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, TEST_JUMBO_FRAME*j + 2, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != j) test_failed();
        for (jj = 0; jj < ret; jj++) if (size[jj] != TEST_JUMBO_FRAME) test_failed();
    }
    fprintf(stdout, "    X=1 F=1..7 CBR (%7d cases) .............. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        static const int ref_dur[8] = {1, 2, 4, 8, 16, 24, 32, 48};
        int frame_samp;
        int base_dur, base_idx, f_inc;
        /* Extended ToC VBR, F=0 (1 frame) */
        packet[0] = (i<<2) + 2;
        packet[1] = 0x80;
        frame_samp = oac_packet_get_samples_per_frame(packet, 48000);
        base_dur = frame_samp / 120;
        base_idx = test_dur_to_idx(base_dur);
        for (jj = 0; jj < TEST_SIZE_SWEEP; jj++) {
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + jj, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != 1) test_failed();
            if (size[0] != jj) test_failed();
            if ((toc>>2) != i) test_failed();
        }
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2 + (oac_int32)OAC_SIZE_MAX, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != 1) test_failed();
        if (size[0] != OAC_SIZE_MAX) test_failed();
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 2 + (oac_int32)OAC_SIZE_MAX + 1, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();
        for (f_inc = 1; base_idx + f_inc < 8; f_inc++) {
            if ((ref_dur[base_idx + f_inc] % base_dur) != 0) continue;
            j = ref_dur[base_idx + f_inc] / base_dur;
            packet[1] = (unsigned char)(0x80 | (f_inc << 4));
            /*Length code overflow*/
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + j - 2, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*A three-byte length code that does not fit in the packet*/
            nb = ref_put_size(&packet[2], TEST_SIZE_2B_MAX + 1);
            for (jj = 2 + nb; jj < 2 + j; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + j, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*One byte too short*/
            for (jj = 2; jj < 2 + j; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + j - 2, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*One byte too short thanks to length coding*/
            nb = ref_put_size(&packet[2], TEST_SIZE_2B_MAX + 1);
            for (jj = 2 + nb; jj < 2 + nb + j - 2; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + (j - 2) + TEST_SIZE_2B_MAX + 1 - 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != OAC_INVALID_PACKET) test_failed();
            /*...and exactly long enough parses, with a three-byte length code*/
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + nb + (j - 2) + TEST_SIZE_2B_MAX + 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != j) test_failed();
            if (size[0] != TEST_SIZE_2B_MAX + 1) test_failed();
            for (jj = 1; jj < j; jj++) if (size[jj] != 0) test_failed();
            if ((toc>>2) != i) test_failed();
            /*Most expensive way of coding zeros*/
            for (jj = 2; jj < 2 + j; jj++) packet[jj] = 0;
            UNDEFINE_FOR_PARSE
                ret = oac_packet_parse(packet, 2 + j - 1, &toc, frames, size, &payload_offset);
            cfgs++;
            if (ret != j) test_failed();
            for (jj = 0; jj < j; jj++) if (size[jj] != 0) test_failed();
            if ((toc>>2) != i) test_failed();
            /*Quasi-CBR use of VBR. The larger entries of tsz[] put the
              per-frame length in the two- and three-byte tiers.*/
            for (sz = 0; sz < 8; sz++) {
                const int tsz[8] = {50, 201, 403, 700, 1472, 5110, 20400, 61298};
                int pos = 0;
                int as = (tsz[sz] + i - j - 2)/j;
                for (jj = 0; jj < j - 1; jj++) pos += ref_put_size(&packet[2 + pos], as);
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, tsz[sz] + i, &toc, frames, size, &payload_offset);
                cfgs++;
                if (ret != j) test_failed();
                for (jj = 0; jj < j - 1; jj++) if (size[jj] != as) test_failed();
                if (size[j - 1] != (tsz[sz] + i - 2 - pos - as*(j - 1))) test_failed();
                if ((toc>>2) != i) test_failed();
            }
        }
    }
    fprintf(stdout, "    X=1 F=0..7 VBR (%6d cases) ............... OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    for (i = 0; i < 64; i++) {
        packet[0] = (i<<2) + 3; /* X=1, P=1 */
        packet[1] = 0x80;       /* V=1, F=0 (1 frame) */
        /*Overflow the length coding*/
        for (jj = 2; jj < 127; jj++) packet[jj] = 255;
        UNDEFINE_FOR_PARSE
            ret = oac_packet_parse(packet, 127, &toc, frames, size, &payload_offset);
        cfgs++;
        if (ret != OAC_INVALID_PACKET) test_failed();

        for (sz = 0; sz < 4; sz++) {
            const int tsz[4] = {0, 72, 512, TEST_PKT_LEN};
            for (jj = sz; jj < 65025; jj += 11) {
                int pos;
                for (pos = 0; pos < jj/254; pos++) packet[2 + pos] = 255;
                packet[2 + pos] = jj%254;
                pos++;
                if (sz == 0 && i == 63) {
                    /*Code more padding than there is room in the packet*/
                    UNDEFINE_FOR_PARSE
                        ret = oac_packet_parse(packet, 2 + jj + pos - 1, &toc, frames, size, &payload_offset);
                    cfgs++;
                    if (ret != OAC_INVALID_PACKET) test_failed();
                }
                UNDEFINE_FOR_PARSE
                    ret = oac_packet_parse(packet, 2 + jj + tsz[sz] + i + pos, &toc, frames, size, &payload_offset);
                cfgs++;
                /*Every size used here is representable, so this always parses.*/
                if (ret != 1) test_failed();
                if (size[0] != tsz[sz] + i) test_failed();
                if ((toc>>2) != i) test_failed();
            }
        }
    }
    fprintf(stdout, "    X=1 P=1 padding (%6d cases) .............. OK.\n", cfgs);
    cfgs_total += cfgs; cfgs = 0;

    /* Test extended channel counts (1..256) and Ambisonics orders (0..15) in ToC */
    {
        int ch, order;
        /* Standard format: 2-byte ToC (2*C + S + 1) -> 1..15 channels */
        for (ch = 1; ch <= 15; ch++) {
            int s_bit = (ch - 1) & 1;
            int c_val = (ch - 1) >> 1;
            packet[0] = (unsigned char)((31 << 3) | (s_bit << 2) | 2);
            packet[1] = (unsigned char)c_val;
            if (oac_packet_get_format(packet, 4) != OAC_FORMAT_STANDARD) test_failed();
            if (oac_packet_get_nb_channels(packet, 4) != ch) test_failed();
            if (oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset) != 1) test_failed();
            if (payload_offset != 2 || size[0] != 2) test_failed();
            cfgs += 3;
        }
        /* Standard format: C=7, S=1 -> 1..256 channels in data[2] */
        for (ch = 1; ch <= 256; ch++) {
            packet[0] = (unsigned char)((31 << 3) | (1 << 2) | 2);
            packet[1] = 7;
            packet[2] = (unsigned char)(ch - 1);
            if (oac_packet_get_format(packet, 5) != OAC_FORMAT_STANDARD) test_failed();
            if (oac_packet_get_nb_channels(packet, 5) != ch) test_failed();
            if (oac_packet_get_nb_channels(packet, 2) != OAC_INVALID_PACKET) test_failed();
            if (oac_packet_parse(packet, 5, &toc, frames, size, &payload_offset) != 1) test_failed();
            if (payload_offset != 3 || size[0] != 2) test_failed();
            cfgs += 4;
        }
        /* Ambisonics orders 0..15 */
        for (order = 0; order <= 15; order++) {
            packet[0] = (unsigned char)((31 << 3) | ((order & 1) << 2) | 2);
            packet[1] = (unsigned char)(0x08 | (order >> 1));
            if (oac_packet_get_format(packet, 4) != OAC_FORMAT_AMBISONICS) test_failed();
            if (oac_packet_get_nb_channels(packet, 4) != (order + 1) * (order + 1)) test_failed();
            if (oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset) != 1) test_failed();
            if (payload_offset != 2 || size[0] != 2) test_failed();
            cfgs += 3;
        }
        /* SILK/Hybrid with A=1 or C>0 must be rejected */
        packet[0] = (unsigned char)((0 << 3) | (1 << 2) | 2);
        packet[1] = 0x08;
        if (oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset) != OAC_INVALID_PACKET) test_failed();
        packet[1] = 1;
        if (oac_packet_parse(packet, 4, &toc, frames, size, &payload_offset) != OAC_INVALID_PACKET) test_failed();
        cfgs += 2;
    }
    fprintf(stdout, "    extended channels & Ambisonics (%4d cases) . OK.\n", cfgs);
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
        /* RESYNTH builds allocate per-channel state sized for OAC_MAX_CHANNELS, so the
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
#define max_out ((TEST_REPACK_MAX + 3)*OAC_MAX_FRAMES_PER_PACKET + 3)
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

    /*Length overflows*/
    VG_UNDEF(packet, 4);
    if (oac_repacketizer_cat(rp, packet, 0) != OAC_INVALID_PACKET) test_failed(); /* Zero len */
    cfgs++;
    packet[0] = 2;
    packet[1] = (1<<4); /* V=0, F=1 (2 equal CBR frames) */
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_INVALID_PACKET) test_failed(); /* Odd payload (1 byte) */
    cfgs++;
    packet[0] = 2;
    if (oac_repacketizer_cat(rp, packet, 1) != OAC_INVALID_PACKET) test_failed(); /* X=1 truncated at 1 byte */
    cfgs++;
    packet[0] = 2;
    packet[1] = 0x80 | (1<<4); /* V=1, F=1 (2 VBR frames) */
    if (oac_repacketizer_cat(rp, packet, 2) != OAC_INVALID_PACKET) test_failed(); /* VBR 2 frames missing size byte */
    cfgs++;
    packet[2] = 255;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_INVALID_PACKET) test_failed(); /* 3-byte size truncated */
    cfgs++;
    packet[2] = 191;
    if (oac_repacketizer_cat(rp, packet, 193) != OAC_INVALID_PACKET) test_failed(); /* 2 + 1 + 191 = 194 > 193 */
    cfgs++;
    packet[1] = (7<<4); /* F=7 on 10ms base (idx 2 + 7 = 9 > 7) */
    if (oac_repacketizer_cat(rp, packet, 100) != OAC_INVALID_PACKET) test_failed(); /* Invalid F */
    cfgs++;
    packet[0] = 0;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = 1<<2;
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_INVALID_PACKET) test_failed(); /* Change in TOC */
    cfgs++;

    /* CBR -> CBR across all valid (base_dur, F) combinations */
    oac_repacketizer_init(rp);
    for (j = 0; j < 32; j++) {
        /* TOC types, test half with stereo */
        int maxi, base_dur;
        packet[0] = ((j<<1) + (j&1))<<2;
        base_dur = oac_packet_get_samples_per_frame(packet, 48000) / 120;
        maxi = 960/oac_packet_get_samples_per_frame(packet, 8000);
        for (i = 1; i <= maxi; i++) {
            /* Number of CBR frames in the input packets */
            int maxp;
            int f_in = test_frames_to_F(base_dur, i);
            int in_hdr = (i > 1) ? 2 : 1;
            if (f_in < 0) continue; /* Skip frame counts not representable by F */
            packet[0] = (((j<<1) + (j&1))<<2) | (i > 1 ? 2 : 0);
            packet[1] = (unsigned char)(f_in << 4);
            maxp = 960/(i*oac_packet_get_samples_per_frame(packet, 8000));
            for (k = 0; k <= TEST_REPACK_MAX; k += 3) {
                /*Payload size*/
                oac_int32 cnt, rcnt;
                if (k%i != 0) continue; /* Only testing CBR here, payload must be a multiple of the count */
                for (cnt = 0; cnt < maxp + 2; cnt++) {
                    if (cnt > 0) {
                        ret = oac_repacketizer_cat(rp, packet, k + in_hdr);
                        if ((cnt <= maxp)?ret != OAC_OK:ret != OAC_INVALID_PACKET) test_failed();
                        cfgs++;
                    }
                    rcnt = cnt < maxp?cnt:maxp;
                    if (oac_repacketizer_get_nb_frames(rp) != rcnt*i) test_failed();
                    cfgs++;
                    ret = oac_repacketizer_out_range(rp, 0, rcnt*i, po, max_out);
                    if (rcnt > 0) {
                        int f_out = test_frames_to_F(base_dur, rcnt*i);
                        if (f_out < 0) {
                            /* Non-representable total duration (e.g. 5x20ms=100ms, 3x40ms=120ms) must return OAC_BAD_ARG */
                            if (ret != OAC_BAD_ARG) test_failed();
                            cfgs++;
                        } else {
                            int len = k*rcnt + ((rcnt*i) > 1 ? 2 : 1);
                            if (ret != len) test_failed();
                            if ((rcnt*i) == 1 && (po[0]&3) != 0) test_failed(); /* X=0, P=0 */
                            if ((rcnt*i) > 1 && (((po[0]&3) != 2) || (po[1] != (f_out<<4)))) test_failed(); /* X=1, V=0 */
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
                    } else if (ret != OAC_BAD_ARG) test_failed(); /* M must not be 0 */
                }
                oac_repacketizer_init(rp);
            }
        }
    }

    /*Change in input frame count, CBR out (1 + 2 = 3 frames of 20 ms -> 60 ms, F=2)*/
    oac_repacketizer_init(rp);
    packet[0] = (1<<3); /* 20 ms SILK NB, X=0 */
    if (oac_repacketizer_cat(rp, packet, 5) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = (1<<3) | 2; /* 20 ms SILK NB, X=1 */
    packet[1] = (1<<4);     /* V=0, F=1 (2 frames of 4 bytes each) */
    if (oac_repacketizer_cat(rp, packet, 10) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    if ((i != (4 + 8 + 2)) || ((po[0]&3) != 2) || (po[1] != (2<<4))) test_failed();
    cfgs++;
    i = oac_repacketizer_out_range(rp, 0, 1, po, max_out);
    if (i != 5 || (po[0]&3) != 0) test_failed();
    cfgs++;
    i = oac_repacketizer_out_range(rp, 1, 2, po, max_out);
    if (i != 5 || (po[0]&3) != 0) test_failed();
    cfgs++;

    /*Change in input frame count, VBR out (2 + 1 = 3 frames of 20 ms -> 60 ms, F=2, V=1)*/
    oac_repacketizer_init(rp);
    packet[0] = (1<<3) | 2;
    packet[1] = (1<<4);
    if (oac_repacketizer_cat(rp, packet, 10) != OAC_OK) test_failed();
    cfgs++;
    packet[0] = (1<<3);
    if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    if ((i != (2 + 8 + 2 + 2)) || ((po[0]&3) != 2) || (po[1] != (0x80 | (2<<4)))) test_failed();
    cfgs++;

    /*VBR in, VBR out (2 + 2 = 4 frames of 10 ms -> 40 ms, F=2, V=1)*/
    oac_repacketizer_init(rp);
    packet[0] = 2; /* 10 ms SILK NB, X=1 */
    packet[1] = 0x80 | (1<<4); /* V=1, F=1 (2 frames) */
    packet[2] = 4; /* first frame = 4 bytes, second frame = 9 - 3 - 4 = 2 bytes */
    if (oac_repacketizer_cat(rp, packet, 9) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_cat(rp, packet, 9) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    if ((i != (2 + 1 + 1 + 1 + 4 + 2 + 4 + 2)) || ((po[0]&3) != 2) || (po[1] != (0x80 | (2<<4)))) test_failed();
    cfgs++;

    /*VBR in, CBR out (2 + 2 = 4 frames of 10 ms -> 40 ms, F=2, V=0)*/
    oac_repacketizer_init(rp);
    packet[0] = 2;
    packet[1] = 0x80 | (1<<4);
    packet[2] = 4; /* first frame = 4 bytes, second frame = 11 - 3 - 4 = 4 bytes */
    if (oac_repacketizer_cat(rp, packet, 11) != OAC_OK) test_failed();
    cfgs++;
    if (oac_repacketizer_cat(rp, packet, 11) != OAC_OK) test_failed();
    cfgs++;
    i = oac_repacketizer_out(rp, po, max_out);
    if ((i != (2 + 4 + 4 + 4 + 4)) || ((po[0]&3) != 2) || (po[1] != (2<<4))) test_failed();
    cfgs++;

    /*X=0 in, VBR out*/
    for (j = 0; j < 32; j++) {
        /* TOC types, test half with stereo */
        int maxi, sum, rcnt, base_dur;
        packet[0] = ((j<<1) + (j&1))<<2;
        base_dur = oac_packet_get_samples_per_frame(packet, 48000) / 120;
        maxi = 960/oac_packet_get_samples_per_frame(packet, 8000);
        sum = 0;
        rcnt = 0;
        oac_repacketizer_init(rp);
        for (i = 1; i <= maxi + 2; i++) {
            int len, f_out;
            ret = oac_repacketizer_cat(rp, packet, i);
            if (rcnt < maxi) {
                if (ret != OAC_OK) test_failed();
                rcnt++;
                sum += i - 1;
            } else if (ret != OAC_INVALID_PACKET) test_failed();
            cfgs++;
            f_out = test_frames_to_F(base_dur, rcnt);
            if (f_out < 0) {
                if (oac_repacketizer_out(rp, po, max_out) != OAC_BAD_ARG) test_failed();
                cfgs++;
                continue;
            }
            len = sum + (rcnt < 2 ? 1 : 2 + rcnt - 1);
            if (oac_repacketizer_out(rp, po, max_out) != len) test_failed();
            if (rcnt > 1 && po[1] != (0x80 | (f_out << 4))) test_failed();
            if (rcnt == 1 && (po[0]&3) != 0) test_failed();
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

    po[0] = (63<<2) | 2;
    po[1] = (7<<4); /* Invalid F=7 on 20ms base */
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

    /* Multi-channel (1..256 discrete and Ambisonics orders 0..15) repacketizer round-trip */
    {
        int ch, order;
        for (ch = 1; ch <= 256; ch++) {
            int in_hdr = (ch <= 2) ? 1 : (ch <= 15 ? 2 : 3);
            int out_hdr = (ch <= 15) ? 2 : 3;
            oac_repacketizer_init(rp);
            if (ch <= 2) {
                packet[0] = (unsigned char)((31 << 3) | ((ch - 1) << 2));
            } else if (ch <= 15) {
                packet[0] = (unsigned char)((31 << 3) | (((ch - 1) & 1) << 2) | 2);
                packet[1] = (unsigned char)((ch - 1) >> 1);
            } else {
                packet[0] = (unsigned char)((31 << 3) | 4 | 2);
                packet[1] = 7;
                packet[2] = (unsigned char)(ch - 1);
            }
            packet[in_hdr] = 0x11;
            packet[in_hdr + 1] = 0x22;
            if (oac_repacketizer_cat(rp, packet, in_hdr + 2) != OAC_OK) test_failed();
            if (oac_repacketizer_cat(rp, packet, in_hdr + 2) != OAC_OK) test_failed();
            ret = oac_repacketizer_out(rp, po, max_out);
            if (ret != out_hdr + 4) test_failed();
            if (oac_packet_get_format(po, ret) != OAC_FORMAT_STANDARD) test_failed();
            if (oac_packet_get_nb_channels(po, ret) != ch) test_failed();
            if (oac_packet_get_nb_frames(po, ret) != 2) test_failed();
            cfgs += 6;
        }
        /* Mixing canonical 1-byte (S=0) and 3-byte escape (C=7, S=1, data[2]=0) for 1 channel must succeed */
        oac_repacketizer_init(rp);
        packet[0] = (unsigned char)(31 << 3);
        packet[1] = 0xAA;
        if (oac_repacketizer_cat(rp, packet, 2) != OAC_OK) test_failed();
        packet[0] = (unsigned char)((31 << 3) | 4 | 2);
        packet[1] = 7;
        packet[2] = 0;
        packet[3] = 0xBB;
        if (oac_repacketizer_cat(rp, packet, 4) != OAC_OK) test_failed();
        ret = oac_repacketizer_out(rp, po, max_out);
        if (ret != 4 || oac_packet_get_nb_channels(po, ret) != 1 || oac_packet_get_nb_frames(po, ret) != 2) test_failed();
        /* Channel count or format mismatch must be rejected */
        packet[2] = 1; /* 2 channels */
        if (oac_repacketizer_cat(rp, packet, 4) != OAC_INVALID_PACKET) test_failed();
        packet[0] = (unsigned char)((31 << 3) | 2);
        packet[1] = 0x08; /* Ambisonics order 0 (1 channel, different format) */
        if (oac_repacketizer_cat(rp, packet, 3) != OAC_INVALID_PACKET) test_failed();
        cfgs += 7;
        for (order = 0; order <= 15; order++) {
            oac_repacketizer_init(rp);
            packet[0] = (unsigned char)((31 << 3) | ((order & 1) << 2) | 2);
            packet[1] = (unsigned char)(0x08 | (order >> 1));
            packet[2] = 0x33;
            if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
            if (oac_repacketizer_cat(rp, packet, 3) != OAC_OK) test_failed();
            ret = oac_repacketizer_out(rp, po, max_out);
            if (ret != 4) test_failed();
            if (oac_packet_get_format(po, ret) != OAC_FORMAT_AMBISONICS) test_failed();
            if (oac_packet_get_nb_channels(po, ret) != (order + 1) * (order + 1)) test_failed();
            if (oac_packet_get_nb_frames(po, ret) != 2) test_failed();
            cfgs += 6;
        }
    }

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
    pkt[0] = (31<<2) + 2;
    pkt[1] = 0x80 | (1<<4); /* V=1, F=1 (2 frames of 20 ms) */
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
            /* Two X=0 frames of different sizes, so the output uses X=1, V=1
               and has to signal the length of the first one explicitly. */
            pkt[0] = (31<<2) + 0;
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
       re-coded as CBR (V=0), which drops the explicit length; anything else keeps
       VBR (V=1). */
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

                /* Build a 2-frame VBR packet by hand: 20 ms Hybrid stereo ToC. */
                in[0] = (15<<3)|(1<<2)|2;
                in[1] = 0x80 | (1<<4);
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

                /* Splitting back into single frames must also work (1-byte ToC). */
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
       that much up front. Also verify max_data_bytes = 1..4 (buffer-too-small
       and low-bitrate PLC paths) with a 0xA5 guard byte immediately past
       max_data_bytes. */
    {
        int order;

        pcm = (short *)malloc(sizeof(short)*960*36);
        out = (short *)malloc(sizeof(short)*960*36);
        if (pcm == NULL || out == NULL) test_failed();
        for (order = 1; order <= 5; order++) {
            OacEncoder *enc;
            OacDecoder *dec;
            unsigned char *data;
            int err, channels, budget, cap;

            channels = (order + 1)*(order + 1);
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

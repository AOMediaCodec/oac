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

/* Copyright (c) 2017 Google Inc.
   Written by Andrew Allen */
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "float_cast.h"
#include "oac.h"
#include "test_oac_common.h"
#include "oac_projection.h"
#include "mathops.h"
#include "../src/mapping_matrix.h"
#include "mathops.h"

#define BUFFER_SIZE 960
#define MAX_DATA_BYTES 32768
#define MAX_FRAME_SAMPLES 5760
#define ERROR_TOLERANCE 1

#define SIMPLE_MATRIX_SIZE 12
#define SIMPLE_MATRIX_FRAME_SIZE 10
#define SIMPLE_MATRIX_INPUT_SIZE 30
#define SIMPLE_MATRIX_OUTPUT_SIZE 40

int assert_is_equal(
    const oac_res *a, const oac_int16 *b, int size, oac_int16 tolerance) {
    int i;
    for (i = 0; i < size; i++) {
        if (abs(RES2INT16(a[i]) - b[i]) > tolerance)
            return 1;
    }
    return 0;
}

int assert_is_equal_short(
    const oac_int16 *a, const oac_int16 *b, int size, oac_int16 tolerance) {
    int i;
    for (i = 0; i < size; i++)
        if (abs(a[i] - b[i]) > tolerance)
            return 1;
    return 0;
}

void test_simple_matrix(void) {
    const MappingMatrix simple_matrix_params = {4, 3, 0};
    const oac_int16 simple_matrix_data[SIMPLE_MATRIX_SIZE] = {0, 32767, 0, 0, 32767, 0, 0, 0, 0, 0, 0, 32767};
    const oac_int16 input_int16[SIMPLE_MATRIX_INPUT_SIZE] = {
        32767, 0, -32768, 29491, -3277, -29491, 26214, -6554, -26214, 22938, -9830,
        -22938, 19661, -13107, -19661, 16384, -16384, -16384, 13107, -19661, -13107,
        9830, -22938, -9830, 6554, -26214, -6554, 3277, -29491, -3277
    };
    const oac_int16 expected_output_int16[SIMPLE_MATRIX_OUTPUT_SIZE] = {
        0, 32767, 0, -32768, -3277, 29491, 0, -29491, -6554, 26214, 0, -26214,
        -9830, 22938, 0, -22938, -13107, 19661, 0, -19661, -16384, 16384, 0, -16384,
        -19661, 13107, 0, -13107, -22938, 9830, 0, -9830, -26214, 6554, 0, -6554,
        -29491, 3277, 0, -3277
    };

    int i, ret;
    oac_int32 simple_matrix_size;
    oac_res *input_pcm;
    oac_res *output_pcm;
    oac_int16 *output_int16;
    MappingMatrix *simple_matrix;

    /* Allocate input/output buffers. */
    input_pcm = (oac_res *)oac_alloc(sizeof(oac_res)*SIMPLE_MATRIX_INPUT_SIZE);
    output_int16 = (oac_int16 *)oac_alloc(sizeof(oac_int16)*SIMPLE_MATRIX_OUTPUT_SIZE);
    output_pcm = (oac_res *)oac_alloc(sizeof(oac_res)*SIMPLE_MATRIX_OUTPUT_SIZE);

    /* Initialize matrix */
    simple_matrix_size = oaci_mapping_matrix_get_size(simple_matrix_params.rows,
    simple_matrix_params.cols);
    if (!simple_matrix_size)
        test_failed();

    simple_matrix = (MappingMatrix *)oac_alloc(simple_matrix_size);
    oaci_mapping_matrix_init(simple_matrix, simple_matrix_params.rows,
    simple_matrix_params.cols, simple_matrix_params.gain, simple_matrix_data,
    sizeof(simple_matrix_data));

    /* Copy inputs. */
    for (i = 0; i < SIMPLE_MATRIX_INPUT_SIZE; i++) {
        input_pcm[i] = INT16TORES(input_int16[i]);
    }

    /* _in_short */
    for (i = 0; i < SIMPLE_MATRIX_OUTPUT_SIZE; i++)
        output_pcm[i] = 0;
    for (i = 0; i < simple_matrix->rows; i++) {
        oaci_mapping_matrix_multiply_channel_in_short(simple_matrix,
      input_int16, simple_matrix->cols, &output_pcm[i], i,
      simple_matrix->rows, SIMPLE_MATRIX_FRAME_SIZE);
    }
    ret = assert_is_equal(output_pcm, expected_output_int16, SIMPLE_MATRIX_OUTPUT_SIZE, ERROR_TOLERANCE);
    if (ret)
        test_failed();

    /* _out_short */
    for (i = 0; i < SIMPLE_MATRIX_OUTPUT_SIZE; i++)
        output_int16[i] = 0;
    for (i = 0; i < simple_matrix->cols; i++) {
        oaci_mapping_matrix_multiply_channel_out_short(simple_matrix,
      &input_pcm[i], i, simple_matrix->cols, output_int16,
      simple_matrix->rows, SIMPLE_MATRIX_FRAME_SIZE);
    }
    ret = assert_is_equal_short(output_int16, expected_output_int16, SIMPLE_MATRIX_OUTPUT_SIZE, ERROR_TOLERANCE);
    if (ret)
        test_failed();

#if !defined(DISABLE_FLOAT_API) && !defined(FIXED_POINT)
    /* _in_float */
    for (i = 0; i < SIMPLE_MATRIX_OUTPUT_SIZE; i++)
        output_pcm[i] = 0;
    for (i = 0; i < simple_matrix->rows; i++) {
        oaci_mapping_matrix_multiply_channel_in_float(simple_matrix,
      input_pcm, simple_matrix->cols, &output_pcm[i], i,
      simple_matrix->rows, SIMPLE_MATRIX_FRAME_SIZE);
    }
    ret = assert_is_equal(output_pcm, expected_output_int16, SIMPLE_MATRIX_OUTPUT_SIZE, ERROR_TOLERANCE);
    if (ret)
        test_failed();

    /* _out_float */
    for (i = 0; i < SIMPLE_MATRIX_OUTPUT_SIZE; i++)
        output_pcm[i] = 0;
    for (i = 0; i < simple_matrix->cols; i++) {
        oaci_mapping_matrix_multiply_channel_out_float(simple_matrix,
      &input_pcm[i], i, simple_matrix->cols, output_pcm,
      simple_matrix->rows, SIMPLE_MATRIX_FRAME_SIZE);
    }
    ret = assert_is_equal(output_pcm, expected_output_int16, SIMPLE_MATRIX_OUTPUT_SIZE, ERROR_TOLERANCE);
    if (ret)
        test_failed();
#endif

    oac_free(input_pcm);
    oac_free(output_int16);
    oac_free(output_pcm);
    oac_free(simple_matrix);
}

void test_creation_arguments(const int channels, const int mapping_family) {
    int streams;
    int coupled_streams;
    int enc_error;
    int dec_error;
    int ret;
    OacProjectionEncoder *st_enc = NULL;
    OacProjectionDecoder *st_dec = NULL;

    const oac_int32 Fs = 48000;
    const int application = OAC_APPLICATION_AUDIO;

    int order_plus_one = (int)floor(sqrt((float)channels));
    int nondiegetic_channels = channels - order_plus_one*order_plus_one;

    int is_channels_valid = 0;
    int is_projection_valid = 0;

    st_enc = oac_projection_ambisonics_encoder_create(Fs, channels,
    mapping_family, &streams, &coupled_streams, application, &enc_error);
    if (st_enc != NULL) {
        oac_int32 matrix_size;
        unsigned char *matrix;

        ret = oac_projection_encoder_ctl(st_enc,
      OAC_PROJECTION_GET_DEMIXING_MATRIX_SIZE_REQUEST, &matrix_size);
        if (ret != OAC_OK || !matrix_size)
            test_failed();

        matrix = (unsigned char *)oac_alloc(matrix_size);
        ret = oac_projection_encoder_ctl(st_enc,
      OAC_PROJECTION_GET_DEMIXING_MATRIX_REQUEST, matrix, matrix_size);

        oac_projection_encoder_destroy(st_enc);

        st_dec = oac_projection_decoder_create(Fs, channels, streams,
      coupled_streams, matrix, matrix_size, &dec_error);
        if (st_dec != NULL) {
            oac_projection_decoder_destroy(st_dec);
        }
        oac_free(matrix);
    }

    is_channels_valid = (order_plus_one >= 2 && order_plus_one <= 6)
                        && (nondiegetic_channels == 0 || nondiegetic_channels == 2);
    is_projection_valid = (enc_error == OAC_OK && dec_error == OAC_OK);
    if (is_channels_valid^is_projection_valid) {
        fprintf(stderr, "Channels: %d, Family: %d\n", channels, mapping_family);
        fprintf(stderr, "Order+1: %d, Non-diegetic Channels: %d\n",
      order_plus_one, nondiegetic_channels);
        fprintf(stderr, "Streams: %d, Coupled Streams: %d\n",
      streams, coupled_streams);
        test_failed();
    }
}

void generate_music(short *buf, oac_int32 len, oac_int32 channels) {
    oac_int32 i, j, k;
    oac_int32 *a, *b, *c, *d;
    a = (oac_int32 *)malloc(sizeof(oac_int32)*channels);
    b = (oac_int32 *)malloc(sizeof(oac_int32)*channels);
    c = (oac_int32 *)malloc(sizeof(oac_int32)*channels);
    d = (oac_int32 *)malloc(sizeof(oac_int32)*channels);
    memset(a, 0, sizeof(oac_int32)*channels);
    memset(b, 0, sizeof(oac_int32)*channels);
    memset(c, 0, sizeof(oac_int32)*channels);
    memset(d, 0, sizeof(oac_int32)*channels);
    j = 0;

    for (i = 0; i < len; i++) {
        for (k = 0; k < channels; k++) {
            oac_uint32 r;
            oac_int32 v;
            v = (((j*((j>>12)^((j>>10|j>>12)&26&j>>7)))&128) + 128)<<15;
            r = fast_rand(); v += r&65535; v -= r>>16;
            b[k] = v - a[k] + ((b[k]*61 + 32)>>6); a[k] = v;
            c[k] = (30*(c[k] + b[k] + d[k]) + 32)>>6; d[k] = b[k];
            v = (c[k] + 128)>>8;
            buf[i*channels + k] = v > 32767?32767:(v < -32768?-32768:v);
            if (i%6 == 0) j++;
        }
    }

    free(a);
    free(b);
    free(c);
    free(d);
}

void test_encode_decode(oac_int32 bitrate, oac_int32 channels,
                        const int mapping_family) {
    const oac_int32 Fs = 48000;
    const int application = OAC_APPLICATION_AUDIO;

    OacProjectionEncoder *st_enc;
    OacProjectionDecoder *st_dec;
    int streams;
    int coupled;
    int error;
    short *buffer_in;
    short *buffer_out;
    unsigned char data[MAX_DATA_BYTES] = { 0 };
    int len;
    int out_samples;
    oac_int32 matrix_size = 0;
    oac_int32 matrix_gain = 0;
    unsigned char *matrix = NULL;

    buffer_in = (short *)malloc(sizeof(short)*BUFFER_SIZE*channels);
    buffer_out = (short *)malloc(sizeof(short)*BUFFER_SIZE*channels);

    st_enc = oac_projection_ambisonics_encoder_create(Fs, channels,
    mapping_family, &streams, &coupled, application, &error);
    if (error != OAC_OK) {
        fprintf(stderr,
      "Couldn\'t create encoder with %d channels and mapping family %d.\n",
      channels, mapping_family);
        free(buffer_in);
        free(buffer_out);
        test_failed();
    }

    error = oac_projection_encoder_ctl(st_enc,
    OAC_SET_BITRATE(bitrate*1000*(streams + coupled)));
    if (error != OAC_OK) {
        goto bad_cleanup;
    }

    error = oac_projection_encoder_ctl(st_enc,
    OAC_PROJECTION_GET_DEMIXING_MATRIX_SIZE_REQUEST, &matrix_size);
    if (error != OAC_OK || !matrix_size) {
        goto bad_cleanup;
    }

    matrix = (unsigned char *)oac_alloc(matrix_size);
    error = oac_projection_encoder_ctl(st_enc,
    OAC_PROJECTION_GET_DEMIXING_MATRIX_REQUEST, matrix, matrix_size);

    st_dec = oac_projection_decoder_create(Fs, channels, streams, coupled,
    matrix, matrix_size, &error);
    oac_free(matrix);

    if (error != OAC_OK) {
        fprintf(stderr,
      "Couldn\'t create decoder with %d channels, %d streams "
      "and %d coupled streams.\n", channels, streams, coupled);
        goto bad_cleanup;
    }

    /* Apply demixing-matrix gain as decoder output gain (OpusHead Output Gain equivalent). */
    error = oac_projection_encoder_ctl(st_enc,
    OAC_PROJECTION_GET_DEMIXING_MATRIX_GAIN_REQUEST, &matrix_gain);
    if (error != OAC_OK) {
        goto bad_cleanup;
    }
    error = oac_projection_decoder_ctl(st_dec, OAC_SET_GAIN(matrix_gain));
    if (error != OAC_OK) {
        goto bad_cleanup;
    }

    generate_music(buffer_in, BUFFER_SIZE, channels);

    len = oac_projection_encode(
    st_enc, buffer_in, BUFFER_SIZE, data, MAX_DATA_BYTES);
    if (len < 0 || len > MAX_DATA_BYTES) {
        fprintf(stderr, "oac_encode() returned %d\n", len);
        goto bad_cleanup;
    }

    out_samples = oac_projection_decode(
    st_dec, data, len, buffer_out, MAX_FRAME_SAMPLES, 0);
    if (out_samples != BUFFER_SIZE) {
        fprintf(stderr, "oac_decode() returned %d\n", out_samples);
        goto bad_cleanup;
    }

    oac_projection_decoder_destroy(st_dec);
    oac_projection_encoder_destroy(st_enc);
    free(buffer_in);
    free(buffer_out);
    return;
bad_cleanup:
    free(buffer_in);
    free(buffer_out);
    test_failed();
}

int main(int _argc, char **_argv) {
    unsigned int i;

    (void)_argc;
    (void)_argv;

    /* Test simple matrix multiplication routines. */
    test_simple_matrix();

    /* Test full range of channels in creation arguments. */
    for (i = 0; i < 255; i++)
        test_creation_arguments(i, 3);

    /* Test encode/decode pipeline. */
    test_encode_decode(64*18, 18, 3);

    fprintf(stderr, "All projection tests passed.\n");
    return 0;
}

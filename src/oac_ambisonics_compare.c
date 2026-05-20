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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "oac.h"
#include "oac_projection.h"

#define MAX_PACKET 32768
#define MAX_FRAME_SIZE 5760  /* 120ms at 48kHz */

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s <mode> <sampling_rate> <channels> <bitrate> <input.pcm> <output_prefix>\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  semac       - Encode using SEMAC (Spatial Energy Matching Ambisonics Coding)\n");
    fprintf(stderr, "  projection  - Encode using Projection API (mapping family 3)\n");
    fprintf(stderr, "  both        - Encode using both methods for comparison\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Parameters:\n");
    fprintf(stderr, "  sampling_rate  - Sampling rate in Hz (8000, 12000, 16000, 24000, 48000)\n");
    fprintf(stderr, "  channels       - Number of ambisonics channels (1, 4, 9, 16, 25, 36)\n");
    fprintf(stderr, "  bitrate        - Bitrate in bits per second\n");
    fprintf(stderr, "  input.pcm      - Input PCM file (16-bit signed, interleaved)\n");
    fprintf(stderr, "  output_prefix  - Prefix for output files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s semac 48000 16 128000 input_16ch.pcm output\n", progname);
    fprintf(stderr, "  %s projection 48000 16 128000 input_16ch.pcm output\n", progname);
    fprintf(stderr, "  %s both 48000 16 128000 input_16ch.pcm output\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Output files:\n");
    fprintf(stderr, "  <output_prefix>_semac.oac       - SEMAC encoded bitstream\n");
    fprintf(stderr, "  <output_prefix>_semac.pcm        - SEMAC decoded audio\n");
    fprintf(stderr, "  <output_prefix>_projection.oac   - Projection encoded bitstream\n");
    fprintf(stderr, "  <output_prefix>_projection.pcm   - Projection decoded audio\n");
}

static int encode_decode_semac(int sampling_rate, int channels, int bitrate,
                                FILE *fin, const char *oac_out, const char *pcm_out) {
    OacEncoder *enc = NULL;
    OacDecoder *dec = NULL;
    FILE *foac = NULL;
    FILE *fpcm = NULL;
    int err;
    int ret = 0;

    short *inbuf = NULL;
    short *outbuf = NULL;
    unsigned char *packet = NULL;

    int frame_size = sampling_rate / 50;  /* 20ms frames */
    int in_samples, out_samples;
    long total_bytes = 0;
    long total_samples = 0;

    printf("=== SEMAC Encoding ===\n");
    printf("  Sampling rate: %d Hz\n", sampling_rate);
    printf("  Channels: %d\n", channels);
    printf("  Bitrate: %d bps\n", bitrate);
    printf("  Frame size: %d samples\n", frame_size);

    /* Allocate buffers */
    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    /* Create encoder */
    enc = oac_encoder_create(sampling_rate, channels, OAC_FORMAT_AMBISONICS,
                             OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || !enc) {
        fprintf(stderr, "Error: Failed to create SEMAC encoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    /* Configure encoder */
    oac_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
    oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(10));

#ifdef ENABLE_SEMAC
    oac_encoder_ctl(enc, OAC_SET_ENABLE_SEMAC(1));
    printf("  SEMAC: enabled\n");
#else
    printf("  Warning: SEMAC not enabled in build (ENABLE_SEMAC not defined)\n");
    printf("  Proceeding with standard ambisonics encoding\n");
#endif

    /* Create decoder */
    dec = oac_decoder_create(sampling_rate, channels, OAC_FORMAT_AMBISONICS, &err);
    if (err != OAC_OK || !dec) {
        fprintf(stderr, "Error: Failed to create decoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    /* Open output files */
    foac = fopen(oac_out, "wb");
    if (!foac) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", oac_out);
        ret = 1;
        goto cleanup;
    }

    fpcm = fopen(pcm_out, "wb");
    if (!fpcm) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", pcm_out);
        ret = 1;
        goto cleanup;
    }

    /* Encode/decode loop */
    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels) {
            /* Pad last frame with zeros */
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));
        }

        /* Encode */
        int nb_bytes = oac_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", oac_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        /* Write packet size and packet data */
        unsigned char size_bytes[2];
        size_bytes[0] = (nb_bytes >> 8) & 0xFF;
        size_bytes[1] = nb_bytes & 0xFF;
        fwrite(size_bytes, 1, 2, foac);
        fwrite(packet, 1, nb_bytes, foac);
        total_bytes += nb_bytes + 2;

        /* Decode */
        out_samples = oac_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", oac_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fpcm);
        total_samples += out_samples;
    }

    printf("  Encoded %ld bytes (%.2f kbps actual)\n",
           total_bytes,
           total_samples > 0 ? (total_bytes * 8.0 * sampling_rate / total_samples / 1000.0) : 0);
    printf("  Output: %s, %s\n", oac_out, pcm_out);

cleanup:
    if (enc) oac_encoder_destroy(enc);
    if (dec) oac_decoder_destroy(dec);
    if (foac) fclose(foac);
    if (fpcm) fclose(fpcm);
    if (inbuf) free(inbuf);
    if (outbuf) free(outbuf);
    if (packet) free(packet);

    return ret;
}

static int encode_decode_projection(int sampling_rate, int channels, int bitrate,
                                     FILE *fin, const char *oac_out, const char *pcm_out) {
    OacProjectionEncoder *enc = NULL;
    OacProjectionDecoder *dec = NULL;
    FILE *foac = NULL;
    FILE *fpcm = NULL;
    int err;
    int ret = 0;

    short *inbuf = NULL;
    short *outbuf = NULL;
    unsigned char *packet = NULL;
    unsigned char *demixing_matrix = NULL;

    int frame_size = sampling_rate / 50;  /* 20ms frames */
    int in_samples, out_samples;
    int streams, coupled_streams;
    long total_bytes = 0;
    long total_samples = 0;
    oac_int32 matrix_size;

    printf("=== Projection Encoding (Mapping Family 3) ===\n");
    printf("  Sampling rate: %d Hz\n", sampling_rate);
    printf("  Channels: %d\n", channels);
    printf("  Bitrate: %d bps\n", bitrate);
    printf("  Frame size: %d samples\n", frame_size);

    /* Allocate buffers */
    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    /* Create projection encoder (mapping family 3) */
    enc = oac_projection_ambisonics_encoder_create(sampling_rate, channels, 3,
                                                    &streams, &coupled_streams,
                                                    OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || !enc) {
        fprintf(stderr, "Error: Failed to create projection encoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    printf("  Streams: %d (coupled: %d)\n", streams, coupled_streams);

    /* Configure encoder */
    oac_projection_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
    oac_projection_encoder_ctl(enc, OAC_SET_COMPLEXITY(10));

    /* Get demixing matrix for decoder */
    oac_projection_encoder_ctl(enc, OAC_PROJECTION_GET_DEMIXING_MATRIX_SIZE_REQUEST, &matrix_size);
    if (matrix_size <= 0) {
        fprintf(stderr, "Error: Failed to get demixing matrix size\n");
        ret = 1;
        goto cleanup;
    }

    demixing_matrix = (unsigned char *)malloc(matrix_size);
    if (!demixing_matrix) {
        fprintf(stderr, "Error: Failed to allocate demixing matrix\n");
        ret = 1;
        goto cleanup;
    }

    oac_projection_encoder_ctl(enc, OAC_PROJECTION_GET_DEMIXING_MATRIX_REQUEST,
                                demixing_matrix, matrix_size);

    /* Create projection decoder */
    dec = oac_projection_decoder_create(sampling_rate, channels, streams, coupled_streams,
                                         demixing_matrix, matrix_size, &err);
    if (err != OAC_OK || !dec) {
        fprintf(stderr, "Error: Failed to create projection decoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    /* Open output files */
    foac = fopen(oac_out, "wb");
    if (!foac) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", oac_out);
        ret = 1;
        goto cleanup;
    }

    fpcm = fopen(pcm_out, "wb");
    if (!fpcm) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", pcm_out);
        ret = 1;
        goto cleanup;
    }

    /* Write demixing matrix to bitstream file header (for standalone decoding) */
    fwrite(&matrix_size, sizeof(oac_int32), 1, foac);
    fwrite(demixing_matrix, 1, matrix_size, foac);
    fwrite(&streams, sizeof(int), 1, foac);
    fwrite(&coupled_streams, sizeof(int), 1, foac);
    total_bytes += sizeof(oac_int32) + matrix_size + 2 * sizeof(int);

    /* Encode/decode loop */
    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels) {
            /* Pad last frame with zeros */
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));
        }

        /* Encode */
        int nb_bytes = oac_projection_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", oac_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        /* Write packet size and packet data */
        unsigned char size_bytes[2];
        size_bytes[0] = (nb_bytes >> 8) & 0xFF;
        size_bytes[1] = nb_bytes & 0xFF;
        fwrite(size_bytes, 1, 2, foac);
        fwrite(packet, 1, nb_bytes, foac);
        total_bytes += nb_bytes + 2;

        /* Decode */
        out_samples = oac_projection_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", oac_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fpcm);
        total_samples += out_samples;
    }

    printf("  Encoded %ld bytes (%.2f kbps actual)\n",
           total_bytes,
           total_samples > 0 ? (total_bytes * 8.0 * sampling_rate / total_samples / 1000.0) : 0);
    printf("  Output: %s, %s\n", oac_out, pcm_out);

cleanup:
    if (enc) oac_projection_encoder_destroy(enc);
    if (dec) oac_projection_decoder_destroy(dec);
    if (foac) fclose(foac);
    if (fpcm) fclose(fpcm);
    if (inbuf) free(inbuf);
    if (outbuf) free(outbuf);
    if (packet) free(packet);
    if (demixing_matrix) free(demixing_matrix);

    return ret;
}

int main(int argc, char *argv[]) {
    const char *mode;
    int sampling_rate, channels, bitrate;
    const char *input_file, *output_prefix;
    FILE *fin = NULL;
    int ret = 0;
    char oac_out[1024], pcm_out[1024];

    if (argc != 7) {
        print_usage(argv[0]);
        return 1;
    }

    mode = argv[1];
    sampling_rate = atoi(argv[2]);
    channels = atoi(argv[3]);
    bitrate = atoi(argv[4]);
    input_file = argv[5];
    output_prefix = argv[6];

    /* Validate parameters */
    if (strcmp(mode, "semac") != 0 && strcmp(mode, "projection") != 0 && strcmp(mode, "both") != 0) {
        fprintf(stderr, "Error: Invalid mode '%s'\n", mode);
        print_usage(argv[0]);
        return 1;
    }

    if (sampling_rate != 8000 && sampling_rate != 12000 && sampling_rate != 16000 &&
        sampling_rate != 24000 && sampling_rate != 48000) {
        fprintf(stderr, "Error: Unsupported sampling rate %d\n", sampling_rate);
        return 1;
    }

    /* Validate ambisonics channel count: must be (order+1)^2 */
    int valid = 0;
    for (int order = 0; order <= 5; order++) {
        if (channels == (order + 1) * (order + 1)) {
            valid = 1;
            break;
        }
    }
    if (!valid) {
        fprintf(stderr, "Error: Channels must be 1, 4, 9, 16, 25, or 36 for ambisonics\n");
        return 1;
    }

    /* Open input file */
    fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Failed to open input file %s\n", input_file);
        return 1;
    }

    printf("OAC Ambisonics Comparison Tool\n");
    printf("================================\n");
    printf("Input: %s\n", input_file);
    printf("Output prefix: %s\n", output_prefix);
    printf("\n");

    /* Run encoding */
    if (strcmp(mode, "semac") == 0 || strcmp(mode, "both") == 0) {
        snprintf(oac_out, sizeof(oac_out), "%s_semac.oac", output_prefix);
        snprintf(pcm_out, sizeof(pcm_out), "%s_semac.pcm", output_prefix);

        if (encode_decode_semac(sampling_rate, channels, bitrate, fin, oac_out, pcm_out) != 0) {
            fprintf(stderr, "SEMAC encoding failed\n");
            ret = 1;
        }
        printf("\n");
    }

    if (strcmp(mode, "projection") == 0 || strcmp(mode, "both") == 0) {
        snprintf(oac_out, sizeof(oac_out), "%s_projection.oac", output_prefix);
        snprintf(pcm_out, sizeof(pcm_out), "%s_projection.pcm", output_prefix);

        if (encode_decode_projection(sampling_rate, channels, bitrate, fin, oac_out, pcm_out) != 0) {
            fprintf(stderr, "Projection encoding failed\n");
            ret = 1;
        }
        printf("\n");
    }

    fclose(fin);

    if (ret == 0) {
        printf("Comparison complete!\n");
    }

    return ret;
}

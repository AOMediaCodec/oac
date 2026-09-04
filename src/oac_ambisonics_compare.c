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
#include "oac_multistream.h"
#include "oac_projection.h"

#ifndef ENABLE_SEMAC
/* OAC_SEMAC_DOMAIN_* are gated on ENABLE_SEMAC in oac_defines.h; define them here so this
   tool still builds without SEMAC (semac mode then fails at runtime; other modes work). */
#define OAC_SEMAC_DOMAIN_SHD  0
#define OAC_SEMAC_DOMAIN_BEAM 1
#endif

#define MAX_PACKET (1276*36)
#define MAX_FRAME_SIZE 5760  /* 120ms at 48kHz */

/* VBR mode: 1 = variable bitrate (default), 0 = constant (CBR) via -cbr. */
static int g_vbr = 1;

/* Achieved rate, which differs from nominal (packet overhead, CBR rounding, VBR).
   coding_channels counts waveform-coded channels, so kbps_per_ch is the core coder's rate. */
static void report_achieved_rate(long total_bytes, long frames, int frame_size,
                                 int sampling_rate, int coding_channels) {
    double seconds, kbps;
    if (frames <= 0) return;
    seconds = (double)frames * frame_size / sampling_rate;
    kbps = total_bytes * 8.0 / seconds / 1000.0;
    fprintf(stderr, "RATE frames=%ld bytes=%ld total_kbps=%.3f coding_channels=%d kbps_per_ch=%.3f\n",
            frames, total_bytes, kbps, coding_channels, kbps / coding_channels);
}

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s <mode> <sampling_rate> <channels> <bitrate> <input.pcm> <output.pcm> [options]\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Modes:\n");
    fprintf(stderr, "  mapping2   - Opus multistream channel mapping family 2 (standard ambisonics)\n");
    fprintf(stderr, "  mapping3   - Opus multistream channel mapping family 3 (projection/demixing)\n");
    fprintf(stderr, "  semac      - SEMAC (Spatial Energy Matching Ambisonics Coding)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Parameters:\n");
    fprintf(stderr, "  sampling_rate  - Sampling rate in Hz (8000, 12000, 16000, 24000, 48000)\n");
    fprintf(stderr, "  channels       - Number of ambisonics channels (1, 4, 9, 16, 25, 36)\n");
    fprintf(stderr, "  bitrate        - Total bitrate in bits per second\n");
    fprintf(stderr, "  input.pcm      - Input PCM file (16-bit signed, interleaved)\n");
    fprintf(stderr, "  output.pcm     - Output PCM file (16-bit signed, interleaved)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -transport_order <N>  : SEMAC transport order (1-5, default: input order)\n");
    fprintf(stderr, "  -domain <shd|beam>    : SEMAC transport domain (default shd)\n");
    fprintf(stderr, "  -cbr                  : constant bitrate (default: VBR)\n");
}

static int encode_decode_mapping2(int sampling_rate, int channels, int bitrate,
                                   FILE *fin, FILE *fout) {
    OacMSEncoder *enc = NULL;
    OacMSDecoder *dec = NULL;
    int err;
    int ret = 0;

    short *inbuf = NULL;
    short *outbuf = NULL;
    unsigned char *packet = NULL;
    unsigned char mapping[256];
    int streams, coupled_streams;

    int frame_size = sampling_rate / 50;  /* 20ms frames */
    int in_samples;
    long total_bytes = 0, frames = 0;

    fprintf(stderr, "=== Mapping Family 2 (Standard Ambisonics Multistream) ===\n");
    fprintf(stderr, "  Sampling rate: %d Hz\n", sampling_rate);
    fprintf(stderr, "  Channels: %d\n", channels);
    fprintf(stderr, "  Bitrate: %d bps\n", bitrate);

    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    enc = oac_multistream_surround_encoder_create(sampling_rate, channels, 2,
                                                   &streams, &coupled_streams,
                                                   mapping,
                                                   OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || !enc) {
        fprintf(stderr, "Error: Failed to create mapping family 2 encoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    fprintf(stderr, "  Streams: %d (coupled: %d)\n", streams, coupled_streams);

    oac_multistream_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
    oac_multistream_encoder_ctl(enc, OAC_SET_VBR(g_vbr));
    oac_multistream_encoder_ctl(enc, OAC_SET_COMPLEXITY(10));

    dec = oac_multistream_decoder_create(sampling_rate, channels,
                                          streams, coupled_streams,
                                          mapping, &err);
    if (err != OAC_OK || !dec) {
        fprintf(stderr, "Error: Failed to create mapping family 2 decoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels)
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));

        int nb_bytes = oac_multistream_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", oac_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        total_bytes += nb_bytes;
        frames++;

        int out_samples = oac_multistream_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", oac_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fout);
    }

    report_achieved_rate(total_bytes, frames, frame_size, sampling_rate, channels);
    fprintf(stderr, "  Done.\n");

cleanup:
    if (enc) oac_multistream_encoder_destroy(enc);
    if (dec) oac_multistream_decoder_destroy(dec);
    free(inbuf);
    free(outbuf);
    free(packet);
    return ret;
}

static int encode_decode_mapping3(int sampling_rate, int channels, int bitrate,
                                   FILE *fin, FILE *fout) {
    OacProjectionEncoder *enc = NULL;
    OacProjectionDecoder *dec = NULL;
    int err;
    int ret = 0;

    short *inbuf = NULL;
    short *outbuf = NULL;
    unsigned char *packet = NULL;
    unsigned char *demixing_matrix = NULL;

    int frame_size = sampling_rate / 50;
    int in_samples;
    long total_bytes = 0, frames = 0;
    int streams, coupled_streams;
    oac_int32 matrix_size;
    oac_int32 matrix_gain_q8 = 0;

    fprintf(stderr, "=== Mapping Family 3 (Projection) ===\n");
    fprintf(stderr, "  Sampling rate: %d Hz\n", sampling_rate);
    fprintf(stderr, "  Channels: %d\n", channels);
    fprintf(stderr, "  Bitrate: %d bps\n", bitrate);

    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    enc = oac_projection_ambisonics_encoder_create(sampling_rate, channels, 3,
                                                    &streams, &coupled_streams,
                                                    OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || !enc) {
        fprintf(stderr, "Error: Failed to create projection encoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    fprintf(stderr, "  Streams: %d (coupled: %d)\n", streams, coupled_streams);

    oac_projection_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
    oac_projection_encoder_ctl(enc, OAC_SET_VBR(g_vbr));
    oac_projection_encoder_ctl(enc, OAC_SET_COMPLEXITY(10));

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

    dec = oac_projection_decoder_create(sampling_rate, channels, streams, coupled_streams,
                                         demixing_matrix, matrix_size, &err);
    if (err != OAC_OK || !dec) {
        fprintf(stderr, "Error: Failed to create projection decoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    /* Apply demixing-matrix gain as decoder output gain (OpusHead Output Gain equivalent). */
    oac_projection_encoder_ctl(enc, OAC_PROJECTION_GET_DEMIXING_MATRIX_GAIN_REQUEST,
                                &matrix_gain_q8);
    fprintf(stderr, "  Demixing matrix gain: %.4f dB\n", (float)matrix_gain_q8 / 256.f);
    oac_projection_decoder_ctl(dec, OAC_SET_GAIN(matrix_gain_q8));

    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels)
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));

        int nb_bytes = oac_projection_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", oac_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        total_bytes += nb_bytes;
        frames++;

        int out_samples = oac_projection_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", oac_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fout);
    }

    report_achieved_rate(total_bytes, frames, frame_size, sampling_rate, channels);
    fprintf(stderr, "  Done.\n");

cleanup:
    if (enc) oac_projection_encoder_destroy(enc);
    if (dec) oac_projection_decoder_destroy(dec);
    free(inbuf);
    free(outbuf);
    free(packet);
    free(demixing_matrix);
    return ret;
}

static int encode_decode_semac(int sampling_rate, int channels, int bitrate,
                                int transport_order, int transport_domain,
                                FILE *fin, FILE *fout) {
    OacEncoder *enc = NULL;
    OacDecoder *dec = NULL;
    int err;
    int ret = 0;

    short *inbuf = NULL;
    short *outbuf = NULL;
    unsigned char *packet = NULL;

    int frame_size = sampling_rate / 50;
    int in_samples;
    long total_bytes = 0, frames = 0;
    /* transport_order < 0 means "input order", i.e. all input channels are coded. */
    int coding_channels = transport_order < 0 ? channels
        : (transport_order + 1) * (transport_order + 1);
    if (coding_channels > channels) coding_channels = channels;

    fprintf(stderr, "=== SEMAC ===\n");
    fprintf(stderr, "  Sampling rate: %d Hz\n", sampling_rate);
    fprintf(stderr, "  Channels: %d\n", channels);
    fprintf(stderr, "  Bitrate: %d bps\n", bitrate);
    fprintf(stderr, "  Transport order: %d\n", transport_order);
    fprintf(stderr, "  Transport domain: %s\n", transport_domain == OAC_SEMAC_DOMAIN_BEAM ? "beam" : "SHD");

    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    enc = oac_encoder_create(sampling_rate, channels, OAC_FORMAT_AMBISONICS,
                             OAC_APPLICATION_AUDIO, &err);
    if (err != OAC_OK || !enc) {
        fprintf(stderr, "Error: Failed to create SEMAC encoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    oac_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
    oac_encoder_ctl(enc, OAC_SET_VBR(g_vbr));
    oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(10));

#ifdef ENABLE_SEMAC
    oac_encoder_ctl(enc, OAC_SET_ENABLE_SEMAC(1));
    oac_encoder_ctl(enc, OAC_SET_SEMAC_TRANSPORT_ORDER(transport_order));
    oac_encoder_ctl(enc, OAC_SET_SEMAC_TRANSPORT_DOMAIN(transport_domain));
    fprintf(stderr, "  SEMAC: enabled\n");
#else
    fprintf(stderr, "  Warning: SEMAC not enabled in build\n");
#endif

    dec = oac_decoder_create(sampling_rate, channels, OAC_FORMAT_AMBISONICS, &err);
    if (err != OAC_OK || !dec) {
        fprintf(stderr, "Error: Failed to create decoder: %s\n", oac_strerror(err));
        ret = 1;
        goto cleanup;
    }

    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels)
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));

        int nb_bytes = oac_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", oac_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        total_bytes += nb_bytes;
        frames++;

        int out_samples = oac_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", oac_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fout);
    }

    report_achieved_rate(total_bytes, frames, frame_size, sampling_rate, coding_channels);
    fprintf(stderr, "  Done.\n");

cleanup:
    if (enc) oac_encoder_destroy(enc);
    if (dec) oac_decoder_destroy(dec);
    free(inbuf);
    free(outbuf);
    free(packet);
    return ret;
}

int main(int argc, char *argv[]) {
    const char *mode;
    int sampling_rate, channels, bitrate;
    const char *input_file, *output_file;
    FILE *fin = NULL;
    FILE *fout = NULL;
    int ret = 0;
    int transport_order = -1;
    int transport_domain = OAC_SEMAC_DOMAIN_SHD;

    if (argc < 7) {
        print_usage(argv[0]);
        return 1;
    }

    mode = argv[1];
    sampling_rate = atoi(argv[2]);
    channels = atoi(argv[3]);
    bitrate = atoi(argv[4]);
    input_file = argv[5];
    output_file = argv[6];

    /* Parse optional arguments */
    for (int i = 7; i < argc; i++) {
        if (strcmp(argv[i], "-transport_order") == 0 && i + 1 < argc) {
            transport_order = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-cbr") == 0) {
            g_vbr = 0;
        } else if (strcmp(argv[i], "-domain") == 0 && i + 1 < argc) {
            const char *d = argv[++i];
            if (strcmp(d, "shd") == 0) {
                transport_domain = OAC_SEMAC_DOMAIN_SHD;
            } else if (strcmp(d, "beam") == 0) {
                transport_domain = OAC_SEMAC_DOMAIN_BEAM;
            } else {
                fprintf(stderr, "Error: unknown domain '%s' (use shd or beam)\n", d);
                print_usage(argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unrecognized option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Validate mode */
    if (strcmp(mode, "mapping2") != 0 && strcmp(mode, "mapping3") != 0 &&
        strcmp(mode, "semac") != 0) {
        fprintf(stderr, "Error: Invalid mode '%s'\n", mode);
        print_usage(argv[0]);
        return 1;
    }

    /* Validate sampling rate */
    if (sampling_rate != 8000 && sampling_rate != 12000 && sampling_rate != 16000 &&
        sampling_rate != 24000 && sampling_rate != 48000) {
        fprintf(stderr, "Error: Unsupported sampling rate %d\n", sampling_rate);
        return 1;
    }

    /* Validate ambisonics channel count */
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

    fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Failed to open input file %s\n", input_file);
        return 1;
    }

    fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Failed to open output file %s\n", output_file);
        fclose(fin);
        return 1;
    }

    if (strcmp(mode, "mapping2") == 0) {
        ret = encode_decode_mapping2(sampling_rate, channels, bitrate, fin, fout);
    } else if (strcmp(mode, "mapping3") == 0) {
        ret = encode_decode_mapping3(sampling_rate, channels, bitrate, fin, fout);
    } else if (strcmp(mode, "semac") == 0) {
        ret = encode_decode_semac(sampling_rate, channels, bitrate, transport_order, transport_domain, fin, fout);
    }

    fclose(fin);
    fclose(fout);

    return ret;
}

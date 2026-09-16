/* Opus channel mapping family 3 (RFC 8486) encode/decode round-trip against STOCK upstream libopus.
 *
 * Why this exists: oac-private is itself a rename-fork of libopus, so its `oac_ambisonics_compare
 * mapping3` mode is NOT upstream Opus -- and the fork has genuinely diverged, most visibly at 2nd
 * order (commit 4e397d31eb64 rewrote the SOA mixing/demixing matrices and zeroed the demixing gain
 * 3050 -> 0). This tool links the clean v1.6.1 checkout instead, giving the experiment a real
 * upstream baseline to sit next to the fork's variant.
 *
 * It is a deliberate line-by-line port of encode_decode_mapping3() from
 * oac-private/src/oac_ambisonics_compare.c, with oac_/OAC_ renamed to opus_/OPUS_. Every encoder
 * setting is mirrored exactly (frame size Fs/50, APPLICATION_AUDIO, complexity 10, plain SET_VBR
 * with no VBR_CONSTRAINT, the demixing-matrix round-trip plus SET_GAIN). Divergence in any of these
 * would confound the very comparison the tool is meant to support, so keep it in sync.
 *
 * The "RATE ..." stderr line is a fixed contract: run_percc_experiment.sh's rate_row() greps it.
 *
 * Build: ./build_opus_cmf3.sh
 * Usage: opus_cmf3_compare <sampling_rate> <channels> <bitrate> <input.pcm> <output.pcm> [-cbr]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opus.h"
#include "opus_multistream.h"
#include "opus_projection.h"

#define MAX_PACKET (1276 * 36)
#define MAX_FRAME_SIZE 5760 /* 120ms at 48kHz */

/* VBR mode: 1 = variable bitrate (default), 0 = constant (CBR) via -cbr. */
static int g_vbr = 1;

/* Nominal != achieved (packet overhead, CBR rounding, VBR), and the per-channel-rate experiment
   pins the core coder's operating point, so it needs the achieved number. */
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
    fprintf(stderr, "Usage: %s <sampling_rate> <channels> <bitrate> <input.pcm> <output.pcm> [-cbr]\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Opus channel mapping family 3 (projection/demixing) round-trip, stock libopus.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  sampling_rate  - Sampling rate in Hz (8000, 12000, 16000, 24000, 48000)\n");
    fprintf(stderr, "  channels       - Number of ambisonics channels (1, 4, 9, 16, 25, 36)\n");
    fprintf(stderr, "  bitrate        - Total bitrate in bits per second\n");
    fprintf(stderr, "  input.pcm      - Input PCM file (16-bit signed, interleaved)\n");
    fprintf(stderr, "  output.pcm     - Output PCM file (16-bit signed, interleaved)\n");
    fprintf(stderr, "  -cbr           - constant bitrate (default: VBR)\n");
}

static int encode_decode_mapping3(int sampling_rate, int channels, int bitrate,
                                  FILE *fin, FILE *fout) {
    OpusProjectionEncoder *enc = NULL;
    OpusProjectionDecoder *dec = NULL;
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
    opus_int32 matrix_size;
    opus_int32 matrix_gain_q8 = 0;

    fprintf(stderr, "=== Mapping Family 3 (Projection), stock libopus ===\n");
    fprintf(stderr, "  Sampling rate: %d Hz\n", sampling_rate);
    fprintf(stderr, "  Channels: %d\n", channels);
    fprintf(stderr, "  Bitrate: %d bps\n", bitrate);
    fprintf(stderr, "  Rate mode: %s\n", g_vbr ? "VBR" : "CBR");

    inbuf = (short *)malloc(frame_size * channels * sizeof(short));
    outbuf = (short *)malloc(MAX_FRAME_SIZE * channels * sizeof(short));
    packet = (unsigned char *)malloc(MAX_PACKET);

    if (!inbuf || !outbuf || !packet) {
        fprintf(stderr, "Error: Failed to allocate buffers\n");
        ret = 1;
        goto cleanup;
    }

    enc = opus_projection_ambisonics_encoder_create(sampling_rate, channels, 3,
                                                   &streams, &coupled_streams,
                                                   OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !enc) {
        fprintf(stderr, "Error: Failed to create projection encoder: %s\n", opus_strerror(err));
        ret = 1;
        goto cleanup;
    }

    fprintf(stderr, "  Streams: %d (coupled: %d)\n", streams, coupled_streams);

    opus_projection_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
    opus_projection_encoder_ctl(enc, OPUS_SET_VBR(g_vbr));
    opus_projection_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10));

    opus_projection_encoder_ctl(enc, OPUS_PROJECTION_GET_DEMIXING_MATRIX_SIZE_REQUEST, &matrix_size);
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

    opus_projection_encoder_ctl(enc, OPUS_PROJECTION_GET_DEMIXING_MATRIX_REQUEST,
                                demixing_matrix, matrix_size);

    dec = opus_projection_decoder_create(sampling_rate, channels, streams, coupled_streams,
                                        demixing_matrix, matrix_size, &err);
    if (err != OPUS_OK || !dec) {
        fprintf(stderr, "Error: Failed to create projection decoder: %s\n", opus_strerror(err));
        ret = 1;
        goto cleanup;
    }

    /* Apply demixing-matrix gain as decoder output gain (OpusHead Output Gain equivalent). */
    opus_projection_encoder_ctl(enc, OPUS_PROJECTION_GET_DEMIXING_MATRIX_GAIN_REQUEST,
                                &matrix_gain_q8);
    fprintf(stderr, "  Demixing matrix gain: %.4f dB\n", (float)matrix_gain_q8 / 256.f);
    opus_projection_decoder_ctl(dec, OPUS_SET_GAIN(matrix_gain_q8));

    rewind(fin);
    while (1) {
        in_samples = fread(inbuf, sizeof(short), frame_size * channels, fin);
        if (in_samples <= 0) break;

        if (in_samples < frame_size * channels)
            memset(inbuf + in_samples, 0, (frame_size * channels - in_samples) * sizeof(short));

        int nb_bytes = opus_projection_encode(enc, inbuf, frame_size, packet, MAX_PACKET);
        if (nb_bytes < 0) {
            fprintf(stderr, "Error: Encoding failed: %s\n", opus_strerror(nb_bytes));
            ret = 1;
            goto cleanup;
        }

        total_bytes += nb_bytes;
        frames++;

        int out_samples = opus_projection_decode(dec, packet, nb_bytes, outbuf, MAX_FRAME_SIZE, 0);
        if (out_samples < 0) {
            fprintf(stderr, "Error: Decoding failed: %s\n", opus_strerror(out_samples));
            ret = 1;
            goto cleanup;
        }

        fwrite(outbuf, sizeof(short), out_samples * channels, fout);
    }

    report_achieved_rate(total_bytes, frames, frame_size, sampling_rate, channels);
    fprintf(stderr, "  Done.\n");

cleanup:
    if (enc) opus_projection_encoder_destroy(enc);
    if (dec) opus_projection_decoder_destroy(dec);
    free(inbuf);
    free(outbuf);
    free(packet);
    free(demixing_matrix);
    return ret;
}

int main(int argc, char *argv[]) {
    int sampling_rate, channels, bitrate;
    FILE *fin = NULL, *fout = NULL;
    int ret, i;

    if (argc < 6) {
        print_usage(argv[0]);
        return 1;
    }

    sampling_rate = atoi(argv[1]);
    channels = atoi(argv[2]);
    bitrate = atoi(argv[3]);

    for (i = 6; i < argc; i++) {
        if (strcmp(argv[i], "-cbr") == 0) {
            g_vbr = 0;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    fin = fopen(argv[4], "rb");
    if (!fin) {
        fprintf(stderr, "Error: Failed to open input file: %s\n", argv[4]);
        return 1;
    }

    fout = fopen(argv[5], "wb");
    if (!fout) {
        fprintf(stderr, "Error: Failed to open output file: %s\n", argv[5]);
        fclose(fin);
        return 1;
    }

    ret = encode_decode_mapping3(sampling_rate, channels, bitrate, fin, fout);

    fclose(fin);
    fclose(fout);
    return ret;
}

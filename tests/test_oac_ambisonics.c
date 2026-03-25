/* Test ambisonics multi-channel encode/decode */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oac.h"

#define FRAME_SIZE 960
#define MAX_PACKET 4000

/* Valid ambisonics channel counts: (order+1)^2 for orders 1-5 (skip order 0 for now) */
static const int ambisonics_channels[] = {4, 9, 16, 25, 36};
static const int num_ambisonics_configs = 5;

static int test_ambisonics_create(int channels) {
    OacEncoder *enc;
    OacDecoder *dec;
    int err;
    int enc_size, dec_size;

    /* Test encoder creation */
    enc_size = oac_encoder_get_size(channels, OAC_FORMAT_AMBISONICS);
    if (enc_size <= 0) {
        fprintf(stderr, "  oac_encoder_get_size(%d, AMBI) failed\n", channels);
        return 0;
    }

    enc = oac_encoder_create(48000, channels, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_AUDIO, &err);
    if (enc == NULL || err != OAC_OK) {
        fprintf(stderr, "  oac_encoder_create(%d, AMBI) failed: %d\n", channels, err);
        return 0;
    }
    oac_encoder_destroy(enc);

    /* Test decoder creation */
    dec_size = oac_decoder_get_size(channels, OAC_FORMAT_AMBISONICS);
    if (dec_size <= 0) {
        fprintf(stderr, "  oac_decoder_get_size(%d, AMBI) failed\n", channels);
        return 0;
    }

    dec = oac_decoder_create(48000, channels, OAC_FORMAT_AMBISONICS, &err);
    if (dec == NULL || err != OAC_OK) {
        fprintf(stderr, "  oac_decoder_create(%d, AMBI) failed: %d\n", channels, err);
        return 0;
    }
    oac_decoder_destroy(dec);

    return 1;
}

static int test_ambisonics_encode_decode(int channels) {
    OacEncoder *enc;
    OacDecoder *dec;
    int err, ret;
    int i, frame;
    float *pcm_in, *pcm_out;
    unsigned char packet[MAX_PACKET];
    int nbBytes;
    oac_uint32 enc_final_range, dec_final_range;
    int verbose = (channels == 4);  /* Only verbose for 4 channels */

    pcm_in = (float *)malloc(FRAME_SIZE * channels * sizeof(float));
    pcm_out = (float *)malloc(FRAME_SIZE * channels * sizeof(float));
    if (pcm_in == NULL || pcm_out == NULL) {
        free(pcm_in);
        free(pcm_out);
        return 0;
    }

    enc = oac_encoder_create(48000, channels, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_AUDIO, &err);
    if (enc == NULL) {
        free(pcm_in);
        free(pcm_out);
        return 0;
    }

    dec = oac_decoder_create(48000, channels, OAC_FORMAT_AMBISONICS, &err);
    if (dec == NULL) {
        oac_encoder_destroy(enc);
        free(pcm_in);
        free(pcm_out);
        return 0;
    }

    /* Set a reasonable bitrate */
    oac_encoder_ctl(enc, OAC_SET_BITRATE(64000 * channels));

    /* Encode and decode 10 frames */
    for (frame = 0; frame < 10; frame++) {
        /* Generate test signal - simple sine waves on each channel */
        for (i = 0; i < FRAME_SIZE * channels; i++) {
            pcm_in[i] = 0.5f * ((float)(i % 97) / 97.0f - 0.5f);
        }

        nbBytes = oac_encode_float(enc, pcm_in, FRAME_SIZE, packet, MAX_PACKET);
        if (nbBytes < 0) {
            fprintf(stderr, "  oac_encode_float failed: %d\n", nbBytes);
            ret = 0;
            goto cleanup;
        }
        if (verbose && frame == 0) {
            fprintf(stderr, "  Encoded %d bytes\n", nbBytes);
        }

        ret = oac_decode_float(dec, packet, nbBytes, pcm_out, FRAME_SIZE, 0);
        if (ret != FRAME_SIZE) {
            fprintf(stderr, "  oac_decode_float failed: %d (expected %d)\n", ret, FRAME_SIZE);
            ret = 0;
            goto cleanup;
        }

        /* Compare final range encoder/decoder states */
        oac_encoder_ctl(enc, OAC_GET_FINAL_RANGE(&enc_final_range));
        oac_decoder_ctl(dec, OAC_GET_FINAL_RANGE(&dec_final_range));
        if (enc_final_range != dec_final_range) {
            fprintf(stderr, "  Range coder mismatch in frame %d: 0x%08x vs 0x%08x\n",
                    frame, enc_final_range, dec_final_range);
            ret = 0;
            goto cleanup;
        }
    }
    ret = 1;

cleanup:
    oac_encoder_destroy(enc);
    oac_decoder_destroy(dec);
    free(pcm_in);
    free(pcm_out);
    return ret;
}

static int test_format_ctl(void) {
    OacEncoder *enc;
    OacDecoder *dec;
    int err;
    oac_int32 format;

    /* Test encoder OAC_GET_FORMAT */
    enc = oac_encoder_create(48000, 4, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_AUDIO, &err);
    if (enc == NULL) return 0;

    oac_encoder_ctl(enc, OAC_GET_FORMAT(&format));
    if (format != OAC_FORMAT_AMBISONICS) {
        fprintf(stderr, "  Encoder format mismatch: expected %d, got %d\n", OAC_FORMAT_AMBISONICS, format);
        oac_encoder_destroy(enc);
        return 0;
    }
    oac_encoder_destroy(enc);

    /* Test decoder OAC_GET_FORMAT */
    dec = oac_decoder_create(48000, 4, OAC_FORMAT_AMBISONICS, &err);
    if (dec == NULL) return 0;

    oac_decoder_ctl(dec, OAC_GET_FORMAT(&format));
    if (format != OAC_FORMAT_AMBISONICS) {
        fprintf(stderr, "  Decoder format mismatch: expected %d, got %d\n", OAC_FORMAT_AMBISONICS, format);
        oac_decoder_destroy(dec);
        return 0;
    }
    oac_decoder_destroy(dec);

    return 1;
}

static int test_invalid_combinations(void) {
    OacEncoder *enc;
    OacDecoder *dec;
    int err;

    /* Test that 3 channels fails for standard format */
    enc = oac_encoder_create(48000, 3, OAC_FORMAT_STANDARD, OAC_APPLICATION_AUDIO, &err);
    if (enc != NULL) {
        oac_encoder_destroy(enc);
        fprintf(stderr, "  Standard format should not accept 3 channels\n");
        return 0;
    }

    /* Test that 3 channels fails for ambisonics format (not a valid order) */
    enc = oac_encoder_create(48000, 3, OAC_FORMAT_AMBISONICS, OAC_APPLICATION_AUDIO, &err);
    if (enc != NULL) {
        oac_encoder_destroy(enc);
        fprintf(stderr, "  Ambisonics format should not accept 3 channels\n");
        return 0;
    }

    /* Test that 5 channels fails for ambisonics format (not a valid order) */
    dec = oac_decoder_create(48000, 5, OAC_FORMAT_AMBISONICS, &err);
    if (dec != NULL) {
        oac_decoder_destroy(dec);
        fprintf(stderr, "  Ambisonics format should not accept 5 channels\n");
        return 0;
    }

    return 1;
}

int main(void) {
    int i;
    int passed = 0, failed = 0;

    printf("Testing ambisonics multi-channel support\n");
    printf("=========================================\n\n");

    /* Test creation for all valid ambisonics channel counts */
    printf("Testing encoder/decoder creation:\n");
    for (i = 0; i < num_ambisonics_configs; i++) {
        int channels = ambisonics_channels[i];
        printf("  %d channels (order %d)... ", channels, i);
        if (test_ambisonics_create(channels)) {
            printf("OK\n");
            passed++;
        } else {
            printf("FAILED\n");
            failed++;
        }
    }

    /* Test encode/decode for all valid ambisonics channel counts */
    printf("\nTesting encode/decode round-trip:\n");
    for (i = 0; i < num_ambisonics_configs; i++) {
        int channels = ambisonics_channels[i];
        printf("  %d channels (order %d)... ", channels, i);
        if (test_ambisonics_encode_decode(channels)) {
            printf("OK\n");
            passed++;
        } else {
            printf("FAILED\n");
            failed++;
        }
    }

    /* Test OAC_GET_FORMAT CTL */
    printf("\nTesting OAC_GET_FORMAT CTL... ");
    if (test_format_ctl()) {
        printf("OK\n");
        passed++;
    } else {
        printf("FAILED\n");
        failed++;
    }

    /* Test invalid combinations */
    printf("Testing invalid format/channel combinations... ");
    if (test_invalid_combinations()) {
        printf("OK\n");
        passed++;
    } else {
        printf("FAILED\n");
        failed++;
    }

    printf("\n=========================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return failed ? 1 : 0;
}

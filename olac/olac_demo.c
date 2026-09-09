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
#if defined(_WIN32) || defined(WIN32)
# include <fcntl.h>
# include <io.h>
#endif

#define CELT_C /* to make celt_assert work */
#include "oac.h"
#include "olac.h"

#define MAX_PAYLOAD_BYTES 65536
#define MAX_FRAME_SIZE 960
#define MAX_CHANNELS 2
#define OVERLAP_SIZE 120
#define SAMPLING_RATE 48000

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s -e <channels (1/2)> [options] <input.pcm> <output.bit>\n", prog);
    fprintf(stderr, "       %s -d [options] <input.bit> <output.pcm>\n", prog);
    fprintf(stderr, "       %s <channels (1/2)> [options] <input.pcm> <output.pcm>\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -framesize <120|240|480|960> : frame size in samples (default: 960)\n");
    fprintf(stderr, "  -16                          : 16-bit PCM (default for encoder)\n");
    fprintf(stderr, "  -24                          : 24-bit PCM\n");
    fprintf(stderr, "  <input> and <output> can be '-' for stdin/stdout\n");
}

static void int_to_char(oac_uint32 i, unsigned char ch[4]) {
    ch[0] = (unsigned char)((i >> 24) & 0xFF);
    ch[1] = (unsigned char)((i >> 16) & 0xFF);
    ch[2] = (unsigned char)((i >> 8) & 0xFF);
    ch[3] = (unsigned char)(i & 0xFF);
}

static oac_uint32 char_to_int(const unsigned char ch[4]) {
    return ((oac_uint32)ch[0] << 24) |
           ((oac_uint32)ch[1] << 16) |
           ((oac_uint32)ch[2] << 8)  |
           (oac_uint32)ch[3];
}

static unsigned char pack_mode_byte(int channels, int frame_size, int bit_depth) {
    int ch_bit = (channels == 2) ? 1 : 0;
    int fs_bits = 0;
    int bd_bit = (bit_depth == 24) ? 1 : 0;
    if (frame_size == 120) {
        fs_bits = 0;
    } else if (frame_size == 240) {
        fs_bits = 1;
    } else if (frame_size == 480) {
        fs_bits = 2;
    } else if (frame_size == 960) {
        fs_bits = 3;
    }
    return (unsigned char)(ch_bit | (fs_bits << 1) | (bd_bit << 3));
}

static void unpack_mode_byte(unsigned char mode_byte, int *channels, int *frame_size, int *bit_depth) {
    int fs_bits;
    *channels = (mode_byte & 1) ? 2 : 1;
    fs_bits = (mode_byte >> 1) & 3;
    if (fs_bits == 0) {
        *frame_size = 120;
    } else if (fs_bits == 1) {
        *frame_size = 240;
    } else if (fs_bits == 2) {
        *frame_size = 480;
    } else {
        *frame_size = 960;
    }
    *bit_depth = ((mode_byte >> 3) & 1) ? 24 : 16;
}

static void bytes_to_pcm(const unsigned char *fbytes, oac_int32 *pcm, int num_samples, int channels, int bit_depth) {
    int i;
    if (bit_depth == 16) {
        for (i = 0; i < num_samples * channels; i++) {
            oac_uint32 s = ((oac_uint32)fbytes[2 * i + 1] << 8) | (oac_uint32)fbytes[2 * i];
            pcm[i] = (oac_int32)(((s & 0xFFFF) ^ 0x8000) - 0x8000);
        }
    } else {
        for (i = 0; i < num_samples * channels; i++) {
            oac_uint32 s = ((oac_uint32)fbytes[3 * i + 2] << 16) | ((oac_uint32)fbytes[3 * i + 1] << 8) | (oac_uint32)fbytes[3 * i];
            pcm[i] = (oac_int32)(((s & 0xFFFFFF) ^ 0x800000) - 0x800000);
        }
    }
}

static void pcm_to_bytes(const oac_int32 *pcm, unsigned char *fbytes, int num_samples, int channels, int src_bit_depth, int dst_bit_depth) {
    int i;
    if (src_bit_depth == dst_bit_depth) {
        if (dst_bit_depth == 16) {
            for (i = 0; i < num_samples * channels; i++) {
                oac_int32 s = pcm[i];
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                fbytes[2 * i] = (unsigned char)(s & 0xFF);
                fbytes[2 * i + 1] = (unsigned char)(((oac_uint32)s >> 8) & 0xFF);
            }
        } else {
            for (i = 0; i < num_samples * channels; i++) {
                oac_int32 s = pcm[i];
                if (s > 8388607) s = 8388607;
                if (s < -8388608) s = -8388608;
                fbytes[3 * i] = (unsigned char)(s & 0xFF);
                fbytes[3 * i + 1] = (unsigned char)(((oac_uint32)s >> 8) & 0xFF);
                fbytes[3 * i + 2] = (unsigned char)(((oac_uint32)s >> 16) & 0xFF);
            }
        }
    } else if (src_bit_depth == 16 && dst_bit_depth == 24) {
        for (i = 0; i < num_samples * channels; i++) {
            oac_int32 s = (oac_int32)((oac_uint32)pcm[i] << 8);
            if (s > 8388607) s = 8388607;
            if (s < -8388608) s = -8388608;
            fbytes[3 * i] = (unsigned char)(s & 0xFF);
            fbytes[3 * i + 1] = (unsigned char)(((oac_uint32)s >> 8) & 0xFF);
            fbytes[3 * i + 2] = (unsigned char)(((oac_uint32)s >> 16) & 0xFF);
        }
    } else {
        for (i = 0; i < num_samples * channels; i++) {
            oac_int32 s = pcm[i];
            if (s > 0x007fff00) s = 0x007fff00;
            if (s < -0x007fff00) s = -0x007fff00;
            s = (s + 128) >> 8;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            fbytes[2 * i] = (unsigned char)(s & 0xFF);
            fbytes[2 * i + 1] = (unsigned char)(((oac_uint32)s >> 8) & 0xFF);
        }
    }
}

int main(int argc, char *argv[]) {
    int encode_only = 0;
    int decode_only = 0;
    int frame_size = 960;
    int bit_depth = 16;
    int bit_depth_set = 0;
    int channels = 0;
    const char *pos_args[3];
    int pos_count = 0;
    const char *inFile = NULL;
    const char *outFile = NULL;
    FILE *fin = NULL;
    FILE *fout = NULL;
    int ret = EXIT_SUCCESS;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (pos_count < 3) {
                pos_args[pos_count++] = argv[i];
            } else {
                fprintf(stderr, "Too many arguments: %s\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-e") == 0) {
            encode_only = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            decode_only = 1;
        } else if (strcmp(argv[i], "-16") == 0) {
            bit_depth = 16;
            bit_depth_set = 1;
        } else if (strcmp(argv[i], "-24") == 0) {
            bit_depth = 24;
            bit_depth_set = 1;
        } else if (strcmp(argv[i], "-framesize") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -framesize requires an argument\n");
                return EXIT_FAILURE;
            }
            frame_size = atoi(argv[++i]);
            if (frame_size != 120 && frame_size != 240 && frame_size != 480 && frame_size != 960) {
                fprintf(stderr, "Invalid frame size %d: must be 120, 240, 480, or 960\n", frame_size);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else {
            if (pos_count < 3) {
                pos_args[pos_count++] = argv[i];
            } else {
                fprintf(stderr, "Too many arguments: %s\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    if (encode_only && decode_only) {
        fprintf(stderr, "Cannot specify both -e and -d\n");
        return EXIT_FAILURE;
    }

    if (decode_only) {
        if (pos_count != 2) {
            fprintf(stderr, "Error: decode mode requires <input.bit> and <output.pcm>\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        inFile = pos_args[0];
        outFile = pos_args[1];
    } else if (encode_only) {
        if (pos_count != 3) {
            fprintf(stderr, "Error: encode mode requires <channels (1/2)>, <input.pcm>, and <output.bit>\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        channels = atoi(pos_args[0]);
        if (channels != 1 && channels != 2) {
            fprintf(stderr, "Invalid channels %d: must be 1 or 2\n", channels);
            return EXIT_FAILURE;
        }
        inFile = pos_args[1];
        outFile = pos_args[2];
    } else {
        if (pos_count != 3) {
            fprintf(stderr, "Error: loopback mode requires <channels (1/2)>, <input.pcm>, and <output.pcm>\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        channels = atoi(pos_args[0]);
        if (channels != 1 && channels != 2) {
            fprintf(stderr, "Invalid channels %d: must be 1 or 2\n", channels);
            return EXIT_FAILURE;
        }
        inFile = pos_args[1];
        outFile = pos_args[2];
    }

    if (strcmp(inFile, "-") == 0) {
        fin = stdin;
    } else {
        fin = fopen(inFile, "rb");
        if (!fin) {
            fprintf(stderr, "Could not open input file: %s\n", inFile);
            return EXIT_FAILURE;
        }
    }

    if (strcmp(outFile, "-") == 0) {
        fout = stdout;
    } else {
        fout = fopen(outFile, "wb");
        if (!fout) {
            fprintf(stderr, "Could not open output file: %s\n", outFile);
            if (fin != stdin) fclose(fin);
            return EXIT_FAILURE;
        }
    }

#if defined(_WIN32) || defined(WIN32)
    if (fin == stdin) _setmode(_fileno(stdin), _O_BINARY);
    if (fout == stdout) _setmode(_fileno(stdout), _O_BINARY);
#endif

    if (decode_only) {
        OlacDecoder dec;
        int dec_initialized = 0;
        int prev_channels = 0;
        int skip = 120;
        unsigned char hdr[9];
        unsigned char payload[MAX_PAYLOAD_BYTES];
        oac_int32 dec_out[MAX_FRAME_SIZE * MAX_CHANNELS];
        unsigned char fbytes[MAX_FRAME_SIZE * MAX_CHANNELS * 3];

        while (1) {
            size_t num_read = fread(hdr, 1, 9, fin);
            int len;
            oac_uint32 rng;
            unsigned char mode_byte;
            int frame_channels, stream_frame_size, frame_bit_depth;
            int out_bit_depth;
            int valid_samples;
            const oac_int32 *out_ptr;
            int err;

            if (num_read == 0) {
                break;
            }
            if (num_read != 9) {
                fprintf(stderr, "Error: Truncated frame header\n");
                ret = EXIT_FAILURE;
                break;
            }
            len = (int)char_to_int(&hdr[0]);
            rng = char_to_int(&hdr[4]);
            mode_byte = hdr[8];
            unpack_mode_byte(mode_byte, &frame_channels, &stream_frame_size, &frame_bit_depth);
            out_bit_depth = bit_depth_set ? bit_depth : frame_bit_depth;

            if (!dec_initialized || frame_channels != prev_channels) {
                olac_decoder_init(&dec, frame_channels, SAMPLING_RATE);
                dec_initialized = 1;
                prev_channels = frame_channels;
                skip = 120;
            }

            if (len < 0 || len > MAX_PAYLOAD_BYTES) {
                fprintf(stderr, "Error: Invalid payload length %d\n", len);
                ret = EXIT_FAILURE;
                break;
            }

            num_read = fread(payload, 1, len, fin);
            if (num_read != (size_t)len) {
                fprintf(stderr, "Error: Truncated payload: expected %d bytes, got %d\n", len, (int)num_read);
                ret = EXIT_FAILURE;
                break;
            }

            err = olac_decode(&dec, payload, len, dec_out, stream_frame_size);
            if (err != OAC_OK) {
                fprintf(stderr, "Error decoding frame: %d\n", err);
                ret = EXIT_FAILURE;
                break;
            }

            if (rng != dec.rng) {
                fprintf(stderr, "Error: Range coder state mismatch between encoder and decoder: 0x%08x vs 0x%08x\n",
                        rng, dec.rng);
                ret = EXIT_FAILURE;
                break;
            }

            if (stream_frame_size <= skip) {
                skip -= stream_frame_size;
                continue;
            }
            valid_samples = stream_frame_size - skip;
            out_ptr = dec_out + skip * frame_channels;
            skip = 0;

            pcm_to_bytes(out_ptr, fbytes, valid_samples, frame_channels, frame_bit_depth, out_bit_depth);
            if (fwrite(fbytes, (out_bit_depth / 8) * frame_channels, valid_samples, fout) != (size_t)valid_samples) {
                fprintf(stderr, "Error writing output PCM\n");
                ret = EXIT_FAILURE;
                break;
            }
        }
    } else if (encode_only) {
        OlacEncoder enc;
        int bytes_per_sample = channels * (bit_depth / 8);
        unsigned char fbytes[MAX_FRAME_SIZE * MAX_CHANNELS * 3];
        oac_int32 pcm[MAX_FRAME_SIZE * MAX_CHANNELS];
        unsigned char payload[MAX_PAYLOAD_BYTES];
        unsigned char hdr[9];
        int stop = 0;
        int last_frame_had_overlap = 0;
        oac_int64 tot_in = 0;
        oac_int64 tot_payload_bytes = 0;

        olac_encoder_init(&enc, channels, SAMPLING_RATE);

        while (!stop) {
            size_t num_read = fread(fbytes, bytes_per_sample, frame_size, fin);
            int curr_read = (int)num_read;
            int len;
            int j;

            if (curr_read > 0) {
                tot_in += curr_read;
                bytes_to_pcm(fbytes, pcm, curr_read, channels, bit_depth);
                for (j = curr_read * channels; j < frame_size * channels; j++) {
                    pcm[j] = 0;
                }
                len = olac_encode(&enc, pcm, frame_size, payload, MAX_PAYLOAD_BYTES);
                if (len < 0) {
                    fprintf(stderr, "olac_encode() returned %d\n", len);
                    ret = EXIT_FAILURE;
                    break;
                }
                tot_payload_bytes += len;
                int_to_char((oac_uint32)len, &hdr[0]);
                int_to_char(enc.rng, &hdr[4]);
                hdr[8] = pack_mode_byte(channels, frame_size, bit_depth);
                if (fwrite(hdr, 1, 9, fout) != 9 || fwrite(payload, 1, len, fout) != (size_t)len) {
                    fprintf(stderr, "Error writing bitstream\n");
                    ret = EXIT_FAILURE;
                    break;
                }
                if (curr_read < frame_size) {
                    stop = 1;
                    if (curr_read > frame_size - 120) {
                        last_frame_had_overlap = 1;
                    } else {
                        last_frame_had_overlap = 0;
                    }
                } else {
                    last_frame_had_overlap = 1;
                }
            } else {
                stop = 1;
            }
        }

        if (ret == EXIT_SUCCESS && tot_in > 0 && last_frame_had_overlap) {
            int len;
            memset(pcm, 0, frame_size * channels * sizeof(oac_int32));
            len = olac_encode(&enc, pcm, frame_size, payload, MAX_PAYLOAD_BYTES);
            if (len < 0) {
                fprintf(stderr, "olac_encode() returned %d\n", len);
                ret = EXIT_FAILURE;
            } else {
                tot_payload_bytes += len;
                int_to_char((oac_uint32)len, &hdr[0]);
                int_to_char(enc.rng, &hdr[4]);
                hdr[8] = pack_mode_byte(channels, frame_size, bit_depth);
                if (fwrite(hdr, 1, 9, fout) != 9 || fwrite(payload, 1, len, fout) != (size_t)len) {
                    fprintf(stderr, "Error writing bitstream\n");
                    ret = EXIT_FAILURE;
                }
            }
        }

        if (ret == EXIT_SUCCESS && tot_in > 0) {
            double duration_s = (double)tot_in / (double)SAMPLING_RATE;
            double bitrate_kbps = (tot_payload_bytes * 8.0) / duration_s / 1000.0;
            double bits_per_sample = (tot_payload_bytes * 8.0) / ((double)tot_in * channels);
            double uncompressed_bytes = (double)tot_in * channels * (bit_depth / 8.0);
            double compression_ratio = ((double)tot_payload_bytes / uncompressed_bytes) * 100.0;
            fprintf(stderr, "Bitrate: %.3f kb/s (%.3f bits/sample), compression ratio: %.1f%%\n",
                    bitrate_kbps, bits_per_sample, compression_ratio);
        }
    } else {
        OlacEncoder enc;
        OlacDecoder dec;
        int bytes_per_sample = channels * (bit_depth / 8);
        unsigned char fbytes[MAX_FRAME_SIZE * MAX_CHANNELS * 3];
        oac_int32 pcm[MAX_FRAME_SIZE * MAX_CHANNELS];
        oac_int32 dec_out[MAX_FRAME_SIZE * MAX_CHANNELS];
        oac_int32 fifo[2 * MAX_FRAME_SIZE * MAX_CHANNELS];
        unsigned char payload[MAX_PAYLOAD_BYTES];
        int stop = 0;
        int last_frame_had_overlap = 0;
        int skip = 120;
        int fifo_len = 0;
        int verification_failed = 0;
        oac_int64 tot_in = 0;
        oac_int64 tot_out = 0;
        oac_int64 tot_payload_bytes = 0;

        olac_encoder_init(&enc, channels, SAMPLING_RATE);
        olac_decoder_init(&dec, channels, SAMPLING_RATE);

        while (!stop) {
            size_t num_read = fread(fbytes, bytes_per_sample, frame_size, fin);
            int curr_read = (int)num_read;
            int len, err;
            int j;

            if (curr_read > 0) {
                tot_in += curr_read;
                bytes_to_pcm(fbytes, pcm, curr_read, channels, bit_depth);
                memcpy(fifo + fifo_len * channels, pcm, curr_read * channels * sizeof(oac_int32));
                fifo_len += curr_read;
                for (j = curr_read * channels; j < frame_size * channels; j++) {
                    pcm[j] = 0;
                }
                len = olac_encode(&enc, pcm, frame_size, payload, MAX_PAYLOAD_BYTES);
                if (len < 0) {
                    fprintf(stderr, "olac_encode() returned %d\n", len);
                    ret = EXIT_FAILURE;
                    break;
                }
                tot_payload_bytes += len;
                err = olac_decode(&dec, payload, len, dec_out, frame_size);
                if (err != OAC_OK) {
                    fprintf(stderr, "Error decoding frame: %d\n", err);
                    ret = EXIT_FAILURE;
                    verification_failed = 1;
                    break;
                }
                if (dec.rng != enc.rng) {
                    fprintf(stderr, "Error: Range coder state mismatch between encoder and decoder: 0x%08x vs 0x%08x\n",
                            enc.rng, dec.rng);
                    verification_failed = 1;
                }

                if (frame_size <= skip) {
                    skip -= frame_size;
                } else {
                    int valid_samples = frame_size - skip;
                    const oac_int32 *out_ptr = dec_out + skip * channels;
                    skip = 0;
                    if (tot_out + valid_samples > tot_in) {
                        valid_samples = (int)(tot_in - tot_out);
                    }
                    if (valid_samples > 0) {
                        for (j = 0; j < valid_samples * channels; j++) {
                            if (out_ptr[j] != fifo[j]) {
                                verification_failed = 1;
                                break;
                            }
                        }
                        pcm_to_bytes(out_ptr, fbytes, valid_samples, channels, bit_depth, bit_depth);
                        if (fwrite(fbytes, bytes_per_sample, valid_samples, fout) != (size_t)valid_samples) {
                            fprintf(stderr, "Error writing output PCM\n");
                            ret = EXIT_FAILURE;
                            verification_failed = 1;
                            break;
                        }
                        tot_out += valid_samples;
                        fifo_len -= valid_samples;
                        if (fifo_len > 0) {
                            memmove(fifo, fifo + valid_samples * channels, fifo_len * channels * sizeof(oac_int32));
                        }
                    }
                }

                if (curr_read < frame_size) {
                    stop = 1;
                    if (curr_read > frame_size - 120) {
                        last_frame_had_overlap = 1;
                    } else {
                        last_frame_had_overlap = 0;
                    }
                } else {
                    last_frame_had_overlap = 1;
                }
            } else {
                stop = 1;
            }
        }

        if (ret == EXIT_SUCCESS && tot_in > 0 && last_frame_had_overlap) {
            int len, err;
            memset(pcm, 0, frame_size * channels * sizeof(oac_int32));
            len = olac_encode(&enc, pcm, frame_size, payload, MAX_PAYLOAD_BYTES);
            if (len < 0) {
                fprintf(stderr, "olac_encode() returned %d\n", len);
                ret = EXIT_FAILURE;
                verification_failed = 1;
            } else {
                tot_payload_bytes += len;
                err = olac_decode(&dec, payload, len, dec_out, frame_size);
                if (err != OAC_OK) {
                    fprintf(stderr, "Error decoding frame: %d\n", err);
                    ret = EXIT_FAILURE;
                    verification_failed = 1;
                } else {
                    if (dec.rng != enc.rng) {
                        fprintf(stderr, "Error: Range coder state mismatch between encoder and decoder: 0x%08x vs 0x%08x\n",
                                enc.rng, dec.rng);
                        verification_failed = 1;
                    }
                    if (frame_size <= skip) {
                        skip -= frame_size;
                    } else {
                        int valid_samples = frame_size - skip;
                        const oac_int32 *out_ptr = dec_out + skip * channels;
                        int j;
                        skip = 0;
                        if (tot_out + valid_samples > tot_in) {
                            valid_samples = (int)(tot_in - tot_out);
                        }
                        if (valid_samples > 0) {
                            for (j = 0; j < valid_samples * channels; j++) {
                                if (out_ptr[j] != fifo[j]) {
                                    verification_failed = 1;
                                    break;
                                }
                            }
                            pcm_to_bytes(out_ptr, fbytes, valid_samples, channels, bit_depth, bit_depth);
                            if (fwrite(fbytes, bytes_per_sample, valid_samples, fout) != (size_t)valid_samples) {
                                fprintf(stderr, "Error writing output PCM\n");
                                ret = EXIT_FAILURE;
                                verification_failed = 1;
                            } else {
                                tot_out += valid_samples;
                                fifo_len -= valid_samples;
                                if (fifo_len > 0) {
                                    memmove(fifo, fifo + valid_samples * channels, fifo_len * channels * sizeof(oac_int32));
                                }
                            }
                        }
                    }
                }
            }
        }

        if (ret == EXIT_SUCCESS && tot_in > 0) {
            double duration_s = (double)tot_in / (double)SAMPLING_RATE;
            double bitrate_kbps = (tot_payload_bytes * 8.0) / duration_s / 1000.0;
            double bits_per_sample = (tot_payload_bytes * 8.0) / ((double)tot_in * channels);
            double uncompressed_bytes = (double)tot_in * channels * (bit_depth / 8.0);
            double compression_ratio = ((double)tot_payload_bytes / uncompressed_bytes) * 100.0;
            fprintf(stderr, "Bitrate: %.3f kb/s (%.3f bits/sample), compression ratio: %.1f%%\n",
                    bitrate_kbps, bits_per_sample, compression_ratio);
        }

        if (tot_in > 0) {
            if (ret == EXIT_SUCCESS && !verification_failed && tot_out == tot_in && fifo_len == 0) {
                fprintf(stderr, "Verification: PASS (bit-exact)\n");
            } else {
                fprintf(stderr, "Verification: FAIL (mismatch detected)\n");
                ret = EXIT_FAILURE;
            }
        } else {
            if (ret == EXIT_SUCCESS) {
                fprintf(stderr, "Verification: PASS (0 samples)\n");
            } else {
                fprintf(stderr, "Verification: FAIL\n");
            }
        }
    }

    if (fin && fin != stdin) fclose(fin);
    if (fout && fout != stdout) fclose(fout);

    return ret;
}

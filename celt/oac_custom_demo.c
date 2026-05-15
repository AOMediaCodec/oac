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

/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Written by Jean-Marc Valin */
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

#include "oac_custom.h"
#include "arch.h"
#include "modes.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAX_PACKET 1275

static OAC_INLINE void _oac_ctl_failed(const char *file, int line) {
    fprintf(stderr, "\n ***************************************************\n");
    fprintf(stderr, " ***         A fatal error was detected.         ***\n");
    fprintf(stderr, " ***************************************************\n");
    fprintf(stderr, "En/decoder ctl function %s failed at %d for %s.\n",
           file, line, oac_get_version_string());
}

#define oac_ctl_failed() _oac_ctl_failed(__FILE__, __LINE__);

static void print_usage(char **argv) {
    fprintf (stderr, "Usage: %s [-e | -d] <rate> <channels> <frame size> "
                  " [<bytes per packet>] [options] "
                  "<input> <output>\n", argv[0]);
    fprintf (stderr, "     -e encode only (default is encode and decode)\n");
    fprintf (stderr, "     -d decode only (default is encode and decode)\n");
    fprintf (stderr, "     <bytes per packet>: required only when encoding\n");
    fprintf (stderr, "options:\n");
    fprintf (stderr, "     -16                      format is 16-bit little-endian (default)\n");
    fprintf (stderr, "     -24                      format is 24-bit little-endian\n");
    fprintf (stderr, "     -f32                     format is 32-bit float little-endian\n");
    fprintf (stderr, "     -complexity <0-10>       optional only when encoding\n");
    fprintf (stderr, "     -loss <percentage>       encoding (robsutness setting) and decoding (simulating loss)\n");
}

static void int_to_char(oac_uint32 i, unsigned char ch[4]) {
    ch[0] = i>>24;
    ch[1] = (i>>16)&0xFF;
    ch[2] = (i>>8)&0xFF;
    ch[3] = i&0xFF;
}

static oac_uint32 char_to_int(unsigned char ch[4]) {
    return ((oac_uint32)ch[0]<<24)|((oac_uint32)ch[1]<<16)
           |((oac_uint32)ch[2]<<8)|(oac_uint32)ch[3];
}

#define check_encoder_option(decode_only, \
                             opt) do {if (decode_only) {fprintf(stderr, "option %s is only for encoding\n", opt); \
                                                        goto failure;}} while (0)
#define check_decoder_option(encode_only, \
                             opt) do {if (encode_only) {fprintf(stderr, "option %s is only for decoding\n", opt); \
                                                        goto failure;}} while (0)

#define FORMAT_S16_LE 0
#define FORMAT_S24_LE 1
#define FORMAT_F32_LE 2

static const int format_size[3] = {2, 3, 4};

typedef union {
    oac_int32 i;
    float f;
} float_bits;


int main(int argc, char *argv[]) {
    int err;
    int ret = 1;
    int args;
    oac_uint32 enc_final_range;
    oac_uint32 dec_final_range;
    int encode_only = 0, decode_only = 0;
    char *inFile, *outFile;
    FILE *fin = NULL, *fout = NULL;
    OacCustomMode *mode = NULL;
    OacCustomEncoder *enc = NULL;
    OacCustomDecoder *dec = NULL;
    int len;
    oac_int32 frame_size, channels, rate;
    int format = FORMAT_S16_LE;
    int bytes_per_packet = 0;
    unsigned char data[MAX_PACKET];
    int complexity = -1;
    float percent_loss = -1;
    int i;
#if !(defined (FIXED_POINT) && !defined(CUSTOM_MODES)) && defined(RESYNTH)
    double rmsd = 0;
#endif
    int count = 0;
    oac_int32 skip;
    oac_int32 *in = NULL, *out = NULL;
    unsigned char *fbytes = NULL;
    args = 1;
    if (argc < 7) {
        print_usage(argv);
        goto failure;
    }
    if (strcmp(argv[args], "-e") == 0) {
        encode_only = 1;
        args++;
    } else if (strcmp(argv[args], "-d") == 0) {
        decode_only = 1;
        args++;
    }

    rate = (oac_int32)atol(argv[args]);
    args++;

    if (rate != 8000 && rate != 12000
        && rate != 16000 && rate != 24000
        && rate != 48000
#ifdef ENABLE_QEXT
        && rate != 96000
#endif
        ) {
        fprintf(stderr, "Supported sampling rates are 8000, 12000, 16000, 24000"
#ifdef ENABLE_QEXT
               ", 48000 and 96000.\n");
#else
               " and 48000.\n");
#endif
        goto failure;
    }

    channels = atoi(argv[args]);
    args++;

    if (channels < 1 || channels > 2) {
        fprintf(stderr, "Oac_demo supports only 1 or 2 channels.\n");
        goto failure;
    }

    frame_size = atoi(argv[args]);
    args++;

    if (!decode_only) {
        bytes_per_packet = (oac_int32)atol(argv[args]);
        args++;
        if (bytes_per_packet < 0 || bytes_per_packet > MAX_PACKET) {
            fprintf (stderr, "bytes per packet must be between 0 and %d\n",
                           MAX_PACKET);
            goto failure;
        }
    }

    mode = oac_custom_mode_create(rate, frame_size, NULL);
    if (mode == NULL) {
        fprintf(stderr, "failed to create a mode\n");
        goto failure;
    }
    while (args < argc - 2) {
        /* process command line options */
        if (strcmp( argv[ args ], "-complexity" ) == 0) {
            check_encoder_option(decode_only, "-complexity");
            args++;
            complexity = atoi(argv[args]);
            args++;
        } else if (strcmp( argv[ args ], "-loss" ) == 0) {
            args++;
            percent_loss = atof(argv[args]);
            args++;
        } else if (strcmp( argv[ args ], "-16" ) == 0) {
            format = FORMAT_S16_LE;
            args++;
        } else if (strcmp( argv[ args ], "-24" ) == 0) {
            format = FORMAT_S24_LE;
            args++;
        } else if (strcmp( argv[ args ], "-f32" ) == 0) {
            format = FORMAT_F32_LE;
            args++;
        } else {
            printf( "Error: unrecognized setting: %s\n\n", argv[ args ] );
            print_usage( argv );
            goto failure;
        }
    }
    if (!decode_only) {
        enc = oac_custom_encoder_create(mode, channels, OAC_FORMAT_STANDARD, &err);
        if (err != 0) {
            fprintf(stderr, "Failed to create the encoder: %s\n", oac_strerror(err));
            goto failure;
        }
        if (complexity >= 0) {
            if (oac_custom_encoder_ctl(
                  enc, OAC_SET_COMPLEXITY(complexity)) != OAC_OK) {
                oac_ctl_failed();
                goto failure;
            }
        }
        if (percent_loss >= 0) {
            if (oac_custom_encoder_ctl(
                  enc, OAC_SET_PACKET_LOSS_PERC((int)percent_loss)) !=
                OAC_OK) {
                oac_ctl_failed();
                goto failure;
            }
        }
    }
    if (!encode_only) {
        dec = oac_custom_decoder_create(mode, channels, OAC_FORMAT_STANDARD, &err);
        if (err != 0) {
            fprintf(stderr, "Failed to create the decoder: %s\n", oac_strerror(err));
            goto failure;
        }
        if (oac_custom_decoder_ctl(dec, OAC_GET_LOOKAHEAD(&skip)) != OAC_OK) {
            oac_ctl_failed();
            goto failure;
        }
    }
    if (argc - args != 2) {
        print_usage(argv);
        goto failure;
    }
    inFile = argv[argc - 2];
    fin = fopen(inFile, "rb");
    if (!fin) {
        fprintf (stderr, "Could not open input file %s\n", argv[argc - 2]);
        goto failure;
    }
    outFile = argv[argc - 1];
    fout = fopen(outFile, "wb+");
    if (!fout) {
        fprintf (stderr, "Could not open output file %s\n", argv[argc - 1]);
        goto failure;
    }
    in = (oac_int32*)malloc(frame_size*channels*sizeof(oac_int32));
    out = (oac_int32*)malloc(frame_size*channels*sizeof(oac_int32));
    fbytes = (unsigned char*)malloc(frame_size*channels*4);

    while (!feof(fin)) {
        int lost = 0;
        if (decode_only) {
            unsigned char ch[4];
            size_t num_read = fread(ch, 1, 4, fin);
            if (num_read != 4)
                break;
            len = char_to_int(ch);
            if (len > MAX_PACKET || len < 0) {
                fprintf(stderr, "Invalid payload length: %d\n", len);
                break;
            }
            num_read = fread(ch, 1, 4, fin);
            if (num_read != 4)
                break;
            enc_final_range = char_to_int(ch);
            num_read = fread(data, 1, len, fin);
            if (num_read != (size_t)len) {
                fprintf(stderr, "Ran out of input, "
                              "expecting %d bytes got %d\n",
                              len, (int)num_read);
                break;
            }
        } else {
            err = fread(fbytes, format_size[format], frame_size*channels, fin);
            if (feof(fin))
                break;
            if (format == FORMAT_S16_LE) {
                for (i = 0; i < frame_size*channels; i++) {
                    oac_int32 s;
                    s = fbytes[2*i + 1]<<8|fbytes[2*i];
                    s = ((s&0xFFFF)^0x8000) - 0x8000;
                    in[i] = s*256;
                }
            } else if (format == FORMAT_S24_LE) {
                for (i = 0; i < frame_size*channels; i++) {
                    oac_int32 s;
                    s = fbytes[3*i + 2]<<16|fbytes[3*i + 1]<<8|fbytes[3*i];
                    s = ((s&0xFFFFFF)^0x800000) - 0x800000;
                    in[i] = s;
                }
            } else if (format == FORMAT_F32_LE) {
                for (i = 0; i < frame_size*channels; i++) {
                    float_bits s;
                    s.i = (unsigned)fbytes[4*i + 3]<<24|fbytes[4*i + 2]<<16|fbytes[4*i + 1]<<8|fbytes[4*i];
                    in[i] = (int)floor(.5 + s.f*8388608);
                }
            }
            len = oac_custom_encode24(enc, in, frame_size, data, bytes_per_packet);
            if (oac_custom_encoder_ctl(
                   enc, OAC_GET_FINAL_RANGE(&enc_final_range)) != OAC_OK) {
                oac_ctl_failed();
                goto failure;
            }
            if (len <= 0)
                fprintf (stderr, "oac_custom_encode() failed: %s\n", oac_strerror(len));
        }

        if (encode_only) {
            unsigned char int_field[4];
            int_to_char(len, int_field);
            if (fwrite(int_field, 1, 4, fout) != 4) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
            int_to_char(enc_final_range, int_field);
            if (fwrite(int_field, 1, 4, fout) != 4) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
            if (fwrite(data, 1, len, fout) != (unsigned)len) {
                fprintf(stderr, "Error writing.\n");
                goto failure;
            }
        } else {
            /* This is for simulating bit errors */
#if 0
            int errors = 0;
            int eid = 0;
            /* This simulates random bit error */
            for (i = 0; i < len*8; i++) {
                if (rand()%atoi(argv[8]) == 0) {
                    if (i < 64) {
                        errors++;
                        eid = i;
                    }
                    data[i/8] ^= 1<<(7 - (i%8));
                }
            }
            if (errors == 1)
                data[eid/8] ^= 1<<(7 - (eid%8));
            else if (errors%2 == 1)
                data[rand()%8] ^= 1<<rand()%8;
#endif

#if 1 /* Set to zero to use the encoder's output instead */
            /* This is to simulate packet loss */
            lost = percent_loss != 0 && (float)rand()/(float)RAND_MAX < .01*percent_loss;
            if (lost)
                /*if (errors && (errors%2==0))*/
                ret = oac_custom_decode24(dec, NULL, len, out, frame_size);
            else
                ret = oac_custom_decode24(dec, data, len, out, frame_size);
            if (oac_custom_decoder_ctl(
                  dec, OAC_GET_FINAL_RANGE(&dec_final_range)) != OAC_OK) {
                oac_ctl_failed();
                goto failure;
            }
            if (ret < 0)
                fprintf(stderr, "oac_custom_decode() failed: %s\n", oac_strerror(ret));
#else
            for (i = 0; i < ret*channels; i++)
                out[i] = in[i];
#endif
#if !(defined (FIXED_POINT) && !defined(CUSTOM_MODES)) && defined(RESYNTH)
            if (!encode_only && !decode_only) {
                for (i = 0; i < ret*channels; i++) {
                    rmsd += (in[i] - out[i])*1.0*(in[i] - out[i]);
                    /*out[i] -= in[i];*/
                }
            }
#endif
            if (format == FORMAT_S16_LE) {
                for (i = 0; i < (ret - skip)*channels; i++) {
                    oac_int32 s;
                    s = out[i + (skip*channels)];
                    if (s > 0x007fff00) s = 0x007fff00;
                    if (s < -0x007fff00) s = -0x007fff00;
                    s = (s + 128)>>8;
                    fbytes[2*i] = s&0xFF;
                    fbytes[2*i + 1] = (s>>8)&0xFF;
                }
            } else if (format == FORMAT_S24_LE) {
                for (i = 0; i < (ret - skip)*channels; i++) {
                    oac_int32 s;
                    s = out[i + (skip*channels)];
                    if (s > 0x007fffff) s = 0x007fffff;
                    if (s < -0x007fffff) s = -0x007fffff;
                    fbytes[3*i] = s&0xFF;
                    fbytes[3*i + 1] = (s>>8)&0xFF;
                    fbytes[3*i + 2] = (s>>16)&0xFF;
                }
            } else if (format == FORMAT_F32_LE) {
                for (i = 0; i < (ret - skip)*channels; i++) {
                    float_bits s;
                    s.f = out[i + (skip*channels)]*(1.f/8388608.f);
                    fbytes[4*i] = s.i&0xFF;
                    fbytes[4*i + 1] = (s.i>>8)&0xFF;
                    fbytes[4*i + 2] = (s.i>>16)&0xFF;
                    fbytes[4*i + 3] = (s.i>>24)&0xFF;
                }
            }
            fwrite(fbytes, format_size[format], (ret - skip)*channels, fout);
        }

        /* compare final range encoder rng values of encoder and decoder */
        if (enc_final_range != 0  && !encode_only
            && !lost
            && dec_final_range != enc_final_range) {
            fprintf (stderr, "Error: Range coder state mismatch "
                           "between encoder and decoder "
                           "in frame %ld: 0x%8lx vs 0x%8lx\n",
                (long)count,
                (unsigned long)enc_final_range,
                (unsigned long)dec_final_range);
            goto failure;
        }

        count++;
        skip = 0;
    }
    PRINT_MIPS(stderr);
    ret = EXIT_SUCCESS;
#if !(defined (FIXED_POINT) && !defined(CUSTOM_MODES)) && defined(RESYNTH)
    if (!encode_only && !decode_only) {
        if (rmsd > 0) {
            rmsd = sqrt(rmsd/(1.0*frame_size*channels*count));
            fprintf (stderr, "Error: encoder doesn't match decoder\n");
            fprintf (stderr, "RMS mismatch is %f\n", rmsd);
            ret = 1;
        } else {
            fprintf (stderr, "Encoder matches decoder!!\n");
        }
    }
#endif
failure:
    /* Cleanup after ourselves. */
    if (enc) oac_custom_encoder_destroy(enc);
    if (dec) oac_custom_decoder_destroy(dec);
    if (fin) fclose(fin);
    if (fout) fclose(fout);
    if (mode) oac_custom_mode_destroy(mode);
    if (in) free(in);
    if (out) free(out);
    if (fbytes) free(fbytes);
    return ret;
}

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

/* Copyright (c) 2011 Xiph.Org Foundation, Skype Limited
   Copyright (c) 2024 Arm Limited
   Written by Jean-Marc Valin and Koen Vos */
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

#include "oac.h"
#include "celt.h"
#include "mathops.h"
#include "oac_private.h"

#ifndef DISABLE_FLOAT_API

void oac_pcm_soft_clip_impl(float *_x, int N, int C, float *declip_mem, int arch) {
    int c;
    int i;
    float *x;
    int all_within_neg1pos1;

    if (C < 1 || N < 1 || !_x || !declip_mem) return;

    /* Clamp everything within the range [-2, +2] which is the domain of the soft
       clipping non-linearity. Outside the defined range the derivative will be zero,
       therefore there is no discontinuity introduced here. The implementation
       might provide a hint if all input samples are within the [-1, +1] range.

       `oac_limit2_checkwithin1()`:
       - Clamps all samples within the valid range [-2, +2].
       - Generic C implementation:
     * Does not attempt early detection whether samples are within hinted range.
     * Always returns 0.
       - Architecture specific implementation:
     * Uses SIMD instructions to efficiently detect if all samples are
            within the hinted range [-1, +1].
     * Returns 1 if no samples exceed the hinted range, 0 otherwise.

       `all_within_neg1pos1`:
       - Optimization hint to skip per-sample out-of-bound checks.
         If true, the check can be skipped. */
    all_within_neg1pos1 = oac_limit2_checkwithin1(_x, N*C, arch);

    for (c = 0; c < C; c++) {
        float a;
        float x0;
        int curr;

        x = _x + c;
        a = declip_mem[c];
        /* Continue applying the non-linearity from the previous frame to avoid
           any discontinuity. */
        for (i = 0; i < N; i++) {
            if (x[i*C]*a >= 0)
                break;
            x[i*C] = x[i*C] + a*x[i*C]*x[i*C];
        }

        curr = 0;
        x0 = x[0];
        while (1) {
            int start, end;
            float maxval;
            int special = 0;
            int peak_pos;
            /* Detection for early exit can be skipped if hinted by `all_within_neg1pos1` */
            if (all_within_neg1pos1) {
                i = N;
            } else {
                for (i = curr; i < N; i++) {
                    if (x[i*C] > 1 || x[i*C] < -1)
                        break;
                }
            }
            if (i == N) {
                a = 0;
                break;
            }
            peak_pos = i;
            start = end = i;
            maxval = ABS16(x[i*C]);
            /* Look for first zero crossing before clipping */
            while (start > 0 && x[i*C]*x[(start - 1)*C] >= 0)
                start--;
            /* Look for first zero crossing after clipping */
            while (end < N && x[i*C]*x[end*C] >= 0) {
                /* Look for other peaks until the next zero-crossing. */
                if (ABS16(x[end*C]) > maxval) {
                    maxval = ABS16(x[end*C]);
                    peak_pos = end;
                }
                end++;
            }
            /* Detect the special case where we clip before the first zero crossing */
            special = (start == 0 && x[i*C]*x[0] >= 0);

            /* Compute a such that maxval + a*maxval^2 = 1 */
            a = (maxval - 1)/(maxval*maxval);
            /* Slightly boost "a" by 2^-22. This is just enough to ensure -ffast-math
               does not cause output values larger than +/-1, but small enough not
               to matter even for 24-bit output.  */
            a += a*2.4e-7f;
            if (x[i*C] > 0)
                a = -a;
            /* Apply soft clipping */
            for (i = start; i < end; i++)
                x[i*C] = x[i*C] + a*x[i*C]*x[i*C];

            if (special && peak_pos >= 2) {
                /* Add a linear ramp from the first sample to the signal peak.
                   This avoids a discontinuity at the beginning of the frame. */
                float delta;
                float offset = x0 - x[0];
                delta = offset/peak_pos;
                for (i = curr; i < peak_pos; i++) {
                    offset -= delta;
                    x[i*C] += offset;
                    x[i*C] = MAX16(-1.f, MIN16(1.f, x[i*C]));
                }
            }
            curr = end;
            if (curr == N)
                break;
        }
        declip_mem[c] = a;
    }
}

OAC_EXPORT void oac_pcm_soft_clip(float *_x, int N, int C, float *declip_mem) {
    oac_pcm_soft_clip_impl(_x, N, C, declip_mem, 0);
}

#endif

/* Frame length signalling. See OAC_SIZE_MAX in oac_defines.h for the format.
   The mapping is bijective, so there is no non-canonical encoding to reject. */
int oaci_encode_size(oac_int32 size, unsigned char *data) {
    celt_assert(size >= 0 && size <= OAC_SIZE_MAX);
    if (size < 192) {
        data[0] = size;
        return 1;
    } else if (size < 8384) {
        oac_int32 v = size - 192;           /* 13 bits */
        data[0] = 192 + (v&0x1F);
        data[1] = v>>5;
        return 2;
    } else {
        oac_int32 v = size - 8384;          /* 21 bits */
        data[0] = 224 + (v&0x1F);
        v >>= 5;
        data[1] = v&0xFF;
        data[2] = v>>8;
        return 3;
    }
}

static int oaci_parse_size(const unsigned char *data, oac_int32 len, oac_int32 *size) {
    if (len < 1) {
        *size = -1;
        return -1;
    } else if (data[0] < 192) {
        *size = data[0];
        return 1;
    } else if (data[0] < 224) {
        if (len < 2) {
            *size = -1;
            return -1;
        }
        *size = 32*(oac_int32)data[1] + data[0];
        return 2;
    } else {
        if (len < 3) {
            *size = -1;
            return -1;
        }
        *size = 32*(256*(oac_int32)data[2] + data[1]) + data[0] + 8160;
        return 3;
    }
}

/* The eight legal frame/packet durations, in units of 2.5 ms. Everything that
   needs to know the duration list -- the OAC_FRAMESIZE_* enum, the ToC F field
   and oaci_frame_size_select() -- goes through this table. */
const unsigned char oaci_frame_dur[OAC_NB_FRAME_DURATIONS] = {1, 2, 4, 8, 16, 24, 32, 48};

int oaci_dur_index(int samples_400) {
    int i;
    for (i = 0; i < OAC_NB_FRAME_DURATIONS; i++) {
        if (oaci_frame_dur[i] == samples_400)
            return i;
    }
    return -1;
}

int oaci_F_to_frames(int dur_index, int F) {
    int base, total;
    if (dur_index < 0 || F < 0 || dur_index + F > OAC_NB_FRAME_DURATIONS - 1)
        return -1;
    base = oaci_frame_dur[dur_index];
    total = oaci_frame_dur[dur_index + F];
    /* The packet duration has to be an integer number of frames. It is not for
       e.g. a 40 ms frame in a 60 ms packet, and those combinations are simply
       invalid: the encoder never produces them and the decoder rejects them. */
    if (total%base != 0)
        return -1;
    return total/base;
}

int oaci_frames_to_F(int dur_index, int nb_frames) {
    int total, total_index;
    if (dur_index < 0 || dur_index >= OAC_NB_FRAME_DURATIONS || nb_frames < 1)
        return -1;
    /* The packet duration is the frame duration times the frame count, and it
       has to land exactly on another entry of the table at or after the base.
       Anything else is a frame count the F field simply cannot express. */
    total = oaci_frame_dur[dur_index]*nb_frames;
    total_index = oaci_dur_index(total);
    if (total_index < dur_index)
        return -1;
    return total_index - dur_index;
}

/* Resolve the ambisonics order for a channel count, or -1 if the count is not
   of the form (order+1)^2. */
static int oaci_ambisonics_order(int channels) {
    int order = 0;
    while ((order + 1)*(order + 1) < channels)
        order++;
    if ((order + 1)*(order + 1) != channels)
        return -1;
    return order;
}

int oaci_toc_bytes(int nb_frames, int format, int channels) {
    /* A=1 always needs the extended byte, and the ambisonics order reaches 15
       within the C/S fields so it never needs the escape byte. */
    if (format == OAC_FORMAT_AMBISONICS)
        return 2;
    /* C=7,S=1 escapes to one more byte holding (channels-1). */
    if (channels > 15)
        return 3;
    /* Otherwise the extended byte is only needed for a non-zero C or F. */
    if (nb_frames > 1 || channels > 2)
        return 2;
    return 1;
}

int oaci_write_toc(unsigned char base_toc, int nb_frames, int vbr, int padding,
                   int format, int channels, int samples_400, unsigned char *data) {
    int S, A, C, F;
    int escape = 0;
    int nb_bytes;

    F = oaci_frames_to_F(oaci_dur_index(samples_400), nb_frames);
    if (F < 0)
        return OAC_BAD_ARG;
    if (format == OAC_FORMAT_AMBISONICS) {
        int order = oaci_ambisonics_order(channels);
        if (order < 0 || order > 15)
            return OAC_BAD_ARG;
        A = 1;
        S = order&0x01;
        C = order>>1;
    } else {
        if (channels < 1 || channels > OAC_MAX_CHANNELS)
            return OAC_BAD_ARG;
        A = 0;
        if (channels > 15) {
            S = 1;
            C = 7;
            escape = channels - 1;
        } else {
            S = (channels - 1)&0x01;
            C = (channels - 1)>>1;
        }
    }
    nb_bytes = oaci_toc_bytes(nb_frames, format, channels);
    /* Bits 0-4 of base_toc are the mode/bandwidth/duration; S, X and P are ours. */
    data[0] = (unsigned char)((base_toc&0xF8) | (S<<2) | (nb_bytes > 1 ? 0x02 : 0)
                              | (padding ? 0x01 : 0));
    if (nb_bytes > 1)
        data[1] = (unsigned char)((vbr ? 0x80 : 0) | (F<<4) | (A<<3) | C);
    if (nb_bytes > 2)
        data[2] = (unsigned char)escape;
    return nb_bytes;
}

int oaci_validate_config(int mode, int format, int channels, int nb_frames) {
    if (channels < 1 || channels > OAC_MAX_CHANNELS)
        return OAC_INVALID_PACKET;
    if (format == OAC_FORMAT_AMBISONICS && oaci_ambisonics_order(channels) < 0)
        return OAC_INVALID_PACKET;
    if (nb_frames < 1 || nb_frames > OAC_MAX_FRAMES_PER_PACKET)
        return OAC_INVALID_PACKET;
    /* SILK and hybrid only ever code one or two channels. The two modes are
       listed explicitly rather than as "anything but CELT" so that the modes
       still to come (lossless, other speech modes) have to state their own
       policy here instead of silently inheriting this one. */
    if ((mode == MODE_SILK_ONLY || mode == MODE_HYBRID) && channels > 2)
        return OAC_INVALID_PACKET;
    return OAC_OK;
}

/* The three functions below read the main ToC byte and nothing else. They do
   no validation and never look at the rest of the packet, which is what lets
   the encoder call them on a ToC byte it is still in the middle of building.
   The public oac_packet_get_*() entry points are validating wrappers. */

int oaci_toc_mode(unsigned char toc) {
    int mode;
    if (toc&0x80) {
        mode = MODE_CELT_ONLY;
    } else if ((toc&0x60) == 0x60) {
        mode = MODE_HYBRID;
    } else {
        mode = MODE_SILK_ONLY;
    }
    return mode;
}

int oaci_toc_bandwidth(unsigned char toc) {
    int bandwidth;
    if (toc&0x80) {
        bandwidth = OAC_BANDWIDTH_MEDIUMBAND + ((toc>>5)&0x3);
        if (bandwidth == OAC_BANDWIDTH_MEDIUMBAND)
            bandwidth = OAC_BANDWIDTH_NARROWBAND;
    } else if ((toc&0x60) == 0x60) {
        bandwidth = (toc&0x10) ? OAC_BANDWIDTH_FULLBAND :
                    OAC_BANDWIDTH_SUPERWIDEBAND;
    } else {
        bandwidth = OAC_BANDWIDTH_NARROWBAND + ((toc>>5)&0x3);
    }
    return bandwidth;
}

int oaci_toc_samples_per_frame(unsigned char toc, oac_int32 Fs) {
    int audiosize;
    if (toc&0x80) {
        audiosize = ((toc>>3)&0x3);
        audiosize = (Fs<<audiosize)/400;
    } else if ((toc&0x60) == 0x60) {
        audiosize = (toc&0x08) ? Fs/50 : Fs/100;
    } else {
        audiosize = ((toc>>3)&0x3);
        if (audiosize == 3)
            audiosize = Fs*60/1000;
        else
            audiosize = (Fs<<audiosize)/100;
    }
    return audiosize;
}

int oaci_packet_parse_toc(const unsigned char *data, oac_int32 len,
                          int *out_format, int *out_channels,
                          int *out_nb_frames, int *out_hdr_bytes) {
    unsigned char toc;
    int S, dur_index;
    int format, channels, count;
    int hdr_bytes;

    if (len < 1)
        return OAC_BAD_ARG;

    /* Asking for the frame size at 400 Hz gives us the duration directly in
       units of 2.5 ms, which is what the F field increments over. */
    toc = data[0];
    dur_index = oaci_dur_index(oaci_toc_samples_per_frame(toc, 400));
    if (dur_index < 0)
        return OAC_INVALID_PACKET;
    S = (toc>>2)&0x01;

    /* X (0x02) says an extended ToC byte follows. Without it the packet holds
       exactly one frame of S+1 channels in the standard format. */
    if (toc&0x02) {
        unsigned char ext;
        int F, A, C;
        if (len < 2)
            return OAC_INVALID_PACKET;
        ext = data[1];
        F = (ext>>4)&0x07;
        A = ext&0x08;
        C = ext&0x07;
        hdr_bytes = 2;
        count = oaci_F_to_frames(dur_index, F);
        if (count < 0)
            return OAC_INVALID_PACKET;
        if (A) {
            int order = 2*C + S;
            format = OAC_FORMAT_AMBISONICS;
            channels = (order + 1)*(order + 1);
        } else {
            format = OAC_FORMAT_STANDARD;
            if (C == 7 && S == 1) {
                /* Escape to an explicit channel count in a third ToC byte. */
                if (len < 3)
                    return OAC_INVALID_PACKET;
                channels = data[2] + 1;
                hdr_bytes = 3;
            } else {
                channels = 2*C + S + 1;
            }
        }
    } else {
        hdr_bytes = 1;
        count = 1;
        format = OAC_FORMAT_STANDARD;
        channels = S + 1;
    }

    /* Everything above is pure bit unpacking; this is the one place that
       decides whether the resulting configuration is one we accept. Every
       public accessor comes through here, so none of them can report a
       property of a packet that oac_decode() would reject. */
    if (oaci_validate_config(oaci_toc_mode(toc), format, channels, count) != OAC_OK)
        return OAC_INVALID_PACKET;
    /* Guaranteed by the duration table: dur_index+F never runs past 120 ms. */
    celt_assert(count*oaci_frame_dur[dur_index] <= 48);

    if (out_format != NULL)
        *out_format = format;
    if (out_channels != NULL)
        *out_channels = channels;
    if (out_nb_frames != NULL)
        *out_nb_frames = count;
    if (out_hdr_bytes != NULL)
        *out_hdr_bytes = hdr_bytes;
    return OAC_OK;
}

int oac_packet_get_samples_per_frame(const unsigned char *data, oac_int32 len,
                                     oac_int32 Fs) {
    int ret = oaci_packet_parse_toc(data, len, NULL, NULL, NULL, NULL);
    if (ret != OAC_OK)
        return ret;
    return oaci_toc_samples_per_frame(data[0], Fs);
}

int oac_packet_parse_impl(const unsigned char *data, oac_int32 len,
                          int self_delimited, unsigned char *out_toc,
                          const unsigned char *frames[OAC_MAX_FRAMES_PER_PACKET], oac_int32 size[OAC_MAX_FRAMES_PER_PACKET],
                          int *payload_offset, oac_int32 *packet_offset,
                          const unsigned char **padding, oac_int32 *padding_len) {
    int i, bytes;
    int count;
    int cbr;
    unsigned char toc;
    int hdr_bytes;
    oac_int32 last_size;
    oac_int32 pad = 0;
    const unsigned char *data0 = data;

    /* Make sure we return NULL/0 on error. */
    if (padding != NULL) {
        *padding = NULL;
        *padding_len = 0;
    }

    if (size == NULL || len < 0)
        return OAC_BAD_ARG;
    if (len == 0)
        return OAC_INVALID_PACKET;

    /* Resolving and validating the ToC is shared with the public accessors so
       that the two can never disagree about what a packet declares. */
    if (oaci_packet_parse_toc(data, len, NULL, NULL, &count, &hdr_bytes) != OAC_OK)
        return OAC_INVALID_PACKET;

    toc = data[0];
    /* V (0x80 of the extended byte) selects VBR. A packet with no extended
       byte holds a single frame, which is trivially CBR. */
    cbr = (toc&0x02) ? !(data[1]&0x80) : 1;
    data += hdr_bytes;
    len -= hdr_bytes;

    /* P (0x01) says the packet is padded. As in Opus, the padding length comes
       before the frame length fields and the padding itself goes at the end. */
    if (toc&0x01) {
        int p;
        do {
            int tmp;
            if (len <= 0)
                return OAC_INVALID_PACKET;
            p = *data++;
            len--;
            tmp = p == 255 ? 254: p;
            len -= tmp;
            pad += tmp;
        } while (p == 255);
    }
    if (len < 0)
        return OAC_INVALID_PACKET;

    last_size = len;
    if (!cbr) {
        /* VBR case: an explicit length for every frame but the last. */
        for (i = 0; i < count - 1; i++) {
            bytes = oaci_parse_size(data, len, size + i);
            len -= bytes;
            if (size[i] < 0 || size[i] > len)
                return OAC_INVALID_PACKET;
            data += bytes;
            last_size -= bytes + size[i];
        }
        if (last_size < 0)
            return OAC_INVALID_PACKET;
    } else if (!self_delimited) {
        /* CBR case */
        last_size = len/count;
        if (last_size*count != len)
            return OAC_INVALID_PACKET;
        for (i = 0; i < count - 1; i++)
            size[i] = last_size;
    }

    /* Self-delimited framing has an extra size for the last frame. */
    if (self_delimited) {
        bytes = oaci_parse_size(data, len, size + count - 1);
        len -= bytes;
        if (size[count - 1] < 0 || size[count - 1] > len)
            return OAC_INVALID_PACKET;
        data += bytes;
        /* For CBR packets, apply the size to all the frames. */
        if (cbr) {
            if (size[count - 1]*count > len)
                return OAC_INVALID_PACKET;
            for (i = 0; i < count - 1; i++)
                size[i] = size[count - 1];
        } else if (bytes + size[count - 1] > last_size)
            return OAC_INVALID_PACKET;
    } else {
        /* Because it's not encoded explicitly, it's possible the size of the
           last frame (or all the frames, for the CBR case) is not representable
           by the frame length codec. Reject them here, so that an implicit
           length is always explicitly representable. */
        if (last_size > OAC_SIZE_MAX)
            return OAC_INVALID_PACKET;
        size[count - 1] = last_size;
    }

    if (payload_offset)
        *payload_offset = (int)(data - data0);

    for (i = 0; i < count; i++) {
        if (frames)
            frames[i] = data;
        data += size[i];
    }

    if (padding != NULL) {
        *padding = data;
        *padding_len = pad;
    }
    if (packet_offset)
        *packet_offset = pad + (oac_int32)(data - data0);

    if (out_toc)
        *out_toc = toc;

    return count;
}

int oac_packet_parse(const unsigned char *data, oac_int32 len,
                     unsigned char *out_toc, const unsigned char *frames[OAC_MAX_FRAMES_PER_PACKET],
                     oac_int32 size[OAC_MAX_FRAMES_PER_PACKET], int *payload_offset) {
    return oac_packet_parse_impl(data, len, 0, out_toc,
                                 frames, size, payload_offset, NULL, NULL, NULL);
}

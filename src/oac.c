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

int oac_packet_get_samples_per_frame(const unsigned char *data,
                                     oac_int32 Fs) {
    int audiosize;
    if (data[0]&0x80) {
        audiosize = ((data[0]>>3)&0x3);
        audiosize = (Fs<<audiosize)/400;
    } else if ((data[0]&0x60) == 0x60) {
        audiosize = (data[0]&0x08) ? Fs/50 : Fs/100;
    } else {
        audiosize = ((data[0]>>3)&0x3);
        if (audiosize == 3)
            audiosize = Fs*60/1000;
        else
            audiosize = (Fs<<audiosize)/100;
    }
    return audiosize;
}

const unsigned char oaci_frame_dur[OAC_NB_FRAME_DURATIONS] = {
    1, 2, 4, 8, 16, 24, 32, 48
};

int oaci_dur_to_index(int dur_2_5ms) {
    int i;
    for (i = 0; i < OAC_NB_FRAME_DURATIONS; i++) {
        if (oaci_frame_dur[i] == dur_2_5ms)
            return i;
    }
    return -1;
}

int oaci_frames_to_F(int base_dur_idx, int count) {
    int target_dur;
    int target_idx;
    if (base_dur_idx < 0 || base_dur_idx >= OAC_NB_FRAME_DURATIONS || count < 1)
        return -1;
    target_dur = (int)oaci_frame_dur[base_dur_idx] * count;
    target_idx = oaci_dur_to_index(target_dur);
    if (target_idx < base_dur_idx)
        return -1;
    return target_idx - base_dur_idx;
}

int oaci_F_to_frames(int base_dur_idx, int F) {
    int base_dur, total_dur;
    if (base_dur_idx < 0 || base_dur_idx >= OAC_NB_FRAME_DURATIONS || F < 0)
        return -1;
    if (base_dur_idx + F >= OAC_NB_FRAME_DURATIONS)
        return -1;
    base_dur = oaci_frame_dur[base_dur_idx];
    total_dur = oaci_frame_dur[base_dur_idx + F];
    if (total_dur % base_dur != 0)
        return -1;
    return total_dur / base_dur;
}

int oaci_toc_bytes(int format, int channels, int count) {
    if (format == OAC_FORMAT_STANDARD && channels >= 16)
        return 3;
    if (count > 1 || format == OAC_FORMAT_AMBISONICS || channels > 2)
        return 2;
    return 1;
}

int oaci_write_toc(unsigned char *data, unsigned char config5,
                   int format, int channels,
                   int base_dur_idx, int count, int vbr, int pad) {
    int F;
    int need_x;
    int S, C;

    F = oaci_frames_to_F(base_dur_idx, count);
    if (F < 0 || channels < 1 || channels > OAC_MAX_CHANNELS)
        return OAC_BAD_ARG;

    need_x = (count > 1) || (format == OAC_FORMAT_AMBISONICS) || (channels > 2);
    if (!need_x) {
        S = (channels == 2) ? 1 : 0;
        data[0] = (unsigned char)(((config5 & 0x1F) << 3) | (S << 2) | (pad ? 0x01 : 0x00));
        return 1;
    }

    if (format == OAC_FORMAT_AMBISONICS) {
        int order = -1;
        int o;
        for (o = 0; o <= OAC_MAX_AMBISONICS_ORDER; o++) {
            if ((o + 1) * (o + 1) == channels) {
                order = o;
                break;
            }
        }
        if (order < 0)
            return OAC_BAD_ARG;
        S = order & 1;
        C = (order >> 1) & 0x07;
        data[0] = (unsigned char)(((config5 & 0x1F) << 3) | (S << 2) | 0x02 | (pad ? 0x01 : 0x00));
        data[1] = (unsigned char)((vbr ? 0x80 : 0x00) | ((F & 0x07) << 4) | 0x08 | C);
        return 2;
    }

    if (channels <= 15) {
        int ch_m1 = channels - 1;
        S = ch_m1 & 1;
        C = (ch_m1 >> 1) & 0x07;
        data[0] = (unsigned char)(((config5 & 0x1F) << 3) | (S << 2) | 0x02 | (pad ? 0x01 : 0x00));
        data[1] = (unsigned char)((vbr ? 0x80 : 0x00) | ((F & 0x07) << 4) | C);
        return 2;
    }

    data[0] = (unsigned char)(((config5 & 0x1F) << 3) | 0x04 | 0x02 | (pad ? 0x01 : 0x00));
    data[1] = (unsigned char)((vbr ? 0x80 : 0x00) | ((F & 0x07) << 4) | 0x07);
    data[2] = (unsigned char)(channels - 1);
    return 3;
}

int oaci_validate_config(int mode, int format, int channels,
                         int nb_frames, int samples_400) {
    if (nb_frames < 1 || samples_400 < 1 || nb_frames * samples_400 > 48)
        return OAC_INVALID_PACKET;
    if (channels < 1 || channels > OAC_MAX_CHANNELS)
        return OAC_INVALID_PACKET;
    if (format != OAC_FORMAT_STANDARD && format != OAC_FORMAT_AMBISONICS)
        return OAC_INVALID_PACKET;
    if ((mode == MODE_SILK_ONLY || mode == MODE_HYBRID) &&
        (channels > 2 || format != OAC_FORMAT_STANDARD))
        return OAC_INVALID_PACKET;
    return OAC_OK;
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
    int S, X, P;
    int pkt_format, pkt_channels;
    int samples_400, base_dur_idx;
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

    samples_400 = oac_packet_get_samples_per_frame(data, 400);
    base_dur_idx = oaci_dur_to_index(samples_400);

    toc = *data++;
    len--;
    S = (toc >> 2) & 0x1;
    X = (toc >> 1) & 0x1;
    P = toc & 0x1;

    if (!X) {
        cbr = 1;
        count = 1;
        pkt_format = OAC_FORMAT_STANDARD;
        pkt_channels = S + 1;
    } else {
        unsigned char ext;
        int F, C;
        if (len < 1)
            return OAC_INVALID_PACKET;
        ext = *data++;
        len--;
        cbr = !(ext & 0x80);
        F = (ext >> 4) & 0x07;
        C = ext & 0x07;
        count = oaci_F_to_frames(base_dur_idx, F);
        if (ext & 0x08) {
            int order = (C << 1) | S;
            pkt_format = OAC_FORMAT_AMBISONICS;
            pkt_channels = (order + 1) * (order + 1);
        } else {
            pkt_format = OAC_FORMAT_STANDARD;
            if (C == 7 && S == 1) {
                if (len < 1)
                    return OAC_INVALID_PACKET;
                pkt_channels = (int)(*data++) + 1;
                len--;
            } else {
                pkt_channels = ((C << 1) | S) + 1;
            }
        }
        if (count == 1)
            cbr = 1;
    }

    if (oaci_validate_config(oac_packet_get_mode(&toc), pkt_format,
                             pkt_channels, count, samples_400) != OAC_OK)
        return OAC_INVALID_PACKET;

    if (P) {
        int p;
        do {
            int tmp;
            if (len <= 0)
                return OAC_INVALID_PACKET;
            p = *data++;
            len--;
            tmp = p == 255 ? 254 : p;
            len -= tmp;
            pad += tmp;
        } while (p == 255);
    }
    if (len < 0)
        return OAC_INVALID_PACKET;

    last_size = len;
    if (!cbr) {
        /* VBR case */
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
        last_size = len / count;
        if (last_size * count != len)
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

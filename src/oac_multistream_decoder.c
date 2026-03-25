/* Copyright (c) 2011 Xiph.Org Foundation
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

#include "oac_multistream.h"
#include "oac.h"
#include "oac_private.h"
#include "stack_alloc.h"
#include <stdarg.h>
#include "float_cast.h"
#include "os_support.h"

/* DECODER */

#if defined(ENABLE_HARDENING) || defined(ENABLE_ASSERTIONS)
static void oaci_validate_ms_decoder(OacMSDecoder *st) {
    oaci_validate_layout(&st->layout);
}
# define VALIDATE_MS_DECODER(st) oaci_validate_ms_decoder(st)
#else
# define VALIDATE_MS_DECODER(st)
#endif


oac_int32 oac_multistream_decoder_get_size(int nb_streams, int nb_coupled_streams) {
    int coupled_size;
    int mono_size;

    if (nb_streams < 1 || nb_coupled_streams > nb_streams || nb_coupled_streams < 0) return 0;
    coupled_size = oac_decoder_get_size(2, OAC_FORMAT_STANDARD);
    mono_size = oac_decoder_get_size(1, OAC_FORMAT_STANDARD);
    return oaci_align(sizeof(OacMSDecoder))
           + nb_coupled_streams*oaci_align(coupled_size)
           + (nb_streams - nb_coupled_streams)*oaci_align(mono_size);
}

int oac_multistream_decoder_init(
    OacMSDecoder *st,
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping) {
    int coupled_size;
    int mono_size;
    int i, ret;
    char *ptr;

    if ((channels > 255) || (channels < 1) || (coupled_streams > streams)
        || (streams < 1) || (coupled_streams < 0) || (streams > 255 - coupled_streams))
        return OAC_BAD_ARG;

    st->layout.nb_channels = channels;
    st->layout.nb_streams = streams;
    st->layout.nb_coupled_streams = coupled_streams;

    for (i = 0; i < st->layout.nb_channels; i++)
        st->layout.mapping[i] = mapping[i];
    if (!oaci_validate_layout(&st->layout))
        return OAC_BAD_ARG;

    ptr = (char*)st + oaci_align(sizeof(OacMSDecoder));
    coupled_size = oac_decoder_get_size(2, OAC_FORMAT_STANDARD);
    mono_size = oac_decoder_get_size(1, OAC_FORMAT_STANDARD);

    for (i = 0; i < st->layout.nb_coupled_streams; i++) {
        ret = oac_decoder_init((OacDecoder*)ptr, Fs, 2, OAC_FORMAT_STANDARD);
        if (ret != OAC_OK) return ret;
        ptr += oaci_align(coupled_size);
    }
    for (; i < st->layout.nb_streams; i++) {
        ret = oac_decoder_init((OacDecoder*)ptr, Fs, 1, OAC_FORMAT_STANDARD);
        if (ret != OAC_OK) return ret;
        ptr += oaci_align(mono_size);
    }
    return OAC_OK;
}


OacMSDecoder *oac_multistream_decoder_create(
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping,
    int *error) {
    int ret;
    OacMSDecoder *st;
    if ((channels > 255) || (channels < 1) || (coupled_streams > streams)
        || (streams < 1) || (coupled_streams < 0) || (streams > 255 - coupled_streams)) {
        if (error)
            *error = OAC_BAD_ARG;
        return NULL;
    }
    st = (OacMSDecoder *)oac_alloc(oac_multistream_decoder_get_size(streams, coupled_streams));
    if (st == NULL) {
        if (error)
            *error = OAC_ALLOC_FAIL;
        return NULL;
    }
    ret = oac_multistream_decoder_init(st, Fs, channels, streams, coupled_streams, mapping);
    if (error)
        *error = ret;
    if (ret != OAC_OK) {
        oac_free(st);
        st = NULL;
    }
    return st;
}

static int oac_multistream_packet_validate(const unsigned char *data,
                                           oac_int32 len, int nb_streams, oac_int32 Fs) {
    int s;
    int count;
    unsigned char toc;
    oac_int16 size[48];
    int samples = 0;
    oac_int32 packet_offset;

    for (s = 0; s < nb_streams; s++) {
        int tmp_samples;
        if (len <= 0)
            return OAC_INVALID_PACKET;
        count = oac_packet_parse_impl(data, len, s != nb_streams - 1, &toc, NULL,
                                     size, NULL, &packet_offset, NULL, NULL);
        if (count < 0)
            return count;
        tmp_samples = oac_packet_get_nb_samples(data, packet_offset, Fs);
        if (s != 0 && samples != tmp_samples)
            return OAC_INVALID_PACKET;
        samples = tmp_samples;
        data += packet_offset;
        len -= packet_offset;
    }
    return samples;
}

int oac_multistream_decode_native(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    void *pcm,
    oac_copy_channel_out_func copy_channel_out,
    int frame_size,
    int decode_fec,
    int soft_clip,
    void *user_data) {
    oac_int32 Fs;
    int coupled_size;
    int mono_size;
    int s, c;
    char *ptr;
    int do_plc = 0;
    VARDECL(oac_res, buf);
    ALLOC_STACK;

    VALIDATE_MS_DECODER(st);
    if (frame_size <= 0) {
        RESTORE_STACK;
        return OAC_BAD_ARG;
    }
    /* Limit frame_size to avoid excessive stack allocations. */
    MUST_SUCCEED(oac_multistream_decoder_ctl(st, OAC_GET_SAMPLE_RATE(&Fs)));
    frame_size = IMIN(frame_size, Fs/25*3);
    ALLOC(buf, 2*frame_size, oac_res);
    ptr = (char*)st + oaci_align(sizeof(OacMSDecoder));
    coupled_size = oac_decoder_get_size(2, OAC_FORMAT_STANDARD);
    mono_size = oac_decoder_get_size(1, OAC_FORMAT_STANDARD);

    if (len == 0)
        do_plc = 1;
    if (len < 0) {
        RESTORE_STACK;
        return OAC_BAD_ARG;
    }
    if (!do_plc && len < 2*st->layout.nb_streams - 1) {
        RESTORE_STACK;
        return OAC_INVALID_PACKET;
    }
    if (!do_plc) {
        int ret = oac_multistream_packet_validate(data, len, st->layout.nb_streams, Fs);
        if (ret < 0) {
            RESTORE_STACK;
            return ret;
        } else if (ret > frame_size) {
            RESTORE_STACK;
            return OAC_BUFFER_TOO_SMALL;
        }
    }
    for (s = 0; s < st->layout.nb_streams; s++) {
        OacDecoder *dec;
        oac_int32 packet_offset;
        int ret;

        dec = (OacDecoder*)ptr;
        ptr += (s < st->layout.nb_coupled_streams) ? oaci_align(coupled_size) : oaci_align(mono_size);

        if (!do_plc && len <= 0) {
            RESTORE_STACK;
            return OAC_INTERNAL_ERROR;
        }
        packet_offset = 0;
        ret = oac_decode_native(dec, data, len, buf, frame_size, decode_fec, s != st->layout.nb_streams - 1,
        &packet_offset, soft_clip, NULL, 0);
        if (!do_plc) {
            data += packet_offset;
            len -= packet_offset;
        }
        if (ret <= 0) {
            RESTORE_STACK;
            return ret;
        }
        frame_size = ret;
        if (s < st->layout.nb_coupled_streams) {
            int chan, prev;
            prev = -1;
            /* Copy "left" audio to the channel(s) where it belongs */
            while ((chan = oaci_get_left_channel(&st->layout, s, prev)) != -1) {
                (*copy_channel_out)(pcm, st->layout.nb_channels, chan,
               buf, 2, frame_size, user_data);
                prev = chan;
            }
            prev = -1;
            /* Copy "right" audio to the channel(s) where it belongs */
            while ((chan = oaci_get_right_channel(&st->layout, s, prev)) != -1) {
                (*copy_channel_out)(pcm, st->layout.nb_channels, chan,
               buf + 1, 2, frame_size, user_data);
                prev = chan;
            }
        } else {
            int chan, prev;
            prev = -1;
            /* Copy audio to the channel(s) where it belongs */
            while ((chan = oaci_get_mono_channel(&st->layout, s, prev)) != -1) {
                (*copy_channel_out)(pcm, st->layout.nb_channels, chan,
               buf, 1, frame_size, user_data);
                prev = chan;
            }
        }
    }
    /* Handle muted channels */
    for (c = 0; c < st->layout.nb_channels; c++) {
        if (st->layout.mapping[c] == 255) {
            (*copy_channel_out)(pcm, st->layout.nb_channels, c,
            NULL, 0, frame_size, user_data);
        }
    }
    RESTORE_STACK;
    return frame_size;
}

#if !defined(DISABLE_FLOAT_API)
static void oac_copy_channel_out_float(
    void *dst,
    int dst_stride,
    int dst_channel,
    const oac_res *src,
    int src_stride,
    int frame_size,
    void *user_data) {
    float *float_dst;
    oac_int32 i;
    (void)user_data;
    float_dst = (float*)dst;
    if (src != NULL) {
        for (i = 0; i < frame_size; i++)
            float_dst[i*dst_stride + dst_channel] = RES2FLOAT(src[i*src_stride]);
    } else {
        for (i = 0; i < frame_size; i++)
            float_dst[i*dst_stride + dst_channel] = 0;
    }
}
#endif

static void oac_copy_channel_out_short(
    void *dst,
    int dst_stride,
    int dst_channel,
    const oac_res *src,
    int src_stride,
    int frame_size,
    void *user_data) {
    oac_int16 *short_dst;
    oac_int32 i;
    (void)user_data;
    short_dst = (oac_int16*)dst;
    if (src != NULL) {
        for (i = 0; i < frame_size; i++)
            short_dst[i*dst_stride + dst_channel] = RES2INT16(src[i*src_stride]);
    } else {
        for (i = 0; i < frame_size; i++)
            short_dst[i*dst_stride + dst_channel] = 0;
    }
}

static void oac_copy_channel_out_int24(
    void *dst,
    int dst_stride,
    int dst_channel,
    const oac_res *src,
    int src_stride,
    int frame_size,
    void *user_data) {
    oac_int32 *short_dst;
    oac_int32 i;
    (void)user_data;
    short_dst = (oac_int32*)dst;
    if (src != NULL) {
        for (i = 0; i < frame_size; i++)
            short_dst[i*dst_stride + dst_channel] = RES2INT24(src[i*src_stride]);
    } else {
        for (i = 0; i < frame_size; i++)
            short_dst[i*dst_stride + dst_channel] = 0;
    }
}

#ifdef FIXED_POINT
# define OPTIONAL_CLIP 0
#else
# define OPTIONAL_CLIP 1
#endif

int oac_multistream_decode(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int16 *pcm,
    int frame_size,
    int decode_fec) {
    return oac_multistream_decode_native(st, data, len,
       pcm, oac_copy_channel_out_short, frame_size, decode_fec, OPTIONAL_CLIP, NULL);
}

int oac_multistream_decode24(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int32 *pcm,
    int frame_size,
    int decode_fec) {
    return oac_multistream_decode_native(st, data, len,
       pcm, oac_copy_channel_out_int24, frame_size, decode_fec, 0, NULL);
}

#ifndef DISABLE_FLOAT_API
int oac_multistream_decode_float(OacMSDecoder *st, const unsigned char *data,
                                 oac_int32 len, float *pcm, int frame_size, int decode_fec) {
    return oac_multistream_decode_native(st, data, len,
       pcm, oac_copy_channel_out_float, frame_size, decode_fec, 0, NULL);
}
#endif

int oac_multistream_decoder_ctl_va_list(OacMSDecoder *st, int request,
                                        va_list ap) {
    int coupled_size, mono_size;
    char *ptr;
    int ret = OAC_OK;

    coupled_size = oac_decoder_get_size(2, OAC_FORMAT_STANDARD);
    mono_size = oac_decoder_get_size(1, OAC_FORMAT_STANDARD);
    ptr = (char*)st + oaci_align(sizeof(OacMSDecoder));
    switch (request) {
        case OAC_GET_BANDWIDTH_REQUEST:
        case OAC_GET_SAMPLE_RATE_REQUEST:
        case OAC_GET_GAIN_REQUEST:
        case OAC_GET_LAST_PACKET_DURATION_REQUEST:
        case OAC_GET_PHASE_INVERSION_DISABLED_REQUEST:
        case OAC_GET_COMPLEXITY_REQUEST:
        {
            OacDecoder *dec;
            /* For int32* GET params, just query the first stream */
            oac_int32 *value = va_arg(ap, oac_int32*);
            dec = (OacDecoder*)ptr;
            ret = oac_decoder_ctl(dec, request, value);
        }
        break;
        case OAC_GET_FINAL_RANGE_REQUEST:
        {
            int s;
            oac_uint32 *value = va_arg(ap, oac_uint32*);
            oac_uint32 tmp;
            if (!value) {
                goto bad_arg;
            }
            *value = 0;
            for (s = 0; s < st->layout.nb_streams; s++) {
                OacDecoder *dec;
                dec = (OacDecoder*)ptr;
                if (s < st->layout.nb_coupled_streams)
                    ptr += oaci_align(coupled_size);
                else
                    ptr += oaci_align(mono_size);
                ret = oac_decoder_ctl(dec, request, &tmp);
                if (ret != OAC_OK) break;
                *value ^= tmp;
            }
        }
        break;
        case OAC_RESET_STATE:
        {
            int s;
            for (s = 0; s < st->layout.nb_streams; s++) {
                OacDecoder *dec;

                dec = (OacDecoder*)ptr;
                if (s < st->layout.nb_coupled_streams)
                    ptr += oaci_align(coupled_size);
                else
                    ptr += oaci_align(mono_size);
                ret = oac_decoder_ctl(dec, OAC_RESET_STATE);
                if (ret != OAC_OK)
                    break;
            }
        }
        break;
        case OAC_MULTISTREAM_GET_DECODER_STATE_REQUEST:
        {
            int s;
            oac_int32 stream_id;
            OacDecoder **value;
            stream_id = va_arg(ap, oac_int32);
            if (stream_id < 0 || stream_id >= st->layout.nb_streams)
                goto bad_arg;
            value = va_arg(ap, OacDecoder**);
            if (!value) {
                goto bad_arg;
            }
            for (s = 0; s < stream_id; s++) {
                if (s < st->layout.nb_coupled_streams)
                    ptr += oaci_align(coupled_size);
                else
                    ptr += oaci_align(mono_size);
            }
            *value = (OacDecoder*)ptr;
        }
        break;
        case OAC_SET_GAIN_REQUEST:
        case OAC_SET_COMPLEXITY_REQUEST:
        case OAC_SET_PHASE_INVERSION_DISABLED_REQUEST:
        {
            int s;
            /* This works for int32 params */
            oac_int32 value = va_arg(ap, oac_int32);
            for (s = 0; s < st->layout.nb_streams; s++) {
                OacDecoder *dec;

                dec = (OacDecoder*)ptr;
                if (s < st->layout.nb_coupled_streams)
                    ptr += oaci_align(coupled_size);
                else
                    ptr += oaci_align(mono_size);
                ret = oac_decoder_ctl(dec, request, value);
                if (ret != OAC_OK)
                    break;
            }
        }
        break;
        default:
            ret = OAC_UNIMPLEMENTED;
            break;
    }
    return ret;
bad_arg:
    return OAC_BAD_ARG;
}

int oac_multistream_decoder_ctl(OacMSDecoder *st, int request, ...) {
    int ret;
    va_list ap;
    va_start(ap, request);
    ret = oac_multistream_decoder_ctl_va_list(st, request, ap);
    va_end(ap);
    return ret;
}

void oac_multistream_decoder_destroy(OacMSDecoder *st) {
    oac_free(st);
}

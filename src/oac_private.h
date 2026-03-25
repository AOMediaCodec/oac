/* Copyright (c) 2012 Xiph.Org Foundation
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


#ifndef OAC_PRIVATE_H
#define OAC_PRIVATE_H

#include "arch.h"
#include "oac.h"
#include "celt.h"

#include <stdarg.h> /* va_list */
#include <stddef.h> /* offsetof */

struct OacRepacketizer {
    unsigned char toc;
    int nb_frames;
    const unsigned char *frames[48];
    oac_int16 len[48];
    int framesize;
    const unsigned char *paddings[48];
    oac_int32 padding_len[48];
    unsigned char padding_nb_frames[48];
};

typedef struct OacExtensionIterator {
    const unsigned char *data;
    const unsigned char *curr_data;
    const unsigned char *repeat_data;
    const unsigned char *last_long;
    const unsigned char *src_data;
    oac_int32 len;
    oac_int32 curr_len;
    oac_int32 repeat_len;
    oac_int32 src_len;
    oac_int32 trailing_short_len;
    int nb_frames;
    int frame_max;
    int curr_frame;
    int repeat_frame;
    unsigned char repeat_l;
} OacExtensionIterator;

typedef struct {
    int id;
    int frame;
    const unsigned char *data;
    oac_int32 len;
} oac_extension_data;

void oac_extension_iterator_init(OacExtensionIterator *iter,
    const unsigned char *data, oac_int32 len, oac_int32 nb_frames);

void oac_extension_iterator_reset(OacExtensionIterator *iter);
void oac_extension_iterator_set_frame_max(OacExtensionIterator *iter,
    int frame_max);
int oac_extension_iterator_next(OacExtensionIterator *iter,
    oac_extension_data *ext);
int oac_extension_iterator_find(OacExtensionIterator *iter,
    oac_extension_data *ext, int id);

typedef struct ChannelLayout {
    int nb_channels;
    int nb_streams;
    int nb_coupled_streams;
    unsigned char mapping[256];
} ChannelLayout;

typedef enum {
    MAPPING_TYPE_NONE,
    MAPPING_TYPE_SURROUND,
    MAPPING_TYPE_AMBISONICS
} MappingType;

struct OacMSEncoder {
    ChannelLayout layout;
    int arch;
    int lfe_stream;
    int application;
    oac_int32 Fs;
    int variable_duration;
    MappingType mapping_type;
    oac_int32 bitrate_bps;
    /* Encoder states go here */
    /* then oac_val32 window_mem[channels*120]; */
    /* then oac_val32 preemph_mem[channels]; */
};

struct OacMSDecoder {
    ChannelLayout layout;
    /* Decoder states go here */
};

int oac_multistream_encoder_ctl_va_list(struct OacMSEncoder *st, int request,
    va_list ap);
int oac_multistream_decoder_ctl_va_list(struct OacMSDecoder *st, int request,
    va_list ap);

int oaci_validate_layout(const ChannelLayout *layout);
int oaci_get_left_channel(const ChannelLayout *layout, int stream_id, int prev);
int oaci_get_right_channel(const ChannelLayout *layout, int stream_id, int prev);
int oaci_get_mono_channel(const ChannelLayout *layout, int stream_id, int prev);

typedef void (*oac_copy_channel_in_func)(
    oac_res *dst,
    int dst_stride,
    const void *src,
    int src_stride,
    int src_channel,
    int frame_size,
    void *user_data);

typedef void (*oac_copy_channel_out_func)(
    void *dst,
    int dst_stride,
    int dst_channel,
    const oac_res *src,
    int src_stride,
    int frame_size,
    void *user_data);

#define MODE_SILK_ONLY          1000
#define MODE_HYBRID             1001
#define MODE_CELT_ONLY          1002

#define OAC_SET_VOICE_RATIO_REQUEST         11018
#define OAC_GET_VOICE_RATIO_REQUEST         11019

/** Configures the encoder's expected percentage of voice
 * opposed to music or other signals.
 *
 * @note This interface is currently more aspiration than actuality. It's
 * ultimately expected to bias an automatic signal classifier, but it currently
 * just shifts the static bitrate to mode mapping around a little bit.
 *
 * @param[in] x <tt>int</tt>:   Voice percentage in the range 0-100, inclusive.
 * @hideinitializer */
#define OAC_SET_VOICE_RATIO(x) OAC_SET_VOICE_RATIO_REQUEST, oac_check_int(x)
/** Gets the encoder's configured voice ratio value, @see OAC_SET_VOICE_RATIO
 *
 * @param[out] x <tt>int*</tt>:  Voice percentage in the range 0-100, inclusive.
 * @hideinitializer */
#define OAC_GET_VOICE_RATIO(x) OAC_GET_VOICE_RATIO_REQUEST, oac_check_int_ptr(x)


#define OAC_SET_FORCE_MODE_REQUEST    11002
#define OAC_SET_FORCE_MODE(x) OAC_SET_FORCE_MODE_REQUEST, oac_check_int(x)

typedef void (*downmix_func)(const void *, oac_val32 *, int, int, int, int, int);
void oaci_downmix_float(const void *_x, oac_val32 *sub, int subframe, int offset, int c1, int c2, int C);
void oaci_downmix_int(const void *_x, oac_val32 *sub, int subframe, int offset, int c1, int c2, int C);
void oaci_downmix_int24(const void *_x, oac_val32 *sub, int subframe, int offset, int c1, int c2, int C);
int oaci_is_digital_silence(const oac_res* pcm, int frame_size, int channels, int lsb_depth);

void oac_pcm_soft_clip_impl(float *_x, int N, int C, float *declip_mem, int arch);

int oaci_encode_size(int size, unsigned char *data);

oac_int32 oaci_frame_size_select(int application, oac_int32 frame_size, int variable_duration, oac_int32 Fs);

oac_int32 oac_encode_native(OacEncoder *st, const oac_res *pcm, int frame_size,
    unsigned char *data, oac_int32 out_data_bytes, int lsb_depth,
    const void *analysis_pcm, oac_int32 analysis_size, int c1, int c2,
    int analysis_channels, downmix_func oaci_downmix, int float_api);

int oac_decode_native(OacDecoder *st, const unsigned char *data, oac_int32 len,
    oac_res *pcm, int frame_size, int decode_fec, int self_delimited,
    oac_int32 *packet_offset, int soft_clip, const OacDRED *dred, oac_int32 dred_offset);

/* Make sure everything is properly aligned. */
static OAC_INLINE int oaci_align(int i) {
    struct foo {char c; union { void* p; oac_int32 i; oac_val32 v; } u;};

    unsigned int alignment = offsetof(struct foo, u);

    /* Optimizing compilers should optimize div and multiply into and
       for all sensible alignment values. */
    return ((i + alignment - 1)/alignment)*alignment;
}

int oac_packet_parse_impl(const unsigned char *data, oac_int32 len,
    int self_delimited, unsigned char *out_toc,
    const unsigned char *frames[48], oac_int16 size[48],
    int *payload_offset, oac_int32 *packet_offset,
    const unsigned char **padding, oac_int32 *padding_len);

oac_int32 oac_repacketizer_out_range_impl(OacRepacketizer *rp, int begin, int end,
    unsigned char *data, oac_int32 maxlen, int self_delimited, int pad,
    const oac_extension_data *extensions, int nb_extensions);

int pad_frame(unsigned char *data, oac_int32 len, oac_int32 new_len);

int oac_multistream_encode_native(
    struct OacMSEncoder *st,
    oac_copy_channel_in_func copy_channel_in,
    const void *pcm,
    int analysis_frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes,
    int lsb_depth,
    downmix_func oaci_downmix,
    int float_api,
    void *user_data);

int oac_multistream_decode_native(
    struct OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    void *pcm,
    oac_copy_channel_out_func copy_channel_out,
    int frame_size,
    int decode_fec,
    int soft_clip,
    void *user_data);

oac_int32 oac_packet_extensions_parse(const unsigned char *data,
    oac_int32 len, oac_extension_data *extensions, oac_int32 *nb_extensions,
    int nb_frames);

oac_int32 oac_packet_extensions_parse_ext(const unsigned char *data,
    oac_int32 len, oac_extension_data *extensions, oac_int32 *nb_extensions,
    const oac_int32 *nb_frame_exts, int nb_frames);

oac_int32 oac_packet_extensions_generate(unsigned char *data, oac_int32 len,
    const oac_extension_data *extensions, oac_int32 nb_extensions,
    int nb_frames, int pad);

oac_int32 oac_packet_extensions_count(const unsigned char *data,
    oac_int32 len, int nb_frames);

oac_int32 oac_packet_extensions_count_ext(const unsigned char *data,
    oac_int32 len, oac_int32 *nb_frame_exts, int nb_frames);

oac_int32 oac_packet_pad_impl(unsigned char *data, oac_int32 len, oac_int32 new_len, int pad,
    const oac_extension_data  *extensions, int nb_extensions);

/** Validate format and channel count combination.
 * @param format OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS
 * @param channels Number of channels
 * @returns 1 if valid, 0 if invalid
 */
static OAC_INLINE int oaci_validate_format_channels(int format, int channels) {
    if (format == OAC_FORMAT_STANDARD) {
        return (channels == 1 || channels == 2);
    } else if (format == OAC_FORMAT_AMBISONICS) {
        /* Valid ambisonics channel counts: (order+1)^2 for orders 0-5 */
        return (channels == 1 || channels == 4 || channels == 9 ||
                channels == 16 || channels == 25 || channels == 36);
    }
    return 0;
}
#endif /* OAC_PRIVATE_H */

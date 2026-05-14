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

#include "oac.h"
#include "oac_private.h"
#include "os_support.h"
#include "stack_alloc.h"


int oac_repacketizer_get_size(void) {
    return sizeof(OacRepacketizer);
}

OacRepacketizer *oac_repacketizer_init(OacRepacketizer *rp, int format) {
    rp->nb_frames = 0;
    rp->format = format;
    return rp;
}

OacRepacketizer *oac_repacketizer_create(int format) {
    OacRepacketizer *rp;
    rp = (OacRepacketizer *)oac_alloc(oac_repacketizer_get_size());
    if (rp == NULL) return NULL;
    return oac_repacketizer_init(rp, format);
}

void oac_repacketizer_destroy(OacRepacketizer *rp) {
    oac_free(rp);
}

static int oac_repacketizer_cat_impl(OacRepacketizer *rp, const unsigned char *data, oac_int32 len,
                                     int self_delimited) {
    unsigned char tmp_toc;
    int curr_nb_frames, ret;
    /* Set of check ToC */
    if (len < 1) return OAC_INVALID_PACKET;
    if (rp->nb_frames == 0) {
        rp->toc = data[0];
        rp->framesize = oac_packet_get_samples_per_frame(data, 8000);
    } else if ((rp->toc&0xFC) != (data[0]&0xFC)) {
        /*fprintf(stderr, "toc mismatch: 0x%x vs 0x%x\n", rp->toc, data[0]);*/
        return OAC_INVALID_PACKET;
    }
    curr_nb_frames = oac_packet_get_nb_frames(data, len);
    if (curr_nb_frames < 1) return OAC_INVALID_PACKET;

    /* Check the 120 ms maximum packet size */
    if ((curr_nb_frames + rp->nb_frames)*rp->framesize > 960) {
        return OAC_INVALID_PACKET;
    }

    ret = oac_packet_parse_impl(data, len, self_delimited, &tmp_toc, &rp->frames[rp->nb_frames],
    &rp->len[rp->nb_frames],
       NULL, NULL, &rp->paddings[rp->nb_frames], &rp->padding_len[rp->nb_frames],
       rp->format);
    if (ret < 1) return ret;
    rp->padding_nb_frames[rp->nb_frames] = ret;

    /* set padding length to zero for all but the first frame */
    while (curr_nb_frames > 1) {
        rp->nb_frames++;
        rp->padding_len[rp->nb_frames] = 0;
        rp->padding_nb_frames[rp->nb_frames] = 0;
        rp->paddings[rp->nb_frames] = NULL;
        curr_nb_frames--;
    }
    rp->nb_frames++;
    return OAC_OK;
}

int oac_repacketizer_cat(OacRepacketizer *rp, const unsigned char *data, oac_int32 len) {
    return oac_repacketizer_cat_impl(rp, data, len, 0);
}

int oac_repacketizer_get_nb_frames(OacRepacketizer *rp) {
    return rp->nb_frames;
}

oac_int32 oac_repacketizer_out_range_impl(OacRepacketizer *rp, int begin, int end,
                                          unsigned char *data, oac_int32 maxlen, int self_delimited, int pad,
                                          const oac_extension_data *extensions, int nb_extensions) {
    int i, count;
    oac_int32 tot_size;
    oac_int16 *len;
    const unsigned char **frames;
    unsigned char * ptr;
    int ones_begin = 0, ones_end = 0;
    int ext_begin = 0, ext_len = 0;
    int ext_count, total_ext_count;
    VARDECL(oac_extension_data, all_extensions);
    SAVE_STACK;

    if (begin < 0 || begin >= end || end > rp->nb_frames) {
        /*fprintf(stderr, "%d %d %d\n", begin, end, rp->nb_frames);*/
        RESTORE_STACK;
        return OAC_BAD_ARG;
    }
    count = end - begin;

    len = rp->len + begin;
    frames = rp->frames + begin;
    if (self_delimited)
        tot_size = 1 + (len[count - 1] >= 252);
    else
        tot_size = 0;

    /* figure out total number of extensions */
    total_ext_count = nb_extensions;
    for (i = begin; i < end; i++) {
        int n = oac_packet_extensions_count(rp->paddings[i], rp->padding_len[i],
       rp->padding_nb_frames[i]);
        if (n > 0) total_ext_count += n;
    }
    ALLOC(all_extensions, total_ext_count ? total_ext_count : ALLOC_NONE, oac_extension_data);
    /* copy over any extensions that were passed in */
    for (ext_count = 0; ext_count < nb_extensions; ext_count++) {
        all_extensions[ext_count] = extensions[ext_count];
    }

    /* incorporate any extensions from the repacketizer padding */
    for (i = begin; i < end; i++) {
        int j, ret;
        oac_int32 frame_ext_count;
        frame_ext_count = total_ext_count - ext_count;
        ret = oac_packet_extensions_parse(rp->paddings[i], rp->padding_len[i],
         &all_extensions[ext_count], &frame_ext_count, rp->padding_nb_frames[i]);
        if (ret < 0) {
            RESTORE_STACK;
            return OAC_INTERNAL_ERROR;
        }
        /* renumber the extension frame numbers */
        for (j = 0; j < frame_ext_count; j++) {
            all_extensions[ext_count + j].frame += i - begin;
        }
        ext_count += frame_ext_count;
    }

    ptr = data;
    if (count == 1) {
        /* Code 0 */
        tot_size += len[0] + 1;
        if (tot_size > maxlen) {
            RESTORE_STACK;
            return OAC_BUFFER_TOO_SMALL;
        }
        *ptr++ = rp->toc&0xFC;
    } else if (count == 2) {
        if (len[1] == len[0]) {
            /* Code 1 */
            tot_size += 2*len[0] + 1;
            if (tot_size > maxlen) {
                RESTORE_STACK;
                return OAC_BUFFER_TOO_SMALL;
            }
            *ptr++ = (rp->toc&0xFC)|0x1;
        } else {
            /* Code 2 */
            tot_size += len[0] + len[1] + 2 + (len[0] >= 252);
            if (tot_size > maxlen) {
                RESTORE_STACK;
                return OAC_BUFFER_TOO_SMALL;
            }
            *ptr++ = (rp->toc&0xFC)|0x2;
            ptr += oaci_encode_size(len[0], ptr);
        }
    }
    if (count > 2 || (pad && tot_size < maxlen) || ext_count > 0) {
        /* Code 3 */
        int vbr;
        int pad_amount = 0;

        /* Restart the process for the padding case */
        ptr = data;
        if (self_delimited)
            tot_size = 1 + (len[count - 1] >= 252);
        else
            tot_size = 0;
        vbr = 0;
        for (i = 1; i < count; i++) {
            if (len[i] != len[0]) {
                vbr = 1;
                break;
            }
        }
        if (vbr) {
            tot_size += 2;
            for (i = 0; i < count - 1; i++)
                tot_size += 1 + (len[i] >= 252) + len[i];
            tot_size += len[count - 1];

            if (tot_size > maxlen) {
                RESTORE_STACK;
                return OAC_BUFFER_TOO_SMALL;
            }
            *ptr++ = (rp->toc&0xFC)|0x3;
            *ptr++ = count|0x80;
        } else {
            tot_size += count*len[0] + 2;
            if (tot_size > maxlen) {
                RESTORE_STACK;
                return OAC_BUFFER_TOO_SMALL;
            }
            *ptr++ = (rp->toc&0xFC)|0x3;
            *ptr++ = count;
        }
        pad_amount = pad ? (maxlen - tot_size) : 0;
        if (ext_count > 0) {
            /* figure out how much space we need for the extensions */
            ext_len = oac_packet_extensions_generate(NULL, maxlen - tot_size,
          all_extensions, ext_count, count, 0);
            if (ext_len < 0) return ext_len;
            if (!pad)
                pad_amount = ext_len + (ext_len ? (ext_len + 253)/254 : 1);
        }
        if (pad_amount != 0) {
            int nb_255s;
            data[1] |= 0x40;
            nb_255s = (pad_amount - 1)/255;
            if (tot_size + ext_len + nb_255s + 1 > maxlen) {
                RESTORE_STACK;
                return OAC_BUFFER_TOO_SMALL;
            }
            ext_begin = tot_size + pad_amount - ext_len;
            /* Prepend 0x01 padding */
            ones_begin = tot_size + nb_255s + 1;
            ones_end = tot_size + pad_amount - ext_len;
            for (i = 0; i < nb_255s; i++)
                *ptr++ = 255;
            *ptr++ = pad_amount - 255*nb_255s - 1;
            tot_size += pad_amount;
        }
        if (vbr) {
            for (i = 0; i < count - 1; i++)
                ptr += oaci_encode_size(len[i], ptr);
        }
    }
    if (self_delimited) {
        int sdlen = oaci_encode_size(len[count - 1], ptr);
        ptr += sdlen;
    }
    /* Copy the actual data */
    for (i = 0; i < count; i++) {
        /* Using OAC_MOVE() instead of OAC_COPY() in case we're doing in-place
           padding from oac_packet_pad or oac_packet_unpad(). */
        /* assert disabled because it's not valid in C. */
        /* celt_assert(frames[i] + len[i] <= data || ptr <= frames[i]); */
        OAC_MOVE(ptr, frames[i], len[i]);
        ptr += len[i];
    }
    if (ext_len > 0) {
        int ret = oac_packet_extensions_generate(&data[ext_begin], ext_len,
       all_extensions, ext_count, count, 0);
        celt_assert(ret == ext_len);
    }
    for (i = ones_begin; i < ones_end; i++)
        data[i] = 0x01;
    if (pad && ext_count == 0) {
        /* Fill padding with zeros. */
        while (ptr < data + maxlen)
            *ptr++ = 0;
    }
    RESTORE_STACK;
    return tot_size;
}

oac_int32 oac_repacketizer_out_range(OacRepacketizer *rp, int begin, int end, unsigned char *data, oac_int32 maxlen) {
    return oac_repacketizer_out_range_impl(rp, begin, end, data, maxlen, 0, 0, NULL, 0);
}

oac_int32 oac_repacketizer_out(OacRepacketizer *rp, unsigned char *data, oac_int32 maxlen) {
    return oac_repacketizer_out_range_impl(rp, 0, rp->nb_frames, data, maxlen, 0, 0, NULL, 0);
}

oac_int32 oac_packet_pad_impl(unsigned char *data, oac_int32 len, oac_int32 new_len, int pad,
                              const oac_extension_data  *extensions, int nb_extensions) {
    OacRepacketizer rp;
    oac_int32 ret;
    VARDECL(unsigned char, copy);
    SAVE_STACK;
    if (len < 1)
        return OAC_BAD_ARG;
    if (len == new_len)
        return OAC_OK;
    else if (len > new_len)
        return OAC_BAD_ARG;
    ALLOC(copy, len, unsigned char);
    oac_repacketizer_init(&rp, OAC_FORMAT_STANDARD);
    /* Moving payload to the end of the packet so we can do in-place padding */
    OAC_COPY(copy, data, len);
    ret = oac_repacketizer_cat(&rp, copy, len);
    if (ret != OAC_OK)
        return ret;
    ret = oac_repacketizer_out_range_impl(&rp, 0, rp.nb_frames, data, new_len, 0, pad, extensions, nb_extensions);
    RESTORE_STACK;
    return ret;
}

int oac_packet_pad(unsigned char *data, oac_int32 len, oac_int32 new_len) {
    oac_int32 ret;
    ALLOC_STACK;
    ret = oac_packet_pad_impl(data, len, new_len, 1, NULL, 0);
    RESTORE_STACK;
    if (ret > 0)
        return OAC_OK;
    else
        return ret;
}

oac_int32 oac_packet_unpad(unsigned char *data, oac_int32 len) {
    OacRepacketizer rp;
    oac_int32 ret;
    int i;
    if (len < 1)
        return OAC_BAD_ARG;
    oac_repacketizer_init(&rp, OAC_FORMAT_STANDARD);
    ret = oac_repacketizer_cat(&rp, data, len);
    if (ret < 0)
        return ret;
    /* Discard all padding and extensions. */
    for (i = 0; i < rp.nb_frames; i++) {
        rp.padding_len[i] = 0;
        rp.paddings[i] = NULL;
    }
    ret = oac_repacketizer_out_range_impl(&rp, 0, rp.nb_frames, data, len, 0, 0, NULL, 0);
    celt_assert(ret > 0 && ret <= len);
    return ret;
}

int oac_multistream_packet_pad(unsigned char *data, oac_int32 len, oac_int32 new_len, int nb_streams) {
    int s;
    int count;
    unsigned char toc;
    oac_int16 size[48];
    oac_int32 packet_offset;
    oac_int32 amount;

    if (len < 1)
        return OAC_BAD_ARG;
    if (len == new_len)
        return OAC_OK;
    else if (len > new_len)
        return OAC_BAD_ARG;
    amount = new_len - len;
    /* Seek to last stream */
    for (s = 0; s < nb_streams - 1; s++) {
        if (len <= 0)
            return OAC_INVALID_PACKET;
        count = oac_packet_parse_impl(data, len, 1, &toc, NULL,
                                     size, NULL, &packet_offset, NULL, NULL,
                                     OAC_FORMAT_STANDARD);
        if (count < 0)
            return count;
        data += packet_offset;
        len -= packet_offset;
    }
    return oac_packet_pad(data, len, len + amount);
}

oac_int32 oac_multistream_packet_unpad(unsigned char *data, oac_int32 len, int nb_streams) {
    int s;
    unsigned char toc;
    oac_int16 size[48];
    oac_int32 packet_offset;
    OacRepacketizer rp;
    unsigned char *dst;
    oac_int32 dst_len;

    if (len < 1)
        return OAC_BAD_ARG;
    dst = data;
    dst_len = 0;
    /* Unpad all frames */
    for (s = 0; s < nb_streams; s++) {
        oac_int32 ret;
        int i;
        int self_delimited = s != nb_streams - 1;
        if (len <= 0)
            return OAC_INVALID_PACKET;
        oac_repacketizer_init(&rp, OAC_FORMAT_STANDARD);
        ret = oac_packet_parse_impl(data, len, self_delimited, &toc, NULL,
                                     size, NULL, &packet_offset, NULL, NULL,
                                     OAC_FORMAT_STANDARD);
        if (ret < 0)
            return ret;
        ret = oac_repacketizer_cat_impl(&rp, data, packet_offset, self_delimited);
        if (ret < 0)
            return ret;
        /* Discard all padding and extensions. */
        for (i = 0; i < rp.nb_frames; i++) {
            rp.padding_len[i] = 0;
            rp.paddings[i] = NULL;
        }
        ret = oac_repacketizer_out_range_impl(&rp, 0, rp.nb_frames, dst, len, self_delimited, 0, NULL, 0);
        if (ret < 0)
            return ret;
        else
            dst_len += ret;
        dst += ret;
        data += packet_offset;
        len -= packet_offset;
    }
    return dst_len;
}

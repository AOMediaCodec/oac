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

/**
 * @file oac_multistream.h
 * @brief Oac reference implementation multistream API
 */

#ifndef OAC_MULTISTREAM_H
#define OAC_MULTISTREAM_H

#include "oac.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @cond OAC_INTERNAL_DOC */

/** Macros to trigger compilation errors when the wrong types are provided to a
 * CTL. */
/**@{*/
#define oac_check_encstate_ptr(ptr) ((ptr) + ((ptr) - (OacEncoder**)(ptr)))
#define oac_check_decstate_ptr(ptr) ((ptr) + ((ptr) - (OacDecoder**)(ptr)))
/**@}*/

/** These are the actual encoder and decoder CTL ID numbers.
 * They should not be used directly by applications.
 * In general, SETs should be even and GETs should be odd.*/
/**@{*/
#define OAC_MULTISTREAM_GET_ENCODER_STATE_REQUEST 5120
#define OAC_MULTISTREAM_GET_DECODER_STATE_REQUEST 5122
/**@}*/

/** @endcond */

/** @defgroup oac_multistream_ctls Multistream specific encoder and decoder CTLs
 *
 * These are convenience macros that are specific to the
 * oac_multistream_encoder_ctl() and oac_multistream_decoder_ctl()
 * interface.
 * The CTLs from @ref oac_genericctls, @ref oac_encoderctls, and
 * @ref oac_decoderctls may be applied to a multistream encoder or decoder as
 * well.
 * In addition, you may retrieve the encoder or decoder state for an specific
 * stream via #OAC_MULTISTREAM_GET_ENCODER_STATE or
 * #OAC_MULTISTREAM_GET_DECODER_STATE and apply CTLs to it individually.
 */
/**@{*/

/** Gets the encoder state for an individual stream of a multistream encoder.
 * @param[in] x <tt>oac_int32</tt>: The index of the stream whose encoder you
 *                                   wish to retrieve.
 *                                   This must be non-negative and less than
 *                                   the <code>streams</code> parameter used
 *                                   to initialize the encoder.
 * @param[out] y <tt>OacEncoder**</tt>: Returns a pointer to the given
 *                                       encoder state.
 * @retval OAC_BAD_ARG The index of the requested stream was out of range.
 * @hideinitializer
 */
#define OAC_MULTISTREAM_GET_ENCODER_STATE(x, y) OAC_MULTISTREAM_GET_ENCODER_STATE_REQUEST, oac_check_int(x), \
        oac_check_encstate_ptr(y)

/** Gets the decoder state for an individual stream of a multistream decoder.
 * @param[in] x <tt>oac_int32</tt>: The index of the stream whose decoder you
 *                                   wish to retrieve.
 *                                   This must be non-negative and less than
 *                                   the <code>streams</code> parameter used
 *                                   to initialize the decoder.
 * @param[out] y <tt>OacDecoder**</tt>: Returns a pointer to the given
 *                                       decoder state.
 * @retval OAC_BAD_ARG The index of the requested stream was out of range.
 * @hideinitializer
 */
#define OAC_MULTISTREAM_GET_DECODER_STATE(x, y) OAC_MULTISTREAM_GET_DECODER_STATE_REQUEST, oac_check_int(x), \
        oac_check_decstate_ptr(y)

/**@}*/

/** @defgroup oac_multistream Oac Multistream API
 * @{
 *
 * The multistream API allows individual Oac streams to be combined into a
 * single packet, enabling support for up to 255 channels. Unlike an
 * elementary Oac stream, the encoder and decoder must negotiate the channel
 * configuration before the decoder can successfully interpret the data in the
 * packets produced by the encoder. Some basic information, such as packet
 * duration, can be computed without any special negotiation.
 *
 * The format for multistream Oac packets is defined in
 * <a href="https://tools.ietf.org/html/rfc7845">RFC 7845</a>
 * and is based on the self-delimited Oac framing described in Appendix B of
 * <a href="https://tools.ietf.org/html/rfc6716">RFC 6716</a>.
 * Normal Oac packets are just a degenerate case of multistream Oac packets,
 * and can be encoded or decoded with the multistream API by setting
 * <code>streams</code> to <code>1</code> when initializing the encoder or
 * decoder.
 *
 * Multistream Oac streams can contain up to 255 elementary Oac streams.
 * These may be either "uncoupled" or "coupled", indicating that the decoder
 * is configured to decode them to either 1 or 2 channels, respectively.
 * The streams are ordered so that all coupled streams appear at the
 * beginning.
 *
 * A <code>mapping</code> table defines which decoded channel <code>i</code>
 * should be used for each input/output (I/O) channel <code>j</code>. This table is
 * typically provided as an unsigned char array.
 * Let <code>i = mapping[j]</code> be the index for I/O channel <code>j</code>.
 * If <code>i < 2*coupled_streams</code>, then I/O channel <code>j</code> is
 * encoded as the left channel of stream <code>(i/2)</code> if <code>i</code>
 * is even, or  as the right channel of stream <code>(i/2)</code> if
 * <code>i</code> is odd. Otherwise, I/O channel <code>j</code> is encoded as
 * mono in stream <code>(i - coupled_streams)</code>, unless it has the special
 * value 255, in which case it is omitted from the encoding entirely (the
 * decoder will reproduce it as silence). Each value <code>i</code> must either
 * be the special value 255 or be less than <code>streams + coupled_streams</code>.
 *
 * The output channels specified by the encoder
 * should use the
 * <a href="https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-810004.3.9">Vorbis
 * channel ordering</a>. A decoder may wish to apply an additional permutation
 * to the mapping the encoder used to achieve a different output channel
 * order (e.g. for outputting in WAV order).
 *
 * Each multistream packet contains an Oac packet for each stream, and all of
 * the Oac packets in a single multistream packet must have the same
 * duration. Therefore the duration of a multistream packet can be extracted
 * from the TOC sequence of the first stream, which is located at the
 * beginning of the packet, just like an elementary Oac stream:
 *
 * @code
 * int nb_samples;
 * int nb_frames;
 * nb_frames = oac_packet_get_nb_frames(data, len);
 * if (nb_frames < 1)
 *   return nb_frames;
 * nb_samples = oac_packet_get_samples_per_frame(data, len, 48000) * nb_frames;
 * @endcode
 *
 * The general encoding and decoding process proceeds exactly the same as in
 * the normal @ref oac_encoder and @ref oac_decoder APIs.
 * See their documentation for an overview of how to use the corresponding
 * multistream functions.
 */

/** Oac multistream encoder state.
 * This contains the complete state of a multistream Oac encoder.
 * It is position independent and can be freely copied.
 * @see oac_multistream_encoder_create
 * @see oac_multistream_encoder_init
 */
typedef struct OacMSEncoder OacMSEncoder;

/** Oac multistream decoder state.
 * This contains the complete state of a multistream Oac decoder.
 * It is position independent and can be freely copied.
 * @see oac_multistream_decoder_create
 * @see oac_multistream_decoder_init
 */
typedef struct OacMSDecoder OacMSDecoder;

/**\name Multistream encoder functions */
/**@{*/

/** Gets the size of an OacMSEncoder structure.
 * @param streams <tt>int</tt>: The total number of streams to encode from the
 *                              input.
 *                              This must be no more than 255.
 * @param coupled_streams <tt>int</tt>: Number of coupled (2 channel) streams
 *                                      to encode.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      encoded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than 255.
 * @returns The size in bytes on success, or a negative error code
 *          (see @ref oac_errorcodes) on error.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_multistream_encoder_get_size(
    int streams,
    int coupled_streams);

OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_multistream_surround_encoder_get_size(
    int channels,
    int mapping_family);


/** Allocates and initializes a multistream encoder state.
 * Call oac_multistream_encoder_destroy() to release
 * this object when finished.
 * @param Fs <tt>oac_int32</tt>: Sampling rate of the input signal (in Hz).
 *                                This must be one of 8000, 12000, 16000,
 *                                24000, or 48000.
 * @param channels <tt>int</tt>: Number of channels in the input signal.
 *                               This must be at most 255.
 *                               It may be greater than the number of
 *                               coded channels (<code>streams +
 *                               coupled_streams</code>).
 * @param streams <tt>int</tt>: The total number of streams to encode from the
 *                              input.
 *                              This must be no more than the number of channels.
 * @param coupled_streams <tt>int</tt>: Number of coupled (2 channel) streams
 *                                      to encode.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      encoded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than the number of input channels.
 * @param[in] mapping <code>const unsigned char[channels]</code>: Mapping from
 *                    encoded channels to input channels, as described in
 *                    @ref oac_multistream. As an extra constraint, the
 *                    multistream encoder does not allow encoding coupled
 *                    streams for which one channel is unused since this
 *                    is never a good idea.
 * @param application <tt>int</tt>: The target encoder application.
 *                                  This must be one of the following:
 * <dl>
 * <dt>#OAC_APPLICATION_VOIP</dt>
 * <dd>Process signal for improved speech intelligibility.</dd>
 * <dt>#OAC_APPLICATION_AUDIO</dt>
 * <dd>Favor faithfulness to the original input.</dd>
 * <dt>#OAC_APPLICATION_RESTRICTED_LOWDELAY</dt>
 * <dd>Configure the minimum possible coding delay by disabling certain modes
 * of operation.</dd>
 * </dl>
 * @param[out] error <tt>int *</tt>: Returns #OAC_OK on success, or an error
 *                                   code (see @ref oac_errorcodes) on
 *                                   failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT OacMSEncoder *oac_multistream_encoder_create(
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping,
    int application,
    int *error) OAC_ARG_NONNULL(5);

OAC_EXPORT OAC_WARN_UNUSED_RESULT OacMSEncoder *oac_multistream_surround_encoder_create(
    oac_int32 Fs,
    int channels,
    int mapping_family,
    int *streams,
    int *coupled_streams,
    unsigned char *mapping,
    int application,
    int *error) OAC_ARG_NONNULL(4) OAC_ARG_NONNULL(5) OAC_ARG_NONNULL(6);

/** Initialize a previously allocated multistream encoder state.
 * The memory pointed to by \a st must be at least the size returned by
 * oac_multistream_encoder_get_size().
 * This is intended for applications which use their own allocator instead of
 * malloc.
 * To reset a previously initialized state, use the #OAC_RESET_STATE CTL.
 * @see oac_multistream_encoder_create
 * @see oac_multistream_encoder_get_size
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state to initialize.
 * @param Fs <tt>oac_int32</tt>: Sampling rate of the input signal (in Hz).
 *                                This must be one of 8000, 12000, 16000,
 *                                24000, or 48000.
 * @param channels <tt>int</tt>: Number of channels in the input signal.
 *                               This must be at most 255.
 *                               It may be greater than the number of
 *                               coded channels (<code>streams +
 *                               coupled_streams</code>).
 * @param streams <tt>int</tt>: The total number of streams to encode from the
 *                              input.
 *                              This must be no more than the number of channels.
 * @param coupled_streams <tt>int</tt>: Number of coupled (2 channel) streams
 *                                      to encode.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      encoded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than the number of input channels.
 * @param[in] mapping <code>const unsigned char[channels]</code>: Mapping from
 *                    encoded channels to input channels, as described in
 *                    @ref oac_multistream. As an extra constraint, the
 *                    multistream encoder does not allow encoding coupled
 *                    streams for which one channel is unused since this
 *                    is never a good idea.
 * @param application <tt>int</tt>: The target encoder application.
 *                                  This must be one of the following:
 * <dl>
 * <dt>#OAC_APPLICATION_VOIP</dt>
 * <dd>Process signal for improved speech intelligibility.</dd>
 * <dt>#OAC_APPLICATION_AUDIO</dt>
 * <dd>Favor faithfulness to the original input.</dd>
 * <dt>#OAC_APPLICATION_RESTRICTED_LOWDELAY</dt>
 * <dd>Configure the minimum possible coding delay by disabling certain modes
 * of operation.</dd>
 * </dl>
 * @returns #OAC_OK on success, or an error code (see @ref oac_errorcodes)
 *          on failure.
 */
OAC_EXPORT int oac_multistream_encoder_init(
    OacMSEncoder *st,
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping,
    int application) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(6);

OAC_EXPORT int oac_multistream_surround_encoder_init(
    OacMSEncoder *st,
    oac_int32 Fs,
    int channels,
    int mapping_family,
    int *streams,
    int *coupled_streams,
    unsigned char *mapping,
    int application) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(5) OAC_ARG_NONNULL(6) OAC_ARG_NONNULL(7);

/** Encodes a multistream Oac frame.
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state.
 * @param[in] pcm <tt>const oac_int16*</tt>: The input signal as interleaved
 *                                            samples.
 *                                            This must contain
 *                                            <code>frame_size*channels</code>
 *                                            samples.
 * @param frame_size <tt>int</tt>: Number of samples per channel in the input
 *                                 signal.
 *                                 This must be an Oac frame size for the
 *                                 encoder's sampling rate.
 *                                 For example, at 48 kHz the permitted values
 *                                 are 120, 240, 480, 960, 1920, and 2880.
 *                                 Passing in a duration of less than 10 ms
 *                                 (480 samples at 48 kHz) will prevent the
 *                                 encoder from using the LPC or hybrid modes.
 * @param[out] data <tt>unsigned char*</tt>: Output payload.
 *                                           This must contain storage for at
 *                                           least \a max_data_bytes.
 * @param [in] max_data_bytes <tt>oac_int32</tt>: Size of the allocated
 *                                                 memory for the output
 *                                                 payload. This may be
 *                                                 used to impose an upper limit on
 *                                                 the instant bitrate, but should
 *                                                 not be used as the only bitrate
 *                                                 control. Use #OAC_SET_BITRATE to
 *                                                 control the bitrate.
 * @returns The length of the encoded packet (in bytes) on success or a
 *          negative error code (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_encode(
    OacMSEncoder *st,
    const oac_int16 *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes a multistream Oac frame.
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state.
 * @param[in] pcm <tt>const oac_int32*</tt>: The input signal as interleaved
 *                                            samples representing (or slightly exceeding) 24-bit values.
 *                                            This must contain
 *                                            <code>frame_size*channels</code>
 *                                            samples.
 * @param frame_size <tt>int</tt>: Number of samples per channel in the input
 *                                 signal.
 *                                 This must be an Oac frame size for the
 *                                 encoder's sampling rate.
 *                                 For example, at 48 kHz the permitted values
 *                                 are 120, 240, 480, 960, 1920, and 2880.
 *                                 Passing in a duration of less than 10 ms
 *                                 (480 samples at 48 kHz) will prevent the
 *                                 encoder from using the LPC or hybrid modes.
 * @param[out] data <tt>unsigned char*</tt>: Output payload.
 *                                           This must contain storage for at
 *                                           least \a max_data_bytes.
 * @param [in] max_data_bytes <tt>oac_int32</tt>: Size of the allocated
 *                                                 memory for the output
 *                                                 payload. This may be
 *                                                 used to impose an upper limit on
 *                                                 the instant bitrate, but should
 *                                                 not be used as the only bitrate
 *                                                 control. Use #OAC_SET_BITRATE to
 *                                                 control the bitrate.
 * @returns The length of the encoded packet (in bytes) on success or a
 *          negative error code (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_encode24(
    OacMSEncoder *st,
    const oac_int32 *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes a multistream Oac frame from floating point input.
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state.
 * @param[in] pcm <tt>const float*</tt>: The input signal as interleaved
 *                                       samples with a normal range of
 *                                       +/-1.0.
 *                                       Samples with a range beyond +/-1.0
 *                                       are supported but will be clipped by
 *                                       decoders using the integer API and
 *                                       should only be used if it is known
 *                                       that the far end supports extended
 *                                       dynamic range.
 *                                       This must contain
 *                                       <code>frame_size*channels</code>
 *                                       samples.
 * @param frame_size <tt>int</tt>: Number of samples per channel in the input
 *                                 signal.
 *                                 This must be an Oac frame size for the
 *                                 encoder's sampling rate.
 *                                 For example, at 48 kHz the permitted values
 *                                 are 120, 240, 480, 960, 1920, and 2880.
 *                                 Passing in a duration of less than 10 ms
 *                                 (480 samples at 48 kHz) will prevent the
 *                                 encoder from using the LPC or hybrid modes.
 * @param[out] data <tt>unsigned char*</tt>: Output payload.
 *                                           This must contain storage for at
 *                                           least \a max_data_bytes.
 * @param [in] max_data_bytes <tt>oac_int32</tt>: Size of the allocated
 *                                                 memory for the output
 *                                                 payload. This may be
 *                                                 used to impose an upper limit on
 *                                                 the instant bitrate, but should
 *                                                 not be used as the only bitrate
 *                                                 control. Use #OAC_SET_BITRATE to
 *                                                 control the bitrate.
 * @returns The length of the encoded packet (in bytes) on success or a
 *          negative error code (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_encode_float(
    OacMSEncoder *st,
    const float *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Frees an <code>OacMSEncoder</code> allocated by
 * oac_multistream_encoder_create().
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state to be freed.
 */
OAC_EXPORT void oac_multistream_encoder_destroy(OacMSEncoder *st);

/** Perform a CTL function on a multistream Oac encoder.
 *
 * Generally the request and subsequent arguments are generated by a
 * convenience macro.
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state.
 * @param request This and all remaining parameters should be replaced by one
 *                of the convenience macros in @ref oac_genericctls,
 *                @ref oac_encoderctls, or @ref oac_multistream_ctls.
 * @see oac_genericctls
 * @see oac_encoderctls
 * @see oac_multistream_ctls
 */
OAC_EXPORT int oac_multistream_encoder_ctl(OacMSEncoder *st, int request, ...) OAC_ARG_NONNULL(1);

/**@}*/

/**\name Multistream decoder functions */
/**@{*/

/** Gets the size of an <code>OacMSDecoder</code> structure.
 * @param streams <tt>int</tt>: The total number of streams coded in the
 *                              input.
 *                              This must be no more than 255.
 * @param coupled_streams <tt>int</tt>: Number streams to decode as coupled
 *                                      (2 channel) streams.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      coded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than 255.
 * @returns The size in bytes on success, or a negative error code
 *          (see @ref oac_errorcodes) on error.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_multistream_decoder_get_size(
    int streams,
    int coupled_streams);

/** Allocates and initializes a multistream decoder state.
 * Call oac_multistream_decoder_destroy() to release
 * this object when finished.
 * @param Fs <tt>oac_int32</tt>: Sampling rate to decode at (in Hz).
 *                                This must be one of 8000, 12000, 16000,
 *                                24000, or 48000.
 * @param channels <tt>int</tt>: Number of channels to output.
 *                               This must be at most 255.
 *                               It may be different from the number of coded
 *                               channels (<code>streams +
 *                               coupled_streams</code>).
 * @param streams <tt>int</tt>: The total number of streams coded in the
 *                              input.
 *                              This must be no more than 255.
 * @param coupled_streams <tt>int</tt>: Number of streams to decode as coupled
 *                                      (2 channel) streams.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      coded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than 255.
 * @param[in] mapping <code>const unsigned char[channels]</code>: Mapping from
 *                    coded channels to output channels, as described in
 *                    @ref oac_multistream.
 * @param[out] error <tt>int *</tt>: Returns #OAC_OK on success, or an error
 *                                   code (see @ref oac_errorcodes) on
 *                                   failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT OacMSDecoder *oac_multistream_decoder_create(
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping,
    int *error) OAC_ARG_NONNULL(5);

/** Initialize a previously allocated decoder state object.
 * The memory pointed to by \a st must be at least the size returned by
 * oac_multistream_encoder_get_size().
 * This is intended for applications which use their own allocator instead of
 * malloc.
 * To reset a previously initialized state, use the #OAC_RESET_STATE CTL.
 * @see oac_multistream_decoder_create
 * @see oac_multistream_deocder_get_size
 * @param st <tt>OacMSEncoder*</tt>: Multistream encoder state to initialize.
 * @param Fs <tt>oac_int32</tt>: Sampling rate to decode at (in Hz).
 *                                This must be one of 8000, 12000, 16000,
 *                                24000, or 48000.
 * @param channels <tt>int</tt>: Number of channels to output.
 *                               This must be at most 255.
 *                               It may be different from the number of coded
 *                               channels (<code>streams +
 *                               coupled_streams</code>).
 * @param streams <tt>int</tt>: The total number of streams coded in the
 *                              input.
 *                              This must be no more than 255.
 * @param coupled_streams <tt>int</tt>: Number of streams to decode as coupled
 *                                      (2 channel) streams.
 *                                      This must be no larger than the total
 *                                      number of streams.
 *                                      Additionally, The total number of
 *                                      coded channels (<code>streams +
 *                                      coupled_streams</code>) must be no
 *                                      more than 255.
 * @param[in] mapping <code>const unsigned char[channels]</code>: Mapping from
 *                    coded channels to output channels, as described in
 *                    @ref oac_multistream.
 * @returns #OAC_OK on success, or an error code (see @ref oac_errorcodes)
 *          on failure.
 */
OAC_EXPORT int oac_multistream_decoder_init(
    OacMSDecoder *st,
    oac_int32 Fs,
    int channels,
    int streams,
    int coupled_streams,
    const unsigned char *mapping) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(6);

/** Decode a multistream Oac packet.
 * @param st <tt>OacMSDecoder*</tt>: Multistream decoder state.
 * @param[in] data <tt>const unsigned char*</tt>: Input payload.
 *                                                Use a <code>NULL</code>
 *                                                pointer to indicate packet
 *                                                loss.
 * @param len <tt>oac_int32</tt>: Number of bytes in payload.
 * @param[out] pcm <tt>oac_int16*</tt>: Output signal, with interleaved
 *                                       samples.
 *                                       This must contain room for
 *                                       <code>frame_size*channels</code>
 *                                       samples.
 * @param frame_size <tt>int</tt>: The number of samples per channel of
 *                                 available space in \a pcm.
 *                                 If this is less than the maximum packet duration
 *                                 (120 ms; 5760 for 48kHz), this function will not be capable
 *                                 of decoding some packets. In the case of PLC (data==NULL)
 *                                 or FEC (decode_fec=1), then frame_size needs to be exactly
 *                                 the duration of audio that is missing, otherwise the
 *                                 decoder will not be in the optimal state to decode the
 *                                 next incoming packet. For the PLC and FEC cases, frame_size
 *                                 <b>must</b> be a multiple of 2.5 ms.
 * @param decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band
 *                                 forward error correction data be decoded.
 *                                 If no such data is available, the frame is
 *                                 decoded as if it were lost.
 * @returns Number of samples decoded on success or a negative error code
 *          (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_decode(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int16 *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode a multistream Oac packet.
 * @param st <tt>OacMSDecoder*</tt>: Multistream decoder state.
 * @param[in] data <tt>const unsigned char*</tt>: Input payload.
 *                                                Use a <code>NULL</code>
 *                                                pointer to indicate packet
 *                                                loss.
 * @param len <tt>oac_int32</tt>: Number of bytes in payload.
 * @param[out] pcm <tt>oac_int32*</tt>: Output signal, with interleaved
 *                                       samples representing (or slightly exceeding) 24-bit values.
 *                                       This must contain room for
 *                                       <code>frame_size*channels</code>
 *                                       samples.
 * @param frame_size <tt>int</tt>: The number of samples per channel of
 *                                 available space in \a pcm.
 *                                 If this is less than the maximum packet duration
 *                                 (120 ms; 5760 for 48kHz), this function will not be capable
 *                                 of decoding some packets. In the case of PLC (data==NULL)
 *                                 or FEC (decode_fec=1), then frame_size needs to be exactly
 *                                 the duration of audio that is missing, otherwise the
 *                                 decoder will not be in the optimal state to decode the
 *                                 next incoming packet. For the PLC and FEC cases, frame_size
 *                                 <b>must</b> be a multiple of 2.5 ms.
 * @param decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band
 *                                 forward error correction data be decoded.
 *                                 If no such data is available, the frame is
 *                                 decoded as if it were lost.
 * @returns Number of samples decoded on success or a negative error code
 *          (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_decode24(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int32 *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode a multistream Oac packet with floating point output.
 * @param st <tt>OacMSDecoder*</tt>: Multistream decoder state.
 * @param[in] data <tt>const unsigned char*</tt>: Input payload.
 *                                                Use a <code>NULL</code>
 *                                                pointer to indicate packet
 *                                                loss.
 * @param len <tt>oac_int32</tt>: Number of bytes in payload.
 * @param[out] pcm <tt>oac_int16*</tt>: Output signal, with interleaved
 *                                       samples.
 *                                       This must contain room for
 *                                       <code>frame_size*channels</code>
 *                                       samples.
 * @param frame_size <tt>int</tt>: The number of samples per channel of
 *                                 available space in \a pcm.
 *                                 If this is less than the maximum packet duration
 *                                 (120 ms; 5760 for 48kHz), this function will not be capable
 *                                 of decoding some packets. In the case of PLC (data==NULL)
 *                                 or FEC (decode_fec=1), then frame_size needs to be exactly
 *                                 the duration of audio that is missing, otherwise the
 *                                 decoder will not be in the optimal state to decode the
 *                                 next incoming packet. For the PLC and FEC cases, frame_size
 *                                 <b>must</b> be a multiple of 2.5 ms.
 * @param decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band
 *                                 forward error correction data be decoded.
 *                                 If no such data is available, the frame is
 *                                 decoded as if it were lost.
 * @returns Number of samples decoded on success or a negative error code
 *          (see @ref oac_errorcodes) on failure.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_multistream_decode_float(
    OacMSDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    float *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Perform a CTL function on a multistream Oac decoder.
 *
 * Generally the request and subsequent arguments are generated by a
 * convenience macro.
 * @param st <tt>OacMSDecoder*</tt>: Multistream decoder state.
 * @param request This and all remaining parameters should be replaced by one
 *                of the convenience macros in @ref oac_genericctls,
 *                @ref oac_decoderctls, or @ref oac_multistream_ctls.
 * @see oac_genericctls
 * @see oac_decoderctls
 * @see oac_multistream_ctls
 */
OAC_EXPORT int oac_multistream_decoder_ctl(OacMSDecoder *st, int request, ...) OAC_ARG_NONNULL(1);

/** Frees an <code>OacMSDecoder</code> allocated by
 * oac_multistream_decoder_create().
 * @param st <tt>OacMSDecoder</tt>: Multistream decoder state to be freed.
 */
OAC_EXPORT void oac_multistream_decoder_destroy(OacMSDecoder *st);

/**@}*/

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* OAC_MULTISTREAM_H */

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
   Copyright (c) 2008-2012 Gregory Maxwell
   Written by Jean-Marc Valin and Gregory Maxwell */
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
   @file oac_custom.h
   @brief Oac-Custom reference implementation API
 */

#ifndef OAC_CUSTOM_H
#define OAC_CUSTOM_H

#include "oac_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API)
# define OAC_CUSTOM_EXPORT OAC_EXPORT
# define OAC_CUSTOM_EXPORT_STATIC OAC_EXPORT
#else
# define OAC_CUSTOM_EXPORT
# ifdef OAC_BUILD
#  define OAC_CUSTOM_EXPORT_STATIC static OAC_INLINE
# else
#  define OAC_CUSTOM_EXPORT_STATIC
# endif
#endif

/** @defgroup oac_custom Oac Custom
 * @{
 *  Oac Custom is an optional part of the Oac specification and
 * reference implementation which uses a distinct API from the regular
 * API and supports frame sizes that are not normally supported.\ Use
 * of Oac Custom is discouraged for all but very special applications
 * for which a frame size different from 2.5, 5, 10, or 20 ms is needed
 * (for either complexity or latency reasons) and where interoperability
 * is less important.
 *
 * In addition to the interoperability limitations the use of Oac custom
 * disables a substantial chunk of the codec and generally lowers the
 * quality available at a given bitrate. Normally when an application needs
 * a different frame size from the codec it should buffer to match the
 * sizes but this adds a small amount of delay which may be important
 * in some very low latency applications. Some transports (especially
 * constant rate RF transports) may also work best with frames of
 * particular durations.
 *
 * Liboac only supports custom modes if they are enabled at compile time.
 *
 * The Oac Custom API is similar to the regular API but the
 * @ref oac_encoder_create and @ref oac_decoder_create calls take
 * an additional mode parameter which is a structure produced by
 * a call to @ref oac_custom_mode_create. Both the encoder and decoder
 * must create a mode using the same sample rate (fs) and frame size
 * (frame size) so these parameters must either be signaled out of band
 * or fixed in a particular implementation.
 *
 * Similar to regular Oac the custom modes support on the fly frame size
 * switching, but the sizes available depend on the particular frame size in
 * use. For some initial frame sizes on a single on the fly size is available.
 */

/** Contains the state of an encoder. One encoder state is needed
    for each stream. It is initialized once at the beginning of the
    stream. Do *not* re-initialize the state for every frame.
   @brief Encoder state
 */
typedef struct OacCustomEncoder OacCustomEncoder;

/** State of the decoder. One decoder state is needed for each stream.
    It is initialized once at the beginning of the stream. Do *not*
    re-initialize the state for every frame.
   @brief Decoder state
 */
typedef struct OacCustomDecoder OacCustomDecoder;

/** The mode contains all the information necessary to create an
    encoder. Both the encoder and decoder need to be initialized
    with exactly the same mode, otherwise the output will be
    corrupted. The mode MUST NOT BE DESTROYED until the encoders and
    decoders that use it are destroyed as well.
   @brief Mode configuration
 */
typedef struct OacCustomMode OacCustomMode;

/** Creates a new mode struct. This will be passed to an encoder or
 * decoder. The mode MUST NOT BE DESTROYED until the encoders and
 * decoders that use it are destroyed as well.
 * @param [in] Fs <tt>int</tt>: Sampling rate (8000 to 96000 Hz)
 * @param [in] frame_size <tt>int</tt>: Number of samples (per channel) to encode in each
 *        packet (64 - 1024, prime factorization must contain zero or more 2s, 3s, or 5s and no other primes)
 * @param [out] error <tt>int*</tt>: Returned error code (if NULL, no error will be returned)
 * @return A newly created mode
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT OacCustomMode *oac_custom_mode_create(oac_int32 Fs, int frame_size,
    int *error);

/** Destroys a mode struct. Only call this after all encoders and
 * decoders using this mode are destroyed as well.
 * @param [in] mode <tt>OacCustomMode*</tt>: Mode to be freed.
 */
OAC_CUSTOM_EXPORT void oac_custom_mode_destroy(OacCustomMode *mode);


#if !defined(OAC_BUILD) || defined(CELT_ENCODER_C)

/* Encoder */
/** Gets the size of an OacCustomEncoder structure.
 * @param [in] mode <tt>OacCustomMode *</tt>: Mode configuration
 * @param [in] channels <tt>int</tt>: Number of channels
 * @returns size
 */
OAC_CUSTOM_EXPORT_STATIC OAC_WARN_UNUSED_RESULT int oac_custom_encoder_get_size(
    const OacCustomMode *mode,
    int channels) OAC_ARG_NONNULL(1);

# if defined(CUSTOM_MODES) || defined(ENABLE_OAC_CUSTOM_API)
/** Initializes a previously allocated encoder state
 * The memory pointed to by st must be the size returned by oac_custom_encoder_get_size.
 * This is intended for applications which use their own allocator instead of malloc.
 * @see oac_custom_encoder_create(),oac_custom_encoder_get_size()
 * To reset a previously initialized state use the OAC_RESET_STATE CTL.
 * @param [in] st <tt>OacCustomEncoder*</tt>: Encoder state
 * @param [in] mode <tt>OacCustomMode *</tt>: Contains all the information about the characteristics of
 *  the stream (must be the same characteristics as used for the
 *  decoder)
 * @param [in] channels <tt>int</tt>: Number of channels
 * @param [in] format <tt>int</tt>: Stream format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @return OAC_OK Success or @ref oac_errorcodes
 */
OAC_CUSTOM_EXPORT int oac_custom_encoder_init(
    OacCustomEncoder *st,
    const OacCustomMode *mode,
    int channels,
    int format) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2);
# endif
#endif


/** Creates a new encoder state. Each stream needs its own encoder
 * state (can't be shared across simultaneous streams).
 * @param [in] mode <tt>OacCustomMode*</tt>: Contains all the information about the characteristics of
 *  the stream (must be the same characteristics as used for the
 *  decoder)
 * @param [in] channels <tt>int</tt>: Number of channels
 * @param [in] format <tt>int</tt>: Stream format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @param [out] error <tt>int*</tt>: Returns an error code
 * @return Newly created encoder state.
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT OacCustomEncoder *oac_custom_encoder_create(
    const OacCustomMode *mode,
    int channels,
    int format,
    int *error) OAC_ARG_NONNULL(1);


/** Destroys an encoder state.
 * @param[in] st <tt>OacCustomEncoder*</tt>: State to be freed.
 */
OAC_CUSTOM_EXPORT void oac_custom_encoder_destroy(OacCustomEncoder *st);

/** Encodes a frame of audio.
 * @param [in] st <tt>OacCustomEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>float*</tt>: PCM audio in float format, with a normal range of +/-1.0.
 *          Samples with a range beyond +/-1.0 are supported but will
 *          be clipped by decoders using the integer API and should
 *          only be used if it is known that the far end supports
 *          extended dynamic range. There must be exactly
 *          frame_size samples per channel.
 * @param [in] frame_size <tt>int</tt>: Number of samples per frame of input signal
 * @param [out] compressed <tt>char *</tt>: The compressed data is written here. This may not alias pcm and must be at least maxCompressedBytes long.
 * @param [in] maxCompressedBytes <tt>int</tt>: Maximum number of bytes to use for compressing the frame
 *          (can change from one frame to another)
 * @return Number of bytes written to "compressed".
 *       If negative, an error has occurred (see error codes). It is IMPORTANT that
 *       the length returned be somehow transmitted to the decoder. Otherwise, no
 *       decoding is possible.
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_encode_float(
    OacCustomEncoder *st,
    const float *pcm,
    int frame_size,
    unsigned char *compressed,
    int maxCompressedBytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes a frame of audio.
 * @param [in] st <tt>OacCustomEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>oac_int16*</tt>: PCM audio in signed 16-bit format (native endian).
 *          There must be exactly frame_size samples per channel.
 * @param [in] frame_size <tt>int</tt>: Number of samples per frame of input signal
 * @param [out] compressed <tt>char *</tt>: The compressed data is written here. This may not alias pcm and must be at least maxCompressedBytes long.
 * @param [in] maxCompressedBytes <tt>int</tt>: Maximum number of bytes to use for compressing the frame
 *          (can change from one frame to another)
 * @return Number of bytes written to "compressed".
 *       If negative, an error has occurred (see error codes). It is IMPORTANT that
 *       the length returned be somehow transmitted to the decoder. Otherwise, no
 *       decoding is possible.
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_encode(
    OacCustomEncoder *st,
    const oac_int16 *pcm,
    int frame_size,
    unsigned char *compressed,
    int maxCompressedBytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes a frame of audio.
 * @param [in] st <tt>OacCustomEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>oac_int32*</tt>: PCM audio in signed 32-bit format (native endian) representing (or slightly exceeding) 24-bit values.
 *          There must be exactly frame_size samples per channel.
 * @param [in] frame_size <tt>int</tt>: Number of samples per frame of input signal
 * @param [out] compressed <tt>char *</tt>: The compressed data is written here. This may not alias pcm and must be at least maxCompressedBytes long.
 * @param [in] maxCompressedBytes <tt>int</tt>: Maximum number of bytes to use for compressing the frame
 *          (can change from one frame to another)
 * @return Number of bytes written to "compressed".
 *       If negative, an error has occurred (see error codes). It is IMPORTANT that
 *       the length returned be somehow transmitted to the decoder. Otherwise, no
 *       decoding is possible.
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_encode24(
    OacCustomEncoder *st,
    const oac_int32 *pcm,
    int frame_size,
    unsigned char *compressed,
    int maxCompressedBytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Perform a CTL function on an Oac custom encoder.
 *
 * Generally the request and subsequent arguments are generated
 * by a convenience macro.
 * @see oac_encoderctls
 */
OAC_CUSTOM_EXPORT int oac_custom_encoder_ctl(OacCustomEncoder * OAC_RESTRICT st, int request, ...) OAC_ARG_NONNULL(1);


#if !defined(OAC_BUILD) || defined(CELT_DECODER_C)
/* Decoder */

/** Gets the size of an OacCustomDecoder structure.
 * @param [in] mode <tt>OacCustomMode *</tt>: Mode configuration
 * @param [in] channels <tt>int</tt>: Number of channels
 * @returns size
 */
OAC_CUSTOM_EXPORT_STATIC OAC_WARN_UNUSED_RESULT int oac_custom_decoder_get_size(
    const OacCustomMode *mode,
    int channels) OAC_ARG_NONNULL(1);

/** Initializes a previously allocated decoder state
 * The memory pointed to by st must be the size returned by oac_custom_decoder_get_size.
 * This is intended for applications which use their own allocator instead of malloc.
 * @see oac_custom_decoder_create(),oac_custom_decoder_get_size()
 * To reset a previously initialized state use the OAC_RESET_STATE CTL.
 * @param [in] st <tt>OacCustomDecoder*</tt>: Decoder state
 * @param [in] mode <tt>OacCustomMode *</tt>: Contains all the information about the characteristics of
 *  the stream (must be the same characteristics as used for the
 *  encoder)
 * @param [in] channels <tt>int</tt>: Number of channels
 * @param [in] format <tt>int</tt>: Stream format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @return OAC_OK Success or @ref oac_errorcodes
 */
OAC_CUSTOM_EXPORT_STATIC int oac_custom_decoder_init(
    OacCustomDecoder *st,
    const OacCustomMode *mode,
    int channels,
    int format) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2);

#endif


/** Creates a new decoder state. Each stream needs its own decoder state (can't
 * be shared across simultaneous streams).
 * @param [in] mode <tt>OacCustomMode</tt>: Contains all the information about the characteristics of the
 *          stream (must be the same characteristics as used for the encoder)
 * @param [in] channels <tt>int</tt>: Number of channels
 * @param [in] format <tt>int</tt>: Stream format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @param [out] error <tt>int*</tt>: Returns an error code
 * @return Newly created decoder state.
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT OacCustomDecoder *oac_custom_decoder_create(
    const OacCustomMode *mode,
    int channels,
    int format,
    int *error) OAC_ARG_NONNULL(1);

/** Destroys a decoder state.
 * @param[in] st <tt>OacCustomDecoder*</tt>: State to be freed.
 */
OAC_CUSTOM_EXPORT void oac_custom_decoder_destroy(OacCustomDecoder *st);

/** Decode an oac custom frame with floating point output
 * @param [in] st <tt>OacCustomDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>int</tt>: Number of bytes in payload
 * @param [out] pcm <tt>float*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(float)
 * @param [in] frame_size Number of samples per channel of available space in *pcm.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_decode_float(
    OacCustomDecoder *st,
    const unsigned char *data,
    int len,
    float *pcm,
    int frame_size) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode an oac custom frame
 * @param [in] st <tt>OacCustomDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>int</tt>: Number of bytes in payload
 * @param [out] pcm <tt>oac_int16*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(oac_int16)
 * @param [in] frame_size Number of samples per channel of available space in *pcm.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_decode(
    OacCustomDecoder *st,
    const unsigned char *data,
    int len,
    oac_int16 *pcm,
    int frame_size) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode an oac custom frame
 * @param [in] st <tt>OacCustomDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>int</tt>: Number of bytes in payload
 * @param [out] pcm <tt>oac_int32*</tt>: Output signal (interleaved if 2 channels) representing (or slightly exceeding) 24-bit values. length
 *  is frame_size*channels*sizeof(oac_int32)
 * @param [in] frame_size Number of samples per channel of available space in *pcm.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_CUSTOM_EXPORT OAC_WARN_UNUSED_RESULT int oac_custom_decode24(
    OacCustomDecoder *st,
    const unsigned char *data,
    int len,
    oac_int32 *pcm,
    int frame_size) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Perform a CTL function on an Oac custom decoder.
 *
 * Generally the request and subsequent arguments are generated
 * by a convenience macro.
 * @see oac_genericctls
 */
OAC_CUSTOM_EXPORT int oac_custom_decoder_ctl(OacCustomDecoder * OAC_RESTRICT st, int request, ...) OAC_ARG_NONNULL(1);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* OAC_CUSTOM_H */

/* Copyright (c) 2010-2011 Xiph.Org Foundation, Skype Limited
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

/**
 * @file oac.h
 * @brief Oac reference implementation API
 */

#ifndef OAC_H
#define OAC_H

#include "oac_types.h"
#include "oac_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @mainpage Oac
 *
 * The Oac codec is designed for interactive speech and audio transmission over the Internet.
 * It is designed by the IETF Codec Working Group and incorporates technology from
 * Skype's SILK codec and Xiph.Org's CELT codec.
 *
 * The Oac codec is designed to handle a wide range of interactive audio applications,
 * including Voice over IP, videoconferencing, in-game chat, and even remote live music
 * performances. It can scale from low bit-rate narrowband speech to very high quality
 * stereo music. Its main features are:

 * @li Sampling rates from 8 to 48 kHz
 * @li Bit-rates from 6 kb/s to 510 kb/s
 * @li Support for both constant bit-rate (CBR) and variable bit-rate (VBR)
 * @li Audio bandwidth from narrowband to full-band
 * @li Support for speech and music
 * @li Support for mono and stereo
 * @li Support for multichannel (up to 255 channels)
 * @li Frame sizes from 2.5 ms to 60 ms
 * @li Good loss robustness and packet loss concealment (PLC)
 * @li Floating point and fixed-point implementation
 *
 * Documentation sections:
 * @li @ref oac_encoder
 * @li @ref oac_decoder
 * @li @ref oac_repacketizer
 * @li @ref oac_multistream
 * @li @ref oac_libinfo
 * @li @ref oac_custom
 */

/** @defgroup oac_encoder Oac Encoder
 * @{
 *
 * @brief This page describes the process and functions used to encode Oac.
 *
 * Since Oac is a stateful codec, the encoding process starts with creating an encoder
 * state. This can be done with:
 *
 * @code
 * int          error;
 * OacEncoder *enc;
 * enc = oac_encoder_create(Fs, channels, application, &error);
 * @endcode
 *
 * From this point, @c enc can be used for encoding an audio stream. An encoder state
 * @b must @b not be used for more than one stream at the same time. Similarly, the encoder
 * state @b must @b not be re-initialized for each frame.
 *
 * While oac_encoder_create() allocates memory for the state, it's also possible
 * to initialize pre-allocated memory:
 *
 * @code
 * int          size;
 * int          error;
 * OacEncoder *enc;
 * size = oac_encoder_get_size(channels);
 * enc = malloc(size);
 * error = oac_encoder_init(enc, Fs, channels, application);
 * @endcode
 *
 * where oac_encoder_get_size() returns the required size for the encoder state. Note that
 * future versions of this code may change the size, so no assumptions should be made about it.
 *
 * The encoder state is always continuous in memory and only a shallow copy is sufficient
 * to copy it (e.g. memcpy())
 *
 * It is possible to change some of the encoder's settings using the oac_encoder_ctl()
 * interface. All these settings already default to the recommended value, so they should
 * only be changed when necessary. The most common settings one may want to change are:
 *
 * @code
 * oac_encoder_ctl(enc, OAC_SET_BITRATE(bitrate));
 * oac_encoder_ctl(enc, OAC_SET_COMPLEXITY(complexity));
 * oac_encoder_ctl(enc, OAC_SET_SIGNAL(signal_type));
 * @endcode
 *
 * where
 *
 * @arg bitrate is in bits per second (b/s)
 * @arg complexity is a value from 1 to 10, where 1 is the lowest complexity and 10 is the highest
 * @arg signal_type is either OAC_AUTO (default), OAC_SIGNAL_VOICE, or OAC_SIGNAL_MUSIC
 *
 * See @ref oac_encoderctls and @ref oac_genericctls for a complete list of parameters that can be set or queried. Most parameters can be set or changed at any time during a stream.
 *
 * To encode a frame, oac_encode() or oac_encode_float() must be called with exactly one frame (2.5, 5, 10, 20, 40 or 60 ms) of audio data:
 * @code
 * len = oac_encode(enc, audio_frame, frame_size, packet, max_packet);
 * @endcode
 *
 * where
 * <ul>
 * <li>audio_frame is the audio data in oac_int16 (or float for oac_encode_float())</li>
 * <li>frame_size is the duration of the frame in samples (per channel)</li>
 * <li>packet is the byte array to which the compressed data is written</li>
 * <li>max_packet is the maximum number of bytes that can be written in the packet (4000 bytes is recommended).
 *     Do not use max_packet to control VBR target bitrate, instead use the #OAC_SET_BITRATE CTL.</li>
 * </ul>
 *
 * oac_encode() and oac_encode_float() return the number of bytes actually written to the packet.
 * The return value <b>can be negative</b>, which indicates that an error has occurred. If the return value
 * is 2 bytes or less, then the packet does not need to be transmitted (DTX).
 *
 * Once the encoder state if no longer needed, it can be destroyed with
 *
 * @code
 * oac_encoder_destroy(enc);
 * @endcode
 *
 * If the encoder was created with oac_encoder_init() rather than oac_encoder_create(),
 * then no action is required aside from potentially freeing the memory that was manually
 * allocated for it (calling free(enc) for the example above)
 *
 */

/** Oac encoder state.
 * This contains the complete state of an Oac encoder.
 * It is position independent and can be freely copied.
 * @see oac_encoder_create,oac_encoder_init
 */
typedef struct OacEncoder OacEncoder;

/** Gets the size of an <code>OacEncoder</code> structure.
 * @param[in] channels <tt>int</tt>: Number of channels.
 *                                   For OAC_FORMAT_STANDARD: 1 or 2.
 *                                   For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param[in] format <tt>int</tt>: Format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS).
 * @returns The size in bytes, or 0 on error.
 * @note Since this function does not take the application as input, it will overestimate
 * the size required for OAC_APPLICATION_RESTRICTED_SILK and OAC_APPLICATION_RESTRICTED_CELT.
 * That is generally not a problem, except when trying to know the size to use for a copy.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_encoder_get_size(int channels, int format);

/**
 */

/** Allocates and initializes an encoder state.
 * There are three coding modes:
 *
 * @ref OAC_APPLICATION_VOIP gives best quality at a given bitrate for voice
 *    signals. It enhances the  input signal by high-pass filtering and
 *    emphasizing formants and harmonics. Optionally  it includes in-band
 *    forward error correction to protect against packet loss. Use this
 *    mode for typical VoIP applications. Because of the enhancement,
 *    even at high bitrates the output may sound different from the input.
 *
 * @ref OAC_APPLICATION_AUDIO gives best quality at a given bitrate for most
 *    non-voice signals like music. Use this mode for music and mixed
 *    (music/voice) content, broadcast, and applications requiring less
 *    than 15 ms of coding delay.
 *
 * @ref OAC_APPLICATION_RESTRICTED_LOWDELAY configures low-delay mode that
 *    disables the speech-optimized mode in exchange for slightly reduced delay.
 *    This mode can only be set on an newly initialized or freshly reset encoder
 *    because it changes the codec delay.
 *
 * This is useful when the caller knows that the speech-optimized modes will not be needed (use with caution).
 * @param [in] Fs <tt>oac_int32</tt>: Sampling rate of input signal (Hz)
 *                                     This must be one of 8000, 12000, 16000,
 *                                     24000, or 48000.
 * @param [in] channels <tt>int</tt>: Number of channels in input signal.
 *                                    For OAC_FORMAT_STANDARD: 1 or 2.
 *                                    For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param [in] format <tt>int</tt>: Audio format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @param [in] application <tt>int</tt>: Coding mode (one of @ref OAC_APPLICATION_VOIP, @ref OAC_APPLICATION_AUDIO, or @ref OAC_APPLICATION_RESTRICTED_LOWDELAY)
 * @param [out] error <tt>int*</tt>: @ref oac_errorcodes
 * @note Regardless of the sampling rate and number channels selected, the Oac encoder
 * can switch to a lower audio bandwidth or number of channels if the bitrate
 * selected is too low. This also means that it is safe to always use 48 kHz stereo input
 * and let the encoder optimize the encoding.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT OacEncoder *oac_encoder_create(
    oac_int32 Fs,
    int channels,
    int format,
    int application,
    int *error);

/** Initializes a previously allocated encoder state
 * The memory pointed to by st must be at least the size returned by oac_encoder_get_size().
 * This is intended for applications which use their own allocator instead of malloc.
 * @see oac_encoder_create(),oac_encoder_get_size()
 * To reset a previously initialized state, use the #OAC_RESET_STATE CTL.
 * @param [in] st <tt>OacEncoder*</tt>: Encoder state
 * @param [in] Fs <tt>oac_int32</tt>: Sampling rate of input signal (Hz)
 *                                      This must be one of 8000, 12000, 16000,
 *                                      24000, or 48000.
 * @param [in] channels <tt>int</tt>: Number of channels in input signal.
 *                                    For OAC_FORMAT_STANDARD: 1 or 2.
 *                                    For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param [in] format <tt>int</tt>: Audio format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @param [in] application <tt>int</tt>: Coding mode (one of OAC_APPLICATION_VOIP, OAC_APPLICATION_AUDIO, or OAC_APPLICATION_RESTRICTED_LOWDELAY)
 * @retval #OAC_OK Success or @ref oac_errorcodes
 */
OAC_EXPORT int oac_encoder_init(
    OacEncoder *st,
    oac_int32 Fs,
    int channels,
    int format,
    int application) OAC_ARG_NONNULL(1);

/** Encodes an Oac frame.
 * @param [in] st <tt>OacEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>oac_int16*</tt>: Input signal (interleaved if 2 channels). length is frame_size*channels*sizeof(oac_int16)
 * @param [in] frame_size <tt>int</tt>: Number of samples per channel in the
 *                                      input signal.
 *                                      This must be an Oac frame size for
 *                                      the encoder's sampling rate.
 *                                      For example, at 48 kHz the permitted
 *                                      values are 120, 240, 480, 960, 1920,
 *                                      and 2880.
 *                                      Passing in a duration of less than
 *                                      10 ms (480 samples at 48 kHz) will
 *                                      prevent the encoder from using the LPC
 *                                      or hybrid modes.
 * @param [out] data <tt>unsigned char*</tt>: Output payload.
 *                                            This must contain storage for at
 *                                            least \a max_data_bytes.
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
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_encode(
    OacEncoder *st,
    const oac_int16 *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes an Oac frame.
 * @param [in] st <tt>OacEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>oac_int32*</tt>: Input signal (interleaved if 2 channels) representing (or slightly exceeding) 24-bit values. length is frame_size*channels*sizeof(oac_int32)
 * @param [in] frame_size <tt>int</tt>: Number of samples per channel in the
 *                                      input signal.
 *                                      This must be an Oac frame size for
 *                                      the encoder's sampling rate.
 *                                      For example, at 48 kHz the permitted
 *                                      values are 120, 240, 480, 960, 1920,
 *                                      and 2880.
 *                                      Passing in a duration of less than
 *                                      10 ms (480 samples at 48 kHz) will
 *                                      prevent the encoder from using the LPC
 *                                      or hybrid modes.
 * @param [out] data <tt>unsigned char*</tt>: Output payload.
 *                                            This must contain storage for at
 *                                            least \a max_data_bytes.
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
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_encode24(
    OacEncoder *st,
    const oac_int32 *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Encodes an Oac frame from floating point input.
 * @param [in] st <tt>OacEncoder*</tt>: Encoder state
 * @param [in] pcm <tt>float*</tt>: Input in float format (interleaved if 2 channels), with a normal range of +/-1.0.
 *          Samples with a range beyond +/-1.0 are supported but will
 *          be clipped by decoders using the integer API and should
 *          only be used if it is known that the far end supports
 *          extended dynamic range.
 *          length is frame_size*channels*sizeof(float)
 * @param [in] frame_size <tt>int</tt>: Number of samples per channel in the
 *                                      input signal.
 *                                      This must be an Oac frame size for
 *                                      the encoder's sampling rate.
 *                                      For example, at 48 kHz the permitted
 *                                      values are 120, 240, 480, 960, 1920,
 *                                      and 2880.
 *                                      Passing in a duration of less than
 *                                      10 ms (480 samples at 48 kHz) will
 *                                      prevent the encoder from using the LPC
 *                                      or hybrid modes.
 * @param [out] data <tt>unsigned char*</tt>: Output payload.
 *                                            This must contain storage for at
 *                                            least \a max_data_bytes.
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
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_encode_float(
    OacEncoder *st,
    const float *pcm,
    int frame_size,
    unsigned char *data,
    oac_int32 max_data_bytes) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2) OAC_ARG_NONNULL(4);

/** Frees an <code>OacEncoder</code> allocated by oac_encoder_create().
 * @param[in] st <tt>OacEncoder*</tt>: State to be freed.
 */
OAC_EXPORT void oac_encoder_destroy(OacEncoder *st);

/** Perform a CTL function on an Oac encoder.
 *
 * Generally the request and subsequent arguments are generated
 * by a convenience macro.
 * @param st <tt>OacEncoder*</tt>: Encoder state.
 * @param request This and all remaining parameters should be replaced by one
 *                of the convenience macros in @ref oac_genericctls or
 *                @ref oac_encoderctls.
 * @see oac_genericctls
 * @see oac_encoderctls
 */
OAC_EXPORT int oac_encoder_ctl(OacEncoder *st, int request, ...) OAC_ARG_NONNULL(1);
/**@}*/

/** @defgroup oac_decoder Oac Decoder
 * @{
 *
 * @brief This page describes the process and functions used to decode Oac.
 *
 * The decoding process also starts with creating a decoder
 * state. This can be done with:
 * @code
 * int          error;
 * OacDecoder *dec;
 * dec = oac_decoder_create(Fs, channels, &error);
 * @endcode
 * where
 * @li Fs is the sampling rate and must be 8000, 12000, 16000, 24000, or 48000
 * @li channels is the number of channels (1 or 2)
 * @li error will hold the error code in case of failure (or #OAC_OK on success)
 * @li the return value is a newly created decoder state to be used for decoding
 *
 * While oac_decoder_create() allocates memory for the state, it's also possible
 * to initialize pre-allocated memory:
 * @code
 * int          size;
 * int          error;
 * OacDecoder *dec;
 * size = oac_decoder_get_size(channels);
 * dec = malloc(size);
 * error = oac_decoder_init(dec, Fs, channels);
 * @endcode
 * where oac_decoder_get_size() returns the required size for the decoder state. Note that
 * future versions of this code may change the size, so no assumptions should be made about it.
 *
 * The decoder state is always continuous in memory and only a shallow copy is sufficient
 * to copy it (e.g. memcpy())
 *
 * To decode a frame, oac_decode() or oac_decode_float() must be called with a packet of compressed audio data:
 * @code
 * frame_size = oac_decode(dec, packet, len, decoded, max_size, 0);
 * @endcode
 * where
 *
 * @li packet is the byte array containing the compressed data
 * @li len is the exact number of bytes contained in the packet
 * @li decoded is the decoded audio data in oac_int16 (or float for oac_decode_float())
 * @li max_size is the max duration of the frame in samples (per channel) that can fit into the decoded_frame array
 *
 * oac_decode() and oac_decode_float() return the number of samples (per channel) decoded from the packet.
 * If that value is negative, then an error has occurred. This can occur if the packet is corrupted or if the audio
 * buffer is too small to hold the decoded audio.
 *
 * Oac is a stateful codec with overlapping blocks and as a result Oac
 * packets are not coded independently of each other. Packets must be
 * passed into the decoder serially and in the correct order for a correct
 * decode. Lost packets can be replaced with loss concealment by calling
 * the decoder with a null pointer and zero length for the missing packet.
 *
 * A single codec state may only be accessed from a single thread at
 * a time and any required locking must be performed by the caller. Separate
 * streams must be decoded with separate decoder states and can be decoded
 * in parallel unless the library was compiled with NONTHREADSAFE_PSEUDOSTACK
 * defined.
 *
 */

/** Oac decoder state.
 * This contains the complete state of an Oac decoder.
 * It is position independent and can be freely copied.
 * @see oac_decoder_create,oac_decoder_init
 */
typedef struct OacDecoder OacDecoder;

/** Oac DRED decoder.
 * This contains the complete state of an Oac DRED decoder.
 * It is position independent and can be freely copied.
 * @see oac_dred_decoder_create,oac_dred_decoder_init
 */
typedef struct OacDREDDecoder OacDREDDecoder;


/** Oac DRED state.
 * This contains the complete state of an Oac DRED packet.
 * It is position independent and can be freely copied.
 * @see oac_dred_create,oac_dred_init
 */
typedef struct OacDRED OacDRED;

/** Gets the size of an <code>OacDecoder</code> structure.
 * @param [in] channels <tt>int</tt>: Number of channels.
 *                                    For OAC_FORMAT_STANDARD: 1 or 2.
 *                                    For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param [in] format <tt>int</tt>: Format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS).
 * @returns The size in bytes, or 0 on error.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_decoder_get_size(int channels, int format);

/** Allocates and initializes a decoder state.
 * @param [in] Fs <tt>oac_int32</tt>: Sample rate to decode at (Hz).
 *                                     This must be one of 8000, 12000, 16000,
 *                                     24000, or 48000.
 * @param [in] channels <tt>int</tt>: Number of channels to decode.
 *                                    For OAC_FORMAT_STANDARD: 1 or 2.
 *                                    For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param [in] format <tt>int</tt>: Audio format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @param [out] error <tt>int*</tt>: #OAC_OK Success or @ref oac_errorcodes
 *
 * Internally Oac stores data at 48000 Hz, so that should be the default
 * value for Fs. However, the decoder can efficiently decode to buffers
 * at 8, 12, 16, and 24 kHz so if for some reason the caller cannot use
 * data at the full sample rate, or knows the compressed data doesn't
 * use the full frequency range, it can request decoding at a reduced
 * rate. Likewise, the decoder is capable of filling in either mono or
 * interleaved stereo pcm buffers, at the caller's request.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT OacDecoder *oac_decoder_create(
    oac_int32 Fs,
    int channels,
    int format,
    int *error);

/** Initializes a previously allocated decoder state.
 * The state must be at least the size returned by oac_decoder_get_size().
 * This is intended for applications which use their own allocator instead of malloc. @see oac_decoder_create,oac_decoder_get_size
 * To reset a previously initialized state, use the #OAC_RESET_STATE CTL.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state.
 * @param [in] Fs <tt>oac_int32</tt>: Sampling rate to decode to (Hz).
 *                                     This must be one of 8000, 12000, 16000,
 *                                     24000, or 48000.
 * @param [in] channels <tt>int</tt>: Number of channels to decode.
 *                                    For OAC_FORMAT_STANDARD: 1 or 2.
 *                                    For OAC_FORMAT_AMBISONICS: 1, 4, 9, 16, 25, or 36.
 * @param [in] format <tt>int</tt>: Audio format (OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS)
 * @retval #OAC_OK Success or @ref oac_errorcodes
 */
OAC_EXPORT int oac_decoder_init(
    OacDecoder *st,
    oac_int32 Fs,
    int channels,
    int format) OAC_ARG_NONNULL(1);

/** Decode an Oac packet.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>oac_int32</tt>: Number of bytes in payload*
 * @param [out] pcm <tt>oac_int16*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(oac_int16)
 * @param [in] frame_size Number of samples per channel of available space in \a pcm.
 *  If this is less than the maximum packet duration (120ms; 5760 for 48kHz), this function will
 *  not be capable of decoding some packets. In the case of PLC (data==NULL) or FEC (decode_fec=1),
 *  then frame_size needs to be exactly the duration of audio that is missing, otherwise the
 *  decoder will not be in the optimal state to decode the next incoming packet. For the PLC and
 *  FEC cases, frame_size <b>must</b> be a multiple of 2.5 ms.
 * @param [in] decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band forward error correction data be
 *  decoded. If no such data is available, the frame is decoded as if it were lost.
 * @returns Number of decoded samples per channel or @ref oac_errorcodes
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_decode(
    OacDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int16 *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode an Oac packet.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>oac_int32</tt>: Number of bytes in payload*
 * @param [out] pcm <tt>oac_int32*</tt>: Output signal (interleaved if 2 channels) representing (or slightly exceeding) 24-bit values. length
 *  is frame_size*channels*sizeof(oac_int32)
 * @param [in] frame_size Number of samples per channel of available space in \a pcm.
 *  If this is less than the maximum packet duration (120ms; 5760 for 48kHz), this function will
 *  not be capable of decoding some packets. In the case of PLC (data==NULL) or FEC (decode_fec=1),
 *  then frame_size needs to be exactly the duration of audio that is missing, otherwise the
 *  decoder will not be in the optimal state to decode the next incoming packet. For the PLC and
 *  FEC cases, frame_size <b>must</b> be a multiple of 2.5 ms.
 * @param [in] decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band forward error correction data be
 *  decoded. If no such data is available, the frame is decoded as if it were lost.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_decode24(
    OacDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    oac_int32 *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Decode an Oac packet with floating point output.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] data <tt>char*</tt>: Input payload. Use a NULL pointer to indicate packet loss
 * @param [in] len <tt>oac_int32</tt>: Number of bytes in payload
 * @param [out] pcm <tt>float*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(float)
 * @param [in] frame_size Number of samples per channel of available space in \a pcm.
 *  If this is less than the maximum packet duration (120ms; 5760 for 48kHz), this function will
 *  not be capable of decoding some packets. In the case of PLC (data==NULL) or FEC (decode_fec=1),
 *  then frame_size needs to be exactly the duration of audio that is missing, otherwise the
 *  decoder will not be in the optimal state to decode the next incoming packet. For the PLC and
 *  FEC cases, frame_size <b>must</b> be a multiple of 2.5 ms.
 * @param [in] decode_fec <tt>int</tt>: Flag (0 or 1) to request that any in-band forward error correction data be
 *  decoded. If no such data is available the frame is decoded as if it were lost.
 * @returns Number of decoded samples per channel or @ref oac_errorcodes
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_decode_float(
    OacDecoder *st,
    const unsigned char *data,
    oac_int32 len,
    float *pcm,
    int frame_size,
    int decode_fec) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Perform a CTL function on an Oac decoder.
 *
 * Generally the request and subsequent arguments are generated
 * by a convenience macro.
 * @param st <tt>OacDecoder*</tt>: Decoder state.
 * @param request This and all remaining parameters should be replaced by one
 *                of the convenience macros in @ref oac_genericctls or
 *                @ref oac_decoderctls.
 * @see oac_genericctls
 * @see oac_decoderctls
 */
OAC_EXPORT int oac_decoder_ctl(OacDecoder *st, int request, ...) OAC_ARG_NONNULL(1);

/** Frees an <code>OacDecoder</code> allocated by oac_decoder_create().
 * @param[in] st <tt>OacDecoder*</tt>: State to be freed.
 */
OAC_EXPORT void oac_decoder_destroy(OacDecoder *st);

/** Gets the size of an <code>OacDREDDecoder</code> structure.
 * @returns The size in bytes.
 */
OAC_EXPORT int oac_dred_decoder_get_size(void);

/** Allocates and initializes an OacDREDDecoder state.
 * @param [out] error <tt>int*</tt>: #OAC_OK Success or @ref oac_errorcodes
 */
OAC_EXPORT OacDREDDecoder *oac_dred_decoder_create(int *error);

/** Initializes an <code>OacDREDDecoder</code> state.
 * @param[in] dec <tt>OacDREDDecoder*</tt>: State to be initialized.
 */
OAC_EXPORT int oac_dred_decoder_init(OacDREDDecoder *dec);

/** Frees an <code>OacDREDDecoder</code> allocated by oac_dred_decoder_create().
 * @param[in] dec <tt>OacDREDDecoder*</tt>: State to be freed.
 */
OAC_EXPORT void oac_dred_decoder_destroy(OacDREDDecoder *dec);

/** Perform a CTL function on an Oac DRED decoder.
 *
 * Generally the request and subsequent arguments are generated
 * by a convenience macro.
 * @param dred_dec <tt>OacDREDDecoder*</tt>: DRED Decoder state.
 * @param request This and all remaining parameters should be replaced by one
 *                of the convenience macros in @ref oac_genericctls or
 *                @ref oac_decoderctls.
 * @see oac_genericctls
 * @see oac_decoderctls
 */
OAC_EXPORT int oac_dred_decoder_ctl(OacDREDDecoder *dred_dec, int request, ...);

/** Gets the size of an <code>OacDRED</code> structure.
 * @returns The size in bytes.
 */
OAC_EXPORT int oac_dred_get_size(void);

/** Allocates and initializes a DRED state.
 * @param [out] error <tt>int*</tt>: #OAC_OK Success or @ref oac_errorcodes
 */
OAC_EXPORT OacDRED *oac_dred_alloc(int *error);

/** Frees an <code>OacDRED</code> allocated by oac_dred_create().
 * @param[in] dec <tt>OacDRED*</tt>: State to be freed.
 */
OAC_EXPORT void oac_dred_free(OacDRED *dec);

/** Decode an Oac DRED packet.
 * @param [in] dred_dec <tt>OacDRED*</tt>: DRED Decoder state
 * @param [in] dred <tt>OacDRED*</tt>: DRED state
 * @param [in] data <tt>char*</tt>: Input payload
 * @param [in] len <tt>oac_int32</tt>: Number of bytes in payload
 * @param [in] max_dred_samples <tt>oac_int32</tt>: Maximum number of DRED samples that may be needed (if available in the packet).
 * @param [in] sampling_rate <tt>oac_int32</tt>: Sampling rate used for max_dred_samples argument. Needs not match the actual sampling rate of the decoder.
 * @param [out] dred_end <tt>oac_int32*</tt>: Number of non-encoded (silence) samples between the DRED timestamp and the last DRED sample.
 * @param [in] defer_processing <tt>int</tt>: Flag (0 or 1). If set to one, the CPU-intensive part of the DRED decoding is deferred until oac_dred_process() is called.
 * @returns Offset (positive) of the first decoded DRED samples, zero if no DRED is present, or @ref oac_errorcodes
 */
OAC_EXPORT int oac_dred_parse(OacDREDDecoder *dred_dec, OacDRED *dred, const unsigned char *data, oac_int32 len,
    oac_int32 max_dred_samples, oac_int32 sampling_rate, int *dred_end, int defer_processing) OAC_ARG_NONNULL(1);

/** Finish decoding an Oac DRED packet. The function only needs to be called if oac_dred_parse() was called with defer_processing=1.
 * The source and destination will often be the same DRED state.
 * @param [in] dred_dec <tt>OacDRED*</tt>: DRED Decoder state
 * @param [in] src <tt>OacDRED*</tt>: Source DRED state to start the processing from.
 * @param [out] dst <tt>OacDRED*</tt>: Destination DRED state to store the updated state after processing.
 * @returns @ref oac_errorcodes
 */
OAC_EXPORT int oac_dred_process(OacDREDDecoder *dred_dec, const OacDRED *src, OacDRED *dst);

/** Decode audio from an Oac DRED packet with 16-bit output.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] dred <tt>OacDRED*</tt>: DRED state
 * @param [in] dred_offset <tt>oac_int32</tt>: position of the redundancy to decode (in samples before the beginning of the real audio data in the packet).
 * @param [out] pcm <tt>oac_int16*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(oac_int16)
 * @param [in] frame_size Number of samples per channel to decode in \a pcm.
 *  frame_size <b>must</b> be a multiple of 2.5 ms.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_EXPORT int oac_decoder_dred_decode(OacDecoder *st, const OacDRED *dred, oac_int32 dred_offset, oac_int16 *pcm,
    oac_int32 frame_size);

/** Decode audio from an Oac DRED packet with 24-bit output.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] dred <tt>OacDRED*</tt>: DRED state
 * @param [in] dred_offset <tt>oac_int32</tt>: position of the redundancy to decode (in samples before the beginning of the real audio data in the packet).
 * @param [out] pcm <tt>oac_int32*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(oac_int16)
 * @param [in] frame_size Number of samples per channel to decode in \a pcm.
 *  frame_size <b>must</b> be a multiple of 2.5 ms.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_EXPORT int oac_decoder_dred_decode24(OacDecoder *st, const OacDRED *dred, oac_int32 dred_offset, oac_int32 *pcm,
    oac_int32 frame_size);

/** Decode audio from an Oac DRED packet with floating point output.
 * @param [in] st <tt>OacDecoder*</tt>: Decoder state
 * @param [in] dred <tt>OacDRED*</tt>: DRED state
 * @param [in] dred_offset <tt>oac_int32</tt>: position of the redundancy to decode (in samples before the beginning of the real audio data in the packet).
 * @param [out] pcm <tt>float*</tt>: Output signal (interleaved if 2 channels). length
 *  is frame_size*channels*sizeof(float)
 * @param [in] frame_size Number of samples per channel to decode in \a pcm.
 *  frame_size <b>must</b> be a multiple of 2.5 ms.
 * @returns Number of decoded samples or @ref oac_errorcodes
 */
OAC_EXPORT int oac_decoder_dred_decode_float(OacDecoder *st, const OacDRED *dred, oac_int32 dred_offset, float *pcm,
    oac_int32 frame_size);


/** Parse an oac packet into one or more frames.
 * Oac_decode will perform this operation internally so most applications do
 * not need to use this function.
 * This function does not copy the frames, the returned pointers are pointers into
 * the input packet.
 * @param [in] data <tt>char*</tt>: Oac packet to be parsed
 * @param [in] len <tt>oac_int32</tt>: size of data
 * @param [out] out_toc <tt>char*</tt>: TOC pointer
 * @param [out] frames <tt>char*[48]</tt> encapsulated frames
 * @param [out] size <tt>oac_int16[48]</tt> sizes of the encapsulated frames
 * @param [out] payload_offset <tt>int*</tt>: returns the position of the payload within the packet (in bytes)
 * @returns number of frames
 */
OAC_EXPORT int oac_packet_parse(
    const unsigned char *data,
    oac_int32 len,
    unsigned char *out_toc,
    const unsigned char *frames[48],
    oac_int16 size[48],
    int *payload_offset) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(5);

/** Gets the bandwidth of an Oac packet.
 * @param [in] data <tt>char*</tt>: Oac packet
 * @retval OAC_BANDWIDTH_NARROWBAND Narrowband (4kHz bandpass)
 * @retval OAC_BANDWIDTH_MEDIUMBAND Mediumband (6kHz bandpass)
 * @retval OAC_BANDWIDTH_WIDEBAND Wideband (8kHz bandpass)
 * @retval OAC_BANDWIDTH_SUPERWIDEBAND Superwideband (12kHz bandpass)
 * @retval OAC_BANDWIDTH_FULLBAND Fullband (20kHz bandpass)
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_get_bandwidth(const unsigned char *data) OAC_ARG_NONNULL(1);

/** Gets the number of samples per frame from an Oac packet.
 * @param [in] data <tt>char*</tt>: Oac packet.
 *                                  This must contain at least one byte of
 *                                  data.
 * @param [in] Fs <tt>oac_int32</tt>: Sampling rate in Hz.
 *                                     This must be a multiple of 400, or
 *                                     inaccurate results will be returned.
 * @returns Number of samples per frame.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_get_samples_per_frame(const unsigned char *data,
    oac_int32 Fs) OAC_ARG_NONNULL(1);

/** Gets the number of channels from an Oac packet.
 * @param [in] data <tt>char*</tt>: Oac packet
 * @returns Number of channels
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_get_nb_channels(const unsigned char *data) OAC_ARG_NONNULL(1);

/** Gets the number of frames in an Oac packet.
 * @param [in] packet <tt>char*</tt>: Oac packet
 * @param [in] len <tt>oac_int32</tt>: Length of packet
 * @returns Number of frames
 * @retval OAC_BAD_ARG Insufficient data was passed to the function
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_get_nb_frames(const unsigned char packet[],
    oac_int32 len) OAC_ARG_NONNULL(1);

/** Gets the number of samples of an Oac packet.
 * @param [in] packet <tt>char*</tt>: Oac packet
 * @param [in] len <tt>oac_int32</tt>: Length of packet
 * @param [in] Fs <tt>oac_int32</tt>: Sampling rate in Hz.
 *                                     This must be a multiple of 400, or
 *                                     inaccurate results will be returned.
 * @returns Number of samples
 * @retval OAC_BAD_ARG Insufficient data was passed to the function
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_get_nb_samples(const unsigned char packet[], oac_int32 len,
    oac_int32 Fs) OAC_ARG_NONNULL(1);

/** Checks whether an Oac packet has LBRR.
 * @param [in] packet <tt>char*</tt>: Oac packet
 * @param [in] len <tt>oac_int32</tt>: Length of packet
 * @returns 1 is LBRR is present, 0 otherwise
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_packet_has_lbrr(const unsigned char packet[], oac_int32 len);

/** Gets the number of samples of an Oac packet.
 * @param [in] dec <tt>OacDecoder*</tt>: Decoder state
 * @param [in] packet <tt>char*</tt>: Oac packet
 * @param [in] len <tt>oac_int32</tt>: Length of packet
 * @returns Number of samples
 * @retval OAC_BAD_ARG Insufficient data was passed to the function
 * @retval OAC_INVALID_PACKET The compressed data passed is corrupted or of an unsupported type
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_decoder_get_nb_samples(const OacDecoder *dec, const unsigned char packet[],
    oac_int32 len) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2);

/** Applies soft-clipping to bring a float signal within the [-1,1] range. If
 * the signal is already in that range, nothing is done. If there are values
 * outside of [-1,1], then the signal is clipped as smoothly as possible to
 * both fit in the range and avoid creating excessive distortion in the
 * process.
 * @param [in,out] pcm <tt>float*</tt>: Input PCM and modified PCM
 * @param [in] frame_size <tt>int</tt> Number of samples per channel to process
 * @param [in] channels <tt>int</tt>: Number of channels
 * @param [in,out] softclip_mem <tt>float*</tt>: State memory for the soft clipping process (one float per channel, initialized to zero)
 */
OAC_EXPORT void oac_pcm_soft_clip(float *pcm, int frame_size, int channels, float *softclip_mem);


/**@}*/

/** @defgroup oac_repacketizer Repacketizer
 * @{
 *
 * The repacketizer can be used to merge multiple Oac packets into a single
 * packet or alternatively to split Oac packets that have previously been
 * merged. Splitting valid Oac packets is always guaranteed to succeed,
 * whereas merging valid packets only succeeds if all frames have the same
 * mode, bandwidth, and frame size, and when the total duration of the merged
 * packet is no more than 120 ms. The 120 ms limit comes from the
 * specification and limits decoder memory requirements at a point where
 * framing overhead becomes negligible.
 *
 * The repacketizer currently only operates on elementary Oac
 * streams. It will not manipulate multistream packets successfully, except in
 * the degenerate case where they consist of data from a single stream.
 *
 * The repacketizing process starts with creating a repacketizer state, either
 * by calling oac_repacketizer_create() or by allocating the memory yourself,
 * e.g.,
 * @code
 * OacRepacketizer *rp;
 * rp = (OacRepacketizer*)malloc(oac_repacketizer_get_size());
 * if (rp != NULL)
 *     oac_repacketizer_init(rp);
 * @endcode
 *
 * Then the application should submit packets with oac_repacketizer_cat(),
 * extract new packets with oac_repacketizer_out() or
 * oac_repacketizer_out_range(), and then reset the state for the next set of
 * input packets via oac_repacketizer_init().
 *
 * For example, to split a sequence of packets into individual frames:
 * @code
 * unsigned char *data;
 * int len;
 * while (get_next_packet(&data, &len))
 * {
 *   unsigned char out[1276];
 *   oac_int32 out_len;
 *   int nb_frames;
 *   int err;
 *   int i;
 *   err = oac_repacketizer_cat(rp, data, len);
 *   if (err != OAC_OK)
 *   {
 *     release_packet(data);
 *     return err;
 *   }
 *   nb_frames = oac_repacketizer_get_nb_frames(rp);
 *   for (i = 0; i < nb_frames; i++)
 *   {
 *     out_len = oac_repacketizer_out_range(rp, i, i+1, out, sizeof(out));
 *     if (out_len < 0)
 *     {
 *        release_packet(data);
 *        return (int)out_len;
 *     }
 *     output_next_packet(out, out_len);
 *   }
 *   oac_repacketizer_init(rp);
 *   release_packet(data);
 * }
 * @endcode
 *
 * Alternatively, to combine a sequence of frames into packets that each
 * contain up to <code>TARGET_DURATION_MS</code> milliseconds of data:
 * @code
 * // The maximum number of packets with duration TARGET_DURATION_MS occurs
 * // when the frame size is 2.5 ms, for a total of (TARGET_DURATION_MS*2/5)
 * // packets.
 * unsigned char *data[(TARGET_DURATION_MS*2/5)+1];
 * oac_int32 len[(TARGET_DURATION_MS*2/5)+1];
 * int nb_packets;
 * unsigned char out[1277*(TARGET_DURATION_MS*2/2)];
 * oac_int32 out_len;
 * int prev_toc;
 * nb_packets = 0;
 * while (get_next_packet(data+nb_packets, len+nb_packets))
 * {
 *   int nb_frames;
 *   int err;
 *   nb_frames = oac_packet_get_nb_frames(data[nb_packets], len[nb_packets]);
 *   if (nb_frames < 1)
 *   {
 *     release_packets(data, nb_packets+1);
 *     return nb_frames;
 *   }
 *   nb_frames += oac_repacketizer_get_nb_frames(rp);
 *   // If adding the next packet would exceed our target, or it has an
 *   // incompatible TOC sequence, output the packets we already have before
 *   // submitting it.
 *   // N.B., The nb_packets > 0 check ensures we've submitted at least one
 *   // packet since the last call to oac_repacketizer_init(). Otherwise a
 *   // single packet longer than TARGET_DURATION_MS would cause us to try to
 *   // output an (invalid) empty packet. It also ensures that prev_toc has
 *   // been set to a valid value. Additionally, len[nb_packets] > 0 is
 *   // guaranteed by the call to oac_packet_get_nb_frames() above, so the
 *   // reference to data[nb_packets][0] should be valid.
 *   if (nb_packets > 0 && (
 *       ((prev_toc & 0xFC) != (data[nb_packets][0] & 0xFC)) ||
 *       oac_packet_get_samples_per_frame(data[nb_packets], 48000)*nb_frames >
 *       TARGET_DURATION_MS*48))
 *   {
 *     out_len = oac_repacketizer_out(rp, out, sizeof(out));
 *     if (out_len < 0)
 *     {
 *        release_packets(data, nb_packets+1);
 *        return (int)out_len;
 *     }
 *     output_next_packet(out, out_len);
 *     oac_repacketizer_init(rp);
 *     release_packets(data, nb_packets);
 *     data[0] = data[nb_packets];
 *     len[0] = len[nb_packets];
 *     nb_packets = 0;
 *   }
 *   err = oac_repacketizer_cat(rp, data[nb_packets], len[nb_packets]);
 *   if (err != OAC_OK)
 *   {
 *     release_packets(data, nb_packets+1);
 *     return err;
 *   }
 *   prev_toc = data[nb_packets][0];
 *   nb_packets++;
 * }
 * // Output the final, partial packet.
 * if (nb_packets > 0)
 * {
 *   out_len = oac_repacketizer_out(rp, out, sizeof(out));
 *   release_packets(data, nb_packets);
 *   if (out_len < 0)
 *     return (int)out_len;
 *   output_next_packet(out, out_len);
 * }
 * @endcode
 *
 * An alternate way of merging packets is to simply call oac_repacketizer_cat()
 * unconditionally until it fails. At that point, the merged packet can be
 * obtained with oac_repacketizer_out() and the input packet for which
 * oac_repacketizer_cat() needs to be re-added to a newly reinitialized
 * repacketizer state.
 */

typedef struct OacRepacketizer OacRepacketizer;

/** Gets the size of an <code>OacRepacketizer</code> structure.
 * @returns The size in bytes.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_repacketizer_get_size(void);

/** (Re)initializes a previously allocated repacketizer state.
 * The state must be at least the size returned by oac_repacketizer_get_size().
 * This can be used for applications which use their own allocator instead of
 * malloc().
 * It must also be called to reset the queue of packets waiting to be
 * repacketized, which is necessary if the maximum packet duration of 120 ms
 * is reached or if you wish to submit packets with a different Oac
 * configuration (coding mode, audio bandwidth, frame size, or channel count).
 * Failure to do so will prevent a new packet from being added with
 * oac_repacketizer_cat().
 * @see oac_repacketizer_create
 * @see oac_repacketizer_get_size
 * @see oac_repacketizer_cat
 * @param rp <tt>OacRepacketizer*</tt>: The repacketizer state to
 *                                       (re)initialize.
 * @returns A pointer to the same repacketizer state that was passed in.
 */
OAC_EXPORT OacRepacketizer *oac_repacketizer_init(OacRepacketizer *rp) OAC_ARG_NONNULL(1);

/** Allocates memory and initializes the new repacketizer with
 * oac_repacketizer_init().
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT OacRepacketizer *oac_repacketizer_create(void);

/** Frees an <code>OacRepacketizer</code> allocated by
 * oac_repacketizer_create().
 * @param[in] rp <tt>OacRepacketizer*</tt>: State to be freed.
 */
OAC_EXPORT void oac_repacketizer_destroy(OacRepacketizer *rp);

/** Add a packet to the current repacketizer state.
 * This packet must match the configuration of any packets already submitted
 * for repacketization since the last call to oac_repacketizer_init().
 * This means that it must have the same coding mode, audio bandwidth, frame
 * size, and channel count.
 * This can be checked in advance by examining the top 6 bits of the first
 * byte of the packet, and ensuring they match the top 6 bits of the first
 * byte of any previously submitted packet.
 * The total duration of audio in the repacketizer state also must not exceed
 * 120 ms, the maximum duration of a single packet, after adding this packet.
 *
 * The contents of the current repacketizer state can be extracted into new
 * packets using oac_repacketizer_out() or oac_repacketizer_out_range().
 *
 * In order to add a packet with a different configuration or to add more
 * audio beyond 120 ms, you must clear the repacketizer state by calling
 * oac_repacketizer_init().
 * If a packet is too large to add to the current repacketizer state, no part
 * of it is added, even if it contains multiple frames, some of which might
 * fit.
 * If you wish to be able to add parts of such packets, you should first use
 * another repacketizer to split the packet into pieces and add them
 * individually.
 * @see oac_repacketizer_out_range
 * @see oac_repacketizer_out
 * @see oac_repacketizer_init
 * @param rp <tt>OacRepacketizer*</tt>: The repacketizer state to which to
 *                                       add the packet.
 * @param[in] data <tt>const unsigned char*</tt>: The packet data.
 *                                                The application must ensure
 *                                                this pointer remains valid
 *                                                until the next call to
 *                                                oac_repacketizer_init() or
 *                                                oac_repacketizer_destroy().
 * @param len <tt>oac_int32</tt>: The number of bytes in the packet data.
 * @returns An error code indicating whether or not the operation succeeded.
 * @retval #OAC_OK The packet's contents have been added to the repacketizer
 *                  state.
 * @retval #OAC_INVALID_PACKET The packet did not have a valid TOC sequence,
 *                              the packet's TOC sequence was not compatible
 *                              with previously submitted packets (because
 *                              the coding mode, audio bandwidth, frame size,
 *                              or channel count did not match), or adding
 *                              this packet would increase the total amount of
 *                              audio stored in the repacketizer state to more
 *                              than 120 ms.
 */
OAC_EXPORT int oac_repacketizer_cat(OacRepacketizer *rp, const unsigned char *data,
    oac_int32 len) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(2);


/** Construct a new packet from data previously submitted to the repacketizer
 * state via oac_repacketizer_cat().
 * @param rp <tt>OacRepacketizer*</tt>: The repacketizer state from which to
 *                                       construct the new packet.
 * @param begin <tt>int</tt>: The index of the first frame in the current
 *                            repacketizer state to include in the output.
 * @param end <tt>int</tt>: One past the index of the last frame in the
 *                          current repacketizer state to include in the
 *                          output.
 * @param[out] data <tt>const unsigned char*</tt>: The buffer in which to
 *                                                 store the output packet.
 * @param maxlen <tt>oac_int32</tt>: The maximum number of bytes to store in
 *                                    the output buffer. In order to guarantee
 *                                    success, this should be at least
 *                                    <code>1276</code> for a single frame,
 *                                    or for multiple frames,
 *                                    <code>1277*(end-begin)</code>.
 *                                    However, <code>1*(end-begin)</code> plus
 *                                    the size of all packet data submitted to
 *                                    the repacketizer since the last call to
 *                                    oac_repacketizer_init() or
 *                                    oac_repacketizer_create() is also
 *                                    sufficient, and possibly much smaller.
 * @returns The total size of the output packet on success, or an error code
 *          on failure.
 * @retval #OAC_BAD_ARG <code>[begin,end)</code> was an invalid range of
 *                       frames (begin < 0, begin >= end, or end >
 *                       oac_repacketizer_get_nb_frames()).
 * @retval #OAC_BUFFER_TOO_SMALL \a maxlen was insufficient to contain the
 *                                complete output packet.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_repacketizer_out_range(OacRepacketizer *rp, int begin, int end,
    unsigned char *data, oac_int32 maxlen) OAC_ARG_NONNULL(1) OAC_ARG_NONNULL(4);

/** Return the total number of frames contained in packet data submitted to
 * the repacketizer state so far via oac_repacketizer_cat() since the last
 * call to oac_repacketizer_init() or oac_repacketizer_create().
 * This defines the valid range of packets that can be extracted with
 * oac_repacketizer_out_range() or oac_repacketizer_out().
 * @param rp <tt>OacRepacketizer*</tt>: The repacketizer state containing the
 *                                       frames.
 * @returns The total number of frames contained in the packet data submitted
 *          to the repacketizer state.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT int oac_repacketizer_get_nb_frames(OacRepacketizer *rp) OAC_ARG_NONNULL(1);

/** Construct a new packet from data previously submitted to the repacketizer
 * state via oac_repacketizer_cat().
 * This is a convenience routine that returns all the data submitted so far
 * in a single packet.
 * It is equivalent to calling
 * @code
 * oac_repacketizer_out_range(rp, 0, oac_repacketizer_get_nb_frames(rp),
 *                             data, maxlen)
 * @endcode
 * @param rp <tt>OacRepacketizer*</tt>: The repacketizer state from which to
 *                                       construct the new packet.
 * @param[out] data <tt>const unsigned char*</tt>: The buffer in which to
 *                                                 store the output packet.
 * @param maxlen <tt>oac_int32</tt>: The maximum number of bytes to store in
 *                                    the output buffer. In order to guarantee
 *                                    success, this should be at least
 *                                    <code>1277*oac_repacketizer_get_nb_frames(rp)</code>.
 *                                    However,
 *                                    <code>1*oac_repacketizer_get_nb_frames(rp)</code>
 *                                    plus the size of all packet data
 *                                    submitted to the repacketizer since the
 *                                    last call to oac_repacketizer_init() or
 *                                    oac_repacketizer_create() is also
 *                                    sufficient, and possibly much smaller.
 * @returns The total size of the output packet on success, or an error code
 *          on failure.
 * @retval #OAC_BUFFER_TOO_SMALL \a maxlen was insufficient to contain the
 *                                complete output packet.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_repacketizer_out(OacRepacketizer *rp, unsigned char *data,
    oac_int32 maxlen) OAC_ARG_NONNULL(1);

/** Pads a given Oac packet to a larger size (possibly changing the TOC sequence).
 * @param[in,out] data <tt>const unsigned char*</tt>: The buffer containing the
 *                                                   packet to pad.
 * @param len <tt>oac_int32</tt>: The size of the packet.
 *                                 This must be at least 1.
 * @param new_len <tt>oac_int32</tt>: The desired size of the packet after padding.
 *                                 This must be at least as large as len.
 * @returns an error code
 * @retval #OAC_OK \a on success.
 * @retval #OAC_BAD_ARG \a len was less than 1 or new_len was less than len.
 * @retval #OAC_INVALID_PACKET \a data did not contain a valid Oac packet.
 */
OAC_EXPORT int oac_packet_pad(unsigned char *data, oac_int32 len, oac_int32 new_len);

/** Remove all padding from a given Oac packet and rewrite the TOC sequence to
 * minimize space usage.
 * @param[in,out] data <tt>const unsigned char*</tt>: The buffer containing the
 *                                                   packet to strip.
 * @param len <tt>oac_int32</tt>: The size of the packet.
 *                                 This must be at least 1.
 * @returns The new size of the output packet on success, or an error code
 *          on failure.
 * @retval #OAC_BAD_ARG \a len was less than 1.
 * @retval #OAC_INVALID_PACKET \a data did not contain a valid Oac packet.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_packet_unpad(unsigned char *data, oac_int32 len);

/** Pads a given Oac multi-stream packet to a larger size (possibly changing the TOC sequence).
 * @param[in,out] data <tt>const unsigned char*</tt>: The buffer containing the
 *                                                   packet to pad.
 * @param len <tt>oac_int32</tt>: The size of the packet.
 *                                 This must be at least 1.
 * @param new_len <tt>oac_int32</tt>: The desired size of the packet after padding.
 *                                 This must be at least 1.
 * @param nb_streams <tt>oac_int32</tt>: The number of streams (not channels) in the packet.
 *                                 This must be at least as large as len.
 * @returns an error code
 * @retval #OAC_OK \a on success.
 * @retval #OAC_BAD_ARG \a len was less than 1.
 * @retval #OAC_INVALID_PACKET \a data did not contain a valid Oac packet.
 */
OAC_EXPORT int oac_multistream_packet_pad(unsigned char *data, oac_int32 len, oac_int32 new_len, int nb_streams);

/** Remove all padding from a given Oac multi-stream packet and rewrite the TOC sequence to
 * minimize space usage.
 * @param[in,out] data <tt>const unsigned char*</tt>: The buffer containing the
 *                                                   packet to strip.
 * @param len <tt>oac_int32</tt>: The size of the packet.
 *                                 This must be at least 1.
 * @param nb_streams <tt>oac_int32</tt>: The number of streams (not channels) in the packet.
 *                                 This must be at least 1.
 * @returns The new size of the output packet on success, or an error code
 *          on failure.
 * @retval #OAC_BAD_ARG \a len was less than 1 or new_len was less than len.
 * @retval #OAC_INVALID_PACKET \a data did not contain a valid Oac packet.
 */
OAC_EXPORT OAC_WARN_UNUSED_RESULT oac_int32 oac_multistream_packet_unpad(unsigned char *data, oac_int32 len,
    int nb_streams);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* OAC_H */

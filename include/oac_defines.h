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
 * @file oac_defines.h
 * @brief Oac reference implementation constants
 */

#ifndef OAC_DEFINES_H
#define OAC_DEFINES_H

#include "oac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup oac_errorcodes Error codes
 * @{
 */
/** No error @hideinitializer*/
#define OAC_OK                0
/** One or more invalid/out of range arguments @hideinitializer*/
#define OAC_BAD_ARG          -1
/** Not enough bytes allocated in the buffer @hideinitializer*/
#define OAC_BUFFER_TOO_SMALL -2
/** An internal error was detected @hideinitializer*/
#define OAC_INTERNAL_ERROR   -3
/** The compressed data passed is corrupted @hideinitializer*/
#define OAC_INVALID_PACKET   -4
/** Invalid/unsupported request number @hideinitializer*/
#define OAC_UNIMPLEMENTED    -5
/** An encoder or decoder structure is invalid or already freed @hideinitializer*/
#define OAC_INVALID_STATE    -6
/** Memory allocation has failed @hideinitializer*/
#define OAC_ALLOC_FAIL       -7
/**@}*/

/** @cond OAC_INTERNAL_DOC */
/**Export control for oac functions */

#ifndef OAC_EXPORT
# if defined(_WIN32)
#  if defined(OAC_BUILD) && defined(DLL_EXPORT)
#   define OAC_EXPORT __declspec(dllexport)
#  else
#   define OAC_EXPORT
#  endif
# elif defined(__GNUC__) && defined(OAC_BUILD)
#  define OAC_EXPORT __attribute__ ((visibility ("default")))
# else
#  define OAC_EXPORT
# endif
#endif

#if !defined(OAC_GNUC_PREREQ)
# if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define OAC_GNUC_PREREQ(_maj, _min) \
        ((__GNUC__<<16) + __GNUC_MINOR__ >= ((_maj)<<16) + (_min))
# else
#  define OAC_GNUC_PREREQ(_maj, _min) 0
# endif
#endif

#if (!defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L))
# if OAC_GNUC_PREREQ(3, 0)
#  define OAC_RESTRICT __restrict__
# elif (defined(_MSC_VER) && _MSC_VER >= 1400)
#  define OAC_RESTRICT __restrict
# else
#  define OAC_RESTRICT
# endif
#else
# define OAC_RESTRICT restrict
#endif

#if (!defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L))
# if OAC_GNUC_PREREQ(2, 7)
#  define OAC_INLINE __inline__
# elif (defined(_MSC_VER))
#  define OAC_INLINE __inline
# else
#  define OAC_INLINE
# endif
#else
# define OAC_INLINE inline
#endif

/**Warning attributes for oac functions
 * NONNULL is not used in OAC_BUILD to avoid the compiler optimizing out
 * some paranoid null checks. */
#if defined(__GNUC__) && OAC_GNUC_PREREQ(3, 4)
# define OAC_WARN_UNUSED_RESULT __attribute__ ((__warn_unused_result__))
#else
# define OAC_WARN_UNUSED_RESULT
#endif
#if !defined(OAC_BUILD) && defined(__GNUC__) && OAC_GNUC_PREREQ(3, 4)
# define OAC_ARG_NONNULL(_x)  __attribute__ ((__nonnull__(_x)))
#else
# define OAC_ARG_NONNULL(_x)
#endif

/** These are the actual Encoder CTL ID numbers.
 * They should not be used directly by applications.
 * In general, SETs should be even and GETs should be odd.*/
#define OAC_SET_APPLICATION_REQUEST         4000
#define OAC_GET_APPLICATION_REQUEST         4001
#define OAC_SET_BITRATE_REQUEST             4002
#define OAC_GET_BITRATE_REQUEST             4003
#define OAC_SET_MAX_BANDWIDTH_REQUEST       4004
#define OAC_GET_MAX_BANDWIDTH_REQUEST       4005
#define OAC_SET_VBR_REQUEST                 4006
#define OAC_GET_VBR_REQUEST                 4007
#define OAC_SET_BANDWIDTH_REQUEST           4008
#define OAC_GET_BANDWIDTH_REQUEST           4009
#define OAC_SET_COMPLEXITY_REQUEST          4010
#define OAC_GET_COMPLEXITY_REQUEST          4011
#define OAC_SET_INBAND_FEC_REQUEST          4012
#define OAC_GET_INBAND_FEC_REQUEST          4013
#define OAC_SET_PACKET_LOSS_PERC_REQUEST    4014
#define OAC_GET_PACKET_LOSS_PERC_REQUEST    4015
#define OAC_SET_DTX_REQUEST                 4016
#define OAC_GET_DTX_REQUEST                 4017
#define OAC_SET_VBR_CONSTRAINT_REQUEST      4020
#define OAC_GET_VBR_CONSTRAINT_REQUEST      4021
#define OAC_SET_FORCE_CHANNELS_REQUEST      4022
#define OAC_GET_FORCE_CHANNELS_REQUEST      4023
#define OAC_SET_SIGNAL_REQUEST              4024
#define OAC_GET_SIGNAL_REQUEST              4025
#define OAC_GET_LOOKAHEAD_REQUEST           4027
/* #define OAC_RESET_STATE 4028 */
#define OAC_GET_SAMPLE_RATE_REQUEST         4029
#define OAC_GET_FINAL_RANGE_REQUEST         4031
#define OAC_GET_PITCH_REQUEST               4033
#define OAC_SET_GAIN_REQUEST                4034
#define OAC_GET_GAIN_REQUEST                4045 /* Should have been 4035 */
#define OAC_SET_LSB_DEPTH_REQUEST           4036
#define OAC_GET_LSB_DEPTH_REQUEST           4037
#define OAC_GET_LAST_PACKET_DURATION_REQUEST 4039
#define OAC_SET_EXPERT_FRAME_DURATION_REQUEST 4040
#define OAC_GET_EXPERT_FRAME_DURATION_REQUEST 4041
#define OAC_SET_PREDICTION_DISABLED_REQUEST 4042
#define OAC_GET_PREDICTION_DISABLED_REQUEST 4043
/* Don't use 4045, it's already taken by OAC_GET_GAIN_REQUEST */
#define OAC_SET_PHASE_INVERSION_DISABLED_REQUEST 4046
#define OAC_GET_PHASE_INVERSION_DISABLED_REQUEST 4047
#define OAC_GET_IN_DTX_REQUEST              4049
#define OAC_SET_DRED_DURATION_REQUEST 4050
#define OAC_GET_DRED_DURATION_REQUEST 4051
#define OAC_SET_DNN_BLOB_REQUEST 4052
/*#define OAC_GET_DNN_BLOB_REQUEST 4053 */
#define OAC_SET_OSCE_BWE_REQUEST 4054
#define OAC_GET_OSCE_BWE_REQUEST 4055


#define OAC_SET_IGNORE_EXTENSIONS_REQUEST 4058
#define OAC_GET_IGNORE_EXTENSIONS_REQUEST 4059
#define OAC_GET_FORMAT_REQUEST 4060

/** Defines for the presence of extended APIs. */
#define OAC_HAVE_OAC_PROJECTION_H

/* Macros to trigger compilation errors when the wrong types are provided to a CTL */
#define oac_check_int(x) (((void)((x) == (oac_int32)0)), (oac_int32)(x))

#ifdef DISABLE_PTR_CHECK
/* Disable checks to prevent ubsan from complaining about NULL checks
   in test_oac_api. */
# define oac_check_int_ptr(ptr) (ptr)
# define oac_check_uint_ptr(ptr) (ptr)
# define oac_check_uint8_ptr(ptr) (ptr)
# define oac_check_val16_ptr(ptr) (ptr)
# define oac_check_void_ptr(ptr) (ptr)
#else
# define oac_check_int_ptr(ptr) ((ptr) + ((ptr) - (oac_int32*)(ptr)))
# define oac_check_uint_ptr(ptr) ((ptr) + ((ptr) - (oac_uint32*)(ptr)))
# define oac_check_uint8_ptr(ptr) ((ptr) + ((ptr) - (oac_uint8*)(ptr)))
# define oac_check_val16_ptr(ptr) ((ptr) + ((ptr) - (oac_val16*)(ptr)))
# define oac_check_void_ptr(x) ((void)((void *)0 == (x)), (x))
#endif
/** @endcond */

/** @defgroup oac_ctlvalues Pre-defined values for CTL interface
 * @see oac_genericctls, oac_encoderctls
 * @{
 */
/* Values for the various encoder CTLs */
#define OAC_AUTO                           -1000 /**<Auto/default setting @hideinitializer*/
#define OAC_BITRATE_MAX                       -1 /**<Maximum bitrate @hideinitializer*/

/** Best for most VoIP/videoconference applications where listening quality and intelligibility matter most
 * @hideinitializer */
#define OAC_APPLICATION_VOIP                2048
/** Best for broadcast/high-fidelity application where the decoded audio should be as close as possible to the input
 * @hideinitializer */
#define OAC_APPLICATION_AUDIO               2049
/** Only use when lowest-achievable latency is what matters most. Voice-optimized modes cannot be used.
 * @hideinitializer */
#define OAC_APPLICATION_RESTRICTED_LOWDELAY 2051
/** Experts only: forces SILK encoding; don't allocate CELT state at all. Disables OAC_SET_APPLICATION. */
#define OAC_APPLICATION_RESTRICTED_SILK     2052
/** Experts only: forces CELT encoding; don't allocate SILK state at all. Disables OAC_SET_APPLICATION. */
#define OAC_APPLICATION_RESTRICTED_CELT     2053

/** Standard format supporting mono and stereo (1-2 channels) @hideinitializer */
#define OAC_FORMAT_STANDARD    0
/** Ambisonics format supporting orders 0-5 (1, 4, 9, 16, 25, or 36 channels) @hideinitializer */
#define OAC_FORMAT_AMBISONICS  1
#define OAC_SIGNAL_VOICE                    3001 /**< Signal being encoded is voice */
#define OAC_SIGNAL_MUSIC                    3002 /**< Signal being encoded is music */
#define OAC_BANDWIDTH_NARROWBAND            1101 /**< 4 kHz bandpass @hideinitializer*/
#define OAC_BANDWIDTH_MEDIUMBAND            1102 /**< 6 kHz bandpass @hideinitializer*/
#define OAC_BANDWIDTH_WIDEBAND              1103 /**< 8 kHz bandpass @hideinitializer*/
#define OAC_BANDWIDTH_SUPERWIDEBAND         1104 /**<12 kHz bandpass @hideinitializer*/
#define OAC_BANDWIDTH_FULLBAND              1105 /**<20 kHz bandpass @hideinitializer*/

#define OAC_FRAMESIZE_ARG                   5000 /**< Select frame size from the argument (default) */
#define OAC_FRAMESIZE_2_5_MS                5001 /**< Use 2.5 ms frames */
#define OAC_FRAMESIZE_5_MS                  5002 /**< Use 5 ms frames */
#define OAC_FRAMESIZE_10_MS                 5003 /**< Use 10 ms frames */
#define OAC_FRAMESIZE_20_MS                 5004 /**< Use 20 ms frames */
#define OAC_FRAMESIZE_40_MS                 5005 /**< Use 40 ms frames */
#define OAC_FRAMESIZE_60_MS                 5006 /**< Use 60 ms frames */
#define OAC_FRAMESIZE_80_MS                 5007 /**< Use 80 ms frames */
#define OAC_FRAMESIZE_100_MS                5008 /**< Use 100 ms frames */
#define OAC_FRAMESIZE_120_MS                5009 /**< Use 120 ms frames */

/**@}*/


/** @defgroup oac_encoderctls Encoder related CTLs
 *
 * These are convenience macros for use with the \c oac_encode_ctl
 * interface. They are used to generate the appropriate series of
 * arguments for that call, passing the correct type, size and so
 * on as expected for each particular request.
 *
 * Some usage examples:
 *
 * @code
 * int ret;
 * ret = oac_encoder_ctl(enc_ctx, OAC_SET_BANDWIDTH(OAC_AUTO));
 * if (ret != OAC_OK) return ret;
 *
 * oac_int32 rate;
 * oac_encoder_ctl(enc_ctx, OAC_GET_BANDWIDTH(&rate));
 *
 * oac_encoder_ctl(enc_ctx, OAC_RESET_STATE);
 * @endcode
 *
 * @see oac_genericctls, oac_encoder
 * @{
 */

/** Configures the encoder's computational complexity.
 * The supported range is 0-10 inclusive with 10 representing the highest complexity.
 * @see OAC_GET_COMPLEXITY
 * @param[in] x <tt>oac_int32</tt>: Allowed values: 0-10, inclusive.
 *
 * @hideinitializer */
#define OAC_SET_COMPLEXITY(x) OAC_SET_COMPLEXITY_REQUEST, oac_check_int(x)
/** Gets the encoder's complexity configuration.
 * @see OAC_SET_COMPLEXITY
 * @param[out] x <tt>oac_int32 *</tt>: Returns a value in the range 0-10,
 *                                      inclusive.
 * @hideinitializer */
#define OAC_GET_COMPLEXITY(x) OAC_GET_COMPLEXITY_REQUEST, oac_check_int_ptr(x)

/** Configures the bitrate in the encoder.
 * Rates from 500 to 512000 bits per second are meaningful, as well as the
 * special values #OAC_AUTO and #OAC_BITRATE_MAX.
 * The value #OAC_BITRATE_MAX can be used to cause the codec to use as much
 * rate as it can, which is useful for controlling the rate by adjusting the
 * output buffer size.
 * @see OAC_GET_BITRATE
 * @param[in] x <tt>oac_int32</tt>: Bitrate in bits per second. The default
 *                                   is determined based on the number of
 *                                   channels and the input sampling rate.
 * @hideinitializer */
#define OAC_SET_BITRATE(x) OAC_SET_BITRATE_REQUEST, oac_check_int(x)
/** Gets the encoder's bitrate configuration.
 * @see OAC_SET_BITRATE
 * @param[out] x <tt>oac_int32 *</tt>: Returns the bitrate in bits per second.
 *                                      The default is determined based on the
 *                                      number of channels and the input
 *                                      sampling rate.
 * @hideinitializer */
#define OAC_GET_BITRATE(x) OAC_GET_BITRATE_REQUEST, oac_check_int_ptr(x)

/** Enables or disables variable bitrate (VBR) in the encoder.
 * The configured bitrate may not be met exactly because frames must
 * be an integer number of bytes in length.
 * @see OAC_GET_VBR
 * @see OAC_SET_VBR_CONSTRAINT
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Hard CBR. For LPC/hybrid modes at very low bit-rate, this can
 *               cause noticeable quality degradation.</dd>
 * <dt>1</dt><dd>VBR (default). The exact type of VBR is controlled by
 *               #OAC_SET_VBR_CONSTRAINT.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_VBR(x) OAC_SET_VBR_REQUEST, oac_check_int(x)
/** Determine if variable bitrate (VBR) is enabled in the encoder.
 * @see OAC_SET_VBR
 * @see OAC_GET_VBR_CONSTRAINT
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>Hard CBR.</dd>
 * <dt>1</dt><dd>VBR (default). The exact type of VBR may be retrieved via
 *               #OAC_GET_VBR_CONSTRAINT.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_VBR(x) OAC_GET_VBR_REQUEST, oac_check_int_ptr(x)

/** Enables or disables constrained VBR in the encoder.
 * This setting is ignored when the encoder is in CBR mode.
 * @warning Only the MDCT mode of Oac currently heeds the constraint.
 *  Speech mode ignores it completely, hybrid mode may fail to obey it
 *  if the LPC layer uses more bitrate than the constraint would have
 *  permitted.
 * @see OAC_GET_VBR_CONSTRAINT
 * @see OAC_SET_VBR
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Unconstrained VBR.</dd>
 * <dt>1</dt><dd>Constrained VBR (default). This creates a maximum of one
 *               frame of buffering delay assuming a transport with a
 *               serialization speed of the nominal bitrate.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_VBR_CONSTRAINT(x) OAC_SET_VBR_CONSTRAINT_REQUEST, oac_check_int(x)
/** Determine if constrained VBR is enabled in the encoder.
 * @see OAC_SET_VBR_CONSTRAINT
 * @see OAC_GET_VBR
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>Unconstrained VBR.</dd>
 * <dt>1</dt><dd>Constrained VBR (default).</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_VBR_CONSTRAINT(x) OAC_GET_VBR_CONSTRAINT_REQUEST, oac_check_int_ptr(x)

/** Configures mono/stereo forcing in the encoder.
 * This can force the encoder to produce packets encoded as either mono or
 * stereo, regardless of the format of the input audio. This is useful when
 * the caller knows that the input signal is currently a mono source embedded
 * in a stereo stream.
 * @see OAC_GET_FORCE_CHANNELS
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>#OAC_AUTO</dt><dd>Not forced (default)</dd>
 * <dt>1</dt>         <dd>Forced mono</dd>
 * <dt>2</dt>         <dd>Forced stereo</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_FORCE_CHANNELS(x) OAC_SET_FORCE_CHANNELS_REQUEST, oac_check_int(x)
/** Gets the encoder's forced channel configuration.
 * @see OAC_SET_FORCE_CHANNELS
 * @param[out] x <tt>oac_int32 *</tt>:
 * <dl>
 * <dt>#OAC_AUTO</dt><dd>Not forced (default)</dd>
 * <dt>1</dt>         <dd>Forced mono</dd>
 * <dt>2</dt>         <dd>Forced stereo</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_FORCE_CHANNELS(x) OAC_GET_FORCE_CHANNELS_REQUEST, oac_check_int_ptr(x)

/** Configures the maximum bandpass that the encoder will select automatically.
 * Applications should normally use this instead of #OAC_SET_BANDWIDTH
 * (leaving that set to the default, #OAC_AUTO). This allows the
 * application to set an upper bound based on the type of input it is
 * providing, but still gives the encoder the freedom to reduce the bandpass
 * when the bitrate becomes too low, for better overall quality.
 * @see OAC_GET_MAX_BANDWIDTH
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>OAC_BANDWIDTH_NARROWBAND</dt>    <dd>4 kHz passband</dd>
 * <dt>OAC_BANDWIDTH_MEDIUMBAND</dt>    <dd>6 kHz passband</dd>
 * <dt>OAC_BANDWIDTH_WIDEBAND</dt>      <dd>8 kHz passband</dd>
 * <dt>OAC_BANDWIDTH_SUPERWIDEBAND</dt><dd>12 kHz passband</dd>
 * <dt>OAC_BANDWIDTH_FULLBAND</dt>     <dd>20 kHz passband (default)</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_MAX_BANDWIDTH(x) OAC_SET_MAX_BANDWIDTH_REQUEST, oac_check_int(x)

/** Gets the encoder's configured maximum allowed bandpass.
 * @see OAC_SET_MAX_BANDWIDTH
 * @param[out] x <tt>oac_int32 *</tt>: Allowed values:
 * <dl>
 * <dt>#OAC_BANDWIDTH_NARROWBAND</dt>    <dd>4 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_MEDIUMBAND</dt>    <dd>6 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_WIDEBAND</dt>      <dd>8 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_SUPERWIDEBAND</dt><dd>12 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_FULLBAND</dt>     <dd>20 kHz passband (default)</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_MAX_BANDWIDTH(x) OAC_GET_MAX_BANDWIDTH_REQUEST, oac_check_int_ptr(x)

/** Sets the encoder's bandpass to a specific value.
 * This prevents the encoder from automatically selecting the bandpass based
 * on the available bitrate. If an application knows the bandpass of the input
 * audio it is providing, it should normally use #OAC_SET_MAX_BANDWIDTH
 * instead, which still gives the encoder the freedom to reduce the bandpass
 * when the bitrate becomes too low, for better overall quality.
 * @see OAC_GET_BANDWIDTH
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>#OAC_AUTO</dt>                    <dd>(default)</dd>
 * <dt>#OAC_BANDWIDTH_NARROWBAND</dt>    <dd>4 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_MEDIUMBAND</dt>    <dd>6 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_WIDEBAND</dt>      <dd>8 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_SUPERWIDEBAND</dt><dd>12 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_FULLBAND</dt>     <dd>20 kHz passband</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_BANDWIDTH(x) OAC_SET_BANDWIDTH_REQUEST, oac_check_int(x)

/** Configures the type of signal being encoded.
 * This is a hint which helps the encoder's mode selection.
 * @see OAC_GET_SIGNAL
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>#OAC_AUTO</dt>        <dd>(default)</dd>
 * <dt>#OAC_SIGNAL_VOICE</dt><dd>Bias thresholds towards choosing LPC or Hybrid modes.</dd>
 * <dt>#OAC_SIGNAL_MUSIC</dt><dd>Bias thresholds towards choosing MDCT modes.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_SIGNAL(x) OAC_SET_SIGNAL_REQUEST, oac_check_int(x)
/** Gets the encoder's configured signal type.
 * @see OAC_SET_SIGNAL
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>#OAC_AUTO</dt>        <dd>(default)</dd>
 * <dt>#OAC_SIGNAL_VOICE</dt><dd>Bias thresholds towards choosing LPC or Hybrid modes.</dd>
 * <dt>#OAC_SIGNAL_MUSIC</dt><dd>Bias thresholds towards choosing MDCT modes.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_SIGNAL(x) OAC_GET_SIGNAL_REQUEST, oac_check_int_ptr(x)


/** Configures the encoder's intended application.
 * The initial value is a mandatory argument to the encoder_create function.
 * @see OAC_GET_APPLICATION
 * @param[in] x <tt>oac_int32</tt>: Returns one of the following values:
 * <dl>
 * <dt>#OAC_APPLICATION_VOIP</dt>
 * <dd>Process signal for improved speech intelligibility.</dd>
 * <dt>#OAC_APPLICATION_AUDIO</dt>
 * <dd>Favor faithfulness to the original input.</dd>
 * <dt>#OAC_APPLICATION_RESTRICTED_LOWDELAY</dt>
 * <dd>Configure the minimum possible coding delay by disabling certain modes
 * of operation.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_APPLICATION(x) OAC_SET_APPLICATION_REQUEST, oac_check_int(x)
/** Gets the encoder's configured application.
 * @see OAC_SET_APPLICATION
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>#OAC_APPLICATION_VOIP</dt>
 * <dd>Process signal for improved speech intelligibility.</dd>
 * <dt>#OAC_APPLICATION_AUDIO</dt>
 * <dd>Favor faithfulness to the original input.</dd>
 * <dt>#OAC_APPLICATION_RESTRICTED_LOWDELAY</dt>
 * <dd>Configure the minimum possible coding delay by disabling certain modes
 * of operation.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_APPLICATION(x) OAC_GET_APPLICATION_REQUEST, oac_check_int_ptr(x)

/** Gets the total samples of delay added by the entire codec.
 * This can be queried by the encoder and then the provided number of samples can be
 * skipped on from the start of the decoder's output to provide time aligned input
 * and output. From the perspective of a decoding application the real data begins this many
 * samples late.
 *
 * The decoder contribution to this delay is identical for all decoders, but the
 * encoder portion of the delay may vary from implementation to implementation,
 * version to version, or even depend on the encoder's initial configuration.
 * Applications needing delay compensation should call this CTL rather than
 * hard-coding a value.
 * @param[out] x <tt>oac_int32 *</tt>:   Number of lookahead samples
 * @hideinitializer */
#define OAC_GET_LOOKAHEAD(x) OAC_GET_LOOKAHEAD_REQUEST, oac_check_int_ptr(x)

/** Configures the encoder's use of inband forward error correction (FEC).
 * @note This is only applicable to the LPC layer
 * @see OAC_GET_INBAND_FEC
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Disable inband FEC (default).</dd>
 * <dt>1</dt><dd>Inband FEC enabled. If the packet loss rate is sufficiently high, Oac will automatically switch to SILK even at high rates to enable use of that FEC.</dd>
 * <dt>2</dt><dd>Inband FEC enabled, but does not necessarily switch to SILK if we have music.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_INBAND_FEC(x) OAC_SET_INBAND_FEC_REQUEST, oac_check_int(x)
/** Gets encoder's configured use of inband forward error correction.
 * @see OAC_SET_INBAND_FEC
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>Inband FEC disabled (default).</dd>
 * <dt>1</dt><dd>Inband FEC enabled. If the packet loss rate is sufficiently high, Oac will automatically switch to SILK even at high rates to enable use of that FEC.</dd>
 * <dt>2</dt><dd>Inband FEC enabled, but does not necessarily switch to SILK if we have music.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_INBAND_FEC(x) OAC_GET_INBAND_FEC_REQUEST, oac_check_int_ptr(x)

/** Configures the encoder's expected packet loss percentage.
 * Higher values trigger progressively more loss resistant behavior in the encoder
 * at the expense of quality at a given bitrate in the absence of packet loss, but
 * greater quality under loss.
 * @see OAC_GET_PACKET_LOSS_PERC
 * @param[in] x <tt>oac_int32</tt>:   Loss percentage in the range 0-100, inclusive (default: 0).
 * @hideinitializer */
#define OAC_SET_PACKET_LOSS_PERC(x) OAC_SET_PACKET_LOSS_PERC_REQUEST, oac_check_int(x)
/** Gets the encoder's configured packet loss percentage.
 * @see OAC_SET_PACKET_LOSS_PERC
 * @param[out] x <tt>oac_int32 *</tt>: Returns the configured loss percentage
 *                                      in the range 0-100, inclusive (default: 0).
 * @hideinitializer */
#define OAC_GET_PACKET_LOSS_PERC(x) OAC_GET_PACKET_LOSS_PERC_REQUEST, oac_check_int_ptr(x)

/** Configures the encoder's use of discontinuous transmission (DTX).
 * @note This is only applicable to the LPC layer
 * @see OAC_GET_DTX
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Disable DTX (default).</dd>
 * <dt>1</dt><dd>Enabled DTX.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_DTX(x) OAC_SET_DTX_REQUEST, oac_check_int(x)
/** Gets encoder's configured use of discontinuous transmission.
 * @see OAC_SET_DTX
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>DTX disabled (default).</dd>
 * <dt>1</dt><dd>DTX enabled.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_DTX(x) OAC_GET_DTX_REQUEST, oac_check_int_ptr(x)
/** Configures the depth of signal being encoded.
 *
 * This is a hint which helps the encoder identify silence and near-silence.
 * It represents the number of significant bits of linear intensity below
 * which the signal contains ignorable quantization or other noise.
 *
 * For example, OAC_SET_LSB_DEPTH(14) would be an appropriate setting
 * for G.711 u-law input. OAC_SET_LSB_DEPTH(16) would be appropriate
 * for 16-bit linear pcm input with oac_encode_float().
 *
 * When using oac_encode() instead of oac_encode_float(), or when liboac
 * is compiled for fixed-point, the encoder uses the minimum of the value
 * set here and the value 16.
 *
 * @see OAC_GET_LSB_DEPTH
 * @param[in] x <tt>oac_int32</tt>: Input precision in bits, between 8 and 24
 *                                   (default: 24).
 * @hideinitializer */
#define OAC_SET_LSB_DEPTH(x) OAC_SET_LSB_DEPTH_REQUEST, oac_check_int(x)
/** Gets the encoder's configured signal depth.
 * @see OAC_SET_LSB_DEPTH
 * @param[out] x <tt>oac_int32 *</tt>: Input precision in bits, between 8 and
 *                                      24 (default: 24).
 * @hideinitializer */
#define OAC_GET_LSB_DEPTH(x) OAC_GET_LSB_DEPTH_REQUEST, oac_check_int_ptr(x)

/** Configures the encoder's use of variable duration frames.
 * When variable duration is enabled, the encoder is free to use a shorter frame
 * size than the one requested in the oac_encode*() call.
 * It is then the user's responsibility
 * to verify how much audio was encoded by checking the ToC byte of the encoded
 * packet. The part of the audio that was not encoded needs to be resent to the
 * encoder for the next call. Do not use this option unless you <b>really</b>
 * know what you are doing.
 * @see OAC_GET_EXPERT_FRAME_DURATION
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>OAC_FRAMESIZE_ARG</dt><dd>Select frame size from the argument (default).</dd>
 * <dt>OAC_FRAMESIZE_2_5_MS</dt><dd>Use 2.5 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_5_MS</dt><dd>Use 5 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_10_MS</dt><dd>Use 10 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_20_MS</dt><dd>Use 20 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_40_MS</dt><dd>Use 40 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_60_MS</dt><dd>Use 60 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_80_MS</dt><dd>Use 80 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_100_MS</dt><dd>Use 100 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_120_MS</dt><dd>Use 120 ms frames.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_EXPERT_FRAME_DURATION(x) OAC_SET_EXPERT_FRAME_DURATION_REQUEST, oac_check_int(x)
/** Gets the encoder's configured use of variable duration frames.
 * @see OAC_SET_EXPERT_FRAME_DURATION
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>OAC_FRAMESIZE_ARG</dt><dd>Select frame size from the argument (default).</dd>
 * <dt>OAC_FRAMESIZE_2_5_MS</dt><dd>Use 2.5 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_5_MS</dt><dd>Use 5 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_10_MS</dt><dd>Use 10 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_20_MS</dt><dd>Use 20 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_40_MS</dt><dd>Use 40 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_60_MS</dt><dd>Use 60 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_80_MS</dt><dd>Use 80 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_100_MS</dt><dd>Use 100 ms frames.</dd>
 * <dt>OAC_FRAMESIZE_120_MS</dt><dd>Use 120 ms frames.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_EXPERT_FRAME_DURATION(x) OAC_GET_EXPERT_FRAME_DURATION_REQUEST, oac_check_int_ptr(x)

/** If set to 1, disables almost all use of prediction, making frames almost
 * completely independent. This reduces quality.
 * @see OAC_GET_PREDICTION_DISABLED
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Enable prediction (default).</dd>
 * <dt>1</dt><dd>Disable prediction.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_PREDICTION_DISABLED(x) OAC_SET_PREDICTION_DISABLED_REQUEST, oac_check_int(x)
/** Gets the encoder's configured prediction status.
 * @see OAC_SET_PREDICTION_DISABLED
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>Prediction enabled (default).</dd>
 * <dt>1</dt><dd>Prediction disabled.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_PREDICTION_DISABLED(x) OAC_GET_PREDICTION_DISABLED_REQUEST, oac_check_int_ptr(x)

/** If non-zero, enables Deep Redundancy (DRED) and use the specified maximum number of 10-ms redundant frames
 * @hideinitializer */
#define OAC_SET_DRED_DURATION(x) OAC_SET_DRED_DURATION_REQUEST, oac_check_int(x)
/** Gets the encoder's configured Deep Redundancy (DRED) maximum number of frames.
 * @hideinitializer */
#define OAC_GET_DRED_DURATION(x) OAC_GET_DRED_DURATION_REQUEST, oac_check_int_ptr(x)

/** Provide external DNN weights from binary object (only when explicitly built without the weights)
 * @hideinitializer */
#define OAC_SET_DNN_BLOB(data, len) OAC_SET_DNN_BLOB_REQUEST, oac_check_void_ptr(data), oac_check_int(len)


/**@}*/

/** @defgroup oac_genericctls Generic CTLs
 *
 * These macros are used with the \c oac_decoder_ctl and
 * \c oac_encoder_ctl calls to generate a particular
 * request.
 *
 * When called on an \c OacDecoder they apply to that
 * particular decoder instance. When called on an
 * \c OacEncoder they apply to the corresponding setting
 * on that encoder instance, if present.
 *
 * Some usage examples:
 *
 * @code
 * int ret;
 * oac_int32 pitch;
 * ret = oac_decoder_ctl(dec_ctx, OAC_GET_PITCH(&pitch));
 * if (ret == OAC_OK) return ret;
 *
 * oac_encoder_ctl(enc_ctx, OAC_RESET_STATE);
 * oac_decoder_ctl(dec_ctx, OAC_RESET_STATE);
 *
 * oac_int32 enc_bw, dec_bw;
 * oac_encoder_ctl(enc_ctx, OAC_GET_BANDWIDTH(&enc_bw));
 * oac_decoder_ctl(dec_ctx, OAC_GET_BANDWIDTH(&dec_bw));
 * if (enc_bw != dec_bw) {
 *   printf("packet bandwidth mismatch!\n");
 * }
 * @endcode
 *
 * @see oac_encoder, oac_decoder_ctl, oac_encoder_ctl, oac_decoderctls, oac_encoderctls
 * @{
 */

/** Resets the codec state to be equivalent to a freshly initialized state.
 * This should be called when switching streams in order to prevent
 * the back to back decoding from giving different results from
 * one at a time decoding.
 * @hideinitializer */
#define OAC_RESET_STATE 4028

/** Gets the final state of the codec's entropy coder.
 * This is used for testing purposes,
 * The encoder and decoder state should be identical after coding a payload
 * (assuming no data corruption or software bugs)
 *
 * @param[out] x <tt>oac_uint32 *</tt>: Entropy coder state
 *
 * @hideinitializer */
#define OAC_GET_FINAL_RANGE(x) OAC_GET_FINAL_RANGE_REQUEST, oac_check_uint_ptr(x)

/** Gets the encoder's configured bandpass or the decoder's last bandpass.
 * @see OAC_SET_BANDWIDTH
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>#OAC_AUTO</dt>                    <dd>(default)</dd>
 * <dt>#OAC_BANDWIDTH_NARROWBAND</dt>    <dd>4 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_MEDIUMBAND</dt>    <dd>6 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_WIDEBAND</dt>      <dd>8 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_SUPERWIDEBAND</dt><dd>12 kHz passband</dd>
 * <dt>#OAC_BANDWIDTH_FULLBAND</dt>     <dd>20 kHz passband</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_BANDWIDTH(x) OAC_GET_BANDWIDTH_REQUEST, oac_check_int_ptr(x)

/** Gets the sampling rate the encoder or decoder was initialized with.
 * This simply returns the <code>Fs</code> value passed to oac_encoder_init()
 * or oac_decoder_init().
 * @param[out] x <tt>oac_int32 *</tt>: Sampling rate of encoder or decoder.
 * @hideinitializer
 */
#define OAC_GET_SAMPLE_RATE(x) OAC_GET_SAMPLE_RATE_REQUEST, oac_check_int_ptr(x)

/** If set to 1, disables the use of phase inversion for intensity stereo,
 * improving the quality of mono downmixes, but slightly reducing normal
 * stereo quality. Disabling phase inversion in the decoder does not comply
 * with RFC 6716, although it does not cause any interoperability issue and
 * is expected to become part of the Oac standard once RFC 6716 is updated
 * by draft-ietf-codec-oac-update.
 * @see OAC_GET_PHASE_INVERSION_DISABLED
 * @param[in] x <tt>oac_int32</tt>: Allowed values:
 * <dl>
 * <dt>0</dt><dd>Enable phase inversion (default).</dd>
 * <dt>1</dt><dd>Disable phase inversion.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_SET_PHASE_INVERSION_DISABLED(x) OAC_SET_PHASE_INVERSION_DISABLED_REQUEST, oac_check_int(x)
/** Gets the encoder's configured phase inversion status.
 * @see OAC_SET_PHASE_INVERSION_DISABLED
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>Stereo phase inversion enabled (default).</dd>
 * <dt>1</dt><dd>Stereo phase inversion disabled.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_PHASE_INVERSION_DISABLED(x) OAC_GET_PHASE_INVERSION_DISABLED_REQUEST, oac_check_int_ptr(x)
/** Gets the DTX state of the encoder.
 * Returns whether the last encoded frame was either a comfort noise update
 * during DTX or not encoded because of DTX.
 * @param[out] x <tt>oac_int32 *</tt>: Returns one of the following values:
 * <dl>
 * <dt>0</dt><dd>The encoder is not in DTX.</dd>
 * <dt>1</dt><dd>The encoder is in DTX.</dd>
 * </dl>
 * @hideinitializer */
#define OAC_GET_IN_DTX(x) OAC_GET_IN_DTX_REQUEST, oac_check_int_ptr(x)

/**@}*/

/** @defgroup oac_decoderctls Decoder related CTLs
 * @see oac_genericctls, oac_encoderctls, oac_decoder
 * @{
 */

/** Configures decoder gain adjustment.
 * Scales the decoded output by a factor specified in Q8 dB units.
 * This has a maximum range of -32768 to 32767 inclusive, and returns
 * OAC_BAD_ARG otherwise. The default is zero indicating no adjustment.
 * This setting survives decoder reset.
 *
 * gain = pow(10, x/(20.0*256))
 *
 * @param[in] x <tt>oac_int32</tt>:   Amount to scale PCM signal by in Q8 dB units.
 * @hideinitializer */
#define OAC_SET_GAIN(x) OAC_SET_GAIN_REQUEST, oac_check_int(x)
/** Gets the decoder's configured gain adjustment. @see OAC_SET_GAIN
 *
 * @param[out] x <tt>oac_int32 *</tt>: Amount to scale PCM signal by in Q8 dB units.
 * @hideinitializer */
#define OAC_GET_GAIN(x) OAC_GET_GAIN_REQUEST, oac_check_int_ptr(x)

/** Gets the duration (in samples) of the last packet successfully decoded or concealed.
 * @param[out] x <tt>oac_int32 *</tt>: Number of samples (at current sampling rate).
 * @hideinitializer */
#define OAC_GET_LAST_PACKET_DURATION(x) OAC_GET_LAST_PACKET_DURATION_REQUEST, oac_check_int_ptr(x)

/** Gets the pitch of the last decoded frame, if available.
 * This can be used for any post-processing algorithm requiring the use of pitch,
 * e.g. time stretching/shortening. If the last frame was not voiced, or if the
 * pitch was not coded in the frame, then zero is returned.
 *
 * This CTL is only implemented for decoder instances.
 *
 * @param[out] x <tt>oac_int32 *</tt>: pitch period at 48 kHz (or 0 if not available)
 *
 * @hideinitializer */
#define OAC_GET_PITCH(x) OAC_GET_PITCH_REQUEST, oac_check_int_ptr(x)

/** Enables blind bandwidth extension for wideband signals if decoding sampling rate is 48 kHz.
 * @param[in] x <tt>oac_int32 </tt>: 1 enables bandwidth extension, 0 disables it.
 * The default is 0.
 *
 * @hideinitializer */
#define OAC_SET_OSCE_BWE(x) OAC_SET_OSCE_BWE_REQUEST, oac_check_int(x)
/** Gets blind bandwidth extension flag for wideband signals if decoding sampling rate is 48 kHz.
 * @param[out] x <tt>oac_int32 *</tt>: 1 if bwe enabled, 0 if disabled.
 *
 * @hideinitializer */
#define OAC_GET_OSCE_BWE(x) OAC_GET_OSCE_BWE_REQUEST, oac_check_int_ptr(x)

/** If set to 1, the decoder will ignore all extensions found in the padding area
 * (does not affect DRED, which is decoded separately).
 * @hideinitializer */
#define OAC_SET_IGNORE_EXTENSIONS(x) OAC_SET_IGNORE_EXTENSIONS_REQUEST, oac_check_int(x)
/** Gets whether the decoder is ignoring extensions.
 * @hideinitializer */
#define OAC_GET_IGNORE_EXTENSIONS(x) OAC_GET_IGNORE_EXTENSIONS_REQUEST, oac_check_int_ptr(x)

/** Gets the format the encoder or decoder was initialized with.
 * @param[out] x <tt>oac_int32 *</tt>: Returns OAC_FORMAT_STANDARD or OAC_FORMAT_AMBISONICS.
 * @hideinitializer
 */
#define OAC_GET_FORMAT(x) OAC_GET_FORMAT_REQUEST, oac_check_int_ptr(x)
/**@}*/

/** @defgroup oac_libinfo Oac library information functions
 * @{
 */

/** Converts an oac error code into a human readable string.
 *
 * @param[in] error <tt>int</tt>: Error number
 * @returns Error string
 */
OAC_EXPORT const char *oac_strerror(int error);

/** Gets the liboac version string.
 *
 * Applications may look for the substring "-fixed" in the version string to
 * determine whether they have a fixed-point or floating-point build at
 * runtime.
 *
 * @returns Version string
 */
OAC_EXPORT const char *oac_get_version_string(void);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* OAC_DEFINES_H */

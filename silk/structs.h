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

/***********************************************************************
   Copyright (c) 2006-2011, Skype Limited. All rights reserved.
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   - Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   - Neither the name of Internet Society, IETF or IETF Trust, nor the
   names of specific contributors, may be used to endorse or promote
   products derived from this software without specific prior written
   permission.
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

#ifndef SILK_STRUCTS_H
#define SILK_STRUCTS_H

#include "typedef.h"
#include "SigProc_FIX.h"
#include "define.h"
#include "entenc.h"
#include "entdec.h"

#ifdef ENABLE_DEEP_PLC
# include "lpcnet.h"
# include "lpcnet_private.h"
#endif

#ifdef ENABLE_DRED
# include "dred_encoder.h"
# include "dred_decoder.h"
#endif

#ifdef ENABLE_OSCE
# include "osce_config.h"
# include "osce_structs.h"
#endif


/************************************/
/* Noise shaping quantization state */
/************************************/
typedef struct {
    oac_int16 xq[           2*MAX_FRAME_LENGTH ];                    /* Buffer for quantized output signal                             */
    oac_int32 sLTP_shp_Q14[ 2*MAX_FRAME_LENGTH ];
    oac_int32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
    oac_int32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
    oac_int32 sLF_AR_shp_Q14;
    oac_int32 sDiff_shp_Q14;
    oac_int lagPrev;
    oac_int sLTP_buf_idx;
    oac_int sLTP_shp_buf_idx;
    oac_int32 rand_seed;
    oac_int32 prev_gain_Q16;
    oac_int rewhite_flag;
} silk_nsq_state;

/********************************/
/* VAD state                    */
/********************************/
typedef struct {
    oac_int32 AnaState[ 2 ];                                   /* Analysis filterbank state: 0-8 kHz                                   */
    oac_int32 AnaState1[ 2 ];                                  /* Analysis filterbank state: 0-4 kHz                                   */
    oac_int32 AnaState2[ 2 ];                                  /* Analysis filterbank state: 0-2 kHz                                   */
    oac_int32 XnrgSubfr[ VAD_N_BANDS ];                        /* Subframe energies                                                    */
    oac_int32 NrgRatioSmth_Q8[ VAD_N_BANDS ];                  /* Smoothed energy level in each band                                   */
    oac_int16 HPstate;                                         /* State of differentiator in the lowest band                           */
    oac_int32 NL[ VAD_N_BANDS ];                               /* Noise energy level in each band                                      */
    oac_int32 inv_NL[ VAD_N_BANDS ];                           /* Inverse noise energy level in each band                              */
    oac_int32 NoiseLevelBias[ VAD_N_BANDS ];                   /* Noise level estimator bias/offset                                    */
    oac_int32 counter;                                         /* Frame counter used in the initial phase                              */
} silk_VAD_state;

/* Variable cut-off low-pass filter state */
typedef struct {
    oac_int32 In_LP_State[ 2 ];                             /* Low pass filter state */
    oac_int32 transition_frame_no;                          /* Counter which is mapped to a cut-off frequency */
    oac_int mode;                                           /* Operating mode, <0: switch down, >0: switch up; 0: do nothing           */
    oac_int32 saved_fs_kHz;                                 /* If non-zero, holds the last sampling rate before a bandwidth switching reset. */
} silk_LP_state;

/* Structure containing NLSF codebook */
typedef struct {
    const oac_int16 nVectors;
    const oac_int16 order;
    const oac_int16 quantStepSize_Q16;
    const oac_int16 invQuantStepSize_Q6;
    const oac_uint8             *CB1_NLSF_Q8;
    const oac_int16             *CB1_Wght_Q9;
    const oac_uint8             *CB1_iCDF;
    const oac_uint8             *pred_Q8;
    const oac_uint8             *ec_sel;
    const oac_uint8             *ec_iCDF;
    const oac_uint8             *ec_Rates_Q5;
    const oac_int16             *deltaMin_Q15;
} silk_NLSF_CB_struct;

typedef struct {
    oac_int16 pred_prev_Q13[ 2 ];
    oac_int16 sMid[ 2 ];
    oac_int16 sSide[ 2 ];
    oac_int32 mid_side_amp_Q0[ 4 ];
    oac_int16 smth_width_Q14;
    oac_int16 width_prev_Q14;
    oac_int16 silent_side_len;
    oac_int8 predIx[ MAX_FRAMES_PER_PACKET ][ 2 ][ 3 ];
    oac_int8 mid_only_flags[ MAX_FRAMES_PER_PACKET ];
} stereo_enc_state;

typedef struct {
    oac_int16 pred_prev_Q13[ 2 ];
    oac_int16 sMid[ 2 ];
    oac_int16 sSide[ 2 ];
} stereo_dec_state;

typedef struct {
    oac_int8 GainsIndices[ MAX_NB_SUBFR ];
    oac_int8 LTPIndex[ MAX_NB_SUBFR ];
    oac_int8 NLSFIndices[ MAX_LPC_ORDER + 1 ];
    oac_int16 lagIndex;
    oac_int8 contourIndex;
    oac_int8 signalType;
    oac_int8 quantOffsetType;
    oac_int8 NLSFInterpCoef_Q2;
    oac_int8 PERIndex;
    oac_int8 LTP_scaleIndex;
    oac_int8 Seed;
} SideInfoIndices;

/********************************/
/* Encoder state                */
/********************************/
typedef struct {
    oac_int32 In_HP_State[ 2 ];                                    /* High pass filter state                                           */
    oac_int32 variable_HP_smth1_Q15;                               /* State of first smoother                                          */
    oac_int32 variable_HP_smth2_Q15;                               /* State of second smoother                                         */
    silk_LP_state sLP;                                              /* Low pass filter state                                            */
    silk_VAD_state sVAD;                                            /* Voice activity detector state                                    */
    silk_nsq_state sNSQ;                                            /* Noise Shape Quantizer State                                      */
    oac_int16 prev_NLSFq_Q15[ MAX_LPC_ORDER ];                     /* Previously quantized NLSF vector                                 */
    oac_int speech_activity_Q8;                                    /* Speech activity                                                  */
    oac_int allow_bandwidth_switch;                                /* Flag indicating that switching of internal bandwidth is allowed  */
    oac_int8 LBRRprevLastGainIndex;
    oac_int8 prevSignalType;
    oac_int prevLag;
    oac_int pitch_LPC_win_length;
    oac_int max_pitch_lag;                                         /* Highest possible pitch lag (samples)                             */
    oac_int32 API_fs_Hz;                                           /* API sampling frequency (Hz)                                      */
    oac_int32 prev_API_fs_Hz;                                      /* Previous API sampling frequency (Hz)                             */
    oac_int maxInternal_fs_Hz;                                     /* Maximum internal sampling frequency (Hz)                         */
    oac_int minInternal_fs_Hz;                                     /* Minimum internal sampling frequency (Hz)                         */
    oac_int desiredInternal_fs_Hz;                                 /* Soft request for internal sampling frequency (Hz)                */
    oac_int fs_kHz;                                                /* Internal sampling frequency (kHz)                                */
    oac_int nb_subfr;                                              /* Number of 5 ms subframes in a frame                              */
    oac_int frame_length;                                          /* Frame length (samples)                                           */
    oac_int subfr_length;                                          /* Subframe length (samples)                                        */
    oac_int ltp_mem_length;                                        /* Length of LTP memory                                             */
    oac_int la_pitch;                                              /* Look-ahead for pitch analysis (samples)                          */
    oac_int la_shape;                                              /* Look-ahead for noise shape analysis (samples)                    */
    oac_int shapeWinLength;                                        /* Window length for noise shape analysis (samples)                 */
    oac_int32 TargetRate_bps;                                      /* Target bitrate (bps)                                             */
    oac_int PacketSize_ms;                                         /* Number of milliseconds to put in each packet                     */
    oac_int PacketLoss_perc;                                       /* Packet loss rate measured by farend                              */
    oac_int32 frameCounter;
    oac_int Complexity;                                            /* Complexity setting                                               */
    oac_int nStatesDelayedDecision;                                /* Number of states in delayed decision quantization                */
    oac_int useInterpolatedNLSFs;                                  /* Flag for using NLSF interpolation                                */
    oac_int shapingLPCOrder;                                       /* Filter order for noise shaping filters                           */
    oac_int predictLPCOrder;                                       /* Filter order for prediction filters                              */
    oac_int pitchEstimationComplexity;                             /* Complexity level for pitch estimator                             */
    oac_int pitchEstimationLPCOrder;                               /* Whitening filter order for pitch estimator                       */
    oac_int32 pitchEstimationThreshold_Q16;                        /* Threshold for pitch estimator                                    */
    oac_int32 sum_log_gain_Q7;                                     /* Cumulative max prediction gain                                   */
    oac_int NLSF_MSVQ_Survivors;                                   /* Number of survivors in NLSF MSVQ                                 */
    oac_int first_frame_after_reset;                               /* Flag for deactivating NLSF interpolation, pitch prediction       */
    oac_int controlled_since_last_payload;                         /* Flag for ensuring codec_control only runs once per packet        */
    oac_int warping_Q16;                                           /* Warping parameter for warped noise shaping                       */
    oac_int useCBR;                                                /* Flag to enable constant bitrate                                  */
    oac_int prefillFlag;                                           /* Flag to indicate that only buffers are prefilled, no coding      */
    const oac_uint8             *pitch_lag_low_bits_iCDF;          /* Pointer to iCDF table for low bits of pitch lag index            */
    const oac_uint8             *pitch_contour_iCDF;               /* Pointer to iCDF table for pitch contour index                    */
    const silk_NLSF_CB_struct    *psNLSF_CB;                        /* Pointer to NLSF codebook                                         */
    oac_int input_quality_bands_Q15[ VAD_N_BANDS ];
    oac_int input_tilt_Q15;
    oac_int SNR_dB_Q7;                                             /* Quality setting                                                  */

    oac_int8 VAD_flags[ MAX_FRAMES_PER_PACKET ];
    oac_int8 LBRR_flag;
    oac_int LBRR_flags[ MAX_FRAMES_PER_PACKET ];

    SideInfoIndices indices;
    oac_int8 pulses[ MAX_FRAME_LENGTH ];

    int arch;

    /* Input/output buffering */
    oac_int16 inputBuf[ MAX_FRAME_LENGTH + 2 ];                    /* Buffer containing input signal                                   */
    oac_int inputBufIx;
    oac_int nFramesPerPacket;
    oac_int nFramesEncoded;                                        /* Number of frames analyzed in current packet                      */

    oac_int nChannelsAPI;
    oac_int nChannelsInternal;
    oac_int channelNb;

    /* Parameters For LTP scaling Control */
    oac_int frames_since_onset;

    /* Specifically for entropy coding */
    oac_int ec_prevSignalType;
    oac_int16 ec_prevLagIndex;

    silk_resampler_state_struct resampler_state;

    /* DTX */
    oac_int useDTX;                                                /* Flag to enable DTX                                               */
    oac_int inDTX;                                                 /* Flag to signal DTX period                                        */
    oac_int noSpeechCounter;                                       /* Counts concecutive nonactive frames, used by DTX                 */

    /* Inband Low Bitrate Redundancy (LBRR) data */
    oac_int useInBandFEC;                                          /* Saves the API setting for query                                  */
    oac_int LBRR_enabled;                                          /* Depends on useInBandFRC, bitrate and packet loss rate            */
    oac_int LBRR_GainIncreases;                                    /* Gains increment for coding LBRR frames                           */
    SideInfoIndices indices_LBRR[ MAX_FRAMES_PER_PACKET ];
    oac_int8 pulses_LBRR[ MAX_FRAMES_PER_PACKET ][ MAX_FRAME_LENGTH ];
} silk_encoder_state;


#ifdef ENABLE_OSCE
typedef struct {
    OSCEFeatureState features;
    OSCEState state;
    int method;
} silk_OSCE_struct;

typedef struct {
    OSCEBWEFeatureState features;
    OSCEBWEState state;
} silk_OSCE_BWE_struct;
#endif

/* Struct for Packet Loss Concealment */
typedef struct {
    oac_int32 pitchL_Q8;                                           /* Pitch lag to use for voiced concealment                          */
    oac_int16 LTPCoef_Q14[ LTP_ORDER ];                            /* LTP coefficients to use for voiced concealment                   */
    oac_int16 prevLPC_Q12[ MAX_LPC_ORDER ];
    oac_int last_frame_lost;                                       /* Was previous frame lost                                          */
    oac_int32 rand_seed;                                           /* Seed for unvoiced signal generation                              */
    oac_int16 randScale_Q14;                                       /* Scaling of unvoiced random signal                                */
    oac_int32 conc_energy;
    oac_int conc_energy_shift;
    oac_int16 prevLTP_scale_Q14;
    oac_int32 prevGain_Q16[ 2 ];
    oac_int fs_kHz;
    oac_int nb_subfr;
    oac_int subfr_length;
    oac_int enable_deep_plc;
#ifdef ENABLE_OSCE
    oac_int16 prev_osce_out[ MAX_LPC_ORDER ];
#endif
} silk_PLC_struct;

/* Struct for CNG */
typedef struct {
    oac_int32 CNG_exc_buf_Q14[ MAX_FRAME_LENGTH ];
    oac_int16 CNG_smth_NLSF_Q15[ MAX_LPC_ORDER ];
    oac_int32 CNG_synth_state[ MAX_LPC_ORDER ];
    oac_int32 CNG_smth_Gain_Q16;
    oac_int32 rand_seed;
    oac_int fs_kHz;
} silk_CNG_struct;

/********************************/
/* Decoder state                */
/********************************/
typedef struct {
#ifdef ENABLE_OSCE
    silk_OSCE_struct osce;
# ifdef ENABLE_OSCE_BWE
    silk_OSCE_BWE_struct osce_bwe;
# endif
#endif
#define SILK_DECODER_STATE_RESET_START prev_gain_Q16
    oac_int32 prev_gain_Q16;
    oac_int32 exc_Q14[ MAX_FRAME_LENGTH ];
    oac_int32 sLPC_Q14_buf[ MAX_LPC_ORDER ];
    oac_int16 outBuf[ MAX_FRAME_LENGTH + 2*MAX_SUB_FRAME_LENGTH ];                     /* Buffer for output signal                     */
    oac_int lagPrev;                                               /* Previous Lag                                                     */
    oac_int8 LastGainIndex;                                        /* Previous gain index                                              */
    oac_int fs_kHz;                                                /* Sampling frequency in kHz                                        */
    oac_int32 fs_API_hz;                                           /* API sample frequency (Hz)                                        */
    oac_int nb_subfr;                                              /* Number of 5 ms subframes in a frame                              */
    oac_int frame_length;                                          /* Frame length (samples)                                           */
    oac_int subfr_length;                                          /* Subframe length (samples)                                        */
    oac_int ltp_mem_length;                                        /* Length of LTP memory                                             */
    oac_int LPC_order;                                             /* LPC order                                                        */
    oac_int16 prevNLSF_Q15[ MAX_LPC_ORDER ];                       /* Used to interpolate LSFs                                         */
    oac_int first_frame_after_reset;                               /* Flag for deactivating NLSF interpolation                         */
    const oac_uint8            *pitch_lag_low_bits_iCDF;           /* Pointer to iCDF table for low bits of pitch lag index            */
    const oac_uint8            *pitch_contour_iCDF;                /* Pointer to iCDF table for pitch contour index                    */

    /* For buffering payload in case of more frames per packet */
    oac_int nFramesDecoded;
    oac_int nFramesPerPacket;

    /* Specifically for entropy coding */
    oac_int ec_prevSignalType;
    oac_int16 ec_prevLagIndex;

    oac_int VAD_flags[ MAX_FRAMES_PER_PACKET ];
    oac_int LBRR_flag;
    oac_int LBRR_flags[ MAX_FRAMES_PER_PACKET ];

    silk_resampler_state_struct resampler_state;

    const silk_NLSF_CB_struct   *psNLSF_CB;                         /* Pointer to NLSF codebook                                         */

    /* Quantization indices */
    SideInfoIndices indices;

    /* CNG state */
    silk_CNG_struct sCNG;

    /* Stuff used for PLC */
    oac_int lossCnt;
    oac_int prevSignalType;
    int arch;

    silk_PLC_struct sPLC;

} silk_decoder_state;

/************************/
/* Decoder control      */
/************************/
typedef struct {
    /* Prediction and coding parameters */
    oac_int pitchL[ MAX_NB_SUBFR ];
    oac_int32 Gains_Q16[ MAX_NB_SUBFR ];
    /* Holds interpolated and final coefficients, 4-byte aligned */
    silk_DWORD_ALIGN oac_int16 PredCoef_Q12[ 2 ][ MAX_LPC_ORDER ];
    oac_int16 LTPCoef_Q14[ LTP_ORDER*MAX_NB_SUBFR ];
    oac_int LTP_scale_Q14;
} silk_decoder_control;


#endif

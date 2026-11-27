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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <math.h>
#include "pitchdnn.h"
#include "os_support.h"
#include "nnet.h"
#include "lpcnet_private.h"


float oaci_compute_pitchdnn(
    PitchDNNState *st,
    const float *if_features,
    const float *xcorr_features,
    int arch) {
    float if1_out[DENSE_IF_UPSAMPLER_1_OUT_SIZE];
    float downsampler_in[NB_XCORR_FEATURES + DENSE_IF_UPSAMPLER_2_OUT_SIZE];
    float downsampler_out[DENSE_DOWNSAMPLER_OUT_SIZE];
    float conv1_tmp1[(NB_XCORR_FEATURES + 2)*8] = {0};
    float conv1_tmp2[(NB_XCORR_FEATURES + 2)*8] = {0};
    float output[DENSE_FINAL_UPSAMPLER_OUT_SIZE];
    int i;
    int pos = 0;
    float maxval = -1;
    float sum = 0;
    float count = 0;
    PitchDNN *model = &st->model;
    /* IF */
    oaci_compute_generic_dense(&model->dense_if_upsampler_1, if1_out, if_features, ACTIVATION_TANH, arch);
    oaci_compute_generic_dense(&model->dense_if_upsampler_2, &downsampler_in[NB_XCORR_FEATURES], if1_out, ACTIVATION_TANH,
    arch);
    /* xcorr*/
    OAC_COPY(&conv1_tmp1[1], xcorr_features, NB_XCORR_FEATURES);
    oaci_compute_conv2d(&model->conv2d_1, &conv1_tmp2[1], st->xcorr_mem1, conv1_tmp1, NB_XCORR_FEATURES,
    NB_XCORR_FEATURES + 2, ACTIVATION_TANH, arch);
    oaci_compute_conv2d(&model->conv2d_2, downsampler_in, st->xcorr_mem2, conv1_tmp2, NB_XCORR_FEATURES, NB_XCORR_FEATURES,
    ACTIVATION_TANH, arch);

    oaci_compute_generic_dense(&model->dense_downsampler, downsampler_out, downsampler_in, ACTIVATION_TANH, arch);
    oaci_compute_generic_gru(&model->gru_1_input, &model->gru_1_recurrent, st->gru_state, downsampler_out, arch);
    oaci_compute_generic_dense(&model->dense_final_upsampler, output, st->gru_state, ACTIVATION_LINEAR, arch);
    for (i = 0; i < 180; i++) {
        if (output[i] > maxval) {
            pos = i;
            maxval = output[i];
        }
    }
    for (i = IMAX(0, pos - 2); i <= IMIN(179, pos + 2); i++) {
        float p = exp(output[i]);
        sum += p*i;
        count += p;
    }
    /*printf("%d %f\n", pos, sum/count);*/
    return (1.f/60.f)*(sum/count) - 1.5;
    /*return 256.f/pow(2.f, (1.f/60.f)*i);*/
}


void oaci_pitchdnn_init(PitchDNNState *st) {
    int ret;
    OAC_CLEAR(st, 1);
#ifndef USE_WEIGHTS_FILE
    ret = oaci_init_pitchdnn(&st->model, oaci_pitchdnn_arrays);
#else
    ret = 0;
#endif
    celt_assert(ret == 0);
}

int oaci_pitchdnn_load_model(PitchDNNState *st, const void *data, int len) {
    WeightArray *list;
    int ret;
    if (oaci_parse_weights(&list, data, len) < 0) return OAC_BAD_ARG;
    ret = oaci_init_pitchdnn(&st->model, list);
    oac_free(list);
    if (ret == 0) return 0;
    else return -1;
}

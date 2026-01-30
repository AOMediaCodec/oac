/* Copyright (c) 2018-2019 Mozilla
                 2023 Amazon */
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
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
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

#include "arm/armcpu.h"
#include "nnet.h"

#if defined(OAC_HAVE_RTCD)

# if (defined(OAC_ARM_MAY_HAVE_DOTPROD) && !defined(OAC_ARM_PRESUME_DOTPROD))

void (*const oaci_DNN_COMPUTE_LINEAR_IMPL[OAC_ARCHMASK + 1])(
         const LinearLayer *linear,
         float *out,
         const float *in
    ) = {
    oaci_compute_linear_c,              /* default */
    oaci_compute_linear_c,
    oaci_compute_linear_c,
    MAY_HAVE_NEON(oaci_compute_linear), /* neon  */
    MAY_HAVE_DOTPROD(oaci_compute_linear) /* dotprod  */
};

# endif

# if (defined(OAC_ARM_MAY_HAVE_DOTPROD) || defined(OAC_ARM_MAY_HAVE_NEON)) && !defined(OAC_ARM_PRESUME_NEON)

void (*const oaci_DNN_COMPUTE_ACTIVATION_IMPL[OAC_ARCHMASK + 1])(
         float *output,
         const float *input,
         int N,
         int activation
    ) = {
    oaci_compute_activation_c,                /* default */
    oaci_compute_activation_c,
    oaci_compute_activation_c,
    MAY_HAVE_NEON(oaci_compute_activation),   /* neon  */
    MAY_HAVE_DOTPROD(oaci_compute_activation) /* dotprod  */
};

void (*const oaci_DNN_COMPUTE_CONV2D_IMPL[OAC_ARCHMASK + 1])(
         const Conv2dLayer *conv,
         float *out,
         float *mem,
         const float *in,
         int height,
         int hstride,
         int activation
    ) = {
    oaci_compute_conv2d_c,                /* default */
    oaci_compute_conv2d_c,
    oaci_compute_conv2d_c,
    MAY_HAVE_NEON(oaci_compute_conv2d),   /* neon  */
    MAY_HAVE_DOTPROD(oaci_compute_conv2d) /* dotprod  */
};


# endif


#endif

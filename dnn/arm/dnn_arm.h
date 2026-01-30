/* Copyright (c) 2011-2019 Mozilla
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

#ifndef DNN_ARM_H
#define DNN_ARM_H

#include "cpu_support.h"
#include "oac_types.h"

void oaci_compute_linear_dotprod(const LinearLayer *linear, float *out, const float *in);
void oaci_compute_linear_neon(const LinearLayer *linear, float *out, const float *in);

void oaci_compute_activation_neon(float *output, const float *input, int N, int activation);
void oaci_compute_activation_dotprod(float *output, const float *input, int N, int activation);

void oaci_compute_conv2d_neon(const Conv2dLayer *conv, float *out, float *mem, const float *in, int height, int hstride,
    int activation);
void oaci_compute_conv2d_dotprod(const Conv2dLayer *conv, float *out, float *mem, const float *in, int height, int hstride,
    int activation);

#if defined(OAC_ARM_PRESUME_DOTPROD)

# define OVERRIDE_COMPUTE_LINEAR
# define oaci_compute_linear(linear, out, in, arch) ((void)(arch), oaci_compute_linear_dotprod(linear, out, in))

#elif defined(OAC_ARM_PRESUME_NEON_INTR) && !defined(OAC_ARM_MAY_HAVE_DOTPROD)

# define OVERRIDE_COMPUTE_LINEAR
# define oaci_compute_linear(linear, out, in, arch) ((void)(arch), oaci_compute_linear_neon(linear, out, in))

#elif defined(OAC_HAVE_RTCD) && (defined(OAC_ARM_MAY_HAVE_DOTPROD) || defined(OAC_ARM_MAY_HAVE_NEON))

extern void (*const oaci_DNN_COMPUTE_LINEAR_IMPL[OAC_ARCHMASK + 1])(
                    const LinearLayer *linear,
                    float *out,
                    const float *in
    );
# define OVERRIDE_COMPUTE_LINEAR
# define oaci_compute_linear(linear, out, in, arch) \
        ((*oaci_DNN_COMPUTE_LINEAR_IMPL[(arch)&OAC_ARCHMASK])(linear, out, in))


#endif

#if defined(OAC_ARM_PRESUME_NEON)

# define OVERRIDE_COMPUTE_ACTIVATION
# define oaci_compute_activation(output, input, N, activation, arch) ((void)(arch), \
                                                                 oaci_compute_activation_neon(output, input, N, activation))
# define OVERRIDE_COMPUTE_CONV2D
# define oaci_compute_conv2d(conv, out, mem, in, height, hstride, activation, arch) ((void)(arch), \
                                                                                oaci_compute_conv2d_neon(conv, out, mem, in, \
    height, hstride, activation))

#elif defined(OAC_HAVE_RTCD) && (defined(OAC_ARM_MAY_HAVE_DOTPROD) || defined(OAC_ARM_MAY_HAVE_NEON))

extern void (*const oaci_DNN_COMPUTE_ACTIVATION_IMPL[OAC_ARCHMASK + 1])(
                    float *output,
                    const float *input,
                    int N,
                    int activation
    );
# define OVERRIDE_COMPUTE_ACTIVATION
# define oaci_compute_activation(output, input, N, activation, arch) \
        ((*oaci_DNN_COMPUTE_ACTIVATION_IMPL[(arch)&OAC_ARCHMASK])(output, input, N, activation))


extern void (*const oaci_DNN_COMPUTE_CONV2D_IMPL[OAC_ARCHMASK + 1])(
                    const Conv2dLayer *conv,
                    float *out,
                    float *mem,
                    const float *in,
                    int height,
                    int hstride,
                    int activation
    );
# define OVERRIDE_COMPUTE_CONV2D
# define oaci_compute_conv2d(conv, out, mem, in, height, hstride, activation, arch) \
        ((*oaci_DNN_COMPUTE_CONV2D_IMPL[(arch)&OAC_ARCHMASK])(conv, out, mem, in, height, hstride, activation))


#endif


#endif /* DNN_ARM_H */

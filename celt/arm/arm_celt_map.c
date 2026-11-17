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

/* Copyright (c) 2010 Xiph.Org Foundation
 * Copyright (c) 2013 Parrot
 * Copyright (c) 2024 Arm Limited */
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

#include "kiss_fft.h"
#include "mathops.h"
#include "mdct.h"
#include "pitch.h"

#if defined(OAC_HAVE_RTCD)

# if !defined(DISABLE_FLOAT_API)
#  if defined(OAC_ARM_MAY_HAVE_NEON_INTR) && !defined(OAC_ARM_PRESUME_NEON_INTR)
void (*const OACI_CELT_FLOAT2INT16_IMPL[OAC_ARCHMASK + 1])(const float * OAC_RESTRICT in, short * OAC_RESTRICT out,
int cnt) = {
    oaci_celt_float2int16_c, /* ARMv4 */
    oaci_celt_float2int16_c, /* EDSP */
    oaci_celt_float2int16_c, /* Media */
    oaci_celt_float2int16_neon,/* NEON */
    oaci_celt_float2int16_neon /* DOTPROD */
};

int (*const OAC_LIMIT2_CHECKWITHIN1_IMPL[OAC_ARCHMASK + 1])(float * samples, int cnt) = {
    oac_limit2_checkwithin1_c, /* ARMv4 */
    oac_limit2_checkwithin1_c, /* EDSP */
    oac_limit2_checkwithin1_c, /* Media */
    oac_limit2_checkwithin1_neon,/* NEON */
    oac_limit2_checkwithin1_neon /* DOTPROD */
};
#  endif
# endif

# if defined(OAC_ARM_MAY_HAVE_NEON_INTR) && !defined(OAC_ARM_PRESUME_NEON_INTR)
oac_val32 (*const OACI_CELT_INNER_PROD_IMPL[OAC_ARCHMASK + 1])(const oac_val16 *x, const oac_val16 *y, int N) = {
    oaci_celt_inner_prod_c, /* ARMv4 */
    oaci_celt_inner_prod_c, /* EDSP */
    oaci_celt_inner_prod_c, /* Media */
    oaci_celt_inner_prod_neon,/* NEON */
    oaci_celt_inner_prod_neon /* DOTPROD */
};

void (*const OACI_DUAL_INNER_PROD_IMPL[OAC_ARCHMASK + 1])(const oac_val16 *x, const oac_val16 *y01, const oac_val16 *y02,
      int N, oac_val32 *xy1, oac_val32 *xy2) = {
    oaci_dual_inner_prod_c, /* ARMv4 */
    oaci_dual_inner_prod_c, /* EDSP */
    oaci_dual_inner_prod_c, /* Media */
    oaci_dual_inner_prod_neon,/* NEON */
    oaci_dual_inner_prod_neon /* DOTPROD */
};
# endif

# if defined(FIXED_POINT)
#  if ((defined(OAC_ARM_MAY_HAVE_NEON) && !defined(OAC_ARM_PRESUME_NEON)) || \
    (defined(OAC_ARM_MAY_HAVE_MEDIA) && !defined(OAC_ARM_PRESUME_MEDIA)) || \
    (defined(OAC_ARM_MAY_HAVE_EDSP) && !defined(OAC_ARM_PRESUME_EDSP)))
oac_val32 (*const OACI_CELT_PITCH_XCORR_IMPL[OAC_ARCHMASK + 1])(const oac_val16 *,
    const oac_val16 *, oac_val32 *, int, int, int) = {
    oaci_celt_pitch_xcorr_c,             /* ARMv4 */
    MAY_HAVE_EDSP(oaci_celt_pitch_xcorr), /* EDSP */
    MAY_HAVE_MEDIA(oaci_celt_pitch_xcorr), /* Media */
    MAY_HAVE_NEON(oaci_celt_pitch_xcorr), /* NEON */
    MAY_HAVE_NEON(oaci_celt_pitch_xcorr) /* DOTPROD */
};

#  endif
# else /* !FIXED_POINT */
#  if defined(OAC_ARM_MAY_HAVE_NEON_INTR) && !defined(OAC_ARM_PRESUME_NEON_INTR)
void (*const OACI_CELT_PITCH_XCORR_IMPL[OAC_ARCHMASK + 1])(const oac_val16 *,
    const oac_val16 *, oac_val32 *, int, int, int) = {
    oaci_celt_pitch_xcorr_c,            /* ARMv4 */
    oaci_celt_pitch_xcorr_c,            /* EDSP */
    oaci_celt_pitch_xcorr_c,            /* Media */
    oaci_celt_pitch_xcorr_float_neon,   /* Neon */
    oaci_celt_pitch_xcorr_float_neon    /* DOTPROD */
};
#  endif
# endif /* FIXED_POINT */

# if defined(FIXED_POINT) && defined(OAC_HAVE_RTCD) && \
    defined(OAC_ARM_MAY_HAVE_NEON_INTR) && !defined(OAC_ARM_PRESUME_NEON_INTR)

void (*const OACI_XCORR_KERNEL_IMPL[OAC_ARCHMASK + 1])(
         const oac_val16 *x,
         const oac_val16 *y,
         oac_val32 sum[4],
         int len
    ) = {
    oaci_xcorr_kernel_c,              /* ARMv4 */
    oaci_xcorr_kernel_c,              /* EDSP */
    oaci_xcorr_kernel_c,              /* Media */
    oaci_xcorr_kernel_neon_fixed,     /* Neon */
    oaci_xcorr_kernel_neon_fixed      /* DOTPROD */
};

# endif

# if defined(OAC_ARM_MAY_HAVE_NEON_INTR)
#  if defined(HAVE_ARM_NE10)
#   if defined(CUSTOM_MODES)
int (*const OAC_FFT_ALLOC_ARCH_IMPL[OAC_ARCHMASK + 1])(kiss_fft_state *st) = {
    oac_fft_alloc_arch_c,       /* ARMv4 */
    oac_fft_alloc_arch_c,       /* EDSP */
    oac_fft_alloc_arch_c,       /* Media */
    oac_fft_alloc_arm_neon,     /* Neon with NE10 library support */
    oac_fft_alloc_arm_neon      /* DOTPROD with NE10 library support */
};

void (*const OAC_FFT_FREE_ARCH_IMPL[OAC_ARCHMASK + 1])(kiss_fft_state *st) = {
    oac_fft_free_arch_c,        /* ARMv4 */
    oac_fft_free_arch_c,        /* EDSP */
    oac_fft_free_arch_c,        /* Media */
    oac_fft_free_arm_neon,      /* Neon with NE10 */
    oac_fft_free_arm_neon       /* DOTPROD with NE10 */
};
#   endif /* CUSTOM_MODES */

void (*const OAC_FFT[OAC_ARCHMASK + 1])(const kiss_fft_state *cfg,
                                        const kiss_fft_cpx *fin,
                                        kiss_fft_cpx *fout) = {
    oac_fft_c,                  /* ARMv4 */
    oac_fft_c,                  /* EDSP */
    oac_fft_c,                  /* Media */
    oac_fft_neon,               /* Neon with NE10 */
    oac_fft_neon                /* DOTPROD with NE10 */
};

void (*const OAC_IFFT[OAC_ARCHMASK + 1])(const kiss_fft_state *cfg,
                                         const kiss_fft_cpx *fin,
                                         kiss_fft_cpx *fout) = {
    oac_ifft_c,                  /* ARMv4 */
    oac_ifft_c,                  /* EDSP */
    oac_ifft_c,                  /* Media */
    oac_ifft_neon,               /* Neon with NE10 */
    oac_ifft_neon                /* DOTPROD with NE10 */
};

void (*const OACI_CLT_MDCT_FORWARD_IMPL[OAC_ARCHMASK + 1])(const mdct_lookup *l,
                                                     kiss_fft_scalar *in,
                                                     kiss_fft_scalar * OAC_RESTRICT out,
                                                     const oac_val16 *window,
                                                     int overlap, int shift,
                                                     int stride, kiss_fft_scalar *mem, int arch) = {
    oaci_clt_mdct_forward_c,          /* ARMv4 */
    oaci_clt_mdct_forward_c,          /* EDSP */
    oaci_clt_mdct_forward_c,          /* Media */
    oaci_clt_mdct_forward_neon,       /* Neon with NE10 */
    oaci_clt_mdct_forward_neon        /* DOTPROD with NE10 */
};

void (*const OACI_CLT_MDCT_BACKWARD_IMPL[OAC_ARCHMASK + 1])(const mdct_lookup *l,
                                                      kiss_fft_scalar *in,
                                                      kiss_fft_scalar * OAC_RESTRICT out,
                                                      const oac_val16 *window,
                                                      int overlap, int shift,
                                                      int stride, int arch) = {
    oaci_clt_mdct_backward_c,          /* ARMv4 */
    oaci_clt_mdct_backward_c,          /* EDSP */
    oaci_clt_mdct_backward_c,          /* Media */
    oaci_clt_mdct_backward_neon,       /* Neon with NE10 */
    oaci_clt_mdct_backward_neon        /* DOTPROD with NE10 */
};

#  endif /* HAVE_ARM_NE10 */
# endif /* OAC_ARM_MAY_HAVE_NEON_INTR */

#endif /* OAC_HAVE_RTCD */

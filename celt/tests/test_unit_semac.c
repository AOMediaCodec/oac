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
    OR SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ENABLE_SEMAC
#include <stdio.h>
#include <math.h>
#include "celt.h"
#include "semac.h"

static int ret = 0;

static void test_allocation(void) {
    /* Excluded edges, equal energy, amplitude scaling, signed half ties,
       and a fractional mean. Inputs are on the shared Q15 grid. */
    static const oac_int32 logs[24] = {
         983040, 0,  32768,  32, 42, -983040,
         983040, 0,      0, -32,  0, -983040,
         983040, 0, -32768,   0,  0, -983040,
         983040, 0,      0,   0,  0, -983040
    };
    static const double centered[24] = {
        0, 0,  32768,  32,  31.5, 0,
        0, 0,      0, -32, -10.5, 0,
        0, 0, -32768,   0, -10.5, 0,
        0, 0,      0,   0, -10.5, 0
    };
    celt_glog energies[24];
    oac_int32 offsets[24];
    int i;
    for (i = 0; i < 24; i++) {
#ifdef FIXED_POINT
        energies[i] = logs[i] * (1 << (DB_SHIFT - 15));
#else
        energies[i] = logs[i] * (1.f/32768);
#endif
        offsets[i] = 42;
    }
    oaci_semac_compute_bit_weights_from_tc(energies, 4, 6, 1, 5, offsets);
    for (i = 0; i < 24; i++) {
        /* Known mean-centered values give an oracle without a second allocator. */
        double scaled = centered[i] * SEMAC_ALLOC_LAMBDA_Q15
            * ((1 << BITRES)*64) / 1073741824.;
        oac_int32 expected = (oac_int32)floor(fabs(scaled) + .5);
        if (scaled < 0) expected = -expected;
        if (offsets[i] != expected) {
            fprintf(stderr, "SEMAC allocation mismatch at index %d: %d != %d\n",
                i, offsets[i], expected);
            ret = 1;
        }
    }
}
#endif

int main(void) {
#ifdef ENABLE_SEMAC
    test_allocation();
    return ret;
#else
    return 0;
#endif
}

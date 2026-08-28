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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "arch.h"
#include "os_support.h"
#include "olac.h"
#include "entcode.h"

/* printf("%d, ", round(2^31*(1-w(1:60))./w(120:-1:61)));printf("\n"); */
oac_int32 lift_p[] = {
    2147339155, 2146183711, 2143875477, 2140419745, 2135824409,
    2130099919, 2123259212, 2115317633, 2106292838, 2096204694,
    2085075150, 2072928117, 2059789324, 2045686176, 2030647603,
    2014703902, 1997886584, 1980228209, 1961762232, 1942522843,
    1922544813, 1901863342, 1880513915, 1858532164, 1835953729,
    1812814139, 1789148691, 1764992343, 1740379609, 1715344476,
    1689920312, 1664139801, 1638034874, 1611636655, 1584975414,
    1558080527, 1530980449, 1503702685, 1476273780, 1448719303,
    1421063849, 1393331038, 1365543523, 1337723002, 1309890236,
    1282065067, 1254266439, 1226512432, 1198820281, 1171206416,
    1143686487, 1116275403, 1088987363, 1061835895, 1034833892,
    1007993647, 981326888, 954844819, 928558152, 902477144
};

/* printf("%d, ", round(2^31*-w(120:-1:61)));printf("\n"); */
oac_int32 lift_q[] = {
-2147483643, -2147483254, -2147480612, -2147471992, -2147451825,
-2147412716, -2147345468, -2147239114, -2147080958, -2146856619,
-2146550078, -2146143742, -2145618502, -2144953808, -2144127741,
-2143117098, -2141897478, -2140443381, -2138728301, -2136724838,
-2134404805, -2131739344, -2128699050, -2125254093, -2121374347,
-2117029527, -2112189321, -2106823532, -2100902218, -2094395836,
-2087275385, -2079512553, -2071079861, -2061950807, -2052100006,
-2041503336, -2030138067, -2017983004, -2005018607, -1991227122,
-1976592692, -1961101475, -1944741741, -1927503972, -1909380946,
-1890367817, -1870462177, -1849664120, -1827976283, -1805403880,
-1781954730, -1757639263, -1732470525, -1706464158, -1679638383,
-1652013957, -1623614129, -1594464578, -1564593343, -1534030740
};

#define MAX_PACKET_SIZE 50000
#define CHANNELS 2

oac_int8 rc_center[PRED_ORDER] = {
    -64, 62, -32, 9, -19, 10, -11, 8, -13, 8, -10, 9, -7, 8, -5, 5, -3, 4, 0, 4, 1, 2, 1, 2, 1, 1, 0, 2, 0, 1, 0, 0, -1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0
};

oac_int8 rc_scale[PRED_ORDER] = {
    24, 60, 53, 61, 39, 33, 35, 28, 29, 27, 31, 24, 33, 25, 32, 24, 25, 23, 25, 26, 25, 23, 22, 20, 19, 19, 18, 17, 16, 16, 14, 15, 14, 14, 14, 13, 14, 14, 13, 14, 14, 13, 13, 13, 12, 12, 11, 12, 12, 12, 11, 11, 11, 11, 10, 10, 11, 11, 12, 11, 11, 10, 11
};

void olac_aks_from_rc(oac_int32 *aks, const oac_int32 *rc, int order) {
    int n, k;
    oac_int32 ak[PRED_ORDER];
    for( n = 0; n < order; n++ ) {
        oac_int32 tmp1, tmp2;
        /* Update the AR coefficients */
        for( k = 0; k < (n + 1) >> 1; k++ ) {
            tmp1 = ak[ k ];
            tmp2 = ak[ n - k - 1 ];
            ak[ k ]         = tmp1 + OLAC_PSHR64(rc[n] * (oac_int64)tmp2, RC_SHIFT);
            ak[ n - k - 1 ] = tmp2 + OLAC_PSHR64(rc[n] * (oac_int64)tmp1, RC_SHIFT);
        }
#if OLAC_COEF_SHIFT > RC_SHIFT
        ak[ n ] = OLAC_SHL32(rc[n], OLAC_COEF_SHIFT-RC_SHIFT);
#else
        ak[ n ] = OLAC_SHR32(rc[n], RC_SHIFT-OLAC_COEF_SHIFT);
#endif
        for (k=0;k<=n;k++) *aks++ = -ak[ k ];
    }
}

static oac_int32 olac_sqrt(oac_int32 x)
{
   int k;
   oac_int16 n;
   oac_int32 rt;
   /* These coeffs are optimized in fixed-point to minimize both RMS and max error
      of sqrt(x) over .25<x<1 without exceeding 32767.
      The RMS error is 3.4e-5 and the max is 8.2e-5. */
   static const oac_int16 C[6] = {23170, 11574, -2901, 1592, -1002, 336};
   if (x>=1073733632)
      return 32767;
   k = ((EC_ILOG(x)-1)>>1)-7;
   x = OLAC_PSHR32(x, 2*k);
   n = x-32768;
   rt = C[0] + OLAC_MUL16_16_Q15(n, C[1] + OLAC_MUL16_16_Q15(n, C[2] +
              OLAC_MUL16_16_Q15(n, C[3] + OLAC_MUL16_16_Q15(n, C[4] + OLAC_MUL16_16_Q15(n, (C[5]))))));
   rt = OLAC_PSHR32(rt,7-k);
   return rt;
}


oac_int16 rc_factor(oac_int32 rc) {
    oac_int32 x2;
    celt_assert(2*RC_SHIFT-30 > 0);
    x2 = 1073741824 - OLAC_SHR64(rc*(oac_int64)rc, 2*RC_SHIFT-30);
    return olac_sqrt(IMAX(1<<24, x2));
}

#ifdef TEST_OLAC
int main() {
    int i;
    int packet_len;
    OlacEncoder enc;
    OlacDecoder dec;
    oac_int64 samples=0;
    oac_int64 compressed=0;
    unsigned char packet[MAX_PACKET_SIZE];
    oac_int32 pcm[FRAME_SIZE*CHANNELS];
    oac_int32 pcm_dec[FRAME_SIZE*CHANNELS];
    oac_int32 last[IMAX(1, OVERLAP_SIZE*CHANNELS)]={0};
    olac_encoder_init(&enc, CHANNELS, 48000);
    olac_decoder_init(&dec, CHANNELS, 48000);
    while (1) {
        short tmp[FRAME_SIZE*CHANNELS];
        if (fread(tmp, sizeof(tmp), 1, stdin) != 1) break;
        for (i=0;i<FRAME_SIZE*CHANNELS;i++) pcm[i] = OLAC_SHL32(tmp[i], 8);
        packet_len = olac_encode(&enc, pcm, FRAME_SIZE, packet, MAX_PACKET_SIZE);
        compressed += packet_len;
        samples += CHANNELS*FRAME_SIZE;
        olac_decode(&dec, packet, packet_len, pcm_dec, FRAME_SIZE);
        if (memcmp(last, pcm_dec, sizeof(oac_int32)*CHANNELS*OVERLAP_SIZE)) {
            fprintf(stderr, "decode failed1\n");
            exit(1);
        }
        if (memcmp(pcm, &pcm_dec[CHANNELS*OVERLAP_SIZE], sizeof(oac_int32)*(CHANNELS*(FRAME_SIZE-OVERLAP_SIZE)))) {
            fprintf(stderr, "decode failed2\n");
            exit(1);
        }
        OAC_COPY(last, &pcm[CHANNELS*(FRAME_SIZE-OVERLAP_SIZE)], CHANNELS*OVERLAP_SIZE);
    }
    printf("%f\n", 8*compressed/(double)samples/16.);
    return 0;
}
#endif

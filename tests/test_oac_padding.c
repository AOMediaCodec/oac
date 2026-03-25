/* Copyright (c) 2012 Xiph.Org Foundation
   Written by Jüri Aedla and Ralph Giles */
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

/* Check for overflow in reading the padding length.
 * http://lists.xiph.org/pipermail/oac/2012-November/001834.html
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oac.h"
#include "test_oac_common.h"

#define PACKETSIZE 16909318
#define CHANNELS 2
#define FRAMESIZE 5760

void test_overflow(void) {
    OacDecoder *decoder;
    int result;
    int error;

    unsigned char *in = (unsigned char *)malloc(PACKETSIZE);
    oac_int16 *out = (oac_int16 *)malloc(FRAMESIZE*CHANNELS*sizeof(*out));

    fprintf(stderr, "  Checking for padding overflow... ");
    if (!in || !out) {
        fprintf(stderr, "FAIL (out of memory)\n");
        test_failed();
    }
    in[0] = 0xff;
    in[1] = 0x41;
    memset(in + 2, 0xff, PACKETSIZE - 3);
    in[PACKETSIZE - 1] = 0x0b;

    decoder = oac_decoder_create(48000, CHANNELS, OAC_FORMAT_STANDARD, &error);
    result = oac_decode(decoder, in, PACKETSIZE, out, FRAMESIZE, 0);
    oac_decoder_destroy(decoder);

    free(in);
    free(out);

    if (result != OAC_INVALID_PACKET) {
        fprintf(stderr, "FAIL!\n");
        test_failed();
    }

    fprintf(stderr, "OK.\n");
}

int main(void) {
    const char *oversion;

    iseed = 0;
    oversion = oac_get_version_string();
    if (!oversion) test_failed();
    fprintf(stderr, "Testing %s padding.\n", oversion);

    test_overflow();

    fprintf(stderr, "All padding tests passed.\n");

    return 0;
}

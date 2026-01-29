/* Test oac_demo encode-only (-e) followed by decode-to-WAV
   It creates a short synthetic PCM buffer, encodes it with
   `oac_demo -e` to produce a bitstream, then decodes the bitstream
   using `oac_demo -d -wav` and checks the resulting WAV header and size.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

int run(const char *cmd) {
    int r = system(cmd);
    return WEXITSTATUS(r);
}

int check_wav(const char *filename, int sample_rate, int channels, int bits) {
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen"); return 1; }
    unsigned char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return 1; }
    if (memcmp(hdr, "RIFF", 4) != 0) { fclose(f); return 1; }
    if (memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return 1; }
    uint32_t sr = hdr[24] | (hdr[25]<<8) | (hdr[26]<<16) | (hdr[27]<<24);
    uint16_t ch = hdr[22] | (hdr[23]<<8);
    uint16_t bps = hdr[34] | (hdr[35]<<8);
    if (sr != (uint32_t)sample_rate || ch != (uint16_t)channels || bps != (uint16_t)bits) {
        fclose(f); return 1; }
    uint32_t data_size = hdr[40] | (hdr[41]<<8) | (hdr[42]<<16) | (hdr[43]<<24);
    /* Verify file length matches header */
    struct stat st;
    if (stat(filename, &st) != 0) { fclose(f); return 1; }
    if ((uint32_t)(st.st_size - 44) != data_size) { fclose(f); return 1; }
    fclose(f);
    return 0;
}

int main(void) {
    const char *raw = "test_enc_raw.pcm";
    const char *bit = "test_enc.oac";
    const char *outw = "test_enc_out.wav";
    int sample_rate = 8000;
    int channels = 1;
    int bits = 16;

    /* Create a short 1-second 8000 Hz mono 16-bit PCM with silence */
    FILE *f = fopen(raw, "wb");
    if (!f) return 1;
    for (int i = 0; i < sample_rate; i++) {
        short s = 0; /* silence */
        fwrite(&s, sizeof(s), 1, f);
    }
    fclose(f);

    /* Encode with oac_demo -e (encode-only) */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./oac_demo -e audio %d %d 64000 %s %s", sample_rate, channels, raw, bit);
    if (run(cmd) != 0) { unlink(raw); return 1; }

    /* Decode with oac_demo into WAV */
    snprintf(cmd, sizeof(cmd), "./oac_demo -d %d %d -wav %s %s", sample_rate, channels, bit, outw);
    if (run(cmd) != 0) { unlink(raw); unlink(bit); return 1; }

    /* Check WAV header */
    int rc = check_wav(outw, sample_rate, channels, bits);

    unlink(raw);
    unlink(bit);
    unlink(outw);
    return rc;
}

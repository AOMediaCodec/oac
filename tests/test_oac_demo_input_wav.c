/* Test oac_demo encoding from WAV input
   Creates a small WAV file, runs oac_demo encoder with -wav_in, then
   decodes the resulting bitstream to WAV and validates the WAV header.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

static int run(const char *cmd) {
    int r = system(cmd);
    return WEXITSTATUS(r);
}

static int write_wav(const char *fn, int sample_rate, int channels, int bits, int samples) {
    FILE *f = fopen(fn, "wb");
    if (!f) return -1;
    uint32_t byte_rate = sample_rate * channels * (bits/8);
    uint16_t block_align = channels * (bits/8);
    uint32_t data_sz = samples * channels * (bits/8);
    unsigned char hdr[44] = {0};
    memcpy(hdr + 0, "RIFF", 4);
    uint32_t chunk_size = 36 + data_sz;
    hdr[4] = chunk_size & 0xFF; hdr[5] = (chunk_size>>8)&0xFF; hdr[6] = (chunk_size>>16)&0xFF; hdr[7] = (chunk_size>>24)&0xFF;
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    hdr[16] = 16; /* fmt chunk size */
    hdr[20] = 1; /* PCM */
    hdr[22] = channels & 0xFF; hdr[23] = (channels>>8)&0xFF;
    hdr[24] = sample_rate & 0xFF; hdr[25] = (sample_rate>>8)&0xFF; hdr[26] = (sample_rate>>16)&0xFF; hdr[27] = (sample_rate>>24)&0xFF;
    hdr[28] = byte_rate & 0xFF; hdr[29] = (byte_rate>>8)&0xFF; hdr[30] = (byte_rate>>16)&0xFF; hdr[31] = (byte_rate>>24)&0xFF;
    hdr[32] = block_align & 0xFF; hdr[33] = (block_align>>8)&0xFF;
    hdr[34] = bits & 0xFF; hdr[35] = (bits>>8)&0xFF;
    memcpy(hdr + 36, "data", 4);
    hdr[40] = data_sz & 0xFF; hdr[41] = (data_sz>>8)&0xFF; hdr[42] = (data_sz>>16)&0xFF; hdr[43] = (data_sz>>24)&0xFF;
    if (fwrite(hdr, 1, 44, f) != 44) { fclose(f); return -1; }
    /* write silence */
    for (int i = 0; i < samples * channels; i++) {
        int16_t s = 0;
        fwrite(&s, sizeof(s), 1, f);
    }
    fclose(f);
    return 0;
}

static int check_wav(const char *filename, int sample_rate, int channels, int bits) {
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
        fclose(f); return 1;
    }
    uint32_t data_size = hdr[40] | (hdr[41]<<8) | (hdr[42]<<16) | (hdr[43]<<24);
    struct stat st;
    if (stat(filename, &st) != 0) { fclose(f); return 1; }
    if ((uint32_t)(st.st_size - 44) != data_size) { fclose(f); return 1; }
    fclose(f);
    return 0;
}

int main(void) {
    const char *wav_in = "t_in.wav";
    const char *bit = "t_in.oac";
    const char *wav_out = "t_out.wav";
    int sr = 8000;
    int ch = 1;
    int bits = 16;
    if (write_wav(wav_in, sr, ch, bits, sr) != 0) return 1;
    char cmd[512];
    /* Encode from WAV input (use -wav_in) */
    snprintf(cmd, sizeof(cmd), "./oac_demo audio %d %d 64000 -wav_in %s %s", sr, ch, wav_in, bit);
    if (run(cmd) != 0) { unlink(wav_in); return 1; }
    /* Decode to WAV */
    snprintf(cmd, sizeof(cmd), "./oac_demo -d %d %d -wav %s %s", sr, ch, bit, wav_out);
    if (run(cmd) != 0) { unlink(wav_in); unlink(bit); return 1; }
    int rc = check_wav(wav_out, sr, ch, bits);
    unlink(wav_in);
    unlink(bit);
    unlink(wav_out);
    return rc;
}

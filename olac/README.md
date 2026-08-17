# OLAC (Overlapped Lossless Audio Codec)

OLAC is an experimental standalone lossless audio codec operating at 48 kHz. It is designed for low-latency compression that can blend in efficiently with MDCT-based lossless codecs. Some of its features are:
- Time-Domain Alias Cancellation (TDAC) with integer lifting steps.
- Pre-emphasis support to match CELT.
- Forward adaptive prediction filtering with progressive order.
- Golomb-Rice residual entropy coding.
- Inter-channel prediction (stereo mode).
- Supported frame sizes: 120, 240, 480, and 960 samples (2.5, 5, 10, and 20 ms).

---

## 1. Building `olac_demo`

The `olac_demo` tool allows testing OLAC independently without the rest of OAC or `liboac`.

### Autotools
```bash
./autogen.sh
./configure
make olac_demo
```
The binary will be generated in the repository root (`./olac_demo`).

### CMake
```bash
cmake -B build -DOAC_BUILD_PROGRAMS=ON
cmake --build build --target olac_demo
```

### Meson
```bash
meson setup build -Dextra-programs=enabled
ninja -C build olac_demo
```

---

## 2. Input / Output Audio Format

`olac_demo` operates on headerless raw PCM at **48 kHz**:
- **Channels**: Mono (`1`) or Stereo (`2`).
- **Bit Depth**:
  - `-16`: 16-bit signed integer, little-endian (`S16LE`, 2 bytes/sample).
  - `-24`: 24-bit signed integer, packed little-endian (`S24LE`, 3 bytes/sample).
- **Frame Sizes**: `120`, `240`, `480`, or `960` samples (`-framesize`, default `960`).

---

## 3. Usage & Modes

```
Usage:
  olac_demo <channels (1/2)> [options] <input.pcm> <output.pcm>             # Loopback
  olac_demo -e <channels (1/2)> [options] <input.pcm> <output.bit>          # Encode only
  olac_demo -d [options] <input.bit> <output.pcm>                           # Decode only
```

Both `<input>` and `<output>` accept `-` for `stdin` / `stdout`.

### Options
- `-framesize <120|240|480|960>`: Frame size in samples (encoder only; default: `960`).
- `-16`: 16-bit PCM (default for encoder).
- `-24`: 24-bit PCM.
  - On decode (`-d`), `-16` or `-24` forces conversion to the requested output bit depth; if omitted, the decoder outputs the bit depth encoded in the bitstream.

---

## 4. Examples

### Loopback Mode (Encode + Decode with Bit-Exact Verification)
Runs encoder and decoder in a single pass. Automatically trims the 120-sample TDAC delay and trailing padding so the output length matches the input sample-for-sample, then verifies bit-exact identity:

```bash
# 16-bit stereo (default 960-sample / 20 ms frames)
./olac_demo 2 input_16k.pcm output_16k.pcm

# 24-bit mono with 240-sample (5 ms) frames
./olac_demo 1 -24 -framesize 240 input_24k.pcm output_24k.pcm
```

Upon completion, it reports bitrate, bits per sample (per channel), raw compression ratio, and verification result:
```
Bitrate: 421.350 kb/s (4.389 bits/sample), compression ratio: 54.9%
Verification: PASS (bit-exact)
```

### Separate Encode and Decode
```bash
# Encode 24-bit stereo at 10 ms frames (480 samples)
./olac_demo -e 2 -24 -framesize 480 music_48k_24b.pcm music.olac

# Decode (channels, frame size, and bit depth are auto-detected from the stream)
./olac_demo -d music.olac music_decoded.pcm

# Decode and downconvert to 16-bit PCM
./olac_demo -d -16 music.olac music_decoded_16b.pcm
```

> **Note on EOF latency in separate decode**:
> OLAC has a 120-sample lookahead/overlap. When encoding, if the final audio chunk extends into the frame's right overlap (or when using `framesize == 120`), the encoder emits a trailing zero-padded frame to flush the overlap state. Consequently, decoding with `-d` may produce up to `frame_size - 120` trailing zeros. Loopback mode automatically trims these to match the exact input length.

### Streaming with Pipes
```bash
# Convert WAV to raw PCM, run through olac_demo loopback, and write back to WAV
sox input.wav -t raw -r 48000 -c 2 -b 16 -e signed-integer - | \
  ./olac_demo 2 - - | \
  sox -t raw -r 48000 -c 2 -b 16 -e signed-integer - output.wav

# Pipe encoder directly to decoder
cat input.pcm | ./olac_demo -e 2 - - | ./olac_demo -d - - > output.pcm
```

---

## 5. Bitstream Framing Format

The `.bit` format produced by `olac_demo -e` consists of consecutive frame packets:
- **`len` (4 bytes, big-endian)**: Payload size in bytes (excluding the 9-byte header).
- **`rng` (4 bytes, big-endian)**: Range coder state for decoder synchronization checks.
- **`mode` (1 byte)**:
  - Bit 0: Channel count (`0` = mono, `1` = stereo)
  - Bits 1–2: Frame size (`00` = 120, `01` = 240, `10` = 480, `11` = 960)
  - Bit 3: Bit depth (`0` = 16-bit, `1` = 24-bit)
  - Bits 4–7: Reserved (`0`)
- **`payload` (`len` bytes)**: Raw entropy-coded OLAC frame.

Because the configuration byte is repeated per frame, concatenated `.bit` files can be decoded seamlessly by `olac_demo -d`.

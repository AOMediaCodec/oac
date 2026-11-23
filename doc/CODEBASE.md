# OAC Codebase Tour

This document describes how the liboac source tree is organized and the
assumptions the code relies on. It is written for anyone, human or agent,
who needs to read, change or review the code. It describes the code as it is
today; a change that makes any of it untrue should update this file in the
same patch. See [REVIEW.md](REVIEW.md) for what to consider when evaluating
a change.

## Relation to Opus

liboac started from libopus and was then renamed and reformatted. Most of
what is known about the Opus codebase still applies, with these differences:

- Identifiers were renamed from `opus_`/`OPUS_` to `oac_`/`OAC_`.
- Internal (non-public) symbols carry an `oaci_` prefix. It was introduced
  to avoid symbol clashes with libopus when both are linked into the same
  program.
- The whole tree was reformatted to a single coding style (see
  [Coding style](#coding-style)).
- The bitstream is not Opus-compatible and is not frozen. The framing (ToC
  bytes, padding, frame size coding) has already changed, as have several
  CELT entropy coding tables and the band splitting.
- The extra precision that Opus enabled with `ENABLE_RES24` is always on.
- `ENABLE_QEXT` no longer enables a quality extension layer. It only adds
  96 kHz support.
- Pre-RFC 8251 compatibility code was removed.

## Organization

### Directory structure

| Directory  | Contents |
|------------|----------|
| `celt/`    | Entropy coder, basic operators and helper functions, and the CELT (MDCT) codec. Has no dependency on the other directories; on its own it is the "custom" codec. |
| `silk/`    | SILK (linear prediction) codec. Depends on `celt/` for the entropy coder and a few operators. |
| `dnn/`     | Neural network code: DRED, deep PLC, OSCE (LACE/NoLACE), bandwidth extension, pitch estimator. `dnn/torch/` holds the training code. |
| `src/`     | Glue code that combines everything into the full codec: encoder, decoder, mode decisions, multistream, projection/ambisonics, packet extensions, repacketizer, signal analysis, and the demo tools. |
| `include/` | Public API headers. |
| `tests/`   | API-level tests, the decoder fuzzer target, and the random build test scripts. Unit tests live in `celt/tests/` and `silk/tests/`. |
| `doc/`     | Doxygen setup, `trivial_example.c`, and these documents. |

Packets can be SILK-only, hybrid (SILK for the low band, CELT above) or
CELT-only, as in Opus.

### Build system

The list of source files is kept in `*.mk` files at the top level
(`celt_sources.mk`, `celt_headers.mk`, `silk_sources.mk`, `oac_sources.mk`,
`lpcnet_sources.mk` for `dnn/`, ...). All build systems read these lists:

- Autotools (`configure.ac`, `Makefile.am`): the most complete, and the one
  the test scripts use.
- CMake (`CMakeLists.txt`, `cmake/`), see `cmake/README.md`.
- Meson (`meson.build`, `meson_options.txt`), see `meson/README.md`.
- Plain Makefile (`Makefile.unix`).

Not every option exists in every build system. For example, `--enable-qext`
is only available with Autotools.

`./autogen.sh` downloads the neural network model data before generating
`configure`. The data is a tarball named after its SHA-256 hash; the hash is
in `autogen.sh` and the download is done by `dnn/download_model.sh`. The
files it contains are listed in `tar_list.txt`, and `create_opus_data.sh`
builds a new tarball from that list. Large binary files are kept out of git
this way.

`scripts/local_build.py` tries a build with CMake, Meson and Autotools in
turn.

### Coding style

The tree was reformatted with uncrustify using `oac.cfg`: 4-space
indentation, no tabs, opening braces on the same line, lines up to 120
columns. New code should look like the code around it. Small deviations in
details such as operator spacing are fine when they help readability.
Trailing whitespace is rejected by CI.

### Naming

- Public API: `oac_` for functions and types, `OAC_` for macros.
- Internal symbols with external linkage: `oaci_`.
- Static functions do not need a prefix.
- Some modules keep their own prefix, for example `celt_`, `silk_`, `ec_`.
- Macros and types in a header that is included from another directory need
  a module prefix, so that they cannot clash with or silently replace a
  definition from another module. Unprefixed names are fine inside a `.c`
  file or in a header that is private to one directory.

## Assumptions

The code is C90:

- No `//` comments.
- No declarations after statements.
- No C99 integer types (`int32_t`, ...). Use `oac_int32` and friends.
- No single-precision math functions (`expf()`, `floorf()`, ...).
- Variable-length arrays are only used through the `VAR_ARRAYS` mechanism.
- Features of later standards can be used when they are optional and the
  code still compiles as C90.

The code must also compile as C++, so `void *` conversions need explicit
casts and C++ keywords cannot be used as identifiers.

No undefined behaviour, as defined by C90 or any later standard, is allowed.

The code relies on the following implementation-defined behaviour:

- Two's complement integers.
- `>>` on a negative value is an arithmetic shift, so `a>>b` is
  `floor(a/2^b)`.
- Converting an out-of-range value to a signed integer type reduces it
  modulo 2^N. Casting between unsigned and negative signed values is fine.
- Integer division of a negative value truncates towards zero.
- A 64-bit integer type is available.
- `int` can be as small as 16 bits. This is a goal and does not fully work
  today, but new code should not assume a 32-bit `int`.

Library code does not use `stdio` and does not print anything. Only the
demo tools, the tests and debug code that is disabled by default do.

## Stack allocation

Temporary arrays are allocated with the macros in `celt/stack_alloc.h`:

- `VARDECL(type, var)` declares the array with the other declarations.
- `ALLOC(var, size, type)` allocates it once the size is known.
- `SAVE_STACK` or `ALLOC_STACK` goes at the start of the function, after
  the declarations, and `RESTORE_STACK` before every `return`.

The same code then builds in three modes: C99 variable-length arrays
(`VAR_ARRAYS`), `alloca()` (`USE_ALLOCA`), or a separately allocated
pseudostack for targets with a small stack (`NONTHREADSAFE_PSEUDOSTACK`).

A plain fixed-size array on the stack is fine when it is small. Anything
above about 1 kB, or anything whose size scales with the frame size or the
number of channels, uses `ALLOC`, so that the pseudostack mode accounts for
it.

## Asserts

- `celt_assert(cond)` is for conditions that always hold unless there is a
  bug, and that are cheap to test. It is active when `ENABLE_HARDENING` is
  defined, which is the default, so it stays on in production.
- `celt_sig_assert(cond)` is for asserts that would be unsafe to keep
  enabled all the time: either the condition cannot be proven
  mathematically and the code can recover when it fails, or the test is too
  costly. It is only active with `ENABLE_ASSERTIONS`.
- `MUST_SUCCEED(call)` wraps an internal call, typically a CTL on a
  sub-codec, whose failure means that something has gone really wrong. It
  asserts when asserts are enabled and otherwise makes the function return
  `OAC_INTERNAL_ERROR`.

## Important macros

| Macro | Meaning |
|-------|---------|
| `FIXED_POINT` | Build the fixed-point implementation. |
| `FIXED_DEBUG` | Fixed-point operators check for overflows and count operations. |
| `DISABLE_FLOAT_API` | Remove all floating point from the library, including the float API. |
| `VAR_ARRAYS`, `USE_ALLOCA`, `NONTHREADSAFE_PSEUDOSTACK` | How variable-size arrays are allocated on the stack. Exactly one must be defined (see `celt/stack_alloc.h`). |
| `ENABLE_HARDENING` | Cheap asserts that are safe in production (`celt_assert()`). On by default. |
| `ENABLE_ASSERTIONS` | Also enables the more costly asserts on signal values (`celt_sig_assert()`). |
| `CUSTOM_MODES` | Support for non-standard sampling rates and frame sizes in CELT. |
| `ENABLE_OAC_CUSTOM_API` | Expose the custom API (`include/oac_custom.h`). |
| `ENABLE_QEXT` | 96 kHz support. |
| `ENABLE_DEEP_PLC`, `ENABLE_DRED`, `ENABLE_OSCE` | Neural network features from `dnn/`. |
| `SMALL_FOOTPRINT` | Trade speed for smaller code and tables. |
| `FLOAT_APPROX` | Faster, approximate float math functions. |
| `RESYNTH` | Make the CELT encoder resynthesize the decoded signal (debug). |
| `FUZZING` | Make the encoder take random decisions (debug). |
| `OAC_*_MAY_HAVE_*`, `OAC_*_PRESUME_*`, `OAC_HAVE_RTCD` | SIMD support, see below. |
| `OAC_CHECK_ASM` | Check SIMD code against the C code at run time. |

## Types

- Integer types: `oac_int8`, `oac_uint8`, `oac_int16`, ... `oac_int64`
  (`include/oac_types.h`). They are equivalent to the C99 types and exist for
  C90 compatibility.
- Arithmetic types: `oac_val16`, `oac_val32`, `oac_val64`. They map to
  `oac_int16`, `oac_int32`, `oac_int64` in fixed-point and all map to
  `float` otherwise.
- Signal types: `oac_res` (PCM at the API resolution), `celt_sig` (time and
  MDCT domain signals), `celt_norm` (normalized bands), `celt_ener` (band
  energies), `celt_glog` (log energies), `celt_coef` (filter and window
  coefficients). In fixed-point they currently all map to `oac_val32`.
  Using the right one says which scaling a value has and makes it possible
  to change the fixed-point representation later.

## Fixed-point

Most of the code is written once and uses basic operators that have a
fixed-point definition and a trivial float definition:

- `MULT16_16_Q15(a, b)` is `((a)*(b))>>15` in fixed-point and `(a)*(b)` in
  float.
- `SHR32(a, b)` is `(a)>>(b)` in fixed-point and `(a)` in float.

The float definitions are in `celt/arch.h`. The fixed-point definitions are
in `celt/fixed_generic.h`, or in `celt/fixed_debug.h` when `FIXED_DEBUG` is
defined. Architecture-specific variants can override them, for example
`celt/arm/fixed_armv5e.h`.

The operators with an `_ovflw` suffix (`ADD32_ovflw()`, `SUB32_ovflw()`,
...) wrap around instead of overflowing. They are only for cases where
there is no reasonable way to prevent the wrap-around and where it is
harmless, such as a decoder producing garbage from a garbage packet. They
are not a way to silence an overflow that should not happen.

Exceptions:

- SILK has separate implementations in `silk/fixed/` and `silk/float/`, with
  the common code in `silk/`.
- The signal analysis code (`src/analysis.c`, `src/mlp.c`) is float code. It
  is built in fixed-point builds too, unless `DISABLE_FLOAT_API` is defined.
- `dnn/` is float-only. `configure` refuses `--enable-fixed-point` together
  with `--enable-deep-plc`, `--enable-dred` or `--enable-osce`.

The fixed-point headers for TI C5x/C6x (`celt/fixed_c5x.h`,
`celt/fixed_c6x.h`) and the remaining MIPS headers (`celt/mips/`,
`silk/mips/`) are inherited from Opus and are not maintained.

## SIMD support

Optimized code lives in `arm/` and `x86/` subdirectories of `celt/`, `silk/`
and `dnn/`. Supported levels:

- ARM: ARMv4, ARMv5E (EDSP), ARMv6 (media), NEON, AArch64 NEON,
  NEON with dot product.
- x86: SSE, SSE2, SSE4.1, AVX2 (with FMA).

Support is decided both at compile time and at run time:

- `OAC_<ARCH>_PRESUME_<LEVEL>` means the target is known to have at least
  that level, so the code is called directly.
- `OAC_<ARCH>_MAY_HAVE_<LEVEL>` means the code for that level is built and
  may be used.
- When a level may be present but is not presumed, run-time CPU detection
  (RTCD, `OAC_HAVE_RTCD`) selects the implementation through the `arch`
  argument that is passed down to most functions.

Every optimized function has a C version with the same behaviour. With
`--enable-check-asm --enable-assertions`, the code inside
`#ifdef OAC_CHECK_ASM` runs both and asserts that the results match.

## Testing infrastructure

- `make check` runs the unit tests (`celt/tests/`, `silk/tests/`) and the
  API-level tests in `tests/` (`test_oac_api`, `test_oac_encode`,
  `test_oac_decode`, `test_oac_extensions`, `test_oac_padding`,
  `test_oac_projection`, plus `test_oac_custom` and `test_oac_dred` when
  those features are enabled). Unit test coverage is small. The higher-level
  tests include torture tests, but nothing in `make check` measures audio
  quality.
- `tests/oac_build_test.sh` builds a release tarball with many random
  configurations (`tests/random_config.sh`), sometimes with sanitizers or
  valgrind, and runs `make check` on each.
- There are no OAC conformance test vectors yet. `tests/run_vectors.sh`,
  and the step of `random_config.sh` that calls it, are leftovers from Opus
  and wait for new vectors. `random_config.sh` also still passes the removed
  `--disable-fixed-res24` option.
- `tests/oac_decode_fuzzer.c` is a libFuzzer target for the decoder. It has
  to be built by hand; there is no continuous fuzzing yet.
- CI (`.github/workflows/`) builds and tests float configurations with
  Autotools, CMake and `Makefile.unix`, cross-builds for Android, Windows
  and MIPS, and checks for trailing whitespace. It does not cover x86
  fixed-point, `-std=c90`, or sanitizers.
- Quality testing scripts are not part of this repository.

## Debug tools

- Standard tools all work: ASan, UBSan, valgrind. `celt/stack_alloc.h` has
  valgrind annotations for the pseudostack (`ENABLE_VALGRIND`).
- `--enable-fixed-point-debug` (`FIXED_DEBUG`) reports fixed-point
  overflows.
- `--enable-assertions` turns on all asserts.
- Encoder/decoder synchronization: `oac_demo` compares the encoder and
  decoder range coder state for each packet using `OAC_GET_FINAL_RANGE()`.
  A mismatch means the encoder and decoder disagree on what was coded.
- `RESYNTH` (set with `CFLAGS=-DRESYNTH`) makes the CELT encoder perform its
  own resynthesis. `oac_custom_demo` (built with `--enable-custom-modes`)
  then checks that the encoder's signal matches the decoder output.
- `--enable-fuzzing` (`FUZZING`) makes the encoder take random decisions,
  which exercises bitstream paths that normal encoding rarely reaches.
- `oac_compare` and `qext_compare` compare two decoded PCM files.

## Common build configurations

```sh
./autogen.sh

# Float (default)
./configure --enable-assertions
# Fixed-point
./configure --enable-assertions --enable-fixed-point
# Fixed-point with overflow checking
./configure --enable-assertions --enable-fixed-point --enable-fixed-point-debug
# No float at all
./configure --enable-assertions --enable-fixed-point --disable-float-api
# SIMD self-test
./configure --enable-assertions --enable-check-asm
# Neural network features (float only)
./configure --enable-assertions --enable-deep-plc --enable-dred --enable-osce
# C90 check
CFLAGS="-O2 -g -std=c90" ./configure --enable-assertions

make -j && make check
```

Run `make clean` when changing configuration.

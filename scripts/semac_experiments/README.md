# SEMAC transport factorial + CMF3 baseline

Reproduces the fixed-total-bitrate comparison of SEMAC against plain ambisonic order truncation
(channel mapping family 3, RFC 8486), scored with BINAQUAL.

Both arms code the same sources at the same total bitrates and are scored against the same uncoded
references at the study's reference order, so the only difference is the codec. SEMAC downmixes to a
smaller transport and restores directionality from a transmitted spatial energy envelope; CMF3 simply
drops the higher-order channels.

> **The CMF3 arm here is not upstream Opus.** It is `oac_ambisonics_compare mapping3`, this repo's
> own projection encoder, and its mapping matrices at every order are SEMAC's own SPFB beam designs
> rather than the upstream/RFC ones. Do not describe these numbers as "Opus". For a real upstream
> baseline see **Stock-Opus CMF3 tool** below.

## Run order

```sh
./run_full_experiment_factorial.sh     # 840 SEMAC items (rebuilds the codec 4x)
python full_experiment_eval.py         # -> $WORK/results.csv
./render_cmf3.sh                       # 210 CMF3 items, two arms (reuses the references above)
python score_cmf3.py                   # -> $WORK/results_cmf3_arms.csv
```

`run_full_experiment_factorial.sh` must run first: it produces the `ref/` renders that both
scorers compare against. Each script prints the next command on completion.

## Configuration

Each script has a `SITE CONFIGURATION` block at the top with plain constants — edit those for your
machine. The repo root is derived from the script's own location. `SEMAC_*` environment variables
override the main ones so a sweep can write to its own tree; `WORK` must agree across all four
scripts.

## Conditions

| axis | values |
| --- | --- |
| signals | BandDry, BandWet, Clapping, MachineAer, MovingPink, Orchestra, Scene (7) |
| transport order | tc4 … tc36, up to the input order (4 / 9 / 16 / 25 / 36 transport channels) |
| transport domain | shd, beam |
| spatial normalisation | norm, nonorm |
| allocation lambda | 1.0, 0.0 (`alloc_1`, `alloc_equal`) |
| total bitrate | 256k, 512k, 768k |

The CMF3 baseline is rendered as **two arms**, since the fork is not upstream Opus:

| arm | codec | matrices |
| --- | --- | --- |
| `semaccmf3` | this repo's projection encoder | SEMAC's own SPFB beam matrices |
| `opuscmf3` | clean upstream libopus v1.6.1 | upstream |

Both are scored into `results_cmf3_arms.csv`, which carries an `arm` column.

Input and output are always 3rd-order (16ch); only the transport varies. Spatial normalisation and
allocation lambda are compile-time, so the codec is rebuilt for each of the six combinations —
`semac.h` is edited in place and restored by an `EXIT` trap, including on error or Ctrl-C.

## Two caveats worth knowing before comparing against other studies

**This experiment holds the TOTAL bitrate fixed.** A smaller transport therefore gets more bits per
channel, which means "fewer channels scores better" partly reflects rate concentration and not only
the SEMAC mechanism. The separate per-channel-rate study exists to remove that confound; do not mix
conclusions between the two.

**Encoding is VBR.** The `-cbr` option postdates these results. The per-channel-rate study is CBR
throughout, so the two are not comparable on rate behaviour.

## External dependencies

Not vendored here — install or check out separately and point the `SITE CONFIGURATION` blocks at
them:

- **BINAQUAL** reference implementation (`binaqual.calculate_binaqual`) — `BINAQUAL_DIR` in `bq_score.py`
- **`render_ambi2bin.py`** ambisonics-to-binaural renderer, from the OAC-Meta repo — `RENDER`
- **A SOFA HRIR set** (results here used `HRIR_L2702.sofa`) — `SOFA`
- **3rd-order source signals** named `{signal}_AMBIX.wav` — `SRC`
- **ffmpeg** on `PATH`
- **Python** with numpy, scipy, soundfile and whatever `render_ambi2bin.py` needs — `PY`

The codec must be configured with SEMAC enabled and built before running:

```sh
./configure --enable-semac && make
```

## Stock-Opus CMF3 tool

`opus_cmf3_compare.c` plus `build_opus_cmf3.sh` provide a CMF3 round-trip against a **clean upstream
libopus** checkout, so the fork's CMF3 can be compared against the real thing. Upstream ships the
projection API but no CLI that exercises it (`opus_demo` is mono/stereo only), hence this tool. It
is a deliberate line-by-line port of `encode_decode_mapping3()` from
`src/oac_ambisonics_compare.c`, mirroring every encoder setting; keep the two in sync.

```sh
./build_opus_cmf3.sh    # builds libopus if needed, then the tool
./opus_cmf3_compare <sampling_rate> <channels> <bitrate> <in.pcm> <out.pcm> [-cbr]
```

Point `OPUS_SRC` in `build_opus_cmf3.sh` at your libopus checkout. Nothing is written into it beyond
its own build artefacts, deliberately — the checkout's value is being a verifiable clean upstream.

Verified against the fork at v1.6.1 with identical settings: stream layout, demixing gain and
achieved CBR byte count match exactly, confirming the port is faithful, while the coded output
differs. That residual is genuine fork-versus-upstream drift — the two now use different mapping
matrices at every order — not a configuration mismatch. This tool is used by the per-channel-rate
study, which lives outside this directory.

## Scoring notes

`bq_score.py` holds the single scoring core both scorers import. This matters: the SEMAC and CMF3
numbers are compared head to head, and the core was previously duplicated in each script, where an
edit to one copy would have silently invalidated the comparison.

Delay alignment is mandatory and is applied per file, estimated from the left-ear
cross-correlation peak. The codec delay differs by arm — about 120 samples for SEMAC versus 312 for
CMF3 — so a single global offset would misalign one of them and destroy its scores. Without any
alignment every coded score collapses to roughly 0.22 regardless of condition. The renders written
to disk are deliberately left unaligned; alignment is a scoring step, not a stored property, so
listening to those files against a reference will show the offset. The estimated lag per item is
recorded in the `lag` column of both CSVs.

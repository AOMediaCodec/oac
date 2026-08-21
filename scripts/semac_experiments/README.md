# SEMAC transport factorial + Opus CMF3 baseline

Reproduces the fixed-total-bitrate comparison of SEMAC against plain ambisonic order truncation
(Opus channel mapping family 3, RFC 8486), scored with BINAQUAL.

Both arms code the same sources at the same total bitrates and are scored against the same uncoded
3rd-order references, so the only difference is the codec. SEMAC downmixes to a smaller transport
and restores directionality from a transmitted spatial energy envelope; CMF3 simply drops the
higher-order channels.

## Run order

```sh
./run_full_experiment_factorial.sh     # 864 SEMAC items (rebuilds the codec 6x)
python full_experiment_eval.py         # -> $WORK/results.csv
./render_cmf3.sh                       # 72 CMF3 items (reuses the references above)
python score_cmf3.py                   # -> $WORK/results_cmf3.csv
```

`run_full_experiment_factorial.sh` must run first: it produces the `ref/` renders that both
scorers compare against. Each script prints the next command on completion.

## Configuration

There are no environment variables. Each script has a `SITE CONFIGURATION` block at the top with
plain constants — edit those for your machine. The repo root is derived from the script's own
location, so it does not need setting. `WORK` must be the same in all four scripts.

## Conditions

| axis | values |
| --- | --- |
| signals | BandDry, BandWet, Clapping, MachineAer, MovingPink, Orchestra, Scene, Eigenmike (8) |
| transport order | tc4, tc9, tc16 (4 / 9 / 16 transport channels) |
| transport domain | shd, beam |
| spatial normalisation | norm, nonorm |
| allocation lambda | 1.0, 0.5, 0.0 (`alloc_1`, `alloc_0p5`, `alloc_equal`) |
| total bitrate | 256k, 512k, 768k |

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

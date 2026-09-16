#!/bin/bash
# CMF3 baselines for the SEMAC transport factorial (RFC 8486 channel mapping family 3).
#
# CMF3 codes the first (N+1)^2 input channels directly with no energy restoration, so each order
# (4/9/16/25/36 channels) is plain ambisonic ORDER TRUNCATION. That is the honest baseline for
# SEMAC's reduced transport: same channel count, same total bitrate, no spatial envelope.
#
# TWO arms, because the fork is not upstream Opus:
#   semaccmf3  this repo's projection encoder. Its mapping matrices are SEMAC's own SPFB beam
#              matrices (see the header of src/mapping_matrix.c), so this is NOT Opus CMF3.
#   opuscmf3   a clean upstream libopus build, via scripts/semac_experiments/opus_cmf3_compare.
#              This is the real external baseline.
#
# 2 arms x 7 signals x REFERENCE_ORDER orders x 3 bitrates = 210 items at the default order 5, written
# alongside the SEMAC renders and scored against the same uncoded references.
#
# Run AFTER run_full_experiment_factorial.sh -- it reuses that script's ref/ renders.
# Encoding is VBR, matching the SEMAC arm it is compared against.
#
# semaccmf3 depends on how the fork is currently built: oac_ambisonics_compare is a libtool wrapper
# resolving .libs/liboac.so at run time. Rebuild after changing the mapping matrices.
#
# Layout:     $WORK/{ambix,binaural}/{semaccmf3,opuscmf3}/{256,320,512,768}kbits/
# Filename:   {sig}_Ambix{N}_{arm}_{tc4|tc9|tc16|tc25|tc36}_{256kb|320kb|512kb|768kb}[_bin]
set -e

# ============================= SITE CONFIGURATION ============================================
# Must match run_full_experiment_factorial.sh.
DATA=/data/users/$USER                          # devserver data root; edit if your layout differs
WORK=${SEMAC_WORK:-$DATA/scratch/oac_run/full_experiment}   # where renders and results go
SRC=${SEMAC_SRC:-$DATA/scratch/HOA/AMBIX}       # {signal}_AMBIX.wav sources, ACN/SN3D, 5th order
SOFA=$DATA/scratch/HRTFs/HRIR_L2702.sofa        # HRIR set for the binaural render
RENDER=$DATA/OAC-Meta/render_ambi2bin.py        # ambisonics -> binaural renderer
PY=$HOME/.conda/envs/test/bin/python            # interpreter with numpy/scipy/soundfile
REFERENCE_ORDER=${SEMAC_REFERENCE_ORDER:-5}     # SH5; must match run_full_experiment_factorial.sh
# =============================================================================================

# Unlike SEMAC, which restores the full order at its decoder, a CMF3 arm's output stays at its
# transport order, so its channel count varies per condition. See the --order note in render_one.

OAC=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)      # repo root

# Must match SIGNALS in run_full_experiment_factorial.sh. Eigenmike is excluded there because it is
# a 4th-order recording and cannot supply a 5th-order condition.
SIGNALS=${SEMAC_SIGNALS:-"BandDry BandWet Clapping MachineAer MovingPink Orchestra Scene"}
ARMS="semaccmf3 opuscmf3"        # fork (SEMAC matrices) and clean upstream libopus
OPUSCMF3=$OAC/scripts/semac_experiments/opus_cmf3_compare   # build with build_opus_cmf3.sh
# "<channels>:<tag>" per coded order, derived from REFERENCE_ORDER so the arm never codes more channels
# than the reference has. Orders 1..REFERENCE_ORDER -> 4:tc4 9:tc9 16:tc16 25:tc25 36:tc36.
ORDERS=""
for _n in $(seq 1 "$REFERENCE_ORDER"); do
  _c=$(( (_n + 1) * (_n + 1) ))
  ORDERS="$ORDERS $_c:tc$_c"
done
# "<bits per second>:<tag>". The tag goes in the FILENAME as-is; the folder is the tag + "its"
# (so tag 256kb -> folder 256kbits). Must match the SEMAC arm for the comparison to be fair.
BRATES=${SEMAC_BRATES:-"256000:256kb 320000:320kb 512000:512kb 768000:768kb"}
NJOBS=${SEMAC_NJOBS:-8}          # parallel per-item workers; raise to the core count if idle

TMP=/tmp/sem_cmf3
trap 'rm -rf "$TMP"' EXIT

# Pin each worker single-threaded so NJOBS workers map to NJOBS cores (no BLAS/FFT oversubscription).
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 NUMEXPR_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1

throttle(){ while [ "$(jobs -r | wc -l)" -ge "$NJOBS" ]; do wait -n; done; }

# One coded item: encode+decode the first <ch> channels, wrap as wav, then binaural render.
# No -cbr: the factorial is VBR throughout, matching the SEMAC arm.
render_one(){
  local sig=$1 ch=$2 tc=$3 bps=$4 tag=$5 arm=$6
  local base="${sig}_Ambix${REFERENCE_ORDER}_${arm}_${tc}_${tag}"
  local ambix="$WORK/ambix/$arm/${tag}its/$base.wav"
  if [ "$arm" = semaccmf3 ]; then
    "$OAC/oac_ambisonics_compare" mapping3 48000 "$ch" "$bps" \
       "$TMP/in_${sig}_${ch}.pcm" "$TMP/out_${base}.pcm" >/dev/null 2>&1
  else
    "$OPUSCMF3" 48000 "$ch" "$bps" \
       "$TMP/in_${sig}_${ch}.pcm" "$TMP/out_${base}.pcm" >/dev/null 2>&1
  fi
  ffmpeg -y -f s16le -ar 48000 -ac "$ch" -i "$TMP/out_${base}.pcm" -c:a pcm_s16le "$ambix" 2>/dev/null
  # --order pins the binaural decode to REFERENCE_ORDER (SH5). This arm's coded output has only $ch
  # channels, so without it the renderer would derive a lower-order magLS decoder per coded order and
  # mix a decoder difference into the codec comparison against the reference. The renderer zero-pads
  # the absent higher degrees, which is exact -- they are genuinely zero.
  "$PY" "$RENDER" "$ambix" "$WORK/binaural/$arm/${tag}its/${base}_bin.wav" \
     --order "$REFERENCE_ORDER" --hrirs_sofa_file "$SOFA" --gain_db -12 >/dev/null 2>&1
  rm -f "$TMP/out_${base}.pcm"
}

cd "$OAC"
[ -x ./oac_ambisonics_compare ] || { echo "oac_ambisonics_compare not built in $OAC -- run ./configure && make"; exit 1; }
[ -x "$OPUSCMF3" ] || { echo "missing $OPUSCMF3 -- build it with scripts/semac_experiments/build_opus_cmf3.sh"; exit 1; }
[ -d "$WORK/binaural/ref" ] || { echo "no references in $WORK/binaural/ref -- run run_full_experiment_factorial.sh first"; exit 1; }

mkdir -p "$TMP"
for dm in ambix binaural; do for arm in $ARMS; do for rate in $BRATES; do
  mkdir -p "$WORK/$dm/$arm/${rate##*:}its"
done; done; done

# Per-order input PCMs: the first <ch> channels of the source, first 5 s. Built once.
for sig in $SIGNALS; do for order in $ORDERS; do ch=${order%%:*}
  [ -f "$TMP/in_${sig}_${ch}.pcm" ] || ffmpeg -y -i "$SRC/${sig}_AMBIX.wav" -ss 0 -to 5 \
     -af "channelmap=map=$(seq -s'|' 0 $((ch-1)))" -c:a pcm_s16le -f s16le "$TMP/in_${sig}_${ch}.pcm" 2>/dev/null
done; done

for arm in $ARMS; do
  echo "rendering $(( $(echo $SIGNALS | wc -w) * $(echo $ORDERS | wc -w) * $(echo $BRATES | wc -w) )) $arm items (NJOBS=$NJOBS)"
  for sig in $SIGNALS; do for order in $ORDERS; do for rate in $BRATES; do
    throttle
    render_one "$sig" "${order%%:*}" "${order##*:}" "${rate%%:*}" "${rate##*:}" "$arm" &
  done; done; done
  wait
done

echo "DONE -> $WORK/{ambix,binaural}/{semaccmf3,opuscmf3}/"
echo "next: $PY $(dirname "${BASH_SOURCE[0]}")/score_cmf3.py"

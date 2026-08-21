#!/bin/bash
# SEMAC transport factorial. Input and output are ALWAYS 3rd-order (16ch); what varies is the
# TRANSPORT the codec squeezes them through:
#
#   transport order   tc4 / tc9 / tc16     (4, 9 or 16 transport channels)
#   transport domain  shd / beam           (spherical-harmonic or beam-projection)
#   spatial norm      norm / nonorm        (SEMAC_SPATIAL_NORMALIZATION on/off)
#   allocation        alloc_1 / alloc_0p5 / alloc_equal   (SEMAC_ALLOC_LAMBDA 1.0 / 0.5 / 0.0)
#   total bitrate     256k / 512k / 768k
#
# 8 signals x 2 domains x 3 transport orders x 3 bitrates x 2 norms x 3 allocations = 864 items.
# Norm and allocation are COMPILE-TIME (semac.h is edited and the codec rebuilt for each of the 6
# combinations); transport order and domain are runtime flags.
#
# This experiment holds the TOTAL bitrate fixed, so a smaller transport means more bits per
# channel. That confound is what the separate per-channel-rate study exists to remove -- keep the
# two straight when comparing results.
#
# Encoding is VBR. The -cbr option did not exist when these results were produced.
#
# Run order:  this script -> full_experiment_eval.py -> render_cmf3.sh -> score_cmf3.py
# Layout:     $WORK/{ambix,binaural}/alloc_{1,0p5,equal}/{256,512,768}kbits/
#             $WORK/{ambix,binaural}/ref/
# Filename:   {sig}_Ambix3_semac_{shd|beam}_{norm|nonorm}_{tc}_alloc{tag}_{256kb|512kb|768kb}[_bin]
set -e

# ============================= SITE CONFIGURATION ============================================
# Plain constants, no environment variables. Edit these for your machine; the repo root is
# derived from this script's own location. See README.md for the external dependencies.
DATA=/data/users/$USER                          # data root; edit if your layout differs
WORK=$DATA/scratch/oac_run/full_experiment      # where renders and results go
SRC=$DATA/scratch/HOA/AMBIX                     # {signal}_AMBIX.wav 3rd-order sources
SOFA=$DATA/scratch/HRTFs/HRIR_L2702.sofa        # HRIR set for the binaural render
RENDER=$DATA/OAC-Meta/render_ambi2bin.py        # ambisonics -> binaural renderer
PY=$HOME/.conda/envs/test/bin/python            # interpreter with numpy/scipy/soundfile
# =============================================================================================

OAC=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)      # repo root
SH=$OAC/celt/semac.h

SIGNALS="BandDry BandWet Clapping MachineAer MovingPink Orchestra Scene Eigenmike"
DOMAINS="shd beam"
TORDERS="1 2 3"                  # transport order -> tc4 / tc9 / tc16
NORMS="norm nonorm"
ALLOCS="1 0p5 equal"             # folder alloc_<tag>, lambda from LAMBDA below
# "<bits per second>:<tag>". The tag goes in the FILENAME as-is; the folder is the tag + "its"
# (so tag 256kb -> folder 256kbits). Do not change the tags -- the scorer matches on them.
BRATES="256000:256kb 512000:512kb 768000:768kb"
NJOBS=8                          # parallel per-item workers; raise to the core count if idle

declare -A TC=( [1]=tc4 [2]=tc9 [3]=tc16 )
declare -A LAMBDA=( [1]=1.0 [0p5]=0.5 [equal]=0.0 )
MAP16=$(seq -s'|' 0 15)
TMP=/tmp/sem_factorial

# Pin each worker single-threaded so NJOBS workers map to NJOBS cores (no BLAS/FFT oversubscription).
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 NUMEXPR_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1

# ---------------------------------------------------------------------------------------------
# semac.h is edited in place, so ALWAYS put it back -- on success, on error and on Ctrl-C alike.
# Without this a failed run leaves the header mutated and the next build silently inherits it.
norm_off(){   sed -i 's|^#define SEMAC_SPATIAL_NORMALIZATION$|/* #define SEMAC_SPATIAL_NORMALIZATION */|' "$SH"; }
norm_on(){    sed -i 's|^/\* #define SEMAC_SPATIAL_NORMALIZATION \*/|#define SEMAC_SPATIAL_NORMALIZATION|' "$SH"; }
alloc_on(){   sed -i 's|^/\* #define SEMAC_ENERGY_ALLOC \*/|#define SEMAC_ENERGY_ALLOC|' "$SH"; }
set_lambda(){ sed -i "s|#define SEMAC_ALLOC_LAMBDA [0-9.]*f|#define SEMAC_ALLOC_LAMBDA ${1}f|" "$SH"; }
build(){      make -j8 >/dev/null 2>&1; }

restore(){
  set_lambda 1.0; norm_off; alloc_on; build || true
  rm -rf "$TMP"
  echo "restored semac.h to shipping defaults (nonorm + energy alloc + lambda 1.0)"
}
trap restore EXIT

# ---------------------------------------------------------------------------------------------
bin_render(){ "$PY" "$RENDER" "$1" "$2" --hrirs_sofa_file "$SOFA" --gain_db -12 >/dev/null 2>&1; }
throttle(){   while [ "$(jobs -r | wc -l)" -ge "$NJOBS" ]; do wait -n; done; }

# The 16ch input PCM for each signal, first 5 s, built once and shared by every build and job.
prep_inputs(){
  local s
  for s in $SIGNALS; do
    ffmpeg -y -i "$SRC/${s}_AMBIX.wav" -ss 0 -to 5 -af "channelmap=map=$MAP16" \
       -c:a pcm_s16le -f s16le "$TMP/in_${s}.pcm" 2>/dev/null
  done
}

# The uncoded 3rd-order reference every coded item is scored against.
render_refs(){
  local s
  for s in $SIGNALS; do
    throttle
    ( ffmpeg -y -i "$SRC/${s}_AMBIX.wav" -ss 0 -to 5 -af "channelmap=map=$MAP16" \
         -c:a pcm_s16le "$WORK/ambix/ref/${s}_Ambix3_ref.wav" 2>/dev/null
      bin_render "$WORK/ambix/ref/${s}_Ambix3_ref.wav" "$WORK/binaural/ref/${s}_Ambix3_ref_bin.wav" ) &
  done
  wait; echo "  refs done"
}

# One coded item: encode+decode, wrap the raw PCM as a wav, then binaural render.
render_one(){
  local sig=$1 dom=$2 torder=$3 bps=$4 tag=$5 norm=$6 alloc=$7
  local base="${sig}_Ambix3_semac_${dom}_${norm}_${TC[$torder]}_alloc${alloc}_${tag}"
  local pcm="$TMP/out_a${alloc}_${base}.pcm"
  local ambix="$WORK/ambix/alloc_$alloc/${tag}its/$base.wav"
  "$OAC/oac_ambisonics_compare" semac 48000 16 "$bps" "$TMP/in_${sig}.pcm" "$pcm" \
     -transport_order "$torder" -domain "$dom" >/dev/null 2>&1
  ffmpeg -y -f s16le -ar 48000 -ac 16 -i "$pcm" -c:a pcm_s16le "$ambix" 2>/dev/null
  bin_render "$ambix" "$WORK/binaural/alloc_$alloc/${tag}its/${base}_bin.wav"
  rm -f "$pcm"
}

# Every coded item for the build that is currently compiled.
render_all(){
  local norm=$1 alloc=$2 sig dom torder rate
  for sig in $SIGNALS; do for dom in $DOMAINS; do for torder in $TORDERS; do
    for rate in $BRATES; do
      throttle
      render_one "$sig" "$dom" "$torder" "${rate%%:*}" "${rate##*:}" "$norm" "$alloc" &
    done
  done; done; done
  wait; echo "  done $norm / alloc_$alloc"
}

# ---------------------------------------------------------------------------------------------
cd "$OAC"
[ -x ./oac_ambisonics_compare ] || { echo "oac_ambisonics_compare not built in $OAC -- run ./configure --enable-semac && make"; exit 1; }
grep -q '^#define ENABLE_SEMAC 1' config.h || { echo "ENABLE_SEMAC is not set in config.h -- reconfigure with --enable-semac"; exit 1; }
[ -d "$SRC" ] || { echo "source signals not found at $SRC -- edit the SITE CONFIGURATION block"; exit 1; }

mkdir -p "$TMP" "$WORK/ambix/ref" "$WORK/binaural/ref"
for dm in ambix binaural; do for alloc in $ALLOCS; do for rate in $BRATES; do
  mkdir -p "$WORK/$dm/alloc_$alloc/${rate##*:}its"
done; done; done

echo "prep inputs (NJOBS=$NJOBS)"; prep_inputs
echo "== references (uncoded 3rd order) =="; render_refs

alloc_on                                  # energy allocation on for the whole factorial
for alloc in $ALLOCS; do
  set_lambda "${LAMBDA[$alloc]}"
  for norm in $NORMS; do
    if [ "$norm" = norm ]; then norm_on; else norm_off; fi
    build
    echo "== $norm alloc_$alloc (lambda ${LAMBDA[$alloc]}) =="
    render_all "$norm" "$alloc"
  done
done
echo "DONE -> $WORK"
echo "next: $PY $(dirname "${BASH_SOURCE[0]}")/full_experiment_eval.py"

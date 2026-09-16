#!/bin/bash
# Normalise the SEMAC listening-test stimulus set into pipeline-ready sources:
# 48 kHz, 36-channel (5th-order) AmbiX float WAV, named {Signal}_AMBIX.wav to match
# run_full_experiment_factorial.sh's SRC convention.
#
# Retention rule: keep only items already at 5th order or above.
#   - 5th order (36 ch) -> passthrough (resampled if not 48 kHz)
#   - 6th order (49 ch) -> truncated to the first 36 channels. AmbiX is ACN-ordered, which is
#     nested by degree, so the first (N+1)^2 channels ARE the order-N signal; truncation is exact
#     order reduction, not an approximation.
#   - below 5th order -> DISCARDED. A 25- or 16-channel file cannot supply a 36-channel condition,
#     and padding it would mean mixing input orders within one condition.
#
# Items 18-23 are 44.1 kHz. Opus/OAC accepts only 8/12/16/24/48 kHz (include/oac.h), and the
# factorial's prep_inputs sets no -ar, so feeding 44.1 kHz through it would write 44.1 kHz raw PCM
# that the codec is then told is 48 kHz -- no error, just an ~8.8% rate error against the reference.
# They are resampled here instead. This ffmpeg has no soxr, so swr is used with a 256-tap filter;
# 44.1->48 kHz artefacts at that length sit far below the codec distortion being measured.
#
# Levels are deliberately NOT touched: the set ships unnormalised (37 dB RMS spread) and matching it
# is a separate decision, since it interacts with the beam domain's level sensitivity.
set -e

SRC="/data/users/chrishold/scratch/HOA/SEMAC Listening Test Stimuli"
OUT=/data/users/chrishold/scratch/HOA/AMBIX_LT5
TARGET_CH=36                                    # 5th order
TARGET_SR=48000

# "<source file>:<signal name>" for every retained item. Items 8-17 (orders 4 and 3) are absent by
# design -- see the retention rule above.
ITEMS=(
  "01_tomfast_5OA.wav:TomFast"
  "02_cbvr_MusicBlues_5OA.wav:CbvrMusicBlues"
  "03_cbvr_MusicMallets_5OA.wav:CbvrMusicMallets"
  "04_cbvr_TeleconferenceOne_5OA.wav:CbvrTeleconfOne"
  "05_cbvr_TeleconferenceTwo_5OA.wav:CbvrTeleconfTwo"
  "06_Applause_6oA-AMBIX.wav:Applause"
  "07_MarnieGlum_Live_6oA-AMBIX.wav:MarnieGlumLive"
  "18_bialowiezaMiscc-5oA-AMBIX.wav:BialowiezaC"
  "19_bialowiezaMisce-5oA-AMBIX.wav:BialowiezaE"
  "20_bialowiezaMiscb-5oA-AMBIX.wav:BialowiezaB"
  "21_bialowiezaMiscf-5oA-AMBIX.wav:BialowiezaF"
  "22_bialowiezaMisca-5oA-AMBIX.wav:BialowiezaA"
  "23_bialowiezaMiscd-5oA-AMBIX.wav:BialowiezaD"
  "24_mocap-R12.wav:MocapR12"
  "25_mocap-R20.wav:MocapR20"
  "26_mocap-R24.wav:MocapR24"
  "27_mocap-R01.wav:MocapR01"
)

MAP=$(seq -s'|' 0 $((TARGET_CH - 1)))
mkdir -p "$OUT"

for entry in "${ITEMS[@]}"; do
  f="${entry%%:*}"; name="${entry##*:}"
  [ -f "$SRC/$f" ] || { echo "MISSING: $f" >&2; exit 1; }
  sr=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 "$SRC/$f")
  ch=$(ffprobe -v error -select_streams a:0 -show_entries stream=channels -of csv=p=0 "$SRC/$f")
  [ "$ch" -ge "$TARGET_CH" ] || { echo "SKIP $f: only $ch ch" >&2; continue; }

  # channelmap both strips the (unknown) layout and performs the 49->36 truncation for the 6th-order
  # items; soxr only engages when the rate actually differs.
  filt="channelmap=map=$MAP"
  note="passthrough"
  if [ "$sr" != "$TARGET_SR" ]; then
    filt="$filt,aresample=$TARGET_SR:filter_size=256:cutoff=0.97"
    note="resampled ${sr}->${TARGET_SR}"
  fi
  [ "$ch" -gt "$TARGET_CH" ] && note="$note, truncated ${ch}->${TARGET_CH} ch"

  ffmpeg -y -i "$SRC/$f" -af "$filt" -c:a pcm_f32le "$OUT/${name}_AMBIX.wav" 2>/dev/null
  printf "  %-22s <- %-38s (%s)\n" "${name}_AMBIX.wav" "$f" "$note"
done

echo
echo "wrote $(ls -1 "$OUT"/*_AMBIX.wav | wc -l) sources -> $OUT"
echo "SIGNALS=\"$(for e in "${ITEMS[@]}"; do printf '%s ' "${e##*:}"; done | sed 's/ $//')\""

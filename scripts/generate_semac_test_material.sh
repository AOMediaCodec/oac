#!/bin/bash
set -e

# =============================================================================
# SEMAC Ambisonics Test Material Generator
#
# Compares opus multistream mapping families 2 and 3 against SEMAC
# with and without spatial normalization, for transport orders 1..input_order.
#
# Bitrates: 24 kb/s per channel, 32 kb/s per channel
# Orders: 1 (FOA, 4ch), 2 (SOA, 9ch), 3 (TOA, 16ch)
# =============================================================================

OAC_DIR="$HOME/Projects/OAC/code/oac-private"
INPUT_BASE="$HOME/Projects/OAC/listening/listening_set_1/ambix"
OUTPUT_BASE="$HOME/Projects/OAC/listening/semac"

SAMPLE_RATE=48000

# Helper: get input directory for a given order
get_input_dir() {
    case "$1" in
        1) echo "orig_foa" ;;
        2) echo "orig_soa" ;;
        3) echo "orig" ;;
    esac
}

# Helper: get channel count for a given order
get_channels() {
    case "$1" in
        1) echo 4 ;;
        2) echo 9 ;;
        3) echo 16 ;;
    esac
}

# =============================================================================
# Step 1: Build with and without SEMAC_SPATIAL_NORMALIZATION
# =============================================================================

build_variant() {
    variant="$1"  # "norm" or "nonorm"
    semac_h="$OAC_DIR/celt/semac.h"
    bin_dest="$OAC_DIR/oac_ambisonics_compare_${variant}"

    echo "========================================"
    echo "Building variant: $variant"
    echo "========================================"

    if [ "$variant" = "norm" ]; then
        # Ensure SEMAC_SPATIAL_NORMALIZATION is defined
        if ! grep -q '^#define SEMAC_SPATIAL_NORMALIZATION' "$semac_h"; then
            sed -i.bak 's|^/\* *#define SEMAC_SPATIAL_NORMALIZATION *\*/|#define SEMAC_SPATIAL_NORMALIZATION|' "$semac_h"
            sed -i.bak 's|^// *#define SEMAC_SPATIAL_NORMALIZATION|#define SEMAC_SPATIAL_NORMALIZATION|' "$semac_h"
        fi
    else
        # Comment out SEMAC_SPATIAL_NORMALIZATION
        if grep -q '^#define SEMAC_SPATIAL_NORMALIZATION' "$semac_h"; then
            sed -i.bak 's|^#define SEMAC_SPATIAL_NORMALIZATION|/* #define SEMAC_SPATIAL_NORMALIZATION */|' "$semac_h"
        fi
    fi

    cd "$OAC_DIR"
    make -j$(sysctl -n hw.ncpu) oac_ambisonics_compare 2>&1 | tail -5

    cp "oac_ambisonics_compare" "$bin_dest"
    echo "Built: $bin_dest"
}

restore_semac_h() {
    semac_h="$OAC_DIR/celt/semac.h"
    if [ -f "${semac_h}.bak" ]; then
        mv "${semac_h}.bak" "$semac_h"
    fi
}

# =============================================================================
# Step 2: Process files
# =============================================================================

wav_to_pcm() {
    sox "$1" -t raw -r "$SAMPLE_RATE" -b 16 -e signed-integer -c "$3" "$2"
}

pcm_to_wav() {
    sox -t raw -r "$SAMPLE_RATE" -b 16 -e signed-integer -c "$3" "$1" "$2"
}

run_encode_decode() {
    binary="$1"
    mode="$2"
    channels="$3"
    bitrate="$4"
    input_pcm="$5"
    output_pcm="$6"
    transport_order="$7"

    if [ "$mode" = "semac" ] && [ -n "$transport_order" ]; then
        "$binary" "$mode" "$SAMPLE_RATE" "$channels" "$bitrate" \
            "$input_pcm" "$output_pcm" -transport_order "$transport_order"
    else
        "$binary" "$mode" "$SAMPLE_RATE" "$channels" "$bitrate" \
            "$input_pcm" "$output_pcm"
    fi
}

binauralize() {
    render_ambi2bin.py "$1" "$2"
}

# =============================================================================
# Main
# =============================================================================

echo "SEMAC Ambisonics Test Material Generator"
echo "========================================="
echo ""

# Build both variants
cd "$OAC_DIR"

# Reconfigure with static linking to produce self-contained binaries
echo "Reconfiguring with --disable-shared..."
./configure --enable-semac --disable-shared 2>&1 | tail -3
make clean 2>&1 | tail -1

build_variant "norm"
build_variant "nonorm"
restore_semac_h

BINARY_NORM="$OAC_DIR/oac_ambisonics_compare_norm"
BINARY_NONORM="$OAC_DIR/oac_ambisonics_compare_nonorm"
# mapping2 and mapping3 don't depend on spatial normalization; use either binary
BINARY_MAPPING="$BINARY_NORM"

# Rebuild with norm enabled (restore default state)
if ! grep -q '^#define SEMAC_SPATIAL_NORMALIZATION' celt/semac.h; then
    sed -i.bak 's|^/\* *#define SEMAC_SPATIAL_NORMALIZATION *\*/|#define SEMAC_SPATIAL_NORMALIZATION|' celt/semac.h
    make -j$(sysctl -n hw.ncpu) oac_ambisonics_compare 2>&1 | tail -3
    rm -f celt/semac.h.bak
fi

echo ""
echo "Processing files..."
echo ""

for order in 1 2 3; do
    channels=$(get_channels "$order")
    input_dir="${INPUT_BASE}/$(get_input_dir "$order")"

    for bps_per_ch in 24000 32000; do
        total_bitrate=$((bps_per_ch * channels))
        bps_label="$((bps_per_ch / 1000))kbps_ch"

        for wav_file in "$input_dir"/*.wav; do
            item_name=$(basename "$wav_file" .wav)
            item_name=$(echo "$item_name" | sed 's/_AMBIX$//')

            ambix_dir="${OUTPUT_BASE}/ambix/order${order}/${bps_label}/${item_name}"
            bin_dir="${OUTPUT_BASE}/binaural/order${order}/${bps_label}/${item_name}"
            mkdir -p "$ambix_dir" "$bin_dir"

            echo "--- Order $order, $bps_label, $item_name ---"

            # Convert input to PCM
            input_pcm="$ambix_dir/_input.pcm"
            wav_to_pcm "$wav_file" "$input_pcm" "$channels"

            # Copy reference and binauralize
            cp "$wav_file" "$ambix_dir/ref.wav"
            binauralize "$ambix_dir/ref.wav" "$bin_dir/ref.wav"

            # --- Mapping family 2 ---
            echo "  mapping2..."
            output_pcm="$ambix_dir/mapping2.pcm"
            run_encode_decode "$BINARY_MAPPING" "mapping2" "$channels" "$total_bitrate" \
                "$input_pcm" "$output_pcm"
            pcm_to_wav "$output_pcm" "$ambix_dir/mapping2.wav" "$channels"
            binauralize "$ambix_dir/mapping2.wav" "$bin_dir/mapping2.wav"
            rm -f "$output_pcm"

            # --- Mapping family 3 ---
            echo "  mapping3..."
            output_pcm="$ambix_dir/mapping3.pcm"
            run_encode_decode "$BINARY_MAPPING" "mapping3" "$channels" "$total_bitrate" \
                "$input_pcm" "$output_pcm"
            pcm_to_wav "$output_pcm" "$ambix_dir/mapping3.wav" "$channels"
            binauralize "$ambix_dir/mapping3.wav" "$bin_dir/mapping3.wav"
            rm -f "$output_pcm"

            # --- SEMAC with spatial normalization ---
            t_order=1
            while [ "$t_order" -le "$order" ]; do
                echo "  semac_norm t${t_order}..."
                output_pcm="$ambix_dir/semac_norm_t${t_order}.pcm"
                run_encode_decode "$BINARY_NORM" "semac" "$channels" "$total_bitrate" \
                    "$input_pcm" "$output_pcm" "$t_order"
                pcm_to_wav "$output_pcm" "$ambix_dir/semac_norm_t${t_order}.wav" "$channels"
                binauralize "$ambix_dir/semac_norm_t${t_order}.wav" \
                    "$bin_dir/semac_norm_t${t_order}.wav"
                rm -f "$output_pcm"
                t_order=$((t_order + 1))
            done

            # --- SEMAC without spatial normalization ---
            t_order=1
            while [ "$t_order" -le "$order" ]; do
                echo "  semac_nonorm t${t_order}..."
                output_pcm="$ambix_dir/semac_nonorm_t${t_order}.pcm"
                run_encode_decode "$BINARY_NONORM" "semac" "$channels" "$total_bitrate" \
                    "$input_pcm" "$output_pcm" "$t_order"
                pcm_to_wav "$output_pcm" "$ambix_dir/semac_nonorm_t${t_order}.wav" "$channels"
                binauralize "$ambix_dir/semac_nonorm_t${t_order}.wav" \
                    "$bin_dir/semac_nonorm_t${t_order}.wav"
                rm -f "$output_pcm"
                t_order=$((t_order + 1))
            done

            # Clean up temp PCM
            rm -f "$input_pcm"
        done
    done
done

echo ""
echo "========================================="
echo "All done! Output in: $OUTPUT_BASE"
echo "========================================="

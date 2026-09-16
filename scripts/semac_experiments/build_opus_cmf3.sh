#!/bin/bash
# Build opus_cmf3_compare: the stock-upstream-Opus CMF3 round-trip tool used as the baseline arm
# of the per-channel-rate study.
#
# This links a CLEAN upstream libopus checkout, deliberately kept separate from oac-private (which
# is itself a rename-fork of libopus and has diverged from upstream -- see the header of
# opus_cmf3_compare.c). Keeping the checkout pristine is the whole point, so nothing is written
# into it beyond its own build artefacts.
#
# Run this once; re-run after editing opus_cmf3_compare.c. Safe to re-run, it skips the libopus
# build if the static library is already there.
set -e

# ============================= SITE CONFIGURATION ============================================
# Plain constants, no environment variables. Edit for your machine.
OPUS_SRC=$HOME/local/opus                       # clean upstream libopus checkout (git clone)
# =============================================================================================

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$HERE/opus_cmf3_compare.c
BIN=$HERE/opus_cmf3_compare

[ -d "$OPUS_SRC" ] || { echo "libopus checkout not found at $OPUS_SRC -- edit OPUS_SRC above"; exit 1; }
[ -f "$SRC" ] || { echo "missing $SRC"; exit 1; }

LIB=$OPUS_SRC/.libs/libopus.a
if [ ! -f "$LIB" ]; then
  echo "building libopus in $OPUS_SRC (one-off, a few minutes)"
  cd "$OPUS_SRC"
  # autoreconf directly rather than ./autogen.sh: autogen.sh first downloads DNN model weights from
  # media.xiph.org, which fails on a devserver with no external network. Those weights are only
  # needed for DRED / deep-PLC / OSCE, all of which default to disabled, so skipping them is safe.
  [ -f configure ] || autoreconf -isf
  [ -f Makefile ] || ./configure --disable-shared --enable-static --disable-doc --disable-extra-programs
  make -j"$(nproc)"
  [ -f "$LIB" ] || { echo "libopus build did not produce $LIB"; exit 1; }
fi

echo "compiling opus_cmf3_compare"
cc -O2 -Wall -Wextra -o "$BIN" "$SRC" -I"$OPUS_SRC/include" "$LIB" -lm

"$BIN" 2>&1 | head -3 || true
echo
echo "built $BIN"
echo "libopus: $(cd "$OPUS_SRC" && git describe --tags 2>/dev/null || echo 'unknown version')"

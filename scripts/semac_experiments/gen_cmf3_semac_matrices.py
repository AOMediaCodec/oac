#!/usr/bin/env python3
"""Regenerate the CMF3 mixing and demixing matrices from SEMAC's SPFB beam matrices.

Rewrites the *_data arrays in src/mapping_matrix.c in place, taking celt/semac.c as the source
of truth. Run from anywhere; paths are derived from this script's location.

The two matrix families are the same thing in different notation:

    SEMAC  oaci_spfb_from_shd_*  (ambisonics -> beams)  ->  CMF3  *_mixing_data
    SEMAC  oaci_spfb_to_shd_*    (beams -> ambisonics)  ->  CMF3  *_demixing_data

Three conversions are needed, and all three are easy to get silently wrong:

1. SCALING. SPFB is float and unbounded; the mapping matrices are Q15, so |value| < 1. from_shd
   peaks at 2.24, which would saturate. Scaling mixing by s and demixing by 1/s leaves the
   round-trip unchanged and lets both sides fit. s is set to the geometric mean of its valid
   bounds, which maximises the headroom on both sides at once.

2. LAYOUT. The CMF3 matrices are ((N+1)^2 + 2) square: the extra two rows/cols are the non-diegetic
   stereo passthrough (unity). The codec only touches the top-left (N+1)^2 block for a given input
   channel count, so the passthrough is written but otherwise inert.

3. STORAGE ORDER. SPFB is row-major with rows = outputs (semac.c's oaci_semac_project indexes
   proj[i*channels + j]). The mapping matrices are COLUMN-major with rows = outputs
   (MATRIX_INDEX(nb_rows, row, col) = nb_rows*col + row). Same logical matrix, transposed storage,
   hence the Fortran-order flatten below. Getting this wrong transposes the beams, which still
   round-trips cleanly through encoder+decoder and so will NOT be caught by the identity check.

The gain field is deliberately left at 0: it is not applied inside the multiply, only reported via
PROJECTION_GET_DEMIXING_MATRIX_GAIN for the caller to apply as a decoder output gain. Baking the
scale into the data keeps the change self-contained.
"""

import re
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
SEMAC_C = REPO / "celt" / "semac.c"
MAPPING_C = REPO / "src" / "mapping_matrix.c"

ORDERS = [("foa", 4), ("soa", 9), ("toa", 16), ("fourthoa", 25), ("fifthoa", 36)]
Q15 = 32767
ROUNDTRIP_TOL = 1e-4


def read_spfb(src, name, n):
    m = re.search(re.escape(name) + r"\[[0-9]*\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit(f"could not find {name} in {SEMAC_C}")
    vals = [float(x) for x in re.findall(r"-?\d+\.\d+(?:e[-+]?\d+)?", m.group(1))]
    if len(vals) != n * n:
        sys.exit(f"{name}: expected {n * n} values, found {len(vals)}")
    return np.array(vals).reshape(n, n)  # [output][input]


def quantise(logical, n_amb, n_full):
    """Embed a logical [out][in] ambisonic block into the full square and flatten column-wise."""
    full = np.zeros((n_full, n_full))
    full[:n_amb, :n_amb] = logical
    for i in range(n_amb, n_full):  # non-diegetic stereo passthrough
        full[i, i] = 1.0
    q = np.round(full * Q15).astype(np.int64)
    if np.abs(q).max() > Q15:
        sys.exit("value would saturate Q15")
    return q.flatten(order="F")


def format_array(name, data):
    body = ",\n".join(
        "    " + ", ".join(f"{v:6d}" for v in data[i : i + 8])
        for i in range(0, len(data), 8)
    )
    return f"const oac_int16 {name}[{len(data)}] = {{\n{body}\n}};"


def main():
    semac_src = SEMAC_C.read_text()
    mapping_src = MAPPING_C.read_text()

    print(
        f"  {'order':<9}{'s':<8} {'mix absmax':<12} {'demix absmax':<14} "
        f"{'roundtrip err':<14} {'cond'}"
    )
    replacements = {}
    for tag, n in ORDERS:
        F = read_spfb(semac_src, f"oaci_spfb_from_shd_{tag}", n)
        T = read_spfb(semac_src, f"oaci_spfb_to_shd_{tag}", n)

        lo, hi = np.abs(T).max(), 1.0 / np.abs(F).max()
        if lo >= hi:
            sys.exit(
                f"{tag}: no scale factor lets both sides fit Q15 ({lo:.4f} >= {hi:.4f})"
            )
        s = float(np.sqrt(lo * hi))

        n_full = n + 2
        mix = quantise(F * s, n, n_full)
        demix = quantise(T / s, n, n_full)

        # Reconstruct exactly what the codec will compute, and check it still inverts.
        Mq = mix.reshape(n_full, n_full, order="F")[:n, :n] / Q15 / s
        Dq = demix.reshape(n_full, n_full, order="F")[:n, :n] / Q15 * s
        err = np.abs(Dq @ Mq - np.eye(n)).max()
        if err > ROUNDTRIP_TOL:
            sys.exit(f"{tag}: round-trip error {err:.2e} exceeds {ROUNDTRIP_TOL:.0e}")

        cond = 20 * np.log10(np.linalg.cond(F))
        print(
            f"  {tag:<9}{s:<8.4f} {np.abs(mix).max():<12d} {np.abs(demix).max():<14d} "
            f"{err:<14.2e} {cond:>5.1f} dB"
        )

        replacements[f"oaci_mapping_matrix_{tag}_mixing_data"] = mix
        replacements[f"oaci_mapping_matrix_{tag}_demixing_data"] = demix

    for name, data in replacements.items():
        pattern = re.compile(
            r"const oac_int16 " + re.escape(name) + r"\[[0-9]*\]\s*=\s*\{.*?\};", re.S
        )
        if not pattern.search(mapping_src):
            sys.exit(f"could not find array {name} in {MAPPING_C}")
        mapping_src = pattern.sub(
            lambda _m, n=name, d=data: format_array(n, d), mapping_src, count=1
        )

    MAPPING_C.write_text(mapping_src)
    print(f"\n  rewrote {len(replacements)} arrays in {MAPPING_C}")
    print("  rebuild with: make oac_ambisonics_compare")


if __name__ == "__main__":
    main()

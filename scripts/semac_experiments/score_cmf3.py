#!/usr/bin/env python3
"""Score the Opus CMF3 baseline with BINAQUAL.

Reads every CMF3 render under $WORK/binaural/cmf3/*kbits/ and scores it against the same uncoded
3rd-order references the SEMAC factorial uses, writing $WORK/results_cmf3.csv.

Filename shape:  {sig}_Ambix3_cmf3_{tc4|tc9|tc16}_{256kb|512kb|768kb}_bin.wav

Scoring goes through bq_score.score_file, the same core full_experiment_eval.py uses -- these two
sets of numbers are compared directly, so they must not drift apart.
"""

import os

# Must precede the numpy import (pulled in via bq_score) so the pool workers stay single-threaded.
os.environ.update(OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1")

import csv  # noqa: E402
import getpass  # noqa: E402
import glob  # noqa: E402
import re  # noqa: E402
from multiprocessing import Pool  # noqa: E402
from pathlib import Path  # noqa: E402

import numpy as np  # noqa: E402
from bq_score import score_file  # noqa: E402

# ============================= SITE CONFIGURATION ============================================
# Must match WORK in render_cmf3.sh.
EXP = f"/data/users/{getpass.getuser()}/scratch/oac_run/full_experiment"
NJOBS = 16
# =============================================================================================

TCS = ["tc4", "tc9", "tc16"]
BRATES = ["256kb", "512kb", "768kb"]
TC_ORDER = {tc: i for i, tc in enumerate(TCS)}
PAT = re.compile(r"(?P<sig>.+?)_Ambix3_cmf3_(?P<tc>tc\d+)_(?P<br>\d+kb)(?:_bin)?$")


def score(path):
    m = PAT.match(Path(path).stem)
    sig = m.group("sig")
    lag, ls = score_file(path, f"{EXP}/binaural/ref/{sig}_Ambix3_ref_bin.wav")
    return dict(sig=sig, tc=m.group("tc"), bitrate=m.group("br"), lag=lag, LS=ls)


def main():
    jobs = sorted(glob.glob(f"{EXP}/binaural/cmf3/*kbits/*_cmf3_*_bin.wav"))
    if not jobs:
        raise SystemExit(
            f"no renders found under {EXP}/binaural/cmf3/ -- run render_cmf3.sh first"
        )
    print(f"{len(jobs)} CMF3 scorings")
    with Pool(NJOBS) as p:
        rows = list(p.imap_unordered(score, jobs))

    out = f"{EXP}/results_cmf3.csv"
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["sig", "tc", "bitrate", "lag", "LS"])
        w.writeheader()
        for r in sorted(
            rows, key=lambda x: (TC_ORDER[x["tc"]], x["bitrate"], x["sig"])
        ):
            w.writerow(r)
    print(f"wrote {out}")

    print(f"mean over {len({r['sig'] for r in rows})} signals:")
    for br in BRATES:
        cells = " ".join(
            f"{tc}={np.mean([r['LS'] for r in rows if r['tc'] == tc and r['bitrate'] == br]):.3f}"
            for tc in TCS
        )
        print(f"  {br}: {cells}")


if __name__ == "__main__":
    main()

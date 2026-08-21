#!/usr/bin/env python3
"""Score the SEMAC transport factorial with BINAQUAL.

Reads every coded render under $WORK/binaural/alloc_*/*kbits/ and scores it against the uncoded
3rd-order reference for the same signal, writing $WORK/results.csv.

Filename shape:
  {sig}_Ambix3_semac_{shd|beam}_{norm|nonorm}_{tc}_alloc{1|0p5|equal}_{256kb|512kb|768kb}_bin.wav

The alloc tag encodes SEMAC_ALLOC_LAMBDA: 1 -> 1.0, 0p5 -> 0.5, equal -> 0.0 (equal split).
Run render_cmf3.sh + score_cmf3.py for the baseline this is compared against.
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
from scipy import stats  # noqa: E402

# ============================= SITE CONFIGURATION ============================================
# Must match WORK in run_full_experiment_factorial.sh.
EXP = f"/data/users/{getpass.getuser()}/scratch/oac_run/full_experiment"
NJOBS = 16
# =============================================================================================

SIGNALS = [
    "BandDry",
    "BandWet",
    "Clapping",
    "MachineAer",
    "MovingPink",
    "Orchestra",
    "Scene",
    "Eigenmike",
]
NORMS = ["norm", "nonorm"]
DOMAINS = ["shd", "beam"]
TCS = ["tc4", "tc9", "tc16"]
BRATES = ["256kb", "512kb", "768kb"]
ALLOCS = ["equal", "0p5", "1"]

TCRIT = stats.t.ppf(0.975, df=len(SIGNALS) - 1)
TC_ORDER = {tc: i for i, tc in enumerate(TCS)}
PAT = re.compile(
    r"(?P<sig>.+?)_Ambix3_semac_(?P<dom>shd|beam)_(?P<norm>nonorm|norm)"
    r"_(?P<tc>tc\d+)_alloc(?P<alloc>[0-9a-z]+)_(?P<br>\d+kb)(?:_bin)?$"
)


def score(path):
    m = PAT.match(Path(path).stem)
    sig = m.group("sig")
    lag, ls = score_file(path, f"{EXP}/binaural/ref/{sig}_Ambix3_ref_bin.wav")
    return dict(
        sig=sig,
        norm=m.group("norm"),
        dom=m.group("dom"),
        tc=m.group("tc"),
        bitrate=m.group("br"),
        alloc=m.group("alloc"),
        lag=lag,
        LS=ls,
    )


def mean_ci(values):
    """Mean and half-width of the 95% t-interval. Returns (nan, nan) for an empty cell."""
    a = np.asarray(values, float)
    if a.size == 0:
        return float("nan"), float("nan")
    return a.mean(), (a.std(ddof=1) / np.sqrt(a.size) * TCRIT if a.size > 1 else 0.0)


def main():
    jobs = sorted(glob.glob(f"{EXP}/binaural/alloc_*/*kbits/*_semac_*_bin.wav"))
    if not jobs:
        raise SystemExit(
            f"no renders found under {EXP}/binaural/alloc_*/ "
            "-- run run_full_experiment_factorial.sh first"
        )
    print(f"{len(jobs)} scorings")
    rows = []
    with Pool(NJOBS) as p:
        for i, r in enumerate(p.imap_unordered(score, jobs), 1):
            rows.append(r)
            if i % 60 == 0 or i == len(jobs):
                print(f"  {i}/{len(jobs)}")

    def cell(norm, dom, tc, br, alloc):
        return mean_ci(
            [
                r["LS"]
                for r in rows
                if r["norm"] == norm
                and r["dom"] == dom
                and r["tc"] == tc
                and r["bitrate"] == br
                and r["alloc"] == alloc
            ]
        )

    nsig = len({r["sig"] for r in rows})
    print(f"\n=== BINAQUAL LS by allocation lambda, mean over {nsig} signals ===")
    for norm in NORMS:
        for dom in DOMAINS:
            print(f"\n#### spatial_norm={norm}  domain={dom} ####")
            for br in BRATES:
                print(f"-- {br} --      {'equal':>13} {'a0p5':>13} {'a1':>13}")
                for tc in TCS:
                    means = [cell(norm, dom, tc, br, a)[0] for a in ALLOCS]
                    print(f"  {tc:>4}: " + " ".join(f"{m:13.3f}" for m in means))

    out = f"{EXP}/results.csv"
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(
            fh, fieldnames=["sig", "norm", "dom", "tc", "bitrate", "alloc", "lag", "LS"]
        )
        w.writeheader()
        for r in sorted(
            rows,
            key=lambda x: (
                x["norm"],
                x["dom"],
                TC_ORDER[x["tc"]],
                x["bitrate"],
                x["alloc"],
                x["sig"],
            ),
        ):
            w.writerow(r)
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()

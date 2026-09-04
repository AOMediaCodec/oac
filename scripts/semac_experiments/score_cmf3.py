#!/usr/bin/env python3
"""Score the factorial study's two CMF3 baselines with BINAQUAL.

Reads every render under $WORK/binaural/{semaccmf3,opuscmf3}/*kbits/ and scores it against the same
uncoded references the SEMAC factorial uses, writing $WORK/results_cmf3_arms.csv.

Two arms, because the fork is not upstream Opus: semaccmf3 is this repo's projection encoder (whose
mapping matrices are SEMAC's own SPFB beam matrices), opuscmf3 is a clean upstream libopus build.

Filename shape:  {sig}_Ambix{N}_{semaccmf3|opuscmf3}_{tc4|tc9|tc16|tc25|tc36}_{256kb|512kb|768kb}_bin.wav

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
EXP = os.environ.get(
    "SEMAC_EXP", f"/data/users/{getpass.getuser()}/scratch/oac_run/full_experiment"
)
NJOBS = 16
# =============================================================================================

BRATES = ["256kb", "512kb", "768kb"]
ARMS = ["semaccmf3", "opuscmf3"]
ARM_ORDER = {arm: i for i, arm in enumerate(ARMS)}
# The arm alternation is explicit so a mis-tagged file fails the match instead of being absorbed
# into the non-greedy sig group.
PAT = re.compile(
    r"(?P<sig>.+?)_Ambix(?P<order>\d+)_(?P<arm>semaccmf3|opuscmf3)_(?P<tc>tc\d+)_(?P<br>\d+kb)(?:_bin)?$"
)


def score(path):
    m = PAT.match(Path(path).stem)
    if not m:
        raise SystemExit(f"filename does not match the expected shape: {path}")
    sig = m.group("sig")
    # The reference carries the same Ambix{N} tag as the item, so read the order off the filename.
    ref = f"{EXP}/binaural/ref/{sig}_Ambix{m.group('order')}_ref_bin.wav"
    lag, ls = score_file(path, ref)
    return dict(
        sig=sig,
        arm=m.group("arm"),
        tc=m.group("tc"),
        bitrate=m.group("br"),
        lag=lag,
        LS=ls,
    )


def main():
    jobs = sorted(
        p for arm in ARMS for p in glob.glob(f"{EXP}/binaural/{arm}/*kbits/*_bin.wav")
    )
    if not jobs:
        raise SystemExit(
            f"no renders found under {EXP}/binaural/{{{','.join(ARMS)}}}/ -- run render_cmf3.sh first"
        )
    print(f"{len(jobs)} CMF3 scorings")
    with Pool(NJOBS) as p:
        rows = list(p.imap_unordered(score, jobs))

    out = f"{EXP}/results_cmf3_arms.csv"
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["sig", "arm", "tc", "bitrate", "lag", "LS"])
        w.writeheader()
        for r in sorted(
            rows,
            key=lambda x: (
                ARM_ORDER[x["arm"]],
                int(x["tc"][2:]),
                x["bitrate"],
                x["sig"],
            ),
        ):
            w.writerow(r)
    print(f"wrote {out}")

    for arm in ARMS:
        arm_rows = [r for r in rows if r["arm"] == arm]
        tcs = sorted({r["tc"] for r in arm_rows}, key=lambda t: int(t[2:]))
        print(f"\n{arm}: mean over {len({r['sig'] for r in arm_rows})} signals")
        for br in BRATES:
            cells = " ".join(
                f"{tc}={np.mean([r['LS'] for r in arm_rows if r['tc'] == tc and r['bitrate'] == br]):.3f}"
                for tc in tcs
            )
            print(f"  {br}: {cells}")


if __name__ == "__main__":
    main()

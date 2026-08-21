#!/usr/bin/env python3
"""Shared BINAQUAL scoring core for the SEMAC experiments.

The SEMAC factorial and the CMF3 baseline are scored by SEPARATE scripts but their numbers are
compared directly against each other. This module exists so both go through provably identical
alignment and BINAQUAL settings -- when the core was duplicated in each script, an edit to one
copy would have silently invalidated the comparison.

IMPORTANT: importing this module imports numpy, so a caller that wants single-threaded workers
must set the thread environment variables BEFORE importing it.
"""

import contextlib
import getpass
import os
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.signal import fftconvolve

# ============================= SITE CONFIGURATION ============================================
# Checkout of the BINAQUAL reference implementation (provides binaqual.calculate_binaqual).
BINAQUAL_DIR = f"/data/users/{getpass.getuser()}/Binaqual"
# =============================================================================================

# BINAQUAL settings, identical for every arm: silence threshold, ear-canal filter, ignored frames.
LEVEL, ELC, IGN = -180.0, 2, 0


def score_file(degraded, reference):
    """BINAQUAL LS of one binaural render against its reference. Returns (lag, LS).

    The delay alignment is MANDATORY, not a refinement: the codec's algorithmic delay is ~120
    samples for SEMAC and ~312 for CMF3, and without removing it every coded score collapses to
    ~0.22 regardless of condition. Because the two arms have DIFFERENT delays, a single global
    offset would not do -- the lag is estimated per file from the left-ear cross-correlation peak.
    """
    import soundfile as sf

    sys.path.insert(0, BINAQUAL_DIR)
    os.chdir(BINAQUAL_DIR)
    from binaqual import calculate_binaqual

    ref_sig, sr = sf.read(reference)
    deg = sf.read(degraded)[0]
    n = min(len(ref_sig), len(deg))
    a = ref_sig[:n, 0] - ref_sig[:n, 0].mean()
    b = deg[:n, 0] - deg[:n, 0].mean()
    lag = int(fftconvolve(b, a[::-1], "full").argmax() - (n - 1))
    # Shift the degraded signal only, all channels by the same amount (the delay is common-mode).
    if lag > 0:
        deg = np.vstack([deg[lag:], np.zeros((lag, deg.shape[1]))])
    elif lag < 0:
        deg = np.vstack([np.zeros((-lag, deg.shape[1])), deg[:lag]])

    # BINAQUAL reads from disk, so the aligned signal goes to a scratch file. The renders on disk
    # are deliberately left unaligned -- alignment is a scoring step, not a stored property.
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tf:
        tmp = tf.name
    sf.write(tmp, deg, sr)
    with contextlib.redirect_stdout(open(os.devnull, "w")):
        _, ls = calculate_binaqual(Path(reference), Path(tmp), LEVEL, ELC, IGN)
    os.unlink(tmp)
    return lag, float(ls)

#!/usr/bin/env python3
"""Generate ambisonics <-> beam projection matrices via spaudiopy.

Designs a spherical filterbank for a given ambisonics order:
    A : SHD -> beams   (analysis / "from_shd" / mixing)
    B : beams -> SHD   (synthesis / "to_shd"  / demixing),  B @ A = I

The modal weighting controls the beam shape and, crucially, the conditioning
of A (which sets how much the decoder amplifies quantization noise):
    - 'hypercardioid' : max directivity; near energy-preserving (cond ~ 1).
    - 'maxre'         : max-rE beams (spatially selective, what SEMAC uses);
                        well-behaved but not orthonormal.
    - 'flat'          : unit modal weights (plain SH sampling).

CONVENTION: spaudiopy works in N3D-normalized ambisonics, but Opus/OAC use
AMBIX = ACN/SN3D. Signals relate per ACN channel k of degree n=floor(sqrt(k)):
    x_N3D[k] = sqrt(2n+1) * x_SN3D[k]   ->  S = diag(sqrt(2n+1))
Beams carry no normalization convention, so only the SHD side is rescaled:
    A_SN3D = A_N3D @ S        (SN3D ambisonics -> beams)
    B_SN3D = inv(S) @ B_N3D   (beams -> SN3D ambisonics)
Reconstruction is preserved: B_SN3D @ A_SN3D = inv(S) B_N3D A_N3D S = I.

"""

import contextlib
import sys
import warnings

warnings.filterwarnings(
    "ignore"
)  # spaudiopy warns about the missing sounddevice backend

import numpy as np

# spaudiopy's optional sounddevice backend prints "PortAudio library not found" to stdout while
# importing when the PortAudio library is absent. This generator writes C source to stdout, so that
# line would land inside the generated file and break the build. Send anything the import prints to
# stderr; the audio backend is irrelevant here, as only the grid and spherical-harmonic maths are used.
with contextlib.redirect_stdout(sys.stderr):
    import spaudiopy as spa
import spaudiopy as spa


def sn3d_scaling(order):
    """Diagonal S with S[k]=sqrt(2n+1) per ACN channel k (N3D = S @ SN3D)."""
    n = np.floor(np.sqrt(np.arange((order + 1) ** 2))).astype(int)
    return np.sqrt(2 * n + 1)


def modal_weights(order, kind):
    if kind == "maxre":
        return spa.sph.maxre_modal_weights(order)
    if kind == "hypercardioid":
        return spa.sph.hypercardioid_modal_weights(order)
    if kind == "flat":
        return np.ones(order + 1)
    raise ValueError(f"unknown modal weight kind: {kind}")


def design(order, kind="hypercardioid", convention="n3d", grid="fliege"):
    """Return (A, B) for the given order, modal weighting, convention, and grid.

    Recipe confirmed to reproduce OAC's fourthoa/fifthoa matrices:
    grid='fliege' (Fliege-Maier nodes, (N+1)^2 points), kind='hypercardioid',
    convention='n3d'.  A: ambisonics -> beams, B: beams -> ambisonics, B @ A = I.

    convention: 'n3d' (spaudiopy native, what the OAC matrices use) or 'sn3d'.

    TODO: the OAC Opus pipeline feeds SN3D (AMBIX) input with no SN3D->N3D conversion,
    so an N3D-designed matrix gets a built-in 1/sqrt(2n+1) taper.
    A proper SN3D-native design would reflect the
    input convention directly; revisit when optimizing beam quality.
    """
    if grid == "fliege":
        vecs, _ = spa.grids.load_Fliege_Maier_nodes(order + 1)  # (N+1)^2 nodes
    elif grid == "maxdet":
        vecs, _ = spa.grids.load_maxDet(order)
    else:
        raise ValueError(f"unknown grid: {grid}")
    vecs = np.asarray(vecs)
    if vecs.shape[0] == 3:
        vecs = vecs.T
    dirs = spa.utils.vec2dir(vecs)  # columns: azimuth, colatitude
    c_n = modal_weights(order, kind)
    A, B = spa.sph.design_sph_filterbank(order, dirs[:, 0], dirs[:, 1], c_n, "pinv")
    if convention == "sn3d":
        s = sn3d_scaling(order)
        A = A * s[np.newaxis, :]  # A @ S : scale columns
        B = B / s[np.newaxis, :].T  # inv(S) @ B : scale rows
    elif convention != "n3d":
        raise ValueError(f"unknown convention: {convention}")
    return A, B


def report(order, kind, convention):
    A, B = design(order, kind, convention)
    s = np.linalg.svd(A, compute_uv=False)
    recon = np.abs(B @ A - np.eye(A.shape[1])).max()
    cond = s.max() / s.min()
    print(
        f"  order {order} [{kind:13s}] "
        f"beams={A.shape[0]:2d} cond={cond:6.3f} "
        f"sigma=[{s.min():.3f},{s.max():.3f}] spread={20 * np.log10(cond):5.2f} dB "
        f"||BA-I||={recon:.2e}"
    )


def scaled_pair(order, kind="hypercardioid", convention="n3d"):
    """Return (A, B) scaled by (s, 1/s) for gain-0 Q15 storage. The balanced
    split s = sqrt(max|B|/max|A|) gives both equal headroom; raises if gain-0
    storage is infeasible (max|A|*max|B| > 1)."""
    A, B = design(order, kind, convention)
    mA, mB = np.abs(A).max(), np.abs(B).max()
    if mA * mB > 1.0:
        raise ValueError(
            f"order {order}: gain-0 storage infeasible (max|A|*max|B|={mA * mB:.3f})"
        )
    s = np.sqrt(mB / mA)
    return A * s, B / s


def print_matrix(name, M, gain=0):
    """Print one MappingMatrix declaration + Q15 int16 data array (no clipping)."""
    dim = M.shape[0]
    q = np.round(M.flatten() * 32768.0).astype(int)
    if q.max() > 32767 or q.min() < -32768:
        raise ValueError(f"{name}: Q15 overflow")
    print(f"const MappingMatrix {name} = {{ {dim}, {dim}, {gain} }};")
    print(f"const oac_int16 {name}_data[{q.size}] = {{")
    for i in range(0, q.size, 8):
        print("    " + ", ".join(f"{int(v):6d}" for v in q[i : i + 8]) + ",")
    print("};\n")


def embed_cmf3(block):
    """Embed an NxN ambisonics block into the (N+2)x(N+2) CMF3 layout: top-left
    ambisonics block, bottom-right 2x2 non-diegetic stereo identity, borders 0."""
    n = block.shape[0]
    dim = n + 2
    M = np.zeros((dim, dim))
    M[:n, :n] = block
    M[n, n] = M[n + 1, n + 1] = 32767.0 / 32768.0  # unity (32768 overflows int16)
    return M


def emit_cmf3(order, name, kind="hypercardioid", convention="n3d"):
    """Emit the (N+2)x(N+2) CMF3 mixing/demixing tables for mapping_matrix.c."""
    As, Bs = scaled_pair(order, kind, convention)
    Mmix, Mdemix = embed_cmf3(As), embed_cmf3(Bs)
    recon = np.abs(Mdemix @ Mmix - np.eye(Mmix.shape[0])).max()
    print(
        f"/* {name}: order {order} ({kind}, {convention.upper()}, Fliege-Maier), "
        f"gain 0, {Mmix.shape[0]}x{Mmix.shape[0]} CMF3 recon err={recon:.2e} */"
    )
    print_matrix(f"oaci_mapping_matrix_{name}_mixing", Mmix)
    print_matrix(f"oaci_mapping_matrix_{name}_demixing", Mdemix)


def emit_semac_spfb(order, name, convention="sn3d"):
    """Emit the SEMAC SPFB float C arrays for semac.c (maxDet grid + maxRE).
    SEMAC input is SN3D (AMBIX), so convention defaults to 'sn3d' so the maxRE
    beams are correct for the input (N3D would add a spurious 1/sqrt(2n+1) taper).
    Reproduces oaci_spfb_from_shd_<name> / oaci_spfb_to_shd_<name>."""
    A, B = design(order, "maxre", convention, grid="maxdet")
    n = A.shape[0]
    recon = np.abs(B @ A - np.eye(n)).max()
    cond = np.linalg.svd(A, compute_uv=False)
    print(
        f"/* {name}: order {order} (maxRE, {convention.upper()}, maxDet grid), "
        f"cond={cond.max() / cond.min():.3f}, recon err={recon:.2e} */"
    )

    def emit(suffix, M):
        print(f"static const float oaci_spfb_{suffix}_{name}[{n * n}] = {{")
        for i, row in enumerate(M):
            vals = ",".join(f"{v:16.10f}f" for v in row)
            print(f"    /* row {i:2d} */ {vals},")
        print("};\n")

    emit("from_shd", A)
    emit("to_shd", B)


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "cmf3":
        order = int(sys.argv[2]) if len(sys.argv) > 2 else 2
        name = sys.argv[3] if len(sys.argv) > 3 else "soa"
        emit_cmf3(order, name, "hypercardioid", "n3d")
    elif len(sys.argv) > 1 and sys.argv[1] == "semac":
        order = int(sys.argv[2]) if len(sys.argv) > 2 else 2
        name = sys.argv[3] if len(sys.argv) > 3 else "soa"
        emit_semac_spfb(order, name)
    else:
        convention = "n3d"
        print(
            f"Spherical filterbank conditioning ({convention.upper()} domain, "
            f"Fliege-Maier grid):"
        )
        for order in (1, 2, 3, 4, 5):
            for kind in ("hypercardioid", "maxre", "flat"):
                report(order, kind, convention)
            print()

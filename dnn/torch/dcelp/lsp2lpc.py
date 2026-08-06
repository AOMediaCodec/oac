import torch
import torchaudio

def stabilize_lsp(lsp: torch.Tensor, min_spacing: float) -> torch.Tensor:
    """
    Vectorized enforcement of minimal spacing between LSPs without loops.
    Supports any tensor shape (..., lsp_dim), e.g., (batch, lsp) or (batch, seq, lsp).
    """
    # Extract only the final dimension size
    lsp_dim = lsp.shape[-1]

    # Cap the requested spacing to the mathematical maximum possible
    max_spacing = 1.0 / (lsp_dim + 1)
    min_spacing = min(min_spacing, max_spacing)

    # 1. Sort to establish baseline order along the last dimension
    lsp_sorted, _ = torch.sort(lsp, dim=-1)

    # 2. Vectorized Forward Pass (Enforces spacing from 0 and moving right)
    # Shape: (lsp_dim,)
    forward_offset = torch.arange(1, lsp_dim + 1, dtype=lsp.dtype, device=lsp.device) * min_spacing

    # Broadcasting automatically handles N-D subtraction: (..., D) - (D,) -> (..., D)
    shifted_forward = lsp_sorted - forward_offset
    clamped_forward = torch.clamp_min(shifted_forward, 0.0)
    cummax_forward, _ = torch.cummax(clamped_forward, dim=-1)

    forward_lsps = cummax_forward + forward_offset

    # 3. Vectorized Backward Pass (Enforces spacing from 1 and moving left)
    # Shape: (lsp_dim,)
    rev_offset = torch.arange(lsp_dim, 0, -1, dtype=lsp.dtype, device=lsp.device) * min_spacing

    shifted_backward = forward_lsps + rev_offset
    clamped_backward = torch.clamp_max(shifted_backward, 1.0)

    # Flip, cummin, and flip back along the last dimension
    flipped_backward = torch.flip(clamped_backward, dims=[-1])
    cummin_backward, _ = torch.cummin(flipped_backward, dim=-1)
    unflipped_backward = torch.flip(cummin_backward, dims=[-1])

    backward_lsps = unflipped_backward - rev_offset

    return backward_lsps


def lsp2lpc(lsp):
    x = lsp*torch.pi
    proots = x[...,0::2,None]
    qroots = x[...,1::2,None]
    ones = torch.ones_like(qroots)
    p = torch.cat([ones, -2*torch.cos(proots), ones], -1)
    q = torch.cat([ones, -2*torch.cos(qroots), ones], -1)
    while (p.shape[-2] > 1):
        p = torchaudio.functional.convolve(p[...,0::2,:],p[...,1::2,:])
        q = torchaudio.functional.convolve(q[...,0::2,:],q[...,1::2,:])
    ones=ones[...,:1,:]
    p = torchaudio.functional.convolve(p[...,:,:],torch.cat([ones,ones],-1))
    q = torchaudio.functional.convolve(q[...,:,:],torch.cat([ones,-ones],-1))
    a = .5*(p+q)
    a = a[...,0,:-1]
    #print(a)
    return a

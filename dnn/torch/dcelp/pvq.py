import torch

def pvq_quantize_hard(x, K):
    """
    Exact PVQ with Straight-Through Estimator.
    Supports a scalar integer K or a tensor K of varying target pulses.
    """
    # 1. Align K for broadcasting
    if isinstance(K, torch.Tensor):
        # If K is missing the trailing dimension (e.g. shape (B,) for x shape (B, N))
        if K.dim() == x.dim() - 1:
            K = K.unsqueeze(-1)
        # Ensure K is floating point for the continuous projection
        K = K.to(x.dtype)
            
    # 2. Continuous projection onto L1 sphere (Differentiable)
    l1_norm = x.abs().sum(dim=-1, keepdim=True)
    y = K * x / (l1_norm + 1e-8)
    
    # 3. Exact Hard Quantization (Non-differentiable)
    with torch.no_grad():
        y_abs = y.abs()
        y_floor = y_abs.floor()
        
        # Calculate missing pulses per vector in the batch
        # pulses_missing shape: (..., 1)
        pulses_missing = torch.round(K - y_floor.sum(dim=-1, keepdim=True)).long()
        
        # Rank fractional remainders
        fractions = y_abs - y_floor
        ranks = torch.argsort(fractions, dim=-1, descending=True)
        
        # Vectorized pulse distribution
        N = x.shape[-1]
        rank_grid = torch.arange(N, device=x.device).expand_as(x)
        
        # Broadcasting magic: (..., N) < (..., 1)
        mask = (rank_grid < pulses_missing).to(x.dtype)
        
        # Scatter the pulses back to their proper dimensional indices
        add_pulses = torch.zeros_like(x).scatter_(-1, ranks, mask)
        
        y_hard_abs = y_floor + add_pulses
        y_hard = y_hard_abs * torch.sign(y)
        
    # 4. Straight-Through Estimator
    return y_hard.detach()

def pvq_ste(x, K):
    x_norm2 = x / (1e-15 + torch.norm(x, dim=-1, keepdim=True))
    x_quant = pvq_quantize_hard(x, K)
    xq = x_quant / (1e-15 + torch.norm(x_quant, dim=-1, keepdim=True))

    return xq.detach() + x - x.detach()

def pvq_diveq(x, K):
    x_norm2 = x / (1e-15 + torch.norm(x, dim=-1, keepdim=True))

    x_quant = pvq_quantize_hard(x, K)
    xq = x_quant / (1e-15 + torch.norm(x_quant, dim=-1, keepdim=True))
    diff = xq - x_norm2
    dist = torch.norm(diff, dim=-1, keepdim=True) + 1e-5
    direction = (diff/dist).detach()
    return x_norm2 + dist*direction

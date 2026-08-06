import torch
import torch.nn as nn
import math

class PulseSpreader6(nn.Module):
    def __init__(self, N=80, max_angle=math.pi/4):
        """
        A 6-level phase-shifted bilateral cascade. 
        Uses mismatched forward/inverse passes to maintain maximum energy 
        diffusion at all times, preventing the "weak level" identity decay.
        """
        super().__init__()
        self.N = N
        self.levels = 6
        self.max_angle = max_angle
        
        # Fixed, alternating base angles near pi/4 for maximum chaotic mixing.
        # Alternating signs prevent DC accumulation across the levels.
        base_angles = torch.tensor([0.71, -0.83, 0.65, -0.77, 0.88, -0.69])
        self.register_buffer('base_angles', base_angles)
        
        # Precompute the index pairs for strides 1, 2, 4, 8, 20, 40
        strides = [1, 2, 4, 8, 20, 40]
        for lvl, stride in enumerate(strides):
            idx1, idx2 = [], []
            for i in range(0, N, 2 * stride):
                for j in range(stride):
                    idx1.append(i + j)
                    idx2.append(i + j + stride)
            self.register_buffer(f'idx1_l{lvl}', torch.tensor(idx1, dtype=torch.long))
            self.register_buffer(f'idx2_l{lvl}', torch.tensor(idx2, dtype=torch.long))

    def _cascade(self, x, angles, reverse=False):
        """
        Executes a single unidirectional 6-level cascade.
        """
        level_order = reversed(range(self.levels)) if reverse else range(self.levels)
        out = x.clone()
        
        for lvl in level_order:
            idx1 = getattr(self, f'idx1_l{lvl}')
            idx2 = getattr(self, f'idx2_l{lvl}')
            
            x1 = out[..., idx1]
            x2 = out[..., idx2]
            
            # Extract the angle for this specific level
            theta = angles[..., lvl]
            
            # Defensively expand theta to match x's dimensions for safe broadcasting
            while theta.dim() < x.dim():
                theta = theta.unsqueeze(-1)
                
            cos_t = torch.cos(theta)
            sin_t = torch.sin(theta)
            
            # Transpose the 2x2 rotation matrix if executing an inverse cascade
            if reverse:
                sin_t = -sin_t
                
            y1 = x1 * cos_t - x2 * sin_t
            y2 = x1 * sin_t + x2 * cos_t
            
            next_out = out.clone()
            next_out[..., idx1] = y1
            next_out[..., idx2] = y2
            out = next_out
            
        return out

    def forward(self, x, alpha, inverse=False):
        """
        x: Input tensor of shape (batch_size, ..., N)
        alpha: Conditioning tensor of shape (batch_size, 1) spanning [0.0, 1.0]
        inverse: Set to True for the despreader (decoder side)
        """
        # Expand alpha to calculate the shifted angle sequence
        angles_spread = self.base_angles + alpha * self.max_angle
        
        if not inverse:
            # ENCODER: W_spread = B(base) * F(spread)
            out = self._cascade(x, angles_spread, reverse=False)
            out = self._cascade(out, self.base_angles, reverse=True)
        else:
            # DECODER: W_despread = B(spread) * F(base)
            out = self._cascade(x, self.base_angles, reverse=False)
            out = self._cascade(out, angles_spread, reverse=True)
            
        return out

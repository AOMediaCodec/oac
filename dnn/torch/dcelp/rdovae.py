import torch
from torch import nn
import torch.nn.functional as F

def soft_dead_zone(x, dead_zone):
    """ approximates application of a dead zone to x """
    d = dead_zone * 0.05
    return x - d * torch.tanh(x / (0.1 + d))

def hard_quantize(x):
    return x + (torch.round(x) - x).detach()

class RDOVAE(nn.Module):
    def __init__(self, nb_dims, nb_quantizers):
        super(RDOVAE, self).__init__()
        self.qmax = nb_quantizers-1

        scale = 3-2*torch.arange(nb_quantizers)[:,None]/nb_quantizers -1 - 0*torch.arange(nb_dims)[None,:]/nb_dims
        deadzone = torch.zeros(nb_quantizers, nb_dims)
        r_soft = torch.zeros(nb_quantizers, nb_dims)
        r_hard = torch.zeros(nb_quantizers, nb_dims)
        theta = torch.zeros(nb_quantizers, nb_dims)

        self.scale = nn.Parameter(scale)
        self.deadzone = nn.Parameter(deadzone)
        self.r_soft = nn.Parameter(r_soft)
        self.r_hard = nn.Parameter(r_hard)
        self.theta = nn.Parameter(theta)

    def _broadcast(self, param, target_tensor):
        """Helper to unsqueeze parameter tensors to match sequence dimensions."""
        while param.dim() < target_tensor.dim():
            param = param.unsqueeze(1)
        return param

    def forward(self, x, q):
        mask = (q <= self.qmax).float()
        q = torch.clamp(q, max=self.qmax)

        s = torch.exp(self.scale[q])
        d = F.softplus(self.deadzone[q])

        # Safely broadcast parameters to match x, regardless of sequence length
        s = self._broadcast(s, x)
        d = self._broadcast(d, x)
        mask_bc = self._broadcast(mask, x)

        z = soft_dead_zone(x*s, d)
        xn = (z + torch.rand_like(z) - .5)/s
        xh = hard_quantize(z)/s

        # Interleaved masking applied strictly on the batch dimension (dim 0)
        b = x.shape[0]
        mask_hard = (torch.arange(b, device=x.device) % 2 == 0).float()
        mask_hard = self._broadcast(mask_hard, x)

        x_mixed = xh * mask_hard + xn * (1.0 - mask_hard)
        xquant = torch.clip(x_mixed * mask_bc, min=-1, max=1)

        return xquant, self.rates_soft(z, q), self.rates_hard(z, q)

    def rates_soft(self, z, q):
        mask = (q <= self.qmax).float()
        q = torch.clamp(q, max=self.qmax)
        r = torch.sigmoid(self.r_soft[q])

        r = self._broadcast(r, z)
        # Drop the final feature dimension so the mask matches the summed rates
        mask_bc = self._broadcast(mask, z)[..., 0]

        rate = torch.sum(-torch.log2((1 - r)/(1 + r) * r ** torch.abs(z) + 1e-6), dim=-1)
        return rate * mask_bc

    def rates_hard(self, z, q):
        mask = (q <= self.qmax).float()
        q = torch.clamp(q, max=self.qmax)
        r = torch.sigmoid(self.r_hard[q])
        theta = torch.sigmoid(self.theta[q])

        r = self._broadcast(r, z)
        theta = self._broadcast(theta, z)
        mask_bc = self._broadcast(mask, z)[..., 0]

        z_q = torch.round(z)
        p0 = 1 - r ** (0.5 + 0.5 * theta)
        alpha = torch.relu(1 - torch.abs(z_q)) ** 2

        rate = -torch.sum((alpha * torch.log2(p0 * r ** torch.abs(z_q) + 1e-6)
                          + (1 - alpha) * torch.log2(0.5 * (1 - p0) * (1 - r) * r ** (torch.abs(z_q) - 1) + 1e-6)),
                          dim=-1)

        return rate * mask_bc

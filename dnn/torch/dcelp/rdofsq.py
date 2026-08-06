import torch
from torch import nn
import torch.nn.functional as F

def stochastic_custom_round(s):
    p_dying = torch.clamp(s, min=0.0, max=1.0)
    sample_dying = torch.bernoulli(p_dying)+1e-5

    s_scaled = s * 2.0
    S = torch.floor(s_scaled)
    f = s_scaled - S
    sample_active = (S + torch.bernoulli(f)) / 2.0

    sample = torch.where(s < 1.0, sample_dying, sample_active)
    return s + (sample - s).detach()

def hard_quantize(x):
    return x + (torch.round(x) - x).detach()

def hard_quantize_offset(x, scale):
    """ round with copy gradient trick """
    sign = torch.sign(torch.randn_like(scale))
    offset = sign*torch.clamp(scale.detach()-.5, min=0)
    return x + ((torch.round(x*scale+offset)-offset) - x*scale).detach()/scale

class RDOFSQ(nn.Module):
    def __init__(self, nb_dims, nb_quantizers):
        super(RDOFSQ, self).__init__()
        self.qmax = nb_quantizers-1

        #scale = torch.zeros(nb_quantizers, nb_dims)+1
        scale = 3-2*torch.arange(nb_quantizers)[:,None]/nb_quantizers -1 - 0*torch.arange(nb_dims)[None,:]/nb_dims
        scale = scale + .1*torch.rand_like(scale)-.05
        self.scale = nn.Parameter(scale)

    def forward(self, x, q):
        mask = q <= self.qmax
        q = torch.clamp(q, max=self.qmax)
        s = torch.exp(self.scale[q])
        s = stochastic_custom_round(s)
        s_hard = torch.where(s >= .5, s, s.detach())
        xn = x + (torch.rand_like(x) - .5)/s
        #s_hard = torch.where(s > 0.5, s, s.detach())
        xh = hard_quantize_offset(x, s_hard)
        b = xn.shape[0]
        mask_hard = (torch.arange(b, device=x.device) % 2 == 0).float()[:, None]
        out = xh * mask_hard + xn * (1 - mask_hard)
        return torch.clip(out*mask[:,None], min=-1, max=1)
        #return torch.clip(xn*mask[:,None], min=-1, max=1)
        #return torch.clip(torch.cat([xh[:bound], xn[bound:]], 0)*mask[:,None], min=-1, max=1)
        #return torch.clip(xh*mask[:,None], min=-1, max=1)

    def rates(self, q, reg):
        mask = q <= self.qmax
        q = torch.clamp(q, max=self.qmax)
        s = torch.exp(self.scale[q])
        #return torch.sum(torch.log2(1+2*s), -1)*mask
        xx = torch.min(.5*s**2, torch.clamp(s-.5, min=.5))
        s2 = 1-torch.cos(2*torch.pi*xx)**2
        s_warped = s + .15*reg*s2
        return torch.sum(1.4427*torch.min(s_warped, 1/(4*s_warped) + torch.log(1e-15 + 2*s_warped)), dim=-1)*mask
        rh = torch.sum(torch.log2(torch.clamp(2*s, min=1)), -1)
        rn = torch.sum(1.4427*torch.min(s, 1/(4*s) + torch.log(1e-15 + 2*s)), dim=-1)
        b = rn.shape[0]
        bound = b//2+1
        return torch.cat([rh[:bound], rn[bound:]], 0)*mask

    def rate_metric(self, q):
        mask = q <= self.qmax
        q = torch.clamp(q, max=self.qmax)
        s = torch.exp(self.scale[q])
        #return torch.mean(torch.sum(torch.log2(1+2*s), -1))
        return torch.sum(torch.log2(torch.clamp(2*s, min=1)), -1)

    def regularizer(self):
        s = torch.exp(self.scale)
        xx = torch.min(.5*s**2, torch.clamp(s-.5, min=.5))
        return torch.mean(1-torch.cos(2*torch.pi*xx)**2)

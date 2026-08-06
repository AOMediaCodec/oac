import torch
from torch import nn
import torch.nn.functional as F
import numpy as np
import filters

class PitchAnalysis(nn.Module):
    def __init__(self, subframe_size, device=None):
        super(PitchAnalysis, self).__init__()

        self.subframe_size = subframe_size
        F = np.array([0.000000, 0.006794, 0.010021, -0.000000, -0.021384, -0.030100, 0.000000, 0.055822, 0.074399, -0.000000, -0.131585, -0.178893, 0.000000, 0.399018, 0.819684, 1.000000, 0.819684, 0.399018, 0.000000, -0.178893, -0.131585, -0.000000, 0.074399, 0.055822, 0.000000, -0.030100, -0.021384, -0.000000, 0.010021, 0.006794])

        C = np.zeros((17*3, 17+10))
        for k in range(17):
            for m in range(3):
                C[3*k+m, k:k+10] = F[(2-m)::3]
        interp = torch.tensor(C,dtype=torch.float)
        self.register_buffer('interp', interp)

    def forward(self, x, fir, p0):
        #print(fir_mat.shape, x.shape)
        #wx = torch.bmm(fir_mat, x[...,None])
        #wx = wx[...,0]
        wx = filters.batched_fir(x, fir.flip(-1))
        #print(*(wx.detach().cpu().numpy()[0,...]))

        target = wx[...,-self.subframe_size:]
        idx = wx.shape[-1]-self.subframe_size-p0[:,None]
        rng = torch.arange(self.subframe_size+26, device=x.device)
        idx = idx + rng[None,:] - 13
        y = torch.gather(wx, 1, idx)

        frac = F.conv1d(y[:,None,:], self.interp[:,None,:])
        error = frac - target[:,None,:]
        mse = torch.mean(error**2, dim=-1)
        best_mse, best_ind = torch.min(mse, dim=-1)
        #print(*(mse.detach().cpu().numpy()[0,...]))
        #print(error.shape)
        return torch.clamp(p0 + 9 - ((best_ind+2)//3), min=32, max=256)
        #return torch.clamp(p0 + ((26-best_ind+2)//3), min=32, max=256)

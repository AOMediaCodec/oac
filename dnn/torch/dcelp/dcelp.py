import os
import sys

import numpy as np
import torch
from torch import nn
import torch.nn.functional as F
import filters
from torch.nn.utils import weight_norm
#from convert_lsp import lpc_to_lsp, lsp_to_lpc
from lsp2lpc import lsp2lpc, stabilize_lsp
from lpc2ceps import lpc2ceps
import math
from pitch import PitchAnalysis
from rdofsq import RDOFSQ
from pvq import pvq_diveq, pvq_ste
from spread import PulseSpreader6

source_dir = os.path.split(os.path.abspath(__file__))[0]
sys.path.append(os.path.join(source_dir, "../dnntools"))
from dnntools.quantization import soft_quant


Fs = 16000

pitch_mem_size = 280

nb_quant = 16
fsq_dims = 10
feedback_size = 20

fid_dict = {}
def dump_signal(x, filename):
    return
    if filename in fid_dict:
        fid = fid_dict[filename]
    else:
        fid = open(filename, "w")
        fid_dict[filename] = fid
    x = x.detach().numpy().astype('float32')
    x.tofile(fid)

def sig_l1(y_true, y_pred):
    return torch.mean(abs(y_true-y_pred))/torch.mean(abs(y_true))

def sig_loss(y_true, y_pred):
    t = y_true/(1e-15+torch.norm(y_true, dim=-1, p=2, keepdim=True))
    p = y_pred/(1e-15+torch.norm(y_pred, dim=-1, p=2, keepdim=True))
    return 1.-torch.sum(p*t, dim=-1)

def sig_loss_split(y_true, y_pred):
    y_true = y_true.view(y_true.shape[0], -1, 160)
    y_pred = y_pred.view(y_pred.shape[0], -1, 160)
    xx = torch.norm(y_true, dim=-1, p=2)
    yy = torch.norm(y_pred, dim=-1, p=2)
    t = y_true
    p = y_pred
    #print(xx.shape, t.shape, torch.sum(p*t, dim=-1).shape)
    return torch.mean(torch.sqrt(1.01-2*torch.sum(p*t, dim=-1)/(1e-15+xx**2+yy**2))*xx, dim=-1) / (1e-10+torch.mean(xx, dim=-1))

class DCT(nn.Module):
    def __init__(self, N, device=None):
        super(DCT, self).__init__()

        self.N = N
        n = torch.arange(N, device=device)
        k = torch.arange(N, device=device)
        table = torch.cos(torch.pi/N * (n[None,:]+.5) * k[:,None])
        table[0,:] = table[0,:] * math.sqrt(.5)
        table = table / math.sqrt(N/2)
        self.register_buffer('table', table)

    def forward(self, x):
        return F.linear(x, self.table, None)

def analysis_filter(x, lpc, nb_subframes=4, subframe_size=40, gamma=.9):
    device = x.device
    batch_size = lpc.size(0)

    nb_frames = lpc.shape[1]


    sig = torch.zeros(batch_size, subframe_size+16, device=device)
    x = torch.reshape(x, (batch_size, nb_frames*nb_subframes, subframe_size))
    out = torch.zeros((batch_size, 0), device=device)

    #if gamma is not None:
    #    bw = gamma**(torch.arange(1, 17, device=device))
    #    lpc = lpc*bw[None,None,:]
    ones = torch.ones((*(lpc.shape[:-1]), 1), device=device)
    zeros = torch.zeros((*(lpc.shape[:-1]), subframe_size-1), device=device)
    a = torch.cat([ones, lpc], -1)
    a_big = torch.cat([a, zeros], -1)
    fir_mat_big = filters.toeplitz_from_filter(a_big)

    #print(a_big[:,0,:])
    for n in range(nb_frames):
        for k in range(nb_subframes):

            sig = torch.cat([sig[:,subframe_size:], x[:,n*nb_subframes + k, :]], 1)
            exc = torch.bmm(fir_mat_big[:,n,:,:], sig[:,:,None])
            out = torch.cat([out, exc[:,-subframe_size:,0]], 1)

    return out


# weight initialization and clipping
def init_weights(module):
    if isinstance(module, nn.GRU):
        for p in module.named_parameters():
            if p[0].startswith('weight_hh_'):
                nn.init.orthogonal_(p[1])

def gen_phase_embedding(periods, frame_size):
    device = periods.device
    batch_size = periods.size(0)
    nb_frames = periods.size(1)
    w0 = 2*torch.pi/periods
    w0_shift = torch.cat([2*torch.pi*torch.rand((batch_size, 1), device=device)/frame_size, w0[:,:-1]], 1)
    cum_phase = frame_size*torch.cumsum(w0_shift, 1)
    fine_phase = w0[:,:,None]*torch.broadcast_to(torch.arange(frame_size, device=device), (batch_size, nb_frames, frame_size))
    embed = torch.unsqueeze(cum_phase, 2) + fine_phase
    embed = torch.reshape(embed, (batch_size, -1))
    return torch.cos(embed), torch.sin(embed)

def process_audio(audio):
    audio = audio.float() / 2**15
    return audio

def process_features(features):
    E = features[...,:1]
    period = features[...,1:2]
    pgain = features[...,2:3]
    lsp = features[...,-16:]
    #lsp = stabilize_lsp(lsp, 0.01)
    lpc = lsp2lpc(lsp)
    ceps = 11.1*lpc2ceps(lpc, 256)[...,:45]
    lsp = 10.*(lsp-torch.arange(1, 17, device=lsp.device)/18.)
    out_features = torch.cat([E, period, pgain, lsp, ceps], -1)
    periods = torch.round(torch.clamp(256./2**(features[...,1]+1.5), min=32, max=255)).int()
    return out_features, periods, lpc, pgain

def feature_distortion(quantized, orig):
    lsp = orig[...,-16:]
    qlsp = quantized[...,-16:]
    lpc = lsp2lpc(lsp)
    qlpc = lsp2lpc(qlsp)
    ceps = 11.1*lpc2ceps(lpc, 256)
    qceps = 11.1*lpc2ceps(qlpc, 256)
    E = orig[...,:1]
    qE = quantized[...,:1]
    pitch = orig[...,1:2]
    qpitch = quantized[...,1:2]
    pgain = orig[...,2:3]
    qpgain = quantized[...,2:3]
    return torch.mean((lsp-qlsp)**2) + .001*torch.mean((E-qE)**2) + torch.mean(torch.abs(pitch-qpitch)) + torch.mean((pgain-qpgain)**2)
    #return torch.mean((ceps-qceps)**2) + torch.mean((E-qE)**2) + torch.mean(torch.abs(pitch-qpitch)) + torch.mean((pgain-qpgain)**2)

def scale_grad(x, scale):
    return x*scale + (1.-scale)*x.detach()

class GLU(nn.Module):
    def __init__(self, feat_size, softquant=False):
        super(GLU, self).__init__()

        torch.manual_seed(5)

        self.gate = weight_norm(nn.Linear(feat_size, feat_size))

        if softquant:
            self.gate = soft_quant(self.gate)

        self.init_weights()

    def init_weights(self):

        for m in self.modules():
            if isinstance(m, nn.Conv1d) or isinstance(m, nn.ConvTranspose1d)\
            or isinstance(m, nn.Linear) or isinstance(m, nn.Embedding):
                nn.init.orthogonal_(m.weight.data)

    def forward(self, x):

        out = x * torch.sigmoid(self.gate(x))

        return out

class FWConv(nn.Module):
    def __init__(self, in_size, out_size, kernel_size=2, softquant=False):
        super(FWConv, self).__init__()

        torch.manual_seed(5)

        self.in_size = in_size
        self.kernel_size = kernel_size
        self.conv = weight_norm(nn.Linear(in_size*self.kernel_size, out_size))
        self.glu = GLU(out_size, softquant=softquant)

        if softquant:
            self.conv = soft_quant(self.conv)

        self.init_weights()

    def init_weights(self):

        for m in self.modules():
            if isinstance(m, nn.Conv1d) or isinstance(m, nn.ConvTranspose1d)\
            or isinstance(m, nn.Linear) or isinstance(m, nn.Embedding):
                nn.init.orthogonal_(m.weight.data)

    def forward(self, x, state):
        xcat = torch.cat((state, x), -1)
        #print(x.shape, state.shape, xcat.shape, self.in_size, self.kernel_size)
        out = self.glu(torch.tanh(self.conv(xcat)))
        return out, xcat[:,self.in_size:]

def n(x):
    return torch.clamp(x + (1./127.)*(torch.rand_like(x)-.5), min=-1., max=1.)

class DCELPCond(nn.Module):
    def __init__(self, feature_dim=20, cond_size=256, pembed_dims=12, nb_subframes=2, softquant=False):
        super(DCELPCond, self).__init__()

        self.feature_dim = feature_dim
        self.cond_size = cond_size

        self.pembed = nn.Embedding(224, pembed_dims)
        self.fdense1 = nn.Linear(self.feature_dim + pembed_dims, 64)
        self.fconv1 = nn.Conv1d(64, 128, kernel_size=3, padding='valid')
        self.fdense2 = nn.Linear(128, 80*nb_subframes)

        if softquant:
            self.fconv1 = soft_quant(self.fconv1)
            self.fdense2 = soft_quant(self.fdense2)

        self.apply(init_weights)
        nb_params = sum(p.numel() for p in self.parameters())
        print(f"cond model: {nb_params} weights")

    def forward(self, features, period):
        features = features[:,2:,:]
        period = period[:,2:]
        p = self.pembed(period-32)
        features = torch.cat((features, p), -1)
        tmp = torch.tanh(self.fdense1(features))
        tmp = tmp.permute(0, 2, 1)
        tmp = torch.tanh(self.fconv1(tmp))
        tmp = tmp.permute(0, 2, 1)
        tmp = torch.tanh(self.fdense2(tmp))
        #tmp = torch.tanh(self.fdense2(tmp))
        return tmp

class DCELPSub(nn.Module):
    def __init__(self, fsq, subframe_size=40, nb_subframes=4, cond_size=256, softquant=False):
        super(DCELPSub, self).__init__()

        self.fsq = fsq
        self.subframe_size = subframe_size
        self.nb_subframes = nb_subframes
        self.cond_size = cond_size
        self.cond_gain_dense = nn.Linear(80, 1)
        self.dct = DCT(self.subframe_size)
        self.pitch = PitchAnalysis(self.subframe_size)

        #self.sig_dense1 = nn.Linear(4*self.subframe_size+self.passthrough_size+self.cond_size, self.cond_size, bias=False)
        self.fc0 = nn.Linear(2*self.subframe_size+feedback_size+80 + fsq_dims+self.subframe_size + 1, 160)
        self.fc1 = nn.Linear(160+2*self.subframe_size+fsq_dims+feedback_size, 128)
        self.fc2 = nn.Linear(128+2*self.subframe_size+fsq_dims+feedback_size, 128)
        self.fc3 = nn.Linear(128+2*self.subframe_size+fsq_dims+feedback_size, 128)

        self.fc0_glu = GLU(160, softquant=softquant)
        self.fc1_glu = GLU(128, softquant=softquant)
        self.fc2_glu = GLU(128, softquant=softquant)
        self.fc3_glu = GLU(128, softquant=softquant)
        self.skip_glu = GLU(128, softquant=softquant)
        #self.ptaps_dense = nn.Linear(4*self.cond_size, 5)

        self.skip_dense = nn.Linear(160+128+2*128+2*self.subframe_size+fsq_dims+feedback_size, 128)
        self.sig_dense_out = nn.Linear(128, self.subframe_size+feedback_size)
        self.gain_dense_out = nn.Linear(160, 5)

        self.enc1 = nn.Linear(4*self.subframe_size+80+feedback_size, 128)
        self.enc2 = nn.Linear(128, 128)
        self.enc3 = nn.Linear(128, 128)
        self.enc4 = nn.Linear(128*3+4*self.subframe_size+80+feedback_size, fsq_dims+self.subframe_size)
        self.enc1_glu = GLU(128, softquant=softquant)
        self.enc2_glu = GLU(128, softquant=softquant)
        self.enc3_glu = GLU(128, softquant=softquant)
        self.spread = PulseSpreader6(self.subframe_size)

        if softquant:
            #self.gru1 = soft_quant(self.gru1, names=['weight_hh', 'weight_ih'])
            #self.gru2 = soft_quant(self.gru2, names=['weight_hh', 'weight_ih'])
            #self.gru3 = soft_quant(self.gru3, names=['weight_hh', 'weight_ih'])
            self.skip_dense = soft_quant(self.skip_dense)
            self.sig_dense_out = soft_quant(self.sig_dense_out)

        self.apply(init_weights)
        nb_params = sum(p.numel() for p in self.parameters())
        print(f"subframe model: {nb_params} weights")

    def forward(self, cond, feedback, exc_mem, pgain, lpc, wlpc, syn, target, period, q):
        device = exc_mem.device

        fir_mat = filters.toeplitz_from_filter(lpc)
        iir_mat = filters.toeplitz_from_filter(syn)

        #print(cond.shape, prev.shape)
        cond = n(cond)
        #dump_signal(gain, 'gain0.f32')
        gain = torch.exp(self.cond_gain_dense(cond))
        dump_signal(gain, 'gain1.f32')

        target = n(target/(1e-5+gain))
        residual = filters.batched_fir(target[:,-self.subframe_size-16:], lpc.flip(-1))[:,-self.subframe_size:]
        new_period = self.pitch(target, wlpc, period)
        #print(*(period.detach().cpu().numpy()), *(new_period.detach().cpu().numpy()))
        period=new_period

        target = target[...,-self.subframe_size:]

        prev = exc_mem[:,-self.subframe_size:]
        dump_signal(prev, 'prev_in.f32')
        prev = n(prev/(1e-5+gain))
        mem = prev[:,-16:].reshape(-1, 16, 1)
        exc = torch.bmm(fir_mat[...,:16,:16], mem)
        zero_resp = torch.bmm(iir_mat[...,16:16+self.subframe_size,:16], exc)
        zero_resp = zero_resp[:,:,0]

        idx = pitch_mem_size-period[:,None]-1
        rng = torch.arange(self.subframe_size+2, device=device)
        idx = idx + rng[None,:]
        mask = idx >= pitch_mem_size
        idx = idx - mask*period[:,None]
        mask = idx >= pitch_mem_size
        idx = idx - mask*period[:,None]

        iir_sf_mat = iir_mat[...,:self.subframe_size,:self.subframe_size]
        wexc = filters.batched_fir(exc_mem, lpc.flip(-1))
        ltp = torch.gather(wexc, 1, idx)
        residual = residual - ltp[...,1:-1]*(torch.sum(residual*ltp[...,1:-1], dim=-1, keepdim=True)/(1e-15 + torch.sum(ltp[...,1:-1]*ltp[...,1:-1], dim=-1, keepdim=True)))
        ltp2 = ltp[...,0:-2] + ltp[...,2:]
        ltp = filters.batched_iir(ltp[...,1:-1], iir_sf_mat)
        ltp2 = filters.batched_iir(ltp2, iir_sf_mat)
        #ltp = torch.gather(exc_mem, 1, idx)

        ltp = ltp/(1e-5+gain)
        ltp2 = ltp2/(1e-5+gain)
        dump_signal(ltp, 'pred0.f32')
        pred = n(ltp + zero_resp)
        #print((torch.mean((target-pred)**2).cpu().detach().numpy()), (torch.mean((target)**2).cpu().detach().numpy()))

        dump_signal(prev, 'prev.f32')
        dump_signal(pred, 'pred1.f32')
        dump_signal(zero_resp, 'zero_resp.f32')
        #dump_signal(exc_mem, 'exc_mem.f32')
        #prev = self.dct(prev)
        #pred = self.dct(pred)

        feedback = scale_grad(n(feedback), .4)

        #prev = zero_resp
        #print(torch.mean((target[...,:20])**2).detach().numpy(), torch.mean((target[...,:20]-zero_resp[...,:20])**2).detach().numpy())

        #print(cond.shape, pred.shape, prev.shape, target.shape)
        enc_input = torch.cat((cond, pred, feedback.detach(), prev, target, n(residual)), 1)
        e1 = self.enc1_glu(torch.tanh(self.enc1(enc_input)))
        e2 = self.enc2_glu(torch.tanh(self.enc2(e1)))
        e3 = self.enc3_glu(torch.tanh(self.enc3(e2)))
        e = torch.cat([enc_input, e1, e2, e3], -1)
        e_all = self.enc4(e)
        e = torch.tanh(e_all[...,:fsq_dims])
        pe = e_all[...,fsq_dims:]
        #alpha = .5*(cond[...,0:1]+1)
        alpha = .5 - pgain
        if self.training:
            alpha = alpha * torch.rand_like(alpha)
        else:
            alpha = .5*alpha + .5*(q[:, None]/16.)
        #print(alpha.cpu().detach().item())
        alpha = alpha*(torch.randint_like(alpha, 0, 2)*2-1)
        #alpha2 = torch.rand_like(pe[...,0:1])
        pe = self.spread(pe, alpha)
        #pe = self.spread(pe, alpha2, inverse=True)
        pe = pvq_diveq(pe, torch.clamp(nb_quant-q, min=1)[:,None])
        if self.training:
            noise = torch.randn_like(pe)
            noise = noise/(1e-15+torch.norm(noise, p=2, dim=-1, keepdim=True))
            pe = pe + torch.clamp(q-nb_quant, min=0)[:,None]*.5*noise
            pe = pe/(1e-15+torch.norm(pe, p=2, dim=-1, keepdim=True))
        else:
            noise = torch.randn_like(pe)
            noise = noise/(1e-15+torch.norm(noise, p=2, dim=-1, keepdim=True))
            pe = pe*(q<16)[:,None] + noise*(q>=16)[:,None]
        #pe = self.spread(pe, alpha2)
        pe = self.spread(pe, alpha, inverse=True)
        res_corr = torch.mean(torch.sum(residual.detach()*pe, dim=-1)/(1e-15+torch.norm(residual.detach(), dim=-1, p=2)))
        #res_corr = torch.mean(torch.norm(pe, dim=-1, p=2))
        #sig_latent = e + .99*(torch.rand_like(e)-.5)
        e_var = e**2
        e = self.fsq(e, q)
        #pe = filters.batched_iir(pe, iir_sf_mat)
        sig_latent = n(torch.cat([e, pe], -1))

        tmp = torch.cat((cond, pred, feedback, prev, sig_latent, .1*torch.clamp(q[:,None]-nb_quant/2, max=8)), 1)
        #fpitch = taps[:,0:1]*pred[:,:-4] + taps[:,1:2]*pred[:,1:-3] + taps[:,2:3]*pred[:,2:-2] + taps[:,3:4]*pred[:,3:-1] + taps[:,4:]*pred[:,4:]
        #fpitch = pred #[:,2:-2]

        #tmp = self.dense1_glu(torch.tanh(self.sig_dense1(tmp)))
        fc0_out = self.fc0_glu(torch.tanh(self.fc0(tmp)))
        fc0_out = n(fc0_out)
        pitch_gain = torch.sigmoid(self.gain_dense_out(fc0_out))

        t0 = pitch_gain[:,4:5]
        t1 = (1-t0)/2
        ltp = t1*ltp2 + t0*ltp

        fc1_out = n(self.fc1_glu(n(torch.tanh(self.fc1(torch.cat([fc0_out, n(zero_resp + pitch_gain[:,0:1]*ltp), feedback, sig_latent], 1))))))
        fc2_out = n(self.fc2_glu(n(torch.tanh(self.fc2(torch.cat([fc1_out, n(zero_resp + pitch_gain[:,1:2]*ltp), feedback, sig_latent], 1))))))
        fc3_out = n(self.fc3_glu(n(torch.tanh(self.fc3(torch.cat([fc2_out, n(zero_resp + pitch_gain[:,2:3]*ltp), feedback, sig_latent], 1))))))
        skip_out = torch.tanh(self.skip_dense(torch.cat([fc0_out, fc1_out, fc2_out, fc3_out, n(zero_resp + pitch_gain[:,3:4]*ltp), feedback, sig_latent], 1)))
        skip_out = n(self.skip_glu(n(skip_out)))
        tmp_out = torch.tanh(self.sig_dense_out(skip_out))
        sig_out = tmp_out[...,:self.subframe_size]
        feedback = tmp_out[...,self.subframe_size:]

        dump_signal(sig_out, 'sig_out.f32')
        #taps = self.ptaps_dense(gru3_out)
        #taps = .2*taps + torch.exp(taps)
        #taps = taps / (1e-2 + torch.sum(torch.abs(taps), dim=-1, keepdim=True))
        #dump_signal(taps, 'taps.f32')

        #dump_signal(pitch_gain, 'pgain.f32')
        #sig_out = (sig_out + pitch_gain*fpitch) * gain
        sig_out = sig_out * gain
        exc_mem = torch.cat([exc_mem[:,self.subframe_size:], sig_out], 1)
        #dump_signal(sig_out, 'sig_out.f32')
        return sig_out, exc_mem, feedback, e_var, res_corr

class DCELP(nn.Module):
    def __init__(self, subframe_size=40, nb_subframes=4, feature_dim=64, cond_size=256, passthrough_size=0, has_gain=False, gamma=None, softquant=False):
        super(DCELP, self).__init__()

        self.subframe_size = subframe_size
        self.nb_subframes = nb_subframes
        self.frame_size = self.subframe_size*self.nb_subframes
        self.feature_dim = feature_dim
        self.cond_size = cond_size

        self.fsq = RDOFSQ(fsq_dims, nb_quant)
        self.cond_net = DCELPCond(feature_dim=feature_dim, cond_size=cond_size, nb_subframes=nb_subframes, softquant=softquant)
        self.sig_net = DCELPSub(self.fsq, subframe_size=subframe_size, nb_subframes=nb_subframes, cond_size=cond_size, softquant=softquant)

        lambda_min = 0.00003
        lambda_max = 0.0005
        denominator = (nb_quant - 1) / np.log(lambda_max / lambda_min)
        arange = np.minimum(np.arange(nb_quant*2), nb_quant-1)
        qlambda = lambda_min * np.exp(arange.astype(np.float32) / denominator).astype(np.float32)
        self.register_buffer('qlambda', torch.tensor(qlambda))

    def forward(self, features_in, target, q, nb_frames, pre=None):
        device = features_in.device
        batch_size = features_in.size(0)

        cond_features, period, lpc, pgain = process_features(features_in)
        feedback = torch.zeros(batch_size, feedback_size, device=device)
        exc_mem = torch.zeros(batch_size, pitch_mem_size, device=device)
        target_padded = F.pad(target, (pitch_mem_size, 0))
        nb_pre_frames = pre.size(1)//self.frame_size if pre is not None else 0

        gamma=0.9
        bw = gamma**(torch.arange(0, 17, device=device))
        wlpc = lpc*bw
        syn = filters.filter_iir_response(lpc, self.subframe_size+20)

        sig_list = []
        latent_var_list = []

        cond = self.cond_net(cond_features, period)
        if pre is not None:
            exc_mem[:,-self.frame_size:] = pre[:, :self.frame_size]
        start = 1 if nb_pre_frames>0 else 0
        res_corr = torch.tensor(0., device=device)
        for n in range(start, nb_frames+nb_pre_frames):
            for k in range(self.nb_subframes):
                pos = n*self.frame_size + k*self.subframe_size
                #print("now: ", preal.shape, feedback.shape, sig_in.shape)
                pitch = period[:, 4+n]
                #gain = gain[:,:,None]
                p_pos = pos + pitch_mem_size
                target_mem = target_padded[:, p_pos - pitch_mem_size : p_pos + self.subframe_size]
                out, exc_mem, feedback, e_var, corr = self.sig_net(cond[:, n, k*80:(k+1)*80], feedback, exc_mem, pgain[:, 4+n,:], lpc[:, 4+n,:], wlpc[:, 4+n,:], syn[:, 4+n,:], target_mem, pitch, q)

                if n < nb_pre_frames:
                    out = pre[:, pos:pos+self.subframe_size]
                    exc_mem[:,-self.subframe_size:] = out
                else:
                    sig_list.append(out)
                    latent_var_list.append(e_var)
                    res_corr += corr

        sig = torch.cat(sig_list, dim=1)
        latent_var = torch.stack(latent_var_list, dim=0)

        return sig, torch.mean(latent_var, 0), res_corr/(nb_frames*self.nb_subframes)

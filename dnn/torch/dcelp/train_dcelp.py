import os
import argparse
import random
import numpy as np

import torch
from torch import nn
import torch.nn.functional as F
import tqdm

import dcelp
from dataset import DCELPDataset
from stft_loss import *

torch.set_float32_matmul_precision('high')

parser = argparse.ArgumentParser()

parser.add_argument('features', type=str, help='path to feature file in .f32 format')
parser.add_argument('signal', type=str, help='path to signal file in .s16 format')
parser.add_argument('output', type=str, help='path to output folder')

parser.add_argument('--suffix', type=str, help="model name suffix", default="")
parser.add_argument('--cuda-visible-devices', type=str, help="comma separates list of cuda visible device indices, default: CUDA_VISIBLE_DEVICES", default=None)


model_group = parser.add_argument_group(title="model parameters")
model_group.add_argument('--cond-size', type=int, help="first conditioning size, default: 256", default=256)
model_group.add_argument('--gamma', type=float, help="Use A(z/gamma), default: 0.9", default=0.9)
model_group.add_argument('--softquant', action="store_true", help="enables soft quantization during training")

training_group = parser.add_argument_group(title="training parameters")
training_group.add_argument('--batch-size', type=int, help="batch size, default: 512", default=512)
training_group.add_argument('--lr', type=float, help='learning rate, default: 1e-3', default=1e-3)
training_group.add_argument('--epochs', type=int, help='number of training epochs, default: 20', default=20)
training_group.add_argument('--sequence-length', type=int, help='sequence length, default: 15', default=15)
training_group.add_argument('--lr-decay', type=float, help='learning rate decay factor, default: 1e-4', default=1e-4)
training_group.add_argument('--initial-checkpoint', type=str, help='initial checkpoint to start training from, default: None', default=None)

args = parser.parse_args()

if args.cuda_visible_devices != None:
    os.environ['CUDA_VISIBLE_DEVICES'] = args.cuda_visible_devices

# checkpoints
checkpoint_dir = os.path.join(args.output, 'checkpoints')
checkpoint = dict()
os.makedirs(checkpoint_dir, exist_ok=True)


# training parameters
batch_size = args.batch_size
lr = args.lr
epochs = args.epochs
sequence_length = args.sequence_length
lr_decay = args.lr_decay

adam_betas = [0.8, 0.95]
adam_eps = 1e-8
features_file = args.features
signal_file = args.signal

# model parameters
cond_size  = args.cond_size


checkpoint['batch_size'] = batch_size
checkpoint['lr'] = lr
checkpoint['lr_decay'] = lr_decay
checkpoint['epochs'] = epochs
checkpoint['sequence_length'] = sequence_length
checkpoint['adam_betas'] = adam_betas


device = torch.device("cuda") if torch.cuda.is_available() else torch.device("cpu")

checkpoint['model_args']    = ()
checkpoint['model_kwargs']  = {'cond_size': cond_size, 'gamma': args.gamma, 'softquant': args.softquant, 'subframe_size': 80, 'nb_subframes': 2}
print(checkpoint['model_kwargs'])
model = dcelp.DCELP(*checkpoint['model_args'], **checkpoint['model_kwargs'])
have_schedule = True

#model = dcelp.DCELP()
#model = nn.DataParallel(model)

if type(args.initial_checkpoint) != type(None):
    checkpoint = torch.load(args.initial_checkpoint, map_location='cpu')
    clean_state_dict = {}
    for k, v in checkpoint['state_dict'].items():
        clean_key = k.replace('_orig_mod.', '')
        clean_state_dict[clean_key] = v
    model.load_state_dict(clean_state_dict, strict=False)
    lr_decay = 0
    have_schedule = False

checkpoint['state_dict']    = model.state_dict()


dataset = DCELPDataset(features_file, signal_file, sequence_length=sequence_length, lookahead=0)
dataloader = torch.utils.data.DataLoader(dataset, batch_size=batch_size, shuffle=True, drop_last=True, num_workers=0)


optimizer = torch.optim.AdamW(model.parameters(), weight_decay=1e-4, lr=lr, betas=adam_betas, eps=adam_eps, fused=True)


# learning rate scheduler
scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer=optimizer, lr_lambda=lambda x : 1 / (1 + lr_decay * x))

states = None

spect_loss =  MultiResolutionSTFTLoss(device).to(device)
wass_loss =  WassersteinSTFTLoss(device).to(device)

end_window = .5+.5*torch.cos(torch.pi*torch.arange(80, device=device)/80)
start_window = 1.-end_window

nb_quant = dcelp.nb_quant

lcomp = 0.0

rate_target = torch.tensor((nb_quant-np.arange(nb_quant))*1, device=device)+5
#print(rate_target)
batch = 0

cont_weight_q = torch.tensor(3+(nb_quant-np.arange(nb_quant))/4., device=device)

if __name__ == '__main__':
    model.to(device)
    model.sig_net = torch.compile(model.sig_net, mode='reduce-overhead')
    spect_loss = torch.compile(spect_loss, mode='reduce-overhead')

    for epoch in range(1, epochs + 1):

        running_specc_loss = torch.tensor(0., device=device)
        running_specc_metric = torch.tensor(0., device=device)
        #running_wass = torch.tensor(0., device=device)
        running_cont_loss = torch.tensor(0., device=device)
        running_loss = torch.tensor(0., device=device)
        running_rate_metric = torch.tensor(0., device=device)
        running_rate_loss = torch.tensor(0., device=device)
        running_var_loss = torch.tensor(0., device=device)
        running_var_metric = torch.tensor(0., device=device)
        running_scale_reg = torch.tensor(0., device=device)

        print(f"training epoch {epoch}...")
        with tqdm.tqdm(dataloader, unit='batch') as tepoch:
            for i, (features, target) in enumerate(tepoch):
                #torch.compiler.cudagraph_mark_step_begin()
                optimizer.zero_grad()
                features = features.to(device)
                target = target.to(device)
                target = dcelp.process_audio(target)

                if (np.random.rand() > 0.1):
                    target = target[:, :sequence_length*160]
                    features = features[:,:sequence_length+4,:]
                else:
                    target=target[::2, :]
                    features=features[::2,:]

                #q = torch.randint(nb_quant+4, (features.shape[0],), device=device)
                curr_batch = features.shape[0]
                q = torch.arange(curr_batch, device=device)*(nb_quant+4)//curr_batch

                if have_schedule:
                    lambda_schedule = 1 - .8*np.exp(-batch/4000.)
                    scale_reg_weight = min(2., 5e-10*batch*batch)
                    corr_weight = .1/(1+batch/800)
                    bias_weight = 1e-5*min(1, batch/8000)
                else:
                    lambda_schedule = 1
                    scale_reg_weight = 2.
                    corr_weight = 1e-4
                    bias_weight = 1e-5
                cont_weight = cont_weight_q[torch.clamp(q, max=15)]*0.1
                batch_lambda = lambda_schedule*model.qlambda[q]
                nb_pre = 2
                pre = target[:, :nb_pre*160]
                sig, latent_var, res_corr, wsig, wtarget = model(features, target, q, target.size(1)//160 - nb_pre, pre=pre)
                error = sig - target[:, nb_pre*160:]
                error_trunk = error[...,:sig.shape[-1]//320*320]
                bias50 = torch.mean(torch.mean(torch.reshape(32768*error_trunk, (-1, 320)), 0)**2)
                bias100 = torch.mean(torch.mean(torch.reshape(32768*error, (-1, 160)), 0)**2)
                bias200 = torch.mean(torch.mean(torch.reshape(32768*error, (-1, 80)), 0)**2)
                bias = .1*bias50 + .1*bias100 + .8*bias200
                sig = torch.cat([pre, sig], -1)

                #cont_loss = torch.mean(cont_weight*dcelp.sig_loss_split(target[:, nb_pre*160:], sig[:, nb_pre*160:])/(batch_lambda**lcomp))
                cont_loss = torch.mean(cont_weight*dcelp.sig_loss_split(wtarget, wsig)/(batch_lambda**lcomp))
                cont_metric = torch.mean(dcelp.sig_loss(target[:, nb_pre*160:], sig[:, nb_pre*160:])/(batch_lambda**lcomp))

                sig = torch.cat([sig[:,:80]*start_window, sig[:,80:-80], sig[:,-80:]*end_window], -1)
                target = target.detach()
                target = torch.cat([target[:,:80]*start_window, target[:,80:-80], target[:,-80:]*end_window], -1)
                specc = spect_loss(sig, target)
                specc_loss = torch.mean(specc/(batch_lambda**lcomp))
                specc_metric = torch.mean(specc)
                #w_loss = wass_loss(sig, target)

                reg = max(0, min((batch-10000)/20000., 10))
                rate_loss = torch.mean(model.fsq.rates(q, reg)*(batch_lambda**(1-lcomp)))
                rate_metric = torch.mean(model.fsq.rate_metric(q))
                rate_low = model.fsq.rate_metric(torch.tensor(nb_quant-1, device=device))
                rate_high = model.fsq.rate_metric(torch.tensor(0, device=device))
                all_rates = model.fsq.rate_metric(torch.arange(nb_quant, device=device))
                curr_rate_target = rate_target/lambda_schedule
                rate_error = torch.mean(torch.minimum((all_rates-curr_rate_target)**2, 4*torch.abs(all_rates-curr_rate_target)))

                var_metric = torch.mean(latent_var)
                chunk_var = torch.mean(latent_var[:curr_batch//(nb_quant+4),:], 0)
                var_loss = torch.mean(torch.abs(chunk_var-.333))
                for k in range(1, nb_quant):
                    chunk_var = torch.mean(latent_var[k*curr_batch//(nb_quant+4):(k+1)*curr_batch//(nb_quant+4),:], 0)
                    var_loss = var_loss + torch.mean(torch.abs(chunk_var-.333))
                var_loss = var_loss/nb_quant
                scale_reg = model.fsq.regularizer()

                loss = cont_loss + specc_loss + rate_loss + .01*lambda_schedule*rate_error + .05*var_loss + 0*scale_reg_weight*scale_reg + bias_weight*bias - corr_weight*res_corr
                batch += 1

                greater = all_rates > curr_rate_target + .32
                lower = all_rates < curr_rate_target - .32
                lambda_adjust = 5e-3*lambda_schedule
                model.qlambda[:nb_quant] = torch.clamp(model.qlambda[:nb_quant]*(1 + lambda_adjust*greater - lambda_adjust*lower), max=1.)


                loss.backward()
                optimizer.step()

                #model.clip_weights()

                scheduler.step()

                running_specc_loss += specc_loss.detach()
                running_specc_metric += specc_metric.detach()
                #running_wass += w_loss.detach()
                running_cont_loss += cont_metric.detach()
                rate_metric = torch.sqrt(rate_error).detach()
                running_var_loss += var_loss.detach()
                running_var_metric = .9*running_var_metric + .1*var_metric.detach()
                running_scale_reg += scale_reg.detach()

                running_loss += loss.detach()
                if i%10 == 0:
                    tepoch.set_postfix(loss=f"{running_loss.cpu()/(i+1):8.5f}",
                                   cont_loss=f"{running_cont_loss/(i+1):8.5f}",
                                   bias=f"{torch.sqrt(bias):8.5f}",
                                   rerr=f"{rate_metric:8.5f}",
                                   rlow=f"{rate_low:8.5f}",
                                   rhigh=f"{rate_high:8.5f}",
                                   lmax=f"{model.qlambda[nb_quant-1].detach().cpu().item():8.5f}",
                                   mvar=f"{running_var_metric:8.5f}",
                                   reg=f"{scale_reg:8.5f}",
                                   corr=f"{res_corr:8.5f}",
                                   specc=f"{running_specc_metric/(i+1):8.5f}",
                                   #lwass=f"{running_wass/(i+1):8.5f}",
                                   )

        # save checkpoint
        checkpoint_path = os.path.join(checkpoint_dir, f'dcelp{args.suffix}_{epoch}.pth')
        checkpoint['state_dict'] = model.state_dict()
        checkpoint['loss'] = running_loss / len(dataloader)
        checkpoint['epoch'] = epoch
        torch.save(checkpoint, checkpoint_path)

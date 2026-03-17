"""
/* Copyright (c) 2022 Amazon
   Written by Jan Buethe */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
"""

import os
import argparse

import numpy as np
import torch
import tqdm

from rdovae import RDOVAE, dist_func, soft_rate_estimate


parser = argparse.ArgumentParser()

parser.add_argument('model', type=str, help='path to model')
parser.add_argument('quantizer', type=int, help='quantizer')
parser.add_argument('features', type=str, help='path to feature file in .f32 format')
parser.add_argument('output', type=str, help='output features')

parser.add_argument('--cuda-visible-devices', type=str, help="comma separates list of cuda visible device indices, default: ''", default="")


args = parser.parse_args()

# set visible devices
os.environ['CUDA_VISIBLE_DEVICES'] = args.cuda_visible_devices

device = torch.device("cuda") if torch.cuda.is_available() else torch.device("cpu")

num_features = 20
latent_dim = 80
quant_levels = 16
cond_size = 256
cond_size2 = 256

feature_file = args.features

#checkpoint['model_args']    = (num_features, latent_dim, quant_levels, cond_size, cond_size2)
#checkpoint['model_kwargs']  = {'softquant': softquant, 'adapt': adapt}
#model = RDOVAE(*checkpoint['model_args'], **checkpoint['model_kwargs'])
model = RDOVAE(num_features, latent_dim, quant_levels, cond_size, cond_size2)

checkpoint = torch.load(args.model, map_location='cpu')
model.load_state_dict(checkpoint['state_dict'], strict=False)

checkpoint['state_dict']    = model.state_dict()

features = np.reshape(np.memmap(feature_file, dtype=np.float32), (1, -1, num_features))
q_ids = args.quantizer*np.ones((1, features.shape[1]//4,), dtype=np.int32)

features = torch.tensor(features, device=device)
q_ids = torch.tensor(q_ids, device=device)

if __name__ == '__main__':

    # push model to device
    model.to(device)
    features    = features.to(device)
    q_ids       = q_ids.to(device)
    model_output = model(features, q_ids)
    z                   = model_output['z']
    outputs_hard_quant  = model_output['outputs_hard_quant']
    outputs_soft_quant  = model_output['outputs_soft_quant']
    statistical_model   = model_output['statistical_model']
    print(statistical_model['quant_scale'][0,0,:latent_dim])
    hard_rate = soft_rate_estimate(statistical_model['quant_scale'][:,:,:latent_dim], reduce=False)
    print("rate = ", np.mean(hard_rate.detach().numpy()))

    outputs_hard_quant.detach().numpy().tofile(args.output)

import torch
from torch import nn
import torch.nn.functional as F
import math

#Gemini-generated
def toeplitz_from_filter(a):
    L = a.size(-1)

    # Pad the last dimension with L-1 zeros on the left.
    # For a=, this becomes
    padded = F.pad(a, (L - 1, 0))

    # .unfold(dimension, size, step) creates a sliding window view.
    # .flip(-1) reverses the row elements to match your gather output exactly.
    return padded.unfold(-1, L, 1).flip(-1)

#Gemini version that does not modify tensors
def filter_iir_response(a, N):
    device = a.device
    L = a.size(-1)
    ar = a.flip(dims=(2,))

    # Store the sequences in a list instead of pre-allocating a tensor
    R_list = [torch.ones(a.shape[:-1], device=device)]

    for i in range(1, L):
        # Stack the collected slices on the last dimension to match previous shape
        current_R = torch.stack(R_list, dim=-1)
        new_R = -torch.sum(ar[..., L-i-1:-1] * current_R, dim=-1)
        R_list.append(new_R)

    for i in range(L, N):
        # Only take the last (L-1) elements for the sliding window
        current_R = torch.stack(R_list[-(L-1):], dim=-1)
        new_R = -torch.sum(ar[..., :-1] * current_R, dim=-1)
        R_list.append(new_R)

    return torch.stack(R_list, dim=-1)

def filter_iir_response_original(a, N):
    device = a.device
    L = a.size(-1)
    ar = a.flip(dims=(2,))
    size = (*(a.shape[:-1]), N)
    R = torch.zeros(size, device=device)
    R[:,:,0] = torch.ones((a.shape[:-1]), device=device)
    for i in range(1, L):
        R[:,:,i] = - torch.sum(ar[:,:,L-i-1:-1] * R[:,:,:i], axis=-1)
        #R[:,:,i] = - torch.einsum('ijk,ijk->ij', ar[:,:,L-i-1:-1], R[:,:,:i])
    for i in range(L, N):
        R[:,:,i] = - torch.sum(ar[:,:,:-1] * R[:,:,i-L+1:i], axis=-1)
        #R[:,:,i] = - torch.einsum('ijk,ijk->ij', ar[:,:,:-1], R[:,:,i-L+1:i])
    return R

#Gemini-generated
def batched_fir(signals, filters):
    B, L = signals.shape
    K = filters.shape[1]

    # 1. Reshape signals: (Batch, Channels, Length) -> (1, B, L)
    # We trick PyTorch into treating the batch items as independent channels
    x = signals.view(1, B, L)

    # 2. Reshape filters: (Out_Channels, In_Channels/Groups, Length) -> (B, 1, K)
    w = filters.view(B, 1, K)

    # 3. Pad the signal to match Toeplitz alignment
    # Padding the left side by K-1 ensures the output matches the shape
    # and alignment of multiplying by a lower-triangular Toeplitz matrix.
    x_padded = F.pad(x, (K - 1, 0))

    # 4. Perform grouped convolution
    # groups=B ensures channel i of the input is convolved ONLY with filter i
    out = F.conv1d(x_padded, w, groups=B)

    # 5. Restore original batch shape
    return out.view(B, -1)

def batched_iir(x, iir_mat):
    return torch.bmm(iir_mat, x[...,None])[...,0]

if __name__ == '__main__':
    #a = torch.tensor([ [[1, -.9, 0.02], [1, -.8, .01]], [[1, .9, 0], [1, .8, 0]]])
    a = torch.tensor([ [[1, -.9, 0.02], [1, -.8, .01]]])
    A = toeplitz_from_filter(a)
    print(A)
    R = filter_iir_response(a, 5)

    RA = toeplitz_from_filter(R)
    print(RA)

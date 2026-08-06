import torch

def lpc2ceps(a, fftsize):
    x = torch.nn.functional.pad(a, (0, fftsize - a.shape[-1]), mode='constant', value=0)
    X = torch.abs(torch.fft.rfft(x))**2

    # Use the natural logarithm (mathematically correct for cepstrum)
    L = -torch.log(X)

    # irfft inherently computes the correct symmetric inverse and scales by 1/fftsize
    C = torch.fft.irfft(L + 0j, n=fftsize)

    return C

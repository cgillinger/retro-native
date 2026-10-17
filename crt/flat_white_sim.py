#!/usr/bin/env python3
"""Offline check: mean linear output of crt.frag for a flat white frame (mirrors the shader maths).
Usage: flat_white_sim.py   -> prints mean output (1.0 = no brightness change) per scale/preset."""
from math import erf, sqrt, floor
import numpy as np

def cdf(x): return 0.5 * (1 + np.vectorize(erf)(x / sqrt(2)))

def sim(scale, scan, mask, bmin=0.30, bmax=0.45, lum=1.0):
    H = int(round(scale * 224)); h = 1 / scale
    sig = max(bmin + (bmax - bmin) * lum, 1e-3)
    beam = np.zeros(H)
    for k in range(H):
        y = (k + 0.5) / scale
        a = int(floor(y - 0.5))
        beam[k] = sum((cdf((y + h / 2 - (a + kk + 0.5)) / sig) - cdf((y - h / 2 - (a + kk + 0.5)) / sig)) / h
                      for kk in range(-1, 3))
    col = ((1 - scan) + scan * beam)[40 * int(scale):180 * int(scale)]   # interior lines only
    acc = []
    for px in range(3):
        stripe = np.array([px == 0, px == 1, px == 2], float)
        m = ((1 - mask) + mask * stripe) / (1 - mask + mask / 3) if mask > 0 else np.ones(3)
        acc.append(np.clip(col[:, None] * m, 0, 1))
    return float(np.mean(acc))

if __name__ == "__main__":
    for sc in (3.5, 4.82, 5.99):
        print(f"Crt     scale {sc:5.2f}: {sim(sc, 0.65, 0.0):.4f}")
    for sc in (6.0, 6.43, 10.0):
        print(f"CrtMask scale {sc:5.2f}: {sim(sc, 0.70, 0.20):.4f}")

"""Print level and decay figures for roomlab renders: peak, RMS, and the time
the energy envelope takes to fall 30 dB (x2 is a T60 estimate)."""
import sys
import numpy as np
from scipy.io import wavfile

for path in sys.argv[1:]:
    fs, x = wavfile.read(path)
    x = x.astype(np.float64)
    m = x.mean(axis=1)
    e = np.cumsum((m ** 2)[::-1])[::-1]          # Schroeder backward integral
    edb = 10 * np.log10(np.maximum(e / e[0], 1e-30))
    i30 = np.argmax(edb < -30) if (edb < -30).any() else len(edb)
    corr = np.corrcoef(x[:, 0], x[:, 1])[0, 1]
    print(f"{path}: peak {np.abs(x).max():.4g}  rms {np.sqrt((x**2).mean()):.4g}  "
          f"T30x2 {2*i30/fs:.2f}s  L/R corr {corr:.2f}")

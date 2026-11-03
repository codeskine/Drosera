import numpy as np, struct, sys
sr = 48000
def load_wav_f32(p):
    d = open(p, "rb").read()[44:]
    return np.frombuffer(d, dtype="<f4").reshape(-1, 2)
rec = np.load(sys.argv[1])                       # standalone, 20 s
off = load_wav_f32(sys.argv[2])                  # offline, 25 s
def seg(x, a, b): return x[int(a*sr):int(b*sr)]
def db(v): return 20*np.log10(max(v, 1e-12))
def partials(x, n=8):
    m = x.mean(axis=1); N = 1 << 18
    w = np.hanning(N); X = np.abs(np.fft.rfft(m[:N] * w)) * 4 / N
    f = np.fft.rfftfreq(N, 1/sr); out = []
    for i in np.argsort(X)[::-1]:
        if f[i] < 15: continue
        if all(abs(f[i]-g) > 5 for g, _ in out): out.append((f[i], db(X[i])))
        if len(out) == n: break
    return sorted(out, key=lambda t: -t[1])
def centroid(x):
    m = x.mean(axis=1); X = np.abs(np.fft.rfft(m)); f = np.fft.rfftfreq(len(m), 1/sr)
    return (f*X).sum()/X.sum()
def echo(x, lo=0.3, hi=3.0):
    e = np.abs(x.mean(axis=1)); e = e - e.mean()
    E = np.fft.rfft(e, 2*len(e)); ac = np.fft.irfft(E*np.conj(E))[:len(e)]; ac /= ac[0]
    a, b = int(lo*sr), int(hi*sr); k = a + np.argmax(ac[a:b]); return k/sr, ac[k]
def report(name, x):
    L, R = x[:,0], x[:,1]
    print(f"--- {name}: {len(x)/sr:.1f} s")
    print(f"  rms L/R {db(np.sqrt((L**2).mean())):6.1f} / {db(np.sqrt((R**2).mean())):6.1f} dBFS   peak {np.abs(x).max():.3f}   DC {L.mean():+.4f}/{R.mean():+.4f}")
    print(f"  clipped samples (|x|>=0.999): {(np.abs(x)>=0.999).sum()}   zero runs >= 5 ms: {int(np.sum(np.convolve((np.abs(L)<1e-6).astype(int), np.ones(240), 'valid')==240)>0)}")
    print(f"  L/R correlation {np.corrcoef(L, R)[0,1]:+.3f}   spectral centroid {centroid(x):.0f} Hz")
    t, c = echo(x); print(f"  envelope autocorrelation peak at {t:.3f} s (strength {c:.2f}); knob Echo Time = 0.254 s (state), PONG on")
    print("  strongest partials:", "  ".join(f"{f:.1f} Hz ({d:.1f} dB)" for f, d in partials(x)))
    rms_t = [db(np.sqrt((seg(x, s, s+1)**2).mean())) for s in range(0, int(len(x)/sr))]
    print("  rms per second:", " ".join(f"{v:.0f}" for v in rms_t))
report("STANDALONE (BlackHole)", rec)
report("OFFLINE render_cli, Default", off)

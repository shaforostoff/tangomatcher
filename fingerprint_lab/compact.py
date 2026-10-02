"""The compact fingerprint the component would embed, tested against the
full features evaluate.py uses.

A fingerprint is an excerpt of the start of the music:

  - chroma averaged into 0.25s bins, each pitch class 2 bits relative to the
    loudest one in the bin (3 bytes a bin);
  - the strongest onsets, about 4 a second, at 10ms with a 4 bit strength;
  - the duration, the tuning offset and where the music starts.

Times are the file's own; speed is dealt with at match time, as in
evaluate.py. Matching first narrows the references to those of a similar
duration and an overall chroma profile like the query's, then runs the
coarse and fine stages on those alone - nothing per reference has to be held
in memory beyond the fingerprint itself.

    py -3 compact.py --limit 800 [--excerpt 90] [--mode tuning|search|hybrid]
"""

import argparse
import csv
import os
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import scipy.fft as sfft

import evaluate as ev
import report

BIN = 0.25            # seconds per chroma bin
ONSET_RATE = float(os.environ.get("FP_RATE", 4.0))      # onsets kept per second
NOV_HOP = 0.01
SIGMA = float(os.environ.get("FP_SIGMA", 0.015))  # seconds, the width an onset is rendered at
PREFILTER = 300       # references the coarse stage sees
NFFT = 1024


class FP:
    __slots__ = ("dur", "music", "tuning", "chroma", "valid", "on_t", "on_s", "profile")


def make(t, excerpt):
    """Fingerprint of a Track (evaluate.load), cut to `excerpt` seconds."""
    f = FP()
    f.dur = t.end - t.start
    f.tuning = round(t.tuning)
    # Chroma, raw (before evaluate's normalisation): recompute from the
    # normalised frames is lossy, so re-read sqrt-chroma magnitudes.
    x = t.chroma  # centred and normalised per frame; positive part is enough
    n_src = len(x)
    start = t.start
    end = min(t.end, start + excerpt) if excerpt else t.end
    nb = int((end - start) / BIN)
    out = np.zeros((nb, 12))
    valid = np.zeros(nb, bool)
    for b in range(nb):
        i0 = int((start + b * BIN) / t.hop)
        i1 = max(i0 + 1, int((start + (b + 1) * BIN) / t.hop))
        seg = x[i0:min(i1, n_src)]
        ok = t.valid[i0:min(i1, n_src)]
        if ok.sum() == 0:
            continue
        m = seg[ok].mean(0)
        m = np.maximum(m - m.min(), 0)
        if m.max() <= 0:
            continue
        out[b] = np.round(3 * m / m.max())
        valid[b] = True
    f.chroma, f.valid = out, valid
    f.music = start
    p = out[valid].sum(0)
    f.profile = p / (np.linalg.norm(p) + 1e-9)

    # Onsets: local maxima of the novelty, the strongest ONSET_RATE a second.
    nov = t.nov
    r = t.nov_rate
    a, b_ = int(start * r), int(end * r)
    seg = nov[a:b_]
    pk = np.flatnonzero((seg[1:-1] > seg[:-2]) & (seg[1:-1] >= seg[2:]) & (seg[1:-1] > 0)) + 1
    keep = int(ONSET_RATE * (end - start))
    if len(pk) > keep:
        pk = pk[np.argsort(-seg[pk])[:keep]]
        pk.sort()
    s = seg[pk]
    f.on_t = np.round((pk + a) / r / NOV_HOP).astype(int)          # 10ms units, file time
    f.on_s = np.maximum(1, np.round(15 * s / (s.max() if len(s) else 1)))
    return f


def size_bytes(f):
    return 16 + len(f.chroma) * 3 + len(f.on_t) * 2


# ---------------------------------------------------------------- matching

def chroma_seq(f, scale, hop, k=0):
    """Fingerprint chroma on a grid of `hop` in normalised time (file time * scale)."""
    x = f.chroma.copy()
    x -= x.mean(1, keepdims=True)
    nrm = np.linalg.norm(x, axis=1)
    ok = f.valid & (nrm > 0)
    x[ok] /= nrm[ok, None]
    x[~ok] = 0
    n_src = len(x)
    if n_src == 0:
        return np.zeros((0, 12)), np.zeros(0, bool)
    src_t = (f.music + np.arange(n_src) * BIN) * scale
    n = int(src_t[-1] / hop) + 1
    grid = np.arange(n) * hop
    pos = (grid / scale - f.music) / BIN
    inside = (pos >= 0) & (pos <= n_src - 1)
    i0 = np.clip(np.floor(pos).astype(int), 0, n_src - 1)
    i1 = np.clip(i0 + 1, 0, n_src - 1)
    w = np.clip(pos - i0, 0, 1)[:, None]
    y = x[i0] * (1 - w) + x[i1] * w
    v = inside & ok[i0] & ok[i1]
    if k:
        y = np.roll(y, -k, axis=1)
    nrm = np.linalg.norm(y, axis=1)
    v &= nrm > 1e-6
    y[v] /= nrm[v, None]
    y[~v] = 0
    return y, v


def onset_curve(f, scale, t0, n):
    """Onsets rendered at 10ms on normalised time t0 + i*0.01."""
    grid = t0 + np.arange(n) * NOV_HOP
    out = np.zeros(n)
    times = f.on_t * NOV_HOP * scale
    for tt, s in zip(times, f.on_s):
        i = (tt - t0) / NOV_HOP
        lo, hi = int(i - 4 * SIGMA / NOV_HOP), int(i + 4 * SIGMA / NOV_HOP) + 1
        lo, hi = max(lo, 0), min(hi, n)
        if lo >= hi:
            continue
        idx = np.arange(lo, hi)
        out[idx] += s * np.exp(-0.5 * ((idx - i) * NOV_HOP / SIGMA) ** 2)
    return out


def ref_scale(f, mode):
    return 2.0 ** (f.tuning / 1200.0) if mode == "tuning" else 1.0


def variants(q, mode):
    if mode == "tuning":
        return [(2.0 ** (q.tuning / 1200.0) * 2.0 ** (k / 12.0), [k]) for k in (-1, 0, 1)]
    return [(s, [-1, 0, 1]) for s in np.arange(0.95, 1.0501, 0.01)]


def prefilter(refs, q, n):
    durs = np.array([r.dur for r in refs])
    prof = np.stack([r.profile for r in refs])
    ok = np.abs(durs / max(q.dur, 1) - 1) <= 0.15
    sim = np.max([prof @ np.roll(q.profile, -k) for k in (-1, 0, 1)], axis=0)
    sim[~ok] = -np.inf
    order = np.argsort(-sim)[:n]
    return [i for i in order if np.isfinite(sim[i])]


def coarse(refs, cand, q, mode):
    hop = 0.2
    max_lag = int(30 / hop)
    cols = np.r_[np.arange(0, max_lag + 1), np.arange(NFFT - max_lag, NFFT)]
    lags = np.r_[np.arange(0, max_lag + 1), np.arange(-max_lag, 0)]
    fx = np.zeros((12, len(cand), NFFT // 2 + 1), np.complex64)
    fm = np.zeros((len(cand), NFFT // 2 + 1), np.complex64)
    nv = np.zeros(len(cand))
    for j, i in enumerate(cand):
        x, ok = chroma_seq(refs[i], ref_scale(refs[i], mode), hop)
        x, ok = x[:NFFT - max_lag - 1], ok[:NFFT - max_lag - 1]
        fx[:, j] = np.conj(sfft.rfft(x.T, NFFT, axis=1))
        fm[j] = np.conj(sfft.rfft(ok.astype(float), NFFT))
        nv[j] = ok.sum()
    best = np.full(len(cand), -np.inf)
    bk = np.zeros(len(cand), int)
    bs = np.ones(len(cand))
    bl = np.zeros(len(cand))
    for s, rots in variants(q, mode):
        x, ok = chroma_seq(q, s, hop)
        x, ok = x[:NFFT - max_lag - 1], ok[:NFFT - max_lag - 1]
        fq = sfft.rfft(x.T, NFFT, axis=1)
        fqm = sfft.rfft(ok.astype(float), NFFT)
        den = sfft.irfft(fm * fqm, NFFT, axis=1)[:, cols]
        need = np.maximum(0.5 * np.minimum(nv, ok.sum())[:, None], 40)
        for k in rots:
            acc = np.zeros((len(cand), NFFT // 2 + 1), np.complex64)
            for d in range(12):
                acc += fx[d] * fq[(d + k) % 12]
            num = sfft.irfft(acc, NFFT, axis=1)[:, cols]
            sc = np.where(den >= need, num / np.maximum(den, 1), -np.inf)
            j = sc.argmax(1)
            v = sc[np.arange(len(cand)), j]
            u = v > best
            best[u], bk[u], bs[u], bl[u] = v[u], k, s, lags[j[u]] * hop
    return best, bk, bs, bl


def fine(ref, q, k, base, lag, mode):
    hop = 0.1
    sr = ref_scale(ref, mode)
    a, aok = chroma_seq(ref, sr, hop)
    best = (-np.inf, 1.0, lag)
    for e in np.linspace(0.994, 1.006, 13):
        b, bok = chroma_seq(q, base * e, hop, k)
        for d in range(-10, 11):
            l = int(round(lag / hop)) + d
            j0, j1 = max(0, -l), min(len(a), len(b) - l)
            if j1 - j0 < 60:
                continue
            m = aok[j0:j1] & bok[j0 + l:j1 + l]
            if m.sum() < 60:
                continue
            sc = float((a[j0:j1][m] * b[j0 + l:j1 + l][m]).sum() / m.sum())
            if sc > best[0]:
                best = (sc, e, l * hop)
    c, e, lagf = best
    if not np.isfinite(c):
        return c, e, lagf, np.nan
    sq = base * e
    t0 = ref.music * sr
    t1 = t0 + len(ref.chroma) * BIN * sr
    n = int((t1 - t0) / NOV_HOP)
    ra = onset_curve(ref, sr, t0, n)
    nb = int(0.08 / NOV_HOP)
    # Query curve on the reference's normalised grid, widened by the slack.
    qa = onset_curve(q, sq, t0 + lagf - nb * NOV_HOP, n + 2 * nb)
    L = int(8.0 / NOV_HOP)
    scores = []
    for s0 in range(0, n - L + 1, L):
        x = ra[s0:s0 + L]
        if x.std() < 1e-9:
            continue
        bc = -1.0
        for sh in range(2 * nb + 1):
            y = qa[s0 + sh:s0 + sh + L]
            if y.std() < 1e-9:
                continue
            bc = max(bc, float(np.corrcoef(x, y)[0, 1]))
        scores.append(bc)
    return c, e, lagf, (float(np.median(scores)) if scores else np.nan)


def identify(refs, q, mode):
    cand = prefilter(refs, q, PREFILTER)
    if not cand:
        return {}, cand
    sc, ks, ss, ls = coarse(refs, cand, q, mode)
    fin = {}
    for j in np.argsort(-sc)[:5]:
        if np.isfinite(sc[j]):
            fin[cand[j]] = fine(refs[cand[j]], q, ks[j], ss[j], ls[j], mode)
    return fin, cand


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=800)
    ap.add_argument("--excerpt", type=float, default=90)
    ap.add_argument("--mode", default="hybrid", choices=("tuning", "search", "hybrid"))
    ap.add_argument("--threads", type=int, default=8)
    args = ap.parse_args()

    rows = list(csv.DictReader(open(os.path.join(ev.DATA, "sample.tsv"), encoding="utf-8"),
                               delimiter="\t"))
    have = set(os.listdir(ev.FEAT))
    rows = [r for r in rows if r["feature"] in have]
    refs_rows = [r for r in rows if r["role"] == "ref"]
    t0 = time.time()
    refs = [make(ev.load(r["feature"]), args.excerpt) for r in refs_rows]
    sizes = [size_bytes(f) for f in refs]
    print(f"{len(refs)} reference fingerprints in {time.time() - t0:.0f}s, "
          f"{np.mean(sizes):.0f} bytes on average", file=sys.stderr)

    by_rkey = defaultdict(list)
    for i, r in enumerate(refs_rows):
        if r["rkey"]:
            by_rkey[r["rkey"]].append(i)
    queries = [r for r in rows if r["role"] != "ref"]
    for i, r in enumerate(refs_rows):
        if r["rkey"] and len(by_rkey[r["rkey"]]) > 1:
            queries.append(dict(r, role="query_tt", ref_index=i))
    rng = np.random.default_rng(1)
    queries = [queries[i] for i in sorted(rng.choice(len(queries), args.limit, replace=False))]

    def one(qr):
        # The query is fingerprinted whole: the component has the file.
        q = make(ev.load(qr["feature"]), 0)
        self_i = qr.get("ref_index")
        pool = refs
        modes = ["tuning", "search"] if args.mode == "hybrid" else [args.mode]
        fin, cand = {}, []
        for m in modes:
            f2, c2 = identify(pool, q, m)
            f2.pop(self_i, None)
            best_new = max((v[3] for v in f2.values() if np.isfinite(v[3])), default=-1)
            best_old = max((v[3] for v in fin.values() if np.isfinite(v[3])), default=-1)
            if best_new > best_old:
                fin, cand = f2, c2
            if best_old >= 0.85 or best_new >= 0.85:
                break
        truth = [i for i in by_rkey.get(qr["rkey"], []) if i != self_i] \
            if qr["role"] in ("query_same", "query_tt") else []
        out = {"feature": qr["feature"], "role": qr["role"], "rkey": qr["rkey"],
               "path": qr["path"], "true_in_prefilter": int(any(t in cand for t in truth)) if truth else ""}
        if fin:
            b = max(fin, key=lambda i: (np.nan_to_num(fin[i][3], nan=-1), fin[i][0]))
            out["fine_best"] = refs_rows[b]["rkey"]
            out["fine_best_onset"] = f"{fin[b][3]:.4f}"
            out["fine_best_chroma"] = f"{fin[b][0]:.4f}"
            out["fine_best_is_true"] = int(b in truth)
        return out

    results = []
    with ThreadPoolExecutor(args.threads) as pool:
        for n, r in enumerate(pool.map(one, queries), 1):
            results.append(r)
            if n % 100 == 0:
                print(f"{n}/{len(queries)} {time.time() - t0:.0f}s", file=sys.stderr, flush=True)
    name = f"results_compact_{args.mode}_{int(args.excerpt)}.tsv"
    cols = sorted({k for r in results for k in r})
    with open(os.path.join(ev.DATA, name), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t")
        w.writeheader()
        w.writerows(results)
    pos = [r for r in results if r["role"] in ("query_same", "query_tt")]
    print(f"true recording survives the prefilter: "
          f"{sum(r['true_in_prefilter'] == 1 for r in pos)}/{len(pos)}")
    report.report(os.path.join(ev.DATA, name))


if __name__ == "__main__":
    main()

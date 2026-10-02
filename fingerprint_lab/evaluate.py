"""Evaluate fingerprint matching on the extracted features.

Every query (a file from the FLAC or MP3 collection, or a second TangoTunes
transfer) is matched against every TangoTunes reference, and the result is
compared with what the tags say.

Matching, as proposed:

  1. Speed. Each track's time axis is multiplied by 2^(tuning_cents/1200),
     which brings every transfer of one recording to the same speed - up to a
     whole number k of semitones, since the tuning is only known modulo 100
     cents. So the query is tried at k = -1, 0, +1: chroma rotated by k, time
     scaled by 2^(k/12).
  2. Coarse: chroma at 0.2s, every reference, every offset within +/-30s, by
     FFT cross-correlation. Score = mean per-frame correlation along the best
     diagonal.
  3. Fine, for the best few: chroma at 0.1s with a small residual time scale
     searched (the tuning is measured to a few cents), then the onset novelty
     at 10ms compared block by block - the same recording has the same
     micro-timing, a re-recording does not.

    py -3 evaluate.py                  # speed from the tuning offset
    py -3 evaluate.py --mode search    # speed searched over +/-5% instead
    py -3 evaluate.py --mode raw       # baseline: no speed handling at all
    py -3 evaluate.py --limit 300      # quicker, on a fixed subset of queries

Writes data/results_<mode>.tsv and prints a summary.
"""

import argparse
import csv
import os
import struct
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import scipy.fft as sfft

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
FEAT = os.path.join(DATA, "features")

COARSE_HOP = 0.2
COARSE_FRAMES = 1600          # 320s, longer is cut
COARSE_NFFT = 2048          # >= frames + lag, so nothing wraps
COARSE_MAX_LAG = 30.0         # seconds either way
FINE_HOP = 0.1
FINE_LAG = 1.0                # seconds around the coarse lag
FINE_SCALES = np.linspace(0.994, 1.006, 13)
NOV_HOP = 0.01
BLOCK = 8.0                   # seconds per onset block
BLOCK_LAG = 0.08              # seconds a block may slide
TOP = 5                       # candidates taken to the fine stage
DURATION_SLACK = 0.15         # prefilter: normalised music duration within 15%


# ---------------------------------------------------------------- features

class Track:
    __slots__ = ("name", "dur", "tuning", "tuning_r", "hop", "nov_rate", "gate",
                 "chroma", "valid", "nov", "start", "end")


def load(name):
    b = open(os.path.join(FEAT, name), "rb").read()
    if b[:4] != b"TFP1":
        raise ValueError(name)
    t = Track()
    t.name = name
    t.dur, t.tuning, t.tuning_r, t.hop, t.nov_rate = struct.unpack_from("<5d", b, 4)
    (t.gate,) = struct.unpack_from("<f", b, 44)
    c, n = struct.unpack_from("<2i", b, 48)
    o = 56
    chroma = np.frombuffer(b, np.float32, c * 12, o).reshape(c, 12)
    o += c * 48
    rms = np.frombuffer(b, np.float32, c, o)
    o += c * 4
    t.nov = np.frombuffer(b, np.float32, n, o).astype(np.float32)
    t.valid = (rms >= t.gate) & (chroma.sum(1) > 0)
    # Compressed, centred and normalised per frame, so the frame score is a
    # correlation: unrelated frames average out near 0, not near the
    # similarity any two tonal frames have.
    x = np.sqrt(np.maximum(chroma, 0)).astype(np.float64)
    x -= x.mean(1, keepdims=True)
    nrm = np.linalg.norm(x, axis=1)
    x[nrm > 0] /= nrm[nrm > 0, None]
    x[~t.valid] = 0
    t.chroma = x
    idx = np.flatnonzero(t.valid)
    t.start = idx[0] * t.hop if len(idx) else 0.0
    t.end = idx[-1] * t.hop if len(idx) else 0.0
    return t


def chroma_grid(t, scale, hop, k=0, frames=None):
    """Chroma resampled onto a grid in normalised time: original time * scale.

    k rotates the pitch classes: a query k semitones above the reference is
    read k classes further on."""
    n_src = len(t.chroma)
    src_t = np.arange(n_src) * t.hop * scale
    n = int(src_t[-1] / hop) + 1 if n_src else 0
    if frames is not None:
        n = min(n, frames)
    grid = np.arange(n) * hop
    pos = grid / (t.hop * scale)
    i0 = np.clip(np.floor(pos).astype(int), 0, n_src - 1)
    i1 = np.clip(i0 + 1, 0, n_src - 1)
    w = (pos - i0)[:, None]
    x = t.chroma[i0] * (1 - w) + t.chroma[i1] * w
    v = t.valid[i0] & t.valid[i1]
    if k:
        x = np.roll(x, -k, axis=1)
    nrm = np.linalg.norm(x, axis=1)
    ok = v & (nrm > 1e-6)
    x[ok] /= nrm[ok, None]
    x[~ok] = 0
    return x, ok


def nov_at(t, times):
    """Novelty at original-time positions, linearly interpolated."""
    pos = times * t.nov_rate
    return np.interp(pos, np.arange(len(t.nov)), t.nov, left=0.0, right=0.0)


# ---------------------------------------------------------------- matching

class RefBank:
    """Every reference's coarse chroma, transformed once."""

    def __init__(self, refs, mode):
        self.refs = refs
        r = len(refs)
        # Conjugated and laid out pitch class first, so the cross-correlation
        # is twelve multiply-adds over (references, bins).
        self.fx = np.zeros((12, r, COARSE_NFFT // 2 + 1), np.complex64)
        self.fm = np.zeros((r, COARSE_NFFT // 2 + 1), np.complex64)
        self.nvalid = np.zeros(r)
        self.mdur = np.zeros(r)
        for i, t in enumerate(refs):
            s = ref_scale(t, mode)
            x, ok = chroma_grid(t, s, COARSE_HOP, frames=COARSE_FRAMES)
            self.fx[:, i] = np.conj(sfft.rfft(x.T, COARSE_NFFT, axis=1))
            self.fm[i] = np.conj(sfft.rfft(ok.astype(np.float64), COARSE_NFFT))
            self.nvalid[i] = ok.sum()
            self.mdur[i] = (t.end - t.start) * s


def speed_scale(t):
    return 2.0 ** (t.tuning / 1200.0)


SEARCH_SCALES = np.arange(0.95, 1.0501, 0.01)


def ref_scale(t, mode):
    return speed_scale(t) if mode == "tuning" else 1.0


def query_variants(q, mode):
    """(time scale, [pitch rotations]) to try the query at.

    tuning: speed predicted from the tuning offset, k semitones either way,
            pitch and time moving together.
    search: speed searched directly over +/-5%; pitch left to the chroma,
            which is in tune already, with a semitone either way allowed.
    raw:    as it comes."""
    if mode == "tuning":
        return [(speed_scale(q) * 2.0 ** (k / 12.0), [k]) for k in (-1, 0, 1)]
    if mode == "search":
        return [(s, [-1, 0, 1]) for s in SEARCH_SCALES]
    return [(1.0, [0])]


def coarse(bank, q, mode, prefilter):
    """Best coarse score per reference: (score, k, scale, lag_seconds)."""
    r = len(bank.refs)
    best = np.full(r, -np.inf)
    best_k = np.zeros(r, int)
    best_lag = np.zeros(r)
    best_s = np.ones(r)
    max_lag = int(COARSE_MAX_LAG / COARSE_HOP)
    lags = np.r_[np.arange(0, max_lag + 1), np.arange(-max_lag, 0)]
    cols = np.r_[np.arange(0, max_lag + 1), np.arange(COARSE_NFFT - max_lag, COARSE_NFFT)]
    for s, rots in query_variants(q, mode):
        qdur = (q.end - q.start) * s
        sel = np.arange(r)
        if prefilter:
            sel = np.flatnonzero(np.abs(bank.mdur / max(qdur, 1e-6) - 1) <= DURATION_SLACK)
            if not len(sel):
                continue
        x, ok = chroma_grid(q, s, COARSE_HOP, frames=COARSE_FRAMES)
        fq = sfft.rfft(x.T, COARSE_NFFT, axis=1).astype(np.complex64)
        fqm = sfft.rfft(ok.astype(np.float64), COARSE_NFFT).astype(np.complex64)
        all_refs = len(sel) == r
        fm = bank.fm if all_refs else bank.fm[sel]
        den = sfft.irfft(fm * fqm, COARSE_NFFT, axis=1, workers=2)[:, cols]
        need = 0.5 * np.minimum(bank.nvalid[sel], ok.sum())[:, None]
        enough = den >= np.maximum(need, 50)
        for k in rots:
            # corr[lag] = sum_t ref[t] . query[t + lag], the query read k
            # pitch classes on - rotating the transformed rows is the same
            # as rotating the chroma.
            acc = np.zeros((len(sel), fq.shape[1]), np.complex64)
            for d in range(12):
                acc += (bank.fx[d] if all_refs else bank.fx[d][sel]) * fq[(d + k) % 12]
            num = sfft.irfft(acc, COARSE_NFFT, axis=1, workers=2)[:, cols]
            score = np.where(enough, num / np.maximum(den, 1), -np.inf)
            j = score.argmax(1)
            sc = score[np.arange(len(sel)), j]
            upd = sc > best[sel]
            best[sel[upd]] = sc[upd]
            best_k[sel[upd]] = k
            best_s[sel[upd]] = s
            best_lag[sel[upd]] = lags[j[upd]] * COARSE_HOP
    return best, best_k, best_s, best_lag


def fine(ref, q, k, base, lag, mode):
    """Refine one alignment and compare the onsets along it."""
    sr = ref_scale(ref, mode)
    a, aok = chroma_grid(ref, sr, FINE_HOP)
    best = (-np.inf, 1.0, lag)
    nl = int(FINE_LAG / FINE_HOP)
    for e in FINE_SCALES:
        b, bok = chroma_grid(q, base * e, FINE_HOP, k=k)
        for d in range(-nl, nl + 1):
            l = int(round(lag / FINE_HOP)) + d
            # ref frame j against query frame j + l
            j0, j1 = max(0, -l), min(len(a), len(b) - l)
            if j1 - j0 < 100:
                continue
            m = aok[j0:j1] & bok[j0 + l:j1 + l]
            if m.sum() < 100:
                continue
            sc = float((a[j0:j1][m] * b[j0 + l:j1 + l][m]).sum() / m.sum())
            if sc > best[0]:
                best = (sc, e, l * FINE_HOP)
    chroma_score, e, lagf = best
    if not np.isfinite(chroma_score):
        return chroma_score, 1.0, lagf, np.nan, np.nan
    sq = base * e
    # Onsets along that alignment: reference normalised time tau sits at
    # query normalised time tau + lag.
    t0 = max(ref.start * sr, ref.start * sr + 0)  # music start, normalised
    t1 = ref.end * sr
    scores, shifts = [], []
    tau = t0
    nb = int(BLOCK_LAG / NOV_HOP)
    while tau + BLOCK <= t1:
        grid = tau + np.arange(int(BLOCK / NOV_HOP)) * NOV_HOP
        ra = nov_at(ref, grid / sr)
        ext = tau - nb * NOV_HOP + np.arange(int(BLOCK / NOV_HOP) + 2 * nb) * NOV_HOP
        qb = nov_at(q, (ext + lagf) / sq)
        best_c, best_s = -1.0, 0
        if ra.std() > 1e-6:
            for s in range(2 * nb + 1):
                seg = qb[s:s + len(ra)]
                if seg.std() < 1e-6:
                    continue
                c = float(np.corrcoef(ra, seg)[0, 1])
                if c > best_c:
                    best_c, best_s = c, s - nb
            scores.append(best_c)
            shifts.append(best_s)
        tau += BLOCK
    if not scores:
        return chroma_score, e, lagf, np.nan, np.nan
    onset = float(np.median(scores))
    spread = float(np.subtract(*np.percentile(shifts, [75, 25])) * NOV_HOP * 1000)
    return chroma_score, e, lagf, onset, spread


# ---------------------------------------------------------------- driver

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=("tuning", "search", "raw"), default="tuning")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-prefilter", action="store_true")
    ap.add_argument("--threads", type=int, default=6)
    args = ap.parse_args()

    rows = list(csv.DictReader(open(os.path.join(DATA, "sample.tsv"), encoding="utf-8"),
                               delimiter="\t"))
    have = set(os.listdir(FEAT))
    rows = [r for r in rows if r["feature"] in have]

    t_start = time.time()
    refs_rows = [r for r in rows if r["role"] == "ref"]
    refs = [load(r["feature"]) for r in refs_rows]
    bank = RefBank(refs, args.mode)
    print(f"{len(refs)} references in {time.time() - t_start:.0f}s", file=sys.stderr)

    by_rkey = defaultdict(list)
    for i, r in enumerate(refs_rows):
        if r["rkey"]:
            by_rkey[r["rkey"]].append(i)

    # Queries: the other collections, plus each TangoTunes file whose
    # recording TangoTunes has a second transfer of (matched against the rest).
    queries = [r for r in rows if r["role"] != "ref"]
    for i, r in enumerate(refs_rows):
        if r["rkey"] and len(by_rkey[r["rkey"]]) > 1:
            queries.append(dict(r, role="query_tt", ref_index=i))
    if args.limit:
        rng = np.random.default_rng(1)
        queries = [queries[i] for i in sorted(rng.choice(len(queries), args.limit, replace=False))]
    print(f"{len(queries)} queries", file=sys.stderr)

    def one(qr):
        q = load(qr["feature"])
        score, ks, ss, lags = coarse(bank, q, args.mode, not args.no_prefilter)
        self_i = qr.get("ref_index")
        if self_i is not None:
            score[self_i] = -np.inf
        truth = [i for i in by_rkey.get(qr["rkey"], []) if i != self_i] \
            if qr["role"] in ("query_same", "query_tt") else []
        order = np.argsort(-score)
        cands = list(order[:TOP])
        best_true = max(truth, key=lambda i: score[i]) if truth else None
        if best_true is not None and best_true not in cands and np.isfinite(score[best_true]):
            cands.append(best_true)
        fin = {}
        for i in cands:
            if np.isfinite(score[i]):
                fin[i] = fine(refs[i], q, ks[i], ss[i], lags[i], args.mode)
        rank = (int(np.flatnonzero(order == best_true)[0]) + 1
                if best_true is not None and np.isfinite(score[best_true]) else 0)
        wrong = [i for i in order[:TOP + 1] if i not in truth]
        bw = wrong[0] if wrong else None
        out = {
            "feature": qr["feature"], "role": qr["role"], "source": qr["source"],
            "rkey": qr["rkey"], "path": qr["path"],
            "tuning": f"{q.tuning:.1f}",
            "n_true": len(truth), "rank": rank,
            "best": refs_rows[order[0]]["rkey"],
            "best_coarse": f"{score[order[0]]:.4f}",
        }

        def put(prefix, i):
            if i is None or i not in fin:
                return
            c, e, l, o, sp = fin[i]
            out[prefix + "_coarse"] = f"{score[i]:.4f}"
            out[prefix + "_chroma"] = f"{c:.4f}"
            out[prefix + "_onset"] = f"{o:.4f}"
            out[prefix + "_spread_ms"] = f"{sp:.0f}"
            out[prefix + "_k"] = int(ks[i])
            # How much faster the query runs than the reference, in percent.
            if args.mode != "raw":
                ratio = ss[i] * e / ref_scale(refs[i], args.mode)
                out[prefix + "_speed_pct"] = f"{(ratio - 1) * 100:.2f}"
            out[prefix + "_ref"] = refs_rows[i]["path"]

        put("true", best_true)
        put("wrong", bw)
        # The best candidate after the fine stage, by onset then chroma.
        ranked = sorted(fin, key=lambda i: (np.nan_to_num(fin[i][3], nan=-1), fin[i][0]),
                        reverse=True)
        if ranked:
            out["fine_best"] = refs_rows[ranked[0]]["rkey"]
            out["fine_best_is_true"] = int(ranked[0] in truth)
            out["fine_best_onset"] = f"{fin[ranked[0]][3]:.4f}"
            out["fine_best_chroma"] = f"{fin[ranked[0]][0]:.4f}"
            out["fine_best_ref"] = refs_rows[ranked[0]]["path"]
        return out

    results = []
    done = 0
    with ThreadPoolExecutor(args.threads) as pool:
        for res in pool.map(one, queries):
            results.append(res)
            done += 1
            if done % 100 == 0:
                el = time.time() - t_start
                print(f"{done}/{len(queries)} {el:.0f}s", file=sys.stderr)

    name = f"results_{args.mode}{'_limit' if args.limit else ''}.tsv"
    cols = sorted({k for r in results for k in r},
                  key=lambda c: (c != "feature", c != "role", c))
    with open(os.path.join(DATA, name), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t")
        w.writeheader()
        w.writerows(results)
    print(f"-> {name}, {time.time() - t_start:.0f}s", file=sys.stderr)
    summarise(results)


def summarise(results):
    def fl(r, k):
        try:
            return float(r[k])
        except (KeyError, ValueError):
            return np.nan

    for role in ("query_same", "query_tt", "query_rerecording"):
        rs = [r for r in results if r["role"] == role]
        if not rs:
            continue
        print(f"\n== {role}: {len(rs)}")
        if role != "query_rerecording":
            ranks = Counter(min(r["rank"], 6) if r["rank"] else 0 for r in rs)
            print("  coarse rank of the true recording (0 = filtered out, 6 = 6+):",
                  dict(sorted(ranks.items())))
            fb = [r.get("fine_best_is_true") for r in rs if "fine_best_is_true" in r]
            print(f"  fine stage picks the true recording: {sum(fb)}/{len(fb)}")
            for key in ("true_coarse", "true_chroma", "true_onset", "true_spread_ms",
                        "wrong_coarse", "wrong_chroma", "wrong_onset", "wrong_spread_ms"):
                v = np.array([fl(r, key) for r in rs])
                v = v[np.isfinite(v)]
                if len(v):
                    p = np.percentile(v, [5, 25, 50, 75, 95])
                    print(f"  {key:16s} " + " ".join(f"{x:7.3f}" for x in p) + "   (p5 p25 p50 p75 p95)")
            sp = np.array([fl(r, "true_speed_pct") for r in rs])
            sp = sp[np.isfinite(sp)]
            if len(sp):
                print("  speed vs TangoTunes, %:", " ".join(
                    f"{x:6.2f}" for x in np.percentile(sp, [5, 25, 50, 75, 95])))
                print("  k:", dict(Counter(r.get("true_k") for r in rs if "true_k" in r)))
        else:
            for key in ("wrong_coarse", "wrong_chroma", "wrong_onset", "wrong_spread_ms"):
                v = np.array([fl(r, key) for r in rs])
                v = v[np.isfinite(v)]
                if len(v):
                    p = np.percentile(v, [5, 25, 50, 75, 95])
                    print(f"  {key:16s} " + " ".join(f"{x:7.3f}" for x in p) + "   (p5 p25 p50 p75 p95)")


if __name__ == "__main__":
    main()

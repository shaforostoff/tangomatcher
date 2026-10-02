"""Identify untagged files by fingerprint against the tagged collections.

    py -3 identify.py prepare E:\\Tango-Flac   # writes data/identify_extract.tsv
    build\\Release\\fp_extract.exe data\\identify_extract.tsv data\\features
    py -3 identify.py match E:\\Tango-Flac [shard shards]
    py -3 identify.py collect                # merges the shards into data/identify.tsv

Matching appends each result to data/identify_<shard>.jsonl as it is made
and skips files already there, so an interrupted run resumes; shards split
the files between processes.

References are every file in data/sources.tsv - TangoTunes and both sorted
collections - so a recording TangoTunes lacks can still be found. Each query
is matched with the speed taken from the tuning offset first, and with the
speed searched directly when that finds nothing at 0.85 or better.

For each query the result names the best reference, its tags, the onset
agreement and band (identified >= 0.85, probable >= 0.70), the speed relative
to that reference, and the best-scoring *other* recording, whose score is
the margin the identification has.
"""

import csv
import json
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

import evaluate as ev
from make_sample import feature_name

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
AUDIO = (".flac", ".m4a", ".mp3", ".wav", ".aif", ".aiff", ".ogg", ".opus")
TOP = 10


def queries_in(root):
    out = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.startswith("._") or not f.lower().endswith(AUDIO):
                continue
            out.append(os.path.join(r, f))
    return sorted(out)


def references():
    return list(csv.DictReader(open(os.path.join(DATA, "sources.tsv"), encoding="utf-8"),
                               delimiter="\t"))


def prepare(root):
    paths = [r["path"] for r in references()] + queries_in(root)
    with open(os.path.join(DATA, "identify_extract.tsv"), "w", encoding="utf-8", newline="") as f:
        for p in paths:
            f.write(f"{feature_name(p)}\t{p}\n")
    print(f"{len(paths)} files listed", file=sys.stderr)


def recording(r):
    """What makes two references the same recording, as far as tags go."""
    key = f"{r['okey']}|{r['date']}|{r['tkey']}"
    return key if r["okey"] and r["tkey"] and r["date"] else r["path"]


def match(root, threads, shard=0, shards=1):
    out_path = os.path.join(DATA, f"identify_{shard}.jsonl")
    done = set()
    if os.path.exists(out_path):
        for line in open(out_path, encoding="utf-8"):
            done.add(json.loads(line)["query"])
    have = set(os.listdir(ev.FEAT))
    rows = [r for r in references() if feature_name(r["path"]) in have]
    t0 = time.time()
    refs = [ev.load(feature_name(r["path"])) for r in rows]
    banks = {m: ev.RefBank(refs, m) for m in ("tuning", "search")}
    print(f"{len(refs)} references in {time.time() - t0:.0f}s", file=sys.stderr)
    qpaths = [p for n, p in enumerate(queries_in(root))
              if n % shards == shard and feature_name(p) in have and p not in done]
    print(f"{len(qpaths)} queries", file=sys.stderr)

    def run(q, mode):
        score, ks, ss, lags = ev.coarse(banks[mode], q, mode, True)
        fin = {}
        for i in np.argsort(-score)[:TOP]:
            if np.isfinite(score[i]):
                fin[i] = ev.fine(refs[i], q, ks[i], ss[i], lags[i], mode) + (ss[i], ks[i])
        return fin

    def onset(v):
        return -1.0 if not np.isfinite(v[3]) else v[3]

    def one(path):
        q = ev.load(feature_name(path))
        fin = run(q, "tuning")
        mode = "tuning"
        if not fin or max(onset(v) for v in fin.values()) < ev_high:
            alt = run(q, "search")
            if alt and (not fin or max(onset(v) for v in alt.values())
                        > max(onset(v) for v in fin.values())):
                fin, mode = alt, "search"
        out = {"query": path, "tuning": f"{q.tuning:.1f}", "mode": mode}
        if not fin:
            out["band"] = "none"
            return out
        order = sorted(fin, key=lambda i: (onset(fin[i]), fin[i][0]), reverse=True)
        b = order[0]
        c, e, lag, o, spread, s, k = fin[b]
        rb = rows[b]
        out.update({
            "band": "identified" if o >= ev_high else "probable" if o >= ev_low else "none",
            "onset": f"{o:.3f}", "chroma": f"{c:.3f}",
            "speed_pct": f"{(s * e / ev.ref_scale(refs[b], mode) - 1) * 100:.2f}",
            "orchestra": rb["orchestra"], "artist": rb["artist"], "title": rb["title"],
            "date": rb["date"], "genre": rb["genre"], "match": rb["path"],
            # Other references that are the same recording by their tags
            # and agree with the fingerprint too.
            "agreeing": sum(1 for i in order[1:]
                            if recording(rows[i]) == recording(rb) and onset(fin[i]) >= ev_low),
        })
        other = [i for i in order if recording(rows[i]) != recording(rb)]
        if other:
            j = other[0]
            out["runner_up_onset"] = f"{onset(fin[j]):.3f}"
            out["runner_up"] = f"{rows[j]['artist']} - {rows[j]['title']} - {rows[j]['date']}"
        return out

    with ThreadPoolExecutor(threads) as pool, open(out_path, "a", encoding="utf-8") as f:
        for n, res in enumerate(pool.map(one, qpaths), 1):
            f.write(json.dumps(res, ensure_ascii=False) + "\n")
            f.flush()
            if n % 10 == 0:
                print(f"{n}/{len(qpaths)} {time.time() - t0:.0f}s", file=sys.stderr, flush=True)


def collect():
    results = []
    for name in sorted(os.listdir(DATA)):
        if name.startswith("identify_") and name.endswith(".jsonl"):
            results += [json.loads(l) for l in open(os.path.join(DATA, name), encoding="utf-8")]
    results.sort(key=lambda r: r["query"])
    cols = ["band", "onset", "query", "orchestra", "artist", "title", "date", "genre",
            "speed_pct", "chroma", "agreeing", "runner_up_onset", "runner_up", "mode",
            "tuning", "match"]
    with open(os.path.join(DATA, "identify.tsv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t", extrasaction="ignore")
        w.writeheader()
        w.writerows(results)
    from collections import Counter
    print(Counter(r["band"] for r in results))


ev_high, ev_low = 0.85, 0.70

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "prepare" and len(sys.argv) > 2:
        prepare(sys.argv[2])
    elif cmd == "match" and len(sys.argv) > 2:
        shard, shards = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (0, 1)
        match(sys.argv[2], 4 if shards > 1 else 8, shard, shards)
    elif cmd == "collect":
        collect()
    else:
        print(__doc__)
        sys.exit(2)

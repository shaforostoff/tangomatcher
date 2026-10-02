"""Score foo_tangotagger's C++ fingerprint matching (fp_eval) on the same
queries compact.py uses, with the same decision rule.

    py -3 cpp_eval.py [--limit 800]
"""

import argparse
import csv
import os
import subprocess
import sys
from collections import defaultdict

import numpy as np

import evaluate as ev
import report

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=800)
    args = ap.parse_args()

    rows = list(csv.DictReader(open(os.path.join(ev.DATA, "sample.tsv"), encoding="utf-8"),
                               delimiter="\t"))
    have = set(os.listdir(ev.FEAT))
    rows = [r for r in rows if r["feature"] in have]
    refs_rows = [r for r in rows if r["role"] == "ref"]
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

    lst = os.path.join(ev.DATA, "cpp_eval_list.tsv")
    with open(lst, "w", encoding="utf-8", newline="") as f:
        for i, r in enumerate(refs_rows):
            f.write(f"ref\t{i}\t{r['feature']}\n")
        for j, q in enumerate(queries):
            f.write(f"query\t{j}\t{q['feature']}\t{q.get('ref_index', -1)}\n")
    exe = os.path.join(HERE, "build", "Release", "fp_eval.exe")
    res = subprocess.run([exe, lst, ev.FEAT], capture_output=True, text=True)
    sys.stderr.write(res.stderr[-400:])

    results = []
    for line in res.stdout.splitlines():
        parts = line.split("\t")
        q = queries[int(parts[0])]
        self_i = q.get("ref_index")
        truth = [i for i in by_rkey.get(q["rkey"], []) if i != self_i] \
            if q["role"] in ("query_same", "query_tt") else []
        out = {"role": q["role"], "rkey": q["rkey"], "path": q["path"]}
        if len(parts) > 1:
            rid, onset, chroma, speed, k, wander = parts[1].split()
            rid = int(rid)
            out.update({"fine_best": refs_rows[rid]["rkey"], "fine_best_onset": onset,
                        "fine_best_chroma": chroma, "speed": speed, "k": k, "wander": wander,
                        "fine_best_is_true": int(rid in truth)})
            if len(parts) > 2:
                out["runner_up_onset"] = parts[2].split()[1]
        results.append(out)
    name = os.path.join(ev.DATA, "results_cpp.tsv")
    cols = sorted({k for r in results for k in r})
    with open(name, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t")
        w.writeheader()
        w.writerows(results)
    report.report(name)


if __name__ == "__main__":
    main()

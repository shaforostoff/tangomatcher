"""How small can a fingerprint get: smaller formats emulated in fp_eval
(FP_VARIANT, see fp_eval.cpp), each scored on cpp_eval.py's 800 queries
with the component's decision rule.

    py -3 sweep.py
"""

import csv
import os
import re
import subprocess
import sys

import report

HERE = os.path.dirname(os.path.abspath(__file__))

VARIANTS = [
    ("chroma 1 bit, 3/s", "cbits=1 orate=3"),
    ("chroma 1 bit, 2.5/s", "cbits=1 orate=2.5"),
    ("chroma 1 bit, 2/s", "cbits=1 orate=2"),
    ("strength 8 levels", "slevels=8"),
    ("chroma 1 bit, 3/s, strength 8", "cbits=1 orate=3 slevels=8"),
    ("chroma 1 bit, 2.5/s, strength 8", "cbits=1 orate=2.5 slevels=8"),
]


def score(path):
    rows = list(csv.DictReader(open(path, encoding="utf-8"), delimiter="\t"))
    pos = [r for r in rows if r["role"] in ("query_same", "query_tt")]
    neg = [r for r in rows if r["role"] == "query_rerecording"]

    def band(r):
        try:
            o, w = float(r["fine_best_onset"]), float(r["wander"])
        except (KeyError, ValueError):
            return "none"
        return "identified" if o >= 0.85 and 0 <= w <= 25 else "probable" if o >= 0.70 else "none"

    ident = sum(band(r) == "identified" and report.kind(r["rkey"], r["fine_best"]) == "consistent" for r in pos)
    prob = sum(band(r) == "probable" for r in pos)
    wrong_pos = sum(band(r) == "identified" and report.kind(r["rkey"], r["fine_best"]) != "consistent" for r in pos)
    wrong_neg = [r for r in neg if band(r) == "identified" and report.kind(r["rkey"], r["fine_best"]) != "consistent"]
    prob_neg = sum(band(r) == "probable" and report.kind(r["rkey"], r["fine_best"]) != "consistent" for r in neg)
    return (100 * ident / len(pos), 100 * prob / len(pos), wrong_pos, len(wrong_neg), prob_neg,
            [os.path.basename(r["path"])[:50] for r in wrong_neg])


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    print(f"{'variant':32s} {'bytes':>6s} {'ident%':>7s} {'prob%':>6s} {'wrong+':>6s} {'wrong-':>6s} {'prob-':>6s}")
    for name, spec in VARIANTS:
        env = dict(os.environ, FP_VARIANT=spec)
        res = subprocess.run([sys.executable, os.path.join(HERE, "cpp_eval.py")], env=env,
                             capture_output=True, text=True, encoding="utf-8")
        m = re.search(r"(\d+) bytes on average", res.stderr)
        b = int(m.group(1)) if m else -1
        ident, prob, wp, wn, pn, wrong = score(os.path.join(HERE, "data", "results_cpp.tsv"))
        print(f"{name:32s} {b:6d} {ident:7.1f} {prob:6.1f} {wp:6d} {wn:6d} {pn:6d}  {'; '.join(wrong)}", flush=True)


if __name__ == "__main__":
    main()

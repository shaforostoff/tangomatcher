"""Score evaluate.py's results under the proposed decision rule.

    onset >= 0.85        identified
    0.70 <= onset < 0.85 probable - show it, let a person confirm
    onset < 0.70         not identified

The tags are the ground truth, but they are not always right, so a match is
put in one of these classes before it is counted:

    consistent    the tags agree, or disagree only in ways that cannot be a
                  different recording: the orchestra spelt another way, the
                  TangoTunes file untagged, the date off by days or months
    other year    same orchestra and title, recorded a year or more apart:
                  a re-recording, unless one of the tags is wrong
    other title   a different tune altogether

    py -3 report.py data/results_search_limit.tsv [more files...]
"""

import csv
import sys
from collections import Counter

HIGH, LOW = 0.85, 0.70


def kind(q, b):
    if not b:
        return "consistent"          # the reference is untagged
    q, b = q.split("|"), b.split("|")
    if q == b:
        return "consistent"
    same_orch = q[0] in b[0] or b[0] in q[0]
    if q[2] != b[2] or not same_orch:
        return "other title"
    if q[1][:4] == b[1][:4]:
        return "consistent"
    return "other year"


def band(onset):
    return "identified" if onset >= HIGH else "probable" if onset >= LOW else "none"


def report(path):
    rows = list(csv.DictReader(open(path, encoding="utf-8"), delimiter="\t"))
    print(f"\n### {path}")
    for group, roles in (("same recording in TangoTunes", ("query_same", "query_tt")),
                         ("only a re-recording in TangoTunes", ("query_rerecording",))):
        rs = [r for r in rows if r["role"] in roles]
        if not rs:
            continue
        c = Counter()
        for r in rs:
            try:
                onset = float(r.get("fine_best_onset", "nan"))
            except ValueError:
                onset = float("nan")
            if not onset >= LOW:
                c[("none", "")] += 1
            else:
                c[(band(onset), kind(r["rkey"], r.get("fine_best", "")))] += 1
        n = len(rs)
        print(f"  {group}: {n}")
        for (b, k), v in sorted(c.items()):
            print(f"    {b:11s} {k:12s} {v:5d}  {100 * v / n:5.1f}%")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        report(p)

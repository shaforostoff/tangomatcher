"""Compare tango_fingerprints identify (the component's logic, C++) with
identify.py's run (Python, every file of three collections as references).

    py -3 compare_identify.py <tango_fingerprints identify output>
"""

import csv
import os
import sys

from index_sources import title_key

HERE = os.path.dirname(os.path.abspath(__file__))


def same(cand, title, date):
    a, b = title_key(cand[2]), title_key(title)
    return bool(a and b and (a in b or b in a) and cand[3][:4] == date[:4])


def main(path):
    sys.stdout.reconfigure(encoding="utf-8")
    py = {r["query"]: r for r in csv.DictReader(open(os.path.join(HERE, "data", "identify.tsv"),
                                                     encoding="utf-8-sig"), delimiter="\t")}
    cc = {}
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        cc[os.path.normpath(f[0])] = f
    agree, disagree, lost = 0, [], []
    for p, f in cc.items():
        r = py.get(p)
        if r is None:
            continue
        title, date = r["title"], r["date"]
        if not title and r.get("runner_up"):
            parts = r["runner_up"].rsplit(" - ", 2)
            if len(parts) == 3:
                title, date = parts[1], parts[2]
        if f[1] in ("sound", "tags") and r["band"] == "identified":
            if same(f[2].split(" | "), title, date):
                agree += 1
            else:
                disagree.append((p, f[1], f[2], f"{r['artist']} - {title} - {date} ({r['onset']})"))
        if f[1] in ("none", "tags?", "sound?") and r["band"] == "identified":
            lost.append((p, f[1], f[2] if len(f) > 2 else "", f"{r['artist']} - {title} - {date} ({r['onset']})"))
    print(f"identified by both: {agree} agree, {len(disagree)} disagree")
    for d in disagree:
        print(f"  {d[0]}\n     C++ {d[1]}: {d[2]}\n     Py: {d[3]}")
    print(f"\nidentified by Python, not by the component: {len(lost)}")
    for d in lost:
        print(f"  {d[0]}\n     C++ {d[1]}: {d[2][:90]}\n     Py: {d[3]}")


if __name__ == "__main__":
    main(sys.argv[1])

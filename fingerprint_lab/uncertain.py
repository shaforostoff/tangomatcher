"""List the files identify.py is not certain about.

    py -3 uncertain.py E:\\Tango-Flac > data/uncertain.txt

Five groups: no match; probable (needs confirming); identified, but a
different recording scores close; identified only against untagged files;
identified, but the file name names another title.
"""

import csv
import os
import re
import sys

from index_sources import fold, title_key

HERE = os.path.dirname(os.path.abspath(__file__))


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return float("nan")


def same_title(a, b):
    a, b = title_key(a), title_key(b)
    return bool(a and b and (a in b or b in a))


def main(root):
    rows = list(csv.DictReader(open(os.path.join(HERE, "data", "identify.tsv"),
                                    encoding="utf-8-sig"), delimiter="\t"))
    prefix = root.rstrip("\\/") + os.sep

    def rel(p):
        return p[len(prefix):] if p.startswith(prefix) else p

    def runner(r):
        a = r.get("runner_up", "").rsplit(" - ", 2)
        return a if len(a) == 3 else ["", "", ""]

    def label(r):
        if r["title"]:
            return f"{r['artist']} - {r['title']} - {r['date']}"
        if num(r["runner_up_onset"]) >= 0.85 and runner(r)[1]:
            return r["runner_up"] + "   (tags from a second copy; the best match is untagged)"
        return "(an untagged reference: " + os.path.basename(r["match"]) + ")"

    none = [r for r in rows if r["band"] == "none"]
    probable = [r for r in rows if r["band"] == "probable"]
    close, untagged, named = [], [], []
    for r in rows:
        if r["band"] != "identified":
            continue
        ru = runner(r)
        if not r["title"] and not (num(r["runner_up_onset"]) >= 0.85 and ru[1]):
            untagged.append(r)
            continue
        title = r["title"] or ru[1]
        year = (r["date"] or ru[2])[:4]
        if r["title"] and num(r["runner_up_onset"]) >= 0.70 and ru[1] \
                and not (same_title(ru[1], title) and ru[2][:4] == year):
            close.append(r)
            continue
        base = os.path.splitext(os.path.basename(r["query"]))[0]
        if re.fullmatch(r"\d+\s*Track\s*\d+", base, re.I):
            continue
        name = fold(re.sub(r"^\d+[\s._-]+", "", base))
        t = title_key(title)
        if t and (t[:7] in name or name[:7] in t):
            continue
        named.append(r)

    print(f"== NO MATCH ({len(none)})")
    for r in none:
        print(f"  {r.get('onset') or '-':>5}  {rel(r['query'])}")
        if r.get("title"):
            print(f"         nearest: {r['artist']} - {r['title']} - {r['date']}")
    print(f"\n== PROBABLE, needs confirming ({len(probable)})")
    for r in probable:
        print(f"  {r['onset']}  {rel(r['query'])}\n         -> {label(r)}")
    print(f"\n== IDENTIFIED, but a different recording scores close ({len(close)})")
    for r in close:
        print(f"  {r['onset']} vs {r['runner_up_onset']}  {rel(r['query'])}\n"
              f"         -> {label(r)}\n         ?? {r['runner_up']}")
    print(f"\n== IDENTIFIED, but only against untagged files ({len(untagged)})")
    for r in untagged:
        print(f"  {r['onset']}  {rel(r['query'])}  -> {os.path.basename(r['match'])}")
    print(f"\n== IDENTIFIED, but the file name names another title ({len(named)})")
    for r in named:
        print(f"  {r['onset']}  {rel(r['query'])}\n         -> {label(r)}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main(sys.argv[1] if len(sys.argv) > 1 else r"E:\Tango-Flac")

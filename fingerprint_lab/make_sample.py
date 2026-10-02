"""Choose the files for the experiment and write the extraction list.

References: every file in TangoTunes, the best source.
Queries: every file in the other two collections that is either
  - the same recording as a TangoTunes file (same orchestra, title and full
    recording date): it should match, or
  - a recording of a title TangoTunes has by the same orchestra, but on
    another date: a re-recording, which must not match.

A recording "date" of 01-01 or 12-31 is taken to be a placeholder for an
unknown day and is not trusted to identify a recording.

Writes data/sample.tsv (the files, with their keys) and data/extract.tsv (the
list fp_extract reads).

    py -3 make_sample.py
"""

import csv
import hashlib
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
FULL = re.compile(r"^\d{4}-\d\d-\d\d$")


def feature_name(path):
    return hashlib.sha1(path.encode("utf-8")).hexdigest()[:16] + ".tfp"


def dated(r):
    d = r["date"]
    return bool(FULL.match(d)) and d[5:] not in ("01-01", "12-31", "00-00")


def main():
    rows = list(csv.DictReader(open(os.path.join(DATA, "sources.tsv"), encoding="utf-8"),
                               delimiter="\t"))
    for r in rows:
        r["rkey"] = f"{r['okey']}|{r['date']}|{r['tkey']}" if dated(r) and r["okey"] and r["tkey"] else ""

    refs = [r for r in rows if r["source"] == "tt"]
    tt_rec = {r["rkey"] for r in refs if r["rkey"]}
    tt_title = {(r["okey"], r["tkey"]) for r in refs if r["rkey"]}

    sample = []
    for r in refs:
        r["role"] = "ref"
        sample.append(r)
    for r in rows:
        if r["source"] == "tt" or not r["rkey"]:
            continue
        if r["rkey"] in tt_rec:
            r["role"] = "query_same"
        elif (r["okey"], r["tkey"]) in tt_title:
            r["role"] = "query_rerecording"
        else:
            continue
        sample.append(r)

    for r in sample:
        r["feature"] = feature_name(r["path"])

    cols = ["feature", "role", "source", "medium", "rkey", "okey", "tkey", "date",
            "duration", "title", "artist", "path"]
    with open(os.path.join(DATA, "sample.tsv"), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t", extrasaction="ignore")
        w.writeheader()
        w.writerows(sample)
    with open(os.path.join(DATA, "extract.tsv"), "w", encoding="utf-8", newline="") as f:
        for r in sample:
            f.write(f"{r['feature']}\t{r['path']}\n")

    from collections import Counter
    print(Counter(r["role"] for r in sample))


if __name__ == "__main__":
    main()

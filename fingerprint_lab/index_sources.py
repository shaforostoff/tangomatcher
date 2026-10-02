"""Index the tagged collections the fingerprints would be built from.

Writes data/sources.tsv: one row per audio file with the tags that say which
recording it is - orchestra, title, full recording date - normalised so the
same recording in two collections gets the same key.

    py -3 index_sources.py
"""

import csv
import os
import re
import sys
import unicodedata
from concurrent.futures import ThreadPoolExecutor

import mutagen

SOURCES = {
    "tt": r"C:\TangoTunes",
    "flac": r"C:\SortedTangoSpanishNamesFLAC",
    "mp3": r"C:\SortedTangoSpanishNames",
}
AUDIO = (".flac", ".m4a", ".mp3", ".wav", ".aif", ".aiff", ".ogg", ".opus")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data", "sources.tsv")


def fold(s):
    """Lower case, no accents, letters and digits only."""
    s = unicodedata.normalize("NFKD", s or "")
    s = "".join(c for c in s if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9]+", "", s.lower())


def title_key(t):
    # "(declicked mpeg)", "(decrackle)", "[remaster]" are about the transfer,
    # not the recording.
    t = re.sub(r"\([^)]*\)|\[[^\]]*\]", "", t or "")
    return fold(t)


def first(tags, key):
    v = tags.get(key)
    return v[0] if v else ""


def read(path):
    try:
        m = mutagen.File(path, easy=True)
    except Exception:
        return None
    if m is None:
        return None
    tags = m.tags or {}
    artist = first(tags, "artist")
    orchestra = first(tags, "albumartist") or artist.split(" - ")[0]
    title = first(tags, "title")
    date = first(tags, "date")
    return {
        "orchestra": orchestra,
        "artist": artist,
        "title": title,
        "date": date,
        "genre": first(tags, "genre"),
        "duration": f"{m.info.length:.2f}" if m.info and m.info.length else "",
    }


def main():
    rows = []
    for source, root in SOURCES.items():
        paths = [os.path.join(r, f) for r, _, fs in os.walk(root) for f in fs
                 if f.lower().endswith(AUDIO)]
        print(f"{source}: {len(paths)} files", file=sys.stderr)
        with ThreadPoolExecutor(16) as pool:
            for path, info in zip(paths, pool.map(read, paths)):
                if info is None:
                    continue
                info["source"] = source
                info["path"] = path
                # TangoTunes keeps the medium in the folder name.
                low = path.lower()
                info["medium"] = ("shellac" if "\\shellac" in low else
                                  "vinyl" if "\\vinyl" in low else "")
                info["okey"] = fold(info["orchestra"])
                info["tkey"] = title_key(info["title"])
                rows.append(info)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    cols = ["source", "medium", "okey", "tkey", "date", "duration",
            "orchestra", "artist", "title", "genre", "path"]
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, cols, delimiter="\t", extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"{len(rows)} rows -> {OUT}", file=sys.stderr)


if __name__ == "__main__":
    main()

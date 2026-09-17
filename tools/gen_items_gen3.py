#!/usr/bin/env python3
"""Genera romfs/data/items_gen3.json per lo Zaino (Gen3 RSE/FRLG).

Sorgenti (mai inventare ID/pocket/nomi):
- https://github.com/pret/pokeruby/master/src/data/items_en.h      (RSE base 0-348)
- https://github.com/pret/pokeemerald/master/src/data/items.h      (RSE full 0-376)
- https://github.com/pret/pokefirered/master/src/data/items.json   (FRLG 0-374)
- rust/pkm_rs_resources/src/items/gen3.rs (nomi UI, cross-check: abort se drift)

Regole (mai incoerenza):
- pocket dai disassembly; key = pocket key; max = 1 se key o HMxx, else 99
  (slot-max Gen3, uguale per RSE e FRLG).
- ruby/emerald/frlg = l'id esiste (non NONE) nella tabella di quel gioco.
  Sapphire == Ruby. Eon Ticket valido RSE+FRLG (id 275, key).
- flag = True solo per i biglietti evento (ricerca offset separata):
  EON/MYSTIC/AURORA_TICKET + OLD_SEA_MAP.
- Esclude id 0 e i filler ITEM_NONE/'???' (mai regalabili).

Uso: python3 tools/gen_items_gen3.py   (richiede rete per pret)
"""
import json
import re
import sys
import urllib.request
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUBY_URL = "https://raw.githubusercontent.com/pret/pokeruby/master/src/data/items_en.h"
EM_URL = "https://raw.githubusercontent.com/pret/pokeemerald/master/src/data/items.h"
FR_URL = "https://raw.githubusercontent.com/pret/pokefirered/master/src/data/items.json"

PKT = {
    "POCKET_ITEMS": "items",
    "POCKET_KEY_ITEMS": "key",
    "POCKET_POKE_BALLS": "balls",
    "POCKET_TM_HM": "tm",
    "POCKET_TM_CASE": "tm",
    "POCKET_BERRIES": "berries",
    "POCKET_BERRY_POUCH": "berries",
}
FLAG_ITEMS = {"ITEM_EON_TICKET", "ITEM_MYSTIC_TICKET", "ITEM_AURORA_TICKET", "ITEM_OLD_SEA_MAP"}


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8")


def parse_h(src):
    body = src[src.index("gItems[]"):]
    marks = [(m.start(), m.group(1)) for m in re.finditer(r"\.itemId\s*=\s*(\w+)", body)]
    entries = []
    for i, (mk, const) in enumerate(marks):
        prev = marks[i - 1][0] if i > 0 else 0
        names = list(re.finditer(r'\.name\s*=\s*_\("([^"]+)"\)', body[prev:mk]))
        name = names[-1].group(1) if names else None
        pm = re.search(r"\.pocket\s*=\s*(\w+)", body[mk:mk + 800])
        entries.append({"id": i, "const": const, "name": name,
                        "pocket": pm.group(1) if pm else None})
    return entries


def main():
    print("fetch pret...", flush=True)
    ruby = parse_h(fetch(RUBY_URL))
    em = parse_h(fetch(EM_URL))
    fr = json.loads(fetch(FR_URL))["items"]
    print(f"ruby={len(ruby)} emerald={len(em)} firered={len(fr)}")

    rs_src = (ROOT / "rust/pkm_rs_resources/src/items/gen3.rs").read_text(encoding="utf-8")
    names_rs = re.findall(r'name:\s*"([^"]+)",', rs_src)

    def norm(s):
        s = re.sub(r"\?+", "?", (s or "")).lower().replace("é", "e").replace("poké", "poke")
        return re.sub(r"[{}. ]", "", s)

    # gate anti-drift: i nomi emerald devono combaciare coi nostri (a meno di maiuscole)
    bad = [(e["id"], e["name"]) for e in em[1:]
           if e["id"] - 1 < len(names_rs) and norm(e["name"]) != norm(names_rs[e["id"] - 1])]
    if bad:
        print(f"ABORT: {len(bad)} nomi driftati vs gen3.rs, es: {bad[:8]}")
        sys.exit(1)

    em_by_id = {e["id"]: e for e in em}
    fr_by_id = {i: x for i, x in enumerate(fr)}
    out = []
    for idx in range(1, 377):
        e, f = em_by_id.get(idx), fr_by_id.get(idx)
        if e is None and f is None:
            continue
        const = e["const"] if e else f["itemId"]
        if const in (None, "ITEM_NONE"):
            continue
        pocket = PKT.get(e["pocket"] if e and e["pocket"] else (f["pocket"] if f else None))
        if pocket is None:
            print(f"ABORT: id={idx} {const} senza pocket noto")
            sys.exit(1)
        key = pocket == "key"
        is_hm = const.startswith("ITEM_HM")
        r_valid = e is not None and e["const"] not in (None, "ITEM_NONE")
        fr_valid = f is not None and f["itemId"] not in (None, "ITEM_NONE")
        ruby_valid = idx < len(ruby) and ruby[idx]["const"] not in (None, "ITEM_NONE")
        out.append({
            "id": idx,
            "name": names_rs[idx - 1] if idx - 1 < len(names_rs) else const,
            "pocket": pocket,
            "key": key,
            "max": 1 if (key or is_hm) else 99,
            "ruby": ruby_valid,
            "emerald": r_valid,
            "frlg": fr_valid,
            "flag": const in FLAG_ITEMS,
        })
    print("pocket:", dict(Counter(x["pocket"] for x in out)))
    print("flag:", [x["name"] for x in out if x["flag"]])
    dst = ROOT / "romfs/data/items_gen3.json"
    dst.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"voci: {len(out)} -> {dst}")


if __name__ == "__main__":
    main()

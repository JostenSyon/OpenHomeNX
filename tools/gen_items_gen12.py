#!/usr/bin/env python3
"""Genera romfs/data/items_gen1.json e items_gen2.json per lo Zaino GB.

Sorgenti (mai inventare ID/tasche/nomi):
- Tasche e liste legali: PKHeX PlayerBag1/PlayerBag2 + ItemStorage1/ItemStorage2
  (https://github.com/kwsch/PKHeX/tree/master/PKHeX.Core/Items) — trascritte sotto,
  gate anti-drift: conteggi e offset devono tornare (abort altrimenti).
- Nomi: PKHeX Resources/text/items/gen1|gen2/text_ItemsG*_en.txt (era-correct,
  id-indicizzati, verificati riga per riga contro Bulbapedia "List of items by
  index number (Generation I)": id 3 = Great Ball, HM 196-200, TM 201-255).
  NON usare rust gen1.rs/gen2.rs: gli id non tornano (es. id 3 = BrightPowder).
- Max stack: 99 (HMs -> 1: Gen1 196-200, Gen2 243-249, da ItemConverter).
- Tasca PC fuori perimetro v1 (solo borse da passeggio, come Gen3/DS).

Formati (PKHeX InventoryPouchGB, mai indovinare):
- Tasche normali: [count][id,count]* + 0xFF (Key: [count][id]* + 0xFF, count 1).
- TM Gen2: array fisso 57B (count per indice lista legale, 0 = assente).

Uso: python3 tools/gen_items_gen12.py   (richiede rete per i nomi PKHeX)
"""
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
G1_URL = "https://raw.githubusercontent.com/kwsch/PKHeX/master/PKHeX.Core/Resources/text/items/gen1/text_ItemsG1_en.txt"
G2_URL = "https://raw.githubusercontent.com/kwsch/PKHeX/master/PKHeX.Core/Resources/text/items/gen2/text_ItemsG2_en.txt"

# ---------------- liste PKHeX (trascritte, conteggi verificati) ----------------
def R(a, b): return list(range(a, b + 1))

G1_GENERAL = ([0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
               29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
               45, 46, 47, 48, 49, 51, 52, 53, 54, 55, 56, 57, 58,
               60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
               75, 76, 77, 78, 79, 80, 81, 82, 83] + R(196, 250))
G1_HM = set(R(196, 200))
# slot gestiti: Items 20, PC fuori perimetro
G1_POUCHES = {'items': (G1_GENERAL, 20)}

G2_GENERAL = ([3, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
               24, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
               40, 41, 42, 43, 44, 46, 47, 48, 49, 51, 52, 53, 57, 60, 62,
               63, 64, 65, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
               84, 85, 86, 87, 88, 89, 91, 92, 93, 94, 95, 96, 97, 98, 99,
               101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
               113, 114, 117, 118, 119, 121, 122, 123, 124, 125, 126, 131,
               132, 138, 139, 140, 143, 144, 146, 150, 151, 152, 156, 158,
               163, 167, 168, 169, 170, 172, 173, 174, 180, 181, 182, 183,
               184, 185, 186, 187, 188, 189])
G2_BALLS = [1, 2, 4, 5, 157, 159, 160, 161, 164, 165, 166]
G2_KEY = [7, 54, 55, 58, 59, 61, 66, 67, 68, 69, 71, 127, 128, 130, 133,
          134, 175, 178]
G2_KEY_CRYSTAL = G2_KEY + [70, 115, 116, 129]
G2_KEY_CRYSTAL_EXTRA = [70, 115, 116, 129]
G2_MACHINE = ([191, 192, 193, 194, 196, 197, 198, 199, 200, 201, 202, 203,
               204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
               216, 217, 218, 219, 221, 222, 223, 224, 225, 226, 227, 228,
               229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
               241, 242] + [243, 244, 245, 246, 247, 248, 249])
G2_HM = set(R(243, 249))
# slot gestiti: TMHM 57 (fisso), Items 20, Key 26, Balls 12 (PC fuori perimetro)
G2_POUCHES = {'tm': (G2_MACHINE, 57), 'items': (G2_GENERAL, None),
              'key': (G2_KEY, 26), 'balls': (G2_BALLS, 12)}
G2_POUCHES_CRYSTAL_KEY = (G2_KEY_CRYSTAL, 26)


def fetch_names(url, expect, min_id):
    import time
    raw = b''
    for attempt in range(4):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=60) as r:
                raw = r.read()
            # integrita': id max richiesto deve esserci (niente troncamenti silenti)
            encProbe = 'utf-16' if b'\x00' in raw[:100] else 'utf-8'
            txt = raw.decode(encProbe, errors='replace')
            lines = txt.split('\n')
            if len(lines) > min_id and lines[min_id].strip().strip('\x00'):
                break
            print(f'retry {attempt + 1}: risposta tronca ({len(lines)} righe)')
            raw = b''
        except Exception as e:
            print(f'retry {attempt + 1}: {e}')
            raw = b''
        time.sleep(2)
    if not raw:
        print(f'ABORT rete: {url}')
        sys.exit(1)
    # PKHeX serve questi txt a encoding variabile (UTF-16 o UTF-8 a seconda
    # di cache/edge): rileva dai NUL, mai presumere.
    enc = 'utf-16' if b'\x00' in raw[:100] else 'utf-8'
    txt = raw.decode(enc, errors='replace')
    lines = txt.split('\n')
    names = {}
    for i, ln in enumerate(lines):
        ln = ln.strip().strip('\x00').strip()
        if i == 0 or not ln:
            continue
        names[i] = ln
    # gate: id di controllo noti (Bulbapedia Gen I/II)
    for idx, want in expect:
        if names.get(idx) != want:
            print(f'ABORT nomi: id={idx} {names.get(idx)!r} != {want!r}')
            sys.exit(1)
    return names


def main():
    print('fetch nomi PKHeX...', flush=True)
    n1 = fetch_names(G1_URL, [(1, 'Master Ball'), (3, 'Great Ball'),
                              (196, 'HM01'), (200, 'HM05'),
                              (201, 'TM01'), (250, 'TM50')], 255)
    n2 = fetch_names(G2_URL, [(1, 'Master Ball'), (243, 'HM01'), (249, 'HM07')], 249)
    print(f'nomi: gen1={len(n1)} gen2={len(n2)}')

    # gate conteggi tasche
    assert len(G1_GENERAL) == 125, len(G1_GENERAL)
    assert len(G2_MACHINE) == 57, len(G2_MACHINE)
    assert len(G2_KEY) == 18 and len(G2_KEY_CRYSTAL) == 22
    assert len(G2_BALLS) == 11, len(G2_BALLS)
    # disgiunzione tasche Gen2 (un id, una tasca)
    seen = {}
    for p, (lst, _) in G2_POUCHES.items():
        for i in lst:
            if i in seen:
                print(f'ABORT Gen2: id {i} in {seen[i]} e {p}')
                sys.exit(1)
            seen[i] = p

    def tm_name(gen, idx):
        if gen == 1:
            if 196 <= idx <= 200:
                return f'HM{idx - 195:02d}'
            if 201 <= idx <= 250:
                return f'TM{idx - 200:02d}'
        else:
            if 243 <= idx <= 249:
                return f'HM{idx - 242:02d}'
            pos = G2_MACHINE.index(idx) if idx in G2_MACHINE else -1
            if 0 <= pos < 50:
                return f'TM{pos + 1:02d}'
        print(f'ABORT tm name gen{gen} id={idx}')
        sys.exit(1)

    # ---- Gen1 ----
    out1 = []
    for idx in sorted(set(G1_GENERAL) - {0}):
        nm = n1.get(idx)
        if not nm:
            print(f'ABORT Gen1: id={idx} senza nome')
            sys.exit(1)
        key = False  # Gen1: niente tasca Key (tutto in Items)
        is_hm = idx in G1_HM
        is_tm = 196 <= idx <= 250
        out1.append({
            'id': idx, 'name': tm_name(1, idx) if is_tm or is_hm else nm,
            'pocket': 'items', 'key': key,
            'max': 1 if is_hm else 99,
            'red': True, 'blue': True, 'yellow': True, 'flag': False,
        })

    # ---- Gen2 ----
    pocket_of = {}
    for p, (lst, _) in G2_POUCHES.items():
        for i in lst:
            pocket_of[i] = p
    # Extra Key solo-Cristallo (PKHeX KeyCrystal): tasca key, solo crystal.
    for i in G2_KEY_CRYSTAL_EXTRA:
        pocket_of.setdefault(i, 'key')
    out2 = []
    for idx in sorted(pocket_of):
        nm = n2.get(idx)
        if not nm:
            print(f'ABORT Gen2: id={idx} senza nome')
            sys.exit(1)
        pocket = pocket_of[idx]
        key = pocket == 'key'
        is_hm = idx in G2_HM
        is_tm = pocket == 'tm'
        if pocket == 'key':
            gold = idx in G2_KEY
            silver = idx in G2_KEY
            in_crys = idx in G2_KEY_CRYSTAL
        else:
            gold = silver = True
            in_crys = True
        out2.append({
            'id': idx, 'name': tm_name(2, idx) if is_tm or is_hm else nm,
            'pocket': pocket, 'key': key,
            'max': 1 if (key or is_hm) else 99,
            'gold': gold, 'silver': silver,
            'crystal': in_crys,
            'flag': False,
        })

    dst1 = ROOT / 'romfs/data/items_gen1.json'
    dst2 = ROOT / 'romfs/data/items_gen2.json'
    dst1.write_text(json.dumps(out1, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    dst2.write_text(json.dumps(out2, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'Gen1 voci: {len(out1)} -> {dst1}')
    print(f'Gen2 voci: {len(out2)} -> {dst2}')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Genera romfs/data/items_gen4.json e items_gen5.json per lo Zaino DS.

Sorgenti (mai inventare ID/tasche/nomi):
- Tasche e liste legali: PKHeX PlayerBag4DP/4Pt/4HGSS/5BW/5B2W2 + ItemStorage4/4DP/4Pt/4HGSS/5/5BW/5B2W2
  (https://github.com/kwsch/PKHeX/tree/master/PKHeX.Core/Items) — trascritte sotto,
  gate anti-drift: i conteggi per tasca devono tornare (abort altrimenti).
- Nomi: rust/pkm_rs_resources/src/items/modern.rs (in-repo, offline) con override d'epoca:
    433 Journal (Bulbapedia: "known as Journal prior to BDSP"; modern dice Guidebook)
    450 Bicycle (Ironmon NDS + Bulbapedia Gen IV/V; modern dice Bike)
  Se modern.rs cambiasse questi due valori, gli assert sotto abortiscono (revisione manuale).
- Max stack: PKHeX PlayerBag (999 / Key 1 / TMHM 99 Gen4, 1 Gen5; HM Gen4 -> 1 via IsItemHM4).
- Esclusi (come i filler Gen3): id 0 + PKHeX Unreleased (Gen4: 5,16,147,499,500;
  Gen5: 5,16,260-264,492-500,576) — mai regalabili; se presenti, lo scan li dice invalid.
- flag: sempre false v1 (event-flag pairing Gen4/5 = lavoro futuro; niente flag inventati).

Uso: python3 tools/gen_items_gen45.py   (offline, nessuna rete)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---------------- liste PKHeX (trascritte, conteggi verificati) ----------------
def R(a, b): return list(range(a, b + 1))

G4_GENERAL_DP = R(68, 99) + R(100, 111) + [135, 136] + R(213, 327)
G4_GENERAL_PT = G4_GENERAL_DP + [112]
G4_KEY = R(428, 464)
G4_KEY_PT = R(428, 467)
G4_KEY_HGSS = [434, 435, 437, 444, 445, 446, 447, 450, 456, 464, 465, 466,
               468, 469] + R(470, 484) + [501, 502, 503, 504] + R(532, 536)
G4_MACHINE = R(328, 419) + R(420, 427)
G4_MAIL = R(137, 148)
G4_MEDICINE = R(17, 54)
G4_BERRY = R(149, 212)
G4_BALLS_DPPT = [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
G4_BALLS_HGSS = G4_BALLS_DPPT + R(492, 500)
G4_BATTLE = R(55, 67)
G4_HM = set(R(420, 427))

G5_GENERAL = (R(1, 16) + R(55, 69) + R(70, 79) + R(80, 89) + R(90, 99) + R(100, 109)
              + [110, 111, 112, 116, 117, 118, 119] + R(135, 139) + R(140, 148)
              + R(213, 219) + R(220, 229) + R(230, 239) + R(240, 249) + R(250, 259)
              + R(260, 269) + R(270, 279) + R(280, 289) + R(290, 299) + R(300, 309)
              + R(310, 319) + R(320, 327) + R(492, 500) + [537, 538, 539]
              + R(540, 549) + R(550, 559) + R(560, 564) + [571, 572, 573, 575, 576, 577]
              + R(580, 590))
G5_KEY_BW = [437, 442, 447, 450, 465, 466, 471, 504, 533, 574, 578, 579,
             616, 617, 621, 623, 624, 625, 626]
G5_KEY_B2W2 = [437, 442, 447, 450, 453, 458, 465, 466, 471, 504, 578,
               616, 617, 621, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635,
               636, 637, 638]
G5_MACHINE = R(328, 419) + [618, 619, 620] + R(420, 425)
G5_MEDICINE = R(17, 54) + [134, 504] + R(565, 570) + [591]
G5_BERRY = R(149, 212)

G4_UNRELEASED = {5, 16, 147, 499, 500}
G5_UNRELEASED = {5, 16, 260, 261, 262, 263, 264, 492, 493, 494, 495, 496, 497,
                 498, 499, 500, 576}

# conteggi PKHeX verificati (slot gestiti per tasca)
G4_COUNTS = {  # (dp, pt, hgss)
    'items': (161, 162, 162), 'key': (37, 40, 38), 'tm': (100, 100, 100),
    'mail': (12, 12, 12), 'med': (38, 38, 38), 'berries': (64, 64, 64),
    'balls': (15, 15, 24), 'battle': (13, 13, 13),
}
G5_COUNTS = {  # (bw, b2w2)
    'items': (261, 261), 'key': (19, 27), 'tm': (101, 101),
    'med': (47, 47), 'berries': (64, 64),
}

ERA_OVERRIDE = {433: 'Journal', 450: 'Bicycle'}
ERA_ASSUME_MODERN = {433: 'Guidebook', 450: 'Bike'}


def load_modern():
    src = (ROOT / 'rust/pkm_rs_resources/src/items/modern.rs').read_text(encoding='utf-8')
    return dict((int(a), b) for a, b in
                re.findall(r'id:\s*(\d+),\s*\n\s*name:\s*"([^"]+)"', src))


def main():
    modern = load_modern()
    for idx, assume in ERA_ASSUME_MODERN.items():
        got = modern.get(idx)
        if got != assume:
            print(f'ABORT: modern.rs[{idx}]={got!r}, atteso {assume!r}: rivedere override d\'epoca')
            sys.exit(1)

    def name(idx, pocket):
        if idx in ERA_OVERRIDE:
            return ERA_OVERRIDE[idx]
        if pocket == 'tm':
            # TM/HM dal numero (come items_gen3.json: 'TM01'), mai da modern.rs
            # (l'id 427 e' un segnaposto '???' nei dati moderni; in Gen4/5 e' HM08).
            if 328 <= idx <= 419:
                return f'TM{idx - 327:02d}'
            if idx in (618, 619, 620):
                return f'TM{idx - 525:02d}'
            if 420 <= idx <= 427:
                return f'HM{idx - 419:02d}'
            print(f'ABORT: tm id={idx} fuori range noti')
            sys.exit(1)
        n = modern.get(idx)
        if not n or n == '???':
            print(f'ABORT: id={idx} senza nome in modern.rs')
            sys.exit(1)
        return n

    # ---- Gen4 ----
    pockets4 = {
        'items': (G4_GENERAL_DP, G4_GENERAL_PT, G4_GENERAL_PT),
        'key': (G4_KEY, G4_KEY_PT, G4_KEY_HGSS),
        'tm': (G4_MACHINE,) * 3,
        'mail': (G4_MAIL,) * 3,
        'med': (G4_MEDICINE,) * 3,
        'berries': (G4_BERRY,) * 3,
        'balls': (G4_BALLS_DPPT, G4_BALLS_DPPT, G4_BALLS_HGSS),
        'battle': (G4_BATTLE,) * 3,
    }
    for p, (a, b, c) in pockets4.items():
        exp = G4_COUNTS[p]
        if (len(a), len(b), len(c)) != exp:
            print(f'ABORT Gen4 tasca {p}: {(len(a), len(b), len(c))} != {exp}')
            sys.exit(1)
    seen = {}
    for p, lists in pockets4.items():
        for gi, lst in enumerate(lists):
            for i in lst:
                seen.setdefault(i, []).append((p, gi))
    dup = {i: v for i, v in seen.items() if len({p for p, _ in v}) > 1}
    if dup:
        print(f'ABORT Gen4: id in due tasche: {sorted(dup)[:10]}')
        sys.exit(1)

    out4 = []
    for idx in sorted(seen):
        if idx in G4_UNRELEASED:
            continue
        games = [any(idx in pockets4[p][gi] for p in pockets4) for gi in range(3)]
        # pocket canonica (prima che lo contiene, ordine tasche PKHeX)
        pocket = next(p for p in pockets4 if idx in pockets4[p][0] or idx in pockets4[p][1] or idx in pockets4[p][2])
        key = pocket == 'key'
        is_hm = idx in G4_HM
        out4.append({
            'id': idx, 'name': name(idx, pocket), 'pocket': pocket,
            'key': key, 'max': 1 if (key or is_hm) else (99 if pocket == 'tm' else 999),
            'dp': games[0] and idx not in G4_UNRELEASED,
            'pt': games[1] and idx not in G4_UNRELEASED,
            'hgss': games[2] and idx not in G4_UNRELEASED,
            'flag': False,
        })
    # dp/pt/hgss devono corrispondere alle liste (l'unreleased e gia' escluso sopra)
    for e in out4:
        i = e['id']
        if e['dp'] != (i in G4_GENERAL_DP or i in G4_KEY or i in G4_MACHINE or i in G4_MAIL
                       or i in G4_MEDICINE or i in G4_BERRY or i in G4_BALLS_DPPT or i in G4_BATTLE):
            print(f"ABORT Gen4 dp flag incoerente id={i}"); sys.exit(1)

    # ---- Gen5 ----
    pockets5 = {
        'items': (G5_GENERAL,) * 2,
        'key': (G5_KEY_BW, G5_KEY_B2W2),
        'tm': (G5_MACHINE,) * 2,
        'med': (G5_MEDICINE,) * 2,
        'berries': (G5_BERRY,) * 2,
    }
    for p, (a, b) in pockets5.items():
        exp = G5_COUNTS[p]
        if (len(a), len(b)) != exp:
            print(f'ABORT Gen5 tasca {p}: {(len(a), len(b))} != {exp}')
            sys.exit(1)
    seen5 = {}
    for p, lists in pockets5.items():
        for gi, lst in enumerate(lists):
            for i in lst:
                seen5.setdefault(i, []).append((p, gi))
    dup5 = {i: v for i, v in seen5.items() if len({p for p, _ in v}) > 1}
    # Unico overlap noto: 504 RageCandyBar (Bulbapedia: Gen IV Key HGSS, Gen V+ Medicine;
    # PKHeX lo accetta in entrambe). Canonica = med (tasca in gioco), mai inventare.
    if set(dup5) - {504}:
        print(f'ABORT Gen5: id in due tasche: {sorted(dup5)[:10]}')
        sys.exit(1)
    CANON5 = {504: 'med'}

    out5 = []
    for idx in sorted(seen5):
        if idx in G5_UNRELEASED:
            continue
        cands = [p for p in pockets5 if idx in pockets5[p][0] or idx in pockets5[p][1]]
        pocket = CANON5.get(idx, cands[0]) if len(cands) == 1 or idx in CANON5 else None
        if pocket is None:
            print(f'ABORT Gen5: pocket ambiguo id={idx}'); sys.exit(1)
        key = pocket == 'key'
        bw = idx in G5_GENERAL or idx in G5_KEY_BW or idx in G5_MACHINE or idx in G5_MEDICINE or idx in G5_BERRY
        b2w2 = idx in G5_GENERAL or idx in G5_KEY_B2W2 or idx in G5_MACHINE or idx in G5_MEDICINE or idx in G5_BERRY
        out5.append({
            'id': idx, 'name': name(idx, pocket), 'pocket': pocket,
            'key': key, 'max': 1 if key else (1 if pocket == 'tm' else 999),
            'bw': bw, 'b2w2': b2w2, 'flag': False,
        })

    dst4 = ROOT / 'romfs/data/items_gen4.json'
    dst5 = ROOT / 'romfs/data/items_gen5.json'
    dst4.write_text(json.dumps(out4, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    dst5.write_text(json.dumps(out5, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'Gen4 voci: {len(out4)} -> {dst4}')
    print(f'Gen5 voci: {len(out5)} -> {dst5}')


if __name__ == '__main__':
    main()

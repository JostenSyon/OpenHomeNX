# Audit gen3/4/5/6 + LGPE — Non-conformities
**Data:** 2026-09-02 | **Scope:** `pkm_rs/src/gen3/`, `pkm_rs/src/gen7_lgpe/`, `openhome_switch/src/lib.rs:220-390`, `CAPABILITY_MATRIX.md`

---

## P1 — Critical (blocks functionality)

### P1-1: Gen3 save loader è una copia sbagliata di Gen7Alola
- **File:** `pkm_rs/src/gen3/mod.rs:4` → `// mod save;` commentato
- **File:** `pkm_rs/src/gen3/save.rs:1-2` → "NOTE: this file is a misplaced copy of gen7_alola/save.rs — NOT a Gen3 save loader"
- **Impatto:** impossibile caricare un save GBA reale → nessun box lettura Gen3
- **Fix:** rimuovere la copia errata; portare `gen3/save.rs` reale da OpenHome upstream (`andrewbenington/OpenHome`)

### P1-2: `openhome_get_pokemon_from_slot` solo SwSh
- **File:** `openhome_switch/src/lib.rs:392-417`
- `match save.inner { SwSh(s) => …, Raw(_) => null }` — nessun ramo per Gen3/7/9
- **Impatto:** anche se il save venisse caricato (P1-1), nessun Pokémon estratto per generazione non-SwSh
- **Fix:** aggiungere rami `Gen7Alola(g)`, `Gen3(g)`, ecc. quando i loader arrivano

### P1-3: `openhome_load_save` solo SwSh
- **File:** `openhome_switch/src/lib.rs:189-201`
- Tenta solo `SwordShieldSave::from_bytes`, fallback → `Raw`
- **Impatto:** nessun save Gen3/7/9/1/2 riconosciuto; tutto finisce in `Raw` → `get_pokemon_from_slot` → null
- **Fix:** aggiungere tentativi per ogni gen loader disponibile

### P1-4: `openhome_get_pokemon_count` / `openhome_get_box_count` solo SwSh
- **File:** `openhome_switch/src/lib.rs:331-351` / `360-370`
- `SwSh(_) => count/32`, `Raw(_) => 0` — solo 32 box hardcoded
- **Impatto:** box count errato o zero per qualsiasi altro gen; UI mostrerà dati sbagliati
- **Fix:** numero box per gen: Gen3=14, Gen4/5=18, Gen6/7=32, Gen8=32, Gen9=32

---

## P2 — Important (missing feature)

### P2-1: Gen4/5/6 interamente mancanti
- `pkm_rs/src/` non contiene `gen4/`, `gen5/`, `gen6/` (nessun file, nessuna dir)
- CAPABILITY_MATRIX: MANCANTE
- **Impatto:** nessun PK4/PK5/PK6, nessuna conversione OHPKM, nessun save loader
- **Stima:** ~1800 LOC da portare letteralmente da upstream OpenHome

### P2-2: LGPE attivo in pkm_rs ma non collegato all'FFI
- `gen7_lgpe/mod.rs:2` → `mod save;` (non commentato, reale)
- `gen7_lgpe/save.rs` → `LetsGoSave` usa `Pb7` + `SaveData` (vero LGPE loader)
- **Manca:** ramo `SaveInner::LGPE` nell'enum, tentativo in `openhome_load_save`, ramo in `get_pokemon_from_slot`
- **Impatto:** LGPE supportato in teoria (pkm_rs), non in pratica (openhome_switch)

### P2-3: Gen9 SV save loader mancante
- CAPABILITY_MATRIX: MANCANTE (`save.rs`/`save_blocks.rs` assenti nel venduto)
- FFI solo SwSh; `Pk9` attivo per conversioni ma nessun load da save SV
- **Impatto:** impossibile aprire un save SV reale direttamente

---

## P3 — Nice-to-have (improvement)

### P3-1: Gen3 save.rs commentato — check compila su alloc
- CAPABILITY_MATRIX riporta "verificare compila su alloc" per `gen3/save.rs` Collegato-Disattivo
- Anche se fosse un reale loader Gen3, andrebbe verificato con `--target aarch64-unknown-none`

### P3-2: `openhome_load_pkm_from_gen` non supporta Gen4/5/6
- **File:** `openhome_switch/src/lib.rs:269-311`
- Match su `3,7,8,9`; `4,5,6` → null
- **Impatto:** impossibile convertire single-PK da/verso Gen4/5/6 senza il backend base
- Dipende da P2-1

---

## Riepilogo rapido

| # | Priorità | Stato | Generazione |
|---|---|---|---|
| P1-1 | CRITICO | save.rs sbagliato | Gen3 |
| P1-2 | CRITICO | FFI solo SwSh | Tutte (non-SwSh) |
| P1-3 | CRITICO | load_save solo SwSh | Tutte (non-SwSh) |
| P1-4 | CRITICO | count hardcoded 32 | Tutte (non-SwSh) |
| P2-1 | IMPORTANTE | MANCANTE | Gen4/5/6 |
| P2-2 | IMPORTANTE | non collegato | Gen7 LGPE |
| P2-3 | IMPORTANTE | MANCANTE | Gen9 SV save |
| P3-1 | MIGLIORIA | verificare compila | Gen3 |
| P3-2 | MIGLIORIA | non collegato | Gen4/5/6 (FFI) |

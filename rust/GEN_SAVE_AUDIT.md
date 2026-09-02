# Gen Save Audit — Tutte le generazioni

**Data:** 2026-09-02
**Scope:** `pkm_rs/src/gen*/save.rs`, `openhome_switch/src/lib.rs`, `source/save_file.cpp`

---

## Tabella Riepilogativa

| Gen | save.rs su disco | gate `any(wasm,alloc)` | get_mon_at | BOX_SIZE | load() usato da FFI | FEDELE/MANCANTE | file:riga |
|-----|------------------|------------------------|------------|----------|---------------------|-----------------|-----------|
| **Gen3** | ⚠️ Copia Gen7Alola | ✅ (`:4-6`) | ❌ Usa `Pk7` (sbagliato) | 80 (`mod.rs:13`) | ❌ Non collegato | **MANCANTE** (copia errata) | `save.rs:1-2` |
| **Gen4** | ❌ Non esiste | — | — | — | — | **MANCANTE** (upstream non implementato) | — |
| **Gen5** | ❌ Non esiste | — | — | — | — | **MANCANTE** (upstream non implementato) | — |
| **Gen6** | ❌ Non esiste | — | — | — | — | **MANCANTE** (upstream non implementato) | — |
| **Gen7Alola** | ✅ Reale | ✅ (`:4-6`) | ✅ `fn get_mon_at` (privato) | 232 (`mod.rs:21`) | ⚠️ Non collegato FFI | **FEDELE** | `save.rs:192` |
| **Gen7LGPE** | ✅ Reale (`LetsGoSave`) | ✅ (`:2-3`) | ✅ Tramite `Pb7` | — | ⚠️ Non collegato FFI | **FEDELE** | `save.rs:113` |
| **Gen8SwSh** | ✅ Reale | ✅ (`:1-3`) | ✅ `pub fn get_mon_at` | 344 (`mod.rs:21`) | ✅ `SaveInner::SwSh` | **FEDELE** | `save.rs:100` |
| **Gen9SV** | ❌ Commentato (`// mod save`) | — | — | 344 (`mod.rs:34`) | ❌ | **MANCANTE** (upstream same) | `mod.rs:27` |
| **Gen9LZA** | ❌ Non esiste | — | — | — | ❌ | **MANCANTE** (upstream same) | — |

---

## Dettaglio per Gen

### Gen3
- **save.rs**: Copia sbagliata di Gen7Alola (`:1-2` "NOTE: misplaced copy")
- **mod.rs:4**: `// mod save;` commentato
- **Problema**: usa `Pk7` invece di `Pk3`, logica sbagliata
- **Stato**: irrecuperabile, serve rewrite da upstream (non esiste upstream)
- **BOX_SIZE**: 80 (corretto per Gen3)

### Gen4/5/6
- **Non esistono** in upstream OpenHome (`/tmp/oh-upstream/pkm_rs/src/`)
- **Non esistono** nel vendored
- **pkHouse** li gestisce già via C++ `PokeCrypto` (crypto engine selector)
- **Stato**: MANCANTE atteso, non implementare

### Gen7Alola
- **save.rs**: Reale, funzionante
- **get_mon_at**: Privato (`fn`, non `pub fn`) — stesso upstream
- **BOX_SIZE**: 232
- **FFI**: Non collegato — `openhome_load_save` non tenta `Gen7AlolaSave::from_bytes`
- **Fallback**: `save_file.cpp:96-97` crea `saveHandleRust_` solo per SwSh; Gen7 finisce in Raw → `getCachedBox` usa PK path (`loadFromEncrypted`)
- **Stato**: FEDELE upstream, funziona via PK engine C++

### Gen7LGPE
- **save.rs**: Reale (`LetsGoSave`)
- **get_mon_at**: Tramite `Pb7` + `SaveData` trait
- **FFI**: Non collegato — nessun ramo `SaveInner::LGPE`
- **Fallback**: stessa logica Gen7 — via PK engine C++
- **Stato**: FEDELE upstream, funziona via PK engine C++

### Gen8SwSh
- **save.rs**: Reale, funzionante
- **get_mon_at**: `pub fn` (`:100`) — upstream è privato, reso pub per FFI
- **BOX_SIZE**: 344
- **FFI**: ✅ `SaveInner::SwSh` → `openhome_load_save`, `get_pokemon_from_slot`, `get_box_count`
- **Stato**: FEDELE, UNICA gen con FFI Rust completo

### Gen9SV
- **save.rs**: Commentato (`// mod save;` a `:27`)
- **Stesso upstream**: non ha save loader per SV
- **BOX_SIZE**: 344 (Pk9)
- **FFI**: Non collegato
- **Stato**: FEDELE upstream (save mancante è intenzionale)

### Gen9LZA
- **Nessun save.rs, nessun get_mon_at**
- **Stesso upstream**: solo `PlusMoveFlags`
- **Stato**: FEDELE upstream

---

## FFI Layer (openhome_switch/src/lib.rs)

### SaveInner enum (`:165`)
```rust
enum SaveInner {
    SwSh(SwordShieldSave),  // ✅ unico con supporto reale
    Raw(Vec<u8>),           // ⚠️ fallback per tutte le altre gen
}
```

### openhome_load_save (`:189-201`)
- Tenta solo `SwordShieldSave::from_bytes`
- Fallback → `SaveInner::Raw`
- **Nessun tentativo** per Gen7Alola, Gen7LGPE, Gen9SV

### openhome_get_pokemon_from_slot (`:383-417`)
- `SaveInner::SwSh(s)` → `get_mon_at` ✅
- `SaveInner::Raw(_)` → `null_mut()` ❌

### openhome_get_box_count (`:361-370`)
- `SaveInner::SwSh(_) => 32` (hardcoded)
- `SaveInner::Raw(_) => 0`

---

## C++ Fallback (save_file.cpp)

### getCachedBox (`:247-283`)
- Condizioni per OH path: `useOpenHome() && saveHandleRust_ && get_box_count > 0`
- SeQualsiasi condizione fallisce → PK fallback (`loadFromEncrypted`)
- **Gen7/LGPE/Gen9**: `saveHandleRust_` == nullptr (non creato) → PK fallback automatico

### saveHandleRust_ creation (`:96-97`)
- `if (useOpenHome()) saveHandleRust_.reset(SaveFileFFI::load(path))`
- Solo per SwSh (Rust loader restituisce null per altre gen)

---

## Conclusione

| Gen | Save Loader | FFI Rust | Fallback PK | Stato |
|-----|-------------|----------|-------------|-------|
| Gen3 | ❌ Copia errata | ❌ | ✅ | Manca upstream |
| Gen4/5/6 | ❌ Non esiste | ❌ | ✅ | Manca upstream |
| Gen7Alola | ✅ Reale | ⚠️ Non collegato | ✅ | Funziona via PK |
| Gen7LGPE | ✅ Reale | ⚠️ Non collegato | ✅ | Funziona via PK |
| **Gen8SwSh** | ✅ Reale | ✅ Completo | ✅ | **OK — pronto HW** |
| Gen9SV | ❌ Commentato | ❌ | ✅ | Manca upstream |
| Gen9LZA | ❌ Non esiste | ❌ | ✅ | Manca upstream |

**Unica gen con FFI Rust completo: Gen8SwSh.** Tutte le altre funzionano via PK engine C++ (fallback).

**Prossimo step:** test HW SwSh Crypto:OH.

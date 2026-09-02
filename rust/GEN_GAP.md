# Gen Gap Documentation

**Data:** 2026-09-02

---

## Gen7 LGPE — PRESENTE-DISATTIVO

**Status:** `pkm_rs/src/gen7_lgpe/` esiste con save loader reale (`LetsGoSave`) ma non compilabile con `--features alloc` su `aarch64-unknown-none`.

**22 errori di compilazione** quando abilitato con gate `any(wasm,alloc)`:

| # | Errore | File | Problema |
|---|---|---|---|
| 1 | `E0432` unresolved import | `save.rs:4` | `crate::encryption::decrypt_pkm_bytes_gen_6_7` non esiste nel nostro encryption module |
| 2 | `E0432` unresolved import | `save.rs:5` | `crate::encryption::unshuffle_blocks_gen_6_7` non esiste |
| 3 | `E0407` method not member | `save.rs` | `is_valid_save` non è nel trait `SaveData` |
| 4 | `E0425` cannot find type | `save.rs` | `Pokerus` non trovato in questo scope |
| 5 | `E0425` cannot find function | `save.rs` | `read_uint5_from_bits` non in `util` |
| 6 | `E0425` cannot find function | `save.rs` | `write_uint5_to_bits` non in `util` |
| 7 | `E0425` cannot find function | `save.rs` | `six_digit_trainer_display` non in `util` |
| 8 | `E0053` incompatible type | `save.rs` | `to_box_bytes` tipo diverso dal trait |
| 9 | `E0053` incompatible type | `save.rs` | `to_party_bytes` tipo diverso dal trait |
| 10 | `E0053` incompatible type | `save.rs` | `get_decrypted_mon_bytes` tipo diverso dal trait |
| 11 | `E0053` incompatible type | `save.rs` | `get_mon_at` tipo diverso dal trait |
| 12 | `E0046` missing trait items | `save.rs` | Mancano: `set_mon_at`, `convert_ohpkm`, `max_box_count`, `is_save`, `includes_origin` |
| 13 | `E0599` no associated function | `save.rs` | `Stats8::from_30_bits` non trovato |
| 14 | `E0599` no method | `save.rs` | `Stats8::write_30_bits` non trovato |
| 15-22 | Altri errori | vari | Dipendenze mancanti |

**Causa:** Il save loader LGPE usa funzioni `encryption` (`decrypt_pkm_bytes_gen_6_7`, `unshuffle_blocks_gen_6_7`) e tipi (`Pokerus`, `Stats8::from_30_bits`) che non sono stati portati nel nostro `no_std`/`alloc` build.

**Fix richiesto:** portare da upstream le funzioni encryption mancanti + i tipi mancanti. ~200-300 LOC extra.

**Stato attuale:** `lib.rs:18` → `// pub mod gen7_lgpe;` commentato. Funziona via PK engine C++ (fallback `loadFromEncrypted` in `save_file.cpp`).

---

## Gen4/5/6 — MANCANTE (atteso)

Non implementati in upstream OpenHome. pkHouse li gestisce già via C++ `PokeCrypto`.

---

## Gen3 — PRESENTE-DISATTIVO

`gen3/save.rs` è una copia sbagliata di Gen7Alola. `mod save;` commentato in `mod.rs:4`. Serve rewrite da upstream (non esiste upstream).

---

## Verifica Build

- `cargo check -p pkm_rs --target aarch64-unknown-none --features alloc` → verde (153 warnings, 0 errori) ✅
- `cargo test -p openhome_switch --features std` → 28/28 pass ✅

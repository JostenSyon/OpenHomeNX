# PB8 Spec — BDSP (Brilliant Diamond / Shining Pearl)

Recon read-only: confronto upstream TS (`/tmp/oh-upstream/src/core/pkm/PB8.ts` vs `PK4.ts`) e Rust (`rust/pkm_rs/src/gen4/pk4.rs`, `ohpkm/convert/pk4.rs`, `ohpkm/v2_sections/pkm_bytes.rs`, `src/lib.rs`).

## 1) Dimensioni box / party

- **PB8**: `getBoxSize()=344` (`PB8.ts:36`, `pkm_bytes.rs:21 PB8_SIZE=344`). Nessuna distinzione box/party in TS: PB8 ha un unico record 344 usato sia per box che per party (es. `toBytes()` crea `ArrayBuffer(344)`). Rust `StoredPkmBytes::Pb8([u8;344])` e `Tag::Pb8=11` già presenti in `pkm_bytes.rs:45,131,155`.
- **PK4**: `BOX_SIZE=136`, `PARTY_SIZE=236` (TS `toBytes(options?.includeExtraFields ? 236 : 136)`, Rust `pk4.rs:56-57`). Layout a blocchi Gen4 con shuffle `encryption.decryptByteArrayGen45` / `shuffleBlocksGen45`. Checksum span `0x08..0x87` (TS `calculateChecksum() { get16BitChecksum(...,0x08,0x87)}`) vs PB8 `0x08..0x148`.

> Implica: PB8 non è un'estensione di PK4; è un Gen8-like (stessi 344 byte di PK8/PA8/PK9/PA9).

## 2) Tag id da usare

- `pkm_bytes.rs:45` `Tag::Pb8 = 11` (già allocato). `PkmFormat::PB8 -> Tag::Pb8` (`pkm_bytes.rs:107`). Data size `PB8_SIZE=344`.
- Da usare per `StoredPkmBytes::Pb8`, `OriginalBackup` e `UnconvertedPkm`. Non richiede nuovo tag.

## 3) Offset / campi che differiscono da PK4

PB8 è quasi 1:1 con **PK8** (`gen8_swsh`), non con PK4. Confronto sintetico:

| Area | PK4 (0x00..0x87) | PB8 (0x00..0x148) |
|---|---|---|
| **Header** | `personality`@0x00, `sanity`@0x04, `checksum`@0x06, `ndex`@0x08 | `encryptionConstant`@0x00, `sanity` zero@0x04, `checksum`@0x06, `ndex`@0x08 (stesso) |
| **OT / EXP** | `trainerID`@0x0C, `secretID`@0x0E, `exp`@0x10 | identico |
| **Ability/Markings** | `ability` u8@0x15, `markings` 6-shape-no-color@0x16 | `ability` u16@0x14, `abilityNum`@0x16, `favorite`/`canGigantamax` bit@0x16, `markingsSixShapesWithColor`@0x18 |
| **EC/PID/Nature** | `personality` già a 0x00 (unica), `nature` da `personality%25` | `personalityValue`@0x1C, `nature`@0x20, `mintNature`@0x21, `isFateful` bit@0x22 |
| **Form/Gender** | `gender` bits@0x40, `formIndex` bits@0x40, `shinyLeaves`@0x41 | `gender` bits@0x22, `formIndex`@0x24, `evs`@0x26, `dynamaxLevel`@0x90, `palma`@0x98 |
| **Moves** | `moves`@0x28, `pp`@0x30, `ppUps`@0x34 | `moves`@0x72, `pp`@0x7A, `ppUps`@0x7E, `relearnMoves`@0x82 |
| **IVs/Egg** | `ivs` 30-bit@0x38 + flags egg/nick@0x38 | `ivs`@0x8C + flags egg/nick@0x8C, `heightScalar`@0x50, `weightScalar`@0x51 |
| **Nick/Trainer** | Gen4 string (12/8 chars, Gen4 encoding) @0x48/@0x68 | UTF-16 12 chars each: `nickname`@0x58, `handlerName`@0xA8, `trainerName`@0xF8 |
| **Ribbons** | 3 blocchi `Gen4Ribbons` @0x24/0x3C/0x60 | 2 blocchi `ModernRibbons` @0x34 (64) + @0x40 (47) |
| **Met/Origin** | `eggLocation DP`@0x7E, `metLocation DP`@0x80, `egg/met PtHGSS`@0x44/0x46, `gameOfOrigin`@0x5F, `ballDPPt`@0x83/`ballHGSS`@0x86, `metLevel`@0x84 | `eggLocationInternal`@0x120, `metLocation`@0x122, `gameOfOrigin`@0xDE, `ball`@0x124, `metLevel` bits@0x125, `homeTracker` u64@0x135, `hyperTraining`@0x126, `trainerGender` bit@0x125, `isCurrentHandler` bit@0xC4 |
| **Extra party** | `statusCondition`@0x88, `currentHP`@0x8E (solo se party 236) | `statusCondition`@0x94, `sociability`@0x48, `fullness`@0xDC, `enjoyment`@0xDD, `stats` u16@0x14A, `level`@0x148, `handlerMemory`@0xC9, `trainerMemory`@0x113 |
| **Encryption** | `decryptByteArrayGen45` + `shuffleBlocksGen45` | `decryptByteArrayGen89` + `shuffleBlocksGen89` (uguale a PK8) |
| **Checksum** | sum `0x08..0x87` | sum `0x08..0x148` |

In sintesi: **nessun campo PK4 riusabile tal quale**; PB8 va trattato come PK8-like.

## 4) Serve `pb8_buffer.rs` separato?

- **Sì.** PK4 buffer non esiste ancora (stub in `pk4.rs`) e comunque ha size/offsets incompatibili (136 vs 344). PB8 riusa **lo stesso layout di PK8**: verificare se `pk8_buffer.rs` può essere parametrizzato o duplicato col nome `pb8_buffer.rs`. Gli offset PB8 elencati sopra sono identici a PK8 tranne piccole differenze BDSP (es. `sociability`@0x48 già presente in PK8, `heightScalar`/`weightScalar` etc.). La via più pulita è **duplicare `pk9_buffer.rs`/`pk8_buffer.rs` in `pb8_buffer.rs`** con le costanti `Offset` prese da `PB8.ts`, oppure estrarre un trait comune Gen8. Non riusare `pk4.rs`.

## 5) Metodi `OhpkmConvert` da implementare

`ohpkm/convert/pk4.rs` già implementa `to_main_data()`, `from_ohpkm()`, `bytes_to_stored()`. Per PB8 serve `impl OhpkmConvert for Pb8` in nuovo `ohpkm/convert/pb8.rs` con:

- `to_main_data() -> MainDataV2` (mapping verso `ohpkm::v2_sections::main_data` + `gen45_data`/`gen67_data`/`swsh_data`/`sv_data` a seconda dei campi PB8)
- `to_gen_45_data()` / `to_gen_67_data()` / `to_swsh_data()`/`to_sv_data()` se presenti (verificare quali sezioni PB8 deve popolare; BDSP è Gen8, quindi `swsh_data` con `canGigantamax`/`dynamaxLevel`/`palma`/`tr_flags` vanno propagati come in `pk8.rs`)
- `from_ohpkm(&OhpkmV2, ConvertStrategy) -> Result<Self>` (usa `PkmConverter::new(PkmFormat::PB8, strategy)` come fa `PB8.ts:218`)
- `bytes_to_stored(bytes: &[u8]) -> Result<StoredPkmBytes>` (BOX 344 → `StoredPkmBytes::Pb8`)
- Registrare il modulo in `ohpkm/convert.rs` e in `ohpkm/mod.rs` se serve.

Dipendenze OHPKM già pronte: `pkm_bytes::Tag::Pb8`, `StoredPkmBytes::Pb8`, `PkmFormat::PB8`.

## 6) Gate feature `newgen`

- `rust/pkm_rs/Cargo.toml:52` `newgen = []` (WIP Gen1/2/4/5/6).
- `rust/pkm_rs/src/lib.rs:21-25`:
  ```rust
  #[cfg(all(feature = "newgen", any(wasm,alloc)))] pub mod gen4;
  #[cfg(all(feature = "newgen", any(wasm,alloc)))] pub mod gen5;
  #[cfg(all(feature = "newgen", any(wasm,alloc)))] pub mod gen6;
  ```
  **Sì**: `gen4::Pk4` e `ohpkm::convert::pk4` sono dietro `newgen`. PB8 è BDSP (Gen8) ma storicamente è stato importato sotto lo stesso gate? In TS `PB8.ts` è core indipendente; in Rust `pkm_bytes.rs` ha già `PB8_SIZE` e `Tag::Pb8` fuori da gate, ma il modulo `gen8` SwSh è fuori da `newgen`. Per coerenza **PB8 dovrebbe essere fuori da `newgen`** (come `gen8_swsh`, `gen8_la`, `gen9_lza`) o al massimo dietro `newgen` temporaneamente se si vuole isolare il WIP BDSP fino al landing del buffer. Verificare `gen8`/`gen9` non sono gated; quindi creare `gen8_bdsp` o `gen4_bdsp` fuori da `newgen`.

## 7) Prossimi step per BDSP

1. Creare `rust/pkm_rs/src/gen8_bdsp/{mod.rs,pb8_buffer.rs,pb8.rs}` (o `rust/pkm_rs/src/gen4/pb8.rs` se si vuole restare sotto `gen4` ma fuori da gate) duplicando `pk8_buffer.rs` con offset PB8.
2. Implementare `PkmBytes` per `Pb8` (`from_bytes`, `write_box_bytes`, `to_box_bytes`, `is_empty_slot`, `calculate_checksum` con span `0x08..0x148`).
3. Aggiungere `ohpkm/convert/pb8.rs` con `OhpkmConvert` come sopra e registrarlo.
4. Esporre in FFI `openhome_switch/src/lib.rs` i `gen` 4→PB8 (già Tag 11 esiste) se serve un id distinto da PK4 (4). Non riusare `4` per PB8.
5. Test round-trip come per PA8/PA9: `hex_to_vec(PB8_BDSP) -> Pb8::from_bytes -> to_box_bytes` con esclusione `0x04..0x08` (sanity+checksum) e OHPKM round-trip su core fields.


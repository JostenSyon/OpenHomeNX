# Upstream Diff: OpenHome 1.16 → 1.17

**Data analisi:** 2026-09-02
**Upstream:** `andrewbenington/OpenHome` (1.17.0)
**Vendored:** `rust/pkm_rs` (1.16.0)

---

## Riepilogo

| Area | Stato | Rischio |
|------|-------|---------|
| `ohpkm/id.rs` | **MANCANTE** | Alto — OpenHomeId è nuovo in 1.17 |
| `ohpkm/v2.rs` | **DIVERGE** | Alto — `regenerate_openhome_id` mancante |
| `sectioned_data.rs` | **DIVERGE** | Medio — API `from_bytes` cambiata |
| `convert/pk9.rs` | **DIVERGE** | Medio — `openhome_id` inizializzazione |
| `gen3/` | **FEDELE** (solo gate) | Basso |
| `gen7_alola/` | **FEDELE** (solo gate) | Basso |
| `gen7_lgpe/` | **FEDELE** (solo gate) | Basso |
| `gen8_swsh/` | **FEDELE** (solo gate) | Basso |
| `gen9_sv/` | **FEDELE** (solo gate) | Basso |
| `gen9_lza/` | **FEDELE** (solo gate) | Basso |
| `convert/pk3.rs` | **FEDELE** (solo gate) | Basso |
| `convert/pk7.rs` | **FEDELE** (solo gate) | Basso |
| `convert/pk8.rs` | **FEDELE** (solo gate) | Basso |

---

## Dettaglio Divergenze

### 1. `ohpkm/id.rs` — MANCANTE (Alto rischio)

**Upstream:** File nuovo in 1.17, definisce `OpenHomeId` (struct con `national_dex`, `trainer_id`, `secret_id`, `personality_value`). Implementa `Display`, `FromStr`, `Serialize/Deserialize`, `TryFromBytes`, `ToBytes`.

**Vendored:** File non esiste. Il nostro `openhome_id()` ritorna `String` (formato legacy), non `OpenHomeId`.

**Impatto:** `OpenHomeId` è usato in `MainDataV2` (upstream `:47`), `convert/pk9.rs` (`:23`), e `v2.rs` (`:273`). Senza questo tipo, il formato OHPKM potrebbe essere incompatibile con 1.17.

**Azione richiesta:** Creare `ohpkm/id.rs` con `OpenHomeId` adattato per `no_std`/`alloc`.

### 2. `ohpkm/v2.rs` — DIVERGE (Alto rischio)

**Upstream (`v2.rs:252-253`):**
```rust
ohpkm.regenerate_openhome_id();
ohpkm.sync_learned_moves();
```

**Vendored (`v2.rs:244-250`):**
```rust
Self { ... }
// manca regenerate_openhome_id() e sync_learned_moves()
```

**Differenze specifiche:**
- `regenerate_openhome_id()` — **completamente mancante** nel vendored
- `sync_learned_moves()` — **esiste** nel vendored (`:1896`) ma non chiamato in `convert_without_backup`
- `openhome_id()` — upstream ritorna `OpenHomeId`, vendored ritorna `String`

**Impatto:** Le OHPKM convertite senza `regenerate_openhome_id` potrebbero avere ID non validi.

### 3. `sectioned_data.rs` — DIVERGE (Medio rischio)

**Upstream (`:208`):**
```rust
pub fn from_bytes(bytes: &[u8], expected_magic_number: u32) -> Result<Self> {
    // magic number check inside
}
```

**Vendored (`:208`):**
```rust
pub fn from_bytes(bytes: &[u8]) -> Result<Self> {
    // magic number check esterno in v2.rs
}
```

**Impatto:** L'API è cambiata. Il vendored gestisce il magic number esternamente in `v2.rs` (`:1620-1622`). Funziona, ma è un drift API.

### 4. `convert/pk9.rs` — DIVERGE (Medio rischio)

**Upstream (`:14`, `:23-28`):**
```rust
use crate::ohpkm::id::OpenHomeId;
// ...
openhome_id: OpenHomeId::new(
    self.species_and_form.into_inner().get_ndex(),
    self.trainer_id,
    self.secret_id,
    self.personality_value,
),
```

**Vendored:** Mancante. `openhome_id` non inizializzato in `from_ohpkm`.

**Impatto:** Le Pk9 convertite da OHPKM non avranno `openhome_id` corretto.

### 5. Per-Gen — FEDELE (Basso rischio)

Tutte le differenze per-gen sono **solo gate** (`#[cfg(feature = "wasm")]` → `#[cfg(any(feature = "wasm", feature = "alloc"))]`). La logica è identica.

| File | Differenze |
|------|-----------|
| `gen3/pk3.rs` | Gate `alloc` aggiunti ✅ |
| `gen3/mod.rs` | Solo fine file |
| `gen7_alola/pk7.rs` | Gate `alloc` aggiunti ✅ |
| `gen7_alola/mod.rs` | Solo fine file |
| `gen7_lgpe/pb7.rs` | Gate `alloc` aggiunti ✅ |
| `gen7_lgpe/save.rs` | Gate `alloc` aggiunti ✅ |
| `gen8_swsh/mod.rs` | `pub mod save/save_blocks` (nostra modifica) |
| `gen9_sv/mod.rs` | Gate `alloc` aggiunti ✅ |
| `gen9_lza/mod.rs` | Gate `alloc` + `wasm_bindgen` adattati ✅ |

---

## Dipendenze Mancanti in 1.17

Upstream 1.17 aggiunge:
- `arrayref` — usato da `OpenHomeId::try_from_bytes`
- `specta::Type` — usato da `OpenHomeId` derive

Vendored 1.16 non ha queste dipendenze.

---

## Proposta

### Opzione A: Re-vendor 1.17 completo (Rischio: Alto)
Portare tutti i file aggiornati. Richiede:
1. Aggiungere `id.rs` con `no_std`/`alloc`
2. Aggiornare `v2.rs` con `regenerate_openhome_id`
3. Aggiornare `sectioned_data.rs` con nuova API
4. Aggiornare `convert/pk9.rs`
5. Aggiungere dipendenze `arrayref`, `specta`
6. Testare tutti i gen

### Opzione B: Portare solo le parti critiche (Rischio: Medio)
1. Creare `id.rs` con `OpenHomeId` (senza `specta`, senza WASM)
2. Aggiungere `regenerate_openhome_id` a `v2.rs`
3. Chiamare `regenerate_openhome_id` + `sync_learned_moves` in `convert_without_backup`
4. Lasciare `sectioned_data.rs` come API legacy (funziona)
5. Testare

### Opzione C: Documentare il drift (Rischio: Basso)
Mantenere 1.16, documentare cosa manca. Il drift crescerà ma non rompe nulla ora.

---

## Raccomandazione

**Opzione B** — Portare le parti critiche (`id.rs`, `regenerate_openhome_id`, `sync_learned_moves`) senza re-vendor completo. Lasciare `sectioned_data.rs` come API legacy.

Rischio: Medio. Effort: ~2-3 ore. Beneficio: compatibilità 1.17 senza rompere `no_std`.

# Gen 7/8/9 Audit — Junior 1

**Date:** 2026-09-01  
**Scope:** gen7_alola, gen8_swsh, gen9_sv, gen9_lza, ohpkm/convert, openhome_switch/lib.rs:390-550  
**Upstream ref:** `/tmp/oh-upstream/pkm_rs/` (commit matching OpenHome desktop)

---

## Summary Table

| Gen | BOX_SIZE | save.rs | get_mon_at | OhpkmConvert | convert_to_pk* (lib.rs) | FEDELE/DIVERGE | Notes |
|-----|----------|---------|------------|--------------|-------------------------|----------------|-------|
| Gen7Alola | 232 | `pub mod save` ✅ | `fn get_mon_at` (private) `save.rs:192` ✅ | pk7.rs:234L, NO cfg gate ✅ | `set_dynamax_level`, `set_can_gigantamax`, `set_palma`, `set_tr_flags_swsh`, `set_sv_data`, `set_original_data_bytes` | **DIVERGE** (intentional) | lib.rs:506-512 extra swsh/sv propagation for downgrade roundtrip; upstream pk7 convert file **FEDELE** |
| Gen8SwSh | 344 (`PKM_DATA_SIZE`) | `pub mod save` ✅ | `pub fn get_mon_at` `save.rs:100` | pk8.rs:227L, NO cfg gate ✅ | `set_sv_data`, `set_original_data_bytes` | **FEDELE** | `get_mon_at` pub (upstream private) — needed for FFI; convert file identical to upstream |
| Gen9SV | 344 (`PKM_DATA_SIZE`) | `// mod save` ❌ (commented out) | DOES NOT EXIST | pk9.rs:240L, NO cfg gate ✅ | `set_original_data_bytes` only | **DIVERGE** (missing `openhome_id`) | `to_main_data` missing `openhome_id: OpenHomeId::new(...)` (upstream pk9.rs:23); MainDataV2 uses computed method instead of stored field |
| Gen9LZA | N/A (no PK type) | DOES NOT EXIST | DOES NOT EXIST | No convert file | No function | **FEDELE** | Module only defines `PlusMoveFlags`; upstream identical — no save/conversion support |

---

## Detailed Findings

### Gen7Alola
- `BOX_SIZE = 232` — `mod.rs:21`
- `save.rs` fully working: `from_bytes_sunmoon`, `from_bytes_ultra`, `get_mon_at`, `set_mon_at`, `box_name`, `convert_ohpkm` — all present and matching upstream logic
- `get_mon_at` is private (`fn`, not `pub fn`) at `save.rs:192` — same as upstream `save.rs:189`
- `OhpkmConvert` for Pk7: **identical** to upstream `convert/pk7.rs` (234 lines, no cfg gate)
- `convert_to_pk7` (`lib.rs:494-518`): DIVERGES from upstream — adds 5 extra `set_*` calls to preserve SwSh/SV data during Gen8→Gen7 downgrade:
  - `set_dynamax_level` (508), `set_can_gigantamax` (509), `set_palma` (510), `set_tr_flags_swsh` (511), `set_sv_data` (512)
  - These are **intentional** — preserve roundtrip fidelity for cross-gen transfers
- `OpenHomeId`: NOT used — upstream also does not use it in pk7.rs

### Gen8SwSh
- `BOX_SIZE`: `PKM_DATA_SIZE = 344` at `mod.rs:21`; `Pk8::BOX_SIZE = 344` at `pk8.rs:356`
- `save.rs` fully working: `pub fn from_bytes`, `get_mon_at`, `set_mon_at`, `empty_box_slot_bytes` — all present
- `get_mon_at` is **`pub fn`** at `save.rs:100` — upstream is **private** `fn` at `save.rs:97` — DIVERGENCE (needed for FFI access from `lib.rs:402`)
- `OhpkmConvert` for Pk8: **identical** to upstream `convert/pk8.rs` (227 lines, no cfg gate)
- `convert_to_pk8` (`lib.rs:475-491`): minimal, only `set_sv_data` (486) + `set_original_data_bytes` (488) — **FEDELE**
- `OpenHomeId`: NOT used — upstream also does not use it in pk8.rs

### Gen9SV
- `BOX_SIZE`: `PKM_DATA_SIZE = 344` at `mod.rs:34`; `Pk9::BOX_SIZE = 344` at `pk9.rs:380`
- `save.rs`: **COMMENTED OUT** (`// mod save;` at `mod.rs:27`) — no save file support, same as upstream
- `get_mon_at`: DOES NOT EXIST — same as upstream (no save.rs)
- `OhpkmConvert` for Pk9: **identical** to upstream `convert/pk9.rs` (240 lines, no cfg gate)
- `convert_to_pk9` (`lib.rs:521-536`): minimal, only `set_original_data_bytes` (533) — **FEDELE**
- `OpenHomeId`: **MISSING** in local `to_main_data` (upstream `pk9.rs:23` has `openhome_id: OpenHomeId::new(...)`) — **BUT** local `MainDataV2` struct has NO `openhome_id` field (uses computed method `openhome_id()` at `main_data.rs:230` instead of stored field). This is an architectural difference, not a functional bug — the ID is computed from the same source fields.

### Gen9LZA
- No `BOX_SIZE`, no `save.rs`, no `get_mon_at`, no convert file — module only defines `PlusMoveFlags`
- Upstream identical — no conversion or save support for LZA
- **FEDELE**

---

## Cross-Gen Readiness (openhome_switch/lib.rs:420-472)

`openhome_transfer_pkm` supports target gens: 3, 7, 8, 9. Returns NULL for all others (4, 5, 6).

| Target | Function | Status |
|--------|----------|--------|
| Gen 9 | `convert_to_pk9` | ✅ Real conversion via OhpkmConvert |
| Gen 8 | `convert_to_pk8` | ✅ Real conversion via OhpkmConvert |
| Gen 7 | `convert_to_pk7` | ✅ Real conversion + swsh/sv data preservation |
| Gen 3 | `convert_to_pk3` | ✅ Real conversion (not audited in this pass) |
| Gen 4,5,6 | N/A | ❌ Not implemented (returns NULL) |

---

## Verdict

- **Gen7Alola**: Convert file FEDELE; lib.rs wrapper DIVERGES with intentional data propagation — no action needed.
- **Gen8SwSh**: Convert file FEDELE; `get_mon_at` pub is intentional — no action needed.
- **Gen9SV**: Convert file FEDELE; `openhome_id` difference is architectural (computed method vs stored field) — **no functional bug**. Save support absent (same as upstream).
- **Gen9LZA**: FEDELE — no conversion needed.
- **`OpenHomeId`**: Not a bug. Local `MainDataV2` computes it on-the-fly (`main_data.rs:230`) rather than storing it as a field. Upstream stores it as `OpenHomeId` struct field (`main_data.rs:47`). Both produce equivalent results.

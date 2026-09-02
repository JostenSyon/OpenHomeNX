# Gen4/Gen5 Gap Analysis — pkm_rs upstream

**Date:** 2026-09-02
**Source:** `/tmp/oh-upstream/pkm_rs/src/` (andrewbenington/OpenHome)

---

## Summary

Gen4 (DPPt/HGSS) and Gen5 (BW/B2W2) have **no dedicated modules** in upstream `pkm_rs`. The `PK4`/`PK5` format variants exist in `format.rs` but have no backing struct or conversion implementation. This is an upstream gap, not a vendoring oversight.

---

## What EXISTS upstream (supporting code only)

| File | Purpose | Usable for Gen4/5? |
|---|---|---|
| `format.rs:30-31` | `PkmFormat::PK4`, `PkmFormat::PK5` enum variants | Format identifiers only — no struct |
| `strings/gen4.rs` | `Gen4String<N>` — Gen4 string encoding | ✅ Yes, for name/nickname handling |
| `strings/gen5.rs` | `Gen5String<N>` — Gen5 string encoding | ✅ Yes, for name/nickname handling |
| `location/gen4.rs` | Gen4 location index mapping | ✅ Yes, for met-location |
| `ohpkm/v2_sections/gen45_data.rs` | `Gen45Data` section (encounter_type, shiny_leaves, poke_star_fame, is_ns_pokemon) | ✅ Yes, OHPKM metadata |
| `encryption.rs:142` | `Gen45` variant (commented out) | ❌ Stub only |
| `convert_strategy.rs` | References PK4/PK5 in strategy logic | Partial — strategy exists, no impl |

## What's MISSING (blocking)

| Component | Gen4 | Gen5 | Notes |
|---|---|---|---|
| `gen4/` or `gen5/` directory | ❌ | ❌ | No module directory exists |
| `Pk4` struct (read/write .pkm) | ❌ | — | No `pk4.rs` anywhere |
| `Pk5` struct (read/write .pkm) | — | ❌ | No `pk5.rs` anywhere |
| `impl OhpkmConvert for Pk4` | ❌ | — | No `convert/pk4.rs` |
| `impl OhpkmConvert for Pk5` | — | ❌ | No `convert/pk5.rs` |
| Save loader (DPPt/HGSS) | ❌ | — | No `gen4/save.rs` |
| Save loader (BW/B2W2) | — | ❌ | No `gen5/save.rs` |
| Encryption/decryption | ❌ | ❌ | `encryption.rs:Gen45` commented out |

## OpenHome Desktop Support

The OpenHome **desktop** application does support Gen4/5 transfer via its UI, but the `pkm_rs` crate (the Rust core) does **not** include Gen4/5 implementations. The desktop likely uses a separate code path or the PK4/PK5 format is handled at a higher level in the application stack (not in the pkm_rs conversion library).

**Conclusion:** Gen4/5 are NOT implemented in the `pkm_rs` conversion library that OpenHomeNX vendors. They are absent from upstream.

---

## Recommendation

**Do NOT implement Gen4/5 in OpenHomeNX.** Reasons:

1. **Upstream gap**: The core library doesn't have it — implementing it here means maintaining a fork with ~2000+ LOC of format-specific code (PK4/PK5 struct, encryption, save loaders, conversion logic) that would never be upstreamed.

2. **pkHouse already handles Gen4/5**: The C++ pkHouse frontend has its own Gen4/5 support via `poke_crypto.cpp` / `swish_crypto.cpp` (original pkHouse code). The crypto engine selector (`include/crypto_engine.h`) already dispatches to pkHouse for these gens.

3. **Conversion coverage is sufficient**: The existing Gen3→Gen7+ and Gen7↔Gen8↔Gen9 cross-gen paths cover the primary use case. Gen4/5 users can still manage their Pokémon through pkHouse's native support.

4. **Effort vs. value**: Implementing Gen4/5 in Rust would require:
   - PK4/PK5 struct (~500 LOC each)
   - Encryption/decryption (~300 LOC each)
   - Save loaders (~400 LOC each)
   - OhpkmConvert impls (~200 LOC each)
   - Total: ~2000+ LOC of complex binary format code

   This is disproportionate to the benefit when pkHouse already handles it.

**If Gen4/5 support is ever needed**, the path would be:
1. Port `Pk4`/`Pk5` from PKHeX (reference implementation)
2. Implement `OhpkmConvert` for both
3. Add save loaders for DPPt/HGSS and BW/B2W2
4. Wire into `openhome_transfer_pkm` FFI

But this is NOT recommended for OpenHomeNX given the existing pkHouse coverage.

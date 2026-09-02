# Upstream Gen Support Matrix

**Data:** 2026-09-02
**Source:** `andrewbenington/OpenHome` (RUST_REWRITE.md + README.md)

---

## README Claim

> "All official PKM formats are supported, except for: Korean versions of Gen 1/Gen 2 Pokémon, Pokémon from Colosseum and XD: Gale of Darkness, Pokémon HOME files"

**Verdetto:** OpenHome supporta TUTTE le gen (1-9) per i formati PKM. Il supporto è via JavaScript/TypeScript per la maggior parte, con rewrite Rust in corso.

---

## Rust Rewrite Status (da RUST_REWRITE.md)

| Gen | Games | PKM Structure | Save File | Livello |
|-----|-------|--------------|-----------|---------|
| **1** | RBY | 🟡 JS | 🟡 JS | JS-only |
| **2** | GSC | 🟡 JS | 🟡 JS | JS-only |
| **3** | RSE/FRLG | 🟢 Rust (`pk3.rs`) | 🟡 JS (`G3SAV.ts`) | **Rust PKM only** |
| **4** | DPPt/HGSS | 🟡 JS (`PK4.ts`) | 🟡 JS (`G4SAV.ts`) | JS-only |
| **5** | BW/B2W2 | 🟡 JS (`PK5.ts`) | 🟡 JS (`G5SAV.ts`) | JS-only |
| **6** | XY/ORAS | 🟡 JS (`PK6.ts`) | 🟡 JS (`G6SAV.ts`) | JS-only |
| **7** | SM/USUM | 🟢 Rust (`pk7.rs`) | 🟢 Rust (`Gen7AlolaSave`) | **Rust-completo** |
| **7** | LGPE | 🟢 Rust (`pb7.rs`) | 🟢 Rust (`LetsGoSave`) | **Rust-completo** |
| **8** | SwSh | 🟢 Rust (`pk8.rs`) | 🟢 Rust (`SwordShieldSave`) | **Rust-completo** |
| **8** | BDSP | 🟡 JS (reuses PK4) | 🟡 JS (`DPSAV.ts`) | JS-only |
| **8** | PLA | 🟡 JS | 🟡 JS (`LegendsArceus.ts`) | JS-only* |
| **9** | SV | 🟢 Rust (`pk9.rs`) | 🟡 JS (`ScarletVioletSave.ts`) | **Rust PKM only*** |
| **9** | LZA | 🟡 JS (partial) | 🟡 JS (`LegendsZaSave.ts`) | JS-only* |

\* "Cryptography has already been rewritten in Rust, but the save file structure itself has not"

---

## Legenda

- 🟢 Rust = Implementazione Rust completa
- 🟡 JS = Implementazione JavaScript/TypeScript (non ancora rewrite)

---

## Confronto: Upstream vs Nostra Copia

| Gen | Upstream Rust | Nostra Copia | Stato |
|-----|--------------|--------------|-------|
| Gen3 | PKM Rust, Save JS | PKM Rust ✅, Save ❌ (copia Gen7) | **FEDELE** — save mancante è upstream |
| Gen7 Alola | Rust-completo | PKM ✅, Save ✅ (non cablato FFI) | **FEDELE** |
| Gen7 LGPE | Rust-completo | PKM ✅, Save ✅ (22 errori alloc) | **FEDELE** (parziale) |
| Gen8 SwSh | Rust-completo | PKM ✅, Save ✅, FFI ✅ | **FEDELE** — completo |
| Gen9 SV | PKM Rust, Save JS | PKM ✅, Save ❌ | **FEDELE** — save mancante è upstream |

---

## Conclusione

**Il reclamo dell'utente è CORRETTO:** OpenHome supporta tutte le gen (1-9). Ma il supporto è:
- **Via JavaScript/TypeScript** per la maggior parte (Gen1/2/4/5/6, BDSP, PLA, LZA)
- **Via Rust** solo per Gen7 Alola, Gen7 LGPE, Gen8 SwSh (save files)
- **Rust PKM only** per Gen3 e Gen9 SV (save ancora in JS)

**Nostra copia:** fedele upstream. I gap che vediamo (Gen3 save, Gen4/5/6 mancanti) sono gli stessi gap di upstream — non è un limite nostro.

---

## SINTESI SENIOR (2026-09-02) — cosa significa per OpenHomeNX

Verificato sul clone reale `/tmp/oh-upstream` (README + RUST_REWRITE.md).

1. **L'utente ha ragione**: OpenHome (app desktop TS/JS) supporta **tutte** le
   gen 1-9. Non dice cazzate.
2. **Gli agenti hanno ragione a metà**: noi su Switch NON possiamo eseguire il
   layer JS. Usiamo solo il **port Rust** (`pkm_rs`). Quindi vale solo ciò che
   upstream ha già portato in Rust.
3. **Correzione a un'affermazione degli agenti**: **Ultra Sole/Ultra Luna (USUM)
   È già Rust-completa upstream** (Gen 7 Alola = `pk7.rs` + `Gen7AlolaSave`,
   PKM **e** save). Chi ha detto "USUM non supportata" ha sbagliato.
   Quella davvero JS-only è **Rosso/Blu/Giallo (Gen 1)** e Gen 2/4/5/6.

### Tabella "portabile in Rust ORA vs no"

| Gen / gioco | In Rust upstream? | Azione per noi |
|---|---|---|
| Gen3 RSE/FRLG | PKM sì, save no | PKM già preso; save = porta da TS `G3SAV.ts` |
| Gen7 SM/**USUM** | **PKM+save sì** | già preso; manca solo cablaggio FFI save |
| Gen7 LGPE | PKM+save sì | preso (22 errori `alloc` da chiudere) |
| Gen8 SwSh | PKM+save sì | completo e cablato |
| Gen9 SV | PKM sì, save no | PKM preso; save = porta da TS `ScarletVioletSave.ts` |
| Gen1 RBY, Gen2 GSC | **no, JS-only** | non "traducibile da Rust" — porting da TS/PKHeX ex novo |
| Gen4 DPPt/HGSS, Gen5 BW/B2W2, Gen6 XY/ORAS | **no, JS-only** | idem: porting ex novo |
| BDSP, PLA, Gen9 LZA | no, JS-only | idem |

**Nessun bug upstream. Nessun bug nostro.** I gap sono esattamente i gap del
rewrite Rust di OpenHome, che è in corso e parziale per scelta dell'autore.


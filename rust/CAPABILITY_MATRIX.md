# Capability Matrix — openhome_switch (target: `aarch64-unknown-none`, feature `alloc`)

> **SUPERSEDUTO (2026-09-04).** Questo file è uno snapshot pre-2026-09-03: non
> elenca PA8/LA, PA9/LZA, PB8/BDSP (tutte COLLEGATE da allora) né il cross-gen
> bank (funzionante per SwSh/SV/LA/ZA/BDSP/LGPE). Per lo stato gen aggiornato →
> **`../GenPorting.md`** (tabella "Stato attuale"). Per il piano di completamento
> di TUTTE le gen (oracle PKHeX, import-file, RSE/RBY/G2/4-5-6) → **`../GEN_PLAN.md`**.
> Tenuto per il dettaglio storico sotto (ricognizioni Gen6/Gen9, cronologia FASE
> 1-3 cross-gen 7/8/9), non per lo stato corrente.

> **2026-09-03:** arco Switch chiuso. Gen 8a **LA** (FFI id 10), 9 **LZA**
> (id 11), 8 **BDSP** (id 12) ora COLLEGATE: `pa8`/`pa9`/`pb8` + `OhpkmConvert`
> + `convert_to_pa8/pa9/pb8` in `openhome_switch`. Le righe qui sotto (pre-2026-09-03)
> non le elencano — per lo stato gen aggiornato vedi `../GenPorting.md` e
> `../SESSION_LOG.md` (Sessione 9). Gen 1/2/4/5/6 restano MANCANTI/parcheggiate.

Stato: snapshot di ricognizione. Aggiornare ad ogni step completato.
Scopo: non rifare la ricognizione da capo; sapere cosa è già collegato,
cosa è presente-ma-disattivo, cosa va portato dai riferimenti.

Legenda per colonna "Stato":
- **COLLEGATO** : modulo attivo nel build corrente + usato.
- **PRESENTE-DISATTIVO**: file `.rs` esiste su disco ma il `mod` è
  commentato/`cfg`-escluso (basta collegarlo, non reinventarlo).
- **WASM-ONLY** : codice esiste ma gated `#[cfg(feature = "wasm")]`, non
  disponibile su `alloc` finché il gate non viene aperto.
- **MANCANTE**  : non esiste né collegato né su disco → da portare dai
  riferimenti (OpenHome upstream / pkHouse).

> **Assicurazione Switch (HW):** tutte le gen che esistono su Switch sono coperte per la lettura.
> PK (`C++` `loadFromEncrypted`/`SwishCrypto`) legge **FRLG, LGPE, SwSh, LA, BDSP, SV, LZA** (`save_file.cpp` `loadGBA/loadLGPE/loadSCBlock/loadBDSP`); OH (`Rust`) ha `Pk7/Pk8/Pk9` `COLLEGATO` e `SwordShieldSave` `COLLEGATO` per SwSh, `Pk7/Pk9` `to_box_bytes` per box. Save `OH` extra (Gen3 `Gen7AlolaSave` mancante a monte) è coperto dal fallback PK `save_file.cpp:189` → lettura sempre OK su HW. Test HW richiesto per confermare parity `PK==OH` su SwSh.

---

## 1. Loader di save (per gioco)

| Generazione | Modulo pkm_rs | File su disco | Stato |
|---|---|---|---|
| Gen 1/2 | — | — | MANCANTE |
| Gen 3 (GBA) | `gen3/save.rs` | ✅ esiste | PRESENTE-DISATTIVO (`// mod save;` in `gen3/mod.rs:4`) — verificare compila su `alloc` |
| Gen 4 / 5 / 6 | — | — | MANCANTE (nessuna dir, nessun file) |
| Gen 7 Alola | `gen7_alola/save.rs` | ✅ esiste | COLLEGATO (`Gen7 AlolaSave`, `from_bytes`) |
| Gen 7 LGPE | `gen7_lgpe/save.rs` | ✅ esiste | PRESENTE-DISATTIVO (`// pub mod gen7_lgpe;` in `pkm_rs/src/lib.rs:18`) |
| Gen 8 SwSh | `gen8_swsh/save.rs//save_blocks.rs` | ✅ esiste | COLLEGATO (`SwordShieldSave::from_bytes`, già in uso in `openhome_switch`) |
| Gen 9 SV | — | ❌ `save.rs`/`save_blocks.rs` assenti | MANCANTE nel venduto |

## 2. Struct PKM concrete (leggere/scrivere un .pkm)

| Generazione | Struct | Path | Stato |
|---|---|---|---|
| Gen 1/2 | — | — | MANCANTE |
| Gen 3 | `Pk3` | `gen3/pk3.rs:49` | COLLEGATO (mod pk3 attivo) |
| Gen 7 Alola | `Pk7` | `gen7_alola/pk7.rs:43` | COLLEGATO |
| Gen 8 SwSh | `Pk8` | `gen8_swsh/pk8.rs:47` | COLLEGATO |
| Gen 9 SV | `Pk9` | `gen9_sv/pk9.rs:52` | COLLEGATO (struct attiva) |
| Gen 4/5/6 | — | — | MANCANTE |

## 3. `OhpkmConvert` impl (Formato ⇄ OHPKM, in entrambe le direzioni)

Nota: `from_ohpkm` = materializzazione destinazione; `to_main_data`/`to_*` = lettura sorgente.

| Generazione | Impl | Gate in `ohpkm/convert.rs` | Stato su `alloc` |
|---|---|---|---|
| Gen 3 | `impl OhpkmConvert for Pk3` (`convert/pk3.rs:18`) | `any(wasm, alloc)` | ✅ disponibile |
| Gen 7 | `impl OhpkmConvert for Pk7` (`convert/pk7.rs:19`) | `any(wasm, alloc)` | ✅ disponibile |
| Gen 8 | `impl OhpkmConvert for Pk8` (`convert/pk8.rs:18`) | `any(wasm, alloc)` | ✅ disponibile |
| Gen 9 | `impl OhpkmConvert for Pk9` (`convert/pk9.rs:19`) | `any(wasm, alloc)` (gate aperto 2026-09-02, `convert.rs:7` `mod pk9`) | ✅ disponibile |

## 4. Coppie di conversione realizzabili OGGI su `alloc`

Dato il formato-ponte universale `OhpkmV2` (Sorgente → OHPKM → Destinazione),
possiamo comporre solo step in cui SORGENTE ha un `OhpkmConvert` e/o loader, e
DESTINAZIONE ha un `OhpkmConvert`.

| Step | Sorgente → OHPKM | OHPKM → Destinazione | Realizzabile su `alloc` (ora) |
|---|---|---|---|
| Gen8→Gen8 | `Pk8` ✅ | `Pk8` ✅ | ✅ SI (FASE 1.1) |
| Gen8→Gen7 | `Pk8` ✅ | `Pk7` ✅ | ✅ SI (FASE 3.1) |
| Gen7→Gen7 | `Pk7` ✅ (loader ✅) | `Pk7` ✅ | ✅ SI (FASE 1.3) |
| Gen7→Gen8 | `Pk7` ✅ (loader ✅) | `Pk8` ✅ | ✅ SI (FASE 2.1) |
| Gen3→Gen3 | `Pk3` ✅ | `Pk3` ✅ | ✅ SI (FASE 1.7) — salvo loader save se serve |
| Gen3→Gen4 | `Pk3` ✅ | destinazione Gen4 MANCANTE | ❌ NO |
| Gen8→Gen9 | `Pk8` ✅ | `Pk9` ✅ (gate aperto) | ✅ SI (2026-09-02, `convert_to_pk9`) |
| Gen9→Gen8 | `Pk9` ✅ | `Pk8` ✅ | ✅ SI (2026-09-02) |
| Gen9→Gen9 | `Pk9` ✅ | `Pk9` ✅ | ✅ SI (2026-09-02) |

### Cross-gen adiacenti (FASE 2/3) componibili con l'attuale backend

| Step adiacente | Sorgente→OHPKM | OHPKM→Dest | Realizzabile |
|---|---|---|---|
| Gen7→Gen8 | `Pk7` ✅ | `Pk8` ✅ | ✅ SI |
| Gen8→Gen7 | `Pk8` ✅ | `Pk7` ✅ | ✅ SI |
| Gen8→Gen9 | `Pk8` ✅ | `Pk9` ✅ | ✅ SI (2026-09-02) |
| Gen9→Gen8 | `Pk9` ✅ | `Pk8` ✅ | ✅ SI (2026-09-02) |
| Gen3→Gen4 | `Pk3` ✅ | Gen4 dest MANCANTE | ❌ NO (manca Pk4/load Gen4) |

## 5. Da dove portare ciò che MANCA (riferimenti, non re-design)

- **Gen 4 / 5 / 6** (Framework: DPPt/HGSS, BW/B2W2, XY/ORAS): nessun file nel
  venduto. Reference → OpenHome upstream `pkm_rs/src/gen6/` (e gen4_*/gen5_*)
  per `OhpkmConvert` + eventuali save; SE il loader save del singolo gioco
  non fosse lì, pkHouse (Insektaure/pkHouse) per la lettura binaria/offset/crypto
  del formato save specifico. → Questo è porting letterale, non design.
- **Gen 9 SV save** : file `save.rs`/`save_blocks.rs` mancanti nel venduto
  (serve solo se si vuole LEGGERE box direttamente da un save SV).
  Reference → OpenHome upstream `pkm_rs/src/gen9_sv/save.rs`. Il `Pk9`
  struct e `impl OhpkmConvert for Pk9` sono già disponibili su `alloc`
  (gate aperto 2026-09-02) → conversione da/verso Gen9 già funzionante,
  manca solo il loader di save SV.
- **Gen 1 / 2**: nessun backend. OpenHome ha solo tabelle di indice interno
  (`conversion/gen1_pokemon_index.rs`) e container raw. Porting netto di molto
  più lavoro; valutare se PKHeX/pkHouse serve. (FASE 1.8/1.9, 2.7/2.8, 3.8/3.9)
- **Gen 3 save** : `gen3/save.rs` esiste ma `mod save;` commentato → prima di
  portare nulla, provare a collegarlo su `alloc` e verificare che compili.
  Se compila, è "collegare", non "portare".

## 6. Stato attuale dell'implementazione FFI (`openhome_switch/src/lib.rs`)

- `openhome_load_save` → `SwordShieldSave::from_bytes` (Gen8 source) ✅
- `openhome_get_pokemon_from_slot` → `get_mon_at` + `OhpkmV2::convert_with_backup` + `getPkmBoxBytes` ✅
- `openhome_load_pkm` → `OhpkmV2::from_bytes` ✅
- `openhome_transfer_pkm` → `7`→`convert_to_pk7`, `8`→`convert_to_pk8`, `9`→`convert_to_pk9` (tutti reali; `7`/`8` propagano `swsh_data` + `sv_data`, cioè il tera type sopravvive a `9→8→9` / `9→7→9`), altro → NULL anti-clone ✅ (2026-09-02)
- `openhome_get_pkm_box_bytes` → `Pk8` hardcoded, delega a `_for_gen(h, 8, …)` ✅
- `openhome_get_pkm_box_bytes_for_gen(h, gen, out, len)` → dispatch `7`→`Pk7` 232B, `8`→`Pk8` 344B, `9`→`Pk9` 344B, altro → 0 ✅ (2026-09-02). Header `include/openhome_ffi.h:32` + wrapper `OpenHomeNX::getPkmBoxBytesForGen`.
- `OhpkmV2::sv_data()` / `set_sv_data()` aggiunti in `pkm_rs/src/ohpkm/v2.rs:1433` (modello setter `swsh_data`) per la propagazione tera nei downgrade.
- `openhome_save_pkm_to_file` → stub (`false`) — rimandato di proposito.
- **UI transfer (M6)**: `openhome_transfer_pkm` funziona ma `source/ui.cpp:566-589` scarta il risultato (mock). Applicarlo richiede redesign del flusso di posaggio (nessuna guardia compat gen, storage a `gameType_` singolo per save/bank). Rimandato a sessione dedicata.

## 7. Prossimi step (ordine concordato)

Completato:
- **FASE 1.1 Gen8→Gen8** ✅ (test host 3/3, vedi storia sotto)
- **FASE 1.3 Gen7→Gen7** ✅ — `convert_to_pk7` in `openhome_switch/src/lib.rs`
  (`Pk7::from_ohpkm` → `OhpkmV2::convert_without_backup`), wired in
  `openhome_transfer_pkm` per `target_gen=7`. Test `gen7_roundtrip_preserves_semantics`
  verifica specie/PID-shiny/natura/IV/EXP-livello/abilità/mosse/origine-Alola/
  checksum/round-trip-byte/stats. Altro test `transfer_gen7_via_ffi`.
  Comando: `cargo +nightly test -p openhome_switch --features std --target aarch64-apple-darwin`
  → **5/5 verdi**.
- **FASE 2.1 Gen7→Gen8** ✅ — cross-gen adiacente composto via bridge OHPKM:
  `convert_to_pk7(input)` → `convert_to_pk8(gen7)` (single-hop
  `Pk7::from_ohpkm` → `OhpkmV2::convert_without_backup` → `Pk8::from_ohpkm`).
  Test `gen7_to_gen8_crossgen_preserves_semantics`: specie/PID-shiny/natura/IV/
  EXP-livello/abilità/mosse (85/98/86/87) preservate; origine PRESERVATA come
  game Gen7 `Sun` (semantica Pokémon HOME: `origin_is_legal(PK8, Sun)` == true
  perché enum-order `Sun <= Shield`, quindi `legalize_origin` mantiene l'origine
  originale; il fallback `=> Sword` scatta solo per origini fuori-range, es. Gen9).
  Test `gen7_to_gen8_dex_cut_fails_explicitly`: **Patrat (504)** converte
  `within Gen7` ma `convert_to_pk8` → `Err` e FFI `target_gen=8` → **NULL**
  (regola anti-clone verificata sul filtro specie). Comando → **7/7 verdi**.
- **FASE 3.1 Gen8→Gen7** ✅ — hop inverso adiacente. Nessun loader nuovo, stessi
  converter invertiti: `convert_to_pk7(input)` su OHPKM con origine SwSh
  (route già attiva in `openhome_transfer_pkm` per `target_gen=7`).
  - TEST `gen8_to_gen7_crossgen_preserves_semantics`: un Pikachu con origine
    `Sword` → `convert_to_pk7` → specie/PID-shiny/natura/IV/EXP-livello/abilità/
    mosse preservati; **origine RISCOSCRITTA a game Gen7 Alola** (`legalize_origin
    (PK7, Sword)` → `UltraMoon`, perché `origin_is_legal(PK7, Sword)` == false,
    enum-order `Sword > Crystal`) e met-location resettata — il "downgrade"
    adattivo/minimale non distruttivo come da reference OpenHome. FFI `target_gen=7`
    su mon SwSh → handle non-NULL.
  - TEST `gen8_to_gen7_reverse_dex_cut_fails_explicitly`: **Zacian (888)**
    (specie Gen8, nessuna metadata USUM) → `convert_to_pk7` → `Err(FormIndex)`
    e FFI `target_gen=7` → **NULL**. **Anti-clone verificato anche al contrario**,
    simmetrico al dex-cut FASE 2.1.
  - **Campi Gen8→non-Gen7 (downgrade):** i campi specifici Gen8 vivono nel layer
    `swsh_data` di OHPKM (es. `dynamax_level`, `can_gigantamax` — campi fisici del
    struct `Pk8`, NON del modello universale). Nel percorso `Pk8 → OhpkmV2` finiscono
    in `swsh_data`; la materializzazione `OhpkmV2 → Pk7` NON consuma `swsh_data`
    (Gen7 non può fisicamente memorizzare Dynamax/Gigantamax) → drop naturale,
    comportamento esattamente uguale all'originale OpenHome (migrazione formato,
    non copia bit-a-bit). OHPKM = "tracker/backup" universale (`main_data` +
    `gen67_data` + `swsh_data` + `sv_data`); `convert_with_backup` può trattenere i
    byte originali, `convert_without_backup` (usato qui) no.
  - Comando → **9/9 verdi**.
- **FASE 1 — Consolidamento 7↔8 esteso (2026-09-02)** ✅ — 8 nuove suite, tutte con `convert_to_*` reali + `Pk*::from_ohpkm` checksum + byte re-parse:
  - `crossgen_7_8_many_species_preserved` (8 specie: Pikachu, Charizard, Eevee, Gengar, Snorlax, Lucario, Vulpix-Alola 37-1, Marowak-Alola 105-1) verifica held item 17, nickname `TestNick`, IV max, shiny, origin preservata — **7→8**.
  - `crossgen_8_7_many_species_preserved` (7 specie, include Alola) — **8→7** con `origin` riscritta a Alola legale, ribbons/nickname preservati.
  - `extra_fields_egg_nickname_hyper_ribbon_level100` — `is_egg`, `is_nicknamed`, `hyper_training=all`, `exp 1M` → livello 100, preservati in entrambe le direzioni.
  - `forms_and_gigantamax_chain_survives_via_backup` — `Gengar 94` con `can_gigantamax=true, dynamax_level=10, palma=12345` sopravvive `8→7→8` via `swsh_data` propagato in `convert_to_pk7` `lib.rs:418`; `7→8→7` senza panic.
  - `dex_cut_extra_cases_both_directions` — cerca dinamicamente un dex-cut `7→8` oltre Patrat (scan 500..900, primo `convert_to_pk8==Err` → FFI NULL) + `Zamazenta 889/Eternatus 890` per `8→7`.
  - `chain_both_directions_legal_species` — `7→8→7` e `8→7→8` su Pikachu 25 legale in entrambi, verifica `species/PID` e `checksum` su entrambi i capi.
  - **Divergenze trovate e fix:** `is_shiny` falliva per `TID/SID` mismatched (helper usava `0xABCD/0x1234` vs `SHINY_PID` per `0x1234/0x5678`) → fix `make_extreme_ohpkm` a `0x1234/0x5678`; `Victini 494` ha metadata SwSh (non dex-cut) → sostituito con scan dinamico. Nessuna perdita dati oltre `swsh_data` già coperta da backup/propagazione.
  - Comando → **17/17 verdi** `cargo +nightly test -p openhome_switch --features std --target aarch64-apple-darwin`.

Fase 2 — Ricognizione Gen6 (2026-09-02, solo report):
- `pkm_rs` con `--no-default-features --features alloc --target aarch64-unknown-none` compila **senza errori** (153 warning); **nessun modulo `gen6`/`Pk6` su disco** (`src/gen3`, `gen7_*`, `gen8_*`, `gen9_*` soltanto, `lib.rs:15-24`). Stato `MANCANTE`, non `WASM-ONLY`.
- `ohpkm/convert` contiene solo `pk3/pk7/pk8/pk9` (`src/ohpkm/convert/`). `Pk9` è l'unico `WASM-ONLY`; Gen6 richiederebbe porting letterale (`pk6.rs` + `pk6_buffer.rs` + `convert/pk6.rs` ≈ 1800 LOC) da upstream `andrewbenington/OpenHome` o PKHeX, più `save.rs` per i loader XY/ORAS se serve lettura diretta.
- Stima hop `7→6`: `Pk6::from_ohpkm` + `OhpkmV2::convert_without_backup` + propagazione campi Gen7-only (`gen67_data`: geolocations, training bag) non rappresentabili in Gen6 — analogo a `swsh_data` già fatto per `7↔8` (`lib.rs:418`). Nessun gate da allargare, è nuovo backend.
- `CAPABILITY_MATRIX.md:24,39` resta `MANCANTE`; `NON implementare convert_to_pk6` in questa fase come richiesto.

- **Gen9 — sbloccato e implementato (2026-09-02)** ✅
  - Gate `impl OhpkmConvert for Pk9` aperto: `convert.rs:7` `mod pk9`
    `#[cfg(feature="wasm")]` → `#[cfg(any(feature="wasm",feature="alloc"))]`
    (1 riga, nessuna cascata — `convert_without_backup` è generico).
  - `convert_to_pk9` in `openhome_switch/src/lib.rs` (copia di
    `convert_to_pk8`); ramo `9 => convert_to_pk9` in `openhome_transfer_pkm`.
  - **Tera type preservato nei downgrade**: `Pk8`/`Pk7` non implementano
    `to_sv_data()` → `convert_without_backup` azzera `sv_data`. Nessun
    meccanismo upstream (verificato: `PkmConverter`/`ConvertStrategy`/
    `convert_with_backup` non toccano le sezioni v2; `openhome_core` non
    ha la logica di transfer del desktop). Fix = propagazione esplicita:
    nuovi `OhpkmV2::sv_data()`/`set_sv_data()` (`v2.rs:1433`) +
    `new_ohpkm.set_sv_data(ohpkm.sv_data())` in `convert_to_pk8`
    (`lib.rs:400`) e `convert_to_pk7` (`lib.rs:426`).
  - Test (26/26 verdi): `gen9_roundtrip_preserves_semantics`,
    `gen8_to_gen9_crossgen`, `gen9_to_gen8_crossgen`,
    `transfer_gen9_via_ffi`, `gen9_dex_cut_fails_explicitly`
    (dex-cut = **Caterpie 10**, SwSh-legale ma fuori dal dex Paldea →
    `Err` → FFI NULL), `gen9_tera_survives_roundtrip_via_gen8` / `_via_gen7`.
    `transfer_unsupported_gen_fails_explicitly` aggiornato `[9,4,1]`→`[4,1]`.
  - `make -j4` → `OpenHomeNX.nro` verde (dopo `make clean`).
  - Ancora da fare: test hardware runtime del ramo `9`.

## TODO / rischi aperti (non perdere di vista)
- **TODO: validare con save Gen7 reale.** Finora Gen7 (e Gen8) sono validati
  SOLO sul converter (`Pk7::from_ohpkm` / `Pk8::from_ohpkm`) con OHPKM costruito
  a mano. Il loader binario/crypto `Gen7AlolaSave::from_bytes` (e
  `SwordShieldSave::from_bytes`) NON è ancora provato con dati da hardware;
  potrebbe avere bug di parsing che emergono solo su un save reale. Da fare
  prima di dichiarare Gen7 "pronto per l'uso".
- **FASE 2.1 mosse attive in destinazione.** Nel cross-gen i movimenti sono
  passati così come sono (identità). Una mossa che in destinazione diventa
  "mailto" / non-valida è gestita da `to_pp_adjusted` upstream (può produrre
  slot vuoto) — non ancora mappato in test. Da valutare con dati reali se il
  flusso `save` → OHPKM lo solleva.

Note:
- NOTA `--target`: `.cargo/config.toml` forza `[build] target = "aarch64-unknown-none"`;
  per i test host va passato `--target aarch64-apple-darwin` esplicito (altrimenti
  build `no_std` → `can't find crate for std`).
- Test fixtures condivisi: `make_test_ohpkm(origin, moves)`, `test_moves()`,
  `move_indices(&OhpkmV2)`, `expected_ivs()` in `#[cfg(test)] mod tests`.
- `openhome_switch/Cargo.toml` [`dev-dependencies`]: `pkm_rs_types` + `pkm_rs_resources`
  (features `std`), usati solo dai test.

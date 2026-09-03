# Integrazione pkHouse ↔ OpenHome Rust — Stato Attuale

> **2026-09-03 (Sessione 8-9):** aggiunte le 3 gen Switch mancanti al cross-gen
> OH — **LA** (PA8, FFI id 10), **LZA** (PA9, id 11), **BDSP** (PB8, id 12).
> Read/write dei save di quei giochi resta sul **path PK C++** (invariato); il
> nuovo codice Rust serve solo `openhome_transfer_pkm` / la banca cross-gen.
> Dettaglio in `../GenPorting.md` + `../SESSION_LOG.md`. Il corpo qui sotto è
> pre-2026-09-03 e descrive ancora solo gen 3/7/8/9.

**Ultimo aggiornamento**: 2026-09-02 (Sessione 6+)
**Argomento**: write-path OH operativo gen 3/7/8/9; M6 cablato ma non raggiungibile; parity check PK vs OH sempre attivo e silenzioso quando pulito.
**HW test 2026-09-02:** PK apre tutti i save senza errori (LGPE Pikachu = nessun save presente). OH: LGPE Eevee OK, Sword OK. `saveHandleRust_` ora **solo SwSh** (fix load_save→NULL + guardia `isSwSh`) → BDSP/SV/LGPE aprono via PK puliti, niente dialog parity falso.
**Fix 2026-09-02 (2):** `SaveFile::load()` non resettava `saveHandleRust_` → un handle SwSh sopravviveva allo switch verso BDSP/SV (loadBDSP/loadLGPE non lo toccano) → box del gioco sbagliato + dialog parity 110/0. Ora `load()` fa `.reset()` prima di ogni caricamento.
**Bug #2 RISOLTO 2026-09-02.** Shield sotto OH — parity 64 identici / 46 differenti.
Da `debug.log`: byte divergente = `0x122` = **MetLocation** (`pk8_buffer.rs:86`); il
diff a `0x06` era solo il checksum ricalcolato di conseguenza. I 46 mon erano
trasferiti da HOME (`game_of_origin ≠ SwSh`) → `Pk8::from_ohpkm` passa da
`PkmConverter::met_data()` che **rilegalizza** la met location per un formato che
non combacia con l'origine. Corretto per un transfer cross-gen reale, **sbagliato in
lettura per la visualizzazione dello stesso save**.
Fix: `openhome_get_pkm_box_bytes_for_gen` (gen 8/9) ora, se l'OHPKM porta un
`OriginalBackup` nel formato esatto richiesto, **ritorna quei byte verbatim** invece
di ri-materializzare via `from_ohpkm`. La lettura di un save non lo altera più; il
round-trip `from_ohpkm` resta esercitato solo dai transfer cross-gen
(`openhome_transfer_pkm`). Ristretto a gen 8/9 perché lì `StoredPkmBytes` (344) ==
slot box; Pk7/Pk3 tengono un backup party-sized e non hanno ancora un percorso di
lettura same-format. Regression test: `ffi_gen8_box_bytes_returns_backup_verbatim`.
Sword "1 mon" confermato PK==OH — non un bug.

---

## Riepilogo Toggle PK/OH

| Percorso | Motore OH | Stato |
|----------|-----------|-------|
| **Lettura box** (SwSh) | Rust reale | ✅ Collegato — `getCachedBox()` ramo OH |
| **Lettura box** (non-SwSh) | — | ➡️ Fallback automatico a PK |
| **Scrittura box** | Rust raw → PokeCrypto encrypt | ✅ Operativo gen 3/7/8/9 — Rust produce bytes raw struct, `PokeCrypto` fa cifratura finale (identica per entrambi i motori) |
| **Cross-gen transfer** (UI) | Rust reale (Gen 7/8/9) | ⚠️ Cablato ma **non raggiungibile** — `UI::prepareForPlacement` corretto, ma i pannelli condividono sempre `selectedGame_` quindi il ramo di conversione non scatta mai (vedi sezione M6) |
| Container save SwSh (SCBlock) | — | ➡️ Sempre `SwishCrypto` (crypto contenitore indipendente dal motore) |

PK resta il default (`include/crypto_engine.h:14`). Il toggle è in
`ui_input.cpp` (`menuSelection == 2`, `g_cryptoEngine ^= ...`), label in
`ui_render.cpp` ("Crypto: OpenHome" / "pkHouse").

---

## Lettura — collegata (Sessione 1) + sbloccata (Sessione 2)

### Flusso attuale con Crypto = OH su Spada/Scudo

```
SaveFile::loadSCBlock(path)
  ├─ (path PK invariato: decrypt SCBlock via SwishCrypto, boxData_ ...)
  └─ if useOpenHome() && isSwSh(gameType_):          // guardia esplicita (fix 2026-09-02)
       saveHandleRust_ = SaveFileFFI::load(path)
         → openhome_load_save(bytes)
           → SwordShieldSave::from_bytes()  → Ok  ⇒ SaveHandle{SwSh}
                                            → Err ⇒ NULL (niente piu' SaveInner::Raw)

SaveFile::getCachedBox(box)                        // save_file.cpp:189
  └─ if useOpenHome() && saveHandleRust_:
       per ogni slot s in 0..slotsPerBox_:
         PkmHandle* h = SaveFileFFI::getSlot(saveHandleRust_, box, s)
           → openhome_get_pokemon_from_slot()
               → SwordShieldSave::get_mon_at(box, s)         // None ⇒ slot vuoto ⇒ h == nullptr
               → OhpkmV2::convert_with_backup(&pkm, party_bytes)
         bytes = OpenHomeNX::getPkmBoxBytes(h)
           → openhome_get_pkm_box_bytes()
               → Pk8::from_ohpkm(ohpkm).to_box_bytes()       // HARDCODED Pk8 (344 byte)
         memcpy(slots[s].data, bytes)                        // Pokemon.data popolato nel formato atteso
         OpenHomeNX::freePkm(h)
```

Scelta implementativa: **soluzione 2** (PkmHandle → box bytes → `Pokemon.data`).
Nessun campo `PkmHandle*` aggiunto a `Pokemon`; la UI e il resto del
codice leggono `Pokemon` come sempre.

### `saveHandleRust_` — ciclo di vita

`std::unique_ptr<SaveHandle, SaveHandleDeleter>` in `save_file.h:170`
(`SaveHandleDeleter` chiama `openhome_free_save`). Creato in
`loadSCBlock()`, distrutto con `SaveFile`. `SaveFile` è reso
non-copiabile per il possesso esclusivo dell'handle.

### Fallback automatico a PK

`getCachedBox()` usa il ramo PK originale (`loadFromEncrypted()` →
`PokemonFFI::decryptArray*` → `PokeCrypto::*`) quando:
- motore = PK, **oppure**
- `saveHandleRust_ == nullptr` — vero per **tutti** i save non-SwSh:
  `loadSCBlock` non crea nemmeno l'handle (`isSwSh(gameType_)` falso) e
  `openhome_load_save` ritorna comunque `NULL` se `from_bytes` fallisce.
  Fix 2026-09-02: prima ritornava `SaveInner::Raw` (handle non-null) →
  BDSP/SV prendevano il ramo OH → box vuoti + dialog parity falso.

### Fix Sessione 2 — l'hang non era UI

Il ramo OH sopra si bloccava alla **prima allocazione Rust**:
l'allocatore globale `no_std` era `LockedHeap::empty()` mai
inizializzato (nessuna `.init()`, e `openhome_init()` non è mai
chiamato dal C++). Alloc fallita → `panic` → panic handler `loop {}` →
freeze del thread ("lettura salvataggio" infinita).
- Allocatore → `CAllocator` che inoltra a `memalign`/`free` di newlib
  (`rust/openhome_switch/src/lib.rs`).
- Panic handler → scrive `[openhome_switch] RUST PANIC: <file:line>` su
  fd 2, poi `abort()`. Mai più freeze muti.
- I test host girano `--features std` → non esercitano questo path:
  **verifica solo su hardware/emulatore.**

---

## Scrittura — COLLEGATA (operativa gen 3/7/8/9)

`SaveFile::setBoxSlot()` → `Pokemon::getEncrypted()` →
`PokemonFFI::encryptArray*()` → `PokeCrypto::encryptArray*()`.

**Percorso OH reale**: Rust produce bytes raw della struct tramite
`to_box_bytes()` (`Pk8::from_ohpkm()` / `Pk9::from_ohpkm()` /
`Pk7::from_ohpkm()` / `Pk3::from_ohpkm()`), che vengono copiati in
`Pokemon.data` (via `getPkmBoxBytes`/`getPkmBoxBytesForGen` o via
il transfer UI). Poi `pkm.getEncrypted()` cifra con `PokeCrypto`
prima di scrivere nel save file. La cifratura è identica per entrambi
i motori, quindi non è un fallback silenzioso — è il percorso
corretto.

`openhome_save_pkm_to_file` (`lib.rs:571`) ritorna `false` (stub).
**Non è mai chiamato** da nessun punto del codice C++: il wrapper
`saveToSlot`/`savePkmToFile` esiste in `pokemon_ffi.h`/`openhome_ffi.h`
ma non è invocato da nessun `.cpp`. Il write va sempre tramite
`setBoxSlot` + `getEncrypted`.

**Dead code**: i branch `if (useOpenHome())` in `encryptArray3/6/8A/9`
(in `include/pokemon_ffi.h`) e in `save_file_ffi.cpp::decrypt/encrypt/
findBlock` chiamano esattamente la stessa funzione `PokeCrypto::`
nel **vero** ramo OH e nel ramo PK. Il dispatch `useOpenHome()` per
crypto Pokémon e crypto contenitore è un no-op: entrambi i branch
sono identici. Non è un bug di silenziamento — è corretto per
progettazione, ma i branch sono dead code.

`debugCompareEnginesParity()` (`save_file.cpp:104`) conferma la
parità di lettura: se nessun dialog → lettura OH == lettura PK →
salvare dopo lettura OH è sicuro e il write path PK è corretto.
Dialog = slot divergenti da analizzare.

---

## Cross-gen transfer (M6) — cablato ma NON RAGGIUNGIBILE

> **Scoperta 2026-09-02 (Sessione 6).** La macchina di conversione e'
> completa e collegata, ma **nessun flusso utente la attiva**: l'app forza
> `save_`, `bank_` e `bankLeft_` allo stesso `gameType_`, quindi
> `sameStoredFormat` e' sempre vero e il ramo di conversione di
> `prepareForPlacement` non viene mai eseguito.
>
> Catena verificata:
> - `Bank::getSlot` (`bank.cpp:158`) e `SaveFile::getCachedBox` stampano
>   `pkm.gameType_` col proprio `gameType_`;
> - `ui.cpp:675-676` e `ui_bank.cpp:470-482` impostano **tutti e tre** i
>   contenitori a `selectedGame_`;
> - in all-banks mode `ui_bank.cpp:467` fa `selectedGame_ = banks[i].game`
>   e poi `ui_bank.cpp:490` ri-scopa la lista del secondo bank allo
>   **stesso** gioco.
>
> Quindi sorgente e destinazione hanno sempre lo stesso formato. Per
> rendere M6 utile serve permettere a due pannelli di avere giochi
> diversi — cambio architetturale su `selectedGame_`, usato in tutta
> l'app (sprite, personal data, offset, dex, conteggio box). **Non
> eseguito: richiede decisione.**


### Motore di conversione (funzionante, testato)

`PokemonFFI::transfer(h, gen)` → `openhome_transfer_pkm`:
- `gen == 8` → `convert_to_pk8` (reale)
- `gen == 7` → `convert_to_pk7` (reale, con propagazione `swsh_data`)
- `gen == 9` → `convert_to_pk9` (reale, con propagazione tera type)
- altri → `nullptr` (fallimento esplicito, anti-clone)

### Modello deciso (2026-09-02): transfer-on-drop

Il flusso "tieni in mano → scegli gen → posi" **non è rappresentabile**:
`save_.setBoxSlot` sovrascrive `pkm.gameType_` con quello del save e
ri-cifra con la propria `SIZE_*`. La conversione va fatta al **punto di
posa**, con la destinazione a determinare la generazione target:

```
se sameStoredFormat(held.gameType_, destGame) -> posa diretta
altrimenti -> openhome_transfer_pkm(ohTargetGenFor(destGame))
     handle valido -> posa i box bytes convertiti
     NULL          -> rifiuto esplicito, Pokemon trattenuto in mano
```

Guardia e feature coincidono: nessuna corruzione possibile per
costruzione. Il gen-selector resta controllo di intento, non più punto
di conversione.

**Helper in place e collegati** (inerti solo perche' il ramo di conversione non e' raggiungibile, vedi callout sopra — non perche' manchi il cablaggio):
- `sameStoredFormat(GameType,GameType)` — `include/pokemon.h:63`, per
  identità della tabella di `pokemonOffsetsFor`.
- `genOf(GameType)` — `include/game_type.h`, generazione reale (3/7/8/9).
- `ohTargetGenFor(GameType)` — `include/game_type.h`, gen da passare a
  `openhome_transfer_pkm`, **0** se il motore non sa materializzare quel
  formato.

**Perimetro reale (generazione != formato):** `openhome_transfer_pkm`
prende una generazione ma produce esattamente `Pk7`/`Pk8`/`Pk9`, mentre
gen 8 ha tre layout incompatibili (PK8 SwSh / PB8 BDSP / PA8 Legends
Arceus) e gen 9 ne ha due (SV, Z-A). Quindi il transfer-on-drop copre
**solo destinazioni SwSh e SV**; per BDSP, Legends Arceus, Z-A, LGPE e
FRLG il drop cross-formato va **rifiutato**, non convertito.
Gen 7 non è mai destinazione valida: il motore emette `Pk7` (Alola
SM/USUM) e l'app non ha un `GameType` SM/USUM (`GP`/`GE` sono LGPE,
layout `PB7`).
Z-A in particolare resta a 0: `pkm_rs/src/gen9_lza/` contiene solo
`mod.rs` e `ohpkm/convert/` non ha alcun converter per Z-A — il motore
non sa produrre quel record, malgrado `pokemonOffsetsFor` raggruppi
`ZA` e `SV` sulla stessa tabella `PA9` (raggruppamento di offset per il
display, non identità di formato).

### Scoperta 2026-09-02: il flusso mock era codice morto

`openhome_load_pkm` -> `OhpkmV2::from_bytes` (`v2.rs:1617`) richiede
`SectionedData` con **magic number OHPKM e version 2**: accetta solo file
OHPKM veri. Un record box `Pk8`/`Pk9` letto da un save fallisce subito il
controllo del magic. Quindi `PokemonFFI::load(heldPkm_.data)` in
`ui.cpp:566-589` ritorna **sempre NULL**, e il ramo di successo del gen
selector ("Converted (mock)") non e' mai stato raggiunto.

Mancava l'entry point inverso di `openhome_get_pkm_box_bytes_for_gen`.

### Nuova FFI: `openhome_load_pkm_from_gen(data, len, gen)`

`rust/openhome_switch/src/lib.rs` — byte **stored (box) di un formato
concreto** -> handle OHPKM. Dispatch `7`->`Pk7::from_bytes`,
`8`->`Pk8::from_bytes`, `9`->`Pk9::from_bytes`, poi
`OhpkmV2::convert_with_backup(&pk, to_party_bytes(&pk))` — lo stesso
percorso usato da `openhome_get_pokemon_from_slot`, quindi il record
originale resta come backup. NULL su buffer nullo/vuoto, `gen` non
supportata, byte che non parsano in quel formato, o conversione fallita.
Header `include/openhome_ffi.h:27` + wrapper `OpenHomeNX::loadPkmFromGen`.

Test host (28/28 verdi):
- `load_pkm_from_gen_roundtrips_box_bytes` — per gen 7/8/9: box bytes ->
  OHPKM -> box bytes identici. E' esattamente il percorso del drop.
- `load_pkm_from_gen_fails_explicitly` — gen 0/3/6/10, buffer nullo o
  vuoto, e 344 byte di `0xFF` devono tutti dare NULL (anti-clone).

**Cablato (2026-09-02), ma senza innesco raggiungibile.** `UI::prepareForPlacement(Pokemon&, Panel, std::string& whyNot)`
(`source/ui_input.cpp`) esegue la catena:

```
sameStoredFormat(pkm.gameType_, destGameFor(panel)) -> true, posa diretta
ohTargetGenFor(dest) == 0                           -> false, rifiuto
loadPkmFromGen(pkm.data, genOf(pkm.gameType_))
  -> transfer(handle, ohTargetGenFor(dest))
  -> getPkmBoxBytesForGen(out, ohTargetGenFor(dest))
  -> sovrascrive pkm.data + pkm.gameType_ = dest
NULL in qualunque punto -> false, `pkm` NON toccato
```

`destGameFor(Panel)` rispecchia il dispatch di `setPokemonAt`: Game+dual ->
`bankLeft_.gameType()`, Game -> `save_.gameType()`, Bank -> `bank_.gameType()`.

Agganciato ai soli siti di posa:
- **singola + swap**: un solo hook prima di `if (target.isEmpty())`, copre
  entrambi i rami (entrambi posano `heldPkm_` nello stesso pannello).
- **multi-select**: pre-conversione **atomica** di tutto `heldMulti_` prima di
  `if (positionPreserve_)`; se anche uno solo fallisce non si posa nulla, quindi
  un multi-drop parziale e' impossibile. Copre entrambi i rami
  (position-preserving e first-available).
- Siti di undo/cancel **non toccati**: il Pokemon torna al pannello d'origine,
  dove il formato e' per definizione gia' corretto.

Su fallimento: `showMessageAndWait` con il motivo e posa abortita, Pokemon
trattenuto in mano. Nessuna scrittura di byte non conformi al layout di
destinazione.

**M6 pt.5 fatto**: rimosso da `ui.cpp` il blocco `transfer` + `free(out)` +
"(mock)" del gen selector. Il selettore resta controllo di intento; se si
conferma tenendo un Pokemon, un messaggio spiega che la conversione avviene
alla posa.

Helper C++ gia' pronti: `Bank::gameType()` (`include/bank.h:15`, aggiunto
per risolvere il `GameType` di destinazione quando il pannello e' una bank).

---

## File coinvolti

| File | Stato | Nota |
|------|-------|------|
| `rust/openhome_switch/src/lib.rs` | ✅ Reale (Gen 7/8/9) | `load_save`/`get_pokemon_from_slot`/`get_pkm_box_bytes`/`transfer_pkm` reali; allocatore `CAllocator`→newlib; panic handler = abort+messaggio |
| `include/save_file_ffi.h` | ✅ Collegato | `load()` → `openhome_load_save`, `getSlot()` → `openhome_get_pokemon_from_slot` |
| `source/save_file_ffi.cpp` | ➡️ Dead code | `decrypt/encrypt/findBlock` = `SwishCrypto` per entrambi i motori (branch `useOpenHome()` identico a quello PK; dead code) |
| `source/save_file.cpp` | ✅ Collegato (lettura) | `loadSCBlock()` crea `saveHandleRust_`; `getCachedBox()` ramo OH reale; fallback PK per non-SwSh / motore PK |
| `include/save_file.h` | ✅ | `saveHandleRust_` (`unique_ptr` + deleter), `SaveFile` non-copiabile |
| `include/pokemon_ffi.h` | ⚠️ Parziale | `load/transfer/saveToSlot` reali; `decryptArray*/encryptArray*` ramo OH = copia PK (dead code — entrambi i branch chiamano `PokeCrypto`); `saveToSlot` wrapper mai invocato |
| `source/pokemon_ffi.cpp` | ➡️ Dead code | solo `_ensure_linked()` per il link |
| `source/ui.cpp` | ⚠️ Mock | gen selector chiama `transfer` reale ma scarta il risultato |
| `include/openhome_ffi.h` | ✅ | C ABI + wrapper inline `namespace OpenHomeNX`, incl. `getPkmBoxBytes` |

---

## Cose da NON rifare / decisioni aperte

- **Non rimuovere gli stub morti senza ok del SENIOR.** `openhome_save_pkm_to_file` e i wrapper `saveToSlot`/`savePkmToFile` in `pokemon_ffi.h`/`openhome_ffi.h` sono header FFI usati dall'intera l'app. Rimuoverli romperebbe il contratto ABI anche se oggi non sono invocati. Stessa cosa per i branch `useOpenHome()` dead code in `encryptArray*` e `save_file_ffi.cpp`: possono essere puliti solo quando il SENIOR decide di riscrivere il contratto FFI.
- **Non toccare `encryptArray*`/`decryptArray*` per aggiungere dispatch Rust.** La cifratura PK e OH è identica (stesso `PokeCrypto`); i branch `useOpenHome()` sono dead code, non un bug. Aggiungere un ramo Rust qui senza un bisogno reale aggiungerebbe complessità senza vantaggio.
- **Non generalizzare `openhome_get_pkm_box_bytes` oltre Pk8** finché altri loader di save non atterrano. Già in lista (prossimo lavoro #5).

---

## Prossimo lavoro

1. **Test hardware parity (bloccante per il punto 2).** Caricare un save
   SwSh con Crypto=OpenHome: **nessun dialog = lettura OH identica a PK**.
   Il check gira da solo, non serve alcun rebuild.
 2. **Scrittura OH** — ✅ operativa gen 3/7/8/9 via "Rust raw bytes → PokeCrypto encrypt". `openhome_save_pkm_to_file` = stub morto mai chiamato. I branch `useOpenHome()` in `encryptArray*`/`save_file_ffi.cpp` = dead code (rami identici). Non rimuoverli senza ok SENIOR (header FFI). Verifica hardware: se parity OK, il write path è confermato corretto.
3. **M6 — dormiente per scelta.** Il cablaggio e' completo
   (`UI::prepareForPlacement`), ma il ramo di conversione non e'
   raggiungibile finche' i due pannelli condividono `selectedGame_`.
   Aprirli a giochi diversi e' un lavoro architetturale a se': da
   affrontare solo con decisione esplicita.
4. Gen6 e sotto: porting letterale da OpenHome upstream (~1800 LOC/gen),
   additivo e testabile su host, non tocca nulla di funzionante.
5. Generalizzare `openhome_get_pkm_box_bytes` oltre Pk8 quando
   atterrano altri loader di save.

---

## Note

- PK resta default (`include/crypto_engine.h:14`)
- Loader save Rust: **solo SwSh**. Altri giochi → nessun handle OH
  (`openhome_load_save` → NULL, `loadSCBlock` guardato da `isSwSh`) → path PK.
- Gen9 wasm gate non toccato
- Questo file è un riepilogo di stato, non una specifica di implementazione

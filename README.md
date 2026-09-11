# OpenHomeNX

Box manager and cross-generation transfer for Nintendo Switch (homebrew `.nro`).

Combines the **pkHouse** UI (C++/SDL2) with the **OpenHome** backend
(Rust via FFI): box management across all generations with real cross-gen
transfer. Failures are always explicit, never silent.

## Features

- Two-panel box viewer, pick & place, multi-select, ZL/ZR box view
- Pokémon details, search/filters, learnset viewer, 9 themes, 12 languages
- Real cross-gen transfer across 13 formats (gen 1–9, PA8/PA9/PB8/PB7) with
  explicit dex-cut and move-drop, OHPKM tracking for lossless return
- Party strip with OT, grab/swap/place from party, compaction on save
- Native GB/GBC/GBA/DS/decrypted-3DS saves + SD/USB import
- Wondercard injection, automatic save backups, LED, network indicator
- Self-update from SD/GitHub releases

## Supported games

| Family | Games | Via |
|---|---|---|
| Switch | Scarlet / Violet (4.0.0), Sword / Shield (1.3.2), BDSP (1.3.0), Legends Arceus (1.1.1), Legends Z-A (2.0.2), Let's Go Pikachu/Eevee (1.0.2), FireRed/LeafGreen (incl. ES/DE/IT/FR/JA) | Installed save (`AccountManager`) |
| GBA (import) | Ruby / Sapphire / Emerald | SD/USB file |
| GB (import) | Red / Blue / Yellow, Gold / Silver / Crystal | SD/USB file |
| DS (import) | Diamond / Pearl / Platinum / HGSS, Black / White / B2W2, X / Y, Sun / Moon (decrypted) | SD/USB file |

Cross-gen bank covers all 13 stored formats (PK1/PK2/PK3/PK4/PK5/PK6/PK7/PK8/PK9/PA8/PA9/PB8/PB7).

## Import (emulators) — already scanned

* `sdmc:/switch/OpenHomeNX/import/` is always scanned (created if missing).
* Extra folders: **Settings → Data → Cartelle import** → `+ Add path…` (any `sdmc:/…` or `usb:/…`), enable/disable per path.
* USB autocheck (optional): **Settings → Data → Autocheck save su USB** scans every mounted drive's `/roms/saves` and `/roms` on hotplug — Ruby/Sapphire/Emerald, GB/GBC/GBA/DS saves are picked up without manual path.
* Rescan: **Settings → Data → Scansiona import** or replug drive.

## USB peripherals

USB drives via `libusbhsfs` (FAT-only, ISC build in `libusbhsfs/lib`). Hotplug detected, safe eject via trash icon or `Y` in game selector, LED + `debug.log` hints (`physical>0 mounted==0` → MBR/FS issue). Backups and import both work from USB; update can also be fetched from `usb:/switch/OpenHomeNX/update/` or network.

## Screenshots

![Gallery — list + preview](docs/screenshot/gallery_view.jpg)
![Classic — remodernized pkHouse grid](docs/screenshot/classic_view.jpg)

## Install (Switch)

1. Download `OpenHomeNX.nro` from the latest [release](../../releases) and copy it to
   `sdmc:/switch/OpenHomeNX/OpenHomeNX.nro` (or launch from hbmenu).
2. Start a Pokémon game at least once (creates the save), then open the app.
3. Later updates: `+` menu → *Check for update* (SD or network).

## Main controls

| Button | Action |
|---|---|
| A | Pick / place |
| B | Cancel / return |
| X | Details (Y = moves in details) |
| Y | Multi-select |
| `-` | Release (hand or details) / About |
| `+` | Menu (themes, language, update, wondercard…) |
| L/R | Previous/next box |
| ZL/ZR | Box overview |
| Stick/D-Pad up | Reach the header party |

## Build from source

Requires: devkitPro + devkitA64 + libnx (portlibs: SDL2, SDL2_image,
SDL2_ttf, curl…), Rust nightly (target `aarch64-unknown-none`,
see `rust/.cargo/config.toml`).

```sh
./release.sh          # build + dist/OpenHomeNX.nro + dist/latest.json
./release.sh serve    # as above + local server for the network updater
```

Plain `make` compiles without packaging. Never copy `.nro` into `dist/` by hand.
Rust tests run on the host (never on the Switch):

```sh
cd rust && cargo +nightly test -p openhome_switch --features std \
  --target aarch64-apple-darwin
```

## Layout

```
source/ include/   C++ frontend (pkHouse base) + FFI wrappers
rust/              Rust workspace (openhome_switch + vendored pkm_rs)
romfs/             sprites, fonts, strings, boxart
tools/             save utilities and test fixtures
```

## Credits

OpenHomeNX would not exist without these projects — thanks to the authors and
communities maintaining them:

- **[pkHouse](https://github.com/Insektaure/pkHouse)** by Insektaure —
  user interface, graphics and Switch homebrew base (SDL2/libnx)
- **[OpenHome](https://github.com/andrewbenington/OpenHome)** by andrewbenington —
  Rust backend (cross-gen conversions, formats, crypto)
- **[PKHeX](https://github.com/kwsch/PKHeX)** by kwsch — reference for
  save-format specifications
- [libnx](https://github.com/switchbrew/libnx) / devkitPro — Switch toolchain

## License

GNU GPL v3 (`LICENSE`). Combines GPLv2 code (pkHouse) with GPL-3.0+ code
(OpenHome): the combined work is distributed under GPL v3.

## Disclaimer

Amateur project, not affiliated with or endorsed by Nintendo, Creatures Inc.,
GAME FREAK, The Pokémon Company, or the authors of the projects above.
Pokémon © Nintendo/Creatures Inc./GAME FREAK inc. Use only with legitimately
owned game copies; always back up your saves.

---

# OpenHomeNX (Italiano)

Box manager e trasferimento cross-generazione per Nintendo Switch (homebrew `.nro`).

Unisce l'interfaccia di **pkHouse** (C++/SDL2) con il backend di **OpenHome**
(Rust via FFI): gestione box su tutte le generazioni con trasferimento
cross-gen reale. I fallimenti sono sempre espliciti, mai silenziosi.

## Funzioni

- Box viewer a due pannelli, pick & place, multi-selezione, box view ZL/ZR
- Dettaglio Pokémon, ricerca/filtri, learnset viewer, 9 temi, 12 lingue
- Transfer cross-gen reale su 13 formati (gen 1–9, PA8/PA9/PB8/PB7) con
  dex-cut e move-drop espliciti, tracking OHPKM per il ritorno lossless
- Party strip con OT, grab/swap/posa dal party, compattamento al salvataggio
- Save nativi GB/GBC/GBA/DS/3DS-decifrati + import da SD/USB
- Wondercard injection, backup automatici dei save, LED, indicatore rete
- Self-update da SD/release GitHub

## Giochi supportati

| Famiglia | Giochi | Via |
|---|---|---|
| Switch | Scarlatto / Violetto (4.0.0), Spada / Scudo (1.3.2), Diamante Lucente / Perla Splendente (1.3.0), Leggende Arceus (1.1.1), Leggende Z-A (2.0.2), Let's Go Pikachu/Eevee (1.0.2), Rosso Fuoco / Verde Foglia (incl. ES/DE/IT/FR/JA) | Save installato (`AccountManager`) |
| GBA (import) | Rubino / Zaffiro / Smeraldo | File SD/USB |
| GB (import) | Rosso / Blu / Giallo, Oro / Argento / Cristallo | File SD/USB |
| DS (import) | Diamante / Perla / Platino / HGSS, Nero / Bianco / B2W2, X / Y, Sole / Luna (decifrati) | File SD/USB |

Banca cross-gen: tutti i 13 formati (PK1/PK2/PK3/PK4/PK5/PK6/PK7/PK8/PK9/PA8/PA9/PB8/PB7).

## Import (emulatori) — già scansionato

* `sdmc:/switch/OpenHomeNX/import/` è sempre scansionato (creato se manca).
* Altre cartelle: **Impostazioni → Dati → Cartelle import** → `+ Aggiungi percorso…` (qualsiasi `sdmc:/…` o `usb:/…`), attivazione per percorso.
* Autocheck USB (opzionale): **Impostazioni → Dati → Autocheck save su USB** scansiona ogni drive montato in `/roms/saves` e `/roms` all'hotplug — save Ruby/GB/GBA/DS rilevati senza percorso manuale.
* Riscansione: **Impostazioni → Dati → Scansiona import** o ricollegando la chiavetta.

## Periferiche USB

Chiavette USB via `libusbhsfs` (solo FAT, build ISC in `libusbhsfs/lib`). Hotplug rilevato, espulsione sicura via icona cestino o `Y` nel selettore giochi, LED + hint `debug.log` (`physical>0 mounted==0` → MBR/FS). Backup e import funzionano da USB; update anche da `usb:/switch/OpenHomeNX/update/` o rete.

## Screenshot

![Galleria — lista + anteprima](docs/screenshot/gallery_view.jpg)
![Classica — griglia pkHouse rimodernizzata](docs/screenshot/classic_view.jpg)

## Installazione (Switch)

1. Scarica `OpenHomeNX.nro` dall'ultima [release](../../releases) e copialo in
   `sdmc:/switch/OpenHomeNX/OpenHomeNX.nro` (o avvialo da hbmenu).
2. Avvia un gioco Pokémon almeno una volta (crea il save), poi apri l'app.
3. Aggiornamenti successivi: menu `+` → *Check for update* (SD o rete).

## Comandi principali

| Tasto | Azione |
|---|---|
| A | Prendi / posa |
| B | Annulla / restituisci |
| X | Dettaglio (Y = mosse nel dettaglio) |
| Y | Selezione multipla |
| `-` | Rilascia (mano o dettaglio) / About |
| `+` | Menu (temi, lingua, update, wondercard…) |
| L/R | Box precedente/successivo |
| ZL/ZR | Panoramica box |
| Stick/D-Pad su | Raggiunge il party in testata |

## Build da sorgente

Requisiti: devkitPro + devkitA64 + libnx (portlibs: SDL2, SDL2_image,
SDL2_ttf, curl…), Rust nightly (target `aarch64-unknown-none`,
vedi `rust/.cargo/config.toml`).

```sh
./release.sh          # build + dist/OpenHomeNX.nro + dist/latest.json
./release.sh serve    # come sopra + server locale per l'updater di rete
```

Solo `make` compila senza impacchettare. Mai copiare `.nro` a mano in `dist/`.
I test Rust girano sull'host (mai sulla Switch):

```sh
cd rust && cargo +nightly test -p openhome_switch --features std \
  --target aarch64-apple-darwin
```

## Layout

```
source/ include/   frontend C++ (base pkHouse) + wrapper FFI
rust/              workspace Rust (openhome_switch + pkm_rs vendored)
romfs/             sprite, font, stringhe, boxart
tools/             utility save e fixture di test
```

## Crediti

Senza questi progetti OpenHomeNX non esisterebbe — grazie agli autori e alle
community che li mantengono:

- **[pkHouse](https://github.com/Insektaure/pkHouse)** di Insektaure —
  interfaccia utente, grafica e base homebrew Switch (SDL2/libnx)
- **[OpenHome](https://github.com/andrewbenington/OpenHome)** di andrewbenington —
  backend Rust (conversioni cross-gen, formati, crittografia)
- **[PKHeX](https://github.com/kwsch/PKHeX)** di kwsch — riferimento per le
  specifiche dei formati di salvataggio
- [libnx](https://github.com/switchbrew/libnx) / devkitPro — toolchain Switch

## Licenza

GNU GPL v3 (`LICENSE`). Combina codice GPLv2 (pkHouse) con codice GPL-3.0+
(OpenHome): l'opera combinata è distribuita sotto GPL v3.

## Disclaimer

Progetto amatoriale non affiliato né approvato da Nintendo, Creatures Inc.,
GAME FREAK, The Pokémon Company, né dagli autori dei progetti citati.
Pokémon © Nintendo/Creatures Inc./GAME FREAK inc. Usare solo con copie dei
giochi legittimamente possedute; fate sempre backup dei salvataggi.

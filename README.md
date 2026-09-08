# OpenHomeNX

Box manager e trasferimento cross-generazione per Nintendo Switch (homebrew `.nro`).

Unisce l'interfaccia di **pkHouse** (C++/SDL2) con il backend di **OpenHome**
(Rust via FFI): visualizzazione e gestione box su tutte le generazioni con
trasferimento cross-gen reale e mai silenzioso (fallimenti sempre espliciti).

## Funzioni

- Box viewer a due pannelli, pick & place, multi-selezione, box view ZL/ZR
- Dettaglio Pokémon, ricerca/filtri, learnset viewer, 9 temi, 12 lingue
- Transfer cross-gen reale su 13 formati (gen 1–9, PA8/PA9/PB8/PB7) con
  dex-cut e move-drop espliciti, tracking OHPKM per il ritorno lossless
- Party strip con OT, grab/swap/posa dal party, compattamento al salvataggio
- Save nativi GB/GBC/GBA/DS/3DS-decifrati + import da SD/USB
- Wondercard injection, backup automatici, LED, indicatore rete WiFi/LAN
- Self-update da SD/rete (vedi `Updater.md`)

Stato dettagliato in `AGENTS.md` (milestone M1–M6).

## Requisiti

- devkitPro + devkitA64 + libnx (portlibs: SDL2, SDL2_image, SDL2_ttf, curl…)
- Rust nightly (target `aarch64-unknown-none`, vedi `rust/.cargo/config.toml`)
- Una Switch con homebrew launcher per l'esecuzione

## Build

```sh
./release.sh          # build + dist/OpenHomeNX.nro + dist/latest.json
./release.sh serve    # come sopra + server locale per l'updater di rete
```

Solo `make` compila senza impacchettare. Mai copiare `.nro` a mano in `dist/`.

## Layout

```
source/ include/   frontend C++ (base pkHouse) + wrapper FFI
rust/              workspace Rust (openhome_switch + pkm_rs vendored)
romfs/             sprite, font, stringhe, boxart
tools/             utility dev (serve_upload.py, script vari)
docs/archive/      documenti storici superati
```

## Crediti

- pkHouse di Insektaure (interfaccia e base Switch)
- OpenHome di andrewbenington (backend Rust)
- PKHeX di kwsch (riferimento specifiche formati)
- libnx / devkitPro (toolchain Switch)

## Licenza

Da definire prima della pubblicazione.

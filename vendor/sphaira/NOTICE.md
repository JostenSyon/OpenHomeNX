# Provenienza di questo codice

Il contenuto di questa cartella (`owo.cpp`/`owo.hpp` e i sorgenti sotto
`source/yati/nx/`) e' preso da **Sphaira**, di ITotalJustice:

https://github.com/ITotalJustice/sphaira

usato con licenza GPL-3.0 (vedi `LICENSE` in questa stessa cartella,
identica a quella del progetto originale e a quella di OpenHomeNX).

Cosa e' stato preso e cosa no: solo il creatore di forwarder (`owo.cpp`/
`owo.hpp`) e le due utility `yati/nx/ns.cpp` e `yati/nx/keys.cpp` da cui
dipende -- non l'app Sphaira nella sua interezza (niente della sua UI,
del suo installer NSP/NSZ generale, del suo browser file, ecc.).

Modifiche fatte rispetto all'originale (per farlo compilare con la
toolchain di questo progetto):
- `#embed "exefs/main"` / `#embed "exefs/main.npdm"` in forma quoted
  (accanto al sorgente, in `source/exefs/`) invece che `<...>`, perche'
  devkitA64 GCC 15.2 non cerca i path di `-I` per `#embed`.
- Alcuni file (`owo.cpp`, `keys.cpp`, `ns.cpp`, `shim.cpp`) compilati in
  C++23 invece di C++20 (serve `std::byteswap`), il resto della repo
  resta C++20 -- vedi `Makefile`.

Il file `tools/sphaira_hbl/` (il piccolo eseguibile ExeFS incorporato nel
forwarder tramite gli `#embed` sopra) e' a sua volta una ricompilazione
di `nx-hbloader` (Atmosphere-NX / switchbrew) con licenza propria: vedi
`tools/sphaira_hbl/nx-hbloader.LICENSE.md`.

#pragma once
#include <string>
#include "game_type.h"

// Rilevamento emulatori esterni per i giochi "file-backed" (rom scansionate
// dall'import, non titoli Switch con titleId). V1: solo mGBA (GBA), solo
// rilevamento automatico -- nessuna configurazione manuale, come richiesto.
// Se l'utente lo tiene altrove, la voce in Impostazioni > Sistema semplicemente
// non compare (niente popup, niente picker di file per adesso).
namespace Emulator {

// Cerca mGBA.nro in una manciata di posizioni comuni della community
// (sdmc:/switch/...). Ritorna il path completo se trovato, "" altrimenti.
// Economica (poche stat()) ma non gratis: va chiamata una tantum (es.
// all'apertura di Impostazioni), mai ogni frame.
std::string findMgba();

// Deduce il percorso della rom accanto al file di salvataggio dato.
// mGBA abbina rom e save per NOME (stesso nome base, cambia solo
// l'estensione) -- se non si chiamano uguale non vede la corrispondenza,
// quindi qui non si inventa nulla: si prova lo stesso nome del save nella
// sua stessa cartella con le estensioni note della generazione (g in base
// a isGen1File/isGen2File/isImportedFile). Ritorna "" se non trovata o se
// la generazione non e' emulabile con mGBA (es. Gen4 DS).
std::string findRomForSave(const std::string& savePath, GameType g);

// Avvia mGBA con la rom indicata, al posto del processo corrente: stesso
// meccanismo del self-update in ui_selectors.cpp (envSetNextLoad + uscita
// pulita, hbloader chainloada il target). NON esce lui stesso: il
// chiamante deve fermare il loop principale se ritorna true (vedi
// UI::requestLaunchInEmulator). Ritorna false senza fare nulla se manca
// uno dei due path o se l'ambiente non supporta il nextLoad (es. avviato
// da un salto Album/Library Applet).
bool launchInMgba(const std::string& mgbaPath, const std::string& romPath);

} // namespace Emulator

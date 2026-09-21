#pragma once
#include <string>
#include <vector>
#include "import_scan.h" // ImportedGame (save path + GameType)

// Copertine 2D per i giochi file-backed (ROM GBA/GBC/GB/NDS).
//
// R36S: solo riuso locale — cover di Skyscraper (images/ + gamelist.xml
// accanto alle ROM) copiate in cache/covers/<sys>/<rom>.png. Mai download.
// Switch: cache hit -> uso; se manca e la rete e' pronta, download
// on-demand da libretro-thumbnails e cache; altrimenti messaggio offline.
// Nulla e' hostato nel repo: solo cache locale su SD.
//
// Il rendering (drawGameArt) non cambia: legge gia' gameIconCache_ per
// primo, e loadGameIcons() la riempie da findCachedCover().
namespace Boxart {

// Stile cover (Sviluppatore -> Stile boxart, persistito in settings.cfg).
// Su Switch sceglie la sottocartella libretro-thumbnails del download;
// in locale guida la priorita' delle sorgenti Skyscraper.
enum class Style { Cover = 0, Shot = 1, Title = 2 };

struct ScrapeResult {
    int found = 0; // cover in cache (hit + nuove)
    int total = 0; // ROM risolte dai save importati
};

// Path cache per una ROM ("" se romPath vuoto). L'estensione segue il
// file sorgente (.png/.jpg); findCachedCover prova entrambe.
std::string coverCachePath(const std::string& basePath, const std::string& romPath);
// "" se nessuna cover in cache, altrimenti il path esistente.
std::string findCachedCover(const std::string& basePath, const std::string& romPath);

// Riempie cache/covers per ogni gioco importato con ROM risolvibile.
// progress(msg) puo' essere nullptr; e' chiamata per mostrare showWorking().
ScrapeResult scrape(const std::string& basePath,
                    const std::vector<ImportedGame>& games);

// Cancella tutti i file in cache/covers/ (tornano le tile composte
// logo+sfondo+label da romfs). Ritorna il numero di file rimossi.
int clearCache(const std::string& basePath);

} // namespace Boxart

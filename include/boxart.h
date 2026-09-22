#pragma once
#include <string>
#include <vector>
#include "import_scan.h" // ImportedGame (save path + GameType)

// Copertine per i giochi file-backed (ROM GBA/GBC/GB/NDS).
//
// Stili (Sviluppatore -> Stile boxart): Locale (arte locale prima, fallback
// download 2D), Box2d (solo download Named_Boxarts), Box3d (arte 3D locale,
// fallback 2D). Screenshot/loghi mai usati. Cache separata per stile in
// cache/covers/<sys>/<rom>.<stile>.png. Nulla e' hostato nel repo.
//
// Il rendering (drawGameArt) non cambia: legge gia' gameIconCache_ per
// primo, e loadGameIcons() la riempie da findCachedCover().
namespace Boxart {

// Stile cover (Sviluppatore -> Stile boxart, persistito in settings.cfg).
enum class Style { Locale = 0, Box2d = 1, Box3d = 2 };

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

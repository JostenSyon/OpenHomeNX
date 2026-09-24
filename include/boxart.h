#pragma once
#include <string>
#include <vector>
#include <functional>
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
    int skipped = 0; // miss recenti saltati senza rete (vedi marker .miss)
    bool cancelled = false; // interrotto con B
};

// "" se nessuna cover in cache, altrimenti il path esistente.
std::string findCachedCover(const std::string& basePath, const std::string& romPath);

// Riempie cache/covers per ogni gioco importato con ROM risolvibile — un
// giro assicura TUTTI i tag (locale/2d/3d), poi cambiare stile legge solo
// la cache senza riscaricare. Tag presenti o con miss fresca si saltano.
// `progress` puo' essere nullptr: se presente viene chiamata per ogni
// ROM e tag con msg "(%d%%) i/n\nnome [tag]" per la barra di showWorking().
// Stessa shape di UpdateProgressFn (std::function<void(const std::string&)>).
// `cancel` (opzionale): flag cooperativo controllato a ogni ROM e fra i
// tag — la UI lo alza sul tasto B (si ferma dopo il download corrente).
using ScrapeProgressFn = std::function<void(const std::string&)>;
ScrapeResult scrape(const std::string& basePath,
                    const std::vector<ImportedGame>& games,
                    ScrapeProgressFn progress = nullptr,
                    const bool* cancel = nullptr);

// Cancella tutti i file in cache/covers/ (tornano le tile composte
// logo+sfondo+label da romfs). Ritorna il numero di file rimossi.
int clearCache(const std::string& basePath);
// Cancella solo i marker .miss (ritenta le saltate SENZA riscaricare le
// cover buone). Ritorna il numero di marker rimossi.
int clearMissMarkers(const std::string& basePath);

} // namespace Boxart

#pragma once
#include <string>

// Unico file impostazioni (key=value): settings.cfg in basePath_.
// Sostituisce i 10 file sparsi (theme/zoom/gallery/crypto/autocheck_usb/
// launcher_prompt_seen/noled/language/defaultuser/quickmenu).
// Restano fuori apposta: favorites.cfg (cambia spesso), import_paths.cfg,
// update.cfg/.off (override updater), overrideOT.cfg (override galleria),
// gallery_cache.dat (cache), debug.enable (flag leggibile).
//
// init() va chiamata una volta a boot (idempotente): se settings.cfg manca,
// importa i file legacy una volta sola e li cancella. Mai perdita: i default
// sono quelli storici di ogni loader.
namespace Settings {
void init(const std::string& basePath);

int themeIndex();
void setThemeIndex(int v);
int zoomGrow();
void setZoomGrow(int v);
int galleryLayout();
void setGalleryLayout(int v);
int cryptoEngine();
void setCryptoEngine(int v); // 1 = OH, 0 = PK
bool autoCheckUsb();
void setAutoCheckUsb(bool v);
bool launcherSeen();
void setLauncherSeen();
bool noLed();
std::string language();
void setLanguage(const std::string& v);
std::string defaultUser(); // "" = chiedi
void setDefaultUser(const std::string& v);
bool quickMenu();
void setQuickMenu(bool v);
bool radialMenu(); // menu radiale (Classica): on/off, default off finche' non implementato
void setRadialMenu(bool v);
bool tradeAnim(); // animazione schermo diviso allo scambio: on/off, default on
void setTradeAnim(bool v);
std::string dockOrder(); // CSV: "Backpack,Banks,SaveMenu,Trade,Eject"
void setDockOrder(const std::string& v);
bool dockVisible(); // mostra/nascondi dock inferiore
void setDockVisible(bool v);
// Impostazioni -> Sviluppatore -> Ricerca dispositivi: host/credenziali del
// Filebrowser web esposto da ArkOS/JELOS/ROCKNIX (vedi remote_sync.h).
std::string remoteSyncHost(); // "" = non ancora configurato (si chiede l'IP)
void setRemoteSyncHost(const std::string& v);
std::string remoteSyncUser(); // default "ark" (credenziale di default ArkOS)
void setRemoteSyncUser(const std::string& v);
std::string remoteSyncPass(); // default "ark"
void setRemoteSyncPass(const std::string& v);
} // namespace Settings

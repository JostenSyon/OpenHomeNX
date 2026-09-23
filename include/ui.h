#pragma once
#include "save_file.h"
#include "bank.h"
#include "bank_manager.h"
#include "account.h"
#include "theme.h"
#include "wondercard.h"
#include "import_paths.h"
#include "import_scan.h"
#include "autocheck_usb.h"
#include "backpack.h"
#include "remote_sync.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <functional>

// Which panel the cursor is on
enum class Panel { Game, Bank };

// App-level screen state
enum class AppScreen { ProfileSelector, GameSelector, BankSelector, MainView };

// Purpose of text input popup
enum class TextInputPurpose {
    CreateBank, RenameBank, RenameBoxName,
    SearchSpecies, SearchOT, SearchLevelMin, SearchLevelMax,
    ImportPathEntry, EditUpdateUrl
};

// Rows of the "+" game-selector menu. A single list drives both the popup's
// draw order and its input handling — the old parallel hardcoded row-count
// arithmetic (see v0.1.37's alignment bug) drifts every time a row is added.
enum class GameSelMenuAction { SwitchCore, DebugLog, ClearLog, SendLog, SendSave, CrashReport, ImportSettings, CheckUpdate, OpenSettings, ToggleDock, ReorderDock, RemoteBox, Exit };

// Search filter enums
enum class GenderFilter { Any, Male, Female, Genderless };
enum class PerfectIVFilter { Off, AtLeastOne, All6 };
enum class RibbonFilter { Off, HasRibbon, HasMark, HasAny };
enum class SearchMode { List, Highlight };

// Search filter criteria
struct SearchFilter {
    uint16_t speciesId = 0;       // national dex ID (0 = any)
    std::string speciesName;
    std::string otName;
    bool filterShiny  = false;
    bool filterEgg    = false;
    bool filterAlpha  = false;
    GenderFilter gender = GenderFilter::Any;
    PerfectIVFilter perfectIVs = PerfectIVFilter::Off;
    RibbonFilter ribbonFilter = RibbonFilter::Off;
    int levelMin = 0;
    int levelMax = 0;
    SearchMode mode = SearchMode::Highlight;
};

// Search result entry
struct SearchResult {
    Panel panel;
    int box;
    int slot;
    std::string speciesName;
    uint8_t level;
    bool isShiny;
    bool isEgg;
    bool isAlpha;
    uint8_t gender;
    std::string otName;
};

// Righe categoria 5 (Sviluppatore) in ordine di visualizzazione.
// UNICA fonte di verita' per count/label/value/activate di cat.5:
// aggiungere una voce = un enumeratore qui + un push_back in devRowList +
// un case nelle tre funzioni (era in ui_settings.cpp, spostato qui perche'
// la tendina tiene la lista disegnata nei membri).
enum class DevRow {
    DbgToggle, QuickMenu,
    ClearBp, Normalize, ClearGal,
    SendLog, Crash,
    Rename, Style, Update, Clear, ShowRomsNoSave, DevSync,
};

// Animazione "tendina" per voci di menù che appaiono/scompaiono (copia
// esatta della molla di Impostazioni > Aspetto per Zoom/Menu radiale):
// 0 = visibili, 1 = nascoste, con overshoot durante la transizione.
// La logica/nav resta istantanea, solo il disegno interpola.
struct CollapseAnim {
    float v = 0.0f;
    float vel = 0.0f;
    void reset(float target) { v = target; vel = 0.0f; }
    // Fermi sul target (nessuna ghost da disegnare).
    bool settled(float target) const { return v == target && vel == 0.0f; }
    // Avanza verso target con la stessa molla del riordino dock (K/D
    // uguali). Ritorna true mentre si muove (serve ridisegno).
    bool step(float target) {
        constexpr float K = 0.35f, D = 0.65f;
        float disp = v - target;
        if (disp == 0.0f && vel == 0.0f) return false;
        vel += -disp * K - vel * D;
        v += vel;
        if (std::fabs(v - target) < 0.01f && std::fabs(vel) < 0.01f) {
            v = target;
            vel = 0.0f;
            return false;
        }
        return true;
    }
};

// Riepilogo di un lato (locale/remoto) per il popup di confronto della
// Sincronizza DevSync — vedi UI::showSyncCompareDialog.
struct SyncSideInfo {
    std::string trainer;
    long playTimeSeconds = -1;   // -1 = non disponibile per questo formato
    int  dexCaught = -1;
    int  dexTotal  = -1;
    bool dexSupported = false;
    long long modifiedUnix = 0;  // 0 = sconosciuto
};

// Cursor position within the two-panel display
struct Cursor {
    Panel panel = Panel::Game;
    int box     = 0;
    int col     = 0; // 0-5 (or 0-4 for LGPE)
    int row     = 0; // 0-4

    int slot(int cols = 6) const { return row * cols + col; }
};

// update.cfg parsed in memory (vedi commento sul formato accanto a
// parseUpdateCfgFile() in ui_update.cpp). Era locale all'anonymous
// namespace di ui_selectors.cpp prima dello split in piu' file -- serve
// sia a ui_update.cpp (parsing) sia a ui_selectors.cpp/ui_backups.cpp/
// ui_settings.cpp (lettura), quindi ora e' un tipo condiviso qui.
struct UpdateCfg {
    std::string url, token;
    std::string channel;   // "" o "stable" = release stabili, "beta" = pre-release
    long backupMb = 256;   // tetto CUMULATIVO auto-backup titoli installati
    long backupMbSd = 32;  // tetto cumulativo save file-backed (SD, piccoli)
    bool autoOn = true;    // check al boot: default ON se la chiave `auto` manca
};

// Main UI class - manages rendering and input for the two-panel box viewer.
class UI {
public:
    bool init();
    void shutdown();
    // holdMs: how long the logo stays up (pumps events meanwhile).
    // fadeOut=false leaves the last logo frame on screen so init work below
    // doesn't play over a black gap; call showSplash(0, true) when ready.
    void showSplash(int holdMs = 2500, bool fadeOut = true);
    int  drawBodyText(const std::string& body, int startY, const std::string& footer);
    std::vector<std::string> wrapText(const std::string& line, TTF_Font* f, int maxW);
    // Righe wrappate dell'intero body (una per riga video) per i dialog
    // scrollabili. -1/0/+1 da D-pad su/giu o stick sinistro Y (con repeat),
    // 0 se nessun controller o nessuna direzione premuta.
    std::vector<std::string> wrapBodyLines(const std::string& body);
    int  dialogScrollDir(uint32_t now, uint32_t& lastTick, int& lastDir);
    int  drawBodyWindow(const std::vector<std::string>& lines, int first,
                        int topY, int bottomY, int lineH,
                        SDL_Color col, SDL_Color footCol);
    void showMessageAndWait(const std::string& title, const std::string& body);
    // Come showMessageAndWait ma con lo sprite dello scambio sopra il testo
    // (conferma visiva, non solo testuale) -- species2 != 0 per lo scambio
    // doppio (Karrablast/Shelmet), mostra entrambi affiancati.
    void showTradeResultDialog(const std::string& title, const std::string& body,
                               uint16_t species1, uint16_t species2 = 0);
    bool showConfirmDialog(const std::string& title, const std::string& body);
    // Scelta tipo banca alla creazione: 0=cross-gen (A), 1=specifica (Y),
    // -1=annullato (B, non crea nulla). Vedi ui_bank.cpp per l'uso.
    int  pickNewBankKind(const std::string& title, const std::string& body);
    // Popup di conferma per "Sincronizza" (DevSync): due riquadri affiancati
    // (locale/remoto) con allenatore, Pokédex, tempo di gioco e data
    // salvataggio, cosi' si vede il criterio usato per scegliere la
    // direzione invece del solo testo "il save locale e' piu' recente".
    // criterionKey e' una StrKey (DevSyncCriterion*) gia' risolta dal
    // chiamante in base a howDecided.
    // alreadySynced=true (tempo di gioco e Pokédex identici su entrambi i
    // lati): niente bordo evidenziato su nessuno dei due lati, "=" al posto
    // della freccia, messaggio "gia' sincronizzati" al posto del criterio --
    // stessi due riquadri, non un popup a se stante, cosi' il colpo d'occhio
    // resta coerente con la normale conferma.
    bool showSyncCompareDialog(const std::string& gameName, const SyncSideInfo& local,
                                const SyncSideInfo& remote, bool remoteNewer,
                                const char* criterionKey, bool alreadySynced = false);
    // Popup di scoperta "Installa launcher" (icona app + testo), mostrato
    // una sola volta in vita: solo informativo (il pulsante vero sta in
    // Impostazioni > Sistema, non ancora costruito), quindi niente scelta
    // Si/No qui -- stesso schema single-dismiss di showMessageAndWait.
    void showLauncherPromptPopup();
    void showWorking(const std::string& msg);
    // Percorso mGBA rilevato (vuoto = non trovato/non ricontrollato). V1:
    // solo rilevamento automatico, controllato una tantum -- vedi
    // ensureMgbaChecked()/Emulator::findMgba() (mai ogni frame).
    std::string mgbaPath_;
    bool mgbaChecked_ = false;
    void ensureMgbaChecked();
    void setAppletMode(bool mode) { appletMode_ = mode; }
    bool isDualBankMode() const { return appletMode_ || allBanksMode_; }
    void run(const std::string& basePath, const std::string& savePath);
    void backupOnExitIfNeeded(); // backup una tantum se dirty+modificato

    // Pending-update fast path (OpenHomeNX.nro.new present). Consolidates it
    // into OpenHomeNX.nro; if a bounce into the fresh .nro is needed, draws the
    // "Updating…" card and returns true (caller must exit). Returns false when
    // there is nothing to bounce (caller continues a normal boot). Needs the
    // renderer already up (init()).
    bool tryUpdateBounce(const std::string& basePath);

private:
    SDL_Window*          window_    = nullptr;
    SDL_Renderer*        renderer_  = nullptr;
    SDL_GameController*  pad_       = nullptr;
    // true se pad_ ha gia' un bind nativo per BACK/START dopo aver caricato
    // l'eventuale mappatura extra (vedi UI::init()) -- se true, il fallback
    // raw-joybutton in ui_input.cpp deve stare zitto per non duplicare
    // l'evento (altrimenti Select/Start scatterebbero due volte a pressione).
    bool padNativeBack_  = false;
    bool padNativeStart_ = false;
    TTF_Font*            font_      = nullptr;
    TTF_Font*            fontSmall_ = nullptr;
    TTF_Font*            fontLarge_ = nullptr;
    TTF_Font*            fontAbout_ = nullptr; // 20pt, descrizioni popup About

    // Sprite cache: (national dex ID | form << 16) -> texture
    std::unordered_map<uint32_t, SDL_Texture*> spriteCache_;
    std::unordered_map<uint32_t, SDL_Texture*> shinySpriteCache_;

    // Ribbon sprite cache: filename -> texture
    std::unordered_map<std::string, SDL_Texture*> ribbonSpriteCache_;
    SDL_Texture* getRibbonSprite(const std::string& filename);

    // Ball sprite cache: ball ID -> texture
    std::unordered_map<uint8_t, SDL_Texture*> ballSpriteCache_;
    SDL_Texture* getBallSprite(uint8_t ballId);

    // Type icon cache: type ID -> texture
    std::unordered_map<uint8_t, SDL_Texture*> typeSpriteCache_;
    SDL_Texture* getTypeSprite(uint8_t typeId);

    // Text texture cache: avoids re-rasterising identical strings every frame
    struct TextCacheKey {
        std::string text;
        TTF_Font*   font;
        uint32_t    colorVal; // packed RGBA
        bool operator==(const TextCacheKey& o) const {
            return text == o.text && font == o.font && colorVal == o.colorVal;
        }
    };
    struct TextCacheKeyHash {
        size_t operator()(const TextCacheKey& k) const {
            size_t h = std::hash<std::string>{}(k.text);
            h ^= std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.font)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.colorVal) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct TextCacheEntry {
        SDL_Texture* tex;
        int w, h;
    };
    std::unordered_map<TextCacheKey, TextCacheEntry, TextCacheKeyHash> textCache_;
    const TextCacheEntry& getTextEntry(const std::string& text, TTF_Font* f, SDL_Color color);
    void clearTextCache();

    // Status icons
    SDL_Texture* iconShiny_      = nullptr;
    SDL_Texture* iconAlpha_      = nullptr;
    SDL_Texture* iconShinyAlpha_ = nullptr;

    // Box-state icons for the ZL/ZR all-boxes overview
    SDL_Texture* iconBoxFull_     = nullptr;
    SDL_Texture* iconBoxEmpty_    = nullptr;
    SDL_Texture* iconBoxNonEmpty_ = nullptr;
    SDL_Texture* iconVault_        = nullptr; // cassaforte rotonda "tutte le banche"
    SDL_Texture* iconEject_        = nullptr; // espulsione sicura USB
    SDL_Texture* iconSettings_     = nullptr; // ingranaggio impostazioni
    SDL_Texture* iconWifi_         = nullptr; // stato rete in alto a dx
    SDL_Texture* iconLan_          = nullptr; // cavo al posto del wifi
    SDL_Texture* iconDebug_        = nullptr; // bug accanto al wifi con debug on
    SDL_Texture* iconArrow_        = nullptr; // frecce pagine (dx ruotata 180)
    SDL_Texture* iconPack_         = nullptr; // zaino eventi/strumenti (48px 1:1)
    SDL_Texture* iconRocket_       = nullptr; // "Avvia" nel menu radiale (48px 1:1)
    SDL_Texture* iconFloppy_       = nullptr; // "Salvataggi" nel menu radiale (48px 1:1)
    SDL_Texture* iconTrade_        = nullptr; // "Scambio": asset pronto, non ancora in radialItems_ (48px 1:1)
    SDL_Texture* iconDevBox_       = nullptr; // "Box remoto" in dock: apre openRemoteBox() (48px 1:1)
    SDL_Texture* iconDevSync_      = nullptr; // "DevSync" in dock: apre remoteSyncTestRow() (48px 1:1)
    SDL_Texture* iconDevLink_      = nullptr; // device remoto trovato: barra di stato accanto a wifi/lan (36px 1:1)

    // Menu radiale (solo layout Classico, dietro Settings::radialMenu()):
    // alla conferma di una tile apre un piccolo arco di scorciatoie sopra
    // la tile stessa, al posto di andare dritti in banca. Elenco voci in
    // radialItems_ (RadialAction) cosi' aggiungerne una nuova in futuro e'
    // solo una entry in piu' + un case nello switch di radialMenuActivate().
    enum class RadialAction { Launch, Bank, Backpack, SaveMenu, Trade };
    bool showRadialMenu_ = false;
    bool radialClosing_ = false;   // true durante l'animazione di chiusura
    int radialGameIdx_ = -1;       // indice in availableGames_ della tile aperta
    int radialCursor_ = 0;         // voce a fuoco in radialItems_
    float radialAnim_ = 0.0f;      // 0..1, apertura/chiusura (easing per-frame)
    int radialAnchorX_ = 0, radialAnchorY_ = 0; // centro-alto della tile, fissato all'apertura
    std::vector<int> radialItems_; // RadialAction disponibili per la tile aperta

    // Dock inferiore (riga bassa del selettore giochi): ordine persistente e
    // personalizzabile via Settings::dockOrder/dockVisible. Il disegno e le
    // zone tap sono guidati da dockLayout(); la navigazione D-pad usa
    // dockMoveFocus() cosi' l'ordine utente non desincronizza mai i flag.
    struct DockState {
        enum class Item { Backpack, Banks, SaveMenu, Trade, RemoteBox, DevSync, Eject };

        std::vector<Item> customOrder; // ordine utente (default: factory)
        bool visible = true;           // mostra/nascondi dock
        bool reorderMode = false;      // modalita' riordino attiva
        int reorderFocusIdx = 0;       // indice in customOrder in riordino
        uint32_t reorderEnterTime = 0; // per timeout auto-uscita
    };

    DockState dockState_;
    bool dockLoaded_ = false; // dockStateLoad() lazy al primo draw (Settings pronto)

    // Focus unificato del selettore giochi (Classica + Galleria).
    // Sostituisce i vecchi flag sparsi gameSelOn*: un solo setter azzera
    // gli altri, cosi' aggiungere una voce dock non richiede di toccare
    // ogni transizione. L'ingranaggio (Settings) resta fuori dock con
    // regole proprie, ma condivide il modello per l'esclusivita'.
    enum class GSFocus : uint8_t {
        Grid = 0, // cursore su lista/griglia giochi
        Avatar,   // avatar profilo in alto a sx
        ChevLeft, // freccia pagina sx
        ChevRight,// freccia pagina dx
        Launch,   // tastino Avvia anteprima Galleria
        Dock,     // voce dock (gsDockItem_)
        Settings  // ingranaggio in basso a dx (fuori dock)
    };
    GSFocus gsFocus_ = GSFocus::Grid;
    DockState::Item gsDockItem_ = DockState::Item::Backpack; // se focus == Dock

    // Setter unico (azzera tutto il resto) + rimozione mirata.
    void gsSetFocus(GSFocus f, DockState::Item dockItem = DockState::Item::Backpack);
    void gsUnfocus(GSFocus f); // a Grid solo se il focus corrente e' f
    bool gsDockIs(DockState::Item it) const; // focus == Dock && voce == it
    bool gsChevronActive() const; // focus su uno dei due chevron

    void dockStateLoad();
    void dockStateSave() const;
    void dockStateResetToDefault();
    void dockStateEnterReorderMode(int startIdx);
    void dockStateExitReorderMode(bool save);
    void dockStateSwapItems(int i, int j);
    bool dockStateItemVisible(DockState::Item item) const;
    // Voci visibili nell'ordine utente + posizioni x centrate (stile riga bassa).
    struct DockSlot { DockState::Item item; int cx; };
    std::vector<DockSlot> dockLayout() const;
    void drawDock();
    // Focus esclusivo su una voce (azzera tutti gli altri flag dock).
    void dockFocusItem(DockState::Item item);
    void dockClearFocus();
    bool dockFocusedItem(DockState::Item& out) const;
    bool dockHasFocus() const;
    // Sposta il focus alla voce visibile precedente/successiva; true se mosso.
    bool dockMoveFocus(int dir);
    // Prima/ultima voce visibile (atterraggi e ripartenza gear).
    bool dockFocusFirst();
    bool dockFocusLast();
    // Attiva la voce puntata (stesse azioni del tap/conferma).
    void dockActivateFocused(bool& running);

    // Game-selector logos for imported (titleId-less) games — see init()'s
    // loadLogo(). Keyed by GameType since there are only a handful of these.
    std::unordered_map<GameType, SDL_Texture*> gameLogoCache_;
    // HD box art for RSE tiles (romfs:/boxart/, from user-provided PNGs).
    // Same lifetime policy as gameLogoCache_ (app lifetime, never freed).
    std::unordered_map<GameType, SDL_Texture*> boxArtCache_;
    // Per-game tile backgrounds (romfs:/backgrounds/). Drawn over the flat
    // color rect (which stays as fallback), e.g. Emerald artwork.
    std::unordered_map<GameType, SDL_Texture*> tileBgCache_;

    // Screen dimensions (Switch: 1280x720, R36S nativo 4:3: 640x480)
#ifdef OH_LINUX
    static constexpr int SCREEN_W = 640;
    static constexpr int SCREEN_H = 480;
#else
    static constexpr int SCREEN_W = 1280;
    static constexpr int SCREEN_H = 720;
#endif

    // Layout
#ifdef OH_LINUX
    // Nativo 4:3 (640px): due pannelli affiancati come da originale,
    // scalati per stare fianco a fianco (305+305). Colonne fisse da save.
    static constexpr int PANEL_W   = 305;
    static constexpr int PANEL_X_L = 10;
    static constexpr int PANEL_X_R = 325;
    static constexpr int BOX_HDR_Y = 10;
    static constexpr int BOX_HDR_H = 40;
    static constexpr int GRID_Y    = 55;

    // Grid cells scalate: 6 col x 47px = 302 <= 305; 5 righe x 70 = 366
    // (55+366=421, status bar a 445).
    static constexpr int CELL_W   = 47;
    static constexpr int CELL_H   = 70;
    static constexpr int CELL_PAD = 4;

    // Sprite size within a cell
    static constexpr int SPRITE_SIZE = 34;
#else
    static constexpr int PANEL_W   = 610;
    static constexpr int PANEL_X_L = 15;
    static constexpr int PANEL_X_R = 655;
    static constexpr int BOX_HDR_Y = 10;
    static constexpr int BOX_HDR_H = 40;
    static constexpr int GRID_Y    = 55;

    // Grid cells: 6 cols x 5 rows
    static constexpr int CELL_W   = 96;
    static constexpr int CELL_H   = 120;
    static constexpr int CELL_PAD = 5;

    // Sprite size within a cell
    static constexpr int SPRITE_SIZE = 68;
#endif

    // Status bar
    static constexpr int STATUS_BAR_H = 40;

    // Box view overlay layout
    static constexpr int BV_COLS         = 8;
    static constexpr int BV_CELL_W       = 140;
    static constexpr int BV_CELL_H       = 32;
    static constexpr int BV_CELL_PAD     = 4;
    static constexpr int BV_MINI_SPRITE  = 32;
    static constexpr int BV_MINI_CELL    = 36;
    static constexpr int BV_MINI_PAD     = 2;
    static constexpr int BV_PREVIEW_PAD  = 8;
    static constexpr int BV_PREVIEW_HDR  = 22;

    // Layout selettore giochi (Classico = griglia storica, Galleria = lista
    // + anteprima grande). Persistito in gallery.cfg, vedi theme.h/.cpp.
    enum class GameSelectorLayout : int { Classic = 0, Gallery = 1 };
    GameSelectorLayout gameSelectorLayout_ = GameSelectorLayout::Gallery;
    float galScrollX_ = 0.0f; // scroll fluido galleria (lerp verso il target)

    // Theme
    int themeIndex_ = DEFAULT_THEME_INDEX;
    const Theme* theme_ = nullptr;
    const Theme* lastTheme_ = nullptr; // tracks theme changes for text cache invalidation
    const Theme& T() const { return *theme_; }

    // Theme selector state
    bool showThemeSelector_ = false;
    int  themeSelCursor_    = 0;
    int  themeSelOriginal_  = 0;

    // Language selector state
    bool showLanguageSelector_ = false;
    int  langSelCursor_        = 0;
    std::vector<std::string> langList_;

    // Import-path settings state: which folders/USB suffixes to scan for
    // emulator save files (docs/archive/GEN_PLAN.md Fase 2/5). Row 0 of the popup is the
    // autoCheckUsb_ toggle, rows 1..importPaths_.size() are the configured
    // paths, the last row is "+ Add path...".
    bool showImportSettings_    = false;
    int  importSettingsCursor_  = 0;
    std::vector<ImportPathEntry> importPaths_;
    bool autoCheckUsb_ = false; // persisted via autocheck_usb.cfg
    std::vector<GameSelMenuAction> gameSelMenuActions() const;
    void drawImportSettingsPopup();

    // Folder browser state (opened from "+ Add path..."): pick a directory
    // to scan instead of typing it via swkbd. Empty folderBrowserPath_ =
    // roots view (sdmc:/ + mounted umsN:/); otherwise a path with trailing
    // '/'. Entries are subdirectories (selectable) plus files (shown dimmed
    // for orientation, not selectable), sorted dirs-first.
    bool showFolderBrowser_ = false;
    std::string folderBrowserPath_;
    struct FolderEntry { std::string name; bool isDir; };
    std::vector<FolderEntry> folderEntries_;
    int folderCursor_  = 0;
    int folderScroll_  = 0;
    // DPad/stick hold-repeat state (single BUTTONDOWN would crawl otherwise).
    int folderRepeatDir_ = 0; // -1/0/+1
    uint32_t folderRepeatTime_ = 0;
    bool folderRepeatFast_ = false;
    void folderMoveCursor(int dir);
    void openFolderBrowser();
    void refreshFolderEntries();
    void handleFolderBrowserInput(const SDL_Event& event);
    void drawFolderBrowserPopup();

    // Games found by scanning importPaths_ + (if autoCheckUsb_) every mounted
    // USB device's /roms/saves and /roms (Ruby/Sapphire/Emerald so far — see
    // isImportedFile()). Rescanned whenever availableGames_ is rebuilt
    // (profile selection, applet-mode entry, USB hotplug) and consumed by
    // selectGame() to bypass AccountManager::mountSave() for these GameTypes.
    std::vector<ImportedGame> importedGames_;

    // Box Remoto: apre save gia' presenti sul dispositivo remoto (R36S/
    // Filebrowser) dentro lo stesso selettore giochi, scaricati in un file
    // temporaneo che si comporta come un ImportedGame qualunque. Attivo solo
    // mentre remoteBoxActive_ e' true; in uscita il save (se modificato)
    // torna al dispositivo remoto previa conferma (vedi UI::returnToGameSelector).
    struct RemoteBoxEntry {
        GameType type = GameType::EMERALD;
        std::string tmpPath;
        std::string host, token, remoteSavePath;
        // Istantanea dimensione+mtime del tmp file appena scaricato (o
        // appena rispedito con successo, vedi UI::returnToGameSelector):
        // permette a UI::closeRemoteBox() di scoprire un save modificato
        // mai rispedito al device, senza un flag "dirty" a parte da tenere
        // sincronizzato a mano in ogni punto che tocca il save.
        long long snapSize = -1;
        long long snapMtime = 0;
    };
    bool remoteBoxActive_ = false;
    std::vector<RemoteBoxEntry> remoteBoxEntries_;
    std::vector<GameType> savedAvailableGames_;
    std::vector<ImportedGame> savedImportedGames_;

    void appendImportedGames();               // scans importPaths_, extends availableGames_
    void rescanImportedGames();      // re-scan in place + popup on newly found games
    // "" if this occurrence has no save (native title, or a ROM-only entry
    // from Settings::showRomsWithoutSave() -- see hasSave on ImportedGame).
    std::string importedSavePath(GameType game, int occurrence = 0) const;
    std::string importedSourceTag(GameType game, int occurrence = 0) const; // small on-tile badge text
    // The actual playable ROM path for this occurrence, whichever way it's
    // known: derived from its save (Emulator::findRomForSave) when hasSave,
    // or the ROM path stored directly for a save-less entry. "" if this
    // occurrence isn't an import at all (native title, no ImportedGame).
    // Single place both loadGameIcons() (cover lookup) and the launcher
    // (requestLaunchGame/isGameLaunchableAt) resolve the ROM from, so they
    // can never drift onto two different files for the same tile.
    std::string importedRomPath(GameType game, int occurrence = 0) const;
    // True only for a save-less ROM-only entry (Settings::showRomsWithoutSave()):
    // launch-only tile, no box/party/items/trade to show for it.
    bool importedIsRomOnly(GameType game, int occurrence = 0) const;
    // Which occurrence of `game` is the tile at availableGames_[cursor]?
    // Counts same-type tiles before it (duplicates = same game, other device).
    int importedOccurrence(int cursor) const;

    // Game selector menu state (+ button: Switch Core / Debug log / Exit)
    bool showGameSelMenu_ = false;
    int  gameSelMenuCursor_ = 0;

    // Pagina impostazioni (ingranaggio selettore): 6 sezioni con header.
    bool showSettings_ = false;
    bool importFromSettings_ = false; // chiudendo import torna alle impostazioni
    int  setCat_ = 0; // 0 Utente, 1 Aspetto, 2 Motore, 3 Dati, 4 Update, 5 Debug, 6 Info
    int zoomGrow_ = 12; // zoom card selezionata (px, step 4: niente aliasing)
    int zoomCard_ = -2, zoomPrev_ = -2;
    float zoomT_ = 1.0f;
    int setRow_ = 0;
    bool setFocusLeft_ = true; // true = colonna sezioni, false = righe destra
    void openSettings();
    int defaultUserIndex() const; // profilo da defaultuser.txt, -1 = chiedi
    void drawSettingsPopup();
    void handleSettingsInput(const SDL_Event& event, bool& running);
    int settingsRowCount(int cat) const;
    std::string settingsRowLabel(int cat, int row) const;
    std::string settingsRowValue(int cat, int row);
    void settingsRowActivate(int cat, int row, int dir, bool& running);
    // Sistema: riga Emulatore (2 se rilevato o ghost in chiusura, -1 se
    // assente) e riga Conferma uscita (3 con emulatore, 2 senza).
    int sysEmuRow() const;
    int sysConfirmRow() const;
    // Impostazioni -> Sviluppatore -> Ricerca dispositivi (stage 1: login +
    // test di lista sul Filebrowser web di ArkOS/JELOS/ROCKNIX via LAN).
    void remoteSyncTestRow();
    // Helpers per il nuovo flusso picker + 3 bottoni (evita spam di dialog per ogni gioco)
    int pickRemoteSyncGame(const std::vector<SyncCandidate>& candidates);
    int pickRemoteSyncAction(const SyncCandidate& c);
    // Login comune a remoteSyncTestRow() e openRemoteBox(): stesso device
    // gia' noto -> scansione LAN -> IP a mano -> credenziali a mano. Ritorna
    // false (con messaggio gia' mostrato) se l'utente annulla o il login fallisce.
    bool remoteSyncEnsureLogin(const std::string& title, std::string& host,
                                std::string& user, std::string& pass, std::string& token);
    void openRemoteBox();
    void closeRemoteBox();
    std::string promptTextBlocking(const std::string& header, const std::string& initial, int maxLen);

    // Debug save popup (Switch X sul gioco con debug on): Backup save /
    // Restore latest backup / Send save. Opera sullo stesso occurrence che
    // aprirebbe A (niente lista separata: semplice, niente UI in piu).
    bool showSaveMenu_ = false;
    int  saveMenuCursor_ = 0;
    GameType saveMenuGame_ = GameType::EMERALD;
    int  saveMenuOcc_ = 0;
    void openSaveMenu(GameType g, int occ);
    void drawSaveMenuPopup();
    void sendSaveFor(GameType g, int occ);

    // Game picker popup (layout Classica): a differenza della Galleria (che
    // evidenzia un gioco) la dock non sa su quale gioco agire, quindi
    // SaveMenu/Trade aprono una lista popup per scegliere. Galleria agisce
    // direttamente sul gioco evidenziato.
    enum class GamePickTarget { SaveMenu, Trade };
    bool showGamePick_ = false;
    GamePickTarget gamePickTarget_ = GamePickTarget::SaveMenu;
    std::vector<int> gamePickAvail_; // indici in availableGames_ selezionabili
    int gamePickCursor_ = 0;
    int gamePickScroll_ = 0;
    void openGamePick(GamePickTarget t);
    void drawGamePickPopup();
    // availableGames_[i] ha un save raggiungibile ora (file importato o
    // titolo installato con nome di save noto)? Stessa regola dello zaino.
    bool tileHasUsableSave(int i) const;
    std::string manualBackupDir(GameType g) const;
    std::string autoBackupDir(GameType g) const;
    // alreadyMounted: se il chiamante ha gia' "save:/" montato per questo
    // stesso gioco (es. lo zaino con backpackSaveMnt_), passarlo qui evita
    // di richiamare account_.mountSave(), che smonterebbe quello attivo
    // (AccountManager ha un solo slot di mount) lasciando il chiamante con
    // un mount ormai fantasma -> scritture successive fallite in silenzio
    // (bug 2026-09-13: regalo fossile ok in memoria ma mai salvato).
    // manual=true -> manualBackupDir() (tag "MAN", mai potato): backup
    // chiesto esplicitamente dall'utente (menu debug "Backup save").
    // manual=false -> autoBackupDir() (tag "AUTO", soggetto al tetto/pruning
    // come tutti gli auto): backup scattato da un'azione automatica (es. lo
    // zaino prima di un regalo) -- per definizione NON e' manuale anche se
    // il chiamante lo fa "una tantum per sessione".
    bool backupGameSave(GameType g, std::string& out, const std::string& alreadyMounted = "",
                         bool manual = true);
    struct BackupListEntry { std::string path; std::string label; };
    std::vector<BackupListEntry> collectBackupEntries(GameType g);
    bool restoreBackupEntry(GameType g, const std::string& entry);
    bool deleteBackupEntry(const std::string& entry); // ZL+ZR nella lista backup, con conferma
    void tryDeleteHighlightedBackup(); // ZL+ZR: conferma + elimina + ricarica lista
    // Voce lista backup: solo unita ripristinabili (file per i save
    // file-backed, dir con file dentro per i titoli installati) + etichetta
    // "[AUTO]/[MAN] data-ora" cosi i file sciolti tipo "main" non compaiono.
    // Lista backup sfogliabile (manual + auto + auto-apertura), newest first.
    bool showBackupList_ = false;
    int  backupListCursor_ = 0;
    int  backupListScroll_ = 0;
    std::vector<BackupListEntry> backupListEntries_;
    GameType backupListGame_ = GameType::EMERALD;
    bool backupListZlHeld_ = false, backupListZrHeld_ = false; // edge-detect ZL+ZR: elimina backup evidenziato
    void openBackupList(GameType g);
    void drawBackupListPopup();
    // Lista crash report scritti da Atmosphere (sdmc:/atmosphere/crash_reports/,
    // fallback fatal_errors/): sfogliabile come i backup, ordinata per data
    // di modifica (piu recente in cima) cosi' l'ultimo crash e' subito
    // selezionato senza dover cercare a mano. A invia col solito upload
    // save (stesso endpoint/URL di update.cfg), B torna indietro.
    bool showCrashList_ = false;
    int  crashListCursor_ = 0;
    int  crashListScroll_ = 0;
    std::vector<BackupListEntry> crashListEntries_;
    std::vector<BackupListEntry> collectCrashReportEntries();
    void openCrashList();
    void drawCrashListPopup();
    void sendCrashReportNow(const std::string& path);
    // Auto-backup best-effort all'apertura dei save file-backed (cap 10).
    void autoBackupFileSave(GameType g, const std::string& path);
    // Tetto spazio auto-backup per gioco da update.cfg (backup_mb[_sd],
    // default 256/32; 0 = illimitato). Mai i manuali.
    long backupCapMb(bool fileBacked) const;
    std::vector<std::string> autoBackupEntries(GameType g) const;
    bool autoBackupNeeded(GameType g, const std::string& srcFile, uint64_t srcSize, long srcMt, bool haveSrc);
    uint64_t pruneBackupsToCap(GameType g, bool fileBacked);

    // Wondercard list state
    bool showWondercardList_ = false;
    int  wcListCursor_  = 0;
    int  wcListScroll_  = 0;
    std::vector<WCInfo> wcList_;

    // Zaino Gen3: sinistra = catalogo trasferibili (sempre visibile),
    // destra = lista giochi finche' non ne scegli uno, poi al suo posto
    // lo zaino VERO del gioco scelto (B torna alla lista giochi, come le
    // banche). Opera su scratch SaveFile del gioco evidenziato
    // (load/gift/save con backup), mai sul save_ della main view.
    // (vista audit: righe = anomalie + voci di giornale, vedi backpackJournal_)
    bool showBackpack_ = false;
    bool backpackFocusItems_ = true; // false = pannello giochi/zaino a destra
    bool backpackAudit_ = false;     // vista verifica invece della lista voci
    std::vector<GameType> backpackGames_;
    std::vector<int> backpackOcc_;   // occurrence in availableGames_ per voce
    int backpackGameCursor_ = 0, backpackGameScroll_ = 0;
    std::vector<Backpack::ItemDef> backpackDefs_;
    std::vector<Backpack::ItemDef> backpackItems_; // filtrate per gioco
    // Righe sinistra: catalogo trasferibili (mai header, sempre voci di
    // indice idx). La stessa struct e' riusata per backpackBag_ (destra,
    // zaino VERO), dove i pocket hanno header non selezionabili (cursore
    // li salta) - vedi backpackLeftStep/backpackBagStep.
    struct BackpackLeftRow { bool header = false; int idx = 0; int pocket = -1; };
    std::vector<BackpackLeftRow> backpackLeft_;
    std::vector<SaveFile::GbaBagSlot> backpackGameBag_; // slot non vuoti dello scratch (GBA)
    std::vector<SaveFile::DsBagSlot> backpackGameBagDs_; // idem DS (Gen4/5)
    std::vector<SaveFile::GbBagSlot> backpackGameBagGb_; // voci presenti (GB Gen1/2, compatte)
    bool backpackBagIsDs_ = false; // destra/audit leggono i vettori DS invece dei GBA
    bool backpackBagIsGb_ = false; // idem GB (mai entrambe vere)
    int backpackLeftCursor_ = 0, backpackLeftScroll_ = 0;
    // Tab categoria del catalogo. GBA: 0=Sfere,1=MN,2=MT,3=Consumabili,
    // 4=Speciali,5=Bacche. Gen4: +6=Posta,7=Med,8=Lotta. Gen5: senza Sfere
    // (le sfere stanno negli Strumenti): 0=MN,1=MT,2=Consumabili,3=Speciali,
    // 4=Bacche,5=Med. Gen1: solo 0=Consumabili. Gen2: 0=Sfere,1=MN,2=MT,
    // 3=Consumabili,4=Speciali (niente Bacche). Vedi bpTabCount/bpCatTabFor/
    // bpCatTabKey in ui_backpack.
    int backpackCatTab_ = 0;
    bool backpackZlHeld_ = false, backpackZrHeld_ = false; // edge-detect ZL/ZR
    // Destra a gioco scelto: righe dello zaino VERO (al posto della
    // lista giochi), stesso tipo di riga ma cursore/scroll propri.
    std::vector<BackpackLeftRow> backpackBag_;
    int backpackBagCursor_ = 0, backpackBagScroll_ = 0;
    int backpackQty_ = 1;
    bool backpackBaseMode_ = false;
    std::vector<Backpack::Anomaly> backpackAnoms_;
    std::vector<Backpack::DsAnomaly> backpackDsAnoms_;
    std::vector<Backpack::GbAnomaly> backpackGbAnoms_;
    std::vector<Backpack::JournalRow> backpackJournal_; // regali (audit): una riga per {item, pocket}
    int backpackAuditCursor_ = 0, backpackAuditScroll_ = 0;
    SaveFile backpackSave_;          // scratch
    std::string backpackSavePath_, backpackSaveMnt_;
    GameType backpackGame_ = GameType::EMERALD;
    bool backpackLoaded_ = false;
    bool backpackGameChosen_ = false; // gioco scelto esplicito con A (prima le voci sono solo lista)
    std::unordered_map<int, int> backpackOwned_; // itemId -> count totale
    std::unordered_set<int> backpackBackedUp_;   // GameType già backuppati qui
    void openBackpack();
    // Come sopra ma, se selIdx punta un gioco borsa valido, lo apre subito
    // (zaino di destinazione già scelto, focus alle voci).
    void openBackpackOn(int selIdx);
    void closeBackpack();
    void backpackLoadGame(GameType g, int occ);
    void backpackReloadItems();
    void backpackRefreshAudit();
    void drawBackpackPopup();
    void handleBackpackInput(const SDL_Event& event);
    void backpackDoGift();
    void backpackDoTake();
    void backpackLeftStep(int& cursor, int dir);
    void backpackBagStep(int& cursor, int dir);
    // Tiene la quantita' dentro il max della voce sotto cursore: senza
    // questo, impostata su un oggetto a max 99, spostandoti su uno a max
    // 5 la barra mostrava "x99 (max 5)" (il dono lo clampava comunque,
    // ma era ingannevole - bug 2026-09-13). Va chiamato a ogni cambio
    // di cursore/ricarica del catalogo.
    void backpackClampQty();
    // Cambia la tab categoria del catalogo (dir=+-1, wrap) e ricarica
    // le righe di sinistra filtrate sulla nuova tab.
    void backpackCatTabStep(int dir);
    // Passo levetta orizzontale: sposta il fuoco tra catalogo
    // (sinistra) e zaino vero/lista giochi (destra). Idempotente
    // (nessun rimbalzo se richiamato piu' volte gia' a destinazione),
    // quindi e' sicuro chiamarlo anche dal repeat per-frame.
    void backpackFocusStep(int dir);
    // Un passo (dir=+-1) su gioco o voce a fuoco: chiamato dal tick
    // per-frame in ui_selectors.cpp per il repeat levetta (vedi
    // "Joystick repeat navigation"), mai da dentro handleBackpackInput
    // (a levetta ferma puo' non arrivare mai un altro evento SDL).
    void backpackStickStep(int dir);
    void backpackDoFixSelected();
    bool backpackPersist(const std::string& why);

    // PK file import list state (.pk1/.pk2 picker, mirrors wondercards)
    struct PkFileInfo {
        std::string filename;  // bare name for display
        std::string path;      // full path
        int gen = 0;           // 1 = .pk1, 2 = .pk2
        uint16_t species = 0;  // 0 = unreadable (listed as invalid)
        bool valid = false;
        bool selected = false; // multi-import selection (X toggles)
        bool imported = false; // already imported this session (stays listed)
    };
    bool showPkImportList_ = false;
    int  pkImportCursor_  = 0;
    int  pkImportScroll_  = 0;
    std::vector<PkFileInfo> pkImportList_;

    // Self-trade (menu Scambio): lista scorrevole su party + TUTTI i box,
    // filtrata alle sole specie che possono evolvere per scambio (pronte o
    // in attesa dello strumento). Eleggibilità ricalcolata dal save ogni
    // volta che si apre (rebuildTradeCandidates).
    bool showTradeList_ = false;
    int  tradeCursor_  = 0;
    int  tradeScroll_  = 0;
    struct TradeCandidate { int box; int slot; }; // box == -1 -> party
    std::vector<TradeCandidate> tradeCandidates_;
    void openTradeList();
    void rebuildTradeCandidates();
    void doTradeEvolve(int candidateIdx);
    void playTradeEvolveAnim(uint16_t fromSpecies, uint16_t toSpecies);

    // Debug test-mon generator (menu Generate, solo debug): segnalini
    // on-demand per i test HW. Tabella estendibile in genMonTable().
    struct GenMonDef {
        const char* label;
        uint16_t species;
        uint8_t level;
        uint16_t moves[4];
    };
    bool showGenMonList_ = false;
    int  genMonCursor_  = 0;
    int  genMonScroll_  = 0;
    std::vector<GenMonDef> genMonList_;
    static std::vector<GenMonDef> genMonTable();
    void drawGenMonListPopup();
    void handleGenMonListInput(const SDL_Event& event);
    bool importGeneratedMon(const GenMonDef& def);

    // Party-strip focus (DS saves, DEBUG ONLY): DPad-UP from the top grid
    // row moves focus to the OT strip minis; A opens a READ-ONLY detail
    // popup (release/export blocked there). -1 = grid focused.
    int partyCursor_ = -1;
    int detailParty_ = -1;

    // Learnset viewer state (X in the detail view)
    bool showLearnset_ = false;
    int  learnsetCursor_  = 0;
    int  learnsetScroll_  = 0;
    // (move id, level); level 0 = evolution move.
    std::vector<std::pair<uint16_t, uint8_t>> learnset_;
    uint16_t learnsetSpecies_ = 0;
    // Currently equipped moves (marked with * in the list).
    uint16_t learnsetEquipped_[4] = {0, 0, 0, 0};

    // Search/Filter state
    bool showSearchFilter_  = false;
    bool showSearchResults_ = false;
    SearchFilter searchFilter_;
    std::vector<SearchResult> searchResults_;
    int  searchFilterCursor_ = 0;
    int  searchLevelFocus_   = 0;   // 0=min, 1=max
    int  searchResultCursor_ = 0;
    int  searchResultScroll_ = 0;
    bool searchHighlightActive_ = false;
    std::unordered_set<uint64_t> searchMatchSet_;

    // Species picker state (letter → species list)
    bool showSpeciesLetterPicker_ = false;
    bool showSpeciesListPicker_   = false;
    int  speciesLetterCursor_ = 0;   // 0="-", 1-26=A-Z
    int  speciesLetterScroll_ = 0;
    int  speciesListCursor_   = 0;
    int  speciesListScroll_   = 0;
    std::vector<uint16_t> availableSpecies_;    // all species for current game
    std::vector<uint16_t> speciesPickerList_;   // species filtered by letter

    // Joystick navigation
    static constexpr int16_t STICK_DEADZONE   = 16000;
    static constexpr int16_t TRIGGER_DEADZONE = 8000;
    static constexpr uint32_t STICK_INITIAL_DELAY = 400; // ms before first repeat
    static constexpr uint32_t STICK_REPEAT_DELAY  = 200; // ms between repeats
    int stickDirX_ = 0;  // -1, 0, +1
    int stickDirY_ = 0;
    uint32_t stickMoveTime_ = 0; // last move timestamp
    bool stickMoved_ = false;    // has initial move fired?
    // Timestamp di quando la direzione CORRENTE della levetta e' iniziata
    // (azzerato in updateStick() ogni volta che la direzione cambia, incluso
    // il ritorno a zero): usato per accelerare lo scroll verticale della
    // lista Galleria quando tenuta ferma a lungo (vedi handleGameSelectorInput).
    uint32_t stickHoldStart_ = 0;
    void updateStick(int16_t axisX, int16_t axisY);

    // L/R shoulder button repeat
    static constexpr uint32_t BUMPER_INITIAL_DELAY = 400;
    static constexpr uint32_t BUMPER_REPEAT_DELAY  = 200;
    bool lHeld_ = false;
    bool rHeld_ = false;
    uint32_t bumperRepeatTime_ = 0;
    bool bumperMoved_ = false;

    // App screen state
    AppScreen screen_ = AppScreen::GameSelector;
    bool appletMode_ = false;
    std::string basePath_;
    std::string savePath_;
    // Box Remoto: true quando savePath_ punta a un file temporaneo scaricato
    // da un dispositivo remoto (vedi RemoteBoxEntry sopra). Letto da
    // UI::returnToGameSelector() per rispedire il save al device all'uscita.
    bool activeSaveIsRemote_ = false;
    std::string activeRemoteHost_, activeRemoteToken_, activeRemoteSavePath_;

    // Scoperta automatica in background del device remoto (vedi
    // remoteSyncWorkerPoll in remote_sync.h): true dal momento in cui il
    // worker trova un host valido. Per ora solo loggato/consumato in
    // UI::run() -- l'icona in dock che lo mostra all'utente e' un passo
    // successivo, non ancora implementato.
    bool remoteDeviceAvailable_ = false;
    std::string remoteDeviceHost_, remoteDeviceToken_;

    // Account manager
    AccountManager account_;

    // Profile selector state
    int profileSelCursor_ = 0;
    int selectedProfile_ = -1;

    // Game selector state (focus periferico: vedi GSFocus/gsSetFocus)
    GameType selectedGame_ = GameType::ZA;
    int gameSelCursor_ = 0;
    int gameSelPage_ = 0;
    float touchStartX_ = 0, touchStartY_ = 0;
    bool touchDown_ = false, touchMoved_ = false;
    void selectorTap(float px, float py, bool& running);
    // Ultimo indice cursore della pagina (continuita' tornando indietro con
    // L / swipe / frecce: si riparte dall'ultima icona, non dalla prima).
    int pageLastCursor(int page) const;
    // Effetto molla/budino sul riordino dock (entry: Y su icona dock o menu +):
    // offset x per slot, il draw lo insegue con molla smorzata (overshoot +
    // ritorno), azzerato a riposo.
    static constexpr int MAX_DOCK_SLOTS = 5; // dock: Backpack, Banks, SaveMenu, Trade, Eject
    float dockSlide_[MAX_DOCK_SLOTS] = {};
    float dockSlideVel_[MAX_DOCK_SLOTS] = {};
    void dockSpringStep(std::vector<DockSlot>& slots); // aggiorna dockSlide_ per frame
    // Tendine (vedi CollapseAnim): una per gruppo di voci che
    // appaiono/scompaiono — Aspetto (Zoom/Menu radiale in Galleria),
    // Sistema (Emulatore se rilevato), Aggiornamenti (Modifica se custom),
    // Sviluppatore (righe da devRowList). Sostituisce i vecchi float
    // appearanceCollapse_/Vel_ sparsi.
    CollapseAnim appearanceAnim_;
    CollapseAnim emuAnim_;
    CollapseAnim modUrlAnim_;
    CollapseAnim devAnim_;
    // Tendina cat 5: lista disegnata (resta la precedente durante la
    // transizione), ghost in apertura/chiusura, direzione.
    std::vector<DevRow> devDrawList_;
    std::vector<DevRow> devGhosts_;
    bool devExpanding_ = false;
    // Lista righe Sviluppatore da usare (transizione in corso o live).
    std::vector<DevRow> devShownList() const;
    // Animazione pulsanti bassi: posizioni/alpha correnti -> target per frame.
    float ejectBtnX_ = -1.0f;
    float ejectBtnA_ = 0.0f;
    int ejectAnimStage_ = 0; // 0 idle, 1 fade eject dopo espulsione, 2 rientro vault
    int selPageShown_ = 0;   // pagina disegnata (segue gameSelPage_ con slide)
    float selSlide_ = 0.0f;  // offset slide in unita pagina (-1..1)
    // Anteprima Galleria: indice disegnato (insegue gameSelCursor_ con
    // slide verticale), stesso schema di selPageShown_/selSlide_ sopra.
    int galSelShown_ = -1;   // -1 = non inizializzato (niente animazione al primo frame)
    float galSlide_ = 0.0f;  // offset slide in unita "altezza pannello" (-1..1)
    void ejectUsbDevices();
    void sendLogNow();
    bool sendAvailable() const; // debug on + override url attivo
    uint64_t prunePoolToCap(bool fileBacked); // tetto cumulativo, oldest-first
    static void writeAutoInfo(const std::string& entry, uint64_t bytes, long mt);
    bool exitBackedUp_ = false; // gioco corrente gia coperto all'uscita
    bool backupTitleNow(GameType g, const std::string& mountPath, const std::string& saveFile);
    // update.cfg / update.cfg.off (sorgente update custom on/off)
    static bool hasCustomUrlFile(const std::string& basePath);
    static void findUpdateCfgFiles(const std::string& basePath, std::string& cfg, std::string& off);
    static std::string customUrlAny(const std::string& basePath);
    static bool writeUpdateCfgUrl(const std::string& basePath, const std::string& url);
    static bool writeUpdateCfgKey(const std::string& basePath, const std::string& key,
                                  const std::string& value);
    bool bottomButtonsAnim(); // true mentre lerp banche/eject non a target
    bool allBanksMode_ = false;       // entered bank selector via "View All Banks"
    bool bankRightCrossGen_ = false;  // right-panel bank selector showing ALL games (cross-gen), normal mode
    std::vector<GameType> availableGames_;
    std::unordered_map<GameType, SDL_Texture*> gameIconCache_;
    // Colore medio della cover (campionato una volta al caricamento in
    // loadGameIcons()): sfondo "vetro" della vista Galleria.
    std::unordered_map<GameType, SDL_Color> gameAccentCache_;
    SDL_Color computeAccentColor(SDL_Surface* surf) const;
    std::unordered_map<GameType, int> gameBankCounts_;
    void refreshBankCounts();
    void loadGameIcons();
    void freeGameIcons();
    void enterAllBanksMode();

    // Preferiti galleria: ZR toggla, stella al posto del pallino, ordine stabile in cima.
    std::unordered_set<int> favorites_;
    bool favTriggerHeld_ = false;
    // Avvio rapido galleria: ZL sul gioco evidenziato. Titoli Switch nativi
    // (titleId reale) -> appletRequestLaunchApplication; emulati (rom
    // scansionata) -> mGBA se rilevato. Non disponibile in appletMode_
    // (salto Album/Library Applet): ne' il chainload ne' un titleId diverso
    // da 0 sono garantiti li', meglio non rischiare per ora. Chiede sempre
    // conferma esplicita.
    bool launchTriggerHeld_ = false;
    void requestLaunchGame(bool& running);
    // Vero se availableGames_[idx] e' lanciabile ora (titolo Switch nativo,
    // oppure emulato con mGBA rilevato + rom trovata accanto al save).
    // Stessa condizione usata sia per l'hint "ZL: Avvia" in basso sia per il
    // tastino "Avvia" nel pannello anteprima Galleria -- unica cosi' le due
    // non possano disallinearsi. Non const: puo' innescare ensureMgbaChecked().
    bool isGameLaunchableAt(int idx);
    // Menu radiale Classica -- vedi enum RadialAction e i membri radial*_
    // sopra. idx e' un indice in availableGames_ (stessa convenzione di
    // gameSelCursor_). Le funzioni di draw/input vivono in ui_selectors.cpp
    // insieme al resto dell'input del selettore giochi.
    void openRadialMenu(int idx);
    void closeRadialMenu();
    void radialMenuActivate(bool& running);
    void handleRadialMenuInput(const SDL_Event& event, bool& running);
    void radialMenuTap(float px, float py, bool& running);
    void drawRadialMenu();
    bool isFavorite(GameType g) const { return favorites_.count(static_cast<int>(g)) != 0; }
    void loadFavorites();
    void saveFavorites() const;
    // Popup di scoperta "Installa launcher" (categoria Sistema): mostrato
    // una sola volta. Stesso schema di noled.cfg (source/led.cpp) --
    // esistenza del file = flag true, nessun contenuto da leggere/scrivere.
    bool hasSeenLauncherPrompt() const;
    void markLauncherPromptSeen() const;
    // Azione della riga "Installa launcher" in Sistema: vedi il commento
    // sopra la definizione (source/ui_selectors.cpp) per lo stato attuale
    // (solo permessi/conferma, install NSP vera e propria non ancora fatta).
    void installLauncherForwarder();
    // Tentativo vero e proprio (permessi gia' assunti ok da chi chiama):
    // condiviso tra il flusso Impostazioni (dopo conferma) e la pressione
    // diretta di A nel popup di scoperta showLauncherPromptPopup().
    void attemptLauncherForwarderInstall();
    void toggleFavorite(GameType g);
    void applyFavoritesOrder();

    // Owned save + bank manager (initialized after game selection)
    SaveFile save_;
    BankManager bankManager_;
    Bank bank_;
    std::string activeBankName_;
    std::string activeBankPath_;

    // Dual-bank state (applet mode: left panel shows bankLeft_ instead of save_)
    Bank bankLeft_;
    std::string leftBankName_;
    std::string leftBankPath_;
    Panel bankSelTarget_ = Panel::Bank;

    // Bank selector state
    int  bankSelCursor_ = 0;
    int  bankSelScroll_ = 0;
    bool showDeleteConfirm_ = false;
    bool newBankCrossGen_ = false;
    // true quando "crea banca" e' stata avviata dalla vista cross-gen
    // (bankRightCrossGen_): quel percorso scende temporaneamente in
    // modalita' singolo-gioco solo per calcolare bankFolderNameOf()/
    // banksDir_ corretti, ma senza questo flag la vista restava li'
    // (lista di un solo gioco) anche dopo la creazione, finche' non si
    // usciva e rientrava -- vedi UI::commitTextInput().
    bool bankCreateWasCrossGenView_ = false;

    TextInputPurpose textInputPurpose_;
    std::string textInputBuffer_;
    int textInputCursorPos_ = 0;
    std::string renamingBankName_;
    int renamingBoxIdx_ = 0;
    Bank* renamingBoxBank_ = nullptr;

    // Pre-computed display attributes for a single slot (avoids repeated
    // species lookups, string formatting, and accessor calls during rendering).
    struct SlotDisplay {
        bool     empty    = true;
        bool     egg      = false;
        bool     shiny    = false;
        bool     alpha    = false;
        uint8_t  gender   = 2;  // 0=male, 1=female, 2=none
        uint16_t species  = 0;  // national dex ID (for sprite lookup)
        uint8_t  form     = 0;  // form index (for form-aware sprites)
        uint8_t  level    = 0;
        uint8_t  ball     = 0;  // ball id (party-marker icon in box cells)
        std::string name;       // truncated display name (≤10 chars)
    };

    // Cache of SlotDisplay per (panel, box). Invalidated on mutations.
    struct BoxDisplayKey {
        Panel panel;
        int   box;
        bool operator==(const BoxDisplayKey& o) const { return panel == o.panel && box == o.box; }
    };
    struct BoxDisplayKeyHash {
        size_t operator()(const BoxDisplayKey& k) const {
            return std::hash<int>{}(static_cast<int>(k.panel) * 10000 + k.box);
        }
    };
    std::unordered_map<BoxDisplayKey, std::vector<SlotDisplay>, BoxDisplayKeyHash> slotDisplayCache_;
    const std::vector<SlotDisplay>& getSlotDisplays(Panel panel, int box);
    void invalidateSlotDisplay(Panel panel, int box);
    void invalidateAllSlotDisplays() { slotDisplayCache_.clear(); }

    // Dirty flag: when true the next frame will be redrawn, then reset.
    // Call markDirty() from any code that changes visible state.
    bool dirty_ = true;
    void markDirty() { dirty_ = true; }

    // Main view state
    Cursor cursor_;
    int    gameBox_ = 0;
    int    bankBox_ = 0;
    bool   showDetail_ = false;
    bool   autoPrompted_ = false; // auto-update boot: prompt mostrato una sola volta
    // Gancio popup "Installa launcher": one-shot per boot come autoPrompted_
    // qui sopra, ma vive SOLO in RAM (mai su file) -- si valuta una volta a
    // boot quando l'autocheck update ha finito senza trovare nulla; se
    // questo boot trova un update invece, semplicemente salta il turno e
    // si riprova dal boot pulito successivo. Il "gia' mostrato per sempre"
    // vero sta su file (hasSeenLauncherPrompt()/markLauncherPromptSeen()).
    bool   launcherPromptChecked_ = false;
    bool   showMenu_   = false;
    int    menuSelection_ = 0;
    bool   saveNow_    = false;
    bool   showAbout_  = false;
    bool   showBoxView_   = false;
    Panel  boxViewPanel_  = Panel::Game;
    int    boxViewCursor_ = 0;
    bool   zlPressed_     = false;
    bool   zrPressed_     = false;
    bool   holding_    = false;
    Pokemon heldPkm_;
    bool   heldFromLGPEParty_ = false;       // block save→bank moves for LGPE party
    int    lgpeHeldPartyIdx_ = -1;          // which party pointer (0-5) held Pokemon belongs to
    bool   heldFromParty_ = false;           // generic party pick (Switch/DS) — hand stays on strip
    int    heldPartyIdx_ = -1;
    int    heldPartyOrig_ = -1;              // pick origin slot (never overwritten by swaps) for cancel/undo
    std::array<uint16_t, 6> lgpePartyBackup_{};  // backup for cancel/undo

    // Swap history for full undo on cancel
    struct SwapRecord {
        Pokemon pkm;
        Panel   panel;
        int     box;
        int     slot;
    };
    std::vector<SwapRecord> swapHistory_;

    // Multi-select state
    std::vector<int> selectedSlots_;           // selected slot indices in selection order
    Panel         selectedPanel_ = Panel::Game;
    int           selectedBox_   = 0;
    std::vector<Pokemon> heldMulti_;           // multi-held Pokemon
    std::vector<int>     heldMultiSlots_;      // original slot indices (for cancel)
    Panel         heldMultiSource_ = Panel::Game;
    int           heldMultiBox_    = 0;
    bool          positionPreserve_ = false; // place at original slot positions

    // Rectangle drag-select state
    bool     yHeld_         = false;
    bool     yDragActive_   = false;
    int      dragAnchorCol_  = 0;
    int      dragAnchorRow_  = 0;
    Panel    dragPanel_      = Panel::Game;
    int      dragBox_         = 0;
    uint32_t lastYTapTime_   = 0;  // for double-tap Y detection
    static constexpr uint32_t DOUBLE_TAP_MS = 300;

    // Sprites
    SDL_Texture* getSprite(uint16_t nationalId, uint8_t form = 0);
    SDL_Texture* getShinySprite(uint16_t nationalId, uint8_t form = 0);
    void freeSprites();

    // Profile selector
    void drawProfileSelectorFrame();
    void handleProfileSelectorInput(bool& running);
    void selectProfile(int index);

    // Game selector
    void drawGameSelectorFrame();
    void handleGameSelectorInput(bool& running);
    // Icona/box-art di availableGames_[i] dentro il rettangolo
    // (iconX, iconY, size, size): loghi, colori flat per GameType,
    // posizionamento custom RSE, badge sorgente import. Condivisa fra la
    // griglia Classica (size=IS) e l'anteprima Galleria (size=COVER).
    // Colore flat per-GameType (Bulbapedia color template) usato come
    // sfondo delle tile senza titleId in drawGameArt() e, quando il gioco
    // non ha una vera icona in cache (quindi niente gameAccentCache_), come
    // colore dell'accent "vetro" in Galleria (drawGameList_Gallery).
    SDL_Color flatBgColorFor(GameType g) const;
    void drawGameArt(int i, int iconX, int iconY, int size, bool scaleInner = false);
    // Vista Galleria (source/ui_gallery.cpp): lista + anteprima grande,
    // alternativa alla griglia Classica scelta in Impostazioni > Aspetto.
    void drawGameList_Gallery();
    void selectorTapGallery(float px, float py, bool& running);
    bool galleryScrollAnim();
    // Party preview Galleria: cache per gioco (specie/livello/shiny/uovo),
    // validata via mtime (mount+stat, niente decrypt). Load completo solo
    // se cambiato, mai eager.
    struct PartyPreviewMon { uint16_t species = 0; uint8_t level = 0; uint8_t form = 0; bool shiny = false; bool egg = false; bool empty = true; };
    struct PartyPreview {
        long mtime = -1; bool loading = false; std::vector<PartyPreviewMon> mons;
        std::string otName;     // valore mostrato (puo' essere l'override)
        std::string otNameReal; // valore vero dal save, mai sovrascritto
        bool dexSupported = false; int dexCaught = 0; int dexTotal = 0;
        long playTimeSeconds = -1; // -1 = non disponibile per questo formato (vedi SaveFile::playTimeSeconds)
    };
    std::unordered_map<GameType, PartyPreview> galPartyCache_;
    int galPreviewGame_ = -1;
    uint32_t galPreviewTick_ = 0;
    // Un solo probe (mount+stat) per atterraggio sulla selezione: il save
    // di un gioco puo' cambiare solo per mano di OpenHomeNX stessa (allora
    // invalida esplicitamente via galInvalidateParty), quindi ricontrollare
    // a ripetizione mentre resti fermo non serve -- serviva solo a
    // rimontare/smontare a ogni frame (causa di un flicker gia' fixato).
    // Resettato a false ad ogni nuovo "settle" (sel != galPreviewGame_) dai
    // due call site in ui_gallery.cpp; messo a true dentro galEnsureParty()
    // stessa dopo il primo probe per quell'atterraggio.
    bool galSettleChecked_ = false;
    // overrideOT.cfg (nome allenatore forzato per screenshot): letto da
    // file ad ogni chiamata di galEnsureParty() per restare "live" mentre
    // resti fermi su un gioco -- ma senza throttle sarebbe comunque un
    // fopen/fread reale a 60Hz, inutile per un file che nessuno riscrive
    // decine di volte al secondo. Diradato a un letture ogni 500ms: resta
    // percettivamente istantaneo per chi sta preparando uno screenshot,
    // ma taglia la spesa di ~30x.
    uint32_t galOverrideOtTick_ = 0;
    std::string galOverrideOtCached_;
    long galSaveMtime(GameType g);
    void galEnsureParty(GameType g);
    void galInvalidateParty(GameType g);
    // Persistenza su disco di galPartyCache_ (party/OT/dex): sopravvive al
    // riavvio, cosi' al rientro in Galleria si vede subito l'ultimo party
    // noto invece di "..." finche' non ti fermi di nuovo — galEnsureParty()
    // ricontrolla comunque l'mtime del save e aggiorna solo se cambiato,
    // stessa garanzia di correttezza di prima, solo senza dover rileggere
    // ogni save ad ogni avvio. File in basePath_ (vedi theme.cfg/gallery.cfg).
    bool galCacheLoadedFromDisk_ = false;
    void galLoadCacheFromDisk();
    // Precarica la cache disco a boot: senza, a ogni apertura la galleria
    // mostra "..." per 400ms sul gioco fermo (cache memoria vuota + load
    // pigro al primo settle). Con i preferiti il gioco in cima è sempre
    // visibile, quindi il pop-in si vedeva a ogni avvio.
    void galPreloadCacheFromDisk();
    void galSaveCacheToDisk() const;
    // true mentre lo slide dell'anteprima Galleria (galSelShown_/
    // galSlide_) non ha ancora raggiunto il target: stesso schema di
    // galleryScrollAnim(), interrogato da bottomButtonsAnim() cosi' il
    // loop principale richiama markDirty() 60fps anche senza input
    // (altrimenti l'animazione avanza di un solo passo per evento).
    bool galleryPreviewAnim();
    void selectGame(GameType game, int occurrence = 0);
    // "A" diretto sulla griglia (non dal radiale, gia' filtrato a monte in
    // openRadialMenu()): una ROM-only entry (importedIsRomOnly()) non ha
    // nulla da editare, la si avvia direttamente invece di fallire su
    // Mount Error dentro selectGame().
    void selectOrLaunchGame(GameType game, int occurrence, bool& running);
    std::string buildBackupDir(GameType game) const;
    bool saveBankFiles();
    // Write the game save (+ account commit + "Saving…" mask + LED) only when a
    // mutator actually changed it since load. No-op in dual-bank mode.
    // False SOLO su abort utente (party vuota + B): il chiamante deve
    // interrompere il suo flusso (restare nel gioco), niente scritto.
    bool persistGameSaveIfDirty();
    // Exit-guard party vuota (debug): nessun gioco accetta party 0. GBA offre
    // il Caterpie segnaposto (A = piazza ed esci, B = torno a sistemare a
    // mano); altrove blocca con messaggio. true = si puo uscire.
    bool ensurePartyOnExit();
    // Quit vero con mano occupata: il mon in mano vive solo in memoria.
    // true = si puo uscire (mano libera o utente consenziente).
    bool confirmQuitWithHold();

    // Bank selector
    void drawBankSelectorFrame();
    void handleBankSelectorInput(bool& running);
    void openSelectedBank();
    void drawDeleteConfirmPopup();
    void handleDeleteConfirmEvent(const SDL_Event& event);
    void beginTextInput(TextInputPurpose purpose);
    void commitTextInput(const std::string& text);

    // Rendering helpers
    void drawFrame();
    void drawDetailPopup(const Pokemon& pkm);
    void drawMenuPopup();
    // Numero di voci visibili nel menu popup (dipende da modalità dual-bank,
    // presenza Wondercard e selezione per l'export). Fonte unica di verità
    // condivisa da drawMenuPopup()/handleMenuInput()/handleStickRepeat().
    int  menuVisibleCount() const;
    void drawAboutPopup();
    void drawThemeSelectorPopup();
    void drawLanguageSelectorPopup();
    void drawGameSelMenuPopup();
    void drawSearchFilterPopup();
    void drawSearchResultsPopup();
    void drawSpeciesLetterPicker();
    void drawSpeciesListPicker();
    void drawWondercardListPopup();
    void drawPkImportListPopup();
    void drawTradeListPopup();
    void handleTradeListInput(const SDL_Event& event);
    void drawLearnsetPopup();
    void drawHeldOverlay();
    void drawBoxViewOverlay();
    void drawBoxPreview(int boxIdx, int anchorX, int anchorY);
    void drawRadarChart(int cx, int cy, int radius, const int values[6], int maxVal);
    void drawPanel(int panelX, const std::string& boxName, int boxIdx,
                   int totalBoxes, bool isActive, SaveFile* save, Bank* bank, int box,
                   Panel panelId);
    void drawSlot(int x, int y, const SlotDisplay& sd, bool isCursor, int selectOrder,
                  int highlightState = 0, bool isParty = false);
    void drawText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* f);
    void drawTextCentered(const std::string& text, int cx, int cy, SDL_Color color, TTF_Font* f);
    // Come drawText ma con alpha separata via modulate: riusa la texture
    // opaca in cache invece di crearne una per step (la tendina al primo
    // giro scattava per il baking di ~16 varianti alpha a frame).
    void drawTextFaded(const std::string& text, int x, int y, SDL_Color color, Uint8 alpha, TTF_Font* f);
    void drawRect(int x, int y, int w, int h, SDL_Color color);
    void drawRectOutline(int x, int y, int w, int h, SDL_Color color, int thickness);
    void drawSpriteFit(int x, int y, int w, int h, SDL_Texture* tex);
    void drawRoundRect(int x, int y, int w, int h, int r, SDL_Color color);
    // Come drawRoundRect() ma con un riempimento a gradiente orizzontale
    // (left -> right), stessa sagoma con angoli arrotondati esatti: una
    // sola passata opaca colonna per colonna, niente blend mode (quindi
    // niente rischio del doppio-alpha "pacman" agli angoli).
    void drawRoundRectGradientH(int x, int y, int w, int h, int r, SDL_Color left, SDL_Color right);
    void drawRoundRectOutline(int x, int y, int w, int h, int r, SDL_Color color, int thickness);
    // Come drawRoundRectOutline() ma tratteggiato: dashLen/gapLen in px,
    // pattern continuo lungo tutto il perimetro (angoli compresi), non
    // riavviato a ogni lato/arco.
    void drawRoundRectOutlineDashed(int x, int y, int w, int h, int r, SDL_Color color,
                                     int thickness, int dashLen, int gapLen);
    void drawRoundSelect(int cx, int cy, int r, bool focused);
    void drawStatusBar(const std::string& msg);

    // Input handling
    void handleInput(bool& running);
    void handleMenuInput(const SDL_Event& event, bool& running);
    void handleDetailInput(const SDL_Event& event);
    void handleNormalInput(const SDL_Event& event);
    void handleStickRepeat();
    void handleBumperRepeat();
    void moveCursor(int dx, int dy);
    void switchBox(int direction);
    void actionSelect();
    void actionCancel();
    // Save banks (+ save file, if loaded) and go back to the game selector.
    // Shared by the "Change Game" menu entry and a plain B press in the box view.
    void returnToGameSelector();
    // Look for a newer OpenHomeNX.nro (SD update/ folder, and USB drives when
    // built with OH_USB_UPDATE), copy it over the running NRO and queue a
    // relaunch. Returns true if the app should quit now (relaunch queued).
    // usbAlreadyMounted: true when the caller already knows a drive just
    // finished mounting (the hotplug rising-edge in run()) — skips the USB
    // retry/settle wait entirely, since it would just re-confirm what the
    // caller already observed. false (menu-triggered "Check for update")
    // keeps the retry: the user could have opened the menu before the
    // hotplug poll even noticed the drive.
    // usbOnly=true: scope ristretto all'USB per l'auto-check su hotplug —
    // se l'USB non ha niente di più recente torna silenzioso senza toccare
    // SD/rete. Il menu manuale usa la catena completa (default).
    bool checkForUpdate(bool usbOnly = false);
    // Consolidate a pending OpenHomeNX.nro.new from a self-update. Returns true
    // when it just wrote the new bytes onto the canonical .nro while running
    // from the throw-away .new — the caller should then bounce straight into
    // the real .nro instead of showing the app from the .new instance.
    bool finalizePendingUpdate();
    void toggleSelect();
    void clearSelection();
    void beginYPress();
    void endYPress();
    void updateDragSelection();
    void selectAll();
    void handleSearchFilterInput(const SDL_Event& event);
    void handleSearchResultsInput(const SDL_Event& event);
    void handleSpeciesLetterPickerInput(const SDL_Event& event);
    void handleSpeciesListPickerInput(const SDL_Event& event);
    void buildAvailableSpeciesList();
    void buildSpeciesListForLetter(int letterIndex);
    bool letterHasSpecies(int letterIndex) const;
    void handleWondercardListInput(const SDL_Event& event);
    void injectWondercard(const WCInfo& info);
    void handlePkImportListInput(const SDL_Event& event);
    bool importPkFile(const PkFileInfo& info, bool closeAfter = true);
    bool importPkFileToBank(const PkFileInfo& info, std::string& err);
    void importSelectedPkFiles(bool closeAfter);
    void openLearnset(const Pokemon& pkm);
    void handleLearnsetInput(const SDL_Event& event);
    std::string exportPokemon(const Pokemon& pkm);
    // Cross-gen bank blob -> .pk1/.pk2 file (format chosen via dialog).
    // Returns the filename, or "" on cancel/failure (nothing written).
    std::string exportCrossGenBlob(const Pokemon& pkm);
    // PK import picker (.pk1/.pk2, mirrors the wondercard list).
    std::vector<PkFileInfo> scanPkImportFiles();
    void executeSearch();
    bool matchesSearchFilter(const Pokemon& pkm, const std::string& filterSpecies,
                             const std::string& filterOT) const;
    bool isSearchMatch(Panel panel, int box, int slot) const;
    void clearSearchHighlight();
    void refreshHighlightSet();
    void handleBoxViewInput(const SDL_Event& event);
    void moveBoxViewCursor(int dx, int dy);
    void openBoxView(Panel panel);
    void closeBoxView(bool navigate);

    // Dynamic grid. Each panel sizes its box grid from its OWN source, so a
    // 30-slot bank (including the universal cross-gen bank) can sit next to a
    // 25-slot Let's Go save or a 20-slot GB/GBC box (5x4, see gridRowsFor).
    int slotsPerBoxFor(Panel p) const {
        if (p == Panel::Bank)        return bank_.slotsPerBox();
        if (isDualBankMode())        return bankLeft_.slotsPerBox();
        return save_.slotsPerBox() > 0 ? save_.slotsPerBox() : 30;
    }
    int gridColsFor(Panel p) const {
        int spb = slotsPerBoxFor(p);
        return spb <= 25 ? 5 : 6;
    }
    // Righe per pannello: box GB/GBC da 20 slot in 5x4 (non 4x5, piu
    // leggibile), gli altri restano 5 righe. Mai celle fantasma oltre spb.
    int gridRowsFor(Panel p) const {
        int spb = slotsPerBoxFor(p);
        int cols = gridColsFor(p);
        if (spb > 0 && cols > 0 && spb % cols == 0) return spb / cols;
        return 5;
    }
    int maxSlotsFor(Panel p) const { return gridColsFor(p) * gridRowsFor(p); }
    // Legacy call sites: they always operated on the cursor's panel.
    int gridCols() const { return gridColsFor(cursor_.panel); }
    int maxSlots() const { return maxSlotsFor(cursor_.panel); }

    // Get pokemon at cursor from the appropriate source
    Pokemon getPokemonAt(int box, int slot, Panel panel) const;
    void setPokemonAt(int box, int slot, Panel panel, const Pokemon& pkm);
    void clearPokemonAt(int box, int slot, Panel panel);
    // True se lo strip party accetta pick/posa: solo con debug attivo, mai su
    // Gen5 (save read-only: l'edit andrebbe perso al save). Toggle dedicato
    // in futuro.
    bool canEditParty() const;

    // M6 transfer-on-drop.
    // Game type whose stored PKM layout a placement into `panel` will use.
    GameType destGameFor(Panel panel) const;
    // True when `panel` targets a cross-gen bank (slots hold OHPKM, no
    // per-format conversion on the way in).
    bool destIsCrossGenBank(Panel panel) const;
    // Prepare `pkm` for placement into `panel`: no-op when the destination
    // stores the same layout, otherwise convert through the OH engine.
    // Returns false WITHOUT touching `pkm` when the conversion is impossible
    // or refused; `whyNot` is filled with a user-facing reason. The caller must
    // then abort the placement — never write bytes that do not match the
    // destination layout.
    bool prepareForPlacement(Pokemon& pkm, Panel panel, std::string& whyNot) const;
    // Second choice (ours, NOT upstream): verbatim restore from the carried
    // OriginalBackup, used only when the Rust reconstruction fails. Returns
    // false (leaving pkm untouched) when there is no matching backup or the
    // mon was edited since conversion.
    bool restoreVerbatimFallback(Pokemon& pkm, GameType dest, int dstGen) const;
};

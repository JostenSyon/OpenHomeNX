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
    ImportPathEntry
};

// Rows of the "+" game-selector menu. A single list drives both the popup's
// draw order and its input handling — the old parallel hardcoded row-count
// arithmetic (see v0.1.37's alignment bug) drifts every time a row is added.
enum class GameSelMenuAction { SwitchCore, DebugLog, SendLog, SendSave, ImportSettings, CheckUpdate, Exit };

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

// Cursor position within the two-panel display
struct Cursor {
    Panel panel = Panel::Game;
    int box     = 0;
    int col     = 0; // 0-5 (or 0-4 for LGPE)
    int row     = 0; // 0-4

    int slot(int cols = 6) const { return row * cols + col; }
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
    static std::vector<std::string> wrapText(const std::string& line, TTF_Font* f, int maxW);
    void showMessageAndWait(const std::string& title, const std::string& body);
    bool showConfirmDialog(const std::string& title, const std::string& body);
    void showWorking(const std::string& msg);
    void setAppletMode(bool mode) { appletMode_ = mode; }
    bool isDualBankMode() const { return appletMode_ || allBanksMode_; }
    void run(const std::string& basePath, const std::string& savePath);

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

    // Game-selector logos for imported (titleId-less) games — see init()'s
    // loadLogo(). Keyed by GameType since there are only a handful of these.
    std::unordered_map<GameType, SDL_Texture*> gameLogoCache_;
    // HD box art for RSE tiles (romfs:/boxart/, from user-provided PNGs).
    // Same lifetime policy as gameLogoCache_ (app lifetime, never freed).
    std::unordered_map<GameType, SDL_Texture*> boxArtCache_;
    // Per-game tile backgrounds (romfs:/backgrounds/). Drawn over the flat
    // color rect (which stays as fallback), e.g. Emerald artwork.
    std::unordered_map<GameType, SDL_Texture*> tileBgCache_;

    // Screen dimensions (Switch: 1280x720)
    static constexpr int SCREEN_W = 1280;
    static constexpr int SCREEN_H = 720;

    // Layout
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
    void appendImportedGames();               // scans importPaths_, extends availableGames_
    void rescanImportedGames();      // re-scan in place + popup on newly found games
    std::string importedSavePath(GameType game, int occurrence = 0) const;
    std::string importedSourceTag(GameType game, int occurrence = 0) const; // small on-tile badge text
    // Which occurrence of `game` is the tile at availableGames_[cursor]?
    // Counts same-type tiles before it (duplicates = same game, other device).
    int importedOccurrence(int cursor) const;

    // Cross-gen transfer selector state (M6a)
    bool showGenSelector_ = false;
    int  genSelCursor_    = 0;
    int  targetGen_       = 9;
    static constexpr int GEN_LIST[7] = {3,4,5,6,7,8,9};

    // Game selector menu state (+ button: Switch Core / Debug log / Exit)
    bool showGameSelMenu_ = false;
    int  gameSelMenuCursor_ = 0;

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
    std::string manualBackupDir(GameType g) const;
    std::string autoBackupDir(GameType g) const;
    bool backupGameSave(GameType g, std::string& out);
    struct BackupListEntry { std::string path; std::string label; };
    std::vector<BackupListEntry> collectBackupEntries(GameType g);
    bool restoreBackupEntry(GameType g, const std::string& entry);
    // Voce lista backup: solo unita ripristinabili (file per i save
    // file-backed, dir con file dentro per i titoli installati) + etichetta
    // "[AUTO]/[MAN] data-ora" cosi i file sciolti tipo "main" non compaiono.
    // Lista backup sfogliabile (manual + auto + auto-apertura), newest first.
    bool showBackupList_ = false;
    int  backupListCursor_ = 0;
    int  backupListScroll_ = 0;
    std::vector<BackupListEntry> backupListEntries_;
    GameType backupListGame_ = GameType::EMERALD;
    void openBackupList(GameType g);
    void drawBackupListPopup();
    // Auto-backup best-effort all'apertura dei save file-backed (cap 10).
    void autoBackupFileSave(GameType g, const std::string& path);
    // Tetto spazio auto-backup per gioco da update.cfg (backup_mb[_sd],
    // default 256/32; 0 = illimitato). Mai i manuali.
    long backupCapMb(bool fileBacked) const;
    std::vector<std::string> autoBackupEntries(GameType g) const;
    bool autoBackupNeeded(GameType g, bool fileBacked, const std::string& src, uint64_t srcSize);
    uint64_t pruneBackupsToCap(GameType g, bool fileBacked);

    // Wondercard list state
    bool showWondercardList_ = false;
    int  wcListCursor_  = 0;
    int  wcListScroll_  = 0;
    std::vector<WCInfo> wcList_;

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

    // Account manager
    AccountManager account_;

    // Profile selector state
    int profileSelCursor_ = 0;
    int selectedProfile_ = -1;

    // Game selector state
    GameType selectedGame_ = GameType::ZA;
    int gameSelCursor_ = 0;
    int gameSelPage_ = 0;
    bool gameSelOnAllBanks_ = false;  // cursor is on "View All Banks" option
    int gameSelOnChevron_ = 0;        // 0=none, -1=left chevron, 1=right chevron
    bool allBanksMode_ = false;       // entered bank selector via "View All Banks"
    bool bankRightCrossGen_ = false;  // right-panel bank selector showing ALL games (cross-gen), normal mode
    std::vector<GameType> availableGames_;
    std::unordered_map<GameType, SDL_Texture*> gameIconCache_;
    std::unordered_map<GameType, int> gameBankCounts_;
    void refreshBankCounts();
    void loadGameIcons();
    void freeGameIcons();
    void enterAllBanksMode();

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
    void selectGame(GameType game, int occurrence = 0);
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
    void drawGenSelectorPopup();
    void drawGameSelMenuPopup();
    void drawSearchFilterPopup();
    void drawSearchResultsPopup();
    void drawSpeciesLetterPicker();
    void drawSpeciesListPicker();
    void drawWondercardListPopup();
    void drawPkImportListPopup();
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
    void drawRect(int x, int y, int w, int h, SDL_Color color);
    void drawRectOutline(int x, int y, int w, int h, SDL_Color color, int thickness);
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

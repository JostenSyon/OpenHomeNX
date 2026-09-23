#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <cstdint>

struct Theme {
    const char* name;

    // Core UI
    SDL_Color bg;
    SDL_Color panelBg;
    SDL_Color statusBarBg;

    // Slots
    SDL_Color slotEmpty;
    SDL_Color slotFull;
    SDL_Color slotEgg;

    // Cursor & Selection
    SDL_Color cursor;
    SDL_Color selected;
    SDL_Color selectedPos;

    // Text
    SDL_Color text;
    SDL_Color textDim;
    SDL_Color textOnBadge;

    // Box & Navigation
    SDL_Color boxName;
    SDL_Color arrow;

    // Status & Accent
    SDL_Color statusText;
    SDL_Color red;
    SDL_Color shiny;
    SDL_Color goldLabel;

    // Gender
    SDL_Color genderMale;
    SDL_Color genderFemale;

    // Overlays
    SDL_Color overlay;
    SDL_Color overlayDark;

    // Menu / Selectors
    SDL_Color menuHighlight;
    SDL_Color iconPlaceholder;

    // Popups
    SDL_Color popupBorder;
    SDL_Color creditsText;

    // Box View
    SDL_Color boxPreviewBg;
    SDL_Color miniCellEmpty;
    SDL_Color miniCellFull;

    // Text Input
    SDL_Color textFieldBg;

    // Search Highlight
    SDL_Color searchMatch;
    SDL_Color searchDim;

    // LGPE party marker
    SDL_Color partyMark;
};

// Contrasto automatico dal colore base: i temi chiari hanno testo scuro
// (ombra chiara), quelli scuri testo chiaro (ombra scura). Evita un campo
// per-tema solo per l'ombra — la scelta segue il testo, mai hardcodata.
inline int luminanceOf(SDL_Color c) { return (299 * c.r + 587 * c.g + 114 * c.b) / 1000; }
inline SDL_Color shadowForText(SDL_Color t, Uint8 alpha) {
    return luminanceOf(t) < 128 ? SDL_Color{255, 255, 255, alpha} : SDL_Color{0, 0, 0, alpha};
}
inline SDL_Color contrastTextForBg(SDL_Color bg) {
    return luminanceOf(bg) < 128 ? SDL_Color{240, 240, 240, 255} : SDL_Color{30, 30, 35, 255};
}

inline constexpr int THEME_COUNT = 9;
// Default for fresh installs (existing theme.cfg choices are preserved).
inline constexpr int DEFAULT_THEME_INDEX = 0; // OH

const Theme& getTheme(int index);
const char*  getThemeName(int index);

int  loadThemeIndex(const std::string& basePath);
void saveThemeIndex(const std::string& basePath, int index);
int  loadZoomGrow(const std::string& basePath);
void saveZoomGrow(const std::string& basePath, int grow);

// Layout selettore giochi (0 = Classico, 1 = Galleria). Stesso schema a
// singolo byte di theme.cfg/zoom.cfg.
int  loadGameSelectorLayout(const std::string& basePath);
void saveGameSelectorLayout(const std::string& basePath, int layout);

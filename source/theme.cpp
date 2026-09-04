#include "theme.h"
#include <cstdio>

static const Theme themes[THEME_COUNT] = {
    // ===== 0: Default (current dark theme) =====
    {
        .name            = "Default",
        .bg              = {30, 30, 40, 255},
        .panelBg         = {45, 45, 60, 255},
        .statusBarBg     = {20, 20, 30, 255},
        .slotEmpty       = {60, 60, 80, 255},
        .slotFull        = {70, 90, 120, 255},
        .slotEgg         = {90, 80, 70, 255},
        .cursor          = {255, 220, 50, 255},
        .selected        = {100, 200, 220, 255},
        .selectedPos     = {120, 200, 120, 255},
        .text            = {240, 240, 240, 255},
        .textDim         = {160, 160, 170, 255},
        .textOnBadge     = {0, 0, 0, 255},
        .boxName         = {200, 200, 220, 255},
        .arrow           = {180, 180, 200, 255},
        .statusText      = {140, 200, 140, 255},
        .red             = {220, 60, 60, 255},
        .shiny           = {255, 215, 0, 255},
        .goldLabel       = {255, 215, 0, 255},
        .genderMale      = {100, 150, 255, 255},
        .genderFemale    = {255, 130, 150, 255},
        .overlay         = {0, 0, 0, 160},
        .overlayDark     = {0, 0, 0, 187},
        .menuHighlight   = {60, 60, 80, 255},
        .iconPlaceholder = {80, 80, 120, 255},
        .popupBorder     = {0x30, 0x30, 0x55, 255},
        .creditsText     = {0x88, 0x88, 0x88, 255},
        .boxPreviewBg    = {40, 40, 55, 230},
        .miniCellEmpty   = {55, 55, 70, 180},
        .miniCellFull    = {55, 70, 90, 200},
        .textFieldBg     = {30, 30, 40, 255},
        .searchMatch     = {255, 140, 50, 255},
        .searchDim       = {0, 0, 0, 120},
        .partyMark       = {100, 200, 255, 255},
    },

    // ===== 1: HOME (Pokemon HOME-inspired light pastel theme) =====
    {
        .name            = "HOME",
        .bg              = {235, 240, 245, 255},
        .panelBg         = {248, 248, 252, 255},
        .statusBarBg     = {225, 230, 238, 255},
        .slotEmpty       = {230, 235, 242, 255},
        .slotFull        = {190, 225, 220, 255},
        .slotEgg         = {245, 225, 210, 255},
        .cursor          = {240, 128, 118, 255},
        .selected        = {90, 195, 190, 255},
        .selectedPos     = {130, 200, 140, 255},
        .text            = {50, 55, 65, 255},
        .textDim         = {120, 130, 145, 255},
        .textOnBadge     = {255, 255, 255, 255},
        .boxName         = {70, 80, 95, 255},
        .arrow           = {140, 150, 170, 255},
        .statusText      = {60, 160, 130, 255},
        .red             = {230, 80, 80, 255},
        .shiny           = {220, 170, 40, 255},
        .goldLabel       = {210, 150, 50, 255},
        .genderMale      = {70, 130, 230, 255},
        .genderFemale    = {230, 110, 140, 255},
        .overlay         = {0, 0, 0, 120},
        .overlayDark     = {0, 0, 0, 150},
        .menuHighlight   = {220, 235, 240, 255},
        .iconPlaceholder = {200, 210, 225, 255},
        .popupBorder     = {180, 190, 210, 255},
        .creditsText     = {140, 150, 165, 255},
        .boxPreviewBg    = {240, 243, 248, 240},
        .miniCellEmpty   = {225, 230, 240, 200},
        .miniCellFull    = {190, 220, 218, 220},
        .textFieldBg     = {245, 245, 250, 255},
        .searchMatch     = {230, 100, 60, 255},
        .searchDim       = {0, 0, 0, 80},
        .partyMark       = {50, 140, 220, 255},
    },

    // ===== 2: HOME - Violet (Pokemon HOME Violet Pokedex theme) =====
    {
        .name            = "HOME - Violet",
        .bg              = {235, 230, 245, 255},     // Light lavender
        .panelBg         = {248, 245, 252, 255},     // Near-white with purple tint
        .statusBarBg     = {110, 80, 160, 255},      // Dark purple bar
        .slotEmpty       = {228, 222, 240, 255},     // Pale lavender
        .slotFull        = {200, 190, 225, 255},     // Soft purple
        .slotEgg         = {242, 228, 215, 255},     // Warm cream
        .cursor          = {240, 165, 60, 255},      // Orange/amber highlight
        .selected        = {140, 120, 200, 255},     // Medium purple
        .selectedPos     = {130, 190, 145, 255},     // Soft green
        .text            = {50, 45, 65, 255},        // Dark purple-charcoal
        .textDim         = {120, 110, 140, 255},     // Muted purple-gray
        .textOnBadge     = {255, 255, 255, 255},     // White on badges
        .boxName         = {80, 65, 110, 255},       // Deep purple
        .arrow           = {150, 135, 175, 255},     // Medium lavender
        .statusText      = {230, 230, 240, 255},     // Light text on dark bar
        .red             = {220, 70, 75, 255},       // Bright red
        .shiny           = {185, 130, 15, 255},      // Deep amber
        .goldLabel       = {200, 145, 50, 255},      // Amber label
        .genderMale      = {80, 120, 220, 255},      // Blue
        .genderFemale    = {220, 100, 135, 255},     // Pink
        .overlay         = {30, 20, 50, 130},        // Purple-tinted overlay
        .overlayDark     = {30, 20, 50, 165},        // Darker purple overlay
        .menuHighlight   = {225, 218, 242, 255},     // Light purple highlight
        .iconPlaceholder = {195, 185, 215, 255},     // Muted lavender
        .popupBorder     = {160, 140, 195, 255},     // Purple border
        .creditsText     = {140, 130, 160, 255},     // Dim purple
        .boxPreviewBg    = {240, 236, 250, 240},     // Light lavender
        .miniCellEmpty   = {222, 215, 238, 200},     // Pale purple
        .miniCellFull    = {195, 185, 218, 220},     // Soft purple
        .textFieldBg     = {242, 238, 250, 255},     // Very light lavender
        .searchMatch     = {240, 140, 40, 255},
        .searchDim       = {30, 20, 50, 90},
        .partyMark       = {60, 180, 240, 255},
    },

    // ===== 3: HOME - Blue (Pokemon HOME GTS-style blue theme) =====
    {
        .name            = "HOME - Blue",
        .bg              = {190, 220, 240, 255},     // Light sky blue
        .panelBg         = {95, 150, 200, 255},      // Medium steel blue (card bg)
        .statusBarBg     = {70, 125, 180, 255},      // Darker blue bar
        .slotEmpty       = {130, 175, 215, 255},     // Muted blue
        .slotFull        = {105, 155, 200, 255},     // Slightly deeper blue
        .slotEgg         = {175, 195, 215, 255},     // Blue-gray
        .cursor          = {255, 200, 50, 255},       // Bright gold-yellow highlight
        .selected        = {80, 200, 230, 255},      // Light cyan
        .selectedPos     = {120, 210, 150, 255},     // Soft green
        .text            = {255, 255, 255, 255},     // White text
        .textDim         = {200, 215, 230, 255},     // Light blue-white
        .textOnBadge     = {40, 80, 130, 255},       // Dark blue on badges
        .boxName         = {240, 245, 255, 255},     // Near-white
        .arrow           = {180, 210, 240, 255},     // Pale blue
        .statusText      = {220, 240, 255, 255},     // Bright white-blue
        .red             = {240, 90, 90, 255},       // Red on blue bg
        .shiny           = {255, 220, 60, 255},      // Bright gold
        .goldLabel       = {255, 220, 80, 255},      // Warm gold
        .genderMale      = {130, 190, 255, 255},     // Light blue
        .genderFemale    = {255, 150, 175, 255},     // Pink
        .overlay         = {20, 50, 90, 140},        // Blue-tinted overlay
        .overlayDark     = {20, 50, 90, 175},        // Darker blue overlay
        .menuHighlight   = {80, 140, 195, 255},      // Highlighted blue
        .iconPlaceholder = {110, 160, 205, 255},     // Mid blue
        .popupBorder     = {60, 130, 190, 255},      // Blue border
        .creditsText     = {170, 195, 220, 255},     // Muted light blue
        .boxPreviewBg    = {85, 145, 195, 240},      // Blue panel
        .miniCellEmpty   = {120, 170, 210, 200},     // Soft blue
        .miniCellFull    = {95, 150, 200, 220},      // Steel blue
        .textFieldBg     = {130, 175, 215, 255},     // Muted blue field
        .searchMatch     = {255, 160, 50, 255},
        .searchDim       = {20, 50, 90, 90},
        .partyMark       = {255, 200, 80, 255},
    },

    // ===== 4: HOME - Green (Pokemon HOME green variant) =====
    {
        .name            = "HOME - Green",
        .bg              = {200, 235, 210, 255},     // Light mint green
        .panelBg         = {90, 165, 120, 255},      // Medium forest green (card bg)
        .statusBarBg     = {65, 135, 95, 255},       // Darker green bar
        .slotEmpty       = {130, 190, 150, 255},     // Muted green
        .slotFull        = {100, 170, 130, 255},     // Slightly deeper green
        .slotEgg         = {180, 200, 170, 255},     // Green-beige
        .cursor          = {255, 200, 50, 255},      // Bright gold-yellow highlight
        .selected        = {80, 200, 160, 255},      // Teal-green
        .selectedPos     = {100, 210, 200, 255},     // Cyan-green
        .text            = {255, 255, 255, 255},     // White text
        .textDim         = {200, 225, 210, 255},     // Light green-white
        .textOnBadge     = {35, 85, 55, 255},        // Dark green on badges
        .boxName         = {240, 250, 242, 255},     // Near-white green tint
        .arrow           = {180, 220, 195, 255},     // Pale green
        .statusText      = {220, 245, 230, 255},     // Bright white-green
        .red             = {235, 85, 85, 255},       // Red on green bg
        .shiny           = {255, 220, 60, 255},      // Bright gold
        .goldLabel       = {255, 220, 80, 255},      // Warm gold
        .genderMale      = {100, 170, 240, 255},     // Blue
        .genderFemale    = {240, 140, 165, 255},     // Pink
        .overlay         = {15, 50, 30, 140},        // Green-tinted overlay
        .overlayDark     = {15, 50, 30, 175},        // Darker green overlay
        .menuHighlight   = {75, 150, 110, 255},      // Highlighted green
        .iconPlaceholder = {110, 175, 140, 255},     // Mid green
        .popupBorder     = {60, 145, 100, 255},      // Green border
        .creditsText     = {165, 200, 180, 255},     // Muted light green
        .boxPreviewBg    = {80, 155, 115, 240},      // Green panel
        .miniCellEmpty   = {120, 185, 150, 200},     // Soft green
        .miniCellFull    = {95, 165, 125, 220},      // Forest green
        .textFieldBg     = {130, 190, 155, 255},     // Muted green field
        .searchMatch     = {255, 160, 50, 255},
        .searchDim       = {15, 50, 30, 90},
        .partyMark       = {80, 160, 255, 255},
    },

    // ===== 5: HOME - Red (Pokemon HOME red variant) =====
    {
        .name            = "HOME - Red",
        .bg              = {240, 210, 210, 255},     // Light rose
        .panelBg         = {195, 95, 95, 255},       // Medium crimson (card bg)
        .statusBarBg     = {170, 70, 70, 255},       // Darker red bar
        .slotEmpty       = {210, 135, 135, 255},     // Muted red
        .slotFull        = {190, 110, 110, 255},     // Slightly deeper red
        .slotEgg         = {210, 185, 175, 255},     // Red-beige
        .cursor          = {255, 220, 50, 255},      // Bright gold-yellow highlight
        .selected        = {230, 140, 80, 255},      // Warm orange
        .selectedPos     = {120, 200, 140, 255},     // Soft green
        .text            = {255, 255, 255, 255},     // White text
        .textDim         = {230, 205, 205, 255},     // Light red-white
        .textOnBadge     = {100, 30, 30, 255},       // Dark red on badges
        .boxName         = {250, 240, 240, 255},     // Near-white red tint
        .arrow           = {225, 190, 190, 255},     // Pale rose
        .statusText      = {250, 230, 230, 255},     // Bright white-rose
        .red             = {255, 220, 50, 255},      // Gold (since bg is already red)
        .shiny           = {255, 220, 60, 255},      // Bright gold
        .goldLabel       = {255, 220, 80, 255},      // Warm gold
        .genderMale      = {120, 170, 245, 255},     // Blue
        .genderFemale    = {255, 180, 200, 255},     // Light pink
        .overlay         = {60, 15, 15, 140},        // Red-tinted overlay
        .overlayDark     = {60, 15, 15, 175},        // Darker red overlay
        .menuHighlight   = {180, 85, 85, 255},       // Highlighted red
        .iconPlaceholder = {200, 120, 120, 255},     // Mid red
        .popupBorder     = {165, 65, 65, 255},       // Red border
        .creditsText     = {210, 175, 175, 255},     // Muted light red
        .boxPreviewBg    = {185, 90, 90, 240},       // Red panel
        .miniCellEmpty   = {200, 130, 130, 200},     // Soft red
        .miniCellFull    = {185, 105, 105, 220},     // Crimson
        .textFieldBg     = {210, 140, 140, 255},     // Muted red field
        .searchMatch     = {255, 200, 50, 255},
        .searchDim       = {60, 15, 15, 90},
        .partyMark       = {80, 180, 255, 255},
    },

    // ===== 6: Pikachu (bright yellow & brown, Pikachu colors) =====
    {
        .name            = "Pikachu",
        .bg              = {255, 235, 140, 255},     // Bright Pikachu yellow
        .panelBg         = {245, 220, 100, 255},     // Warm golden yellow
        .statusBarBg     = {130, 85, 45, 255},       // Dark brown (ears/tail tip)
        .slotEmpty       = {250, 225, 130, 255},     // Pale yellow
        .slotFull        = {240, 205, 90, 255},      // Deeper golden
        .slotEgg         = {245, 225, 175, 255},     // Cream yellow
        .cursor          = {210, 60, 50, 255},       // Red (cheeks!)
        .selected        = {200, 80, 60, 255},       // Warm red
        .selectedPos     = {100, 180, 110, 255},     // Green
        .text            = {65, 45, 25, 255},        // Dark brown text
        .textDim         = {140, 110, 65, 255},      // Medium brown
        .textOnBadge     = {255, 245, 200, 255},     // Light yellow on badges
        .boxName         = {90, 60, 30, 255},        // Deep brown
        .arrow           = {170, 135, 70, 255},      // Tan
        .statusText      = {255, 240, 190, 255},     // Cream on dark bar
        .red             = {210, 55, 45, 255},       // Red
        .shiny           = {180, 60, 20, 255},        // Deep red-brown
        .goldLabel       = {180, 120, 30, 255},      // Deep amber
        .genderMale      = {70, 120, 200, 255},      // Blue
        .genderFemale    = {220, 90, 120, 255},      // Pink
        .overlay         = {60, 40, 10, 140},        // Brown-tinted overlay
        .overlayDark     = {60, 40, 10, 175},        // Darker brown overlay
        .menuHighlight   = {235, 200, 80, 255},      // Highlighted gold
        .iconPlaceholder = {220, 185, 90, 255},      // Warm yellow
        .popupBorder     = {160, 110, 45, 255},      // Brown border
        .creditsText     = {160, 130, 75, 255},      // Muted brown
        .boxPreviewBg    = {240, 215, 100, 240},     // Yellow panel
        .miniCellEmpty   = {245, 220, 120, 200},     // Soft yellow
        .miniCellFull    = {235, 200, 85, 220},      // Golden
        .textFieldBg     = {250, 230, 150, 255},     // Light yellow field
        .searchMatch     = {50, 130, 220, 255},
        .searchDim       = {60, 40, 10, 100},
        .partyMark       = {50, 130, 220, 255},
    },

    // ===== 7: OH dark (OpenHome desktop dark palette, App.css) =====
    {
        .name            = "OH dark",
        .bg              = {0x08, 0x17, 0x21, 255},     // navy-900
        .panelBg         = {0x08, 0x17, 0x21, 255},     // navy-900 (cards)
        .statusBarBg     = {0x05, 0x0E, 0x15, 255},     // navy-900 darker
        .slotEmpty       = {0x14, 0x2E, 0x3A, 255},     // dark slate-teal
        .slotFull        = {0x4A, 0x6C, 0x70, 255},     // teal-700
        .slotEgg         = {0x6E, 0x62, 0x54, 255},
        .cursor          = {0x7D, 0xCE, 0xAB, 255},     // teal-400 (brand)
        .selected        = {0x7D, 0x9A, 0x9C, 255},     // teal-500
        .selectedPos     = {0x53, 0xB4, 0xA5, 255},     // teal-450
        .text            = {0xF0, 0xF0, 0xF0, 255},     // gray-50 dark
        .textDim         = {0xA8, 0xA8, 0xA8, 255},     // gray-700 dark
        .textOnBadge     = {0, 0, 0, 255},
        .boxName         = {0xD8, 0xD8, 0xD8, 255},
        .arrow           = {0xA0, 0xDA, 0xC2, 255},     // teal-300
        .statusText      = {0x53, 0xB4, 0xA5, 255},     // teal-450
        .red             = {0xF0, 0x7A, 0x6E, 255},     // red-dark approx, verify HW
        .shiny           = {0xFC, 0xCB, 0x59, 255},     // hyper-train-color
        .goldLabel       = {0xFC, 0xCB, 0x59, 255},     // hyper-train-color
        .genderMale      = {0x8E, 0xA8, 0xF0, 255},     // blue-dark approx, verify HW
        .genderFemale    = {0xF2, 0x8E, 0xA6, 255},
        .overlay         = {0, 0, 0, 160},
        .overlayDark     = {0, 0, 0, 187},
        .menuHighlight   = {0x7D, 0x9A, 0x9C, 255},     // teal-500
        .iconPlaceholder = {0x36, 0x45, 0x4E, 255},     // navy-800
        .popupBorder     = {0x01, 0x53, 0x54, 255},     // teal-800
        .creditsText     = {0x88, 0x88, 0x88, 255},
        .boxPreviewBg    = {0x10, 0x28, 0x34, 230},
        .miniCellEmpty   = {0x14, 0x2A, 0x36, 180},
        .miniCellFull    = {0x4A, 0x6C, 0x70, 200},     // teal-700
        .textFieldBg     = {0x06, 0x12, 0x1B, 255},
        .searchMatch     = {255, 140, 50, 255},
        .searchDim       = {0, 0, 0, 120},
        .partyMark       = {0x53, 0xB4, 0xA5, 255},     // teal-450
    },

    // ===== 8: OH (chiaro, da descrizione utente mappata sui token App.css) =====
    // bg = surface light navy-950 (blu-verde); slot = gray-900 (griglia nera);
    // panelBg = gray-800 (box un po' più chiaro); bordi = gray-400.
    // Testo chiaro perché le celle sono scure (in light OH il testo è nero
    // su pannelli mint, ma qui la griglia è scura per indicazione utente).
    {
        .name            = "OH",
        .bg              = {0x01, 0x52, 0x54, 255},  // sfondo globale app
        .panelBg         = {0x44, 0x7F, 0x7F, 255},  // sfondo card giochi / box
        .statusBarBg     = {0x01, 0x3E, 0x40, 255},  // teal scuro (barra stato)
        .slotEmpty       = {108, 141, 141, 255},     // come sfondo celle piene (slotFull)
        .slotFull        = {108, 141, 141, 255},     // teal-600
        .slotEgg         = {122, 95, 63, 255},
        .cursor          = {83, 180, 165, 255},      // teal-450
        .selected        = {0x01, 0x52, 0x54, 255},  // selezionato per spostare = sfondo globale
        .selectedPos     = {0x01, 0x52, 0x54, 255},  // idem (preserve posizioni)
        .text            = {240, 240, 240, 255},     // chiaro (celle scure)
        .textDim         = {168, 168, 168, 255},
        .textOnBadge     = {0, 0, 0, 255},
        .boxName         = {216, 216, 216, 255},
        .arrow           = {160, 218, 194, 255},     // teal-300
        .statusText      = {83, 180, 165, 255},      // teal-450
        .red             = {232, 83, 74, 255},       // red-light approx, verify HW
        .shiny           = {219, 164, 63, 255},      // hyper-train light
        .goldLabel       = {219, 164, 63, 255},
        .genderMale      = {142, 168, 240, 255},
        .genderFemale    = {242, 142, 166, 255},
        .overlay         = {0, 0, 0, 160},
        .overlayDark     = {0, 0, 0, 187},
        .menuHighlight   = {0x57, 0x8D, 0x8D, 255},  // gioco/card evidenziato
        .iconPlaceholder = {96, 96, 96, 255},        // gray-600
        .popupBorder     = {144, 144, 144, 255},     // gray-400, riquadri
        .creditsText     = {136, 136, 136, 255},
        .boxPreviewBg    = {48, 48, 48, 230},
        .miniCellEmpty   = {108, 141, 141, 180},     // come slotEmpty
        .miniCellFull    = {108, 141, 141, 200},
        .textFieldBg     = {48, 48, 48, 255},
        .searchMatch     = {255, 140, 50, 255},
        .searchDim       = {0, 0, 0, 120},
        .partyMark       = {83, 180, 165, 255},      // teal-450
    },
};

const Theme& getTheme(int index) {
    if (index < 0 || index >= THEME_COUNT)
        return themes[0];
    return themes[index];
}

const char* getThemeName(int index) {
    if (index < 0 || index >= THEME_COUNT)
        return themes[0].name;
    return themes[index].name;
}

int loadThemeIndex(const std::string& basePath) {
    std::string path = basePath + "theme.cfg";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return DEFAULT_THEME_INDEX;
    uint8_t idx = 0;
    std::fread(&idx, 1, 1, f);
    std::fclose(f);
    if (idx >= THEME_COUNT) idx = DEFAULT_THEME_INDEX;
    return idx;
}

void saveThemeIndex(const std::string& basePath, int index) {
    std::string path = basePath + "theme.cfg";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t idx = static_cast<uint8_t>(index);
    std::fwrite(&idx, 1, 1, f);
    std::fclose(f);
}

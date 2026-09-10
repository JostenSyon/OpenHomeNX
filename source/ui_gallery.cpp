// Vista "Galleria" del selettore giochi: lista verticale + anteprima
// grande, alternativa alla griglia Classica (source/ui_selectors.cpp).
// Nuovo file apposta: la griglia Classica resta invariata, qui c'e' solo
// la logica aggiuntiva. Attivata/salvata da Impostazioni > Aspetto > Layout
// selettore giochi (settingsRowActivate, cat==1, in ui_selectors.cpp) e da
// gameSelectorLayout_/theme.h::loadGameSelectorLayout (persistita in
// gallery.cfg accanto a theme.cfg/zoom.cfg).
//
// La navigazione (D-pad/stick/A/B, avatar/zaino/banche/eject/gear) resta
// quella di handleGameSelectorInput() in ui_selectors.cpp: con COLS=1 e
// GAMES_PER_PAGE=numGames (impostati lì in base a gameSelectorLayout_), la
// stessa moveGrid() diventa scorrimento verticale puro, senza pagine.
// Qui c'e' solo il disegno e il tap/click sulla lista.

#include "ui.h"
#include "i18n.h"
#include <algorithm>
#include <cmath>

// Colore medio della cover (campionamento rado, angoli trasparenti esclusi),
// con una spinta di saturazione leggera perche' la media grezza di una
// copertina tende al grigio/marrone e legge come "sporco" invece che come
// colore. Usato per il velo "vetro opaco" dietro l'anteprima in Galleria.
SDL_Color UI::computeAccentColor(SDL_Surface* surf) const {
    SDL_Color fallback = T().cursor;
    if (!surf) return fallback;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
    if (!rgba) return fallback;
    SDL_LockSurface(rgba);
    Uint32* px = (Uint32*)rgba->pixels;
    int stride = rgba->pitch / 4;
    long rs = 0, gs = 0, bs = 0, n = 0;
    int stepX = std::max(1, rgba->w / 24);
    int stepY = std::max(1, rgba->h / 24);
    for (int y = 0; y < rgba->h; y += stepY) {
        for (int x = 0; x < rgba->w; x += stepX) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(px[y * stride + x], rgba->format, &r, &g, &b, &a);
            if (a < 16) continue; // pixel trasparenti (bordi/angoli) non contano
            rs += r; gs += g; bs += b; n++;
        }
    }
    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);
    if (n == 0) return fallback;
    float r = (float)rs / n, g = (float)gs / n, b = (float)bs / n;
    float mx = std::max(r, std::max(g, b));
    float mn = std::min(r, std::min(g, b));
    if (mx - mn < 40.0f) {
        float avg = (r + g + b) / 3.0f;
        constexpr float BOOST = 1.25f;
        r = avg + (r - avg) * BOOST;
        g = avg + (g - avg) * BOOST;
        b = avg + (b - avg) * BOOST;
    }
    auto clamp8 = [](float v) -> Uint8 {
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        return (Uint8)v;
    };
    return SDL_Color{ clamp8(r), clamp8(g), clamp8(b), 255 };
}

namespace {
// Geometria condivisa fra drawGameList_Gallery() e selectorTapGallery():
// TENERLE IN SYNC se si cambia una delle due.
constexpr int GAL_LIST_X = 70, GAL_LIST_Y = 108, GAL_LIST_W = 360;
constexpr int GAL_ROW_H = 46, GAL_VISIBLE_ROWS = 9;
constexpr int GAL_PREVIEW_X = GAL_LIST_X + GAL_LIST_W + 40;   // 470
constexpr int GAL_PREVIEW_Y = 100;
constexpr int GAL_PREVIEW_H = 460;

int galClampSel(int cursor, int numGames) {
    if (numGames <= 0) return 0;
    if (cursor < 0) return 0;
    if (cursor >= numGames) return numGames - 1;
    return cursor;
}

int galScrollFor(int sel, int numGames) {
    int maxScroll = std::max(0, numGames - GAL_VISIBLE_ROWS);
    int scroll = sel - GAL_VISIBLE_ROWS / 2;
    if (scroll < 0) scroll = 0;
    if (scroll > maxScroll) scroll = maxScroll;
    return scroll;
}
} // namespace

void UI::drawGameList_Gallery() {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    const int previewW = SCREEN_W - GAL_PREVIEW_X - 70;

    int sel = galClampSel(gameSelCursor_, numGames);
    int scroll = galScrollFor(sel, numGames);
    int rowEnd = std::min(scroll + GAL_VISIBLE_ROWS, numGames);

    bool cursorOnList = !gameSelOnAllBanks_ && !gameSelOnSettings_ && !gameSelOnEject_ &&
                         !gameSelOnAvatar_ && !gameSelOnPack_ && gameSelOnChevron_ == 0;

    auto dot = [&](int cx, int cy, int rr, SDL_Color col) {
        SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
        for (int dy = -rr; dy <= rr; dy++) {
            int dx = static_cast<int>(std::sqrt((double)(rr * rr - dy * dy)));
            SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    };

    for (int i = scroll; i < rowEnd; i++) {
        int rowY = GAL_LIST_Y + (i - scroll) * GAL_ROW_H;
        int rowH = GAL_ROW_H - 6;
        bool isSel = cursorOnList && (i == sel);
        if (isSel) {
            drawRoundRect(GAL_LIST_X, rowY, GAL_LIST_W, rowH, 10, T().menuHighlight);
            drawRoundRectOutline(GAL_LIST_X, rowY, GAL_LIST_W, rowH, 10, T().cursor, 2);
        }

        GameType g = availableGames_[i];
        auto acIt = gameAccentCache_.find(g);
        SDL_Color accent = (acIt != gameAccentCache_.end()) ? acIt->second : T().cursor;
        dot(GAL_LIST_X + 18, rowY + rowH / 2, 5, accent);

        std::string name = gameDisplayNameOf(g);
        if (name.substr(0, 8) == "Pokemon ") name = name.substr(8);
        if (name.length() > 26) name = name.substr(0, 25) + ".";
        drawText(name, GAL_LIST_X + 34, rowY + rowH / 2 - 9, T().text, fontSmall_);

        auto bc = gameBankCounts_.find(g);
        int bankCount = (bc != gameBankCounts_.end()) ? bc->second : 0;
        std::string bankStr = std::to_string(bankCount);
        const auto& be = getTextEntry(bankStr, fontSmall_, T().textDim);
        drawText(bankStr, GAL_LIST_X + GAL_LIST_W - 16 - be.w, rowY + rowH / 2 - 9,
                 T().textDim, fontSmall_);
    }
    if (scroll > 0)
        drawTextCentered("...", GAL_LIST_X + GAL_LIST_W / 2, GAL_LIST_Y - 14, T().textDim, fontSmall_);
    if (rowEnd < numGames)
        drawTextCentered("...", GAL_LIST_X + GAL_LIST_W / 2,
                         GAL_LIST_Y + GAL_VISIBLE_ROWS * GAL_ROW_H + 6, T().textDim, fontSmall_);

    // Pannello anteprima: sfondo tema (panelBg, come le card della griglia)
    // + un velo semitrasparente col colore della cover selezionata sopra.
    // Non e' un vero blur (costoso, niente pipeline shader qui): un tint
    // "vetro colorato" sopra panelBg legge comunque bene e costa pochissimo.
    GameType selGame = availableGames_[sel];
    auto acIt = gameAccentCache_.find(selGame);
    SDL_Color accent = (acIt != gameAccentCache_.end()) ? acIt->second : T().cursor;

    drawRoundRect(GAL_PREVIEW_X, GAL_PREVIEW_Y, previewW, GAL_PREVIEW_H, 18, T().panelBg);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    drawRoundRect(GAL_PREVIEW_X, GAL_PREVIEW_Y, previewW, GAL_PREVIEW_H, 18,
                  SDL_Color{accent.r, accent.g, accent.b, 70});
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    drawRoundRectOutline(GAL_PREVIEW_X, GAL_PREVIEW_Y, previewW, GAL_PREVIEW_H, 18, T().cursor, 2);

    constexpr int COVER = 260;
    int coverX = GAL_PREVIEW_X + 46;
    int coverY = GAL_PREVIEW_Y + (GAL_PREVIEW_H - COVER) / 2;
    auto icIt = gameIconCache_.find(selGame);
    if (icIt != gameIconCache_.end() && icIt->second) {
        SDL_Rect dst = {coverX, coverY, COVER, COVER};
        SDL_RenderCopy(renderer_, icIt->second, nullptr, &dst);
    } else {
        // Placeholder semplice: i giochi importati/Gen1/Gen2/RSE senza
        // titleId hanno in griglia un trattamento box-art dedicato molto
        // elaborato (vedi drawGameSelectorFrame) che qui non e' replicato
        // in questa prima versione della Galleria -- solo tag colorato.
        drawRoundRect(coverX, coverY, COVER, COVER, 14, T().iconPlaceholder);
        const char* tag = gameInfo(selGame).gameTag;
        drawTextCentered(tag, coverX + COVER / 2, coverY + COVER / 2, T().text, font_);
    }

    int textX = coverX + COVER + 40;
    int textY = GAL_PREVIEW_Y + GAL_PREVIEW_H / 2 - 40;
    std::string name = gameDisplayNameOf(selGame);
    if (name.substr(0, 8) == "Pokemon ") name = name.substr(8);
    drawText(name, textX, textY, T().text, font_);

    auto bc = gameBankCounts_.find(selGame);
    int bankCount = (bc != gameBankCounts_.end()) ? bc->second : 0;
    std::string bankStr = "(" + std::to_string(bankCount) + ")";
    drawText(bankStr, textX, textY + 40, T().textDim, fontSmall_);
}

void UI::selectorTapGallery(float px, float py, bool& running) {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    int sel = galClampSel(gameSelCursor_, numGames);
    int scroll = galScrollFor(sel, numGames);
    int rowEnd = std::min(scroll + GAL_VISIBLE_ROWS, numGames);

    if (px < GAL_LIST_X || px > GAL_LIST_X + GAL_LIST_W) { (void)running; return; }
    for (int i = scroll; i < rowEnd; i++) {
        int rowY = GAL_LIST_Y + (i - scroll) * GAL_ROW_H;
        int rowH = GAL_ROW_H - 6;
        if (py >= rowY && py <= rowY + rowH) {
            gameSelCursor_ = i;
            gameSelOnAllBanks_ = gameSelOnAvatar_ = false;
            gameSelOnEject_ = gameSelOnSettings_ = false;
            gameSelOnChevron_ = 0;
            selectGame(availableGames_[i], importedOccurrence(i));
            markDirty();
            return;
        }
    }
    (void)running;
}

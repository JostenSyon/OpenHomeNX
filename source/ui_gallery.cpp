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
#include "debug_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

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

// Pallini della lista un po' piu' accesi dell'accent "vero" (quello del
// pannello vetro, che va bene com'e' e non va toccato): stesso trucco gia'
// usato in computeAccentColor(), applicato qui a parte per non alterare
// nient'altro. Espande i canali attorno alla media, poi clampa.
SDL_Color boostSaturation(SDL_Color c, float amount) {
    float r = c.r, g = c.g, b = c.b;
    float avg = (r + g + b) / 3.0f;
    r = avg + (r - avg) * amount;
    g = avg + (g - avg) * amount;
    b = avg + (b - avg) * amount;
    auto clamp8 = [](float v) -> Uint8 {
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        return (Uint8)v;
    };
    return SDL_Color{ clamp8(r), clamp8(g), clamp8(b), c.a };
}
} // namespace

void UI::drawGameList_Gallery() {
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    const int previewW = SCREEN_W - GAL_PREVIEW_X - 70;

    int sel = galClampSel(gameSelCursor_, numGames);
    int scrollT = galScrollFor(sel, numGames);
    // Scroll fluido: insegue il target a frazioni di riga (come le pagine).
    if (galScrollX_ < 0) galScrollX_ = (float)scrollT;
    float d = (float)scrollT - galScrollX_;
    if (d > -0.05f && d < 0.05f) {
        galScrollX_ = (float)scrollT;
    } else {
        galScrollX_ += d * 0.3f;
        markDirty();
    }
    int scroll = (int)galScrollX_;
    int pixOff = (int)((galScrollX_ - scroll) * GAL_ROW_H);
    int rowEnd = std::min(scroll + GAL_VISIBLE_ROWS + 1, numGames);

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
        int rowY = GAL_LIST_Y + (i - scroll) * GAL_ROW_H - pixOff;
        int rowH = GAL_ROW_H - 6;
        bool isSel = cursorOnList && (i == sel);
        if (isSel) {
            drawRoundRect(GAL_LIST_X, rowY, GAL_LIST_W, rowH, 10, T().menuHighlight);
            drawRoundRectOutline(GAL_LIST_X, rowY, GAL_LIST_W, rowH, 10, T().cursor, 2);
        }

        GameType g = availableGames_[i];
        auto acIt = gameAccentCache_.find(g);
        SDL_Color accent = (acIt != gameAccentCache_.end()) ? acIt->second : flatBgColorFor(g);
        dot(GAL_LIST_X + 18, rowY + rowH / 2, 5, boostSaturation(accent, 1.5f));

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
        // Fisso nello slot sotto la 9a riga (posizione di riposo, quella
        // che si vede a scroll fermo): NON deve seguire rowEnd/pixOff, che
        // includono la riga extra e l'offset dello scroll fluido -- quella
        // versione faceva ballare i puntini su e giu' di 46px mentre si
        // scorre, invece di stare fermi dove finira' la lista. Abbassato
        // di un rigo intero (GAL_ROW_H) su richiesta.
        drawTextCentered("...", GAL_LIST_X + GAL_LIST_W / 2,
                         GAL_LIST_Y + (GAL_VISIBLE_ROWS + 1) * GAL_ROW_H + 6, T().textDim, fontSmall_);

    // Animazione anteprima: l'indice mostrato (galSelShown_) insegue il
    // cursore (sel) con uno slide verticale, stesso schema esatto di
    // selPageShown_/selSlide_ nella griglia Classica (drawGameSelectorFrame):
    // fase 1 esce verso il bordo opposto alla direzione di marcia, allo swap
    // l'indice scatta e il nuovo contenuto rientra dal lato opposto.
    if (galSelShown_ < 0) galSelShown_ = sel; // primo frame: niente animazione
    if (galSelShown_ != sel) {
        float dir = (sel > galSelShown_) ? -1.0f : 1.0f;
        float mag = std::fabs(galSlide_) + (1.0f - std::fabs(galSlide_)) * 0.3f + 0.02f;
        if (mag >= 1.0f) {
            galSelShown_ = sel;
            galSlide_ = -dir;
        } else {
            galSlide_ = mag * dir;
        }
        markDirty();
    } else if (galSlide_ != 0.0f) {
        float s = galSlide_ * 0.7f;
        galSlide_ = (std::fabs(s) < 0.02f) ? 0.0f : s;
        markDirty();
    }
    int shown = galClampSel(galSelShown_, numGames);
    // L'intero pannello (vetro + contorno + copertina + testo) si sposta in
    // blocco, non solo il contenuto dentro un riquadro fisso: e' un unico
    // rettangolo "carta" che scorre verticalmente, non una finestra con
    // dentro un contenuto che scivola.
    int panelY = GAL_PREVIEW_Y + (int)(galSlide_ * GAL_PREVIEW_H);

    // Pannello anteprima: gradiente orizzontale con drawRoundRectGradientH()
    // (una sola passata opaca, stessa sagoma di drawRoundRect() ma colorata
    // colonna per colonna: niente blend mode, quindi niente rischio del
    // doppio-alpha "pacman" agli angoli visto con le due passate originali).
    // Accent pieno vicino alla copertina (a sinistra), sfuma verso il nero
    // andando a destra: piu' "vetro colorato" e meno tinta piatta uniforme.
    GameType selGame = availableGames_[shown];
    auto acIt = gameAccentCache_.find(selGame);
    // I giochi senza titleId (importati/Gen1/Gen2/RSE senza NS control data)
    // non passano mai da gameIconCache_ (vedi loadGameIcons()), quindi non
    // hanno mai un campione reale in gameAccentCache_: senza questo
    // fallback la copertina non "prendeva" nessun colore proprio, solo il
    // cursore del tema. flatBgColorFor() e' lo stesso colore usato come
    // sfondo della loro tile in drawGameArt(), quindi resta coerente.
    SDL_Color accent = (acIt != gameAccentCache_.end()) ? acIt->second : flatBgColorFor(selGame);
    SDL_Color glassLeft = accent;
    SDL_Color glassRight = SDL_Color{0, 0, 0, 255};
    drawRoundRectGradientH(GAL_PREVIEW_X, panelY, previewW, GAL_PREVIEW_H, 18, glassLeft, glassRight);
    drawRoundRectOutline(GAL_PREVIEW_X, panelY, previewW, GAL_PREVIEW_H, 18, T().cursor, 2);

    // Stessa logica della griglia Classica (copertina reale se in cache,
    // altrimenti loghi/colori flat/posizionamento RSE custom): drawGameArt()
    // fa gia' al suo interno il lookup su gameIconCache_, quindi le formule
    // (gia' espresse come percentuali di IS) scalano da sole a COVER senza
    // bisogno di duplicare qui il ramo "copertina trovata".
    constexpr int COVER = 260;
    int coverX = GAL_PREVIEW_X + 46;
    int coverY = panelY + (GAL_PREVIEW_H - COVER) / 2;
    drawGameArt(shown, coverX, coverY, COVER, true);

    int textX = coverX + COVER + 40;
    int textY = panelY + GAL_PREVIEW_H / 2 - 46;
    std::string name = gameDisplayNameOf(selGame);
    if (name.substr(0, 8) == "Pokemon ") name = name.substr(8);
    // Titolo in fontLarge_ (28pt, gia' usato per i titoli About/errore) reso
    // in grassetto qui sul momento: piu' vicino allo stile "da copertina"
    // del mockup del font di sistema normale usato ovunque nell'app. Lo
    // stile e' globale sul font_ pointer (SDL_ttf), quindi va ripristinato
    // subito dopo per non sporcare gli altri usi di fontLarge_ nello stesso
    // frame (es. il popup About sopra questa stessa schermata).
    TTF_SetFontStyle(fontLarge_, TTF_STYLE_BOLD);
    drawText(name, textX, textY, T().text, fontLarge_);
    TTF_SetFontStyle(fontLarge_, TTF_STYLE_NORMAL);

    drawText(i18n::get(StrKey::PartyPokemon), textX, textY + 46, T().textDim, fontSmall_);

    // Party preview: lazy sul gioco fermo da 400ms.
    uint32_t nowT = SDL_GetTicks();
    if (sel != galPreviewGame_) {
        galPreviewGame_ = sel;
        galPreviewTick_ = nowT;
        DebugLog::line("gal preview: settled %s cache=%d", gameInfo(selGame).gameTag,
                       galPartyCache_.count(selGame));
    } else if (nowT - galPreviewTick_ > 400) {
        galEnsureParty(selGame);
    }
    {
        int partyY = textY + 46;
        int rightEdge = GAL_PREVIEW_X + previewW - 40;
        int partyW = 5 * 36 + 32;
        int partyXFixed = rightEdge - partyW;
        if (partyXFixed < textX) partyXFixed = textX;
        auto pit = galPartyCache_.find(selGame);
        if (pit == galPartyCache_.end()) {
            drawText("…", partyXFixed, partyY, T().textDim, fontSmall_);
        } else {
            for (int k = 0; k < 6; k++) {
                const PartyPreviewMon& m = pit->second.mons[k];
                bool isEmpty = m.empty;
                SDL_Texture* spr = nullptr;
                if (isEmpty) {
                    spr = getBallSprite(4);
                    if (!spr) spr = iconBoxEmpty_;
                } else if (m.egg) {
                    spr = getSprite(0);
                } else if (m.shiny) {
                    spr = getShinySprite(m.species, m.form);
                    if (!spr) spr = getSprite(m.species, m.form);
                } else {
                    spr = getSprite(m.species, m.form);
                }
                if (!spr) continue;
                if (isEmpty) {
                    SDL_SetTextureColorMod(spr, 110, 110, 110);
                    SDL_SetTextureAlphaMod(spr, 110);
                }
                drawSpriteFit(partyXFixed + k * 36, partyY - 4, 32, 32, spr);
                if (isEmpty) {
                    SDL_SetTextureColorMod(spr, 255, 255, 255);
                    SDL_SetTextureAlphaMod(spr, 255);
                }
            }
            if (!pit->second.otName.empty()) {
                std::string lbl = i18n::get(StrKey::FilterOT);
                int rowY = partyY + 40;
                drawText(lbl, textX, rowY, T().textDim, fontSmall_);
                // OT troncato se supera l'ultima ball
                std::string ot = pit->second.otName;
                int maxOtW = rightEdge - (textX + (int)getTextEntry(lbl, fontSmall_, T().textDim).w + 12);
                if (maxOtW < 40) maxOtW = 40;
                int otw = getTextEntry(ot, fontSmall_, T().text).w;
                while (ot.size() > 5 && otw > maxOtW) {
                    ot = ot.substr(0, ot.size() - 5) + "(..)";
                    otw = getTextEntry(ot, fontSmall_, T().text).w;
                }
                const auto& ve = getTextEntry(ot, fontSmall_, T().text);
                drawText(ot, rightEdge - (int)ve.w, rowY, T().text, fontSmall_);
            }
        }
    }
}

void UI::selectorTapGallery(float px, float py, bool& running) {    int numGames = (int)availableGames_.size();
    if (numGames == 0) return;

    int sel = galClampSel(gameSelCursor_, numGames);
    // Stesso offset fluido del draw (senza avanzare l'animazione qui).
    float fx = galScrollX_ < 0 ? (float)galScrollFor(sel, numGames) : galScrollX_;
    int scroll = (int)fx;
    int pixOff = (int)((fx - scroll) * GAL_ROW_H);
    int rowEnd = std::min(scroll + GAL_VISIBLE_ROWS + 1, numGames);

    if (px < GAL_LIST_X || px > GAL_LIST_X + GAL_LIST_W) { (void)running; return; }
    for (int i = scroll; i < rowEnd; i++) {
        int rowY = GAL_LIST_Y + (i - scroll) * GAL_ROW_H - pixOff;
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

bool UI::galleryScrollAnim() {
    if (gameSelectorLayout_ != GameSelectorLayout::Gallery) return false;
    int numGames = (int)availableGames_.size();
    if (numGames == 0 || galScrollX_ < 0) return false;
    int sel = galClampSel(gameSelCursor_, numGames);
    float d = (float)galScrollFor(sel, numGames) - galScrollX_;
    return d < -0.05f || d > 0.05f;
}

bool UI::galleryPreviewAnim() {
    if (gameSelectorLayout_ != GameSelectorLayout::Gallery) return false;
    int numGames = (int)availableGames_.size();
    if (numGames == 0) return false;
    int sel = galClampSel(gameSelCursor_, numGames);
    // Settle party preview qui (non nel draw: da fermo non parte mai).
    if (sel != galPreviewGame_) {
        galPreviewGame_ = sel;
        galPreviewTick_ = SDL_GetTicks();
        return true;
    }
    if (galSelShown_ != sel || galSlide_ != 0.0f) return true;
    if (galPartyCache_.find(availableGames_[sel]) == galPartyCache_.end()) {
        // Il commento sopra diceva "settle qui" ma il caricamento vero e
        // proprio avveniva solo dentro drawGameList_Gallery(), quindi senza
        // ulteriori markDirty() a valle (es. muovendo il cursore su
        // avatar/zaino/banca) il draw non veniva mai richiamato e la
        // party restava vuota a schermo fermo. Carica direttamente qui.
        if (SDL_GetTicks() - galPreviewTick_ > 400) galEnsureParty(availableGames_[sel]);
        return true;
    }
    return false;
}

long UI::galSaveMtime(GameType g) {
    // Titoli: mount + stat singolo (niente decrypt). File: stat diretto.
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount() &&
        titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0') {
        std::string mnt = account_.mountSave(selectedProfile_, g);
        if (mnt.empty()) return -1;
        struct stat st;
        long mt = (stat((mnt + saveFileNameOf(g)).c_str(), &st) == 0) ? (long)st.st_mtime : -1;
        account_.unmountSave();
        return mt;
    }
    std::string p = importedSavePath(g, 0);
    if (p.empty()) return -1;
    struct stat st;
    return (stat(p.c_str(), &st) == 0) ? (long)st.st_mtime : -1;
}

void UI::galEnsureParty(GameType g) {
    long mt = galSaveMtime(g);
    auto it = galPartyCache_.find(g);
    // Cache valida: esci (niente mount a ogni frame). Ricontrolla al max
    // ogni 10s per i save cambiati fuori dall'app.
    if (it != galPartyCache_.end()) {
        if (it->second.mtime == mt) return;
        if (SDL_GetTicks() - galPreviewTick_ < 10000) return;
    }
    DebugLog::line("gal party: load %s (mt=%ld)", gameInfo(g).gameTag, mt);
    PartyPreview pv;
    pv.mtime = mt;
    pv.mons.assign(6, PartyPreviewMon{});
    // Load scratch (non tocca save_ corrente). Costo solo al cambio save.
    {
        SaveFile sf;
        sf.setGameType(g);
        std::string path;
        std::string mnt;
        bool isTitle = selectedProfile_ >= 0 && titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
        if (isTitle) {
            mnt = account_.mountSave(selectedProfile_, g);
            if (!mnt.empty()) path = mnt + saveFileNameOf(g);
        } else {
            path = importedSavePath(g, 0);
        }
        if (!path.empty()) {
            sf.load(path);
            int filled = 0;
            if (sf.isLoaded()) {
                for (int s = 0; s < 6; s++) {
                    Pokemon pkm = sf.getPartySlot(s);
                    if (pkm.isEmpty()) continue;
                    PartyPreviewMon m;
                    m.empty = false;
                    m.species = pkm.species();
                    m.level = pkm.level();
                    m.form = pkm.form();
                    m.shiny = pkm.isShiny();
                    m.egg = pkm.isEgg();
                    pv.mons[s] = m;
                    filled++;
                }
                pv.otName = sf.dsOtName();
            } else {
                DebugLog::line("gal party: %s load FALLITO path=%s", gameInfo(g).gameTag, path.c_str());
            }
            DebugLog::line("gal party: %s cached %d/6", gameInfo(g).gameTag, filled);
        } else {
            DebugLog::line("gal party: %s nessun path (profilo? import?)", gameInfo(g).gameTag);
            DebugLog::line("gal party: %s cached 0/6", gameInfo(g).gameTag);
        }
        if (!mnt.empty()) account_.unmountSave();
    }
    // Per screenshot/registrazioni: se sdmc:/.../overrideOT.cfg esiste (accanto
    // all'nro, stessa basePath_ di theme.cfg/gallery.cfg), il suo contenuto
    // sostituisce l'OT reale ovunque in Galleria — niente nome vero in giro.
    {
        FILE* f = std::fopen((basePath_ + "overrideOT.cfg").c_str(), "rb");
        if (f) {
            char buf[64] = {0};
            size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
            std::fclose(f);
            std::string ov(buf, n);
            while (!ov.empty() && (ov.back() == '\n' || ov.back() == '\r' || ov.back() == ' '))
                ov.pop_back();
            if (!ov.empty()) {
                DebugLog::line("gal party: overrideOT.cfg attivo, OT mostrato come '%s'", ov.c_str());
                pv.otName = ov;
            }
        }
    }
    galPartyCache_[g] = pv;
    markDirty();
}

void UI::galInvalidateParty(GameType g) {
    galPartyCache_.erase(g);
}

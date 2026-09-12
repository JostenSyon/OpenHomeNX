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
#include "pokedex.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
    auto star = [&](int cx, int cy, int rr, SDL_Color col) {
        SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
        const float PI = 3.14159265f;
        float outer = (float)rr;
        float inner = outer * 0.45f;
        struct Pt { float x, y; };
        Pt pts[10];
        for (int k = 0; k < 10; k++) {
            float r = (k % 2 == 0) ? outer : inner;
            float ang = -90.0f + k * 36.0f;
            float rad = ang * PI / 180.0f;
            pts[k].x = cx + r * std::cos(rad);
            pts[k].y = cy + r * std::sin(rad);
        }
        int y0 = (int)std::floor(cy - outer);
        int y1 = (int)std::ceil(cy + outer);
        float xs[12];
        for (int y = y0; y <= y1; y++) {
            int xn = 0;
            for (int e = 0; e < 10; e++) {
                Pt a = pts[e];
                Pt b = pts[(e + 1) % 10];
                float minY = std::min(a.y, b.y);
                float maxY = std::max(a.y, b.y);
                if (y < minY || y >= maxY) continue;
                if (std::fabs(b.y - a.y) < 0.001f) continue;
                float x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (xn < 12) xs[xn++] = x;
            }
            if (xn < 2) continue;
            for (int a = 0; a < xn - 1; a++) for (int b = a + 1; b < xn; b++) if (xs[a] > xs[b]) std::swap(xs[a], xs[b]);
            for (int k = 0; k + 1 < xn; k += 2) {
                int x0 = (int)std::ceil(xs[k]);
                int x1 = (int)std::floor(xs[k + 1]);
                if (x1 >= x0) SDL_RenderDrawLine(renderer_, x0, y, x1, y);
            }
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
        SDL_Color col = boostSaturation(accent, 1.5f);
        if (isFavorite(g)) star(GAL_LIST_X + 18, rowY + rowH / 2, 7, col);
        else dot(GAL_LIST_X + 18, rowY + rowH / 2, 5, col);

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
        galSettleChecked_ = false;
        DebugLog::line("gal preview: settled %s cache=%d", gameInfo(selGame).gameTag,
                       (int)galPartyCache_.count(selGame));
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
            int statRowY = partyY + 40;
            if (!pit->second.otName.empty()) {
                std::string lbl = i18n::get(StrKey::FilterOT);
                drawText(lbl, textX, statRowY, T().textDim, fontSmall_);
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
                drawText(ot, rightEdge - (int)ve.w, statRowY, T().text, fontSmall_);
                statRowY += 26;
            }
            if (pit->second.dexSupported) {
                // Non tradotto di proposito: e' lo stesso trattamento che
                // questo repo gia' riserva alla parola "Pokedex" altrove
                // (romfs/data/strings/it.json "transfer_not_in_dex" la tiene
                // testuale anche in italiano, "Pokédex"), quindi non serve
                // una nuova chiave i18n -- coerente col resto del progetto.
                std::string lbl = "Pokédex";
                drawText(lbl, textX, statRowY, T().textDim, fontSmall_);
                std::string val = std::to_string(pit->second.dexCaught) + "/" +
                                   std::to_string(pit->second.dexTotal);
                const auto& ve = getTextEntry(val, fontSmall_, T().text);
                drawText(val, rightEdge - (int)ve.w, statRowY, T().text, fontSmall_);
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
        galSettleChecked_ = false;
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
    if (!galCacheLoadedFromDisk_) {
        galCacheLoadedFromDisk_ = true;
        galLoadCacheFromDisk();
    }
    // Override OT (per screenshot/registrazioni): sdmc:/.../overrideOT.cfg
    // accanto all'nro, stessa basePath_ di theme.cfg/gallery.cfg. Riletto
    // al massimo ogni 500ms (galOverrideOtTick_/galOverrideOtCached_),
    // PRIMA del controllo cache sotto -- se fosse dentro il ramo "carica da
    // zero" soltanto, creare/rimuovere il file non avrebbe mai effetto su
    // un gioco gia' in cache finche' il suo save non cambia davvero
    // (l'mtime combacerebbe e la funzione uscirebbe subito, bug segnalato
    // 2026-09-11). 500ms resta percettivamente istantaneo per chi prepara
    // uno screenshot, senza un fopen/fread reale a 60Hz. Pura lettura: non
    // tocca mai il save reale.
    uint32_t nowOt = SDL_GetTicks();
    if (nowOt - galOverrideOtTick_ > 500) {
        galOverrideOtTick_ = nowOt;
        std::string fresh;
        FILE* f = std::fopen((basePath_ + "overrideOT.cfg").c_str(), "rb");
        if (f) {
            char buf[64] = {0};
            size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
            std::fclose(f);
            fresh.assign(buf, n);
            while (!fresh.empty() && (fresh.back() == '\n' || fresh.back() == '\r' || fresh.back() == ' '))
                fresh.pop_back();
        }
        galOverrideOtCached_ = fresh;
    }
    std::string overrideOt = galOverrideOtCached_;
    auto it = galPartyCache_.find(g);
    if (it != galPartyCache_.end()) {
        // Applica/rimuovi l'override sull'entry gia' in cache SEMPRE, ad
        // ogni chiamata: e' una lettura di file locale (nessun mount),
        // costa nulla, e permette di vedere l'effetto dell'override in
        // tempo reale restando fermi sullo stesso gioco (serve per
        // screenshot/registrazioni, vedi commento sopra).
        std::string wanted = overrideOt.empty() ? it->second.otNameReal : overrideOt;
        if (it->second.otName != wanted) {
            it->second.otName = wanted;
            galSaveCacheToDisk();
            markDirty();
        }
    }
    // Il probe vero e proprio (mount+stat) invece avviene al massimo una
    // volta per atterraggio sulla selezione: nessun'altra app puo' scrivere
    // questo save mentre restiamo fermi qui (solo OpenHomeNX puo' farlo, e
    // allora invalida esplicitamente via galInvalidateParty), quindi
    // ripeterlo di continuo serviva solo a rimontare/smontare a ogni frame
    // (causa di un flicker gia' fixato). galSettleChecked_ e' resettato ad
    // ogni nuovo atterraggio dai due call site in drawGameList_Gallery()/
    // galleryPreviewAnim().
    if (galSettleChecked_) return;
    galSettleChecked_ = true;
    // Se il probe fallisce (mt<0) NON ricaricare: una probe flaky non deve
    // mai sovrascrivere una cache buona col vuoto (flicker party/OT/dex).
    long mt = galSaveMtime(g);
    if (mt < 0) {
        if (it == galPartyCache_.end())
            DebugLog::line("gal party: %s probe fallita, niente cache", gameInfo(g).gameTag);
        return;
    }
    // Cache ancora valida (save non cambiato da quando l'abbiamo vista
    // l'ultima volta, es. all'atterraggio precedente o al boot): esci
    // senza ricaricare.
    if (it != galPartyCache_.end() && it->second.mtime == mt) return;
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
            if (!sf.isLoaded()) {
                // Load fallito (save illeggibile): tieni la cache vecchia,
                // mai avvelenare col vuoto. Solo log, niente store.
                DebugLog::line("gal party: %s load FALLITO path=%s (tengo cache)", gameInfo(g).gameTag, path.c_str());
                return;
            }
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
            pv.otNameReal = sf.dsOtName();
            Pokedex::DexStatus dex = Pokedex::getDexStatus(sf);
            pv.dexSupported = dex.supported;
            pv.dexCaught = dex.caught;
            pv.dexTotal = dex.total;
            DebugLog::line("gal party: %s cached %d/6", gameInfo(g).gameTag, filled);
        } else {
            // Nessun path: scrivi il vuoto solo se non c'è già una cache
            // (primo giro), altrimenti tienila (stesso anti-flicker sopra).
            if (it != galPartyCache_.end()) {
                DebugLog::line("gal party: %s nessun path (tengo cache)", gameInfo(g).gameTag);
                return;
            }
            DebugLog::line("gal party: %s nessun path (profilo? import?)", gameInfo(g).gameTag);
            DebugLog::line("gal party: %s cached 0/6", gameInfo(g).gameTag);
        }
        if (!mnt.empty()) account_.unmountSave();
    }
    // overrideOt gia' letto in cima alla funzione (vedi commento li').
    pv.otName = overrideOt.empty() ? pv.otNameReal : overrideOt;
    if (!overrideOt.empty())
        DebugLog::line("gal party: overrideOT.cfg attivo, OT mostrato come '%s'", overrideOt.c_str());
    galPartyCache_[g] = pv;
    galSaveCacheToDisk();
    markDirty();
}

void UI::galInvalidateParty(GameType g) {
    galPartyCache_.erase(g);
    galSaveCacheToDisk();
}

namespace {
constexpr uint32_t GAL_CACHE_MAGIC = 0x47414331; // "GAC1"
}

// Persistenza di galPartyCache_ (party/OT/dex) tra un avvio e l'altro:
// senza, ad ogni riavvio la Galleria mostra "..." finche' non ti fermi di
// nuovo su ogni gioco, anche se il save di quel gioco non e' mai cambiato.
// Formato binario minimo (stesso stile di theme.cfg/zoom.cfg, non serve un
// parser): l'mtime salvato qui e' la stessa garanzia di validita' gia'
// usata a runtime in galEnsureParty() (mtime combacia col save reale ->
// niente ricaricamento, mtime diverso -> ricarica e riscrive).
void UI::galLoadCacheFromDisk() {
    FILE* f = std::fopen((basePath_ + "gallery_cache.dat").c_str(), "rb");
    if (!f) return;
    uint32_t magic = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != GAL_CACHE_MAGIC) {
        std::fclose(f);
        return;
    }
    uint8_t version = 0;
    uint32_t count = 0;
    if (std::fread(&version, sizeof(version), 1, f) != 1 || version != 2 ||
        std::fread(&count, sizeof(count), 1, f) != 1 || count > 4096) {
        std::fclose(f);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t gameByte = 0;
        int64_t mtime = -1;
        uint8_t dexSupported = 0;
        int32_t dexCaught = 0, dexTotal = 0;
        uint8_t otLen = 0;
        if (std::fread(&gameByte, 1, 1, f) != 1 ||
            std::fread(&mtime, sizeof(mtime), 1, f) != 1 ||
            std::fread(&dexSupported, 1, 1, f) != 1 ||
            std::fread(&dexCaught, sizeof(dexCaught), 1, f) != 1 ||
            std::fread(&dexTotal, sizeof(dexTotal), 1, f) != 1 ||
            std::fread(&otLen, 1, 1, f) != 1)
            break;
        std::string ot;
        if (otLen > 0) {
            std::vector<char> buf(otLen);
            if (std::fread(buf.data(), 1, otLen, f) != otLen) break;
            ot.assign(buf.data(), otLen);
        }
        uint8_t otRealLen = 0;
        if (std::fread(&otRealLen, 1, 1, f) != 1) break;
        std::string otReal;
        if (otRealLen > 0) {
            std::vector<char> buf(otRealLen);
            if (std::fread(buf.data(), 1, otRealLen, f) != otRealLen) break;
            otReal.assign(buf.data(), otRealLen);
        }
        PartyPreview pv;
        pv.mtime = static_cast<long>(mtime);
        pv.dexSupported = dexSupported != 0;
        pv.dexCaught = dexCaught;
        pv.dexTotal = dexTotal;
        pv.otName = ot;
        pv.otNameReal = otReal;
        pv.mons.assign(6, PartyPreviewMon{});
        bool ok = true;
        for (int s = 0; s < 6 && ok; s++) {
            uint8_t empty = 1, egg = 0, shiny = 0, level = 0, form = 0;
            uint16_t species = 0;
            if (std::fread(&empty, 1, 1, f) != 1 || std::fread(&egg, 1, 1, f) != 1 ||
                std::fread(&shiny, 1, 1, f) != 1 || std::fread(&species, sizeof(species), 1, f) != 1 ||
                std::fread(&level, 1, 1, f) != 1 || std::fread(&form, 1, 1, f) != 1) {
                ok = false;
                break;
            }
            pv.mons[s] = PartyPreviewMon{species, level, form, shiny != 0, egg != 0, empty != 0};
        }
        if (!ok) break;
        if (gameByte >= GAME_TYPE_COUNT) continue; // file da una build futura/diversa: salta la voce
        galPartyCache_[static_cast<GameType>(gameByte)] = pv;
    }
    std::fclose(f);
    DebugLog::line("gal cache: caricate %zu voci da disco", galPartyCache_.size());
}

void UI::galSaveCacheToDisk() const {
    FILE* f = std::fopen((basePath_ + "gallery_cache.dat").c_str(), "wb");
    if (!f) return;
    uint32_t magic = GAL_CACHE_MAGIC;
    uint8_t version = 2;
    uint32_t count = static_cast<uint32_t>(galPartyCache_.size());
    std::fwrite(&magic, sizeof(magic), 1, f);
    std::fwrite(&version, sizeof(version), 1, f);
    std::fwrite(&count, sizeof(count), 1, f);
    for (const auto& [game, pv] : galPartyCache_) {
        uint8_t gameByte = static_cast<uint8_t>(game);
        int64_t mtime = pv.mtime;
        uint8_t dexSupported = pv.dexSupported ? 1 : 0;
        int32_t dexCaught = pv.dexCaught, dexTotal = pv.dexTotal;
        uint8_t otLen = static_cast<uint8_t>(std::min<size_t>(pv.otName.size(), 255));
        uint8_t otRealLen = static_cast<uint8_t>(std::min<size_t>(pv.otNameReal.size(), 255));
        std::fwrite(&gameByte, 1, 1, f);
        std::fwrite(&mtime, sizeof(mtime), 1, f);
        std::fwrite(&dexSupported, 1, 1, f);
        std::fwrite(&dexCaught, sizeof(dexCaught), 1, f);
        std::fwrite(&dexTotal, sizeof(dexTotal), 1, f);
        std::fwrite(&otLen, 1, 1, f);
        if (otLen > 0) std::fwrite(pv.otName.data(), 1, otLen, f);
        std::fwrite(&otRealLen, 1, 1, f);
        if (otRealLen > 0) std::fwrite(pv.otNameReal.data(), 1, otRealLen, f);
        for (int s = 0; s < 6; s++) {
            const PartyPreviewMon& m = pv.mons[s];
            uint8_t empty = m.empty ? 1 : 0, egg = m.egg ? 1 : 0, shiny = m.shiny ? 1 : 0;
            uint16_t species = m.species;
            uint8_t level = m.level, form = m.form;
            std::fwrite(&empty, 1, 1, f);
            std::fwrite(&egg, 1, 1, f);
            std::fwrite(&shiny, 1, 1, f);
            std::fwrite(&species, sizeof(species), 1, f);
            std::fwrite(&level, 1, 1, f);
            std::fwrite(&form, 1, 1, f);
        }
    }
    std::fclose(f);
}

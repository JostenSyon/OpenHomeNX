#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "crypto_engine.h"
#include "debug_log.h"
#include "species_converter.h"
#include "trade_evo.h"
#include "item_locations.h"
#include "app_version.h"
#include "move_types.h"
#include "update_net.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

// --- Sprites ---

static uint32_t spriteKey(uint16_t id, uint8_t form) {
    return uint32_t(id) | (uint32_t(form) << 16);
}

static SDL_Texture* loadSprite(const char* dir, uint16_t nationalId, uint8_t form,
                               SDL_Renderer* renderer) {
    char filename[64];
    // Try form-specific sprite first (e.g. 019-1.png)
    if (form != 0) {
        std::snprintf(filename, sizeof(filename), "%03d-%d.png", nationalId, form);
        std::string path;
        path = std::string("romfs:/") + dir + "/" + filename;
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FreeSurface(surf);
            return tex;
        }
    }

    // Fall back to base sprite (e.g. 019.png)
    std::snprintf(filename, sizeof(filename), "%03d.png", nationalId);
    std::string path;
    path = std::string("romfs:/") + dir + "/" + filename;
    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

SDL_Texture* UI::getSprite(uint16_t nationalId, uint8_t form) {
    uint32_t key = spriteKey(nationalId, form);
    auto it = spriteCache_.find(key);
    if (it != spriteCache_.end())
        return it->second;

    SDL_Texture* tex = loadSprite("sprites", nationalId, form, renderer_);
    spriteCache_[key] = tex;
    return tex;
}

SDL_Texture* UI::getShinySprite(uint16_t nationalId, uint8_t form) {
    uint32_t key = spriteKey(nationalId, form);
    auto it = shinySpriteCache_.find(key);
    if (it != shinySpriteCache_.end())
        return it->second;

    SDL_Texture* tex = loadSprite("sprites_shiny", nationalId, form, renderer_);
    shinySpriteCache_[key] = tex;
    return tex;
}

void UI::freeSprites() {
    for (auto& [id, tex] : spriteCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    spriteCache_.clear();
    for (auto& [id, tex] : shinySpriteCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    shinySpriteCache_.clear();
    for (auto& [name, tex] : ribbonSpriteCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    ribbonSpriteCache_.clear();
    for (auto& [id, tex] : ballSpriteCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    ballSpriteCache_.clear();
    for (auto& [id, tex] : typeSpriteCache_) {
        if (tex)
            SDL_DestroyTexture(tex);
    }
    typeSpriteCache_.clear();
    if (iconShiny_)      { SDL_DestroyTexture(iconShiny_);      iconShiny_ = nullptr; }
    if (iconAlpha_)      { SDL_DestroyTexture(iconAlpha_);      iconAlpha_ = nullptr; }
    if (iconShinyAlpha_) { SDL_DestroyTexture(iconShinyAlpha_); iconShinyAlpha_ = nullptr; }
    if (iconBoxFull_)     { SDL_DestroyTexture(iconBoxFull_);     iconBoxFull_ = nullptr; }
    if (iconBoxEmpty_)    { SDL_DestroyTexture(iconBoxEmpty_);    iconBoxEmpty_ = nullptr; }
    if (iconBoxNonEmpty_) { SDL_DestroyTexture(iconBoxNonEmpty_); iconBoxNonEmpty_ = nullptr; }
    if (iconVault_)        { SDL_DestroyTexture(iconVault_);        iconVault_ = nullptr; }
    if (iconEject_)        { SDL_DestroyTexture(iconEject_);        iconEject_ = nullptr; }
    if (iconSettings_)     { SDL_DestroyTexture(iconSettings_);     iconSettings_ = nullptr; }
    if (iconWifi_)         { SDL_DestroyTexture(iconWifi_);         iconWifi_ = nullptr; }
    if (iconLan_)          { SDL_DestroyTexture(iconLan_);          iconLan_ = nullptr; }
    if (iconDebug_)        { SDL_DestroyTexture(iconDebug_);        iconDebug_ = nullptr; }
    if (iconArrow_)        { SDL_DestroyTexture(iconArrow_);        iconArrow_ = nullptr; }
    if (iconPack_)         { SDL_DestroyTexture(iconPack_);         iconPack_ = nullptr; }
    if (iconRocket_)       { SDL_DestroyTexture(iconRocket_);       iconRocket_ = nullptr; }
    if (iconFloppy_)       { SDL_DestroyTexture(iconFloppy_);       iconFloppy_ = nullptr; }
    if (iconTrade_)        { SDL_DestroyTexture(iconTrade_);        iconTrade_ = nullptr; }
    if (iconDevBox_)       { SDL_DestroyTexture(iconDevBox_);       iconDevBox_ = nullptr; }
    if (iconDevSync_)      { SDL_DestroyTexture(iconDevSync_);      iconDevSync_ = nullptr; }
    if (iconDevLink_)      { SDL_DestroyTexture(iconDevLink_);      iconDevLink_ = nullptr; }
}

SDL_Texture* UI::getRibbonSprite(const std::string& filename) {
    auto it = ribbonSpriteCache_.find(filename);
    if (it != ribbonSpriteCache_.end())
        return it->second;

    std::string path;
    path = "romfs:/ribbons/" + filename + ".png";
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    ribbonSpriteCache_[filename] = tex; // cache even if null
    return tex;
}

SDL_Texture* UI::getBallSprite(uint8_t ballId) {
    auto it = ballSpriteCache_.find(ballId);
    if (it != ballSpriteCache_.end())
        return it->second;

    std::string path;
    path = "romfs:/balls/_ball" + std::to_string(ballId) + ".png";
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    ballSpriteCache_[ballId] = tex;
    return tex;
}

SDL_Texture* UI::getTypeSprite(uint8_t typeId) {
    auto it = typeSpriteCache_.find(typeId);
    if (it != typeSpriteCache_.end())
        return it->second;

    char filename[32];
    std::snprintf(filename, sizeof(filename), "type_icon_s_%02d.png", typeId);
    std::string path;
    path = std::string("romfs:/types/") + filename;
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    typeSpriteCache_[typeId] = tex;
    return tex;
}

// --- Rendering ---

void UI::drawRect(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

// Sprite inscritto nel box mantenendo le proporzioni (niente schiacciati).
void UI::drawSpriteFit(int x, int y, int w, int h, SDL_Texture* tex) {
    if (!tex) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    int dw = w, dh = h;
    if (tw > 0 && th > 0) {
        float s = std::min((float)w / tw, (float)h / th);
        dw = (int)(tw * s);
        dh = (int)(th * s);
    }
    SDL_Rect dst = {x + (w - dw) / 2, y + (h - dh) / 2, dw, dh};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

// Base rotonda stile card giochi (stessi colori T(): panelBg sempre,
// menuHighlight + anello cursor quando focused) — cosi i pulsanti tondi
// seguono tutti i temi come le card.
void UI::drawRoundSelect(int cx, int cy, int r, bool focused) {
    auto disc = [&](int rr, SDL_Color c) {
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        for (int dy = -rr; dy <= rr; dy++) {
            int dx = static_cast<int>(std::sqrt((double)(rr * rr - dy * dy)));
            SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    };
    if (focused) {
        disc(r + 6, T().menuHighlight);
        disc(r + 3, T().cursor);
    }
    disc(r, T().panelBg);
}

void UI::drawRectOutline(int x, int y, int w, int h, SDL_Color color, int thickness) {
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    for (int t = 0; t < thickness; t++) {
        SDL_Rect r = {x + t, y + t, w - 2*t, h - 2*t};
        SDL_RenderDrawRect(renderer_, &r);
    }
}

// Rettangolo arrotondato pieno (stile card giochi): corpo + 4 dischi angolo.
void UI::drawRoundRect(int x, int y, int w, int h, int r, SDL_Color color) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_Rect hbar = {x + r, y, w - 2 * r, h};
    SDL_RenderFillRect(renderer_, &hbar);
    SDL_Rect vbar = {x, y + r, w, h - 2 * r};
    SDL_RenderFillRect(renderer_, &vbar);
    for (int dy = -r; dy <= r; dy++) {
        int dx = static_cast<int>(std::sqrt((double)(r * r - dy * dy)));
        SDL_RenderDrawLine(renderer_, x + r - dx, y + r + dy, x + r + dx, y + r + dy);
        SDL_RenderDrawLine(renderer_, x + w - r - dx, y + r + dy, x + w - r + dx, y + r + dy);
        SDL_RenderDrawLine(renderer_, x + r - dx, y + h - r + dy, x + r + dx, y + h - r + dy);
        SDL_RenderDrawLine(renderer_, x + w - r - dx, y + h - r + dy, x + w - r + dx, y + h - r + dy);
    }
}

// Come drawRoundRect() ma con un gradiente orizzontale invece di un colore
// piatto: per ogni colonna calcola l'estensione verticale piena (0..h-1 nel
// corpo, ristretta dal cerchio negli angoli, stessa geometria esatta di
// drawRoundRect) e la disegna con il colore interpolato per quella colonna.
// Un solo giro, un solo draw call per colonna, sempre opaco.
void UI::drawRoundRectGradientH(int x, int y, int w, int h, int r, SDL_Color left, SDL_Color right) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int cx = 0; cx < w; cx++) {
        int insetX = std::min(cx, w - 1 - cx);
        int yStart, yEnd;
        if (insetX >= r) {
            yStart = 0;
            yEnd = h - 1;
        } else {
            int dxFromCenter = r - insetX;
            int dyTrim = r - static_cast<int>(std::sqrt((double)(r * r - dxFromCenter * dxFromCenter)));
            yStart = dyTrim;
            yEnd = h - 1 - dyTrim;
        }
        if (yStart > yEnd) continue;
        float t = (w > 1) ? (float)cx / (float)(w - 1) : 0.0f;
        Uint8 cr = static_cast<Uint8>(left.r + (right.r - left.r) * t);
        Uint8 cg = static_cast<Uint8>(left.g + (right.g - left.g) * t);
        Uint8 cb = static_cast<Uint8>(left.b + (right.b - left.b) * t);
        SDL_SetRenderDrawColor(renderer_, cr, cg, cb, 255);
        SDL_RenderDrawLine(renderer_, x + cx, y + yStart, x + cx, y + yEnd);
    }
}

// Bordo arrotondato: spigoli + 4 archi come polilinee (matematica esatta).
void UI::drawRoundRectOutline(int x, int y, int w, int h, int r, SDL_Color color, int thickness) {
    if (r < 0) r = 0;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    auto arc = [&](int cx, int cy, double a0, double a1, int rr) {
        const int STEPS = 12;
        int px = 0, py = 0;
        for (int i = 0; i <= STEPS; i++) {
            double a = (a0 + (a1 - a0) * i / STEPS) * 3.14159265 / 180.0;
            int qx = cx + static_cast<int>(rr * std::cos(a));
            int qy = cy + static_cast<int>(rr * std::sin(a));
            if (i > 0) SDL_RenderDrawLine(renderer_, px, py, qx, qy);
            px = qx;
            py = qy;
        }
    };
    for (int t = 0; t < thickness; t++) {
        int rr = r - t;
        if (rr < 0) rr = 0;
        int xx = x + t, yy = y + t, ww = w - 2 * t, hh = h - 2 * t;
        if (ww <= 0 || hh <= 0) break;
        SDL_RenderDrawLine(renderer_, xx + rr, yy, xx + ww - rr, yy);
        SDL_RenderDrawLine(renderer_, xx + rr, yy + hh, xx + ww - rr, yy + hh);
        SDL_RenderDrawLine(renderer_, xx, yy + rr, xx, yy + hh - rr);
        SDL_RenderDrawLine(renderer_, xx + ww, yy + rr, xx + ww, yy + hh - rr);
        arc(xx + rr, yy + rr, 180.0, 270.0, rr);
        arc(xx + ww - rr, yy + rr, 270.0, 360.0, rr);
        arc(xx + rr, yy + hh - rr, 90.0, 180.0, rr);
        arc(xx + ww - rr, yy + hh - rr, 0.0, 90.0, rr);
    }
}

// Come drawRoundRectOutline() ma tratteggiata: costruisce il perimetro
// (4 lati + 4 archi, stessa geometria/angoli di drawRoundRectOutline) come
// un'unica polilinea continua in senso orario, poi cammina lungo la
// lunghezza cumulata alternando dashLen px accesi / gapLen px spenti --
// cosi' il tratteggio non riparte da zero a ogni lato o angolo.
void UI::drawRoundRectOutlineDashed(int x, int y, int w, int h, int r, SDL_Color color,
                                     int thickness, int dashLen, int gapLen) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (dashLen < 1) dashLen = 1;
    if (gapLen < 0) gapLen = 0;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    for (int t = 0; t < thickness; t++) {
        int rr = r - t;
        if (rr < 0) rr = 0;
        int xx = x + t, yy = y + t, ww = w - 2 * t, hh = h - 2 * t;
        if (ww <= 0 || hh <= 0) break;

        // Perimetro come polilinea densa (corda ~3px sia sui lati dritti
        // che sugli archi): senza questo i lati dritti finivano per essere
        // un solo segmento lunghissimo (tutto acceso o tutto spento, niente
        // tratteggio reale) e gli angoli, con gli archi comunque suddivisi
        // fini, sembravano "sporchi" al confronto -- da qui sia il "non e'
        // tratteggiato" sia il "pixelloso negli angoli".
        std::vector<SDL_Point> pts;
        auto addLine = [&](int x0, int y0, int x1, int y1) {
            float len = std::sqrt((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
            int steps = std::max(1, (int)(len / 3.0f));
            for (int i = 0; i <= steps; i++) {
                float f = (float)i / steps;
                pts.push_back({x0 + (int)((x1 - x0) * f), y0 + (int)((y1 - y0) * f)});
            }
        };
        auto addArc = [&](int cx, int cy, double a0, double a1) {
            int steps = std::max(4, (int)(std::fabs(a1 - a0) * 3.14159265 / 180.0 * rr / 3.0));
            for (int i = 0; i <= steps; i++) {
                double a = (a0 + (a1 - a0) * i / steps) * 3.14159265 / 180.0;
                pts.push_back({cx + static_cast<int>(rr * std::cos(a)),
                               cy + static_cast<int>(rr * std::sin(a))});
            }
        };
        addLine(xx + rr, yy, xx + ww - rr, yy);
        addArc(xx + ww - rr, yy + rr, -90.0, 0.0);
        addLine(xx + ww, yy + rr, xx + ww, yy + hh - rr);
        addArc(xx + ww - rr, yy + hh - rr, 0.0, 90.0);
        addLine(xx + ww - rr, yy + hh, xx + rr, yy + hh);
        addArc(xx + rr, yy + hh - rr, 90.0, 180.0);
        addLine(xx, yy + hh - rr, xx, yy + rr);
        addArc(xx + rr, yy + rr, 180.0, 270.0);
        // L'ultimo addArc rientra esattamente sul primo punto pushato
        // (xx + rr, yy): il perimetro si chiude da solo, niente punto extra.

        float dist = 0.0f;
        float cycle = (float)(dashLen + gapLen);
        for (size_t i = 1; i < pts.size(); i++) {
            float segLen = std::sqrt((float)((pts[i].x - pts[i-1].x) * (pts[i].x - pts[i-1].x) +
                                              (pts[i].y - pts[i-1].y) * (pts[i].y - pts[i-1].y)));
            if (std::fmod(dist, cycle) < (float)dashLen)
                SDL_RenderDrawLine(renderer_, pts[i-1].x, pts[i-1].y, pts[i].x, pts[i].y);
            dist += segLen;
        }
    }
}

// Copia con angoli arrotondati (raggio proporzionale): per le texture delle card.
SDL_Surface* roundCornersSurface(SDL_Surface* src, int radius) {
    if (!src) return nullptr;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!rgba) return nullptr;
    int w = rgba->w, h = rgba->h;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    SDL_LockSurface(rgba);
    Uint32* px = (Uint32*)rgba->pixels;
    int stride = rgba->pitch / 4;
    auto inside = [&](int x, int y) {
        // dentro se non nei quadrati d'angolo fuori dal quarto di cerchio
        int cx = x < radius ? radius - x : (x >= w - radius ? x - (w - radius) : -1);
        int cy = y < radius ? radius - y : (y >= h - radius ? y - (h - radius) : -1);
        if (cx < 0 || cy < 0) return true;
        return cx * cx + cy * cy <= radius * radius;
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (!inside(x, y)) px[y * stride + x] &= 0x00FFFFFF;
    SDL_UnlockSurface(rgba);
    return rgba;
}

static uint32_t packColor(SDL_Color c) {
    return (uint32_t(c.r) << 24) | (uint32_t(c.g) << 16) | (uint32_t(c.b) << 8) | c.a;
}

const UI::TextCacheEntry& UI::getTextEntry(const std::string& text, TTF_Font* f, SDL_Color color) {
    TextCacheKey key{text, f, packColor(color)};
    auto it = textCache_.find(key);
    if (it != textCache_.end())
        return it->second;

    // Cap cache size to limit GPU memory on Switch
    if (textCache_.size() >= 512)
        clearTextCache();

    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text.c_str(), color);
    TextCacheEntry entry{};
    if (surf) {
        entry.tex = SDL_CreateTextureFromSurface(renderer_, surf);
        entry.w = surf->w;
        entry.h = surf->h;
        SDL_FreeSurface(surf);
    }
    return textCache_.emplace(std::move(key), entry).first->second;
}

void UI::clearTextCache() {
    for (auto& [k, e] : textCache_) {
        if (e.tex)
            SDL_DestroyTexture(e.tex);
    }
    textCache_.clear();
}

void UI::drawText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* f) {
    if (!f || text.empty()) return;
    const auto& entry = getTextEntry(text, f, color);
    if (!entry.tex) return;
    SDL_Rect dst = {x, y, entry.w, entry.h};
    SDL_RenderCopy(renderer_, entry.tex, nullptr, &dst);
}

void UI::drawTextCentered(const std::string& text, int cx, int cy, SDL_Color color, TTF_Font* f) {
    if (!f || text.empty()) return;
    const auto& entry = getTextEntry(text, f, color);
    if (!entry.tex) return;
    SDL_Rect dst = {cx - entry.w/2, cy - entry.h/2, entry.w, entry.h};
    SDL_RenderCopy(renderer_, entry.tex, nullptr, &dst);
}

void UI::drawStatusBar(const std::string& msg) {
    drawRect(0, SCREEN_H - 35, SCREEN_W, 35, T().statusBarBg);
    drawText(msg, 15, SCREEN_H - 26, T().statusText, fontSmall_);
}

const std::vector<UI::SlotDisplay>& UI::getSlotDisplays(Panel panel, int box) {
    BoxDisplayKey key{panel, box};
    auto it = slotDisplayCache_.find(key);
    if (it != slotDisplayCache_.end())
        return it->second;

    // Cap cache size
    if (slotDisplayCache_.size() >= 8)
        slotDisplayCache_.clear();

    int slots = maxSlotsFor(panel);
    std::vector<SlotDisplay> displays(slots);
    for (int s = 0; s < slots; s++) {
        Pokemon pkm = getPokemonAt(box, s, panel);
        auto& sd = displays[s];
        if (pkm.isEmpty()) {
            sd.empty = true;
            continue;
        }
        sd.empty   = false;
        sd.egg     = pkm.isEgg();
        sd.shiny   = pkm.isShiny();
        sd.alpha   = pkm.isAlpha();
        sd.gender  = pkm.gender();
        sd.species = pkm.species();
        sd.form    = pkm.form();
        sd.level   = pkm.level();
        sd.ball    = pkm.ball();
        sd.name    = pkm.displayName();
#ifdef OH_LINUX
        // Celle 4:3 da 47px: nomi troncati corti (display-only, dati intatti).
        if (sd.name.length() > 6)
            sd.name = sd.name.substr(0, 5) + ".";
#else
        if (sd.name.length() > 10)
            sd.name = sd.name.substr(0, 9) + ".";
#endif
    }
    return slotDisplayCache_.emplace(key, std::move(displays)).first->second;
}

void UI::invalidateSlotDisplay(Panel panel, int box) {
    slotDisplayCache_.erase(BoxDisplayKey{panel, box});
}

void UI::drawSlot(int x, int y, const SlotDisplay& sd, bool isCursor, int selectOrder,
                  int highlightState, bool isParty) {
    SDL_Color bgColor;
    if (sd.empty) {
        bgColor = T().slotEmpty;
    } else if (sd.egg) {
        bgColor = T().slotEgg;
    } else {
        bgColor = T().slotFull;
    }

    // Slot background
    drawRect(x, y, CELL_W, CELL_H, bgColor);

    // LGPE party member outline
    if (isParty)
        drawRectOutline(x + 1, y + 1, CELL_W - 2, CELL_H - 2, T().partyMark, 2);

    // Search match outline (drawn early so it frames the slot content)
    if (highlightState == 1)
        drawRectOutline(x + 1, y + 1, CELL_W - 2, CELL_H - 2, T().searchMatch, 2);

    // Selection outline (drawn before cursor so cursor overlays it)
    if (selectOrder > 0) {
        SDL_Color selColor = positionPreserve_ ? T().selectedPos : T().selected;
        drawRectOutline(x + 1, y + 1, CELL_W - 2, CELL_H - 2, selColor, 2);
    }

    // Cursor highlight
    if (isCursor)
        drawRectOutline(x, y, CELL_W, CELL_H, T().cursor, 3);

    if (!sd.empty) {
        // Draw sprite centered in top portion of cell (form-aware, shiny variant if available)
        SDL_Texture* sprite = nullptr;
        if (sd.egg) {
            sprite = getSprite(0);
        } else if (sd.shiny) {
            sprite = getShinySprite(sd.species, sd.form);
            if (!sprite) sprite = getSprite(sd.species, sd.form);
        } else {
            sprite = getSprite(sd.species, sd.form);
        }

        if (sprite) {
            int texW, texH;
            SDL_QueryTexture(sprite, nullptr, nullptr, &texW, &texH);

            int dstW, dstH;
            if (texW > 0 && texH > 0) {
                float scale = std::min(static_cast<float>(SPRITE_SIZE) / texW,
                                       static_cast<float>(SPRITE_SIZE) / texH);
                dstW = static_cast<int>(texW * scale);
                dstH = static_cast<int>(texH * scale);
            } else {
                dstW = SPRITE_SIZE;
                dstH = SPRITE_SIZE;
            }

            int sprX = x + (CELL_W - dstW) / 2;
            int sprY = y + 4 + (SPRITE_SIZE - dstH) / 2;
            SDL_Rect dst = {sprX, sprY, dstW, dstH};
            SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
        }

        // Species name below sprite — normalize all-caps Gen3 nicknames to Title Case
        std::string dispName = sd.name;
        bool allCaps = !dispName.empty();
        for (char c : dispName) if (c >= 'a' && c <= 'z') { allCaps = false; break; }
        if (allCaps && dispName.size() > 1) {
            for (size_t i = 1; i < dispName.size(); ++i) dispName[i] = (char)tolower((unsigned char)dispName[i]);
        }
        SDL_Color nameColor = sd.shiny ? T().shiny : T().text;
        drawTextCentered(dispName, x + CELL_W / 2, y + SPRITE_SIZE + 10, nameColor, fontSmall_);

        // Level at the bottom
        if (!sd.egg) {
            std::string lvlStr = i18n::get(StrKey::LvPrefix) + std::to_string(sd.level);
            drawTextCentered(lvlStr, x + CELL_W / 2, y + CELL_H - 12, T().textDim, fontSmall_);
        }

        // Gender indicator (top-right corner)
        if (sd.gender == 0)
            drawText("\xe2\x99\x82", x + CELL_W - 16, y + 2, T().genderMale, fontSmall_);
        else if (sd.gender == 1)
            drawText("\xe2\x99\x80", x + CELL_W - 16, y + 2, T().genderFemale, fontSmall_);

        // Shiny / Alpha icon (top-left corner)
        SDL_Texture* statusIcon = nullptr;
        if (sd.shiny && sd.alpha)
            statusIcon = iconShinyAlpha_;
        else if (sd.shiny)
            statusIcon = iconShiny_;
        else if (sd.alpha)
            statusIcon = iconAlpha_;

        if (statusIcon) {
            SDL_Rect iconDst = {x + 2, y + 2, 14, 14};
            SDL_RenderCopy(renderer_, statusIcon, nullptr, &iconDst);
        }

        // Party member marker (bottom-right corner): the mon's own ball, so a
        // party mon sitting in its box cell is recognizable at a glance.
        if (isParty) {
            SDL_Texture* ballTex = getBallSprite(sd.ball ? sd.ball : 4);
            if (ballTex) {
                SDL_Rect ballDst = {x + CELL_W - 20, y + CELL_H - 20, 16, 16};
                SDL_RenderCopy(renderer_, ballTex, nullptr, &ballDst);
            }
        }
    }

    // Numbered badge on top of everything
    if (selectOrder > 0) {
        std::string num = std::to_string(selectOrder);
        auto& numEntry = getTextEntry(num, font_, positionPreserve_ ? T().selectedPos : T().selected);
        int tw = numEntry.w, th = numEntry.h;
        int badgeR = std::max(tw, th) / 2 + 6;
        int cx = x + CELL_W / 2;
        int cy = y + CELL_H / 2;

        SDL_Color badgeColor = positionPreserve_ ? T().selectedPos : T().selected;
        SDL_SetRenderDrawColor(renderer_, badgeColor.r, badgeColor.g, badgeColor.b, 220);
        for (int dy2 = -badgeR; dy2 <= badgeR; dy2++) {
            int dx2 = static_cast<int>(std::sqrt(badgeR * badgeR - dy2 * dy2));
            SDL_RenderDrawLine(renderer_, cx - dx2, cy + dy2, cx + dx2, cy + dy2);
        }
        drawTextCentered(num, cx, cy, T().textOnBadge, font_);
    }

    // Search dim overlay (drawn last so it covers everything)
    if (highlightState == -1)
        drawRect(x, y, CELL_W, CELL_H, T().searchDim);
}

void UI::drawPanel(int panelX, const std::string& boxName, int boxIdx,
                   int totalBoxes, bool isActive, SaveFile* save, Bank* bank, int box,
                   Panel panelId) {
    // Panel background
    drawRect(panelX, 0, PANEL_W, SCREEN_H - 35, T().panelBg);

    // Box name header with arrows
    SDL_Color hdrColor = isActive ? T().boxName : T().textDim;
    // DS identity strip: OT + party minis in the header row (no grid change).
    // Only for saves that carry it (dsOtName_); every other game keeps the
    // classic centered header.
    bool showDsStrip = save && !save->dsOtName().empty();
    if (showDsStrip) {
        drawTextCentered("<", panelX + 20, BOX_HDR_Y + BOX_HDR_H / 2, T().arrow, font_);
        std::string left = boxName + " (" + std::to_string(boxIdx + 1) + "/" + std::to_string(totalBoxes) + ")";
        left += " · OT " + save->dsOtName();
        int tw = getTextEntry(left, fontSmall_, hdrColor).w;
        // Never collide with the minis: truncate the text first.
        int maxLeftW = PANEL_W - 90 - 6 * 28;
        while (left.size() > 5 && tw > maxLeftW) {
            left = left.substr(0, left.size() - 5) + "(..)";
            tw = getTextEntry(left, fontSmall_, hdrColor).w;
        }
        drawText(left, panelX + 45, BOX_HDR_Y + (BOX_HDR_H - 14) / 2, hdrColor, fontSmall_);
        const auto& party = save->dsParty();
        // Party boxes always visible (6 slots) even when empty — serve da target per drop quando party è vuoto
        {
            int mx = panelX + 45 + tw + 12;
            SDL_Texture* emptyFallback = iconBoxEmpty_;
            for (int pi = 0; pi < 6; pi++) {
                bool isEmpty = (pi >= (int)party.size() || party[pi].isEmpty());
                SDL_Texture* tex = nullptr;
                if (!isEmpty) {
                    // Le uova mostrano il guscio, non la specie interna: senza
                    // questo check un uovo sembrava il mon che contiene (bug
                    // "Ditto diventa uovo" — era un uovo vero fin dall'inizio).
                    if (party[pi].isEgg())
                        tex = getSprite(0);
                    if (!tex) tex = getSprite(party[pi].species(), party[pi].form());
                    if (!tex) tex = getSprite(party[pi].species(), 0);
                    if (!tex) tex = getBallSprite(party[pi].ball());
                    if (!tex) tex = getBallSprite(4);
                } else {
                    tex = getBallSprite(4);
                    if (!tex) tex = emptyFallback;
                }
                if (tex && mx + 24 <= panelX + PANEL_W - 20) {
                    SDL_Rect cell = {mx, BOX_HDR_Y + (BOX_HDR_H - 24) / 2, 24, 24};
                    if (isEmpty) {
                        SDL_SetTextureColorMod(tex, 110, 110, 110);
                        SDL_SetTextureAlphaMod(tex, 110);
                    }
                    drawSpriteFit(cell.x, cell.y, cell.w, cell.h, tex);
                    if (isEmpty) {
                        SDL_SetTextureColorMod(tex, 255, 255, 255);
                        SDL_SetTextureAlphaMod(tex, 255);
                        SDL_SetRenderDrawColor(renderer_, 110, 110, 110, 90);
                        SDL_RenderDrawRect(renderer_, &cell);
                    }
                    if (pi == partyCursor_) {
                        SDL_SetRenderDrawColor(renderer_, T().cursor.r, T().cursor.g, T().cursor.b, 255);
                        SDL_RenderDrawRect(renderer_, &cell);
                    }
                } else if (!tex) {
                    SDL_SetRenderDrawColor(renderer_, T().textDim.r, T().textDim.g, T().textDim.b, 80);
                    SDL_Rect r = {mx, BOX_HDR_Y + (BOX_HDR_H - 10) / 2, 24, 10};
                    SDL_RenderFillRect(renderer_, &r);
                }
                mx += 28;
            }
        }
        drawTextCentered(">", panelX + PANEL_W - 20, BOX_HDR_Y + BOX_HDR_H / 2, T().arrow, font_);
    } else {
    drawTextCentered("<", panelX + 20, BOX_HDR_Y + BOX_HDR_H / 2, T().arrow, font_);
    std::string hdrText = boxName + " (" + std::to_string(boxIdx + 1) + "/" + std::to_string(totalBoxes) + ")";
    // Truncate if too wide for panel (leave room for arrows)
    int maxHdrW = PANEL_W - 80;
    int tw = getTextEntry(hdrText, font_, hdrColor).w;
    if (tw > maxHdrW) {
        while (hdrText.size() > 5 && tw > maxHdrW) {
            hdrText = hdrText.substr(0, hdrText.size() - 5) + "(..)";
            tw = getTextEntry(hdrText, font_, hdrColor).w;
        }
    }
    drawTextCentered(hdrText, panelX + PANEL_W / 2, BOX_HDR_Y + BOX_HDR_H / 2, hdrColor, font_);
    drawTextCentered(">", panelX + PANEL_W - 20, BOX_HDR_Y + BOX_HDR_H / 2, T().arrow, font_);
    } // else (classic header)

    // Grid: dynamic columns x dynamic rows (5x4 for 20-slot GB/GBC boxes),
    // sized from THIS panel's own source.
    int cols = gridColsFor(panelId);
    int rows = gridRowsFor(panelId);
    int gridStartX = panelX + (PANEL_W - (cols * (CELL_W + CELL_PAD) - CELL_PAD)) / 2;
    int gridStartY = GRID_Y;

    const auto& displays = getSlotDisplays(panelId, box);

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int slot = row * cols + col;
            int cellX = gridStartX + col * (CELL_W + CELL_PAD);
            int cellY = gridStartY + row * (CELL_H + CELL_PAD);

            const auto& sd = displays[slot];

            bool isCursor = isActive && cursor_.col == col && cursor_.row == row && partyCursor_ < 0;
            int selOrder = 0;
            if (!selectedSlots_.empty()
                && panelId == selectedPanel_ && box == selectedBox_) {
                for (int i = 0; i < (int)selectedSlots_.size(); i++) {
                    if (selectedSlots_[i] == slot) {
                        selOrder = i + 1;
                        break;
                    }
                }
            }
            int hlState = 0;
            if (searchHighlightActive_ && !sd.empty)
                hlState = isSearchMatch(panelId, box, slot) ? 1 : -1;
            bool partySlot = save && save->isLGPEPartySlot(box, slot);
            drawSlot(cellX, cellY, sd, isCursor, selOrder, hlState, partySlot);
        }
    }
}

void UI::drawFrame() {
    // Clear screen
    SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
    SDL_RenderClear(renderer_);

    // Sync cursor box to the active panel
    if (cursor_.panel == Panel::Game)
        gameBox_ = cursor_.box;
    else
        bankBox_ = cursor_.box;

    auto truncName = [](const std::string& s, size_t max) -> std::string {
        if (s.size() <= max) return s;
        return s.substr(0, max - 3) + "(..)";

    };

    if (isDualBankMode()) {
        // Dual-bank mode: two bank panels side by side
        bool leftActive = (cursor_.panel == Panel::Game);
        bool rightActive = (cursor_.panel == Panel::Bank);

        // Left panel: left bank
        std::string leftBoxName;
        if (!leftBankName_.empty())
            leftBoxName = truncName(leftBankName_, 16) + " - " + bankLeft_.getBoxName(gameBox_);
        else
            leftBoxName = i18n::get(StrKey::NoBankLoaded);
        drawPanel(PANEL_X_L, leftBoxName, gameBox_,
                  leftBankName_.empty() ? 1 : bankLeft_.boxCount(),
                  leftActive, nullptr,
                  leftBankName_.empty() ? nullptr : &bankLeft_,
                  gameBox_, Panel::Game);

        // Right panel: right bank
        std::string rightBoxName = truncName(activeBankName_, 16) + " - " + bank_.getBoxName(bankBox_);
        drawPanel(PANEL_X_R, rightBoxName, bankBox_, bank_.boxCount(),
                  rightActive, nullptr, &bank_, bankBox_, Panel::Bank);
    } else {
        // Normal mode: save + bank
        bool leftActive = (cursor_.panel == Panel::Game);
        std::string gameBoxName = save_.getBoxName(gameBox_);
        drawPanel(PANEL_X_L, gameBoxName, gameBox_, save_.boxCount(),
                  leftActive, &save_, nullptr, gameBox_, Panel::Game);

        bool rightActive = (cursor_.panel == Panel::Bank);
        std::string bankBoxName = truncName(activeBankName_, 16) + " - " + bank_.getBoxName(bankBox_);
        drawPanel(PANEL_X_R, bankBoxName, bankBox_, bank_.boxCount(),
                  rightActive, nullptr, &bank_, bankBox_, Panel::Bank);
    }

    // Status bar
    std::string statusMsg = i18n::get(StrKey::StatusMain);
    if (searchHighlightActive_ && !holding_ && selectedSlots_.empty() && !yHeld_) {
        statusMsg = i18n::fmt(StrKey::StatusSearch, std::to_string(searchResults_.size()));
    } else if (holding_ && !heldMulti_.empty()) {
        statusMsg = i18n::fmt(StrKey::StatusHoldingMulti, std::to_string(heldMulti_.size()),
                    positionPreserve_ ? i18n::get(StrKey::KeepPositions) : "");
    } else if (holding_) {
        std::string heldName = heldPkm_.displayName();
        if (!heldName.empty()) {
            statusMsg = i18n::fmt(StrKey::StatusHoldingSingle, heldName, std::to_string(heldPkm_.level()));
        }
    } else if (yHeld_ && yDragActive_) {
        statusMsg = i18n::fmt(StrKey::StatusDrag, std::to_string(selectedSlots_.size()));
    } else if (!selectedSlots_.empty()) {
        statusMsg = i18n::fmt(StrKey::StatusSelected, std::to_string(selectedSlots_.size()),
                    positionPreserve_ ? i18n::get(StrKey::KeepPositions) : "");
    }
    drawStatusBar(statusMsg);

    // Profile | Game name | Core (bottom right, gold)
    {
        std::string label;
        if (allBanksMode_)
            label = i18n::get(StrKey::LabelAllBanks);
        else if (isDualBankMode())
            label = i18n::get(StrKey::LabelDualBank);
        else if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
            label = account_.profiles()[selectedProfile_].nickname + " | ";
        label += gameDisplayNameOf(selectedGame_);
        label += " | ";
        label += useOpenHome() ? "OH" : "PK";
        if (DebugLog::enabled())
            label += " | DBG";
        label += std::string(" | ") + updateNetLinkStr();
        const auto& entry = getTextEntry(label, fontSmall_, T().goldLabel);
        if (entry.tex)
            drawText(label, SCREEN_W - entry.w - 15, SCREEN_H - 26, T().goldLabel, fontSmall_);
    }

    // Held Pokemon overlay (draw on top of panels, under popups)
    if (holding_)
        drawHeldOverlay();

    // Detail popup overlay
    if (showDetail_) {
        if (detailParty_ >= 0 && (size_t)detailParty_ < save_.dsParty().size()) {
            // Debug OT-strip focus: read-only party detail (no release/export).
            drawDetailPopup(save_.dsParty()[(size_t)detailParty_]);
        } else {
            Pokemon pkm = getPokemonAt(cursor_.box, cursor_.slot(gridCols()), cursor_.panel);
            if (pkm.isEmpty()) {
                showDetail_ = false;
            } else {
                drawDetailPopup(pkm);
            }
        }
    }

    // Menu popup overlay
    if (showMenu_) {
        drawMenuPopup();
    }

    // Box view overlay
    if (showBoxView_) {
        drawBoxViewOverlay();
    }

    // Search popups
    if (showSearchFilter_) {
        drawSearchFilterPopup();
    }
    if (showSearchResults_) {
        drawSearchResultsPopup();
    }

    // Species picker overlays (drawn on top of search filter)
    if (showSpeciesLetterPicker_) {
        drawSpeciesLetterPicker();
    }
    if (showSpeciesListPicker_) {
        drawSpeciesListPicker();
    }

    // Wondercard list popup
    if (showWondercardList_) {
        drawWondercardListPopup();
    }

    // PK file import list popup
    if (showPkImportList_) {
        drawPkImportListPopup();
    }

    // Self-trade party picker popup (Fase 1: Gen3)
    if (showTradeList_) {
        drawTradeListPopup();
    }

    // Debug test-mon generator list popup
    if (showGenMonList_) {
        drawGenMonListPopup();
    }

    // Learnset viewer popup
    if (showLearnset_) {
        drawLearnsetPopup();
    }
}

// --- Polygon rendering helpers for radar charts ---
// Unit vectors for a regular hexagon (top, top-right, bottom-right, bottom, bottom-left, top-left)
static constexpr double HEX_COS[6] = { 0.0,  0.866025,  0.866025, 0.0, -0.866025, -0.866025 };
static constexpr double HEX_SIN[6] = {-1.0, -0.5,       0.5,      1.0,  0.5,      -0.5      };

namespace {

void fillConvexPolygon(SDL_Renderer* renderer, const SDL_Point pts[], int count, SDL_Color color) {
    if (count < 3) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    int minY = pts[0].y, maxY = pts[0].y;
    for (int i = 1; i < count; i++) {
        if (pts[i].y < minY) minY = pts[i].y;
        if (pts[i].y > maxY) maxY = pts[i].y;
    }

    for (int y = minY; y <= maxY; y++) {
        int minX = 99999, maxX = -99999;
        for (int i = 0; i < count; i++) {
            int j = (i + 1) % count;
            int y0 = pts[i].y, y1 = pts[j].y;
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                int x = pts[i].x + (int)((long long)(y - y0) * (pts[j].x - pts[i].x) / (y1 - y0));
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
            }
        }
        if (minX <= maxX)
            SDL_RenderDrawLine(renderer, minX, y, maxX, y);
    }
}

void drawPolygonOutline(SDL_Renderer* renderer, const SDL_Point pts[], int count, SDL_Color color) {
    if (count < 2) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        SDL_RenderDrawLine(renderer, pts[i].x, pts[i].y, pts[j].x, pts[j].y);
    }
}

} // anonymous namespace

void UI::drawRadarChart(int cx, int cy, int radius, const int values[6], int maxVal) {
    const std::string labels[6] = {i18n::get(StrKey::StatHP), i18n::get(StrKey::StatAtk),
        i18n::get(StrKey::StatDef), i18n::get(StrKey::StatSpe), i18n::get(StrKey::StatSpD), i18n::get(StrKey::StatSpA)};
    constexpr int N = 6;
    constexpr int LABEL_MARGIN = 12;

    // Compute hex vertices (starting from top, clockwise)
    SDL_Point outer[N];
    for (int i = 0; i < N; i++) {
        outer[i].x = cx + static_cast<int>(radius * HEX_COS[i]);
        outer[i].y = cy + static_cast<int>(radius * HEX_SIN[i]);
    }

    // Guide lines from center to each vertex
    SDL_Color guide = {T().textDim.r, T().textDim.g, T().textDim.b, 50};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, guide.r, guide.g, guide.b, guide.a);
    for (int i = 0; i < N; i++)
        SDL_RenderDrawLine(renderer_, cx, cy, outer[i].x, outer[i].y);

    // Intermediate ring at 50%
    SDL_Point mid[N];
    for (int i = 0; i < N; i++) {
        mid[i].x = cx + static_cast<int>(radius * 0.5 * HEX_COS[i]);
        mid[i].y = cy + static_cast<int>(radius * 0.5 * HEX_SIN[i]);
    }
    drawPolygonOutline(renderer_, mid, N, guide);

    // Outer hex border
    drawPolygonOutline(renderer_, outer, N, T().textDim);

    // Compute data polygon vertices
    SDL_Point data[N];
    for (int i = 0; i < N; i++) {
        double frac = maxVal > 0 ? std::min(1.0, static_cast<double>(values[i]) / maxVal) : 0.0;
        double r = radius * frac;
        data[i].x = cx + static_cast<int>(r * HEX_COS[i]);
        data[i].y = cy + static_cast<int>(r * HEX_SIN[i]);
    }

    // Fill data polygon using triangle fan from center (handles concave shapes)
    SDL_Color fill = {T().cursor.r, T().cursor.g, T().cursor.b, 60};
    for (int i = 0; i < N; i++) {
        int j = (i + 1) % N;
        SDL_Point tri[3] = {{cx, cy}, data[i], data[j]};
        fillConvexPolygon(renderer_, tri, 3, fill);
    }

    // Data polygon outline
    SDL_Color outline = {T().cursor.r, T().cursor.g, T().cursor.b, 200};
    drawPolygonOutline(renderer_, data, N, outline);

    // Small dots at each data vertex
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, outline.r, outline.g, outline.b, outline.a);
    for (int i = 0; i < N; i++) {
        SDL_Rect dot = {data[i].x - 2, data[i].y - 2, 5, 5};
        SDL_RenderFillRect(renderer_, &dot);
    }

    // Labels and values around the chart
    for (int i = 0; i < N; i++) {
        int lx = cx + static_cast<int>((radius + LABEL_MARGIN) * HEX_COS[i]);
        int ly = cy + static_cast<int>((radius + LABEL_MARGIN) * HEX_SIN[i]);

        const std::string& name = labels[i];
        std::string valStr = std::to_string(values[i]);

        SDL_Color nameColor = T().goldLabel;
        SDL_Color valColor = (values[i] >= maxVal) ? T().shiny
                           : (values[i] == 0)      ? T().textDim
                           :                          T().text;

        auto& ne = getTextEntry(name, fontSmall_, nameColor);
        int nw = ne.w, nh = ne.h;
        auto& ve = getTextEntry(valStr, fontSmall_, valColor);
        int vw = ve.w, vh = ve.h;

        if (i == 0) { // Top: centered, name then value downward
            drawText(name, lx - nw / 2, ly - nh * 2 - 2, nameColor, fontSmall_);
            drawText(valStr, lx - vw / 2, ly - vh, valColor, fontSmall_);
        } else if (i == 3) { // Bottom: centered, value then name downward
            drawText(valStr, lx - vw / 2, ly, valColor, fontSmall_);
            drawText(name, lx - nw / 2, ly + vh + 2, nameColor, fontSmall_);
        } else if (i == 1 || i == 2) { // Right: left-aligned
            drawText(name, lx + 4, ly - nh, nameColor, fontSmall_);
            drawText(valStr, lx + 4, ly + 2, valColor, fontSmall_);
        } else { // Left (4, 5): right-aligned
            drawText(name, lx - nw - 4, ly - nh, nameColor, fontSmall_);
            drawText(valStr, lx - vw - 4, ly + 2, valColor, fontSmall_);
        }
    }
}

void UI::drawDetailPopup(const Pokemon& pkm) {
    // Semi-transparent dark overlay
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    // Popup rect centered. Grow by one line when the optional HT row is shown
    // so the Moves/Ribbons region keeps the same layout as the no-HT case.
#ifdef OH_LINUX
    // 4:3 nativo (620x440): stessa struttura, misure ridotte.
    constexpr int POP_W = 620;
    const int POP_H = 440 + (pkm.hasHandlingTrainer() ? 28 : 0);
    constexpr int LARGE_SPRITE = 96;
    constexpr int MOVE_COL_W = 170;
#else
    constexpr int POP_W = 900;
    const int POP_H = 550 + (pkm.hasHandlingTrainer() ? 28 : 0);
    constexpr int LARGE_SPRITE = 128;
    constexpr int MOVE_COL_W = 230;
#endif
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    // Large sprite top-left (128 Switch, 96 su 4:3: vedi sopra)
    int sprX = popX + 20;
    int sprY = popY + 20;

    SDL_Texture* sprite = nullptr;
    uint8_t pkmForm = pkm.form();
    if (pkm.isEgg()) {
        sprite = getSprite(0);
    } else if (pkm.isShiny()) {
        sprite = getShinySprite(pkm.species(), pkmForm);
        if (!sprite) sprite = getSprite(pkm.species(), pkmForm);
    } else {
        sprite = getSprite(pkm.species(), pkmForm);
    }
    if (sprite) {
        int texW, texH;
        SDL_QueryTexture(sprite, nullptr, nullptr, &texW, &texH);
        int dstW, dstH;
        if (texW > 0 && texH > 0) {
            float scale = std::min(static_cast<float>(LARGE_SPRITE) / texW,
                                   static_cast<float>(LARGE_SPRITE) / texH);
            dstW = static_cast<int>(texW * scale);
            dstH = static_cast<int>(texH * scale);
        } else {
            dstW = LARGE_SPRITE;
            dstH = LARGE_SPRITE;
        }
        int dx = sprX + (LARGE_SPRITE - dstW) / 2;
        int dy = sprY + (LARGE_SPRITE - dstH) / 2;
        SDL_Rect dst = {dx, dy, dstW, dstH};
        SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
    }

    // Shiny/Alpha icon next to sprite
    SDL_Texture* statusIcon = nullptr;
    if (pkm.isShiny() && pkm.isAlpha())
        statusIcon = iconShinyAlpha_;
    else if (pkm.isShiny())
        statusIcon = iconShiny_;
    else if (pkm.isAlpha())
        statusIcon = iconAlpha_;
    if (statusIcon) {
        SDL_Rect iconDst = {sprX + LARGE_SPRITE + 4, sprY, 20, 20};
        SDL_RenderCopy(renderer_, statusIcon, nullptr, &iconDst);
    }

    // --- Left column info (next to sprite) ---
    int infoX = sprX + LARGE_SPRITE + 30;
    int infoY = sprY + 4;
#ifdef OH_LINUX
    // 4:3: righe info compatte (c'e' spazio in verticale).
    constexpr int INFO_DY = 28;
#else
    constexpr int INFO_DY = 34;
#endif

    // Ball icon + Species name + level + gender
    constexpr int BALL_SZ = 24;
    int nameStartX = infoX;
    uint8_t ballId = pkm.ball();
    if (ballId > 0) {
        SDL_Texture* ballTex = getBallSprite(ballId);
        if (ballTex) {
            int textH = TTF_FontHeight(font_);
            int by = infoY + (textH - BALL_SZ) / 2;
            SDL_Rect ballDst = {infoX, by, BALL_SZ, BALL_SZ};
            SDL_RenderCopy(renderer_, ballTex, nullptr, &ballDst);
            nameStartX += BALL_SZ + 4;
        }
    }
    std::string specName = SpeciesName::get(pkm.species());
    SDL_Color nameColor = pkm.isShiny() ? T().shiny : T().text;
    drawText(specName, nameStartX, infoY, nameColor, font_);

    std::string lvlStr = "  " + i18n::get(StrKey::LvPrefix) + std::to_string(pkm.level());
    int nameW = getTextEntry(specName, font_, nameColor).w;
    drawText(lvlStr, nameStartX + nameW, infoY, T().text, font_);

    // Gender symbol
    uint8_t g = pkm.gender();
    int afterLvl = nameStartX + nameW;
    int lvlW = getTextEntry(lvlStr, font_, T().text).w;
    afterLvl += lvlW + 4;
    if (g == 0)
        drawText("\xe2\x99\x82", afterLvl, infoY, T().genderMale, font_);
    else if (g == 1)
        drawText("\xe2\x99\x80", afterLvl, infoY, T().genderFemale, font_);

    infoY += INFO_DY;

    // National dex ID
    std::string idStr = i18n::get(StrKey::NationalDexPrefix) + std::to_string(pkm.species());
    drawText(idStr, infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;

    // OT + TID/SID
#ifdef OH_LINUX
    // 4:3: OT e TID/SID su due righe (in una sola invadono il grafico IVs).
    drawText(i18n::get(StrKey::OTPrefix) + pkm.otName(), infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;
    drawText(i18n::get(StrKey::TIDPrefix) + std::to_string(pkm.displayTid())
             + " | " + i18n::get(StrKey::SIDPrefix) + std::to_string(pkm.displaySid()),
             infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;
#else
    std::string otStr = i18n::get(StrKey::OTPrefix) + pkm.otName() + " | " + i18n::get(StrKey::TIDPrefix) + std::to_string(pkm.displayTid())
                        + " | " + i18n::get(StrKey::SIDPrefix) + std::to_string(pkm.displaySid());
    drawText(otStr, infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;
#endif

    // HT (handling trainer) — only for formats that store one
    if (pkm.hasHandlingTrainer()) {
        std::string ht = pkm.htName();
        std::string htStr = i18n::get(StrKey::HTPrefix) +
                            (ht.empty() ? i18n::get(StrKey::NoneItem) : ht);
        drawText(htStr, infoX, infoY, T().textDim, font_);
        infoY += INFO_DY;
    }

    // Nature
    std::string natureStr = i18n::get(StrKey::NaturePrefix) + NatureName::get(pkm.nature());
    drawText(natureStr, infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;

    // Ability
    std::string abilityStr = i18n::get(StrKey::AbilityPrefix) + AbilityName::get(pkm.ability());
    drawText(abilityStr, infoX, infoY, T().textDim, font_);
    infoY += INFO_DY;

    // Held item — Gen2/Gen3 raw → modern per nome corretto (Quick Claw 183→217, non Belue Berry)
    uint16_t item = pkm.heldItem();
    uint16_t dispItem = TradeEvo::heldToDisplayModern(pkm.gameType_, item);
    std::string itemStr = i18n::get(StrKey::HeldItemPrefix) + (dispItem != 0 ? ItemName::get(dispItem) : i18n::get(StrKey::NoneItem));
    drawText(itemStr, infoX, infoY, T().textDim, font_);
    int infoBottom = infoY + 34; // baseline below the last info line

    // --- Below sprite: Moves ---
    // Start below whichever extends lower: the sprite or the info column.
    // The optional HT line can push the info column past the sprite's bottom.
    int movesX = popX + 30;
    int movesY = std::max(sprY + LARGE_SPRITE + 46, infoBottom);

    drawText(i18n::get(StrKey::Moves), movesX, movesY, T().text, font_);
    movesY += 30;

    constexpr int TYPE_ICON_W = 25;
    constexpr int TYPE_ICON_H = 25;
    constexpr int MOVE_ROW_H = 32;
    // MOVE_COL_W dalla testa funzione (190 su 4:3, 230 Switch).
    int textH = TTF_FontHeight(font_);
    uint16_t moves[4] = {pkm.move1(), pkm.move2(), pkm.move3(), pkm.move4()};
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int mx = movesX + 10 + col * MOVE_COL_W;
        int my = movesY + row * MOVE_ROW_H;
        int iconY = my + (MOVE_ROW_H - TYPE_ICON_H) / 2;
        int txtY  = my + (MOVE_ROW_H - textH) / 2;
        if (moves[i] != 0) {
            uint8_t mtype = getMoveType(moves[i], selectedGame_);
            SDL_Texture* typeTex = getTypeSprite(mtype);
            if (typeTex) {
                SDL_Rect typeDst = {mx, iconY, TYPE_ICON_W, TYPE_ICON_H};
                SDL_RenderCopy(renderer_, typeTex, nullptr, &typeDst);
            }
            drawText(MoveName::get(moves[i]), mx + TYPE_ICON_W + 6, txtY, T().textDim, font_);
        } else {
            drawText("---", mx + TYPE_ICON_W + 6, txtY, T().textDim, font_);
        }
    }
    movesY += MOVE_ROW_H * 2;

    // --- Ribbons & Marks below moves ---
    auto ribbons = pkm.getRibbonsAndMarks();
    if (!ribbons.empty()) {
        movesY += 16;
        std::string ribTitle = i18n::fmt(StrKey::RibbonsMarks, std::to_string(ribbons.size()));
        drawText(ribTitle, movesX, movesY, T().text, font_);
        movesY += 30;

        // Two columns, small font with sprite icons
        int col1X = movesX + 4;
        int col2X = movesX + MOVE_COL_W;
        int ribbonY = movesY;
        constexpr int RIB_ROW_H = 30;
        constexpr int ICON_SZ = 18;
        constexpr int ICON_PAD = 4;
        int maxY = popY + POP_H - 74;
        int col = 0;

        for (size_t i = 0; i < ribbons.size(); i++) {
            int x = (col == 0) ? col1X : col2X;
            if (ribbonY + RIB_ROW_H > maxY) {
                int remaining = static_cast<int>(ribbons.size() - i);
                drawText(i18n::fmt(StrKey::MoreRibbons, std::to_string(remaining)), x, ribbonY, T().textDim, font_);
                break;
            }

            // Center both icon and text vertically within the row
            int textH = TTF_FontHeight(font_);
            int contentH = std::max(ICON_SZ, textH);
            int baseY = ribbonY + (RIB_ROW_H - contentH) / 2;
            int iconY = baseY + (contentH - ICON_SZ) / 2;
            int textY = baseY + (contentH - textH) / 2;

            SDL_Texture* ribTex = getRibbonSprite(ribbons[i].filename);
            if (ribTex) {
                SDL_Rect dst = {x, iconY, ICON_SZ, ICON_SZ};
                SDL_RenderCopy(renderer_, ribTex, nullptr, &dst);
            }

            drawText(ribbons[i].name, x + ICON_SZ + ICON_PAD, textY, T().textDim, font_);

            col++;
            if (col >= 2) {
                col = 0;
                ribbonY += RIB_ROW_H;
            }
        }
    }

    // --- Right column: IV and EV radar charts ---
    // Order: HP, Atk, Def, Spe, SpD, SpA (clockwise from top)
    // 4:3: colonna destra (mosse/ribbon a sinistra finiscono a ~380).
#ifdef OH_LINUX
    int chartCX = popX + POP_W - 122;
    // Chart compatti distribuiti in verticale: titoli vicini ai grafici,
    // label PS/Velocita' senza overlap, tutto sopra il footer hint.
    constexpr int CHART_RADIUS = 42;
#else
    int chartCX = popX + POP_W * 3 / 4;
    constexpr int CHART_RADIUS = 65;
#endif

    // IVs radar chart — 4:3: compatto e vicino al suo header
    drawTextCentered(i18n::get(StrKey::IVs), chartCX, popY + 17, T().text, font_);
    int ivsRadar[] = {pkm.ivHp(), pkm.ivAtk(), pkm.ivDef(), pkm.ivSpe(), pkm.ivSpD(), pkm.ivSpA()};
#ifdef OH_LINUX
    drawRadarChart(chartCX, popY + 112, CHART_RADIUS, ivsRadar, 31);
#else
    drawRadarChart(chartCX, popY + 150, CHART_RADIUS, ivsRadar, 31);
#endif

    // EVs radar chart — 4:3: alzato per non toccare il footer hint
    int evsRadar[] = {pkm.evHp(), pkm.evAtk(), pkm.evDef(), pkm.evSpe(), pkm.evSpD(), pkm.evSpA()};
#ifdef OH_LINUX
    drawTextCentered(i18n::get(StrKey::EVs), chartCX, popY + 225, T().text, font_);
    drawRadarChart(chartCX, popY + 320, CHART_RADIUS, evsRadar, 252);
#else
    drawTextCentered(i18n::get(StrKey::EVs), chartCX, popY + 283, T().text, font_);
    drawRadarChart(chartCX, popY + 415, CHART_RADIUS, evsRadar, 252);
#endif

    // PID, EC, ID, TSV (bottom-left, small font, two lines)
    uint16_t tsv = (pkm.tid() ^ pkm.sid()) >> 4;
    char techBuf1[64], techBuf2[64];
    snprintf(techBuf1, sizeof(techBuf1), "PID: %08X   EC: %08X",
             pkm.pid(), pkm.encryptionConstant());
    snprintf(techBuf2, sizeof(techBuf2), "ID: %05u/%05u   TSV: %04u",
             pkm.tid(), pkm.sid(), tsv);
    drawText(techBuf1, popX + 20, popY + POP_H - 66, T().textDim, fontSmall_);
    drawText(techBuf2, popX + 20, popY + POP_H - 50, T().textDim, fontSmall_);

    // Close hint at bottom
    drawTextCentered(i18n::get(StrKey::DetailFooter), popX + POP_W / 2, popY + POP_H - 20, T().textDim, fontSmall_);
}

int UI::menuVisibleCount() const {
    // Layout voci: vedi labelsNormal/labelsApplet in drawMenuPopup().
    // Indice 4 = Wondercard (solo se il gioco le supporta),
    // indice 5 = Export Selected (solo se ci sono slot selezionati),
    // indice 6 = Import PK files (sempre visibile),
    // indice 7 (solo normal + debug) = Generate test mons,
    // indice 8 (solo normal, mai dual) = Send current save (solo a save caricato),
    // indice 9 (solo normal, mai dual) = Scambio self-trade (solo Gen3: Pk3).
    bool hasWC = gameInfo(selectedGame_).hasWondercards;
    bool hasExport = !selectedSlots_.empty();
    // "Send current save" solo a save caricato, mai dual-bank, e SOLO con
    // override rete attivo (debug + url custom). Senza questo, debug off o
    // sorgente GitHub mostrerebbero comunque la voce, che finirebbe per
    // proporre l'upload verso GitHub venendo pero' rifiutato a runtime.
    // Vedi UI::sendAvailable() — stessa logica del `+` radiale, estesa qui.
    bool hasSend = !isDualBankMode() && save_.isLoaded() && sendAvailable();
    bool hasGen = DebugLog::enabled() && !isDualBankMode();
    bool hasTrade = !isDualBankMode() && save_.isLoaded() && TradeEvo::supported(save_.gameType());
    int allCount = isDualBankMode() ? 12 : 14;
    int count = 0;
    for (int i = 0; i < allCount; i++) {
        if (!hasWC && i == 4) continue;
        if (!hasExport && i == 5) continue;
        if (!hasGen && !isDualBankMode() && i == 7) continue;
        if (!hasSend && !isDualBankMode() && i == 8) continue;
        if (!hasTrade && !isDualBankMode() && i == 9) continue;
        count++;
    }
    return count;
}

void UI::drawMenuPopup() {
    // Semi-transparent dark overlay
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    // Menu items differ by mode and game
    // SV/SwSh games get a "Wondercard" item after Search — +1 Crypto (M3d)
    bool hasWC = gameInfo(selectedGame_).hasWondercards;
    bool hasExport = !selectedSlots_.empty();
    // "Send current save": solo a save caricato, mai dual-bank, e SOLO con
    // override rete attivo (debug + url custom). Senza questo, debug off o
    // sorgente GitHub mostrerebbero comunque la voce, che finirebbe per
    // proporre l'upload verso GitHub venendo pero' rifiutato a runtime.
    // Vedi UI::sendAvailable() — stessa logica del `+` radiale, estesa qui.
    bool hasSend = !isDualBankMode() && save_.isLoaded() && sendAvailable();
    // "Generate test mons": solo debug, mai dual-bank.
    bool hasGen = DebugLog::enabled() && !isDualBankMode();
    // "Scambio": solo a save Gen3 caricato (record Pk3, Fase 1), mai dual-bank.
    bool hasTrade = !isDualBankMode() && save_.isLoaded() && TradeEvo::supported(save_.gameType());

    static char exportBuf[64];
    if (hasExport)
        std::snprintf(exportBuf, sizeof(exportBuf), "%s", i18n::fmt(StrKey::MenuExportSelected, std::to_string((int)selectedSlots_.size())).c_str());
    static char cryptoBuf[32];
    std::snprintf(cryptoBuf, sizeof(cryptoBuf), "Crypto: %s", useOpenHome() ? "OpenHome" : "pkHouse");

    const std::string labelsNormal[] = {
        i18n::get(StrKey::MenuTheme),
        i18n::get(StrKey::MenuLanguage),
        cryptoBuf,
        i18n::get(StrKey::MenuSearch),
        i18n::get(StrKey::MenuWondercard),
        exportBuf,
        i18n::get(StrKey::MenuImportPk),
        "Generate test mons (DBG)",
        i18n::get(StrKey::SendSaveTitle),
        i18n::get(StrKey::MenuTrade),
        i18n::get(StrKey::MenuSwitchBank),
        i18n::get(StrKey::MenuChangeGame),
        i18n::get(StrKey::MenuSaveQuit),
        i18n::get(StrKey::MenuQuitNoSave)
    };
    const std::string labelsApplet[] = {
        i18n::get(StrKey::MenuTheme),
        i18n::get(StrKey::MenuLanguage),
        cryptoBuf,
        i18n::get(StrKey::MenuSearch),
        i18n::get(StrKey::MenuWondercard),
        exportBuf,
        i18n::get(StrKey::MenuImportPk),
        i18n::get(StrKey::MenuSwitchLeft),
        i18n::get(StrKey::MenuSwitchRight),
        i18n::get(StrKey::MenuChangeGame),
        i18n::get(StrKey::MenuSaveBanks),
        i18n::get(StrKey::MenuQuit)
    };
    // Build label list, skipping conditional items — menuCount = vi
    std::string visibleLabels[15];
    const std::string* allLabels = isDualBankMode() ? labelsApplet : labelsNormal;
    int allCount = isDualBankMode() ? 12 : 14;
    int vi = 0;
    for (int i = 0; i < allCount; i++) {
        if (!hasWC && i == 4) continue;
        if (!hasExport && i == 5) continue;
        if (!hasGen && !isDualBankMode() && i == 7) continue;
        if (!hasSend && !isDualBankMode() && i == 8) continue;
        if (!hasTrade && !isDualBankMode() && i == 9) continue;
        visibleLabels[vi++] = allLabels[i];
    }
    int menuCount = vi;

    constexpr int POP_W = 380;
    int POP_H = 50 + menuCount * 36 + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::MenuTitle), popX + POP_W / 2, popY + 22, T().text, font_);

    int rowH = 36;
    int startY = popY + 50;

    // Clamp selezione se menuCount è cambiato (es. hasExport)
    if (menuSelection_ >= menuCount) menuSelection_ = menuCount - 1;
    if (menuSelection_ < 0) menuSelection_ = menuCount - 1;

    for (int i = 0; i < menuCount; i++) {
        int rowY = startY + i * rowH;
        if (i == menuSelection_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        drawTextCentered(visibleLabels[i], popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }

    drawTextCentered(i18n::get(StrKey::AConfirmBCancelMenu), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawThemeSelectorPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 380;
    int POP_H = 50 + THEME_COUNT * 36 + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::SelectTheme), popX + POP_W / 2, popY + 22, T().text, font_);

    int rowH = 36;
    int startY = popY + 50;

    for (int i = 0; i < THEME_COUNT; i++) {
        int rowY = startY + i * rowH;
        if (i == themeSelCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        std::string label = getThemeName(i);
        if (i == themeSelOriginal_) label = "* " + label + " *";
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }

    drawTextCentered(i18n::get(StrKey::ASelectBCancel), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawImportSettingsPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 560;
    int rowH = 36;
    // Row 0 = autoCheckUsb_ toggle, rows 1..N = configured paths, row N+1 =
    // "Add path...".
    int rowCount = (int)importPaths_.size() + 2;
    int POP_H = 50 + rowCount * rowH + 30;
    if (POP_H > SCREEN_H - 40)
        POP_H = SCREEN_H - 40; // clamp: a very long list still fits on screen
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::ImportSettingsTitle), popX + POP_W / 2, popY + 22, T().text, font_);

    int startY = popY + 50;

    // Row 0: global USB autocheck toggle
    {
        int rowY = startY;
        if (0 == importSettingsCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        std::string label = std::string(autoCheckUsb_ ? "[x] " : "[ ] ") + i18n::get(StrKey::ImportAutoCheckUsb);
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, fontSmall_);
    }

    for (int i = 0; i < (int)importPaths_.size(); i++) {
        int rowY = startY + (i + 1) * rowH;
        if (i + 1 == importSettingsCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        const auto& e = importPaths_[i];
        std::string label = std::string(e.enabled ? "[x] " : "[ ] ") + e.path;
        // Long paths (esp. USB) can overrun the row — clip rather than overflow.
        constexpr size_t MAX_LABEL = 60;
        if (label.size() > MAX_LABEL)
            label = label.substr(0, MAX_LABEL - 3) + "...";
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, fontSmall_);
    }
    // "+ Add path..." row, always last
    {
        int i = (int)importPaths_.size() + 1;
        int rowY = startY + i * rowH;
        if (i == importSettingsCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        drawTextCentered(i18n::get(StrKey::ImportAddPath), popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }

    drawTextCentered(i18n::get(StrKey::ImportSettingsFooter), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawFolderBrowserPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 700;
    constexpr int ROW_H = 36;
    constexpr int VISIBLE = 10; // matches handleFolderBrowserInput
    int POP_H = 50 + 28 + VISIBLE * ROW_H + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::FolderBrowserTitle), popX + POP_W / 2, popY + 22, T().text, font_);

    // Current path (roots view shows "/").
    std::string cur = folderBrowserPath_.empty() ? "/" : folderBrowserPath_;
    if (cur.size() > 52) cur = "..." + cur.substr(cur.size() - 49);
    drawTextCentered(cur, popX + POP_W / 2, popY + 46, T().textDim, fontSmall_);

    int listY = popY + 50 + 28;
    int count = static_cast<int>(folderEntries_.size());
    if (folderScroll_ > count - 1) folderScroll_ = std::max(0, count - 1);
    if (count == 0) {
        drawTextCentered("-", popX + POP_W / 2, listY + 10, T().textDim, font_);
    } else {
        if (folderScroll_ > 0)
            drawTextCentered("^", popX + POP_W / 2, listY - 12, T().arrow, fontSmall_);
        if (folderScroll_ + VISIBLE < count)
            drawTextCentered("v", popX + POP_W / 2, listY + VISIBLE * ROW_H + 2, T().arrow, fontSmall_);
        for (int i = 0; i < VISIBLE && (folderScroll_ + i) < count; i++) {
            int idx = folderScroll_ + i;
            int rowY = listY + i * ROW_H;
            if (idx == folderCursor_) {
                drawRect(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().menuHighlight);
                drawRectOutline(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().cursor, 2);
            }
            const auto& e = folderEntries_[idx];
            std::string name = e.name;
            if (!folderBrowserPath_.empty() && e.isDir && name.back() != '/') name += "/";
            if (name.size() > 58) name = name.substr(0, 55) + "...";
            // Files shown dimmed for orientation; only dirs are enterable.
            drawText(name, popX + 30, rowY + (ROW_H - 4) / 2 - 9,
                     e.isDir ? T().text : T().textDim, font_);
        }
    }

    drawTextCentered(i18n::get(StrKey::FolderBrowserFooter), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawLanguageSelectorPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    int langCount = (int)langList_.size();
    constexpr int POP_W = 380;
    int POP_H = 50 + langCount * 36 + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::SelectLanguage), popX + POP_W / 2, popY + 22, T().text, font_);

    int rowH = 36;
    int startY = popY + 50;

    for (int i = 0; i < langCount; i++) {
        int rowY = startY + i * rowH;
        if (i == langSelCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, rowH - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, rowH - 4, T().cursor, 2);
        }
        std::string label = langDisplayName(langList_[i]);
        if (langList_[i] == i18n::currentLang()) label = "* " + label + " *";
        drawTextCentered(label, popX + POP_W / 2, rowY + (rowH - 4) / 2, T().text, font_);
    }

    drawTextCentered(i18n::get(StrKey::ASelectBCancel), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawSearchFilterPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    bool hasAlpha = gameInfo(selectedGame_).hasAlphaForms;
    int rowCount = hasAlpha ? 12 : 11;

    constexpr int POP_W = 600;
    constexpr int ROW_H = 36;
    int POP_H = 50 + rowCount * ROW_H + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::SearchFilter), popX + POP_W / 2, popY + 22, T().text, font_);

    int startY = popY + 50;
    int labelX = popX + 30;
    int valueX = popX + 230;

    int visualRow = 0;
    for (int i = 0; i < 12; i++) {
        if (i == 4 && !hasAlpha) continue;

        int rowY = startY + visualRow * ROW_H;

        if (i == searchFilterCursor_) {
            drawRect(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().menuHighlight);
            drawRectOutline(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().cursor, 2);
        }

        int textY = rowY + (ROW_H - 4) / 2 - 9;

        switch (i) {
            case 0: {
                drawText(i18n::get(StrKey::FilterSpecies), labelX, textY, T().text, font_);
                if (searchFilter_.speciesId > 0) {
                    SDL_Texture* spr = getSprite(searchFilter_.speciesId);
                    if (spr) {
                        int sprSize = ROW_H - 6;
                        SDL_Rect dst = { valueX, rowY + 2, sprSize, sprSize };
                        SDL_RenderCopy(renderer_, spr, nullptr, &dst);
                        drawText(searchFilter_.speciesName, valueX + sprSize + 6, textY, T().text, font_);
                    } else {
                        drawText(searchFilter_.speciesName, valueX, textY, T().text, font_);
                    }
                } else {
                    drawText(i18n::get(StrKey::FilterAny), valueX, textY, T().textDim, font_);
                }
                break;
            }
            case 1:
                drawText(i18n::get(StrKey::FilterOT), labelX, textY, T().text, font_);
                drawText(searchFilter_.otName.empty() ? i18n::get(StrKey::FilterAny) : searchFilter_.otName,
                         valueX, textY, searchFilter_.otName.empty() ? T().textDim : T().text, font_);
                break;
            case 2:
                drawText(i18n::get(StrKey::FilterShiny), labelX, textY, T().text, font_);
                drawText(searchFilter_.filterShiny ? i18n::get(StrKey::FilterYes) : i18n::get(StrKey::FilterOff),
                         valueX, textY, searchFilter_.filterShiny ? T().shiny : T().textDim, font_);
                break;
            case 3:
                drawText(i18n::get(StrKey::FilterEgg), labelX, textY, T().text, font_);
                drawText(searchFilter_.filterEgg ? i18n::get(StrKey::FilterYes) : i18n::get(StrKey::FilterOff),
                         valueX, textY, searchFilter_.filterEgg ? T().text : T().textDim, font_);
                break;
            case 4:
                drawText(i18n::get(StrKey::FilterAlpha), labelX, textY, T().text, font_);
                drawText(searchFilter_.filterAlpha ? i18n::get(StrKey::FilterYes) : i18n::get(StrKey::FilterOff),
                         valueX, textY, searchFilter_.filterAlpha ? T().text : T().textDim, font_);
                break;
            case 5: {
                drawText(i18n::get(StrKey::FilterGender), labelX, textY, T().text, font_);
                const char* g = i18n::get(StrKey::GenderAny).c_str();
                if (searchFilter_.gender == GenderFilter::Male)        g = i18n::get(StrKey::GenderMale).c_str();
                else if (searchFilter_.gender == GenderFilter::Female)  g = i18n::get(StrKey::GenderFemale).c_str();
                else if (searchFilter_.gender == GenderFilter::Genderless) g = i18n::get(StrKey::GenderGenderless).c_str();
                drawText(g, valueX, textY, T().text, font_);
                break;
            }
            case 6: {
                drawText(i18n::get(StrKey::FilterLevel), labelX, textY, T().text, font_);
                std::string minStr = searchFilter_.levelMin > 0 ? std::to_string(searchFilter_.levelMin) : "-";
                std::string maxStr = searchFilter_.levelMax > 0 ? std::to_string(searchFilter_.levelMax) : "-";
                SDL_Color minC = (searchFilterCursor_ == 6 && searchLevelFocus_ == 0) ? T().cursor : T().text;
                SDL_Color maxC = (searchFilterCursor_ == 6 && searchLevelFocus_ == 1) ? T().cursor : T().text;
                drawText("[" + minStr + "]", valueX, textY, minC, font_);
                drawText("-", valueX + 60, textY, T().textDim, font_);
                drawText("[" + maxStr + "]", valueX + 80, textY, maxC, font_);
                break;
            }
            case 7: {
                drawText(i18n::get(StrKey::FilterPerfectIVs), labelX, textY, T().text, font_);
                const char* iv = i18n::get(StrKey::FilterOff).c_str();
                if (searchFilter_.perfectIVs == PerfectIVFilter::AtLeastOne) iv = i18n::get(StrKey::IVOnePlus).c_str();
                else if (searchFilter_.perfectIVs == PerfectIVFilter::All6)  iv = i18n::get(StrKey::IVSix).c_str();
                drawText(iv, valueX, textY, T().text, font_);
                break;
            }
            case 8: {
                drawText(i18n::get(StrKey::FilterRibbons), labelX, textY, T().text, font_);
                const char* rf = i18n::get(StrKey::FilterOff).c_str();
                if (searchFilter_.ribbonFilter == RibbonFilter::HasRibbon) rf = i18n::get(StrKey::RibbonHasRibbon).c_str();
                else if (searchFilter_.ribbonFilter == RibbonFilter::HasMark) rf = i18n::get(StrKey::RibbonHasMark).c_str();
                else if (searchFilter_.ribbonFilter == RibbonFilter::HasAny)  rf = i18n::get(StrKey::RibbonHasAny).c_str();
                drawText(rf, valueX, textY,
                         searchFilter_.ribbonFilter != RibbonFilter::Off ? T().text : T().textDim, font_);
                break;
            }
            case 9: {
                drawText(i18n::get(StrKey::FilterMode), labelX, textY, T().text, font_);
                bool isList = (searchFilter_.mode == SearchMode::List);
                drawText(isList ? i18n::get(StrKey::ModeListOn) : i18n::get(StrKey::ModeListOff),
                         valueX, textY, isList ? T().text : T().textDim, font_);
                drawText(!isList ? i18n::get(StrKey::ModeHighlightOn) : i18n::get(StrKey::ModeHighlightOff),
                         valueX + 100, textY, !isList ? T().searchMatch : T().textDim, font_);
                break;
            }
            case 10:
                drawTextCentered(i18n::get(StrKey::FilterReset), popX + POP_W / 2, rowY + (ROW_H - 4) / 2, T().textDim, font_);
                break;
            case 11:
                drawTextCentered(i18n::get(StrKey::FilterSearch), popX + POP_W / 2, rowY + (ROW_H - 4) / 2, T().text, font_);
                break;
        }
        visualRow++;
    }

    drawTextCentered(i18n::get(StrKey::FilterFooter), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawSearchResultsPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 900;
    constexpr int POP_H = 550;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    std::string title = i18n::fmt(StrKey::SearchResultsTitle, std::to_string(searchResults_.size()));
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);

    if (searchResults_.empty()) {
        drawTextCentered(i18n::get(StrKey::NoPokemonFound), popX + POP_W / 2, popY + POP_H / 2, T().textDim, font_);
    } else {
        constexpr int ROW_H = 36;
        int listY = popY + 50;
        int listBottom = popY + POP_H - 40;
        int visibleRows = (listBottom - listY) / ROW_H;
        int listX = popX + 20;
        int listW = POP_W - 40;

        int maxScroll = std::max(0, (int)searchResults_.size() - visibleRows);
        if (searchResultScroll_ > maxScroll) searchResultScroll_ = maxScroll;

        if (searchResultScroll_ > 0)
            drawTextCentered("^", popX + POP_W / 2, listY - 12, T().arrow, fontSmall_);
        if (searchResultScroll_ + visibleRows < (int)searchResults_.size())
            drawTextCentered("v", popX + POP_W / 2, listBottom + 2, T().arrow, fontSmall_);

        for (int i = 0; i < visibleRows && (searchResultScroll_ + i) < (int)searchResults_.size(); i++) {
            int idx = searchResultScroll_ + i;
            const auto& r = searchResults_[idx];
            int rowY = listY + i * ROW_H;

            if (idx == searchResultCursor_) {
                drawRect(listX, rowY, listW, ROW_H - 4, T().menuHighlight);
                drawRectOutline(listX, rowY, listW, ROW_H - 4, T().cursor, 2);
            }

            int textY = rowY + (ROW_H - 4) / 2 - 9;
            int x = listX + 10;

            // Status badges (fixed-width area for up to three badges)
            {
                int bx = x;
                if (r.isShiny) { drawText(i18n::get(StrKey::BadgeShiny), bx, textY, T().shiny, font_); bx += 35; }
                if (r.isAlpha) { drawText(i18n::get(StrKey::BadgeAlpha), bx, textY, T().text, font_); bx += 35; }
                if (r.isEgg)   { drawText(i18n::get(StrKey::BadgeEgg), bx, textY, T().textDim, font_); }
            }
            x += 105;

            // Species name
            std::string name = r.isEgg ? i18n::get(StrKey::Egg) : r.speciesName;
            if (name.length() > 14) name = name.substr(0, 13) + ".";
            drawText(name, x, textY, r.isShiny ? T().shiny : T().text, font_);
            x += 170;

            // Level
            std::string lvlStr = r.isEgg ? i18n::get(StrKey::Egg) : i18n::get(StrKey::LvPrefix) + std::to_string(r.level);
            drawText(lvlStr, x, textY, T().textDim, font_);
            x += 70;

            // Gender
            if (r.gender == 0)
                drawText("\xe2\x99\x82", x, textY, T().genderMale, font_);
            else if (r.gender == 1)
                drawText("\xe2\x99\x80", x, textY, T().genderFemale, font_);
            x += 30;

            // Location
            std::string loc;
            if (isDualBankMode())
                loc = (r.panel == Panel::Game ? i18n::get(StrKey::LocLeft) : i18n::get(StrKey::LocRight));
            else
                loc = (r.panel == Panel::Game ? i18n::get(StrKey::LocSave) : i18n::get(StrKey::LocBank));
            loc += " " + i18n::get(StrKey::BoxLabel) + " " + std::to_string(r.box + 1) + " " + i18n::get(StrKey::SlotLabel) + " " + std::to_string(r.slot + 1);
            drawText(loc, x, textY, T().textDim, font_);
        }
    }

    std::string footer = searchResults_.empty()
        ? i18n::get(StrKey::ResultsFooterEmpty)
        : i18n::get(StrKey::ResultsFooter);
    drawTextCentered(footer, popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawSpeciesLetterPicker() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 1080;
    constexpr int POP_H = 620;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::SelectLetter), popX + POP_W / 2, popY + 22, T().text, font_);

    constexpr int COLS = 2;
    constexpr int TOTAL_ITEMS = 27; // "-" + A-Z
    constexpr int ROW_H = 56;
    constexpr int COL_W = 500;
    constexpr int PAD = 6;
    int gridX = popX + (POP_W - COLS * COL_W) / 2;
    int gridY = popY + 50;
    int gridH = POP_H - 50 - 30;
    int visibleRows = gridH / ROW_H;
    int totalRows = (TOTAL_ITEMS + COLS - 1) / COLS;

    // Auto-scroll to keep cursor visible
    int cursorRow = speciesLetterCursor_ / COLS;
    if (cursorRow < speciesLetterScroll_)
        speciesLetterScroll_ = cursorRow;
    if (cursorRow >= speciesLetterScroll_ + visibleRows)
        speciesLetterScroll_ = cursorRow - visibleRows + 1;

    // Draw scrollbar
    if (totalRows > visibleRows) {
        int sbX = popX + POP_W - 20;
        int sbH = gridH;
        int thumbH = std::max(20, sbH * visibleRows / totalRows);
        int thumbY = gridY + (sbH - thumbH) * speciesLetterScroll_ / (totalRows - visibleRows);
        drawRect(sbX, gridY, 6, sbH, T().textDim);
        drawRect(sbX, thumbY, 6, thumbH, T().text);
    }

    for (int r = 0; r < visibleRows && (speciesLetterScroll_ + r) < totalRows; r++) {
        int row = speciesLetterScroll_ + r;
        for (int c = 0; c < COLS; c++) {
            int idx = row * COLS + c;
            if (idx >= TOTAL_ITEMS) break;

            int cellX = gridX + c * COL_W + PAD;
            int cellY = gridY + r * ROW_H + PAD;
            int cellW = COL_W - PAD * 2;
            int cellH = ROW_H - PAD * 2;

            bool hasSpecies = letterHasSpecies(idx);

            if (idx == speciesLetterCursor_) {
                drawRect(cellX, cellY, cellW, cellH, T().menuHighlight);
                drawRectOutline(cellX, cellY, cellW, cellH, T().cursor, 2);
            } else if (hasSpecies) {
                // Light background for available letters
                SDL_Color bg = T().panelBg;
                bg.r = std::min(255, bg.r + 15);
                bg.g = std::min(255, bg.g + 15);
                bg.b = std::min(255, bg.b + 15);
                drawRect(cellX, cellY, cellW, cellH, bg);
            } else {
                // Dimmed background for unavailable letters
                drawRect(cellX, cellY, cellW, cellH, T().panelBg);
            }

            std::string label = (idx == 0) ? "-" : std::string(1, 'A' + idx - 1);
            drawText(label, cellX + 20, cellY + cellH / 2 - 9,
                     hasSpecies ? T().text : T().textDim, font_);
        }
    }

    drawTextCentered(i18n::get(StrKey::ASelectBBack), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawSpeciesListPicker() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 1080;
    constexpr int POP_H = 620;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    char letter = 'A' + (speciesLetterCursor_ - 1);
    std::string title = i18n::fmt(StrKey::SpeciesDashLetter, std::string(1, letter));
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);

    if (speciesPickerList_.empty()) {
        drawTextCentered(i18n::get(StrKey::NoSpeciesFound), popX + POP_W / 2, popY + POP_H / 2, T().textDim, font_);
    } else {
        constexpr int COLS = 3;
        constexpr int ROW_H = 56;
        constexpr int PAD = 4;
        int total = static_cast<int>(speciesPickerList_.size());
        int totalRows = (total + COLS - 1) / COLS;
        int COL_W = (POP_W - 40) / COLS;
        int gridX = popX + 20;
        int gridY = popY + 50;
        int gridH = POP_H - 50 - 30;
        int visibleRows = gridH / ROW_H;

        // Auto-scroll to keep cursor visible
        int cursorRow = speciesListCursor_ / COLS;
        if (cursorRow < speciesListScroll_)
            speciesListScroll_ = cursorRow;
        if (cursorRow >= speciesListScroll_ + visibleRows)
            speciesListScroll_ = cursorRow - visibleRows + 1;

        // Draw scrollbar
        if (totalRows > visibleRows) {
            int sbX = popX + POP_W - 20;
            int sbH = gridH;
            int thumbH = std::max(20, sbH * visibleRows / totalRows);
            int thumbY = gridY + (sbH - thumbH) * speciesListScroll_ / (totalRows - visibleRows);
            drawRect(sbX, gridY, 6, sbH, T().textDim);
            drawRect(sbX, thumbY, 6, thumbH, T().text);
        }

        constexpr int SPRITE_SZ = 40;

        for (int r = 0; r < visibleRows && (speciesListScroll_ + r) < totalRows; r++) {
            int row = speciesListScroll_ + r;
            for (int c = 0; c < COLS; c++) {
                int idx = row * COLS + c;
                if (idx >= total) break;

                uint16_t specId = speciesPickerList_[idx];
                const std::string& name = SpeciesName::get(specId);

                int cellX = gridX + c * COL_W + PAD;
                int cellY = gridY + r * ROW_H + PAD;
                int cellW = COL_W - PAD * 2;
                int cellH = ROW_H - PAD * 2;

                if (idx == speciesListCursor_) {
                    drawRect(cellX, cellY, cellW, cellH, T().menuHighlight);
                    drawRectOutline(cellX, cellY, cellW, cellH, T().cursor, 2);
                } else {
                    SDL_Color bg = T().panelBg;
                    bg.r = std::min(255, bg.r + 15);
                    bg.g = std::min(255, bg.g + 15);
                    bg.b = std::min(255, bg.b + 15);
                    drawRect(cellX, cellY, cellW, cellH, bg);
                }

                // Draw sprite (aspect-ratio preserved)
                SDL_Texture* spr = getSprite(specId);
                if (spr) {
                    int texW, texH;
                    SDL_QueryTexture(spr, nullptr, nullptr, &texW, &texH);
                    int dstW = SPRITE_SZ, dstH = SPRITE_SZ;
                    if (texW > 0 && texH > 0 && (texW != texH)) {
                        float scale = std::min(static_cast<float>(SPRITE_SZ) / texW,
                                               static_cast<float>(SPRITE_SZ) / texH);
                        dstW = static_cast<int>(texW * scale);
                        dstH = static_cast<int>(texH * scale);
                    }
                    int sprX = cellX + 6 + (SPRITE_SZ - dstW) / 2;
                    int sprY = cellY + (cellH - dstH) / 2;
                    SDL_Rect dst = { sprX, sprY, dstW, dstH };
                    SDL_RenderCopy(renderer_, spr, nullptr, &dst);
                }

                // Draw name (truncated if needed)
                std::string displayName = name;
                if (displayName.length() > 14) displayName = displayName.substr(0, 13) + ".";
                drawText(displayName, cellX + 6 + SPRITE_SZ + 6, cellY + cellH / 2 - 9, T().text, font_);
            }
        }
    }

    drawTextCentered(i18n::get(StrKey::ASelectBBack), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawWondercardListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 900;
    constexpr int POP_H = 550;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    std::string title = i18n::fmt(StrKey::WondercardsTitle, std::to_string(wcList_.size()));
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);

    if (wcList_.empty()) {
        drawTextCentered(i18n::get(StrKey::NoWCFound), popX + POP_W / 2, popY + POP_H / 2 - 20, T().textDim, font_);
        std::string hint = i18n::fmt(StrKey::PlaceFilesIn, std::string(gameInfo(selectedGame_).wcExtensionHint));
        drawTextCentered(hint, popX + POP_W / 2, popY + POP_H / 2 + 10, T().textDim, fontSmall_);
        std::string path = basePath_ + "wondercards/" + std::string(bankFolderNameOf(selectedGame_)) + "/";
        drawTextCentered(path, popX + POP_W / 2, popY + POP_H / 2 + 30, T().textDim, fontSmall_);
    } else {
        constexpr int ROW_H = 36;
        int listY = popY + 50;
        int listBottom = popY + POP_H - 40;
        int visibleRows = (listBottom - listY) / ROW_H;
        int listX = popX + 20;
        int listW = POP_W - 40;

        int maxScroll = std::max(0, (int)wcList_.size() - visibleRows);
        if (wcListScroll_ > maxScroll) wcListScroll_ = maxScroll;

        if (wcListScroll_ > 0)
            drawTextCentered("^", popX + POP_W / 2, listY - 12, T().arrow, fontSmall_);
        if (wcListScroll_ + visibleRows < (int)wcList_.size())
            drawTextCentered("v", popX + POP_W / 2, listBottom + 2, T().arrow, fontSmall_);

        for (int i = 0; i < visibleRows && (wcListScroll_ + i) < (int)wcList_.size(); i++) {
            int idx = wcListScroll_ + i;
            const auto& wc = wcList_[idx];
            int rowY = listY + i * ROW_H;

            if (idx == wcListCursor_) {
                drawRect(listX, rowY, listW, ROW_H - 4, T().menuHighlight);
                drawRectOutline(listX, rowY, listW, ROW_H - 4, T().cursor, 2);
            }

            int textY = rowY + (ROW_H - 4) / 2 - 9;
            int x = listX + 10;

            if (!wc.valid) {
                // Invalid entry — show filename and marker
                drawText(i18n::get(StrKey::BadgeInvalid), x, textY, T().genderFemale, font_);
                x += 110;
                std::string fn = wc.filename;
                if (fn.length() > 40) fn = fn.substr(0, 39) + ".";
                drawText(fn, x, textY, T().textDim, font_);
            } else {
                // Shiny indicator
                if (wc.isShiny) {
                    drawText(i18n::get(StrKey::BadgeShiny), x, textY, T().shiny, font_);
                }
                x += 40;

                // Sprite (shiny variant if wondercard is shiny)
                SDL_Texture* sprite = nullptr;
                if (wc.isShiny) {
                    sprite = getShinySprite(wc.species);
                    if (!sprite) sprite = getSprite(wc.species);
                } else {
                    sprite = getSprite(wc.species);
                }
                if (sprite) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(sprite, nullptr, nullptr, &tw, &th);
                    int maxH = ROW_H - 6;
                    float scale = std::min(static_cast<float>(maxH) / tw,
                                           static_cast<float>(maxH) / th);
                    int dw = static_cast<int>(tw * scale);
                    int dh = static_cast<int>(th * scale);
                    SDL_Rect dst = {x + (maxH - dw) / 2, rowY + 2 + (maxH - dh) / 2, dw, dh};
                    SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
                }
                x += ROW_H;

                // Species name
                const std::string& name = SpeciesName::get(wc.species);
                std::string displayName = name;
                if (displayName.length() > 14) displayName = displayName.substr(0, 13) + ".";
                drawText(displayName, x, textY, wc.isShiny ? T().shiny : T().text, font_);
                x += 170;

                // Level
                drawText(i18n::get(StrKey::LvPrefix) + std::to_string(wc.level), x, textY, T().textDim, font_);
                x += 70;

                // Player OT tag
                if (!wc.hasOT) {
                    drawText(i18n::get(StrKey::PlayerOTTag), x, textY, T().genderFemale, font_);
                }
                x += 120;

                // Filename (truncate to fit)
                int maxW = listX + listW - x - 5;
                std::string fn = wc.filename;
                while (fn.size() > 4) {
                    int tw = getTextEntry(fn, fontSmall_, T().textDim).w;
                    if (tw <= maxW) break;
                    fn = fn.substr(0, fn.size() - 5) + "..";
                }
                drawText(fn, x, textY, T().textDim, fontSmall_);
            }
        }
    }

    std::string footer = wcList_.empty()
        ? i18n::get(StrKey::BClose)
        : i18n::get(StrKey::WCFooter);
    drawTextCentered(footer, popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawGenMonListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    int count = (int)genMonList_.size();
    constexpr int POP_W = 520;
    constexpr int ROW_H = 36;
    constexpr int VISIBLE = 12;
    int rows = count > 0 ? std::min(count, VISIBLE) : 1;
    int POP_H = 50 + rows * ROW_H + 30;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);
    drawTextCentered("Generate test mon (DBG)", popX + POP_W / 2, popY + 22, T().text, font_);
    int startY = popY + 50;
    if (count == 0) {
        drawTextCentered("Empty table.", popX + POP_W / 2, startY + (ROW_H - 4) / 2, T().textDim, font_);
    } else {
        for (int r = 0; r < rows; r++) {
            int i = genMonScroll_ + r;
            if (i >= count) break;
            int rowY = startY + r * ROW_H;
            if (i == genMonCursor_) {
                drawRect(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().menuHighlight);
                drawRectOutline(popX + 20, rowY, POP_W - 40, ROW_H - 4, T().cursor, 2);
            }
            const auto& e = genMonList_[i];
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  (Lv %u)", e.label, e.level);
            drawText(buf, popX + 30, rowY + 6, T().text, fontSmall_);
        }
    }
    drawTextCentered(i18n::get(StrKey::ASelectBCancel), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawPkImportListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    constexpr int POP_W = 900;
    constexpr int POP_H = 550;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    std::string title = i18n::fmt(StrKey::PkImportTitle, std::to_string(pkImportList_.size()));
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);

    if (pkImportList_.empty()) {
        drawTextCentered(i18n::get(StrKey::PkImportNone), popX + POP_W / 2, popY + POP_H / 2 - 20, T().textDim, font_);
        std::string hint = i18n::fmt(StrKey::PlaceFilesIn, std::string(".pk1/.pk2"));
        drawTextCentered(hint, popX + POP_W / 2, popY + POP_H / 2 + 10, T().textDim, fontSmall_);
        std::string path = basePath_ + "import/ (o export/)";
        drawTextCentered(path, popX + POP_W / 2, popY + POP_H / 2 + 30, T().textDim, fontSmall_);
    } else {
        constexpr int ROW_H = 36;
        int listY = popY + 50;
        int listBottom = popY + POP_H - 40;
        int visibleRows = (listBottom - listY) / ROW_H;
        int listX = popX + 20;
        int listW = POP_W - 40;

        int maxScroll = std::max(0, (int)pkImportList_.size() - visibleRows);
        if (pkImportScroll_ > maxScroll) pkImportScroll_ = maxScroll;

        if (pkImportScroll_ > 0)
            drawTextCentered("^", popX + POP_W / 2, listY - 12, T().arrow, fontSmall_);
        if (pkImportScroll_ + visibleRows < (int)pkImportList_.size())
            drawTextCentered("v", popX + POP_W / 2, listBottom + 2, T().arrow, fontSmall_);

        for (int i = 0; i < visibleRows && (pkImportScroll_ + i) < (int)pkImportList_.size(); i++) {
            int idx = pkImportScroll_ + i;
            const auto& pk = pkImportList_[idx];
            int rowY = listY + i * ROW_H;

            if (idx == pkImportCursor_) {
                drawRect(listX, rowY, listW, ROW_H - 4, T().menuHighlight);
                drawRectOutline(listX, rowY, listW, ROW_H - 4, T().cursor, 2);
            }

            int textY = rowY + (ROW_H - 4) / 2 - 9;
            int x = listX + 10;

            if (!pk.valid) {
                drawText(i18n::get(StrKey::BadgeInvalid), x, textY, T().genderFemale, font_);
                x += 110;
                std::string fn = pk.filename;
                if (fn.length() > 40) fn = fn.substr(0, 39) + ".";
                drawText(fn, x, textY, T().textDim, font_);
            } else {
                // Multi-select marker / imported check (ASCII only: the
                // Switch system font renders other glyphs as tofu).
                if (pk.selected) {
                    drawText(">", x, textY, T().cursor, font_);
                } else if (pk.imported) {
                    drawText("*", x, textY, T().textDim, font_);
                }
                x += 24;
                // Sprite
                SDL_Texture* sprite = getSprite(pk.species, 0);
                if (sprite) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(sprite, nullptr, nullptr, &tw, &th);
                    int maxH = ROW_H - 6;
                    float scale = std::min(static_cast<float>(maxH) / tw,
                                           static_cast<float>(maxH) / th);
                    int dw = static_cast<int>(tw * scale);
                    int dh = static_cast<int>(th * scale);
                    SDL_Rect dst = {x + (maxH - dw) / 2, rowY + 2 + (maxH - dh) / 2, dw, dh};
                    SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
                }
                x += ROW_H;

                // Species name
                const std::string& name = SpeciesName::get(pk.species);
                std::string displayName = name;
                if (displayName.length() > 14) displayName = displayName.substr(0, 13) + ".";
                drawText(displayName, x, textY, T().text, font_);
                x += 170;

                // Gen tag
                drawText("Gen " + std::to_string(pk.gen), x, textY, T().textDim, font_);
                x += 70;

                // Filename (truncate to fit)
                int maxW = listX + listW - x - 5;
                std::string fn = pk.filename;
                while (fn.size() > 4) {
                    int tw = getTextEntry(fn, fontSmall_, T().textDim).w;
                    if (tw <= maxW) break;
                    fn = fn.substr(0, fn.size() - 5) + "..";
                }
                drawText(fn, x, textY, T().textDim, fontSmall_);
            }
        }
    }

    std::string footer = pkImportList_.empty()
        ? i18n::get(StrKey::BClose)
        : i18n::get(StrKey::PkImportFooter);
    drawTextCentered(footer, popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawTradeListPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

#ifdef OH_LINUX
    // 4:3: popup contenuto, colonne compresse (vedi TX_ITEM/TX_EVO sotto).
    constexpr int POP_W = 600;
    constexpr int ROW_H = 56;
    constexpr int VISIBLE = 4;
#else
    constexpr int POP_W = 900;
    constexpr int ROW_H = 64;
    constexpr int VISIBLE = 6;
#endif
    int count = (int)tradeCandidates_.size();
    int rows = count > 0 ? std::min(count, VISIBLE) : 1;
    int POP_H = 60 + rows * ROW_H + 40;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    drawTextCentered(i18n::get(StrKey::TradeTitle), popX + POP_W / 2, popY + 22, T().text, font_);
    if (count > VISIBLE) {
        std::string pos = std::to_string(tradeCursor_ + 1) + "/" + std::to_string(count);
        drawText(pos, popX + POP_W - 60, popY + 16, T().textDim, fontSmall_);
    }

    int listY = popY + 60;
    int listX = popX + 20;
    int listW = POP_W - 40;
#ifdef OH_LINUX
    // 4:3: colonne compresse (item/evo piu' vicini, testi troncati sotto).
    constexpr int TX_ITEM = 140;
    constexpr int TX_EVO = 280;
#else
    constexpr int TX_ITEM = 220;
    constexpr int TX_EVO = 430;
#endif

    for (int r = 0; r < rows; r++) {
        int i = tradeScroll_ + r;
        if (i >= count) break;
        const TradeCandidate& ref = tradeCandidates_[i];
        Pokemon pkm = ref.box < 0 ? save_.getPartySlot(ref.slot) : save_.getBoxSlot(ref.box, ref.slot);
        int rowY = listY + r * ROW_H;
        if (i == tradeCursor_) {
            drawRect(listX, rowY, listW, ROW_H - 4, T().menuHighlight);
            drawRectOutline(listX, rowY, listW, ROW_H - 4, T().cursor, 2);
        }
        int textY = rowY + (ROW_H - 4) / 2 - 9;
        int x = listX + 10;
        SDL_Texture* sprite = getSprite(pkm.species(), 0);
        if (sprite) {
            int tw = 0, th = 0;
            SDL_QueryTexture(sprite, nullptr, nullptr, &tw, &th);
            int maxH = ROW_H - 10;
            float scale = std::min(static_cast<float>(maxH) / tw,
                                   static_cast<float>(maxH) / th);
            int dw = static_cast<int>(tw * scale);
            int dh = static_cast<int>(th * scale);
            SDL_Rect dst = {x + (maxH - dw) / 2, rowY + 2 + (maxH - dh) / 2, dw, dh};
            SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
        }
        x += ROW_H;
        // Riga 1: nome + livello. Riga 2 (piu' piccola): da dove viene
        // (Party / Box N), utile ora che la lista pesca anche dai box.
        std::string label = "Lv " + std::to_string((int)pkm.level()) + " " +
                            SpeciesName::get(pkm.species());
        std::string loc = ref.box < 0 ? i18n::get(StrKey::PartyPokemon)
                                       : (i18n::get(StrKey::BoxLabel) + " " + std::to_string(ref.box + 1));
        uint16_t heldItemId = pkm.heldItem();
        uint16_t dispId = TradeEvo::heldToDisplayModern(save_.gameType(), heldItemId);
        std::string itemStr = dispId != 0 ? ItemName::get(dispId) : i18n::get(StrKey::NoneItem);
        uint16_t heldModern = TradeEvo::heldToModern(save_.gameType(), heldItemId);
        const TradeEvo::TradeRule* rule = TradeEvo::findRule(pkm.species(), heldModern);
        const TradeEvo::TradeRule* base = TradeEvo::baseRuleFor(pkm.species());
        bool outOfRange = rule && TradeEvo::isOutOfRange(save_.gameType(), rule->to);
        bool isPaired = TradeEvo::isPairedSpecies(pkm.species());
        // Etichetta: bianca se evolvibile subito, grigia se serve item, rossa se fuori-range.
        SDL_Color labelCol = outOfRange ? T().genderFemale : (rule ? T().text : T().textDim);
        drawText(label, x, textY, labelCol, font_);
        drawText(loc, x, textY + 20, T().textDim, fontSmall_);
        // 4:3: tronca i testi lunghi per non invadere lo sprite ricevuto a destra.
        auto truncFit = [&](std::string t, TTF_Font* f, SDL_Color c, int maxW) {
            while (t.size() > 4 && getTextEntry(t, f, c).w > maxW)
                t = t.substr(0, t.size() - 5) + "..";
            return t;
        };
        int evoMaxW = listX + listW - (ROW_H - 10) - 10 - 8 - (x + TX_EVO);
        drawText(itemStr, x + TX_ITEM, textY, T().textDim, fontSmall_);
        if (outOfRange) {
            drawText("! " + i18n::get(StrKey::TradeOutOfRange), x + TX_EVO, textY, T().genderFemale, fontSmall_);
        } else if (rule) {
            std::string arr = "-> " + SpeciesName::get(rule->to);
            if (isPaired) arr += " *";
            drawText(truncFit(arr, font_, T().cursor, evoMaxW), x + TX_EVO, textY, T().cursor, font_);
            if (isPaired) drawText(i18n::get(StrKey::TradePairedHint), x + TX_EVO, textY + 20, T().textDim, fontSmall_);
            // Sprite del Pokémon ricevuto (a destra): così si vedono entrambi i lati dello scambio
            SDL_Texture* sprite2 = getSprite(rule->to, 0);
            if (sprite2) {
                int tw = 0, th = 0;
                SDL_QueryTexture(sprite2, nullptr, nullptr, &tw, &th);
                int maxH = ROW_H - 10;
                float scale = std::min(static_cast<float>(maxH) / tw,
                                       static_cast<float>(maxH) / th);
                int dw = static_cast<int>(tw * scale);
                int dh = static_cast<int>(th * scale);
                int sx = listX + listW - maxH - 10 + (maxH - dw) / 2;
                int sy = rowY + 2 + (maxH - dh) / 2;
                SDL_Rect dst2 = {sx, sy, dw, dh};
                SDL_RenderCopy(renderer_, sprite2, nullptr, &dst2);
            }
        } else if (base && base->heldModern != 0) {
            std::string need = ItemLocations::itemName(base->heldModern);
            drawText(truncFit(i18n::get(StrKey::TradeNeedsItem) + " (" + need + ")", fontSmall_, T().textDim, evoMaxW), x + TX_EVO, textY, T().textDim, fontSmall_);
            std::string where = ItemLocations::footerLine(base->heldModern);
            if (!where.empty()) drawText(where, x + TX_EVO, textY + 20, T().textDim, fontSmall_);
        } else {
            drawText(i18n::get(StrKey::TradeNeedsItem), x + TX_EVO, textY, T().textDim, fontSmall_);
        }
    }

    drawTextCentered(i18n::get(StrKey::TradeFooter), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawLearnsetPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

#ifdef OH_LINUX
    constexpr int POP_W = 600;
    constexpr int POP_H = 420;
#else
    constexpr int POP_W = 700;
    constexpr int POP_H = 550;
#endif
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;

    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

    std::string title = i18n::fmt(StrKey::LearnsetTitle, SpeciesName::get(learnsetSpecies_));
    drawTextCentered(title, popX + POP_W / 2, popY + 22, T().text, font_);

    if (learnset_.empty()) {
        drawTextCentered(i18n::get(StrKey::LearnsetNone), popX + POP_W / 2, popY + POP_H / 2 - 10, T().textDim, font_);
    } else {
        constexpr int ROW_H = 36;
        int listY = popY + 50;
        int listBottom = popY + POP_H - 40;
        int visibleRows = (listBottom - listY) / ROW_H;
        int listX = popX + 20;
        int listW = POP_W - 40;

        int maxScroll = std::max(0, (int)learnset_.size() - visibleRows);
        if (learnsetScroll_ > maxScroll) learnsetScroll_ = maxScroll;

        if (learnsetScroll_ > 0)
            drawTextCentered("^", popX + POP_W / 2, listY - 12, T().arrow, fontSmall_);
        if (learnsetScroll_ + visibleRows < (int)learnset_.size())
            drawTextCentered("v", popX + POP_W / 2, listBottom + 2, T().arrow, fontSmall_);

        for (int i = 0; i < visibleRows && (learnsetScroll_ + i) < (int)learnset_.size(); i++) {
            int idx = learnsetScroll_ + i;
            uint16_t moveId = learnset_[idx].first;
            uint8_t lvl = learnset_[idx].second;
            int rowY = listY + i * ROW_H;

            if (idx == learnsetCursor_) {
                drawRect(listX, rowY, listW, ROW_H - 4, T().menuHighlight);
                drawRectOutline(listX, rowY, listW, ROW_H - 4, T().cursor, 2);
            }

            int textY = rowY + (ROW_H - 4) / 2 - 9;
            int x = listX + 10;

            bool equipped = (moveId == learnsetEquipped_[0] || moveId == learnsetEquipped_[1] ||
                             moveId == learnsetEquipped_[2] || moveId == learnsetEquipped_[3]);
            std::string name = MoveName::get(moveId);
            if (name.length() > 16) name = name.substr(0, 15) + ".";
            if (equipped) name += " *";
            drawText(name, x, textY, equipped ? T().shiny : T().text, font_);
            x += 260;

            // Level (0 = evolution move)
            std::string lv = (lvl == 0) ? "Evo" : ("Lv " + std::to_string(lvl));
            drawText(lv, x, textY, T().textDim, font_);
        }
    }

    drawTextCentered(i18n::get(StrKey::BClose), popX + POP_W / 2, popY + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawAboutPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

#ifdef OH_LINUX
    // R36S 640x480: popup compatto, stessa struttura. Senza sezione Comandi
    // (layout tasti Switch, non applicabile); credits incl. pkHouse mantenuti.
    constexpr int POP_W = 600;
    constexpr int POP_H = 460;
#else
    constexpr int POP_W = 700;
    constexpr int POP_H = 600;
#endif
    int px = (SCREEN_W - POP_W) / 2;
    int py = (SCREEN_H - POP_H) / 2;

    drawRect(px, py, POP_W, POP_H, T().panelBg);
    drawRectOutline(px, py, POP_W, POP_H, T().popupBorder, 2);

    int cx = px + POP_W / 2;
#ifdef OH_LINUX
    int y = py + 22;
#else
    int y = py + 25;
#endif

    // Title
    drawTextCentered(i18n::get(StrKey::AboutTitle), cx, y, T().shiny, fontLarge_);
#ifdef OH_LINUX
    y += 32;
#else
    y += 38;
#endif

    // Version / author (GitHub URLs moved to Basato su section)
    drawTextCentered("v" APP_VERSION " - Developed by " APP_AUTHOR, cx, y, T().textDim, fontSmall_);
    y += 20;

    // Divider
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    SDL_RenderDrawLine(renderer_, px + 30, y, px + POP_W - 30, y);
#ifdef OH_LINUX
    y += 12;
#else
    y += 18;
#endif

    // Description - wrapped to stay inside popup ( AboutDesc2 was overflowing )
    constexpr int MAX_W = POP_W - 60;
    auto drawWrappedCentered = [&](const std::string& text, TTF_Font* f, SDL_Color col, int maxW, int lineH, int& yRef) {
        if (text.empty()) return;
        std::vector<std::string> words;
        std::istringstream iss(text);
        std::string w;
        while (iss >> w) words.push_back(w);
        std::vector<std::string> lines;
        std::string cur;
        for (auto &word : words) {
            std::string cand = cur.empty() ? word : cur + " " + word;
            int cw = getTextEntry(cand, f, col).w;
            if (cw <= maxW) {
                cur = cand;
            } else {
                if (!cur.empty()) lines.push_back(cur);
                if (getTextEntry(word, f, col).w > maxW) {
                    std::string part;
                    for (size_t i = 0; i < word.size();) {
                        size_t len = 1;
                        unsigned char c = static_cast<unsigned char>(word[i]);
                        if ((c & 0x80) == 0) len = 1;
                        else if ((c & 0xE0) == 0xC0) len = 2;
                        else if ((c & 0xF0) == 0xE0) len = 3;
                        else if ((c & 0xF8) == 0xF0) len = 4;
                        std::string ch = word.substr(i, len);
                        std::string cand2 = part + ch;
                        if (!part.empty() && getTextEntry(cand2, f, col).w > maxW) {
                            lines.push_back(part);
                            part = ch;
                        } else {
                            part = cand2;
                        }
                        i += len;
                    }
                    cur = part;
                } else {
                    cur = word;
                }
            }
        }
        if (!cur.empty()) lines.push_back(cur);
        for (auto &ln : lines) {
            drawTextCentered(ln, cx, yRef, col, f);
            yRef += lineH;
        }
    };

    drawWrappedCentered(i18n::get(StrKey::AboutDesc1), fontAbout_, T().text, MAX_W, 26, y);
    y += 6;
    // AboutDesc2 uses smaller font and wrapping to avoid going off-screen (it.json is very long)
    drawWrappedCentered(i18n::get(StrKey::AboutDesc2), fontAbout_, T().text, MAX_W, 26, y);
#ifdef OH_LINUX
    y += 10;
#else
    y += 16;
#endif

    drawTextCentered(i18n::get(StrKey::SupportedGames), cx, y, T().selected, font_);
    y += 22;
    drawTextCentered(i18n::get(StrKey::SupportedLGPE), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::SupportedSwSh), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::SupportedSVZA), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::SupportedFRLG), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::SupportedGB), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 12;
#else
    y += 14;
#endif

    // Divider
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    SDL_RenderDrawLine(renderer_, px + 30, y, px + POP_W - 30, y);
#ifdef OH_LINUX
    y += 12;
#else
    y += 14;
#endif

    // Basato su / Based on - ordered by peso reale: OpenHome (core Rust vivo,
    // compilato dentro l'app) prima di pkHouse (ormai solo credito storico,
    // l'idea originale della griglia box/banche - vedi about_based_pkhouse).
    // Poi Sphaira forwarder, PKHeX, libnx, devkitPro.
    drawTextCentered(i18n::get(StrKey::AboutBasedOn), cx, y, T().selected, font_);
#ifdef OH_LINUX
    y += 19;
#else
    y += 20;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedOpenHome), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedPKHouse), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedSphaira), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedPKHeX), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedLibnx), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif
    drawTextCentered(i18n::get(StrKey::AboutBasedDevkitPro), cx, y, T().textDim, fontSmall_);
#ifdef OH_LINUX
    y += 16;
#else
    y += 18;
#endif

#ifdef OH_LINUX
    // R36S: niente sezione Comandi (layout tasti Switch, non applicabile).
#else
    // Divider before controls
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    SDL_RenderDrawLine(renderer_, px + 30, y, px + POP_W - 30, y);
    y += 14;

    // Controls - fixed y increments and POP_H to avoid overlap
    drawTextCentered(i18n::get(StrKey::Controls), cx, y, T().selected, font_);
    y += 22;
    drawText(i18n::get(StrKey::ControlsLine1), px + 50, y, T().textDim, fontSmall_);
    y += 18;
    drawText(i18n::get(StrKey::ControlsLine2), px + 50, y, T().textDim, fontSmall_);
    y += 10;
#endif

    // Footer (anchored to bottom of popup, no longer overlapping controls)
    drawTextCentered(i18n::get(StrKey::PressMinusBClose), cx, py + POP_H - 18, T().textDim, fontSmall_);
}

void UI::drawBoxViewOverlay() {
    // Full-screen dark overlay
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);

    int totalBoxes;
    if (boxViewPanel_ == Panel::Game)
        totalBoxes = (isDualBankMode()) ? bankLeft_.boxCount() : save_.boxCount();
    else
        totalBoxes = bank_.boxCount();
    int usedRows = (totalBoxes + BV_COLS - 1) / BV_COLS;

    // Popup dimensions
    int gridW = BV_COLS * BV_CELL_W + (BV_COLS - 1) * BV_CELL_PAD;
    int gridH = usedRows * BV_CELL_H + (usedRows - 1) * BV_CELL_PAD;
    int popW = gridW + 40;
    int popH = gridH + 100;

    int popX = (SCREEN_W - popW) / 2;
    int popY = (SCREEN_H - popH) / 2;

    // Popup background
    drawRect(popX, popY, popW, popH, T().panelBg);
    drawRectOutline(popX, popY, popW, popH, T().cursor, 2);

    // Title
    const std::string& title2 = (boxViewPanel_ == Panel::Game)
        ? (isDualBankMode() ? i18n::get(StrKey::BoxViewLeft) : i18n::get(StrKey::BoxViewSave))
        : i18n::get(StrKey::BoxViewBank);
    const char* title = title2.c_str();
    drawTextCentered(title, popX + popW / 2, popY + 15, T().text, font_);

    // Subtitle: bank file name or profile | game
    std::string subtitle;
    if (boxViewPanel_ == Panel::Game) {
        if (isDualBankMode())
            subtitle = leftBankName_;
        else if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
            subtitle = account_.profiles()[selectedProfile_].nickname + " | " + gameDisplayNameOf(selectedGame_);
        else
            subtitle = gameDisplayNameOf(selectedGame_);
    } else {
        subtitle = activeBankName_;
    }
    if (!subtitle.empty())
        drawTextCentered(subtitle, popX + popW / 2, popY + 38, T().textDim, fontSmall_);

    // Grid of box cells
    int gridStartX = popX + 20;
    int gridStartY = popY + 55;
    int activeBox = (boxViewPanel_ == Panel::Game) ? gameBox_ : bankBox_;

    int cursorCellX = 0, cursorCellY = 0;

    for (int i = 0; i < totalBoxes; i++) {
        int col = i % BV_COLS;
        int row = i / BV_COLS;
        int cellX = gridStartX + col * (BV_CELL_W + BV_CELL_PAD);
        int cellY = gridStartY + row * (BV_CELL_H + BV_CELL_PAD);

        // Cell background — highlight the currently-active box
        SDL_Color bg = (i == activeBox) ? T().slotFull : T().slotEmpty;
        drawRect(cellX, cellY, BV_CELL_W, BV_CELL_H, bg);

        // Search highlight: outline boxes that contain matches
        if (searchHighlightActive_) {
            bool hasMatch = false;
            int slots = maxSlotsFor(boxViewPanel_);
            for (int s = 0; s < slots && !hasMatch; s++)
                hasMatch = isSearchMatch(boxViewPanel_, i, s);
            if (hasMatch)
                drawRectOutline(cellX + 1, cellY + 1, BV_CELL_W - 2, BV_CELL_H - 2, T().searchMatch, 2);
        }

        // Cursor outline
        if (i == boxViewCursor_) {
            drawRectOutline(cellX, cellY, BV_CELL_W, BV_CELL_H, T().cursor, 2);
            cursorCellX = cellX;
            cursorCellY = cellY;
        }

        // Box label
        std::string boxName;
        if (boxViewPanel_ == Panel::Game)
            boxName = (isDualBankMode()) ? bankLeft_.getBoxName(i) : save_.getBoxName(i);
        else
            boxName = bank_.getBoxName(i);

        std::string label = boxName;
        if (label.length() > 16)
            label = label.substr(0, 15) + ".";

        // Box-state icon on the left of the label
        const auto& disp = getSlotDisplays(boxViewPanel_, i);
        int filled = 0;
        for (const auto& sd : disp) if (!sd.empty) ++filled;
        int slots = maxSlotsFor(boxViewPanel_);
        SDL_Texture* stateIcon = iconBoxEmpty_;
        if (filled >= slots && slots > 0) stateIcon = iconBoxFull_;
        else if (filled > 0)              stateIcon = iconBoxNonEmpty_;

        const int iconSize = 22;
        const int iconPadX = 6;
        int iconX = cellX + iconPadX;
        int iconY = cellY + (BV_CELL_H - iconSize) / 2;
        if (stateIcon) {
            SDL_Rect dst{ iconX, iconY, iconSize, iconSize };
            SDL_RenderCopy(renderer_, stateIcon, nullptr, &dst);
        }

        int textLeft = iconX + iconSize + 4;
        int textRight = cellX + BV_CELL_W - 4;
        drawTextCentered(label, (textLeft + textRight) / 2, cellY + BV_CELL_H / 2,
                         T().text, fontSmall_);
    }

    // Footer hint
    bool canRename = isDualBankMode() || (boxViewPanel_ == Panel::Bank);
    const std::string& footerStr = canRename
        ? i18n::get(StrKey::BoxViewFooterRename)
        : i18n::get(StrKey::BoxViewFooter);
    const char* footer = footerStr.c_str();
    drawTextCentered(footer, popX + popW / 2, popY + popH - 15, T().textDim, fontSmall_);

    // Box preview for cursor box (drawn last so it appears on top)
    drawBoxPreview(boxViewCursor_, cursorCellX, cursorCellY);
}

void UI::drawBoxPreview(int boxIdx, int anchorX, int anchorY) {
    int cols = gridColsFor(boxViewPanel_);
    int rows = 5;

    // Preview panel dimensions
    int previewInnerW = cols * BV_MINI_CELL + (cols - 1) * BV_MINI_PAD;
    int previewInnerH = rows * BV_MINI_CELL + (rows - 1) * BV_MINI_PAD;
    int previewW = previewInnerW + 2 * BV_PREVIEW_PAD;
    int previewH = previewInnerH + 2 * BV_PREVIEW_PAD + BV_PREVIEW_HDR;

    // Position: prefer below the cell
    int prevX = anchorX;
    int prevY = anchorY + BV_CELL_H + 6;

    // Clamp to screen bounds
    if (prevX + previewW > SCREEN_W - 4)
        prevX = SCREEN_W - 4 - previewW;
    if (prevX < 4)
        prevX = 4;
    if (prevY + previewH > SCREEN_H - 4)
        prevY = anchorY - previewH - 6; // flip above
    if (prevY < 4)
        prevY = 4;

    // Background
    drawRect(prevX, prevY, previewW, previewH, T().boxPreviewBg);
    drawRectOutline(prevX, prevY, previewW, previewH, T().textDim, 1);

    // Box name header
    std::string boxName;
    if (boxViewPanel_ == Panel::Game)
        boxName = (isDualBankMode()) ? bankLeft_.getBoxName(boxIdx) : save_.getBoxName(boxIdx);
    else
        boxName = bank_.getBoxName(boxIdx);
    drawTextCentered(boxName, prevX + previewW / 2,
                     prevY + BV_PREVIEW_HDR / 2 + 2, T().boxName, fontSmall_);

    // Mini sprite grid
    int gridX = prevX + BV_PREVIEW_PAD;
    int gridY = prevY + BV_PREVIEW_HDR;

    const auto& prevDisplays = getSlotDisplays(boxViewPanel_, boxIdx);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int slot = r * cols + c;
            int sx = gridX + c * (BV_MINI_CELL + BV_MINI_PAD);
            int sy = gridY + r * (BV_MINI_CELL + BV_MINI_PAD);

            const auto& psd = prevDisplays[slot];

            if (psd.empty) {
                drawRect(sx, sy, BV_MINI_CELL, BV_MINI_CELL, T().miniCellEmpty);
            } else {
                drawRect(sx, sy, BV_MINI_CELL, BV_MINI_CELL, T().miniCellFull);

                SDL_Texture* sprite = nullptr;
                if (psd.egg) {
                    sprite = getSprite(0);
                } else if (psd.shiny) {
                    sprite = getShinySprite(psd.species, psd.form);
                    if (!sprite) sprite = getSprite(psd.species, psd.form);
                } else {
                    sprite = getSprite(psd.species, psd.form);
                }
                if (sprite) {
                    int texW, texH;
                    SDL_QueryTexture(sprite, nullptr, nullptr, &texW, &texH);
                    float scale = std::min(float(BV_MINI_SPRITE) / texW,
                                           float(BV_MINI_SPRITE) / texH);
                    int dstW = static_cast<int>(texW * scale);
                    int dstH = static_cast<int>(texH * scale);
                    SDL_Rect dst = {
                        sx + (BV_MINI_CELL - dstW) / 2,
                        sy + (BV_MINI_CELL - dstH) / 2,
                        dstW, dstH
                    };
                    SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
                }

                // Search highlight on mini slots
                if (searchHighlightActive_) {
                    if (isSearchMatch(boxViewPanel_, boxIdx, slot))
                        drawRectOutline(sx, sy, BV_MINI_CELL, BV_MINI_CELL, T().searchMatch, 1);
                    else
                        drawRect(sx, sy, BV_MINI_CELL, BV_MINI_CELL, T().searchDim);
                }
            }
        }
    }
}

void UI::drawHeldOverlay() {
    if (!holding_)
        return;

    // Determine which Pokemon sprite to show
    const Pokemon& pkm = heldMulti_.empty() ? heldPkm_ : heldMulti_[0];
    uint16_t species = pkm.species();
    if (species == 0)
        return;

    uint8_t heldForm = pkm.form();
    SDL_Texture* sprite = nullptr;
    if (pkm.isEgg()) {
        sprite = getSprite(0);
    } else if (pkm.isShiny()) {
        sprite = getShinySprite(species, heldForm);
        if (!sprite) sprite = getSprite(species, heldForm);
    } else {
        sprite = getSprite(species, heldForm);
    }
    if (!sprite)
        return;

    // Party hand stays on strip: whenever you're on the party row while holding, follow the mini (box->party or party->party)
    int panelX = (cursor_.panel == Panel::Game) ? PANEL_X_L : PANEL_X_R;
    int cellX, cellY;
    bool isPartyHeld = holding_ && partyCursor_ >= 0;
    if (isPartyHeld) {
        // Recompute header text width like drawPanel does to place the minis (mini 24, pitch 28)
        std::string boxName = (cursor_.panel==Panel::Game) ? save_.getBoxName(gameBox_) : bank_.getBoxName(bankBox_);
        int totalBoxes = (cursor_.panel==Panel::Game) ? (isDualBankMode()? bankLeft_.boxCount(): save_.boxCount()) : bank_.boxCount();
        std::string left = boxName + " (" + std::to_string(gameBox_+1) + "/" + std::to_string(totalBoxes) + ") · OT " + save_.dsOtName();
        int tw = getTextEntry(left, fontSmall_, T().text).w;
        int mx = panelX + 45 + tw + 12 + partyCursor_ * 28;
        int hdrY = BOX_HDR_Y + (BOX_HDR_H - 24)/2;
        cellX = mx;
        cellY = hdrY;
    } else {
        int cols = gridCols();
        int gridStartX = panelX + (PANEL_W - (cols * (CELL_W + CELL_PAD) - CELL_PAD)) / 2;
        int gridStartY = GRID_Y;
        cellX = gridStartX + cursor_.col * (CELL_W + CELL_PAD);
        cellY = gridStartY + cursor_.row * (CELL_H + CELL_PAD);
    }

    // Offset to create "dragging" effect — proportional to cell size (party mini 24 vs box 96)
    int drag = isPartyHeld ? 3 : 8;
    int baseX = cellX + drag;
    int baseY = cellY + drag + (isPartyHeld ? -2 : 0);

    // Scale: party mini is 24, box is SPRITE_SIZE 68 — held from party stays small
    int baseSize = isPartyHeld ? 36 : SPRITE_SIZE;
    int texW, texH;
    SDL_QueryTexture(sprite, nullptr, nullptr, &texW, &texH);
    int dstW = baseSize, dstH = baseSize;
    if (texW > 0 && texH > 0) {
        float scale = std::min(static_cast<float>(baseSize) / texW,
                               static_cast<float>(baseSize) / texH);
        dstW = static_cast<int>(texW * scale);
        dstH = static_cast<int>(texH * scale);
    }

    int sprX, sprY;
    if (isPartyHeld) {
        sprX = baseX + (24 - dstW) / 2;
        sprY = baseY + (24 - dstH) / 2;
    } else {
        sprX = baseX + (CELL_W - dstW) / 2;
        sprY = baseY + 4 + (baseSize - dstH) / 2;
    }

    // Draw semi-transparent
    SDL_SetTextureAlphaMod(sprite, 180);
    SDL_Rect dst = {sprX, sprY, dstW, dstH};
    SDL_RenderCopy(renderer_, sprite, nullptr, &dst);
    SDL_SetTextureAlphaMod(sprite, 255);

    // Multi-hold: draw count badge
    if (!heldMulti_.empty() && heldMulti_.size() > 1) {
        std::string num = std::to_string(heldMulti_.size());
        auto& numE = getTextEntry(num, font_, T().textOnBadge);
        int tw = numE.w, th = numE.h;
        int badgeR = std::max(tw, th) / 2 + 6;
        int cx = cellX + CELL_W / 2;
        int cy = cellY + CELL_H / 2;

        SDL_SetRenderDrawColor(renderer_, T().selected.r, T().selected.g, T().selected.b, 220);
        for (int dy = -badgeR; dy <= badgeR; dy++) {
            int dx = static_cast<int>(std::sqrt(badgeR * badgeR - dy * dy));
            SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
        drawTextCentered(num, cx, cy, T().textOnBadge, font_);
    }
}

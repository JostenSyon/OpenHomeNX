#include "ui.h"
#include "ui_util.h"
#include "crypto_engine.h"
#include "pokemon_ffi.h"
#include "led.h"
#include "i18n.h"
#include "debug_log.h"
#include "autoupdate.h"
#include "remote_sync.h"
#include "settings_cfg.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <switch.h>
#ifdef OH_USB_UPDATE
#include <usbhsfs.h>
#endif

bool UI::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0)
        return false;

    if (TTF_Init() < 0) {
        SDL_Quit();
        return false;
    }

    if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & IMG_INIT_PNG) == 0) {
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    window_ = SDL_CreateWindow("OpenHomeNX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!window_) {
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Load font
    // NOTE: PlSharedFontType_Standard covers Latin, Cyrillic, and Japanese glyphs.
    // Korean (PlSharedFontType_KO) and Chinese (PlSharedFontType_ChineseSimplified /
    // PlSharedFontType_ChineseTraditional) require loading separate system fonts.
    // SDL_ttf does not support font fallback chains, so supporting these languages
    // would require switching the primary font based on the active language.
    PlFontData fontData;
    plInitialize(PlServiceType_System);
    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    SDL_RWops* rw = SDL_RWFromMem(fontData.address, fontData.size);
    font_ = TTF_OpenFontRW(rw, 0, 18);
    fontSmall_ = TTF_OpenFontRW(SDL_RWFromMem(fontData.address, fontData.size), 0, 14);
    fontLarge_ = TTF_OpenFontRW(SDL_RWFromMem(fontData.address, fontData.size), 0, 28);
    fontAbout_ = TTF_OpenFontRW(SDL_RWFromMem(fontData.address, fontData.size), 0, 20);

    if (!font_ || !fontSmall_) {
        if (!font_)
            font_ = TTF_OpenFont("romfs:/fonts/default.ttf", 18);
        if (!fontSmall_)
            fontSmall_ = TTF_OpenFont("romfs:/fonts/default.ttf", 14);
    }
    if (!fontLarge_)
        fontLarge_ = TTF_OpenFont("romfs:/fonts/default.ttf", 28);
    if (!fontAbout_)
        fontAbout_ = TTF_OpenFont("romfs:/fonts/default.ttf", 20);
    if (!fontAbout_) fontAbout_ = font_; // mai nullo al draw

    // Load status icons
    {
        const char* iconDir = "romfs:/icons/";
        auto loadIcon = [&](const char* name) -> SDL_Texture* {
            std::string path = std::string(iconDir) + name;
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return nullptr;
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
            SDL_FreeSurface(s);
            return t;
        };
        iconShiny_      = loadIcon("shiny.png");
        iconAlpha_      = loadIcon("alpha.png");
        iconShinyAlpha_ = loadIcon("shiny_alpha.png");
        iconBoxFull_     = loadIcon("box_full.png");
        iconBoxEmpty_    = loadIcon("box_empty.png");
        iconBoxNonEmpty_ = loadIcon("box_nonempty.png");
        iconVault_       = loadIcon("safe.png");
        iconEject_       = loadIcon("eject.png");
        iconSettings_    = loadIcon("settings.png");
        iconWifi_        = loadIcon("wifi.png");
        iconLan_         = loadIcon("lan.png");
        iconDebug_       = loadIcon("debug.png");
        iconArrow_       = loadIcon("arrow.png");
        iconPack_        = loadIcon("backpack.png");
        iconRocket_      = loadIcon("rocket.png");
        iconFloppy_      = loadIcon("floppy.png");
        iconTrade_       = loadIcon("trade.png");
        iconDevBox_      = loadIcon("devbox.png");
        iconDevSync_     = loadIcon("devsync.png");
        iconDevLink_     = loadIcon("devlink.png");
    }

    // Game-selector logos for imported (titleId-less) games: no NS control
    // data to fetch an icon from, so these come from romfs instead — the same
    // PNGs OpenHome upstream ships at public/logos/<Name>.png, downscaled to
    // a sane icon size (source was up to 1200x675, most of it never visible
    // at tile scale).
    {
        auto loadLogo = [&](const char* name) -> SDL_Texture* {
            std::string path = std::string("romfs:/logos/") + name + ".png";
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return nullptr;
            if (SDL_Surface* rr = roundCornersSurface(s, std::min(s->w, s->h) / 12)) {
                SDL_FreeSurface(s);
                s = rr;
            }
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
            SDL_FreeSurface(s);
            if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            return t;
        };
        gameLogoCache_[GameType::RUBY]     = loadLogo("Ruby");
        gameLogoCache_[GameType::SAPPHIRE] = loadLogo("Sapphire");
        gameLogoCache_[GameType::EMERALD]  = loadLogo("Emerald");
    }

    // HD box art for the tiles (user-provided PNGs, aspect-preserved).
    // Tries .png first, then .jpg (RBY art ships as optimized jpg).
    {
        auto loadArt = [&](GameType g, const char* name) -> SDL_Texture* {
            for (const char* ext : {".png", ".jpg"}) {
                std::string path = std::string("romfs:/boxart/") + name + ext;
                SDL_Surface* raw = IMG_Load(path.c_str());
                if (!raw) continue;
                // Solo RBY (full-bleed): pre-crop quadrato + maschera, il draw
                // ricampiona lo stesso quadrato. RSE (overlay SPR): texture
                // intera + maschera, la scala e tunata sulle dims originali.
                SDL_Surface* s = raw;
                bool fullBleed = (g == GameType::RED || g == GameType::BLUE || g == GameType::YELLOW);
                if (fullBleed) {
                    int side = std::min(raw->w, raw->h);
                    SDL_Surface* sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32, SDL_PIXELFORMAT_RGBA32);
                    if (sq) {
                        SDL_Rect src = {(raw->w - side) / 2, (raw->h - side) / 2, side, side};
                        if (SDL_BlitSurface(raw, &src, sq, nullptr) != 0) {
                            SDL_FreeSurface(sq);
                            sq = nullptr;
                        }
                    }
                    SDL_FreeSurface(raw);
                    s = sq;
                }
                if (!s) continue;
                if (SDL_Surface* rr = roundCornersSurface(s, std::min(s->w, s->h) / 12)) {
                    SDL_FreeSurface(s);
                    s = rr;
                }
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s);
                SDL_FreeSurface(s);
                if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                if (t) return t;
            }
            return nullptr;
        };
        boxArtCache_[GameType::RUBY]     = loadArt(GameType::RUBY, "ruby");
        boxArtCache_[GameType::SAPPHIRE] = loadArt(GameType::SAPPHIRE, "sapphire");
        boxArtCache_[GameType::EMERALD]  = loadArt(GameType::EMERALD, "emerald");
        boxArtCache_[GameType::RED]      = loadArt(GameType::RED, "red");
        boxArtCache_[GameType::BLUE]     = loadArt(GameType::BLUE, "blue");
        boxArtCache_[GameType::YELLOW]   = loadArt(GameType::YELLOW, "yellow");
    }

    // Tile backgrounds (user-provided). Missing file = flat color stays.
    // Stesso pre-crop quadrato delle boxart (il draw ricampiona al centro).
    {
        auto loadBg = [&](const char* name) -> SDL_Texture* {
            std::string path = std::string("romfs:/backgrounds/") + name + ".png";
            SDL_Surface* s = IMG_Load(path.c_str());
            if (!s) return nullptr;
            int side = std::min(s->w, s->h);
            SDL_Surface* sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32, SDL_PIXELFORMAT_RGBA32);
            if (sq) {
                SDL_Rect src = {(s->w - side) / 2, (s->h - side) / 2, side, side};
                if (SDL_BlitSurface(s, &src, sq, nullptr) != 0) {
                    SDL_FreeSurface(sq);
                    sq = nullptr;
                }
            }
            SDL_FreeSurface(s);
            if (!sq) return nullptr;
            if (SDL_Surface* rr = roundCornersSurface(sq, side / 12)) {
                SDL_FreeSurface(sq);
                sq = rr;
            }
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, sq);
            SDL_FreeSurface(sq);
            if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            return t;
        };
        tileBgCache_[GameType::EMERALD]  = loadBg("emerald");
        tileBgCache_[GameType::RUBY]     = loadBg("ruby");
        tileBgCache_[GameType::SAPPHIRE] = loadBg("sapphire");
    }

    // Open game controller
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad_ = SDL_GameControllerOpen(i);
            break;
        }
    }

    // Set default theme (persisted selection loaded in run())
    theme_ = &getTheme(0);

    return true;
}

void UI::shutdown() {
    clearTextCache();
    freeGameIcons();
    account_.freeTextures();
    freeSprites();
    if (fontLarge_) TTF_CloseFont(fontLarge_);
    if (fontAbout_) TTF_CloseFont(fontAbout_);
    if (fontSmall_) TTF_CloseFont(fontSmall_);
    if (font_) TTF_CloseFont(font_);
    if (pad_) SDL_GameControllerClose(pad_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    plExit();
}

void UI::showSplash(int holdMs, bool fadeOut) {
    if (!renderer_) return;

    const char* splashPath = "romfs:/splash.png";

    SDL_Surface* surf = IMG_Load(splashPath);
    if (!surf) return;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    int texW, texH;
    SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

    // Scale to fit screen while preserving aspect ratio
    float scale = std::min(static_cast<float>(SCREEN_W) / texW,
                           static_cast<float>(SCREEN_H) / texH);
    int dstW = static_cast<int>(texW * scale);
    int dstH = static_cast<int>(texH * scale);
    SDL_Rect dst = {(SCREEN_W - dstW) / 2, (SCREEN_H - dstH) / 2, dstW, dstH};

    // Hold splash (logo stays on screen; with fadeOut=false the last frame
    // persists while the caller keeps initializing — no black gap).
    Uint32 start = SDL_GetTicks();
    while ((int)(SDL_GetTicks() - start) < holdMs) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_DestroyTexture(tex);
                return;
            }
        }
        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }

    if (!fadeOut) {
        return; // texture cached, last logo frame stays up
    }

    // Fade out over ~0.5 seconds
    Uint32 fadeMs = 500;
    start = SDL_GetTicks();
    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_DestroyTexture(tex);
                return;
            }
        }
        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed >= fadeMs)
            break;
        int alpha = 255 - static_cast<int>(255 * elapsed / fadeMs);
        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha));
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
}

int UI::drawBodyText(const std::string& body, int startY, const std::string& footer) {
    int lineY = startY;
    const int maxW = SCREEN_W - 80;
    std::string remaining = body;
    while (!remaining.empty()) {
        size_t nl = remaining.find('\n');
        std::string line = (nl != std::string::npos) ? remaining.substr(0, nl) : remaining;
        // Word-wrap: righe troppo lunghe (URL update, ...) mai fuori schermo.
        for (auto& wl : wrapText(line, font_, maxW)) {
            drawTextCentered(wl, SCREEN_W / 2, lineY, T().textDim, font_);
            lineY += 24;
        }
        if (nl == std::string::npos) break;
        remaining = remaining.substr(nl + 1);
    }
    drawTextCentered(footer, SCREEN_W / 2, lineY + 20, T().textDim, fontSmall_);
    return lineY;
}

// Spezza una riga in piu righe entro maxW: prima per parole, poi per
// caratteri UTF-8 per le parole singole oltre il limite (URL lunghi).
std::vector<std::string> UI::wrapText(const std::string& line, TTF_Font* f, int maxW) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream iss(line);
    std::string w;
    auto width = [&](const std::string& t) { return getTextEntry(t, f, T().textDim).w; };
    auto flush = [&]() {
        if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    };
    while (iss >> w) {
        std::string cand = cur.empty() ? w : cur + " " + w;
        if (width(cand) <= maxW) {
            cur = cand;
        } else {
            flush();
            if (width(w) <= maxW) {
                cur = w;
            } else {
                // Parola oltre maxW: spezza a caratteri (UTF-8 safe).
                std::string part;
                for (size_t i = 0; i < w.size();) {
                    size_t len = 1;
                    unsigned char c = static_cast<unsigned char>(w[i]);
                    if ((c & 0x80) == 0) len = 1;
                    else if ((c & 0xE0) == 0xC0) len = 2;
                    else if ((c & 0xF0) == 0xE0) len = 3;
                    else if ((c & 0xF8) == 0xF0) len = 4;
                    std::string cand2 = part + w.substr(i, len);
                    if (!part.empty() && width(cand2) > maxW) {
                        out.push_back(part);
                        part = w.substr(i, len);
                    } else {
                        part = cand2;
                    }
                    i += len;
                }
                cur = part;
            }
        }
    }
    flush();
    if (out.empty()) out.push_back("");
    return out;
}

void UI::showMessageAndWait(const std::string& title, const std::string& body) {
    if (!renderer_) return;
    markDirty(); // Force redraw after modal returns

    bool waiting = true;
    while (waiting) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                waiting = false;
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B
                    waiting = false;
            }
        }

        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);

        drawTextCentered(title, SCREEN_W / 2, SCREEN_H / 2 - 40, T().red, fontLarge_);
        drawBodyText(body, SCREEN_H / 2 + 5, i18n::get(StrKey::PressBToDismiss));

        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
}

bool UI::showConfirmDialog(const std::string& title, const std::string& body) {
    if (!renderer_) return false;
    markDirty(); // Force redraw after modal returns

    int result = -1; // -1 = undecided
    while (result < 0) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                result = 0;
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) // Switch A = confirm
                    result = 1;
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B = cancel
                    result = 0;
            }
        }

        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);

        drawTextCentered(title, SCREEN_W / 2, SCREEN_H / 2 - 40, T().red, fontLarge_);
        drawBodyText(body, SCREEN_H / 2 + 5, i18n::get(StrKey::AContinueBCancel));

        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    return result == 1;
}

bool UI::showSyncCompareDialog(const std::string& gameName, const SyncSideInfo& local,
                                const SyncSideInfo& remote, bool remoteNewer,
                                const char* criterionKey) {
    if (!renderer_) return false;
    markDirty(); // Force redraw after modal returns

    constexpr int POP_W = 980;
    constexpr int POP_H = 440;
    constexpr int BOX_GAP = 24;
    constexpr int BOX_W = (POP_W - BOX_GAP) / 2;
    constexpr int BOX_H = 300;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    int leftX  = popX;
    int rightX = popX + BOX_W + BOX_GAP;
    int boxY = popY + 96;

    // "-> " nella direzione scelta cosi' si vede subito quale lato verra'
    // sovrascritto, non solo perche' (vedi criterionKey sotto).
    auto fmtRow = [&](int boxX, int rowY, const std::string& label, const std::string& value) {
        drawText(label, boxX + 20, rowY, T().textDim, fontSmall_);
        const auto& ve = getTextEntry(value, fontSmall_, T().text);
        drawText(value, boxX + BOX_W - 20 - (int)ve.w, rowY, T().text, fontSmall_);
    };
    auto fmtDate = [](long long unixTime) -> std::string {
        if (unixTime <= 0) return "--";
        time_t t = (time_t)unixTime;
        struct tm tmv;
        localtime_r(&t, &tmv);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        return buf;
    };
    auto fmtPlaytime = [](long secs) -> std::string {
        if (secs < 0) return "--";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%ldh %02ldm", secs / 3600, (secs % 3600) / 60);
        return buf;
    };
    auto fmtDex = [](const SyncSideInfo& s) -> std::string {
        if (!s.dexSupported) return "--";
        return std::to_string(s.dexCaught) + "/" + std::to_string(s.dexTotal);
    };

    int result = -1; // -1 = undecided
    while (result < 0) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = 0;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) result = 1; // Switch A = conferma
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) result = 0; // Switch B = annulla
            }
        }

        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);

        drawTextCentered(i18n::fmt(StrKey::DevSyncSyncTitle, gameName), SCREEN_W / 2, popY + 18, T().red, fontLarge_);
        drawTextCentered(i18n::fmt(StrKey::DevSyncCompareCriterion, i18n::get(criterionKey)),
                          SCREEN_W / 2, popY + 56, T().textDim, fontSmall_);

        for (int side = 0; side < 2; side++) {
            bool isLocal = side == 0;
            const SyncSideInfo& s = isLocal ? local : remote;
            int bx = isLocal ? leftX : rightX;
            bool isSource = isLocal ? !remoteNewer : remoteNewer;
            SDL_Color border = isSource ? T().cursor : T().textDim;

            drawRect(bx, boxY, BOX_W, BOX_H, T().panelBg);
            drawRectOutline(bx, boxY, BOX_W, BOX_H, border, isSource ? 3 : 1);

            drawTextCentered(i18n::get(isLocal ? StrKey::DevSyncCompareLocal : StrKey::DevSyncCompareRemote),
                              bx + BOX_W / 2, boxY + 24, isSource ? T().cursor : T().text, font_);
            if (isSource)
                drawTextCentered(i18n::get(StrKey::DevSyncCompareSource), bx + BOX_W / 2, boxY + 50, T().cursor, fontSmall_);

            int rowY = boxY + 96;
            fmtRow(bx, rowY, i18n::get(StrKey::FilterOT), s.trainer.empty() ? "--" : s.trainer);
            rowY += 32;
            fmtRow(bx, rowY, "Pokédex", fmtDex(s));
            rowY += 32;
            fmtRow(bx, rowY, i18n::get(StrKey::GalPlayTime), fmtPlaytime(s.playTimeSeconds));
            rowY += 32;
            fmtRow(bx, rowY, i18n::get(StrKey::DevSyncCompareSaveDate), fmtDate(s.modifiedUnix));
        }

        drawTextCentered(i18n::get(StrKey::AContinueBCancel), SCREEN_W / 2, popY + POP_H - 16, T().textDim, fontSmall_);

        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    return result == 1;
}

void UI::showLauncherPromptPopup() {
    if (!renderer_) return;
    markDirty();

    // Icona: romfs:/splash.png e' l'intero schermo di boot (1280x720,
    // logo + titolo + sottotitolo) -- schiacciarlo per intero in un'icona
    // lo rende illeggibile/deformato. Ritagliamo invece solo il marchio
    // "H" quadrato al centro (crop fisso, l'immagine di boot non cambia).
    SDL_Surface* surf = IMG_Load("romfs:/splash.png");
    SDL_Texture* icon = surf ? SDL_CreateTextureFromSurface(renderer_, surf) : nullptr;
    if (surf) SDL_FreeSurface(surf);
    SDL_Rect iconSrc = {520, 180, 240, 240};

    // Popup vero e proprio (riquadro centrato, come quello Impostazioni),
    // non piu' testo a piena pagina: tiene icona/titolo/corpo raccolti e
    // forza il wrap del testo alla larghezza del riquadro invece che
    // all'intero schermo.
    constexpr int POP_W = 680;
    constexpr int POP_H = 380;
    constexpr int ICON_SZ = 72;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    int maxTextW = POP_W - 80;

    int result = -1; // -1 = in attesa, 1 = installa (A), 0 = chiudi (B)
    while (result < 0) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) result = 0;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) // Switch A = installa
                    result = 1;
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) // Switch B = chiudi
                    result = 0;
            }
        }

        SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
        SDL_RenderClear(renderer_);

        drawRect(popX, popY, POP_W, POP_H, T().panelBg);
        drawRectOutline(popX, popY, POP_W, POP_H, T().cursor, 2);

        // Piu' respiro attorno al logo (era troppo a ridosso del bordo
        // superiore) e piu' distacco prima di titolo/corpo.
        int y = popY + 42;
        if (icon) {
            SDL_Rect dst = {popX + POP_W / 2 - ICON_SZ / 2, y, ICON_SZ, ICON_SZ};
            SDL_RenderCopy(renderer_, icon, &iconSrc, &dst);
            y += ICON_SZ + 30;
        }

        drawTextCentered(i18n::get(StrKey::LauncherPromptTitle), popX + POP_W / 2, y, T().text, fontLarge_);
        y += 44;

        for (auto& wl : wrapText(i18n::get(StrKey::LauncherPromptBody), font_, maxTextW)) {
            drawTextCentered(wl, popX + POP_W / 2, y, T().textDim, font_);
            y += 24;
        }

        drawTextCentered(i18n::get(StrKey::LauncherPromptFooter), popX + POP_W / 2, popY + POP_H - 26, T().textDim, fontSmall_);

        SDL_RenderPresent(renderer_);
        SDL_Delay(16);
    }
    if (icon) SDL_DestroyTexture(icon);
    if (result == 1) attemptLauncherForwarderInstall();
}

void UI::showWorking(const std::string& msg) {
    if (!renderer_) return;
    markDirty(); // Force redraw after modal returns

    SDL_SetRenderDrawColor(renderer_, T().bg.r, T().bg.g, T().bg.b, 255);
    SDL_RenderClear(renderer_);

    // Progress mode if msg contains "%": layout ordinato titolo / barra / stats,
    // ognuno nel suo slot senza sovrapposizioni (niente gear in questo modo).
    int pct = -1;
    {
        size_t p = msg.find('%');
        if (p != std::string::npos && p > 0) {
            size_t s = msg.rfind(' ', p);
            if (s != std::string::npos) {
                pct = std::atoi(msg.substr(s + 1, p - s - 1).c_str());
                if (pct < 0) pct = -1;
                if (pct > 100) pct = 100;
            }
        }
    }

    // Dark card behind gear + message — same box for every showWorking() state
    // (plain message, gear, and download progress) so the update flow doesn't
    // resize the popup between steps.
    constexpr int POP_W = 400;
    constexpr int POP_H = 160;
    int popX = (SCREEN_W - POP_W) / 2;
    int popY = (SCREEN_H - POP_H) / 2;
    drawRect(popX, popY, POP_W, POP_H, T().panelBg);
    drawRectOutline(popX, popY, POP_W, POP_H, T().textDim, 2);

    if (pct >= 0) {
        // "Downloading\n  45% ..." -> title / bar / stats, laid out to fit the
        // same 160px card as the plain popup.
        std::string title = msg, stats;
        size_t nl = msg.find('\n');
        if (nl != std::string::npos) {
            title = msg.substr(0, nl);
            stats = msg.substr(nl + 1);
            size_t f = stats.find_first_not_of(" \t");
            if (f != std::string::npos) stats = stats.substr(f);
        }
        drawTextCentered(title, SCREEN_W / 2, popY + 44, T().text, font_);
        constexpr int BAR_W = 300, BAR_H = 16;
        int barX = (SCREEN_W - BAR_W) / 2;
        int barY = popY + 78;
        drawRect(barX, barY, BAR_W, BAR_H, T().textDim);
        if (pct > 0)
            drawRect(barX + 2, barY + 2, (BAR_W - 4) * pct / 100, BAR_H - 4, T().arrow);
        if (!stats.empty())
            drawTextCentered(stats, SCREEN_W / 2, popY + 124, T().textDim, fontSmall_);
        SDL_RenderPresent(renderer_);
        return;
    }

    // Draw gear icon
    int gearCX = SCREEN_W / 2;
    int gearCY = popY + 58;
    constexpr int OUTER_R = 28;
    constexpr int INNER_R = 18;
    constexpr int HOLE_R  = 9;
    constexpr int TEETH    = 8;
    constexpr int TOOTH_W  = 12;
    constexpr int TOOTH_H  = 14;

    // Filled circle helper (scanline)
    auto fillCircle = [&](int cx, int cy, int r, SDL_Color c) {
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        for (int dy = -r; dy <= r; dy++) {
            int dx = static_cast<int>(std::sqrt(r * r - dy * dy));
            SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    };

    // Tooth rectangles around the gear (8 teeth at 45-degree intervals)
    SDL_Color gearColor = T().arrow;
    SDL_SetRenderDrawColor(renderer_, gearColor.r, gearColor.g, gearColor.b, gearColor.a);
    for (int i = 0; i < TEETH; i++) {
        double angle = i * (3.14159265 * 2.0 / TEETH);
        int tx = gearCX + static_cast<int>((INNER_R + TOOTH_H / 2) * std::cos(angle));
        int ty = gearCY + static_cast<int>((INNER_R + TOOTH_H / 2) * std::sin(angle));
        SDL_Rect tooth = {tx - TOOTH_W / 2, ty - TOOTH_W / 2, TOOTH_W, TOOTH_W};
        SDL_RenderFillRect(renderer_, &tooth);
    }

    // Gear body circle
    fillCircle(gearCX, gearCY, OUTER_R, gearColor);

    // Center hole
    fillCircle(gearCX, gearCY, HOLE_R, T().panelBg);

    // Message text below gear: wrap entro la card (max 2 righe), mai fuori.
    auto lines = wrapText(msg, font_, POP_W - 40);
    int ty = popY + POP_H - 32 - (int)(lines.size() > 1 ? (lines.size() - 1) * 22 : 0);
    for (size_t i = 0; i < lines.size() && i < 2; i++)
        drawTextCentered(lines[i], SCREEN_W / 2, ty + (int)i * 22, T().text, font_);

    SDL_RenderPresent(renderer_);
}

void UI::run(const std::string& basePath, const std::string& savePath) {
    basePath_ = basePath;
    savePath_ = savePath;
    Settings::init(basePath_); // settings.cfg (+ migrate legacy una tantum)
    uint32_t runT0 = SDL_GetTicks();
    auto runMark = [&](const char* what) {
        DebugLog::line("boot: +%ums %s", SDL_GetTicks() - runT0, what);
    };

    // Consolidate a pending self-update before anything else, so a launch from
    // hbmenu/forwarder always ends up on the freshly installed .nro. When it
    // returns true we are running from the throw-away .nro.new and the real
    // .nro has just been written: bounce straight into it (one quick
    // "Updating…" frame) instead of letting the user touch the .new instance.
    // Safety net: main() normally does this via tryUpdateBounce() before we get
    // here, so on a normal boot finalizePendingUpdate() returns false and this
    // is skipped. Kept in case the pending .new appears after main()'s check.
    if (finalizePendingUpdate()) {
        showWorking(i18n::get(StrKey::UpdateUpdating));
        SDL_Delay(150);
        return;  // main() cleans up -> libnx exit chainloads nextLoad (the real .nro)
    }

    // Load persisted theme
    themeIndex_ = loadThemeIndex(basePath_);
    zoomGrow_ = loadZoomGrow(basePath_);
    gameSelectorLayout_ = (GameSelectorLayout)loadGameSelectorLayout(basePath_);
    theme_ = &getTheme(themeIndex_);
    loadFavorites();
    galPreloadCacheFromDisk(); // party/OT/dex subito al primo draw, niente "..."

    // Load persisted crypto engine (PK/OH)
    int cryptoVal = loadCryptoEngine(basePath_);
    g_cryptoEngine = (cryptoVal == 1) ? CryptoEngine::OH : CryptoEngine::PK;

    // Load import-path settings (which folders to scan for emulator saves —
    // always seeds basePath_+"import/" if the config is missing/empty).
    importPaths_ = loadImportPaths(basePath_);
    autoCheckUsb_ = loadAutoCheckUsb(basePath_);
    for (auto& e : importPaths_)
        DebugLog::line("boot: import path [%s] %s", e.enabled ? "on" : "off", e.path.c_str());

    // All games in menu order
    constexpr GameType allGames[] = {
        GameType::GP, GameType::GE, GameType::Sw, GameType::Sh,
        GameType::BD, GameType::SP, GameType::LA, GameType::S,
        GameType::V, GameType::ZA, GameType::FR, GameType::LG,
        GameType::FR_ES, GameType::LG_ES, GameType::FR_DE, GameType::LG_DE,
        GameType::FR_IT, GameType::LG_IT, GameType::FR_FR, GameType::LG_FR,
        GameType::FR_JA, GameType::LG_JA
    };

    // Fill availableGames_ with only the games actually present on the console
    // (installed, or with account savedata). Applet mode / the no-profile
    // fallback used to list all 22 GameType entries, so uninstalled titles
    // (all 12 FireRed/LeafGreen locales, Legends Z-A) showed up as icon-less
    // phantom rows. If enumeration returns nothing, fall back to listing all.
    auto fillPresentGames = [&]() {
        std::set<uint64_t> present = account_.presentApplications();
        availableGames_.clear();
        if (present.empty()) {
            availableGames_.assign(std::begin(allGames), std::end(allGames));
        } else {
            for (GameType g : allGames)
                if (present.count(titleIdOf(g)))
                    availableGames_.push_back(g);
            if (availableGames_.empty())
                availableGames_.assign(std::begin(allGames), std::end(allGames));
        }
        appendImportedGames();
        applyFavoritesOrder();
    };

    if (appletMode_) {
        // Applet mode: skip profile, bank-only access
        screen_ = AppScreen::GameSelector;
        fillPresentGames();
        refreshBankCounts();
        showWorking(i18n::get(StrKey::LoadingGameIcons));
        runMark("prima icone");
        loadGameIcons();
        runMark("icone pronte");
        showMessageAndWait(i18n::get(StrKey::AppletTitle),
            i18n::get(StrKey::AppletBody));
    } else {
        showWorking(i18n::get(StrKey::LoadingProfiles));
        runMark("prima profili");
        if (account_.init() && account_.loadProfiles(renderer_)) {
            runMark("profili pronti");
            // Utente predefinito: salta il selettore profili (torna a chiedere
            // se il profilo non esiste piu o non ha save).
            int autoIdx = defaultUserIndex();
            if (autoIdx >= 0) {
                selectProfile(autoIdx);
                DebugLog::line("boot: utente predefinito #%d", autoIdx);
            }
            if (autoIdx < 0 || availableGames_.empty())
                screen_ = AppScreen::ProfileSelector;
        } else {
            runMark("no profili");
            screen_ = AppScreen::GameSelector;
            fillPresentGames();
            refreshBankCounts();
            showWorking(i18n::get(StrKey::LoadingGameIcons));
            runMark("prima icone");
            loadGameIcons();
            runMark("icone pronte");
        }
    }

    // Rilevamento mGBA una tantum (v1): fatto qui cosi' e' gia' pronto per
    // il tasto rapido in Galleria, non solo quando si apre Impostazioni.
    ensureMgbaChecked();

    bool running = true;

    while (running) {
        // Diagnosi worker auto-update su qualsiasi screen (una riga per boot).
        autoUpdateLogOnceDone();
        // Auto-update al boot: se il thread in parallelo ha trovato una build
        // più recente, lancia il flusso update normale una sola volta quando
        // siamo nella home giochi o utenti (mai durante il boot, mai due volte).
        // Popup di scoperta "Installa launcher": valutato PRIMA del blocco
        // autoupdate qui sotto apposta, perche' autoUpdateTakeResult() (nel
        // blocco successivo) consuma subito lo stato non appena lo vede a
        // "done" -- se controllassimo autoUpdateFinishedWithoutUpdate() dopo,
        // lo troveremmo gia' azzerato nello stesso frame. One-shot per boot
        // (launcherPromptChecked_, solo RAM); se questo boot trova un
        // update invece, salta il turno e si ririprova al prossimo boot
        // pulito -- il "visto per sempre" vero resta su file.
        if (!launcherPromptChecked_ && (screen_ == AppScreen::GameSelector ||
                                        screen_ == AppScreen::ProfileSelector) &&
            autoUpdateFinishedWithoutUpdate()) {
            launcherPromptChecked_ = true;
            // Niente controllo su appletMode_: Sphaira crea il forwarder
            // anche avviato da Album (R su un gioco), quindi non e' un
            // prerequisito reale -- coerente con la voce Impostazioni,
            // che infatti non lo controlla piu' nemmeno lei.
            if (!hasSeenLauncherPrompt()) {
                showLauncherPromptPopup();
                markLauncherPromptSeen();
            }
        }
        if (!autoPrompted_ && (screen_ == AppScreen::GameSelector ||
                               screen_ == AppScreen::ProfileSelector)) {
            std::string newVer;
            if (autoUpdateTakeResult(newVer)) {
                autoPrompted_ = true;
                DebugLog::line("autoupdate: trovata v%s, apro check", newVer.c_str());
                markDirty();
                if (checkForUpdate(false)) {
                    running = false;
                    break;
                }
            }
        }
        // Scoperta automatica del device remoto (worker in background --
        // vedi il commento sopra la sua implementazione in remote_sync.cpp).
        // Parte SOLO dopo che l'autoupdate si e' sistemato (autoUpdateSettled:
        // mai partito, o finito comunque vada) -- l'aggiornamento viene
        // sempre prima e non deve mai competere per la rete con questo
        // worker (bug reale, gia' visto: vedi il commento sul fallback
        // rimosso in remoteSyncWorkerMain). remoteSyncWorkerStart() e' lei
        // stessa no-op dalla seconda chiamata in poi, quindi richiamarla ogni
        // frame una volta sistemato l'autoupdate costa solo una letture
        // atomica in piu'. Poll non bloccante, una volta per frame, su
        // qualsiasi schermata: costa solo una letture atomica nel caso
        // comune (Pending). Su Found si ricorda l'host (stesso posto in cui
        // lo salva il flusso manuale a fine login) cosi' la prossima
        // apertura di RemoteBox/DevSync lo trova gia' pronto.
        if (autoUpdateSettled()) {
            remoteSyncWorkerStart();
            std::string foundHost, foundToken;
            RemoteSyncWorkerResult wr = remoteSyncWorkerPoll(foundHost, foundToken);
            if (wr == RemoteSyncWorkerResult::Found) {
                remoteDeviceAvailable_ = true;
                remoteDeviceHost_ = foundHost;
                remoteDeviceToken_ = foundToken;
                Settings::setRemoteSyncHost(foundHost);
                markDirty();
                DebugLog::line("remote sync: device trovato in background su %s", foundHost.c_str());
            } else if (wr == RemoteSyncWorkerResult::NotFound) {
                DebugLog::line("remote sync: ricerca automatica in background senza esito");
            }
            // Il device trovato potrebbe essere sparito nel frattempo
            // (Switch/box remoto riacceso, cambio rete, ecc.):
            // remoteSyncWorkerStart() tenta la scoperta una sola volta per
            // boot (vedi il commento sopra), quindi senza questo controllo
            // l'app lo considererebbe "trovato" per sempre. Ping leggero
            // gestito internamente da remoteSyncWatchdogCheck(): al massimo
            // una volta ogni un paio di minuti e MAI durante un uso attivo.
            if (remoteDeviceAvailable_ && !remoteDeviceHost_.empty()) {
                if (!remoteSyncWatchdogCheck(remoteDeviceHost_)) {
                    remoteDeviceAvailable_ = false;
                    markDirty();
                }
            }
        }
        // About popup intercepts input from any screen
        if (showAbout_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { running = false; break; }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK ||
                        event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
                        { showAbout_ = false; markDirty(); }
                }
            }
            if (!showAbout_) continue; // dismissed — let main draw section handle it
            if (dirty_) {
                dirty_ = false; // prima del draw: i markDirty durante il draw restano
                if (theme_ != lastTheme_) { clearTextCache(); lastTheme_ = theme_; }
                // Draw the underlying screen, then about popup on top
                if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
                else if (screen_ == AppScreen::GameSelector) {
                    drawGameSelectorFrame();
                    if (showGameSelMenu_) drawGameSelMenuPopup();
                    if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                }
                else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
                else drawFrame();
                drawAboutPopup();
                SDL_RenderPresent(renderer_);
            }
            SDL_Delay(16);
            continue;
        }

        // Theme selector intercepts input from any screen
        if (showThemeSelector_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { running = false; break; }
                if (event.type == SDL_CONTROLLERAXISMOTION) {
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                        event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                        int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                        updateStick(lx, ly);
                    }
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    markDirty();
                    switch (event.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            themeSelCursor_ = (themeSelCursor_ + THEME_COUNT - 1) % THEME_COUNT;
                            theme_ = &getTheme(themeSelCursor_);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            themeSelCursor_ = (themeSelCursor_ + 1) % THEME_COUNT;
                            theme_ = &getTheme(themeSelCursor_);
                            break;
                        case SDL_CONTROLLER_BUTTON_B: // Switch A = confirm
                            themeIndex_ = themeSelCursor_;
                            theme_ = &getTheme(themeIndex_);
                            saveThemeIndex(basePath_, themeIndex_);
                            showThemeSelector_ = false;
                            showMenu_ = false;
                            break;
                        case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                        case SDL_CONTROLLER_BUTTON_X: // Switch Y = cancel
                            themeIndex_ = themeSelOriginal_;
                            theme_ = &getTheme(themeIndex_);
                            showThemeSelector_ = false;
                            break;
                    }
                }
            }
            // Joystick repeat
            if (stickDirY_ != 0) {
                uint32_t now = SDL_GetTicks();
                uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
                if (now - stickMoveTime_ >= delay) {
                    themeSelCursor_ = (themeSelCursor_ + (stickDirY_ > 0 ? 1 : THEME_COUNT - 1)) % THEME_COUNT;
                    theme_ = &getTheme(themeSelCursor_);
                    stickMoveTime_ = now;
                    stickMoved_ = true;
                    markDirty();
                }
            }
            if (!showThemeSelector_) continue; // dismissed — let main draw section handle it
            if (dirty_) {
                dirty_ = false; // prima del draw: i markDirty durante il draw restano
                if (theme_ != lastTheme_) { clearTextCache(); lastTheme_ = theme_; }
                // Draw the underlying screen, then theme popup on top
                if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
                else if (screen_ == AppScreen::GameSelector) {
                    drawGameSelectorFrame();
                    if (showGameSelMenu_) drawGameSelMenuPopup();
                    if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                }
                else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
                else drawFrame();
                drawThemeSelectorPopup();
                SDL_RenderPresent(renderer_);
            }
            SDL_Delay(16);
            continue;
        }

        // Folder browser (opened from "+ Add path...") sits on top of the
        // import settings popup. Always redraws while open: the import block
        // below clears dirty_ in the same frame the browser opens, so a
        // dirty_-gated draw here would never fire (black popup bug).
        if (showFolderBrowser_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { running = false; break; }
                handleFolderBrowserInput(event);
            }
            if (!showFolderBrowser_) continue; // dismissed — let main draw section handle it
            // Hold-repeat for DPad (physical state) and left stick: initial
            // 400ms, then 70ms steps, so long lists scroll fast.
            {
                uint32_t now = SDL_GetTicks();
                int holdDir = 0;
                if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) holdDir = 1;
                else if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_UP)) holdDir = -1;
                int stickDir = 0;
                if (holdDir == 0 && pad_) {
                    int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                    if (ly > 12000) stickDir = 1;
                    else if (ly < -12000) stickDir = -1;
                }
                int dir = (holdDir != 0) ? holdDir : stickDir;
                if (dir == 0) {
                    folderRepeatDir_ = 0;
                    folderRepeatFast_ = false;
                } else {
                    if (folderRepeatDir_ == 0) {
                        // First step immediately (like BUTTONDOWN), then repeat.
                        folderMoveCursor(dir);
                        folderRepeatDir_ = dir;
                        folderRepeatTime_ = now;
                        folderRepeatFast_ = false;
                    } else if (dir == folderRepeatDir_) {
                        uint32_t delay = folderRepeatFast_ ? 70 : 400;
                        if (now - folderRepeatTime_ >= delay) {
                            folderMoveCursor(dir);
                            folderRepeatTime_ = now;
                            folderRepeatFast_ = true;
                        }
                    }
                }
            }
            if (theme_ != lastTheme_) { clearTextCache(); lastTheme_ = theme_; }
            if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
            else if (screen_ == AppScreen::GameSelector) {
                drawGameSelectorFrame();
                if (showRadialMenu_) drawRadialMenu();
                if (showGameSelMenu_) drawGameSelMenuPopup();
                if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                if (showTradeList_) drawTradeListPopup();
            }
            else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
            else drawFrame();
            drawImportSettingsPopup();
            drawFolderBrowserPopup();
            SDL_RenderPresent(renderer_);
            SDL_Delay(16);
            continue;
        }

        // Import-path settings intercepts input from any screen
        if (showImportSettings_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { running = false; break; }
                if (event.type == SDL_CONTROLLERAXISMOTION) {
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                        event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                        int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                        updateStick(lx, ly);
                    }
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    markDirty();
                    // Row 0 = autoCheckUsb_ toggle, rows 1..N = importPaths_
                    // (path index = cursor-1), row N+1 = "Add path...".
                    int rows = (int)importPaths_.size() + 2;
                    switch (event.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            importSettingsCursor_ = (importSettingsCursor_ + rows - 1) % rows;
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            importSettingsCursor_ = (importSettingsCursor_ + 1) % rows;
                            break;
                        case SDL_CONTROLLER_BUTTON_B: // Switch A = toggle / add
                            if (importSettingsCursor_ == 0) {
                                autoCheckUsb_ = !autoCheckUsb_;
                                saveAutoCheckUsb(basePath_, autoCheckUsb_);
                            } else if (importSettingsCursor_ == (int)importPaths_.size() + 1) {
                                // "+ Add path..." — folder browser instead of
                                // swkbd (typing paths with a pad is painful).
                                openFolderBrowser();
                            } else {
                                int pi = importSettingsCursor_ - 1;
                                importPaths_[pi].enabled = !importPaths_[pi].enabled;
                                saveImportPaths(basePath_, importPaths_);
                            }
                            break;
                        case SDL_CONTROLLER_BUTTON_Y: // Switch X = remove (the default import/ folder can't be removed)
                            if (importSettingsCursor_ >= 1 && importSettingsCursor_ <= (int)importPaths_.size()) {
                                int pi = importSettingsCursor_ - 1;
                                const std::string defaultPath = basePath_ + "import/";
                                if (importPaths_[pi].path != defaultPath) {
                                    importPaths_.erase(importPaths_.begin() + pi);
                                    saveImportPaths(basePath_, importPaths_);
                                    if (importSettingsCursor_ > 1) importSettingsCursor_--;
                                }
                            }
                            break;
                        case SDL_CONTROLLER_BUTTON_A: // Switch B = close
                        case SDL_CONTROLLER_BUTTON_X:
                        case SDL_CONTROLLER_BUTTON_BACK:
                            showImportSettings_ = false;
                            // Rescan right away — otherwise a path/toggle just
                            // changed here wouldn't show up until the next
                            // profile reselect or USB hotplug, which isn't
                            // discoverable from this screen.
                            rescanImportedGames();
                            if (importFromSettings_) {
                                importFromSettings_ = false;
                                showSettings_ = true; // torna dov'eri (cat/row intatti)
                                markDirty();
                            }
                            break;
                    }
                }
            }
            if (stickDirY_ != 0) {
                uint32_t now = SDL_GetTicks();
                uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
                if (now - stickMoveTime_ >= delay) {
                    int rows = (int)importPaths_.size() + 2;
                    importSettingsCursor_ = (importSettingsCursor_ + (stickDirY_ > 0 ? 1 : rows - 1)) % rows;
                    stickMoveTime_ = now;
                    stickMoved_ = true;
                    markDirty();
                }
            }
            if (!showImportSettings_) continue; // dismissed — let main draw section handle it
            if (dirty_) {
                dirty_ = false; // prima del draw: i markDirty durante il draw restano
                if (theme_ != lastTheme_) { clearTextCache(); lastTheme_ = theme_; }
                if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
                else if (screen_ == AppScreen::GameSelector) {
                    drawGameSelectorFrame();
                    if (showGameSelMenu_) drawGameSelMenuPopup();
                    if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                }
                else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
                else drawFrame();
                drawImportSettingsPopup();
                SDL_RenderPresent(renderer_);
            }
            SDL_Delay(16);
            continue;
        }

        // Language selector intercepts input from any screen
        if (showLanguageSelector_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { running = false; break; }
                if (event.type == SDL_CONTROLLERAXISMOTION) {
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                        event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
                        int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
                        updateStick(lx, ly);
                    }
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    markDirty();
                    int langCount = (int)langList_.size();
                    switch (event.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            langSelCursor_ = (langSelCursor_ + langCount - 1) % langCount;
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            langSelCursor_ = (langSelCursor_ + 1) % langCount;
                            break;
                        case SDL_CONTROLLER_BUTTON_B: { // Switch A = confirm
                            std::string newLang = langList_[langSelCursor_];
                            i18n::init(newLang);
                            clearTextCache();
                            // Persist choice
                            Settings::setLanguage(newLang);
                            showLanguageSelector_ = false;
                            showMenu_ = false;
                            break;
                        }
                        case SDL_CONTROLLER_BUTTON_A: // Switch B = cancel
                        case SDL_CONTROLLER_BUTTON_X: // Switch Y = cancel
                            showLanguageSelector_ = false;
                            break;
                    }
                }
            }
            // Joystick repeat
            if (stickDirY_ != 0 && !langList_.empty()) {
                uint32_t now = SDL_GetTicks();
                uint32_t delay = stickMoved_ ? STICK_REPEAT_DELAY : STICK_INITIAL_DELAY;
                if (now - stickMoveTime_ >= delay) {
                    int langCount = (int)langList_.size();
                    langSelCursor_ = (langSelCursor_ + (stickDirY_ > 0 ? 1 : langCount - 1)) % langCount;
                    stickMoveTime_ = now;
                    stickMoved_ = true;
                    markDirty();
                }
            }
            if (!showLanguageSelector_) continue;
            if (dirty_) {
                dirty_ = false; // prima del draw: i markDirty durante il draw restano
                if (theme_ != lastTheme_) { clearTextCache(); lastTheme_ = theme_; }
                if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
                else if (screen_ == AppScreen::GameSelector) {
                    drawGameSelectorFrame();
                    if (showGameSelMenu_) drawGameSelMenuPopup();
                    if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                }
                else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
                else drawFrame();
                drawLanguageSelectorPopup();
                SDL_RenderPresent(renderer_);
            }
            SDL_Delay(16);
            continue;
        }

#ifdef OH_USB_UPDATE
        // Hotplug: a USB drive inserted while on the game selector auto-runs the
        // update check. Cheap count poll, rising edge only.
        if (screen_ == AppScreen::GameSelector && !showGameSelMenu_) {
            static u32 s_lastUsbCount = 0;
            static u32 s_lastUsbPhys  = 0;
            u32 usbCount = usbHsFsGetMountedDeviceCount();
            u32 usbPhys  = usbHsFsGetPhysicalDeviceCount();
            if (usbCount != s_lastUsbCount || usbPhys != s_lastUsbPhys) {
                DebugLog::line("usb hotplug: physical %u->%u mounted %u->%u",
                               s_lastUsbPhys, usbPhys, s_lastUsbCount, usbCount);
                if (DebugLog::enabled() && usbPhys > 0 && usbCount == 0)
                    DebugLog::line("usb hotplug: drive present but no FAT volume mounted (blank MBR? reformat MBR+FAT32)");
            }
            bool rising = (usbCount > s_lastUsbCount);
            bool falling = (usbCount < s_lastUsbCount);
            s_lastUsbCount = usbCount;
            s_lastUsbPhys  = usbPhys;
            // Only once per session: a drive being re-seated shouldn't keep
            // re-entering the full update flow (dialogs + possible restart).
            static bool s_didHotplugCheck = false;
            if (rising && !s_didHotplugCheck) {
                s_didHotplugCheck = true;
                DebugLog::line("usb hotplug: running update check (USB-only)");
                if (checkForUpdate(true)) { running = false; break; }
                markDirty();
            }
            // Import scan re-runs on every insertion (not once-per-session
            // like the update check above): the user may swap in a different
            // drive with different emulator saves later in the same session.
            // SD-configured import paths appear passively next time the game
            // list is (re)built; a "usb:" path only ever resolves once a
            // drive is actually mounted, so this is the one that needs an
            // explicit rescan + popup on hotplug.
            if (rising)
                rescanImportedGames();
            // Falling edge (drive pulled or lost): USB games must leave the
            // list or A-press would open stale entries. Silent by design —
            // rescanImportedGames() popups only on newly found games.
            if (falling) {
                DebugLog::line("usb hotplug: drive removed, rescanning imports");
                rescanImportedGames();
                markDirty();
            }
        }
#endif

        AppScreen screenBefore = screen_;
        if (screen_ == AppScreen::ProfileSelector) {
            handleProfileSelectorInput(running);
        } else if (screen_ == AppScreen::GameSelector) {
            handleGameSelectorInput(running);
            // Ticker animazione pulsanti bassi: va avanti anche senza input
            // (es. chiavetta inserita a schermo fermo), 60fps dal loop.
            if (bottomButtonsAnim()) markDirty();
        } else if (screen_ == AppScreen::BankSelector) {
            handleBankSelectorInput(running);
        } else {
            handleInput(running);
            if (saveNow_) {
                if (!confirmQuitWithHold()) {
                    // Quit con mano occupata + B: resta, niente save.
                    saveNow_ = false;
                    running = true;
                } else if (!saveBankFiles()) {
                    saveNow_ = false;
                    running = true;
                } else if (!persistGameSaveIfDirty()) {
                    // Save & Quit abortito (party vuota + B al dialogo):
                    // resta nel gioco, niente save, memoria intatta.
                    saveNow_ = false;
                    running = true;
                } else {
                    saveNow_ = false;
                }
            }
        }
        // running just went false (quit, or checkForUpdate() is about to
        // restart us): skip the redraw below entirely. Otherwise this same
        // iteration still falls through to it before the while() condition is
        // re-checked, drawing one more frame of the screen underneath — which
        // for an update meant a flash of the game selector overwriting the
        // "Updating…" card checkForUpdate() had just presented, right before
        // the process exits. Whatever was last presented (or nothing) stays.
        if (!running) break;

        // Screen transition always triggers redraw
        if (screen_ != screenBefore)
            markDirty();
        // If a popup just activated, skip drawing here — the popup branch
        // will handle it next iteration with dirty_ still set.
        if (dirty_ && !showAbout_ && !showThemeSelector_ && !showLanguageSelector_ && !showImportSettings_) {
            dirty_ = false; // prima del draw: i markDirty durante il draw restano
            if (theme_ != lastTheme_) {
                clearTextCache();
                lastTheme_ = theme_;
            }
            if (screen_ == AppScreen::ProfileSelector) drawProfileSelectorFrame();
            else if (screen_ == AppScreen::GameSelector) {
                drawGameSelectorFrame();
                if (showRadialMenu_) drawRadialMenu();
                if (showGameSelMenu_) drawGameSelMenuPopup();
                if (showGamePick_) drawGamePickPopup();
                    if (showSettings_) drawSettingsPopup();
                if (showSaveMenu_) drawSaveMenuPopup();
                if (showBackpack_) drawBackpackPopup();
                if (showBackupList_) drawBackupListPopup();
                if (showCrashList_) drawCrashListPopup();
                if (showTradeList_) drawTradeListPopup();
            }
            else if (screen_ == AppScreen::BankSelector) drawBankSelectorFrame();
            else drawFrame();
            SDL_RenderPresent(renderer_);
        }
        SDL_Delay(16);
    }
    DebugLog::line("run loop exit (clean)");

    account_.unmountSave();
    account_.shutdown();
}

void UI::selectGame(GameType game, int occurrence) {
    // Backup del gioco precedente se modificato (prima di cambiare).
    backupOnExitIfNeeded();
    exitBackedUp_ = false;
    uint32_t openT0 = SDL_GetTicks();
    selectedGame_ = game;
    partyCursor_ = -1; // save changes: drop any OT-strip focus
    detailParty_ = -1;
    invalidateAllSlotDisplays();
    availableSpecies_.clear(); // rebuild on next species picker open
    save_.setGameType(game);
    bankLeft_.setGameType(game);

    if (isDualBankMode()) {
        // Switching game in dual/applet browsing: no bank is open for the new
        // game. Clearing both sides is what stops a stale activeBankName_ from
        // a previous game from tripping the "already open" check (and from
        // making B in the bank list jump to the main view).
        leftBankName_.clear();
        leftBankPath_.clear();
        activeBankName_.clear();
        activeBankPath_.clear();
        bankSelTarget_ = Panel::Bank;
    }

    if (!isDualBankMode()) {
        showWorking(i18n::get(StrKey::LoadingSaveData));
        activeSaveIsRemote_ = false;

        if (isImportedFile(game) || isGen1File(game) || isGen2File(game) ||
            isGen45File(game) || isGen6XY(game) || isGen6ORAS(game) || isGen7SM(game) || isGen7USUM(game)) {
            // File-backed game (scanned emulator save) — no titleId, no
            // AccountManager mount/backup: load straight from the resolved
            // path found by appendImportedGames(). Read/write both go
            // through this same file (SaveFile::load()/save() already route
            // GBA through loadGBA()/saveGBA(), GB through loadGB()/saveGB()
            // and GBC through loadGBC()/saveGBC), so GBA writes
            // land directly on the user's own emulator save. Tutte le famiglie
            // file-backed sono scrivibili (GB/GBC/GBA/DS/3DS con write-back +
            // CRC ricalcolati dove servono).
            savePath_ = importedSavePath(game, occurrence);
            if (savePath_.empty()) {
                showMessageAndWait(i18n::get(StrKey::MountError), i18n::get(StrKey::FailedMountSave));
                return;
            }
            // Box Remoto: questo file temporaneo arriva da un save remoto
            // (vedi UI::openRemoteBox) -- segna la provenienza cosi'
            // UI::returnToGameSelector() sappia rispedirlo al device in
            // uscita, con conferma, invece di considerarlo un file locale
            // qualunque.
            if (remoteBoxActive_) {
                for (auto& e : remoteBoxEntries_) {
                    if (e.type == game && e.tmpPath == savePath_) {
                        activeSaveIsRemote_ = true;
                        activeRemoteHost_ = e.host;
                        activeRemoteToken_ = e.token;
                        activeRemoteSavePath_ = e.remoteSavePath;
                        break;
                    }
                }
            }
            // Come i titoli installati (backupSaveDir sopra), anche i save
            // file-backed meritano un auto-backup all'apertura: finora non ne
            // esisteva nessuno (da qui "non trova save"). Best-effort, mai
            // dialoghi: cap 10, solo log.
            if (autoBackupEntries(game).empty())
                autoBackupFileSave(game, savePath_);
            else
                DebugLog::line("save: open senza backup (non first-ever)");
        } else if (selectedProfile_ >= 0) {
            std::string mountPath = account_.mountSave(selectedProfile_, game);
            if (mountPath.empty()) {
                showMessageAndWait(i18n::get(StrKey::MountError), i18n::get(StrKey::FailedMountSave));
                return;
            }
            savePath_ = mountPath + saveFileNameOf(game);

            // Spazio: stat del singolo file (mai walk della dir).
            struct stat saveSt;
            size_t saveSize = 0;
            if (stat(savePath_.c_str(), &saveSt) == 0)
                saveSize = (size_t)saveSt.st_size;
            bool doBackup = true;

            struct statvfs vfs;
            if (saveSize > 0 && statvfs("sdmc:/", &vfs) == 0) {
                size_t freeSpace = (size_t)vfs.f_bavail * vfs.f_bsize;
                if (freeSpace < saveSize * 2) {
                    std::string msg = i18n::fmt(StrKey::LowStorageBody, formatSize(freeSpace), formatSize(saveSize));
                    if (!showConfirmDialog(i18n::get(StrKey::LowStorage), msg)) {
                        account_.unmountSave();
                        return;
                    }
                    doBackup = false;
                }
            }

            if (doBackup) {
                // All'apertura SOLO first-ever (niente backup a ogni open:
                // da ora li fa l'uscita se modificato). Check = readdir.
                if (autoBackupEntries(game).empty()) {
                    std::string backupDir = buildBackupDir(game);
                    ledBlink();
                    bool ok = AccountManager::backupSaveDir(mountPath, backupDir);
                    ledOff();
                    if (!ok) {
                        if (!showConfirmDialog(i18n::get(StrKey::BackupFailed),
                                i18n::get(StrKey::BackupFailedBody))) {
                            account_.unmountSave();
                            return;
                        }
                    } else {
                        writeAutoInfo(backupDir, saveSize,
                            saveSize > 0 ? (long)saveSt.st_mtime : 0);
                        prunePoolToCap(false);
                    }
                } else {
                    DebugLog::line("save: open senza backup (non first-ever)");
                }
            }
        } else {
            savePath_ = basePath_ + "main";
        }

        save_.load(savePath_);

        if (!save_.isLoaded()) {
            // Dettaglio tecnico solo nel debug.log, mai a schermo (nemmeno con debug off)
            DebugLog::line("save: load fallito path=%s box=%d slots=%d raw=%zu",
                savePath_.c_str(), save_.boxCount(), save_.slotsPerBox(), save_.rawDataSize());
            // Save illeggibile o senza box sbloccati (inizio gioco): schermata
            // amichevole, poi torna al selettore giochi invece di proseguire in
            // un flusso banche senza senso (save non caricato -> lista vuota).
            showMessageAndWait(i18n::get(StrKey::NoBoxesTitle),
                               i18n::get(StrKey::NoBoxesBody));
            if (DebugLog::enabled()) {
                char dbg[512];
                std::snprintf(dbg, sizeof(dbg), "Path: %s\nLoaded: %d\nBox:%d Slots:%d\nRaw:%zu",
                    savePath_.c_str(), (int)save_.isLoaded(), save_.boxCount(), save_.slotsPerBox(), save_.rawDataSize());
                showMessageAndWait("Save Load (debug)", dbg);
            }
            account_.unmountSave();
            return;
        }

        // Debug: verify encryption round-trip (encrypt(decrypt(file)) == file).
        // Only for SCBlock saves: file-backed GB/GBA/DS/3DS loaders don't
        // fill blocks_/originalFileData_ (they validate per-slot instead).
        if (!isBDSP(game) && !isLGPE(game) && !isFRLG(game) && !isImportedFile(game) && !isGen1File(game) && !isGen2File(game) &&
            !isGen45File(game) && !isGen6XY(game) && !isGen6ORAS(game) && !isGen7SM(game) && !isGen7USUM(game)) {
            std::string rtResult = save_.verifyRoundTrip();
            if (rtResult != "OK")
                showMessageAndWait(i18n::get(StrKey::RoundTripCheck), rtResult);
        }

        // Debug: PK vs OH per-slot box-bytes parity (SwSh + OH handle only).
        // Silenzioso se identico (stringa vuota), dialog solo su divergenze.
        {
            std::string parity = save_.debugCompareEnginesParity();
            if (!parity.empty())
                showMessageAndWait("PK vs OH Parity", parity);
        }
    }

    // Right-panel bank selector shows ALL banks of every game, sectioned by
    // game (cross-gen). Dual/applet mode keeps its own "All Banks" flow.
    if (!isDualBankMode()) {
        bankManager_.initAll(basePath_);
        bankRightCrossGen_ = true;
    } else {
        bankManager_.init(basePath_, game);
    }

    // Reset bank selector state
    bankSelCursor_ = 0;
    bankSelScroll_ = 0;

    screen_ = AppScreen::BankSelector;
    DebugLog::line("open %s: %ums", gameInfo(game).gameTag, SDL_GetTicks() - openT0);
}

std::string UI::buildBackupDir(GameType game) const {
    std::string profileName = "Unknown";
    if (selectedProfile_ >= 0 && selectedProfile_ < account_.profileCount())
        profileName = account_.profiles()[selectedProfile_].pathSafeName;

    std::string dir = basePath_;
    dir += "backups/";
    dir += profileName;
    dir += "/";
    dir += gamePathNameOf(game);
    dir += "/";

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timestamp[64];
    std::snprintf(timestamp, sizeof(timestamp), "%s_%04d-%02d-%02d_%02d-%02d-%02d",
                  profileName.c_str(),
                  t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                  t->tm_hour, t->tm_min, t->tm_sec);
    dir += timestamp;
    dir += "/";
    return dir;
}

bool UI::saveBankFiles() {
    // Write only banks that are (a) open and (b) actually modified since load.
    // An untouched open bank is not rewritten — no popup, no LED, no file churn.
    bool writeLeft  = isDualBankMode() && !leftBankPath_.empty() && bankLeft_.isDirty();
    bool writeRight = !activeBankPath_.empty() && bank_.isDirty();
    if (!writeLeft && !writeRight)
        return true;
    showWorking(i18n::get(StrKey::Saving));
    ledBlink();
    if (writeLeft)  bankLeft_.save(leftBankPath_);
    if (writeRight) bank_.save(activeBankPath_);
    ledOff();
    return true;
}

bool UI::persistGameSaveIfDirty() {
    if (isDualBankMode() || !save_.isLoaded() || !save_.isDirty())
        return true;
    // Choke point unico: MAI persistere con strip vuota. Il guard write-layer
    // tiene il disco valido ma dissocia memoria/disco (box salvati, party
    // vecchio) -> al reload la squadra "resuscita" e si duplica all'infinito
    // (Smeraldo 2026-09-09: pick Treecko, box, reload, Treecko di nuovo).
    // A = Caterpie (GBA) e si salva tutto; B = false, NIENTE save, resto a
    // mano in memoria intatta (niente disco invalido, niente duplicati).
    if (!ensurePartyOnExit()) {
        DebugLog::line("persist: party vuota, save annullato (B), memoria intatta");
        return false;
    }
    showWorking(i18n::get(StrKey::Saving));
    ledBlink();
    bool ok = save_.save(savePath_);
    if (!ok)
        DebugLog::line("persist: save(%s) FALLITO", savePath_.c_str());
    if (ok && savePath_.rfind("save:/", 0) == 0 && !account_.commitSave()) {
        DebugLog::line("persist: commitSave FALLITO dopo save ok (%s)", savePath_.c_str());
        ok = false;
    }
    if (ok) galInvalidateParty(selectedGame_); // preview galleria da ricaricare
    ledOff();
    // Mai fallimento silenzioso: gli errori IO tornano false — l'utente deve
    // saperlo, i dati in memoria restano intatti.
    // (Il fallimento IO non annulla il flusso chiamante: come prima.)
    if (!ok)
        showMessageAndWait(i18n::get(StrKey::SaveFailedTitle),
                           i18n::get(StrKey::SaveFailedBody));
    return true;
}

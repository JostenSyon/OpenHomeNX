#include "boxart.h"
#include "debug_log.h"
#include "emulator.h"
#include "rominfo.h"
#include "settings_cfg.h"
#include "update_net.h" // updateNetEnsureReady/updateNetDownload (rete reale anche su R36S)
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <vector>
#include "json.hpp"
#include <sys/stat.h>
#include <sys/types.h>

namespace Boxart {
namespace {

// ---- piccole utility su path (niente dipendenze nuove) ----
bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string toLowerStr(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string parentDir(const std::string& p) {
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return "";
    return p.substr(0, slash);
}

std::string dirName(const std::string& p) {
    // ultimo segmento della cartella: "/roms/gba" -> "gba"
    std::string d = p;
    while (!d.empty() && d.back() == '/') d.pop_back();
    size_t slash = d.find_last_of('/');
    return (slash == std::string::npos) ? d : d.substr(slash + 1);
}

std::string fileName(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

std::string stemOf(const std::string& p) {
    std::string f = fileName(p);
    size_t dot = f.find_last_of('.');
    if (dot == std::string::npos) return f;
    return f.substr(0, dot);
}

bool ensureDir(const std::string& dir) {
    // mkdir -p minimale: crea ogni livello (ignora EEXIST).
    std::string cur;
    for (size_t i = 0; i < dir.size(); i++) {
        cur += dir[i];
        if (dir[i] == '/' && cur.size() > 1)
            mkdir(cur.c_str(), 0755);
    }
    if (!cur.empty() && cur.back() != '/')
        mkdir(cur.c_str(), 0755);
    struct stat st;
    return stat(dir.c_str(), &st) == 0;
}

bool copyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.good()) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.good()) return false;
    char buf[65536];
    while (in.good()) {
        in.read(buf, sizeof(buf));
        std::streamsize n = in.gcount();
        if (n > 0) out.write(buf, n);
    }
    out.flush();
    return out.good();
}

// ---- lookup locale (Skyscraper): gamelist.xml + images/ ----
// gamelist.xml: cerca <path>./romfile</path> e il primo <image> dopo di esso
// (entro un blocco <game>). Ritorna "" se assente o file inesistente.
std::string gamelistImage(const std::string& romDir, const std::string& romFile) {
    std::string xmlPath = romDir + "/gamelist.xml";
    std::ifstream f(xmlPath);
    if (!f.good()) return "";
    std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Prova sia "./nome" che "nome" come <path>.
    for (const char* prefix : {"./", ""}) {
        std::string needle = std::string("<path>") + prefix + romFile + "</path>";
        size_t pos = xml.find(needle);
        if (pos == std::string::npos) continue;
        size_t img = xml.find("<image>", pos);
        size_t gameEnd = xml.find("</game>", pos);
        if (img == std::string::npos) continue;
        if (gameEnd != std::string::npos && img > gameEnd) continue; // image di un altro blocco
        size_t imgEnd = xml.find("</image>", img);
        if (imgEnd == std::string::npos || imgEnd - img > 1024) continue;
        std::string val = xml.substr(img + 7, imgEnd - (img + 7));
        // trim spazi
        size_t a = val.find_first_not_of(" \t\r\n");
        size_t b = val.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        val = val.substr(a, b - a + 1);
        std::string resolved;
        if (val.rfind("./", 0) == 0) resolved = romDir + "/" + val.substr(2);
        else if (!val.empty() && val[0] == '/') resolved = val;
        else resolved = romDir + "/" + val;
        if (fileExists(resolved)) return resolved;
    }
    return "";
}

// images/<base>[suffisso].png/.jpg (nomi Skyscraper reali visti su R36S).
// Solo suffissi da boxart ("", "-image"); le thumb stanno dopo il gamelist.
std::string imagesDirHit(const std::string& romDir, const std::string& base,
                         bool thumbs) {
    static const char* kBox[] = {"", "-image"};
    static const char* kThumb[] = {"-thumb"};
    static const char* kExt[] = {".png", ".jpg", ".jpeg"};
    const char** suf = thumbs ? kThumb : kBox;
    size_t n = thumbs ? 1 : 2;
    for (size_t i = 0; i < n; i++)
        for (const char* e : kExt) {
            std::string cand = romDir + "/images/" + base + suf[i] + e;
            if (fileExists(cand)) return cand;
        }
    return "";
}

bool isScreenshotPath(const std::string& p) {
    std::string low = toLowerStr(p);
    return low.find("screenshot") != std::string::npos;
}

// Normalizza per il match fuzzy: minuscole, solo alfanumerici separati da spazi.
// Condiviso (serve anche al fuzzy su images/ in locale).
std::string normName(const std::string& s) {
    std::string out;
    bool gap = true;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            out += (char)tolower(c);
            gap = false;
        } else if (!gap) {
            out += ' ';
            gap = true;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> w;
    std::istringstream ss(s);
    std::string t;
    while (ss >> t) w.push_back(t);
    return w;
}

// Punteggio match query (nome ROM) vs candidato (nome file repo, senza .png).
// 100 = uguale, 80 = query come parola intera nel candidato,
// 70 = tutte le parole della query nel candidato, 50 = substring, 0 = niente.
int fuzzyScore(const std::string& query, const std::string& cand) {
    std::string q = normName(query), c = normName(cand);
    if (q.empty() || c.empty()) return 0;
    if (q == c) return 100;
    auto qw = splitWords(q), cw = splitWords(c);
    for (const auto& w : cw)
        if (w == q) return 80;
    bool all = true;
    for (const auto& w : qw) {
        bool found = false;
        for (const auto& cw2 : cw)
            if (cw2 == w) { found = true; break; }
        if (!found) { all = false; break; }
    }
    if (all) return 70;
    if (c.find(q) != std::string::npos || q.find(c) != std::string::npos) return 50;
    return 0;
}

// Match fuzzy sui file in images/: toglie suffissi d'arte noti e confronta
// lo stem col base ROM (score>=70, es. "Pokemon Rosso Fuoco-thumb" vs
// "Pokemon - Versione Rosso Fuoco (Italy)"). Serve quando i nomi non
// coincidono alla lettera (Skyscraper inaffidabile sui nomi).
std::string imagesFuzzy(const std::string& romDir, const std::string& base) {
    static const char* kSuf[] = {"-thumb", "-image", "-marquee", "-screenshot",
                                 "-boxart", "-cover", "-title", "-wheel", "-fanart", "-logo"};
    static const char* kExt[] = {".png", ".jpg", ".jpeg"};
    std::string imgDir = romDir + "/images";
    DIR* d = opendir(imgDir.c_str());
    if (!d) return "";
    std::string bestPath;
    int best = 0;
    size_t bestWords = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string low = toLowerStr(name);
        std::string ext;
        for (const char* x : kExt) {
            std::string xs = x;
            if (low.size() > xs.size() &&
                low.compare(low.size() - xs.size(), xs.size(), xs) == 0) {
                ext = name.substr(name.size() - xs.size());
                break;
            }
        }
        if (ext.empty()) continue;
        std::string stem = name.substr(0, name.size() - ext.size());
        std::string slow = toLowerStr(stem);
        for (const char* s : kSuf) {
            std::string ss = s;
            if (slow.size() > ss.size() &&
                slow.compare(slow.size() - ss.size(), ss.size(), ss) == 0) {
                stem = stem.substr(0, stem.size() - ss.size());
                break;
            }
        }
        int sc = fuzzyScore(stem, base);
        if (sc < 70 || sc < best) continue;
        size_t nw = splitWords(normName(stem)).size();
        if (sc == best && nw >= bestWords) continue;
        best = sc;
        bestWords = nw;
        bestPath = imgDir + "/" + name;
    }
    closedir(d);
    if (!bestPath.empty())
        DebugLog::line("boxart: %s fuzzy images '%s' (score %d)",
                       base.c_str(), bestPath.c_str(), best);
    return bestPath;
}

// Stile scelto in Sviluppatore (persistito in settings.cfg).
static Style curStyle() {
    int v = Settings::boxartStyle();
    if (v < 0 || v > 2) v = 0;
    return (Style)v;
}

// Dimensioni IHDR di un PNG (solo .png). false se non leggibile.
bool pngDims(const std::string& path, unsigned& w, unsigned& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    unsigned char hdr[24] = {0};
    f.read((char*)hdr, 24);
    if (f.gcount() != 24) return false;
    static const unsigned char kPng[8] =
        {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(hdr, kPng, 8) != 0) return false;
    w = ((unsigned)hdr[16] << 24) | ((unsigned)hdr[17] << 16) |
        ((unsigned)hdr[18] << 8) | (unsigned)hdr[19];
    h = ((unsigned)hdr[20] << 24) | ((unsigned)hdr[21] << 16) |
        ((unsigned)hdr[22] << 8) | (unsigned)hdr[23];
    return w > 0 && h > 0;
}

// "3D-ish": nome con box/3d, oppure PNG quadrato (tipico dei render 3D;
// le 2D frontali sono portrait, gli screenshot landscape).
bool is3dFile(const std::string& fullPath, const std::string& stem) {
    std::string low = toLowerStr(stem);
    if (low.find("box") != std::string::npos) return true;
    if (low.find("3d") != std::string::npos) return true;
    std::string llow = toLowerStr(fullPath);
    if (llow.size() < 4 ||
        llow.compare(llow.size() - 4, 4, ".png") != 0)
        return false;
    unsigned w = 0, h = 0;
    if (!pngDims(fullPath, w, h)) return false;
    double r = (double)w / (double)h;
    return r > 0.85 && r < 1.15;
}

// Come imagesFuzzy ma solo candidati 3D-ish (score>=70).
std::string imagesFuzzy3d(const std::string& romDir, const std::string& base) {
    static const char* kSuf[] = {"-thumb", "-image", "-marquee", "-screenshot",
                                 "-boxart", "-cover", "-title", "-wheel", "-fanart", "-logo"};
    static const char* kExt[] = {".png", ".jpg", ".jpeg"};
    std::string imgDir = romDir + "/images";
    DIR* d = opendir(imgDir.c_str());
    if (!d) return "";
    std::string bestPath;
    int best = 0;
    size_t bestWords = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string low = toLowerStr(name);
        std::string ext;
        for (const char* x : kExt) {
            std::string xs = x;
            if (low.size() > xs.size() &&
                low.compare(low.size() - xs.size(), xs.size(), xs) == 0) {
                ext = name.substr(name.size() - xs.size());
                break;
            }
        }
        if (ext.empty()) continue;
        std::string stem = name.substr(0, name.size() - ext.size());
        std::string slow = toLowerStr(stem);
        for (const char* s : kSuf) {
            std::string ss = s;
            if (slow.size() > ss.size() &&
                slow.compare(slow.size() - ss.size(), ss.size(), ss) == 0) {
                stem = stem.substr(0, stem.size() - ss.size());
                break;
            }
        }
        std::string full = imgDir + "/" + name;
        if (!is3dFile(full, name)) continue;
        int sc = fuzzyScore(stem, base);
        if (sc < 70 || sc < best) continue;
        size_t nw = splitWords(normName(stem)).size();
        if (sc == best && nw >= bestWords) continue;
        best = sc;
        bestWords = nw;
        bestPath = full;
    }
    closedir(d);
    if (!bestPath.empty())
        DebugLog::line("boxart: %s fuzzy-3d images '%s' (score %d)",
                       base.c_str(), bestPath.c_str(), best);
    return bestPath;
}

// Priorita' (gamelist.xml di Skyscraper e' inaffidabile: perde giochi).
// Screenshot e loghi mai usati, su nessuno stile.
//  Locale: 1. images/<base>[-image] 2. gamelist (mai screenshot)
//           3. images/<base>-thumb 4. fuzzy su images/ (nomi diversi)
//  Box2d: niente locale (solo download) -> "" sempre qui.
//  Box3d: solo arte 3D locale (nomi box/3d o PNG quadrato).
std::string localCover(const std::string& romPath, Style style) {
    std::string dir = parentDir(romPath);
    std::string file = fileName(romPath);
    std::string base = stemOf(romPath);
    if (dir.empty() || file.empty()) return "";
    if (style == Style::Box2d) return ""; // solo download
    if (style == Style::Box3d) {
        std::string hit = imagesFuzzy3d(dir, base);
        if (hit.empty())
            DebugLog::line("boxart: %s 3D non trovato in locale", base.c_str());
        return hit;
    }
    std::string hit = imagesDirHit(dir, base, false);
    if (!hit.empty()) return hit;
    hit = gamelistImage(dir, file);
    if (!hit.empty()) {
        if (isScreenshotPath(hit)) {
            DebugLog::line("boxart: %s gamelist punta a screenshot, salto (%s)",
                           base.c_str(), hit.c_str());
        } else {
            return hit;
        }
    }
    hit = imagesDirHit(dir, base, true);
    if (!hit.empty()) return hit;
    return imagesFuzzy(dir, base);
}

// ---- download (libretro-thumbnails, repo pubblico usato da RetroArch) ----
std::string libretroRepo(const std::string& sysdirLower) {
    // Il repo ombrello ha solo submodule: i PNG stanno nei repo per-sistema.
    if (sysdirLower == "gba") return "Nintendo_-_Game_Boy_Advance";
    if (sysdirLower == "gbc") return "Nintendo_-_Game_Boy_Color";
    if (sysdirLower == "gb")  return "Nintendo_-_Game_Boy";
    if (sysdirLower == "nds") return "Nintendo_-_Nintendo_DS";
    return "";
}

std::string urlEncodePath(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/') {
            out += (char)c;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

} // namespace

// Cache separata per stile (<base>.locale/.2d/.3d): cambiare stile
// in Sviluppatore deve mostrare arte diversa, non il cache hit di un altro.
std::string styleTag() {
    switch (curStyle()) {
        case Style::Box2d: return "2d";
        case Style::Box3d: return "3d";
        default: return "locale";
    }
}

std::string coverCachePath(const std::string& basePath, const std::string& romPath) {
    if (romPath.empty()) return "";
    std::string sys = toLowerStr(dirName(parentDir(romPath)));
    if (sys.empty()) sys = "rom";
    return basePath + "cache/covers/" + sys + "/" + stemOf(romPath) +
           "." + styleTag() + ".png";
}

std::string findCachedCover(const std::string& basePath, const std::string& romPath) {
    if (romPath.empty()) return "";
    std::string sys = toLowerStr(dirName(parentDir(romPath)));
    if (sys.empty()) sys = "rom";
    std::string stem = basePath + "cache/covers/" + sys + "/" + stemOf(romPath) +
                           "." + styleTag();
    for (const char* e : {".png", ".jpg", ".jpeg"}) {
        if (fileExists(stem + e)) return stem + e;
    }
    return "";
}

ScrapeResult scrape(const std::string& basePath,
                    const std::vector<ImportedGame>& games) {
    ScrapeResult r;
    Style style = curStyle();
    for (const auto& g : games) {
        std::string rom = Emulator::findRomForSave(g.filePath, g.type);
        if (rom.empty()) {
            DebugLog::line("boxart: skip %s (nessuna ROM accanto al save)",
                           g.filePath.c_str());
            continue;
        }
        r.total++;
        std::string base = stemOf(rom);
        std::string hit = findCachedCover(basePath, rom);
        if (!hit.empty()) {
            DebugLog::line("boxart: %s cache hit", base.c_str());
            r.found++;
            continue;
        }
        std::string local = localCover(rom, style);
        if (!local.empty()) {
            std::string dst = coverCachePath(basePath, rom);
            if (ensureDir(parentDir(dst)) && copyFile(local, dst)) {
                DebugLog::line("boxart: %s local %s", base.c_str(), local.c_str());
                r.found++;
            } else {
                DebugLog::line("boxart: %s copia FALLITA (%s -> %s)",
                               base.c_str(), local.c_str(), dst.c_str());
            }
            continue;
        }
        // Download 2D on-demand se la rete e' pronta (entrambe le piattaforme).
        // Locale senza hit e Box3d senza 3D locale arrivano qui.
        if (style == Style::Box3d)
            DebugLog::line("boxart: %s 3D non trovato, uso 2D", base.c_str());
        if (!updateNetEnsureReady()) {
            DebugLog::line("boxart: %s miss (rete off)", base.c_str());
            continue;
        }
        std::string sys = toLowerStr(dirName(parentDir(rom)));
        std::string repo = libretroRepo(sys);
        if (repo.empty()) {
            DebugLog::line("boxart: %s miss (sistema '%s' non mappato)",
                           base.c_str(), sys.c_str());
            continue;
        }
        std::string dst = coverCachePath(basePath, rom);
        std::string err;
        bool ok = false;
        if (ensureDir(parentDir(dst))) {
            // 1. Nomi esatti (veloce): convenzione RetroArch, prima senza
            //    poi con estensione ROM.
            std::string url = "https://raw.githubusercontent.com/libretro-thumbnails/"
                              + repo + "/master/Named_Boxarts/" +
                              urlEncodePath(base + ".png");
            ok = updateNetDownload(url, "", dst, "", err);
            if (!ok) {
                std::string url2 = "https://raw.githubusercontent.com/libretro-thumbnails/"
                                   + repo + "/master/Named_Boxarts/" +
                                   urlEncodePath(fileName(rom) + ".png");
                ok = updateNetDownload(url2, "", dst, "", err);
            }
            // 2. Match fuzzy via elenco repo (nomi No-Intro vs nome ROM).
            if (!ok) {
                std::string api = "https://api.github.com/repos/libretro-thumbnails/"
                                  + repo + "/contents/Named_Boxarts";
                std::string listPath = basePath + "cache/covers/.listing_" + sys + ".json";
                std::string apiErr;
                if (updateNetDownload(api, "", listPath, "", apiErr)) {
                    std::ifstream lf(listPath, std::ios::binary);
                    std::string js((std::istreambuf_iterator<char>(lf)),
                                   std::istreambuf_iterator<char>());
                    auto arr = nlohmann::json::parse(js, nullptr, false);
                    // Query extra: nome canonico da header ROM (lingua reale).
                    // Es. ROM "crystal" (ITA) -> "Pokemon - Versione Cristallo (Italy)".
                    std::string canon;
                    {
                        RomInfo::Info ri;
                        if (RomInfo::detect(rom, ri))
                            canon = RomInfo::canonicalName(ri);
                    }
                    if (!arr.is_discarded() && arr.is_array()) {
                        int best = 0;
                        std::string bestUrl, bestName;
                        for (const auto& it : arr) {
                            std::string nm = it.value("name", std::string());
                            std::string dl = it.value("download_url", std::string());
                            if (nm.size() < 5 || dl.empty()) continue;
                            std::string stem = nm.substr(0, nm.size() - 4); // via .png
                            int s = fuzzyScore(base, stem);
                            if (!canon.empty()) {
                                int s2 = fuzzyScore(canon, stem);
                                if (s2 > s) s = s2;
                            }
                            if (s <= best) continue;
                            // Tiebreak: meno parole = nome piu' pulito.
                            if (s == best && !bestName.empty() &&
                                splitWords(normName(stem)).size() >=
                                splitWords(normName(bestName)).size())
                                continue;
                            best = s;
                            bestUrl = dl;
                            bestName = stem;
                        }
                        if (best > 0) {
                            DebugLog::line("boxart: %s fuzzy '%s' (score %d)",
                                           base.c_str(), bestName.c_str(), best);
                            ok = updateNetDownload(bestUrl, "", dst, "", err);
                        }
                    }
                    std::remove(listPath.c_str());
                } else {
                    DebugLog::line("boxart: %s listing FAIL (%s)",
                                   base.c_str(), apiErr.c_str());
                }
            }
            // 3. Mai cache spazzatura: verifica firma PNG.
            if (ok) {
                std::ifstream vf(dst, std::ios::binary);
                unsigned char magic[8] = {0};
                vf.read((char*)magic, 8);
                static const unsigned char kPng[8] =
                    {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
                if (vf.gcount() != 8 || memcmp(magic, kPng, 8) != 0) {
                    DebugLog::line("boxart: %s scartato (non PNG)", base.c_str());
                    std::remove(dst.c_str());
                    ok = false;
                    err = "not a PNG";
                }
            }
        }
        if (ok) {
            DebugLog::line("boxart: %s download ok", base.c_str());
            r.found++;
        } else {
            DebugLog::line("boxart: %s download FAIL (%s)", base.c_str(), err.c_str());
        }
    }
    DebugLog::line("boxart: scrape %d/%d", r.found, r.total);
    return r;
}

static int clearDirFiles(const std::string& dir) {
    int n = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            n += clearDirFiles(full);
        } else if (std::remove(full.c_str()) == 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

int clearCache(const std::string& basePath) {
    int n = clearDirFiles(basePath + "cache/covers");
    DebugLog::line("boxart: clear %d file", n);
    return n;
}

} // namespace Boxart

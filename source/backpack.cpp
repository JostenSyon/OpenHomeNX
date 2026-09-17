#include "backpack.h"
#include "debug_log.h"
#define JSON_NOEXCEPTION
#include "json.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>

namespace Backpack {
namespace {

SaveFile::GbaBagPocket pocketFromStr(const std::string& p) {
    if (p == "key") return SaveFile::GbaBagPocket::Key;
    if (p == "balls") return SaveFile::GbaBagPocket::Balls;
    if (p == "tm") return SaveFile::GbaBagPocket::TmHm;
    if (p == "berries") return SaveFile::GbaBagPocket::Berries;
    return SaveFile::GbaBagPocket::Items;
}
std::string pocketToStr(SaveFile::GbaBagPocket p) {
    switch (p) {
        case SaveFile::GbaBagPocket::Key: return "key";
        case SaveFile::GbaBagPocket::Balls: return "balls";
        case SaveFile::GbaBagPocket::TmHm: return "tm";
        case SaveFile::GbaBagPocket::Berries: return "berries";
        default: return "items";
    }
}
std::string gameKey(GameType g) { return bankFolderNameOf(g); }

struct JournalEntry {
    std::string game;
    int item = 0, qty = 0;
    std::string pocket;
    long ts = 0;
};
std::string journalPath(const std::string& base) { return base + "backpack.json"; }

std::vector<JournalEntry> journalLoad(const std::string& base) {
    std::vector<JournalEntry> out;
    std::ifstream f(journalPath(base));
    if (!f.good()) return out;
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(buf, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return out;
    for (auto& e : j) {
        if (!e.is_object()) continue;
        JournalEntry je;
        je.game = e.value("game", "");
        je.item = e.value("item", 0);
        je.qty = e.value("qty", 0);
        je.pocket = e.value("pocket", "");
        je.ts = e.value("ts", 0L);
        if (!je.game.empty() && je.item > 0 && je.qty > 0) out.push_back(je);
    }
    return out;
}
bool journalSave(const std::string& base, const std::vector<JournalEntry>& v) {
    nlohmann::json j = nlohmann::json::array();
    for (auto& e : v)
        j.push_back({{"game", e.game}, {"item", e.item}, {"qty", e.qty},
                     {"pocket", e.pocket}, {"ts", e.ts}});
    std::ofstream o(journalPath(base), std::ios::trunc);
    if (!o.good()) return false;
    o << j.dump(1);
    return true;
}

const ItemDef* findDef(const std::vector<ItemDef>& defs, int id) {
    for (auto& d : defs)
        if (d.id == id) return &d;
    return nullptr;
}

} // namespace

bool loadDefs(const std::string& jsonPath, std::vector<ItemDef>& out, std::string& err) {
    out.clear();
    std::ifstream f(jsonPath);
    if (!f.good()) { err = "DB zaino non trovato: " + jsonPath; return false; }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(buf, nullptr, false);
    if (j.is_discarded() || !j.is_array()) { err = "DB zaino corrotto"; return false; }
    for (auto& e : j) {
        if (!e.is_object()) continue;
        ItemDef d;
        d.id = e.value("id", 0);
        d.name = e.value("name", "");
        d.pocket = e.value("pocket", "");
        d.key = e.value("key", false);
        d.max = e.value("max", 99);
        d.ruby = e.value("ruby", false);
        d.emerald = e.value("emerald", false);
        d.frlg = e.value("frlg", false);
        d.flag = e.value("flag", false);
        if (d.id <= 0 || d.name.empty() || d.max < 1) continue; // riga sporca: salta, mai fidarsi
        out.push_back(d);
    }
    if (out.empty()) { err = "DB zaino vuoto"; return false; }
    return true;
}

bool gameOk(GameType g, const ItemDef& d) {
    if (g == GameType::RUBY || g == GameType::SAPPHIRE) return d.ruby;
    if (g == GameType::EMERALD) return d.emerald;
    if (isFRLG(g)) return d.frlg;
    return false; // solo Gen3 GBA
}

SaveFile::GbaBagPocket canonPocket(const ItemDef& d) {
    return pocketFromStr(d.pocket);
}

bool gift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
          int qty, bool baseMode, std::string& msg) {
    if (!sf.gbaBagSupported()) { msg = "Borsa non supportata per questo gioco (solo RSE/FRLG)."; return false; }
    if (!gameOk(sf.gameType(), d)) { msg = d.name + " non valido per questo gioco."; return false; }
    // FRLG Aurora/Mistico: oltre all'oggetto in borsa serve anche il flag nave.
    // SaveBlock1+0xEE0, 1 bit per flag. Per FRLG:
    //  Aurora (371) -> 0x2A7 RECEIVED + 0x84B SHIP_BIRTH_ISLAND
    //  Mistico (370) -> 0x2A8 RECEIVED + 0x84A SHIP_NAVEL_ROCK
    // La Switch mette entrambi, facciamo uguale. Per Smeraldo/Rubino ecc. vedi docs.
    bool needFlag = d.flag;
    if (needFlag) {
        // Flag gestiti: FRLG Aurora/Mistico, RSE Aurora/Mistico/OldSeaMap/Eon, National Dex
        if (isFRLG(sf.gameType()) && (d.id == 371 || d.id == 370)) needFlag = false;
        else if (sf.gameType() == GameType::EMERALD && (d.id == 371 || d.id == 370 || d.id == 376 || d.id == 1000)) needFlag = false;
        else if ((sf.gameType() == GameType::RUBY || sf.gameType() == GameType::SAPPHIRE) && d.id == 275) needFlag = false;
        else if (d.id == 1001) needFlag = false; // National Dex per tutti i Gen3
    }
    if (needFlag) {
        msg = d.name + ": richiede anche il flag evento (non ancora implementato per questo gioco/oggetto).";
        return false;
    }
    SaveFile::GbaBagPocket target = baseMode ? SaveFile::GbaBagPocket::Key : canonPocket(d);
    int want = baseMode ? 1 : qty;
    if (want < 1) want = 1;
    if (want > d.max) want = d.max; // clamp esplicito al max di tabella
    auto slots = sf.readGbaBag();
    if (slots.empty()) { msg = "Borsa illeggibile."; return false; }
    if (baseMode) {
        for (auto& s : slots)
            if (s.pocket == target && s.id == d.id) {
                msg = d.name + " già protetto nei Key Items.";
                return false;
            }
    }
    struct Plan { int idx; uint16_t setQty; };
    std::vector<Plan> plan;
    int rest = want;
    if (!baseMode) {
        // 1. stack su stesso id fino a max (come AddBagItem)
        for (size_t i = 0; i < slots.size() && rest > 0; i++) {
            if (slots[i].pocket != target || slots[i].id != (uint16_t)d.id) continue;
            int room = d.max - slots[i].count;
            if (room <= 0) continue;
            int put = (rest < room) ? rest : room;
            plan.push_back({(int)i, (uint16_t)(slots[i].count + put)});
            rest -= put;
        }
    }
    // 2. primo slot vuoto
    for (size_t i = 0; i < slots.size() && rest > 0; i++) {
        if (slots[i].pocket != target || slots[i].id != 0) continue;
        int put = (rest < d.max) ? rest : d.max;
        plan.push_back({(int)i, (uint16_t)put});
        rest -= put;
    }
    if (rest > 0) {
        msg = "Borsa piena nel pocket " + pocketToStr(target) + " (mancano " +
              std::to_string(rest) + "): niente scritto.";
        return false; // atomico: o tutto o niente
    }
    for (auto& p : plan) {
        if (!sf.writeGbaBagSlot(slots[p.idx].pocket, slots[p.idx].slot, (uint16_t)d.id, p.setQty)) {
            msg = "Scrittura slot fallita (abortito, ricontrolla).";
            return false;
        }
    }
    // Eventi puri senza oggetto (non vanno in borsa, solo flag): Mystery Event e National Dex
    // Per questi non scrivere nulla in borsa, solo il flag e il giornale.
    if (d.id == 1000 || d.id == 1001) {
        if (d.id == 1000 && sf.gameType() == GameType::EMERALD) {
            sf.setGbaFlag(0x8B3);
            DebugLog::line("backpack: Mystery Event flag 0x8B3 impostato (Emerald)");
        } else if (d.id == 1001) {
            sf.setNationalDexEnabled();
            DebugLog::line("backpack: National Dex sbloccato (evento puro, no oggetto in borsa)");
        } else {
            msg = d.name + " non valido per questo gioco.";
            return false;
        }
        // Giornale anche per gli eventi puri (tracciabilità)
        auto jl2 = journalLoad(basePath);
        JournalEntry je2; je2.game = gameKey(sf.gameType()); je2.item = d.id; je2.qty = 1;
        je2.pocket = pocketToStr(target); je2.ts = (long)std::time(nullptr);
        jl2.push_back(je2); journalSave(basePath, jl2);
        msg = std::string(d.name) + " sbloccato (solo flag, nessun oggetto in borsa).";
        sf.markDirty(); // assicura persistenza anche senza scrittura borsa
        return true;
    }
    // Flag nave/evento per FRLG e RSE/Smeraldo (SaveBlock1+0xEE0, 1 bit per flag).
    // FRLG: Aurora 371 -> 0x2A7+0x84B, Mistico 370 -> 0x2A8+0x84A (entrambi insieme)
    // Smeraldo: Aurora 371 0x13A+0x8D5, Mistico 370 0x13B+0x8E0, Old Sea Map/Mew 376 0x13C+0x8D6, Eone 0x8B3 (Mystery Event 1000)
    // Rubino/Zaffiro: Eone Ticket 275 -> 0x853
    // National Dex 1001 -> SaveBlock2+0x19 (gestito sopra come evento puro)
    if (d.id == 371 || d.id == 370 || d.id == 376 || d.id == 275) {
        if (isFRLG(sf.gameType())) {
            if (d.id == 371) { sf.setGbaFlag(0x2A7); sf.setGbaFlag(0x84B); }
            else if (d.id == 370) { sf.setGbaFlag(0x2A8); sf.setGbaFlag(0x84A); }
            // FRLG non ha 376/275 come evento nave, ma se regalati li mettiamo comunque in borsa
            DebugLog::line("backpack: flag FRLG impostati per %s (%d)", d.name.c_str(), d.id);
        } else if (sf.gameType() == GameType::EMERALD) {
            if (d.id == 371) { sf.setGbaFlag(0x13A); sf.setGbaFlag(0x8D5); }
            else if (d.id == 370) { sf.setGbaFlag(0x13B); sf.setGbaFlag(0x8E0); }
            else if (d.id == 376) { sf.setGbaFlag(0x13C); sf.setGbaFlag(0x8D6); }
            else if (d.id == 1000) { sf.setGbaFlag(0x8B3); }
            else if (d.id == 275) { /* Eon Ticket su Smeraldo è via Mystery Event, non 275 */ }
            if (d.id == 371 || d.id == 370 || d.id == 376 || d.id == 1000) DebugLog::line("backpack: flag Smeraldo impostati per %s (%d)", d.name.c_str(), d.id);
        } else if (sf.gameType() == GameType::RUBY || sf.gameType() == GameType::SAPPHIRE) {
            if (d.id == 275) { sf.setGbaFlag(0x853); DebugLog::line("backpack: flag Rubino/Zaffiro Eone impostato"); }
        }
        // National Dex (1001) — valido per tutti i Gen3, sblocca il Dex Nazionale (SaveBlock2+0x19)
        if (d.id == 1001) {
            sf.setNationalDexEnabled();
            DebugLog::line("backpack: National Dex sbloccato per %s", gameKey(sf.gameType()).c_str());
        }
    }
    // Giornale (best-effort: il regalo è già scritto, logga l'esito)
    auto jl = journalLoad(basePath);
    JournalEntry je;
    je.game = gameKey(sf.gameType());
    je.item = d.id;
    je.qty = want;
    je.pocket = pocketToStr(target);
    je.ts = (long)std::time(nullptr);
    jl.push_back(je);
    if (!journalSave(basePath, jl))
        DebugLog::line("backpack: giornale non salvato (regalo ok)");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s x%d nel pocket %s%s.", d.name.c_str(), want,
                  pocketToStr(target).c_str(), baseMode ? " (protetto)" : "");
    msg = buf;
    return true;
}

bool takeBack(SaveFile& sf, const std::string& basePath, GameType g,
              int itemId, const std::string& pocket, std::string& msg) {
    if (!sf.gbaBagSupported()) { msg = "Borsa non supportata per questo gioco."; return false; }
    std::string gk = gameKey(g);
    auto jl = journalLoad(basePath);
    int recorded = 0;
    for (auto& e : jl)
        if (e.game == gk && e.item == itemId && e.pocket == pocket) recorded += e.qty;
    if (recorded <= 0) { msg = "Mai regalato da qui in questa modalita' (giornale vuoto per questa voce)."; return false; }
    // Filtra anche per tasca: lo stesso item puo' essere stato regalato sia
    // Base (Key Items) sia Consumabile (tasca canonica) - senza questo si
    // rischiava di togliere l'istanza sbagliata (bug 2026-09-13).
    SaveFile::GbaBagPocket targetPocket = pocketFromStr(pocket);
    auto slots = sf.readGbaBag();
    int rest = recorded, removed = 0;
    for (auto& s : slots) {
        if (rest <= 0) break;
        if (s.id != itemId || s.pocket != targetPocket) continue;
        int take = (s.count < rest) ? s.count : rest;
        int left = s.count - take;
        if (!sf.writeGbaBagSlot(s.pocket, s.slot, left > 0 ? (uint16_t)itemId : 0, (uint16_t)left))
            { msg = "Scrittura slot fallita."; return false; }
        rest -= take;
        removed += take;
    }
    // Scala il giornale di quanto effettivamente tolto (stessa tasca)
    int dec = removed;
    for (auto it = jl.begin(); it != jl.end() && dec > 0;) {
        if (it->game == gk && it->item == itemId && it->pocket == pocket) {
            int cut = (it->qty < dec) ? it->qty : dec;
            it->qty -= cut;
            dec -= cut;
            if (it->qty <= 0) it = jl.erase(it);
            else ++it;
        } else ++it;
    }
    journalSave(basePath, jl);
    char buf[192];
    if (rest > 0)
        std::snprintf(buf, sizeof(buf), "Tolti %d (di %d): %d già usati o spostati.", removed, recorded, rest);
    else
        std::snprintf(buf, sizeof(buf), "Tolti %d.", removed);
    msg = buf;
    return removed > 0;
}

std::vector<JournalRow> journalFor(const std::string& basePath, GameType g) {
    // Una riga per {item, tasca}: MAI unite per id soltanto, altrimenti
    // Base e Consumabile dello stesso oggetto si confonderebbero in
    // un'unica voce non piu' riprendibile senza ambiguita'.
    std::string gk = gameKey(g);
    std::vector<JournalRow> out;
    for (auto& e : journalLoad(basePath)) {
        if (e.game != gk || e.qty <= 0) continue;
        bool merged = false;
        for (auto& r : out)
            if (r.item == e.item && r.pocket == e.pocket) { r.qty += e.qty; merged = true; break; }
        if (!merged) out.push_back({e.item, e.pocket, e.qty});
    }
    return out;
}

bool clearJournal(const std::string& basePath) {
    return journalSave(basePath, {});
}

std::vector<Anomaly> scanBag(SaveFile& sf, const std::vector<ItemDef>& defs) {
    std::vector<Anomaly> out;
    if (!sf.gbaBagSupported()) return out;
    for (auto& s : sf.readGbaBag()) {
        if (s.id == 0) {
            if (s.count != 0) out.push_back({s.pocket, s.slot, 0, s.count, "invalid"});
            continue;
        }
        const ItemDef* d = findDef(defs, s.id);
        if (!d || !gameOk(sf.gameType(), *d)) {
            out.push_back({s.pocket, s.slot, s.id, s.count, "invalid"});
            continue;
        }
        if (s.count == 0)
            // Voce valida ma a stock zero: slot morto. "over-max" la
            // resuscitava a x1 (un oggetto che non esiste) - svuotarla e'
            // l'unica semantica sensata (bug 2026-09-13).
            out.push_back({s.pocket, s.slot, s.id, 0, "invalid"});
        else if ((int)s.count > d->max)
            out.push_back({s.pocket, s.slot, s.id, s.count, "over-max"});
        else if (pocketFromStr(d->pocket) != s.pocket)
            out.push_back({s.pocket, s.slot, s.id, s.count, "protected"});
    }
    return out;
}

bool fixAnomaly(SaveFile& sf, const Anomaly& a, const std::vector<ItemDef>& defs,
                std::string& msg) {
    if (!sf.gbaBagSupported()) { msg = "Borsa non supportata."; return false; }
    if (a.kind == "invalid") {
        if (!sf.writeGbaBagSlot(a.pocket, a.slot, 0, 0)) { msg = "Scrittura fallita."; return false; }
        msg = "Slot svuotato.";
        return true;
    }
    const ItemDef* d = findDef(defs, a.id);
    if (!d) { msg = "Voce ignota."; return false; }
    if (a.kind == "over-max") {
        int q = a.count > d->max ? d->max : a.count;
        if (q < 1) q = 1;
        if (!sf.writeGbaBagSlot(a.pocket, a.slot, a.id, (uint16_t)q)) { msg = "Scrittura fallita."; return false; }
        msg = "Clampato a " + std::to_string(q) + " (max " + d->name + ").";
        return true;
    }
    if (a.kind == "protected") {
        // Sposta nel pocket canonico (stack o primo vuoto), atomico
        SaveFile::GbaBagPocket dst = canonPocket(*d);
        auto slots = sf.readGbaBag();
        int rest = a.count;
        struct Plan { SaveFile::GbaBagPocket p; int s; uint16_t q; };
        std::vector<Plan> plan;
        for (auto& sl : slots) {
            if (rest <= 0) break;
            if (sl.pocket != dst || sl.id != a.id) continue;
            int room = d->max - sl.count;
            if (room <= 0) continue;
            int put = rest < room ? rest : room;
            plan.push_back({dst, sl.slot, (uint16_t)(sl.count + put)});
            rest -= put;
        }
        for (auto& sl : slots) {
            if (rest <= 0) break;
            if (sl.pocket != dst || sl.id != 0) continue;
            int put = rest < d->max ? rest : d->max;
            plan.push_back({dst, sl.slot, (uint16_t)put});
            rest -= put;
        }
        if (rest > 0) { msg = "Pocket canonico pieno: niente spostato."; return false; }
        if (!sf.writeGbaBagSlot(a.pocket, a.slot, 0, 0)) { msg = "Scrittura fallita."; return false; }
        for (auto& p : plan)
            if (!sf.writeGbaBagSlot(p.p, p.s, a.id, p.q)) { msg = "Scrittura fallita a metà: ricontrolla."; return false; }
        msg = "Spostato nel pocket " + pocketToStr(dst) + " (ora consumabile).";
        return true;
    }
    msg = "Tipo anomalia ignoto.";
    return false;
}

} // namespace Backpack

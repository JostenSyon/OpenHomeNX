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
        d.dp = e.value("dp", false);
        d.pt = e.value("pt", false);
        d.hgss = e.value("hgss", false);
        d.bw = e.value("bw", false);
        d.b2w2 = e.value("b2w2", false);
        d.red = e.value("red", false);
        d.blue = e.value("blue", false);
        d.yellow = e.value("yellow", false);
        d.gold = e.value("gold", false);
        d.silver = e.value("silver", false);
        d.crystal = e.value("crystal", false);
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
    if (g == GameType::DIAMOND || g == GameType::PEARL) return d.dp;
    if (g == GameType::PLATINUM) return d.pt;
    if (g == GameType::HEARTGOLD || g == GameType::SOULSILVER) return d.hgss;
    if (g == GameType::BLACK || g == GameType::WHITE) return d.bw;
    if (g == GameType::BLACK2 || g == GameType::WHITE2) return d.b2w2;
    if (g == GameType::RED) return d.red;
    if (g == GameType::BLUE) return d.blue;
    if (g == GameType::YELLOW) return d.yellow;
    if (g == GameType::GOLD) return d.gold;
    if (g == GameType::SILVER) return d.silver;
    if (g == GameType::CRYSTAL) return d.crystal;
    return false; // solo GBA Gen3 + DS Gen4/5 + GB Gen1/2
}

SaveFile::GbaBagPocket canonPocket(const ItemDef& d) {
    return pocketFromStr(d.pocket);
}

bool gift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
          int qty, bool baseMode, std::string& msg) {
    if (!sf.gbaBagSupported()) { msg = "Borsa non supportata per questo gioco (solo RSE/FRLG)."; return false; }
    if (!gameOk(sf.gameType(), d)) { msg = d.name + " non valido per questo gioco."; return false; }

    // Eventi puri (nessun oggetto reale in borsa: nel gioco vero il National
    // Dex si sblocca SOLO parlando con Birch/Oak, mai con un "giveitem") --
    // vanno gestiti PRIMA di toccare la borsa. Prima di questo fix l'id
    // fittizio 1001 arrivava comunque fino allo scrittura-slot piu' sotto e
    // finiva scritto come un vero oggetto in borsa (garbage: 1001 non
    // corrisponde a nessun oggetto Gen3 reale) -- bug osservato
    // dall'utente ("il national dex dato come oggetto nello zaino").
    if (d.id == 1001) {
        sf.setNationalDexEnabled(); // scrive incondizionatamente, niente da controllare
        DebugLog::line("backpack: National Dex sbloccato (evento puro, no oggetto in borsa)");
        auto jl2 = journalLoad(basePath);
        JournalEntry je2; je2.game = gameKey(sf.gameType()); je2.item = d.id; je2.qty = 1;
        je2.pocket = "event"; je2.ts = (long)std::time(nullptr);
        jl2.push_back(je2); journalSave(basePath, jl2);
        msg = d.name + " sbloccato (solo flag, nessun oggetto in borsa).";
        sf.markDirty(); // assicura persistenza anche senza scrittura borsa
        return true;
    }

    // FRLG Aurora/Mistico e RSE Aurora/Mistico/Old Sea Map/Eon Ticket: oltre
    // all'oggetto in borsa serve anche il/i flag nave (il gioco vero fa
    // sempre "giveitem" + "setflag" insieme -- verificato contro gli script
    // ufficiali pret: LilycoveCity_Harbor su Emerald/Ruby/Sapphire e
    // VermilionCity su FRLG controllano ENTRAMBI checkitem e il flag prima
    // di far salpare la nave, mai uno solo dei due).
    //  FRLG:     Aurora(371) 0x2A7 RECEIVED + 0x84B SHIP_BIRTH_ISLAND
    //            Mistico(370) 0x2A8 RECEIVED + 0x84A SHIP_NAVEL_ROCK
    //  Emerald:  Aurora(371) 0x13A + 0x8D5, Mistico(370) 0x13B + 0x8E0,
    //            Old Sea Map(376) 0x13C + 0x8D6, Eon Ticket(275) 0x8B3
    //            (2026-09-19: prima era un no-op qui -- si credeva che
    //            Smeraldo non usasse l'oggetto 275, ma pret/pokeemerald
    //            src/script_menu.c lo richiede esplicitamente insieme al
    //            flag: CheckBagHasItem(ITEM_EON_TICKET,1) && FlagGet(...))
    //  Ruby/Sapphire: Eon Ticket(275) 0x853
    bool needFlag = d.flag;
    if (needFlag) {
        if (isFRLG(sf.gameType()) && (d.id == 371 || d.id == 370)) needFlag = false;
        else if (sf.gameType() == GameType::EMERALD && (d.id == 371 || d.id == 370 || d.id == 376 || d.id == 275)) needFlag = false;
        else if ((sf.gameType() == GameType::RUBY || sf.gameType() == GameType::SAPPHIRE) && d.id == 275) needFlag = false;
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
    // Flag nave/evento in aggiunta all'oggetto appena scritto in borsa (mai
    // uno dei due da solo, vedi commento sopra su needFlag).
    bool eventFlagOk = true; // false solo se una setGbaFlag() sotto fallisce davvero
    if (d.id == 371 || d.id == 370 || d.id == 376 || d.id == 275) {
        if (isFRLG(sf.gameType())) {
            if (d.id == 371) { eventFlagOk = sf.setGbaFlag(0x2A7) && sf.setGbaFlag(0x84B); }
            else if (d.id == 370) { eventFlagOk = sf.setGbaFlag(0x2A8) && sf.setGbaFlag(0x84A); }
            // FRLG non ha 376/275 come evento nave, ma se regalati li mettiamo comunque in borsa
            DebugLog::line("backpack: flag FRLG %s per %s (%d)", eventFlagOk ? "impostati" : "FALLITI", d.name.c_str(), d.id);
        } else if (sf.gameType() == GameType::EMERALD) {
            if (d.id == 371) { eventFlagOk = sf.setGbaFlag(0x13A) && sf.setGbaFlag(0x8D5); }
            else if (d.id == 370) { eventFlagOk = sf.setGbaFlag(0x13B) && sf.setGbaFlag(0x8E0); }
            else if (d.id == 376) { eventFlagOk = sf.setGbaFlag(0x13C) && sf.setGbaFlag(0x8D6); }
            else if (d.id == 275) { eventFlagOk = sf.setGbaFlag(0x8B3); } // Eon Ticket -> Southern Island
            DebugLog::line("backpack: flag Smeraldo %s per %s (%d)", eventFlagOk ? "impostati" : "FALLITI", d.name.c_str(), d.id);
        } else if (sf.gameType() == GameType::RUBY || sf.gameType() == GameType::SAPPHIRE) {
            if (d.id == 275) {
                eventFlagOk = sf.setGbaFlag(0x853);
                DebugLog::line("backpack: flag Rubino/Zaffiro Eone %s", eventFlagOk ? "impostato" : "FALLITO");
            }
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
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s x%d nel pocket %s%s%s.", d.name.c_str(), want,
                  pocketToStr(target).c_str(), baseMode ? " (protetto)" : "",
                  eventFlagOk ? "" : " -- ATTENZIONE: scrittura del flag evento fallita, l'oggetto e' in borsa ma l'evento non e' sbloccato");
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
    // National Dex (1001): evento puro, nessuno slot borsa da liberare --
    // disattiva invece il flag/VAR/nationalMagic (inverso esatto di
    // setNationalDexEnabled(), stesso meccanismo che il gioco vero usa a
    // New Game). Sicuro: non tocca i bitfield caught/seen, che restano
    // intatti e riappaiono in vista National se lo si riabilita.
    if (itemId == 1001 && pocket == "event") {
        sf.disableNationalDex();
        int dec = recorded;
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
        msg = "National Dex disattivato.";
        return true;
    }
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

// --- Zaino DS (Gen4/5): stesso contratto del Gen3, su readDsBag/writeDsBagSlot.
// Tasche Mail/Med/Battle esistono solo qui (pocket 0-slot = tasca assente).
const ItemDef* dsFindDef(GameType g, const std::vector<ItemDef>& defs, int id) {
    for (auto& d : defs)
        if (d.id == id && gameOk(g, d)) return &d;
    return nullptr;
}

SaveFile::DsBagPocket dsCanonPocket(const ItemDef& d) {
    return dsPocketFromStr(d.pocket);
}

std::string dsPocketToStr(SaveFile::DsBagPocket p) {
    using P = SaveFile::DsBagPocket;
    switch (p) {
        case P::Key: return "key";
        case P::Balls: return "balls";
        case P::TmHm: return "tm";
        case P::Mail: return "mail";
        case P::Medicine: return "med";
        case P::Berries: return "berries";
        case P::Battle: return "battle";
        default: return "items"; // Items (+ Count, fallback mai usato)
    }
}

SaveFile::DsBagPocket dsPocketFromStr(const std::string& p) {
    using P = SaveFile::DsBagPocket;
    if (p == "key") return P::Key;
    if (p == "balls") return P::Balls;
    if (p == "tm") return P::TmHm;
    if (p == "mail") return P::Mail;
    if (p == "med") return P::Medicine;
    if (p == "berries") return P::Berries;
    if (p == "battle") return P::Battle;
    return P::Items;
}

bool dsGift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
            int qty, bool baseMode, std::string& msg) {
    if (!sf.dsBagSupported()) { msg = "Borsa non supportata per questo gioco (solo Gen3/DS)."; return false; }
    if (!gameOk(sf.gameType(), d)) { msg = d.name + " non valido per questo gioco."; return false; }
    if (d.flag) {
        // DB DS v1: mai true (niente flag evento inventati). Se mai lo diventa,
        // rifiuto esplicito come il path Gen3, mai scrittura monca.
        msg = d.name + ": richiede anche il flag evento (non ancora implementato per questo gioco/oggetto).";
        return false;
    }
    SaveFile::DsBagPocket target = baseMode ? SaveFile::DsBagPocket::Key : dsCanonPocket(d);
    int want = baseMode ? 1 : qty;
    if (want < 1) want = 1;
    if (want > d.max) want = d.max;
    auto slots = sf.readDsBag();
    if (slots.empty()) { msg = "Borsa illeggibile."; return false; }
    if (baseMode) {
        for (auto& s : slots)
            if (s.pocket == target && s.id == d.id) {
                msg = d.name + " già protetto nei Key Items.";
                return false;
            }
    }
    // La tasca canonica potrebbe non esistere per questo gioco (es. Mail su
    // Gen5: 0 slot): niente scrittura nel vuoto, esplicito.
    bool pocketHere = false;
    for (auto& s : slots)
        if (s.pocket == target) { pocketHere = true; break; }
    if (!pocketHere) { msg = d.name + ": tasca assente in questo gioco."; return false; }
    struct Plan { int idx; uint16_t setQty; };
    std::vector<Plan> plan;
    int rest = want;
    if (!baseMode) {
        for (size_t i = 0; i < slots.size() && rest > 0; i++) {
            if (slots[i].pocket != target || slots[i].id != (uint16_t)d.id) continue;
            int room = d.max - slots[i].count;
            if (room <= 0) continue;
            int put = (rest < room) ? rest : room;
            plan.push_back({(int)i, (uint16_t)(slots[i].count + put)});
            rest -= put;
        }
    }
    for (size_t i = 0; i < slots.size() && rest > 0; i++) {
        if (slots[i].pocket != target || slots[i].id != 0) continue;
        int put = (rest < d.max) ? rest : d.max;
        plan.push_back({(int)i, (uint16_t)put});
        rest -= put;
    }
    if (rest > 0) {
        msg = "Borsa piena nel pocket " + dsPocketToStr(target) + " (mancano " +
              std::to_string(rest) + "): niente scritto.";
        return false;
    }
    for (auto& p : plan) {
        if (!sf.writeDsBagSlot(slots[p.idx].pocket, slots[p.idx].slot, (uint16_t)d.id, p.setQty)) {
            msg = "Scrittura slot fallita (abortito, ricontrolla).";
            return false;
        }
    }
    auto jl = journalLoad(basePath);
    JournalEntry je;
    je.game = gameKey(sf.gameType());
    je.item = d.id;
    je.qty = want;
    je.pocket = dsPocketToStr(target);
    je.ts = (long)std::time(nullptr);
    jl.push_back(je);
    if (!journalSave(basePath, jl))
        DebugLog::line("backpack: giornale non salvato (regalo ok)");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s x%d nel pocket %s%s.", d.name.c_str(), want,
                  dsPocketToStr(target).c_str(), baseMode ? " (protetto)" : "");
    msg = buf;
    return true;
}

bool dsTakeBack(SaveFile& sf, const std::string& basePath, GameType g,
                int itemId, const std::string& pocket, std::string& msg) {
    if (!sf.dsBagSupported()) { msg = "Borsa non supportata per questo gioco."; return false; }
    std::string gk = gameKey(g);
    auto jl = journalLoad(basePath);
    int recorded = 0;
    for (auto& e : jl)
        if (e.game == gk && e.item == itemId && e.pocket == pocket) recorded += e.qty;
    if (recorded <= 0) { msg = "Mai regalato da qui in questa modalita' (giornale vuoto per questa voce)."; return false; }
    SaveFile::DsBagPocket targetPocket = dsPocketFromStr(pocket);
    auto slots = sf.readDsBag();
    int rest = recorded, removed = 0;
    for (auto& s : slots) {
        if (rest <= 0) break;
        if (s.id != itemId || s.pocket != targetPocket) continue;
        int take = (s.count < rest) ? s.count : rest;
        int left = s.count - take;
        if (!sf.writeDsBagSlot(s.pocket, s.slot, left > 0 ? (uint16_t)itemId : 0, (uint16_t)left))
            { msg = "Scrittura slot fallita."; return false; }
        rest -= take;
        removed += take;
    }
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

std::vector<DsAnomaly> dsScanBag(SaveFile& sf, const std::vector<ItemDef>& defs) {
    std::vector<DsAnomaly> out;
    if (!sf.dsBagSupported()) return out;
    for (auto& s : sf.readDsBag()) {
        if (s.id == 0) {
            if (s.count != 0) out.push_back({s.pocket, s.slot, 0, s.count, "invalid"});
            continue;
        }
        const ItemDef* d = dsFindDef(sf.gameType(), defs, s.id);
        if (!d) {
            out.push_back({s.pocket, s.slot, s.id, s.count, "invalid"});
            continue;
        }
        if (s.count == 0)
            out.push_back({s.pocket, s.slot, s.id, 0, "invalid"});
        else if ((int)s.count > d->max)
            out.push_back({s.pocket, s.slot, s.id, s.count, "over-max"});
        else if (dsPocketFromStr(d->pocket) != s.pocket)
            out.push_back({s.pocket, s.slot, s.id, s.count, "protected"});
    }
    return out;
}

bool dsFixAnomaly(SaveFile& sf, const DsAnomaly& a, const std::vector<ItemDef>& defs,
                  std::string& msg) {
    if (!sf.dsBagSupported()) { msg = "Borsa non supportata."; return false; }
    if (a.kind == "invalid") {
        if (!sf.writeDsBagSlot(a.pocket, a.slot, 0, 0)) { msg = "Scrittura fallita."; return false; }
        msg = "Slot svuotato.";
        return true;
    }
    const ItemDef* d = nullptr;
    for (auto& dd : defs)
        if (dd.id == a.id && gameOk(sf.gameType(), dd)) { d = &dd; break; }
    if (!d) { msg = "Voce ignota."; return false; }
    if (a.kind == "over-max") {
        int q = a.count > d->max ? d->max : a.count;
        if (q < 1) q = 1;
        if (!sf.writeDsBagSlot(a.pocket, a.slot, a.id, (uint16_t)q)) { msg = "Scrittura fallita."; return false; }
        msg = "Clampato a " + std::to_string(q) + " (max " + d->name + ").";
        return true;
    }
    if (a.kind == "protected") {
        SaveFile::DsBagPocket dst = dsCanonPocket(*d);
        auto slots = sf.readDsBag();
        int rest = a.count;
        struct Plan { SaveFile::DsBagPocket p; int s; uint16_t q; };
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
        if (!sf.writeDsBagSlot(a.pocket, a.slot, 0, 0)) { msg = "Scrittura fallita."; return false; }
        for (auto& p : plan)
            if (!sf.writeDsBagSlot(p.p, p.s, a.id, p.q)) { msg = "Scrittura fallita a metà: ricontrolla."; return false; }
        msg = "Spostato nel pocket " + dsPocketToStr(dst) + " (ora consumabile).";
        return true;
    }
    msg = "Tipo anomalia ignoto.";
    return false;
}

// --- Zaino GB (Gen1/2): riscrittura tasche compattate (PKHeX SetPouch).
// Niente slot fissi: slot = posizione nella lista compattata al momento
// della lettura; ogni mutazione ricarica (come DS dopo ogni persist).
// Capacita' tasche (voci massime, da PKHeX PlayerBag1/2).
static int gbPocketCap(SaveFile::GbBagPocket p) {
    using P = SaveFile::GbBagPocket;
    if (p == P::Key) return 26;
    if (p == P::Balls) return 12;
    if (p == P::TmHm) return 57;
    return 20;
}
const ItemDef* gbFindDef(GameType g, const std::vector<ItemDef>& defs, int id) {
    for (auto& d : defs)
        if (d.id == id && gameOk(g, d)) return &d;
    return nullptr;
}

SaveFile::GbBagPocket gbCanonPocket(const ItemDef& d) {
    return gbPocketFromStr(d.pocket);
}

std::string gbPocketToStr(SaveFile::GbBagPocket p) {
    using P = SaveFile::GbBagPocket;
    switch (p) {
        case P::Key: return "key";
        case P::Balls: return "balls";
        case P::TmHm: return "tm";
        default: return "items";
    }
}

SaveFile::GbBagPocket gbPocketFromStr(const std::string& p) {
    using P = SaveFile::GbBagPocket;
    if (p == "key") return P::Key;
    if (p == "balls") return P::Balls;
    if (p == "tm") return P::TmHm;
    return P::Items;
}

bool gbGift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
            int qty, bool baseMode, std::string& msg) {
    if (!sf.gbBagSupported()) { msg = "Borsa non supportata per questo gioco (solo Gen1/2/3/DS)."; return false; }
    if (!gameOk(sf.gameType(), d)) { msg = d.name + " non valido per questo gioco."; return false; }
    if (d.flag) {
        msg = d.name + ": richiede anche il flag evento (non ancora implementato per questo gioco/oggetto).";
        return false;
    }
    SaveFile::GbBagPocket target = baseMode ? SaveFile::GbBagPocket::Key : gbCanonPocket(d);
    // Gen1 non ha tasca Key: la modalita' Base non esiste qui.
    if (target == SaveFile::GbBagPocket::Key && isGen1File(sf.gameType())) {
        msg = d.name + ": tasca Base assente in Gen1.";
        return false;
    }
    int want = (baseMode || d.key) ? 1 : qty;
    if (want < 1) want = 1;
    if (want > d.max) want = d.max;
    const int total = want; // quanto regalato davvero (per giornale e messaggio)
    // Stato attuale della tasca (ordinato come nel save).
    std::vector<std::pair<uint16_t, uint16_t>> cur;
    for (auto& s : sf.readGbBag())
        if (s.pocket == target) cur.push_back({s.id, s.count});
    if (!baseMode && !d.key) {
        for (auto& e : cur) {
            if (want <= 0) break;
            if (e.first != (uint16_t)d.id) continue;
            int room = d.max - e.second;
            if (room <= 0) continue;
            int put = (want < room) ? want : room;
            e.second = (uint16_t)(e.second + put);
            want -= put;
        }
    } else {
        for (auto& e : cur)
            if (e.first == (uint16_t)d.id) {
                msg = d.name + " già protetto nei Key Items.";
                return false;
            }
    }
    // Capacita' tasca (voci massime scrivibili).
    int cap = gbPocketCap(target);
    if (target == SaveFile::GbBagPocket::Key && (int)cur.size() >= cap && want > 0) {
        msg = "Borsa piena nel pocket key: niente scritto.";
        return false;
    }
    while (want > 0) {
        if ((int)cur.size() >= cap) {
            msg = "Borsa piena nel pocket " + gbPocketToStr(target) + ": niente scritto.";
            return false; // atomico: niente scritto fin qui (write sotto, unico)
        }
        int put = (want < d.max) ? want : d.max;
        if (target == SaveFile::GbBagPocket::Key) put = 1;
        cur.push_back({(uint16_t)d.id, (uint16_t)put});
        want -= put;
    }
    int gotQty = total;
    if (!sf.writeGbBagPocket(target, cur)) {
        msg = "Scrittura tasca fallita (niente scritto).";
        return false;
    }
    auto jl = journalLoad(basePath);
    JournalEntry je;
    je.game = gameKey(sf.gameType());
    je.item = d.id;
    je.qty = gotQty;
    je.pocket = gbPocketToStr(target);
    je.ts = (long)std::time(nullptr);
    jl.push_back(je);
    if (!journalSave(basePath, jl))
        DebugLog::line("backpack: giornale non salvato (regalo ok)");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s x%d nel pocket %s%s.", d.name.c_str(), je.qty,
                  gbPocketToStr(target).c_str(), (baseMode || d.key) ? " (protetto)" : "");
    msg = buf;
    return true;
}

bool gbTakeBack(SaveFile& sf, const std::string& basePath, GameType g,
                int itemId, const std::string& pocket, std::string& msg) {
    if (!sf.gbBagSupported()) { msg = "Borsa non supportata per questo gioco."; return false; }
    std::string gk = gameKey(g);
    auto jl = journalLoad(basePath);
    int recorded = 0;
    for (auto& e : jl)
        if (e.game == gk && e.item == itemId && e.pocket == pocket) recorded += e.qty;
    if (recorded <= 0) { msg = "Mai regalato da qui in questa modalita' (giornale vuoto per questa voce)."; return false; }
    SaveFile::GbBagPocket target = gbPocketFromStr(pocket);
    std::vector<std::pair<uint16_t, uint16_t>> cur;
    for (auto& s : sf.readGbBag())
        if (s.pocket == target) cur.push_back({s.id, s.count});
    int rest = recorded, removed = 0;
    for (auto& e : cur) {
        if (rest <= 0) break;
        if (e.first != itemId) continue;
        int take = (e.second < rest) ? e.second : rest;
        e.second = (uint16_t)(e.second - take);
        rest -= take;
        removed += take;
    }
    // Ricompatta (voci a zero spariscono, come PKHeX ClearCount0).
    std::vector<std::pair<uint16_t, uint16_t>> kept;
    for (auto& e : cur)
        if (e.second > 0) kept.push_back(e);
    if (!sf.writeGbBagPocket(target, kept)) { msg = "Scrittura tasca fallita."; return false; }
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

std::vector<GbAnomaly> gbScanBag(SaveFile& sf, const std::vector<ItemDef>& defs) {
    std::vector<GbAnomaly> out;
    if (!sf.gbBagSupported()) return out;
    for (auto& s : sf.readGbBag()) {
        if (s.id == 0) {
            if (s.count != 0) out.push_back({s.pocket, s.slot, 0, s.count, "invalid"});
            continue;
        }
        const ItemDef* d = gbFindDef(sf.gameType(), defs, s.id);
        if (!d) {
            out.push_back({s.pocket, s.slot, s.id, s.count, "invalid"});
            continue;
        }
        if (s.count == 0)
            out.push_back({s.pocket, s.slot, s.id, 0, "invalid"});
        else if ((int)s.count > d->max)
            out.push_back({s.pocket, s.slot, s.id, s.count, "over-max"});
        else if (gbPocketFromStr(d->pocket) != s.pocket)
            out.push_back({s.pocket, s.slot, s.id, s.count, "protected"});
    }
    return out;
}

bool gbFixAnomaly(SaveFile& sf, const GbAnomaly& a, const std::vector<ItemDef>& defs,
                  std::string& msg) {
    if (!sf.gbBagSupported()) { msg = "Borsa non supportata."; return false; }
    // Ricostruisce la tasca: rimuove/clam pa la voce, ricompatta, riscrive.
    std::vector<std::pair<uint16_t, uint16_t>> cur;
    for (auto& s : sf.readGbBag())
        if (s.pocket == a.pocket) cur.push_back({s.id, s.count});
    bool touched = false;
    if (a.kind == "invalid") {
        std::vector<std::pair<uint16_t, uint16_t>> kept;
        for (auto& e : cur) {
            // Rimuove la voce anomala (stesso id; count 0 = slot morto).
            if (!touched && e.first == a.id && (a.count == 0 || e.second == a.count)) {
                touched = true;
                continue;
            }
            kept.push_back(e);
        }
        if (!touched) { msg = "Voce non più presente (già sistemata?)."; return false; }
        if (!sf.writeGbBagPocket(a.pocket, kept)) { msg = "Scrittura fallita."; return false; }
        msg = "Voce rimossa.";
        return true;
    }
    const ItemDef* d = nullptr;
    for (auto& dd : defs)
        if (dd.id == a.id && gameOk(sf.gameType(), dd)) { d = &dd; break; }
    if (!d) { msg = "Voce ignota."; return false; }
    if (a.kind == "over-max") {
        for (auto& e : cur) {
            if (e.first != a.id || touched) continue;
            int q = e.second > d->max ? d->max : e.second;
            if (q < 1) q = 1;
            e.second = (uint16_t)q;
            touched = true;
        }
        if (!touched) { msg = "Voce non più presente."; return false; }
        if (!sf.writeGbBagPocket(a.pocket, cur)) { msg = "Scrittura fallita."; return false; }
        msg = "Clampato a max " + d->name + ".";
        return true;
    }
    if (a.kind == "protected") {
        SaveFile::GbBagPocket dst = gbCanonPocket(*d);
        if (dst == a.pocket) { msg = "Già al posto giusto."; return false; }
        std::vector<std::pair<uint16_t, uint16_t>> kept;
        int moved = 0;
        for (auto& e : cur) {
            if (e.first == a.id && moved < a.count) {
                int take = (e.second < (a.count - moved)) ? e.second : (a.count - moved);
                moved += take;
                e.second = (uint16_t)(e.second - take);
            }
            if (e.second > 0) kept.push_back(e);
        }
        if (moved <= 0) { msg = "Voce non più presente."; return false; }
        if (!sf.writeGbBagPocket(a.pocket, kept)) { msg = "Scrittura fallita."; return false; }
        // Aggiunge nel canonico (stack o coda), con rollback se pieno.
        std::vector<std::pair<uint16_t, uint16_t>> dstCur;
        for (auto& s : sf.readGbBag())
            if (s.pocket == dst) dstCur.push_back({s.id, s.count});
        int rest = moved;
        for (auto& e : dstCur) {
            if (rest <= 0) break;
            if (e.first != a.id) continue;
            int room = d->max - e.second;
            if (room <= 0) continue;
            int put = rest < room ? rest : room;
            e.second = (uint16_t)(e.second + put);
            rest -= put;
        }
        while (rest > 0) {
            // capacita' tasca (stessa di gift).
            int cap = gbPocketCap(dst);
            if ((int)dstCur.size() >= cap) {
                // rollback: rimette dov'era (meglio che perdere l'oggetto)
                std::vector<std::pair<uint16_t, uint16_t>> back;
                for (auto& s : sf.readGbBag())
                    if (s.pocket == a.pocket) back.push_back({s.id, s.count});
                back.push_back({a.id, (uint16_t)moved});
                sf.writeGbBagPocket(a.pocket, back);
                msg = "Pocket canonico pieno: niente spostato.";
                return false;
            }
            int put = rest < d->max ? rest : d->max;
            dstCur.push_back({a.id, (uint16_t)put});
            rest -= put;
        }
        if (!sf.writeGbBagPocket(dst, dstCur)) { msg = "Scrittura fallita a metà: ricontrolla."; return false; }
        msg = "Spostato nel pocket " + gbPocketToStr(dst) + " (ora consumabile).";
        return true;
    }
    msg = "Tipo anomalia ignoto.";
    return false;
}

} // namespace Backpack

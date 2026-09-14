// Zaino Gen3: sinistra = catalogo di tutto cio' che si puo' trasferire
// (ordinato per tipo), sempre visibile. Destra = lista giochi finche' non
// ne scegli uno, poi al suo posto lo zaino VERO del gioco scelto (torni
// alla lista giochi con B, come le banche). Opera su scratch SaveFile del
// gioco evidenziato (load/regalo/save con backup manuale), mai sul save_
// della main view. Logica borsa in backpack.h (gift atomico, giornale,
// validazione), qui solo UI.
#include "ui.h"
#include "ui_util.h"
#include "i18n.h"
#include "debug_log.h"
#include "backpack.h"
#include "species_converter.h" // MoveName::get() per la mossa di MT/MN
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int BP_POP_W = 1120;
constexpr int BP_POP_H = 600;
constexpr int BP_ROW_H = 38;
constexpr int BP_VISIBLE = 12;
constexpr int BP_LEFT_W = 660;
// Barra tab categoria sopra al catalogo: una riga in meno per farle
// posto (vedi BP_VISIBLE_LEFT). Alta abbastanza da non far toccare il
// testo al bordo (richiesto esplicitamente).
constexpr int BP_TAB_H = 34;
constexpr int BP_VISIBLE_LEFT = BP_VISIBLE - 1;
// Tab del catalogo (diverse dai pocket del gioco: TM e MN condividono
// il pocket "tm", qui li separiamo; le Bacche hanno una tab propria
// invece di stare con gli Oggetti). Richiesto esplicitamente: con
// ~300 voci per gioco (es. Emerald) un'unica lista era impraticabile.
constexpr int BP_CAT_COUNT = 6; // Sfere, MN, MT, Consumabili, Speciali, Bacche

const char* bpPocketKey(const std::string& p) {
    if (p == "key") return StrKey::BpKey;
    if (p == "balls") return StrKey::BpBalls;
    if (p == "tm") return StrKey::BpTm;
    if (p == "berries") return StrKey::BpBerries;
    return StrKey::BpItems;
}
const char* bpPocketKeyByEnum(SaveFile::GbaBagPocket p) {
    switch (p) {
        case SaveFile::GbaBagPocket::Key: return StrKey::BpKey;
        case SaveFile::GbaBagPocket::Balls: return StrKey::BpBalls;
        case SaveFile::GbaBagPocket::TmHm: return StrKey::BpTm;
        case SaveFile::GbaBagPocket::Berries: return StrKey::BpBerries;
        default: return StrKey::BpItems;
    }
}
// Tab del catalogo per una voce: 0=Sfere,1=MN,2=MT,3=Consumabili,
// 4=Speciali,5=Bacche. MN prima di MT: nel gioco vero le Macchine
// Nascoste vengono prima (richiesto esplicitamente).
int bpCatTabFor(const Backpack::ItemDef& d) {
    using P = SaveFile::GbaBagPocket;
    P p = Backpack::canonPocket(d);
    if (p == P::Balls) return 0;
    if (p == P::TmHm) {
        bool isHm = d.name.size() >= 2 && d.name[0] == 'H' && d.name[1] == 'M';
        return isHm ? 1 : 2;
    }
    if (p == P::Key) return 4;
    if (p == P::Berries) return 5;
    return 3; // solo Oggetti: le Bacche hanno ora una tab propria
}
const char* bpCatTabKey(int tab) {
    switch (tab) {
        case 0: return StrKey::BpTabBalls;
        case 1: return StrKey::BpTabMn;
        case 2: return StrKey::BpTabMt;
        case 3: return StrKey::BpTabCons;
        case 4: return StrKey::BpTabSpecial;
        default: return StrKey::BpBerries; // stessa etichetta gia' usata nello zaino vero
    }
}
// Mossa insegnata da ciascuna MT/MN Gen3 (id oggetto 289..346 =
// TM01..HM08, stessa in RS/E/FRLG: il set Gen3 non cambia per
// versione). Id mossa verificati riga per riga contro
// romfs/data/moves_en.txt (es. Focus Punch e' davvero l'id 264 li'
// dentro), non solo a memoria.
constexpr int BP_TMHM_FIRST_ID = 289;
constexpr uint16_t kBpTmHmMove[58] = {
    264, 337, 352, 347, 46, 92, 258, 339, 331, 237,   // TM01-10
    241, 269, 58, 59, 63, 113, 182, 240, 202, 219,    // TM11-20
    218, 76, 231, 85, 87, 89, 216, 91, 94, 247,       // TM21-30
    280, 104, 115, 351, 53, 188, 201, 126, 317, 332,  // TM31-40
    259, 263, 290, 156, 213, 168, 211, 285, 289, 315, // TM41-50
    15, 19, 57, 70, 148, 249, 127, 291,               // HM01-08
};
// -1 se non e' una MT/MN.
int bpTmHmMoveId(const Backpack::ItemDef& d) {
    int off = d.id - BP_TMHM_FIRST_ID;
    if (off < 0 || off >= (int)(sizeof(kBpTmHmMove) / sizeof(kBpTmHmMove[0]))) return -1;
    return kBpTmHmMove[off];
}
} // namespace

static bool bpIsBagGame(GameType g) {
    return g == GameType::RUBY || g == GameType::SAPPHIRE ||
           g == GameType::EMERALD || isFRLG(g);
}

// Definita qui (prima di ogni uso: backpackStickStep/backpackReloadItems
// la chiamano) cosi' non serve una forward declaration separata.
static void bpScrollIntoView(int& cursor, int& scroll, int count, int visible = BP_VISIBLE) {
    if (count <= 0) { cursor = 0; scroll = 0; return; }
    if (cursor < 0) cursor = 0;
    if (cursor >= count) cursor = count - 1;
    if (cursor < scroll) scroll = cursor;
    else if (cursor >= scroll + visible) scroll = cursor - visible + 1;
}

void UI::openBackpack() {
    openBackpackOn(-1);
}

void UI::openBackpackOn(int selIdx) {
    if (backpackDefs_.empty()) {
        std::string err;
        if (!Backpack::loadDefs("romfs:/data/items_gen3.json", backpackDefs_, err)) {
            showMessageAndWait(i18n::get(StrKey::Error), err);
            return;
        }
    }
    backpackGames_.clear();
    backpackOcc_.clear();
    for (int i = 0; i < (int)availableGames_.size(); i++) {
        GameType g = availableGames_[i];
        if (!bpIsBagGame(g)) continue;
        int occ = importedOccurrence(i);
        bool fileBacked = !importedSavePath(g, occ).empty();
        bool title = !fileBacked && selectedProfile_ >= 0 && !appletMode_ &&
                     titleIdOf(g) >= 0x0100000000010000ULL && saveFileNameOf(g)[0] != '\0';
        if (!fileBacked && !title) continue;
        backpackGames_.push_back(g);
        backpackOcc_.push_back(occ);
    }
    if (backpackGames_.empty()) {
        showMessageAndWait(i18n::get(StrKey::BackpackTitle), i18n::get(StrKey::BackpackNoGames));
        return;
    }
    backpackGameCursor_ = 0;
    backpackGameScroll_ = 0;
    backpackFocusItems_ = false; // si parte dai giochi: prima si sceglie, poi le voci
    backpackGameChosen_ = false;
    backpackGame_ = backpackGames_[0]; // solo per filtrare il catalogo, save non caricato
    // Se arrivi con un gioco evidenziato (il cursore del selettore), apri
    // direttamente il SUO zaino: niente scelta ridondante.
    if (selIdx >= 0 && selIdx < (int)availableGames_.size()) {
        GameType sg = availableGames_[selIdx];
        int socc = importedOccurrence(selIdx);
        bool found = false;
        for (size_t k = 0; k < backpackGames_.size(); k++) {
            if (backpackGames_[k] == sg && backpackOcc_[k] == socc) {
                backpackGameCursor_ = (int)k;
                found = true;
                break;
            }
        }
        // Il gioco evidenziato non e' zaino-compatibile (es. Gen4+) o non
        // disponibile per lo zaino: NON aprire a caso il primo della lista
        // (si regalava nel gioco sbagliato senza che fosse esplicito). Come
        // per le banche, si ricade sulla scelta manuale (bug 2026-09-13).
        if (!found) selIdx = -1;
    }
    backpackAudit_ = false;
    backpackQty_ = 1;
    backpackBaseMode_ = false;
    backpackLeftCursor_ = 0;
    backpackLeftScroll_ = 0;
    backpackCatTab_ = 0;
    backpackBagCursor_ = 0;
    backpackBagScroll_ = 0;
    backpackBackedUp_.clear();
    showBackpack_ = true;
    if (selIdx >= 0) {
        // Destinazione nota: apri subito e passa allo zaino vero
        backpackLoadGame(backpackGames_[backpackGameCursor_],
                         backpackOcc_[backpackGameCursor_]);
        if (backpackLoaded_) {
            backpackGameChosen_ = true;
            backpackFocusItems_ = true;
            // backpackLoadGame ha gia' ricaricato con backpackGameChosen_
            // ancora falso (lo zaino vero non c'era nelle righe): rifallo
            // ora che e' vero, altrimenti la destra resta vuota.
            backpackReloadItems();
        }
    } else {
        backpackReloadItems();
    }
    markDirty();
}

void UI::closeBackpack() {
    if (!backpackSaveMnt_.empty()) {
        account_.unmountSave();
        backpackSaveMnt_.clear();
    }
    showBackpack_ = false;
    backpackLoaded_ = false;
    markDirty();
}

void UI::backpackLoadGame(GameType g, int occ) {
    if (!backpackSaveMnt_.empty()) {
        account_.unmountSave();
        backpackSaveMnt_.clear();
    }
    backpackGame_ = g;
    backpackLoaded_ = false;
    backpackSavePath_.clear();
    backpackSave_.setGameType(g);
    bool fileBacked = !importedSavePath(g, occ).empty();
    if (fileBacked) {
        backpackSavePath_ = importedSavePath(g, occ);
    } else {
        if (selectedProfile_ < 0 || appletMode_) return;
        std::string mnt = account_.mountSave(selectedProfile_, g);
        if (mnt.empty()) return;
        backpackSaveMnt_ = mnt;
        backpackSavePath_ = mnt + saveFileNameOf(g);
    }
    if (backpackSavePath_.empty() || !backpackSave_.load(backpackSavePath_)) {
        DebugLog::line("backpack: load fallito %s", gameInfo(g).gameTag);
        return;
    }
    backpackLoaded_ = true;
    backpackReloadItems();
}

static const Backpack::ItemDef* bpFindDef(const std::vector<Backpack::ItemDef>& defs, int id) {
    for (auto& d : defs)
        if (d.id == id) return &d;
    return nullptr;
}

void UI::backpackReloadItems() {
    backpackItems_.clear();
    backpackOwned_.clear();
    backpackGameBag_.clear();
    backpackLeft_.clear();
    backpackBag_.clear();
    for (auto& d : backpackDefs_)
        if (Backpack::gameOk(backpackGame_, d)) backpackItems_.push_back(d);
    // Ordinate per id: e' l'ordine "vero" del gioco (lo stesso di
    // items_en.txt, usato ovunque nell'app per i nomi oggetto), non
    // alfabetico - richiesto esplicitamente al posto del vecchio ordine.
    std::sort(backpackItems_.begin(), backpackItems_.end(),
              [](const Backpack::ItemDef& a, const Backpack::ItemDef& b) {
                  return a.id < b.id;
              });
    if (backpackLoaded_ && backpackSave_.gameType() == backpackGame_) {
        for (auto& s : backpackSave_.readGbaBag()) {
            if (s.id != 0) {
                backpackOwned_[s.id] += s.count;
                backpackGameBag_.push_back(s);
            }
        }
    }
    // Sinistra: catalogo di tutto cio' che si puo' trasferire in questo
    // gioco, filtrato sulla tab categoria attiva (backpackCatTab_).
    // Sempre visibile, anche solo in hover sul gioco prima di sceglierlo.
    {
        for (size_t i = 0; i < backpackItems_.size(); i++) {
            if (bpCatTabFor(backpackItems_[i]) != backpackCatTab_) continue;
            backpackLeft_.push_back({false, (int)i, -1});
        }
    }
    // Destra (solo a gioco scelto+caricato): zaino VERO del gioco, al
    // posto della lista giochi (torni li' con B, come le banche).
    // Raggruppato per tasca (un header per pocket): NON riordiniamo
    // alfabeticamente, le voci sono gia' contigue per tasca come le
    // restituisce readGbaBag(), qui aggiungiamo solo l'etichetta di
    // sezione cosi' si trova tutto come nel catalogo a sinistra.
    if (backpackGameChosen_ && backpackLoaded_) {
        int curPocket = -1;
        for (size_t i = 0; i < backpackGameBag_.size(); i++) {
            int p = (int)backpackGameBag_[i].pocket;
            if (p != curPocket) {
                backpackBag_.push_back({true, -1, p});
                curPocket = p;
            }
            backpackBag_.push_back({false, (int)i, -1});
        }
    }
    if (backpackLeftCursor_ >= (int)backpackLeft_.size())
        backpackLeftCursor_ = backpackLeft_.empty() ? 0 : (int)backpackLeft_.size() - 1;
    if (backpackLeftCursor_ < 0) backpackLeftCursor_ = 0;
    // Mai fermo su un header
    if (!backpackLeft_.empty() && backpackLeft_[backpackLeftCursor_].header)
        backpackLeftCursor_ = 0;
    if (backpackBagCursor_ >= (int)backpackBag_.size())
        backpackBagCursor_ = backpackBag_.empty() ? 0 : (int)backpackBag_.size() - 1;
    if (backpackBagCursor_ < 0) backpackBagCursor_ = 0;
    // Mai fermo su un header: a differenza del catalogo di sinistra (che non
    // ne ha), lo zaino vero ha una riga di intestazione per pocket - il
    // cursore parte/puo' finire su indice 0 (quasi sempre un header), e li'
    // il rendering non evidenzia mai nulla (richiesto esplicitamente: "vado
    // a destra e non c'e' nessun elemento evidenziato finche' non premo
    // su/giu'"). Cerca la prima voce reale in avanti.
    if (!backpackBag_.empty() && backpackBag_[backpackBagCursor_].header) {
        int j = backpackBagCursor_;
        while (j < (int)backpackBag_.size() && backpackBag_[j].header) j++;
        backpackBagCursor_ = (j < (int)backpackBag_.size()) ? j : 0;
    }
    // Lo scroll puo' essere rimasto quello di un gioco precedente con uno
    // zaino molto piu' pieno: senza questo l'header della prima categoria
    // (e le prime voci) restano scrollati fuori vista pur essendo presenti
    // nei dati (altro bug segnalato: "manca l'header della prima categoria").
    bpScrollIntoView(backpackBagCursor_, backpackBagScroll_, (int)backpackBag_.size());
    backpackClampQty();
    backpackRefreshAudit();
}

// Cambia la tab categoria del catalogo (dir=+-1, wrap) e ricarica le
// righe filtrate sulla nuova tab. Sempre disponibile (ZL/ZR), a
// prescindere da dove sia il fuoco, perche' il catalogo e' sempre
// visibile a sinistra.
void UI::backpackCatTabStep(int dir) {
    backpackCatTab_ = ((backpackCatTab_ + dir) % BP_CAT_COUNT + BP_CAT_COUNT) % BP_CAT_COUNT;
    backpackLeftCursor_ = 0;
    backpackLeftScroll_ = 0;
    backpackReloadItems();
}

// Un passo (dir=+-1) sul pannello a fuoco: usato sia dal tasto direzionale
// (chiamata diretta da handleBackpackInput) sia dal repeat levetta
// per-frame in ui_selectors.cpp ("Joystick repeat navigation").
void UI::backpackStickStep(int dir) {
    if (backpackFocusItems_) {
        if (backpackAudit_) {
            int n = (int)(backpackAnoms_.size() + backpackJournal_.size());
            backpackAuditCursor_ += dir;
            bpScrollIntoView(backpackAuditCursor_, backpackAuditScroll_, n, BP_VISIBLE_LEFT);
        } else {
            backpackLeftStep(backpackLeftCursor_, dir);
            bpScrollIntoView(backpackLeftCursor_, backpackLeftScroll_, (int)backpackLeft_.size(), BP_VISIBLE_LEFT);
            backpackClampQty();
        }
    } else if (!backpackGameChosen_) {
        backpackGameCursor_ += dir;
        bpScrollIntoView(backpackGameCursor_, backpackGameScroll_, (int)backpackGames_.size());
        if (!backpackGames_.empty()) {
            // Hover: filtra il catalogo sul gioco ma NON carica il save
            // (niente mount a ogni passo); la scelta esplicita con A apre
            // davvero il suo zaino.
            backpackGame_ = backpackGames_[backpackGameCursor_];
            backpackLoaded_ = false;
            backpackLeftCursor_ = 0;
            backpackLeftScroll_ = 0;
            backpackReloadItems();
        }
    } else {
        backpackBagStep(backpackBagCursor_, dir);
        bpScrollIntoView(backpackBagCursor_, backpackBagScroll_, (int)backpackBag_.size());
    }
}

// Sposta il fuoco tra catalogo e zaino/lista giochi. Stessa azione di
// D-pad LEFT/RIGHT, fattorizzata qui cosi' la puo' chiamare anche il
// repeat per-frame della levetta orizzontale (vedi ui_selectors.cpp).
// Idempotente: se il fuoco e' gia' dove dir vuole portarlo, non fa
// nulla, quindi tenerla inclinata non fa "sbattere" avanti e indietro.
void UI::backpackFocusStep(int dir) {
    if (dir < 0) {
        if (!backpackFocusItems_) backpackFocusItems_ = true;
    } else if (dir > 0) {
        if (backpackFocusItems_ && !backpackAudit_) backpackFocusItems_ = false;
    }
}

// Muove il cursore del catalogo saltando gli header di sezione (wrap incluso).
void UI::backpackLeftStep(int& cursor, int dir) {
    const auto& rows = backpackLeft_;
    if (rows.empty()) { cursor = 0; return; }
    for (size_t k = 0; k <= rows.size(); k++) {
        cursor += dir;
        if (cursor < 0) cursor = (int)rows.size() - 1;
        if (cursor >= (int)rows.size()) cursor = 0;
        if (!rows[cursor].header) break;
    }
}

// Come sopra ma per lo zaino VERO a destra (oggi senza header, ma stessa
// logica di wrap per coerenza col catalogo).
void UI::backpackBagStep(int& cursor, int dir) {
    const auto& rows = backpackBag_;
    if (rows.empty()) { cursor = 0; return; }
    for (size_t k = 0; k <= rows.size(); k++) {
        cursor += dir;
        if (cursor < 0) cursor = (int)rows.size() - 1;
        if (cursor >= (int)rows.size()) cursor = 0;
        if (!rows[cursor].header) break;
    }
}

void UI::backpackClampQty() {
    if (backpackItems_.empty()) { backpackQty_ = 1; return; }
    if (backpackLeftCursor_ < 0 || backpackLeftCursor_ >= (int)backpackLeft_.size()) return;
    const auto& row = backpackLeft_[backpackLeftCursor_];
    if (row.header) return;
    if (row.idx < 0 || row.idx >= (int)backpackItems_.size()) return;
    const auto& d = backpackItems_[row.idx];
    if (backpackQty_ > d.max) backpackQty_ = d.max;
    if (backpackQty_ < 1) backpackQty_ = 1;
}

void UI::backpackRefreshAudit() {
    backpackAnoms_.clear();
    backpackJournal_.clear();
    // Solo se lo scratch contiene davvero questo gioco (dopo un hover senza
    // scelta lo scratch è vecchio o vuoto: niente dati altrui in vista).
    if (!backpackLoaded_ || backpackSave_.gameType() != backpackGame_) return;
    backpackAnoms_ = Backpack::scanBag(backpackSave_, backpackDefs_);
    backpackJournal_ = Backpack::journalFor(basePath_, backpackGame_);
    if (backpackAuditCursor_ >= (int)(backpackAnoms_.size() + backpackJournal_.size()))
        backpackAuditCursor_ = 0;
}

// Salva lo scratch (con commit per i titoli). False = errore IO (mai silente).
bool UI::backpackPersist(const std::string& why) {
    if (!backpackLoaded_) return false;
    showWorking(i18n::get(StrKey::Saving));
    bool ok = backpackSave_.save(backpackSavePath_);
    if (!ok)
        DebugLog::line("backpack persist(%s): save(%s) FALLITO", why.c_str(), backpackSavePath_.c_str());
    if (ok && !backpackSaveMnt_.empty()) {
        bool committed = account_.commitSave();
        if (!committed) {
            // Scrittura sul file scratch riuscita ma non "commitata" su
            // Horizon: senza questo il salvataggio puo' non persistere
            // davvero (o farlo in ritardo a un commit successivo) - va
            // trattato come fallimento, mai come successo silenzioso.
            DebugLog::line("backpack persist(%s): commitSave FALLITO dopo save ok", why.c_str());
            ok = false;
        }
    }
    if (ok) galInvalidateParty(backpackGame_);
    if (!ok)
        showMessageAndWait(i18n::get(StrKey::SaveFailedTitle),
                           i18n::get(StrKey::SaveFailedBody));
    return ok;
}

void UI::backpackDoGift() {
    if (!backpackGameChosen_ || !backpackLoaded_ || backpackItems_.empty()) return;
    if (backpackLeftCursor_ < 0 || backpackLeftCursor_ >= (int)backpackLeft_.size()) return;
    const auto& row = backpackLeft_[backpackLeftCursor_];
    if (row.header) return; // regalo solo dalle voci del catalogo
    if (row.idx < 0 || row.idx >= (int)backpackItems_.size()) return;
    const auto& d = backpackItems_[row.idx];
    bool base = backpackBaseMode_ || d.key;
    int qty = base ? 1 : backpackQty_;
    if (qty < 1) qty = 1;
    if (qty > d.max) qty = d.max;
    char body[220];
    std::snprintf(body, sizeof(body), "%s x%d%s\n%s", d.name.c_str(), qty,
                  base ? " (Base)" : "", gameDisplayNameOf(backpackGame_));
    if (!showConfirmDialog(i18n::get(StrKey::BackpackTitle), body)) return;
    // Backup di sicurezza una tantum per gioco per sessione zaino, prima
    // del regalo: e' l'azione "regala" a scatenarlo, non una richiesta
    // esplicita dell'utente -> per definizione e' AUTOMATICO (pool auto/,
    // tag "AUTO", soggetto al tetto come tutti gli auto), NON manuale.
    int gi = static_cast<int>(backpackGame_);
    if (backpackBackedUp_.count(gi) == 0) {
        saveMenuOcc_ = backpackOcc_[backpackGameCursor_];
        std::string out;
        // backpackSaveMnt_ ("save:/" se gioco montato da titolo, vuoto se
        // file-backed): passato cosi' il backup riusa il mount dello zaino
        // invece di rubarglielo (vedi commento in ui.h / bug fossile FRLG).
        if (!backupGameSave(backpackGame_, out, backpackSaveMnt_, false)) {
            if (!showConfirmDialog(i18n::get(StrKey::BackupFailed),
                                   i18n::get(StrKey::BackupFailedBody)))
                return;
        }
        backpackBackedUp_.insert(gi);
    }
    std::string msg;
    if (!Backpack::gift(backpackSave_, basePath_, d, qty, base, msg)) {
        DebugLog::line("backpack gift: %s x%d (%s) rifiutato: %s", d.name.c_str(), qty,
                       base ? "Base" : "Cons.", msg.c_str());
        showMessageAndWait(i18n::get(StrKey::BackpackTitle), msg);
        return;
    }
    if (!backpackPersist("gift")) {
        DebugLog::line("backpack gift: %s x%d in memoria ok ma persist fallito", d.name.c_str(), qty);
        return;
    }
    DebugLog::line("backpack gift: %s x%d (%s) su %s -> ok", d.name.c_str(), qty,
                   base ? "Base" : "Cons.", gameInfo(backpackGame_).gameTag);
    backpackReloadItems();
    showMessageAndWait(i18n::get(StrKey::BackpackTitle), msg);
    markDirty();
}

void UI::backpackDoTake() {
    // Toglie uno slot dello zaino vero, a destra (con conferma: mai per sbaglio).
    if (!backpackGameChosen_ || !backpackLoaded_) return;
    if (backpackBagCursor_ < 0 || backpackBagCursor_ >= (int)backpackBag_.size()) return;
    const auto& row = backpackBag_[backpackBagCursor_];
    if (row.header) return;
    if (row.idx < 0 || row.idx >= (int)backpackGameBag_.size()) return;
    const auto& bs = backpackGameBag_[row.idx];
    std::string nm = "id " + std::to_string(bs.id);
    const auto* dd = bpFindDef(backpackDefs_, bs.id);
    if (dd) nm = dd->name;
    char body[200];
    std::snprintf(body, sizeof(body), "Togli %s x%d dallo zaino?", nm.c_str(), bs.count);
    if (!showConfirmDialog(i18n::get(StrKey::BackpackTitle), body)) return;
    if (!backpackSave_.writeGbaBagSlot(bs.pocket, bs.slot, 0, 0)) {
        DebugLog::line("backpack take: writeGbaBagSlot fallito per %s (pocket %d slot %d)",
                       nm.c_str(), (int)bs.pocket, bs.slot);
        showMessageAndWait(i18n::get(StrKey::BackpackTitle), "Scrittura fallita.");
        return;
    }
    if (!backpackPersist("take")) {
        DebugLog::line("backpack take: %s tolto in memoria ma persist fallito", nm.c_str());
        return;
    }
    DebugLog::line("backpack take: %s tolto su %s -> ok", nm.c_str(), gameInfo(backpackGame_).gameTag);
    backpackReloadItems();
    showMessageAndWait(i18n::get(StrKey::BackpackTitle), "Tolto.");
    markDirty();
}

void UI::backpackDoFixSelected() {
    // Niente cancellazioni per sbaglio: ogni fix distruttiva chiede conferma.
    size_t na = backpackAnoms_.size();
    if (backpackAuditCursor_ < 0) return;
    std::string msg;
    if ((size_t)backpackAuditCursor_ < na) {
        const auto& a = backpackAnoms_[backpackAuditCursor_];
        std::string what = (a.kind == "invalid") ? "Svuota slot?"
                         : (a.kind == "over-max") ? "Clampa al max?"
                         : "Sposta nel pocket canonico?";
        if (!showConfirmDialog(i18n::get(StrKey::BackpackTitle), what)) return;
        if (!Backpack::fixAnomaly(backpackSave_, a, backpackDefs_, msg)) {
            showMessageAndWait(i18n::get(StrKey::BackpackTitle), msg);
            return;
        }
    } else {
        size_t ji = (size_t)backpackAuditCursor_ - na;
        if (ji >= backpackJournal_.size()) return;
        const auto& jr = backpackJournal_[ji];
        int itemId = jr.item;
        std::string mode = (jr.pocket == "key") ? "Base" : "Cons.";
        char body[160];
        std::snprintf(body, sizeof(body), "Riprendi %d (%s)?", itemId, mode.c_str());
        // Nome voce se nota
        for (auto& d : backpackDefs_)
            if (d.id == itemId) { std::snprintf(body, sizeof(body), "Riprendi %s (%s)?", d.name.c_str(), mode.c_str()); break; }
        if (!showConfirmDialog(i18n::get(StrKey::BackpackTitle), body)) return;
        if (!Backpack::takeBack(backpackSave_, basePath_, backpackGame_, itemId, jr.pocket, msg)) {
            showMessageAndWait(i18n::get(StrKey::BackpackTitle), msg);
            return;
        }
    }
    if (!backpackPersist("fix")) return;
    backpackReloadItems();
    showMessageAndWait(i18n::get(StrKey::BackpackTitle), msg);
    markDirty();
}

void UI::drawBackpackPopup() {
    drawRect(0, 0, SCREEN_W, SCREEN_H, T().overlay);
    int popX = (SCREEN_W - BP_POP_W) / 2;
    int popY = (SCREEN_H - BP_POP_H) / 2;
    drawRect(popX, popY, BP_POP_W, BP_POP_H, T().panelBg);
    drawRectOutline(popX, popY, BP_POP_W, BP_POP_H, T().cursor, 2);

    std::string gname = gameDisplayNameOf(backpackGame_);
    if (gname.substr(0, 8) == "Pokemon ") gname = gname.substr(8);
    std::string title = backpackAudit_
        ? i18n::fmt(StrKey::BackpackAudit, gname)
        : i18n::get(StrKey::BackpackTitle) + std::string(" — ") + gname;
    drawTextCentered(title, popX + BP_POP_W / 2, popY + 22, T().text, font_);

    int listY = popY + 60;
    int lx = popX + 20;
    int rx = popX + 20 + BP_LEFT_W + 20;
    int rw = BP_POP_W - (rx - popX) - 20;
    // Divisore verticale
    SDL_SetRenderDrawColor(renderer_, T().popupBorder.r, T().popupBorder.g, T().popupBorder.b, T().popupBorder.a);
    SDL_RenderDrawLine(renderer_, rx - 10, listY, rx - 10, listY + BP_VISIBLE * BP_ROW_H);

    auto dot = [&](int cx, int cy, int rr, SDL_Color col) {
        SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
        for (int dy = -rr; dy <= rr; dy++) {
            int dx = static_cast<int>(std::sqrt((double)(rr * rr - dy * dy)));
            SDL_RenderDrawLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    };

    // Il catalogo cede una riga di lista alla barra tab sopra di se'
    // (solo lui: destra e verifica restano allineate a listY).
    int leftListY = listY + BP_TAB_H;
    if (!backpackAudit_) {
        // --- Barra tab categoria: ZL/ZR scritti ai due estremi (il
        // tasto che le cambia), bordi superiori arrotondati, quella
        // attiva alta quanto tutta la barra cosi' tocca il riquadro
        // sotto senza cuciture (si vede subito qual e' aperta) mentre
        // le altre restano staccate. Richiesto esplicitamente. ---
        const auto& zlTe = getTextEntry("ZL", fontSmall_, T().textDim);
        const auto& zrTe = getTextEntry("ZR", fontSmall_, T().textDim);
        int barCy = listY + BP_TAB_H / 2;
        int sideW = std::max(zlTe.w, zrTe.w) + 12;
        drawTextCentered("ZL", lx + zlTe.w / 2 + 2, barCy, T().textDim, fontSmall_);
        drawTextCentered("ZR", lx + BP_LEFT_W - zrTe.w / 2 - 2, barCy, T().textDim, fontSmall_);
        int tabsX = lx + sideW;
        int tabW = (BP_LEFT_W - 2 * sideW) / BP_CAT_COUNT;
        int tabR = 7; // raggio angoli superiori
        for (int t = 0; t < BP_CAT_COUNT; t++) {
            int tx = tabsX + t * tabW;
            bool active = (t == backpackCatTab_);
            int w = tabW - 4;
            // Attiva: alta come tutta la barra (arriva a leftListY,
            // fusa col riquadro). Inattive: un po' piu' basse e staccate.
            int fillH = active ? BP_TAB_H : (BP_TAB_H - 6);
            SDL_Color fill = active ? T().menuHighlight : T().statusBarBg;
            drawRoundRect(tx, listY, w, fillH, tabR, fill);
            drawRect(tx, listY + fillH - tabR, w, tabR, fill); // squadra il fondo
            drawTextCentered(i18n::get(bpCatTabKey(t)), tx + w / 2, listY + fillH / 2,
                             active ? T().text : T().textDim, fontSmall_);
        }
        // --- Sinistra: catalogo trasferibili, sempre visibile ---
        int n = (int)backpackLeft_.size();
        if (n == 0)
            drawTextCentered("—", lx + BP_LEFT_W / 2, leftListY + 100, T().textDim, fontSmall_);
        for (int r = 0; r < BP_VISIBLE_LEFT; r++) {
            int i = backpackLeftScroll_ + r;
            if (i >= n) break;
            int rowY = leftListY + r * BP_ROW_H;
            const auto& row = backpackLeft_[i];
            bool sel = backpackFocusItems_ && (i == backpackLeftCursor_);
            if (sel) {
                drawRect(lx - 6, rowY, BP_LEFT_W + 12, BP_ROW_H - 4, T().menuHighlight);
                drawRectOutline(lx - 6, rowY, BP_LEFT_W + 12, BP_ROW_H - 4, T().cursor, 2);
            }
            const auto& d = backpackItems_[row.idx];
            std::string nm = d.name;
            if (nm.length() > 34) nm = nm.substr(0, 33) + ".";
            drawText(nm, lx + 8, rowY + 7, T().text, fontSmall_);
            int moveId = bpTmHmMoveId(d);
            bool isKeyItem = d.pocket == "key";
            // Per gli Speciali non ripetiamo piu' l'etichetta pocket ("Base")
            // su ogni riga: la tab stessa ora si chiama col nome reale del
            // gioco, quindi scriverlo anche su ogni oggetto era ridondante
            // (richiesto esplicitamente).
            std::string right = (moveId >= 0) ? MoveName::get((uint16_t)moveId)
                                 : (isKeyItem ? std::string() : i18n::get(bpPocketKey(d.pocket)));
            if (d.flag) right += right.empty() ? "!" : " !";
            bool ownedHere = isKeyItem && backpackOwned_.count(d.id) != 0;
            if (ownedHere) {
                std::string sep = right.empty() ? std::string() : (right + "  ");
                const auto& teSep = getTextEntry(sep, fontSmall_, T().textDim);
                const auto& teOwn = getTextEntry(i18n::get(StrKey::BpOwned), fontSmall_, T().text);
                int xStart = lx + BP_LEFT_W - 8 - teSep.w - teOwn.w;
                if (!sep.empty())
                    drawText(sep, xStart, rowY + 7, T().textDim, fontSmall_);
                TTF_SetFontStyle(fontSmall_, TTF_STYLE_BOLD);
                drawText(i18n::get(StrKey::BpOwned), xStart + teSep.w, rowY + 7, T().text, fontSmall_);
                TTF_SetFontStyle(fontSmall_, TTF_STYLE_NORMAL);
            } else if (!right.empty()) {
                const auto& te = getTextEntry(right, fontSmall_, T().textDim);
                drawText(right, lx + BP_LEFT_W - 8 - te.w, rowY + 7, T().textDim, fontSmall_);
            }
        }
    } else {
        // --- Sinistra: anomalie + regalati (giornale) ---
        size_t na = backpackAnoms_.size();
        size_t nj = backpackJournal_.size();
        size_t total = na + nj;
        if (total == 0)
            drawTextCentered("OK", lx + BP_LEFT_W / 2, leftListY + 100, T().textDim, fontSmall_);
        for (int r = 0; r < BP_VISIBLE_LEFT; r++) {
            size_t i = (size_t)backpackAuditScroll_ + r;
            if (i >= total) break;
            int rowY = leftListY + r * BP_ROW_H;
            bool sel = backpackFocusItems_ && ((int)i == backpackAuditCursor_);
            if (sel) {
                drawRect(lx - 6, rowY, BP_LEFT_W + 12, BP_ROW_H - 4, T().menuHighlight);
                drawRectOutline(lx - 6, rowY, BP_LEFT_W + 12, BP_ROW_H - 4, T().cursor, 2);
            }
            std::string txt;
            if (i < na) {
                const auto& a = backpackAnoms_[i];
                std::string nm = "id " + std::to_string(a.id);
                for (auto& d : backpackDefs_)
                    if (d.id == a.id) { nm = d.name; break; }
                txt = nm + " x" + std::to_string(a.count) + " [" + a.kind + "]";
            } else {
                const auto& jr = backpackJournal_[i - na];
                std::string nm = "id " + std::to_string(jr.item);
                for (auto& d : backpackDefs_)
                    if (d.id == jr.item) { nm = d.name; break; }
                std::string mode = (jr.pocket == "key") ? "Base" : "Cons.";
                txt = nm + " x" + std::to_string(jr.qty) + " [" + mode + "]";
            }
            if (txt.length() > 52) txt = txt.substr(0, 51) + ".";
            drawText(txt, lx + 8, rowY + 7, T().text, fontSmall_);
        }
    }

    // --- Destra: lista giochi finche' non scegli, poi al suo posto lo
    // zaino VERO del gioco scelto (torni ai giochi con B) ---
    if (!backpackGameChosen_) {
        int gn = (int)backpackGames_.size();
        for (int r = 0; r < BP_VISIBLE; r++) {
            int i = backpackGameScroll_ + r;
            if (i >= gn) break;
            int rowY = listY + r * BP_ROW_H;
            bool sel = !backpackFocusItems_ && (i == backpackGameCursor_);
            if (sel) {
                drawRect(rx - 6, rowY, rw + 12, BP_ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 6, rowY, rw + 12, BP_ROW_H - 4, T().cursor, 2);
            }
            GameType g = backpackGames_[i];
            auto acIt = gameAccentCache_.find(g);
            SDL_Color accent = (acIt != gameAccentCache_.end()) ? acIt->second : flatBgColorFor(g);
            dot(rx + 14, rowY + (BP_ROW_H - 4) / 2, 5, accent);
            std::string nm = gameDisplayNameOf(g);
            if (nm.substr(0, 8) == "Pokemon ") nm = nm.substr(8);
            if (nm.length() > 24) nm = nm.substr(0, 23) + ".";
            drawText(nm, rx + 28, rowY + 7, T().text, fontSmall_);
        }
    } else {
        int bn = (int)backpackBag_.size();
        if (bn == 0)
            drawTextCentered("—", rx + rw / 2, listY + 100, T().textDim, fontSmall_);
        for (int r = 0; r < BP_VISIBLE; r++) {
            int i = backpackBagScroll_ + r;
            if (i >= bn) break;
            int rowY = listY + r * BP_ROW_H;
            const auto& row = backpackBag_[i];
            if (row.header) {
                std::string hdr = i18n::get(bpPocketKeyByEnum((SaveFile::GbaBagPocket)row.pocket));
                drawTextCentered(hdr, rx + rw / 2, rowY + 7, T().textDim, fontSmall_);
                continue;
            }
            bool sel = !backpackFocusItems_ && (i == backpackBagCursor_);
            if (sel) {
                drawRect(rx - 6, rowY, rw + 12, BP_ROW_H - 4, T().menuHighlight);
                drawRectOutline(rx - 6, rowY, rw + 12, BP_ROW_H - 4, T().cursor, 2);
            }
            const auto& bs = backpackGameBag_[row.idx];
            std::string nm = "id " + std::to_string(bs.id);
            const auto* dd = bpFindDef(backpackDefs_, bs.id);
            if (dd) nm = dd->name;
            if (nm.length() > 24) nm = nm.substr(0, 23) + ".";
            drawText(nm, rx + 8, rowY + 7, T().text, fontSmall_);
            char qb[48];
            std::snprintf(qb, sizeof(qb), "x%d", bs.count);
            const auto& te = getTextEntry(qb, fontSmall_, T().textDim);
            drawText(qb, rx + rw - 8 - te.w, rowY + 7, T().textDim, fontSmall_);
        }
    }

    // --- Barra modo/qty + footer ---
    int barY = popY + BP_POP_H - 64;
    if (!backpackAudit_ && backpackGameChosen_ && backpackFocusItems_ &&
        backpackLeftCursor_ >= 0 && backpackLeftCursor_ < (int)backpackLeft_.size() &&
        !backpackLeft_[backpackLeftCursor_].header) {
        const auto& d = backpackItems_[backpackLeft_[backpackLeftCursor_].idx];
        bool isBase = backpackBaseMode_ || d.key;
        std::string mode = isBase ? "Base" : "Cons.";
        auto ow = backpackOwned_.find(d.id);
        char qb[96];
        // Per gli oggetti Base la quantita' non ha senso (sempre x1):
        // mostriamo solo la modalita', e se gia' posseduto una piccola
        // scritta in grassetto al posto del "Hai: xN".
        if (isBase)
            std::snprintf(qb, sizeof(qb), "Modo: %s", mode.c_str());
        else if (ow != backpackOwned_.end())
            std::snprintf(qb, sizeof(qb), "Modo: %s   Q.ta: x%d (max %d)   Hai: x%d", mode.c_str(),
                          backpackQty_, d.max, ow->second);
        else
            std::snprintf(qb, sizeof(qb), "Modo: %s   Q.ta: x%d (max %d)", mode.c_str(),
                          backpackQty_, d.max);
        const auto& qte = getTextEntry(qb, fontSmall_, T().selected);
        drawText(qb, lx, barY, T().selected, fontSmall_);
        if (isBase && ow != backpackOwned_.end()) {
            TTF_SetFontStyle(fontSmall_, TTF_STYLE_BOLD);
            drawText(i18n::get(StrKey::BpOwned), lx + qte.w + 16, barY, T().text, fontSmall_);
            TTF_SetFontStyle(fontSmall_, TTF_STYLE_NORMAL);
        }
    } else if (backpackAudit_) {
        drawTextCentered(i18n::get(StrKey::BackpackAuditFooter), popX + BP_POP_W / 2, barY + 22,
                         T().textDim, fontSmall_);
        return;
    }
    drawTextCentered(i18n::get(backpackGameChosen_ ? StrKey::BackpackFooterGame : StrKey::BackpackFooter),
                     popX + BP_POP_W / 2, popY + BP_POP_H - 20, T().textDim, fontSmall_);
}

void UI::handleBackpackInput(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int16_t lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            updateStick(lx, ly);
        }
        // ZL/ZR: cambia tab categoria nel catalogo, sempre attivo perche'
        // il catalogo e' sempre visibile a sinistra. Su Switch sono
        // grilletti "digitali" ma SDL li espone come assi: stesso schema
        // edge-trigger gia' usato per lo ZR dei preferiti altrove nell'app,
        // con variabili di stato dedicate per non interferire con quello.
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            bool pressed = event.caxis.value > TRIGGER_DEADZONE;
            if (pressed && !backpackZlHeld_ && !backpackAudit_) {
                backpackCatTabStep(-1);
                markDirty();
            }
            backpackZlHeld_ = pressed;
        }
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            bool pressed = event.caxis.value > TRIGGER_DEADZONE;
            if (pressed && !backpackZrHeld_ && !backpackAudit_) {
                backpackCatTabStep(1);
                markDirty();
            }
            backpackZrHeld_ = pressed;
        }
    }
    // Levetta verticale: il repeat "mentre resta ferma" e' nel tick
    // per-frame di ui_selectors.cpp ("Joystick repeat navigation"), non
    // qui: qui dentro arriviamo solo quando SDL manda un evento, e a
    // levetta ferma puo' non arrivarne piu' nessuno (era il bug: col
    // d-pad funzionava perche' ogni pressione e' un evento, con la
    // levetta ferma no).
    if (event.type != SDL_CONTROLLERBUTTONDOWN) return;
    markDirty();
    int ng = (int)backpackGames_.size();
    switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            backpackStickStep(-1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            backpackStickStep(1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            backpackFocusStep(-1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            backpackFocusStep(1);
            break;
        case SDL_CONTROLLER_BUTTON_B: // Switch A
            if (backpackAudit_) backpackDoFixSelected();
            else if (backpackFocusItems_) {
                if (!backpackGameChosen_) {
                    // Prima il gioco: sposta a destra invece di regalare nel vuoto
                    showMessageAndWait(i18n::get(StrKey::BackpackTitle),
                                       i18n::get(StrKey::BackpackPickGame));
                    backpackFocusItems_ = false;
                } else {
                    backpackDoGift();
                }
            } else if (!backpackGameChosen_) {
                // Scelta gioco esplicita: apre il suo zaino al posto della
                // lista giochi, passa alle voci del catalogo.
                if (ng > 0) {
                    backpackLoadGame(backpackGames_[backpackGameCursor_],
                                     backpackOcc_[backpackGameCursor_]);
                    if (backpackLoaded_) {
                        backpackGameChosen_ = true;
                        backpackFocusItems_ = true;
                        // Rifai le righe ora che backpackGameChosen_ e' vero:
                        // backpackLoadGame ha gia' ricaricato ma con lo zaino
                        // vero ancora nascosto (era falso durante la sua
                        // stessa chiamata interna).
                        backpackReloadItems();
                    } else {
                        showMessageAndWait(i18n::get(StrKey::MountError),
                                           i18n::get(StrKey::FailedMountSave));
                    }
                }
            } else {
                // Gioco gia' scelto, focus sullo zaino vero a destra: togli.
                backpackDoTake();
            }
            break;
        case SDL_CONTROLLER_BUTTON_X: // Switch Y: Base/Cons (solo voci del catalogo)
            if (backpackFocusItems_ && !backpackAudit_ &&
                backpackLeftCursor_ >= 0 && backpackLeftCursor_ < (int)backpackLeft_.size() &&
                !backpackLeft_[backpackLeftCursor_].header) {
                backpackBaseMode_ = !backpackBaseMode_;
            }
            break;
        case SDL_CONTROLLER_BUTTON_Y: // Switch X: verifica
            backpackAudit_ = !backpackAudit_;
            backpackAuditCursor_ = 0;
            backpackAuditScroll_ = 0;
            if (backpackAudit_) {
                // La verifica vive a sinistra: se il fuoco era sul pannello
                // destro il d-pad avrebbe continuato a muovere il cursore
                // dello zaino/lista giochi mentre l'utente guarda le
                // anomalie (edge bug 2026-09-13).
                backpackFocusItems_ = true;
                backpackRefreshAudit();
            }
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: // Q.tà -
            // Serve il gioco scelto: senza la barra modo/quantità è nascosta
            // e cambiare la quantità prima della scelta non è visibile.
            if (backpackFocusItems_ && !backpackAudit_ && backpackGameChosen_ &&
                backpackLeftCursor_ >= 0 && backpackLeftCursor_ < (int)backpackLeft_.size() &&
                !backpackLeft_[backpackLeftCursor_].header) {
                backpackQty_--;
                if (backpackQty_ < 1) backpackQty_ = 1;
            }
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // Q.tà +
            if (backpackFocusItems_ && !backpackAudit_ && backpackGameChosen_ &&
                backpackLeftCursor_ >= 0 && backpackLeftCursor_ < (int)backpackLeft_.size() &&
                !backpackLeft_[backpackLeftCursor_].header) {
                const auto& d = backpackItems_[backpackLeft_[backpackLeftCursor_].idx];
                backpackQty_++;
                if (backpackQty_ > d.max) backpackQty_ = d.max;
            }
            break;
        case SDL_CONTROLLER_BUTTON_A: // Switch B
        case SDL_CONTROLLER_BUTTON_BACK:
        case SDL_CONTROLLER_BUTTON_START:
            if (backpackAudit_) backpackAudit_ = false;
            else if (backpackGameChosen_) {
                // Torna alla lista giochi al posto dello zaino vero
                // (esattamente come le banche): richiude il gioco corrente
                // ma NON tutto il popup.
                backpackGameChosen_ = false;
                backpackFocusItems_ = false;
                if (!backpackSaveMnt_.empty()) {
                    account_.unmountSave();
                    backpackSaveMnt_.clear();
                }
                backpackLoaded_ = false;
                backpackReloadItems();
            } else closeBackpack();
            break;
    }
}

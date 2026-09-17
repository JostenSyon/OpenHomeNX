#pragma once
#include "save_file.h"
#include <string>
#include <vector>

// Zaino Gen3 (RSE/FRLG) + Gen4/5 DS: regala strumenti in borsa, traccia tutto
// in un giornale e valida il contenuto. Mai silenzioso, mai incoerente: id
// validi dal DB (romfs/data/items_gen3/4/5.json caricati insieme, gameOk
// filtra per gioco), count sempre <= max del pocket, tutto scritto via
// SaveFile (checksum ricalcolati da saveGBA/saveDS4/saveDS5).
namespace Backpack {

struct ItemDef {
    int id = 0;
    std::string name;
    std::string pocket; // "items"|"key"|"balls"|"tm"|"berries"|"mail"|"med"|"battle"
    bool key = false;
    int max = 99;
    bool ruby = false, emerald = false, frlg = false;
    bool dp = false, pt = false, hgss = false;
    bool bw = false, b2w2 = false;
    bool flag = false; // biglietto evento: richiede anche il flag (solo Gen3)
};

// Carica il DB. False + err se manca/corroto (mai proseguire alla cieca).
bool loadDefs(const std::string& jsonPath, std::vector<ItemDef>& out, std::string& err);
// True se la voce vale per questo gioco (filtro per gioco: es. Eone mai su Perla,
// e qui mai su Gen != 3).
bool gameOk(GameType g, const ItemDef& d);
// Pocket canonico della voce (per i consumabili).
SaveFile::GbaBagPocket canonPocket(const ItemDef& d);

// Regala: consumabile = pocket canonico con stacking fino a max (atomico:
// o tutto o niente); base = slot Key Items (protetto: mai consumato dal
// gioco, come da video ACE — il decremento cerca solo il pocket canonico).
// Ritorna false + msg se borsa piena / voce non valida / flag richiesto.
bool gift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
          int qty, bool baseMode, std::string& msg);

// Riprendi indietro (riconcilia col giornale): sottrae fino al totale
// regalato per gioco+item+tasca; dice esplicito quanto mancava (già usato).
// pocket ("key"|"balls"|"tm"|"berries"|"items"): quale istanza riprendere.
// Lo stesso oggetto puo' essere stato regalato sia come Base (Key Items)
// sia come Consumabile (tasca canonica): mai indovinare quale togliere,
// il chiamante lo sa gia' perche' journalFor() elenca le due voci separate
// quando esistono entrambe (bug 2026-09-13: rimuoveva la prima trovata
// per id su qualunque tasca, anche quella sbagliata).
bool takeBack(SaveFile& sf, const std::string& basePath, GameType g,
              int itemId, const std::string& pocket, std::string& msg);

// Voce di giornale per un gioco (per la vista audit): una riga per
// {item, tasca} - MAI unite, cosi' lo stesso item regalato sia Base che
// Consumabile resta distinguibile e riprendibile senza ambiguita'.
struct JournalRow {
    int item = 0;
    std::string pocket;
    int qty = 0;
};
std::vector<JournalRow> journalFor(const std::string& basePath, GameType g);

// Svuota il giornale (TUTTI i giochi): richiesto esplicitamente per non dover
// cancellare backpack.json a mano quando si accumula testando regali/prese.
// Non tocca i save: i regali gia' dati restano nella borsa reale, si perde
// solo la memoria di "quanto e stato dato da qui" (takeBack su voci vecchie
// dira' "mai regalato da qui" invece di poterle togliere).
bool clearJournal(const std::string& basePath);

// Scansione validità borsa: "invalid" (id ignoto/non valido per il gioco),
// "over-max" (count oltre max), "protected" (pocket non canonico: base-mode
// nostro o ACE altrui — info, non errore; offri sposta/rimuovi).
struct Anomaly {
    SaveFile::GbaBagPocket pocket;
    int slot = 0;
    uint16_t id = 0, count = 0;
    std::string kind; // "invalid"|"over-max"|"protected"
};
std::vector<Anomaly> scanBag(SaveFile& sf, const std::vector<ItemDef>& defs);

// Sposta uno slot nel pocket canonico (per i "protected" ripensati) o
// clamp a max per gli "over-max". Ritorna descrizione di ciò che ha fatto.
bool fixAnomaly(SaveFile& sf, const Anomaly& a, const std::vector<ItemDef>& defs,
                std::string& msg);

// --- Zaino DS (Gen4/5): stesso contratto del Gen3, tasche Mail/Med/Battle in
// piu'. Nessun flag evento (DB DS: flag sempre false; se mai true, gift
// rifiuta esplicito come il path Gen3). Giornale condiviso (chiave = gioco).
struct DsAnomaly {
    SaveFile::DsBagPocket pocket;
    int slot = 0;
    uint16_t id = 0, count = 0;
    std::string kind; // "invalid"|"over-max"|"protected"
};
// Voce DB valida per questo gioco + tasca canonica DS.
const ItemDef* dsFindDef(GameType g, const std::vector<ItemDef>& defs, int id);
SaveFile::DsBagPocket dsCanonPocket(const ItemDef& d);
std::string dsPocketToStr(SaveFile::DsBagPocket p);
SaveFile::DsBagPocket dsPocketFromStr(const std::string& p);
bool dsGift(SaveFile& sf, const std::string& basePath, const ItemDef& d,
            int qty, bool baseMode, std::string& msg);
bool dsTakeBack(SaveFile& sf, const std::string& basePath, GameType g,
                int itemId, const std::string& pocket, std::string& msg);
std::vector<DsAnomaly> dsScanBag(SaveFile& sf, const std::vector<ItemDef>& defs);
bool dsFixAnomaly(SaveFile& sf, const DsAnomaly& a, const std::vector<ItemDef>& defs,
                  std::string& msg);

} // namespace Backpack

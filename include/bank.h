#pragma once
#include "pokemon.h"
#include "game_type.h"
#include <string>
#include <vector>

// Bank - persistent storage for extracted Pokemon.
// Stores decrypted Pokemon data in a simple binary file format.
class Bank {
public:

    Bank();

    void setGameType(GameType g);
    GameType gameType() const { return gameType_; }

    // Cross-gen bank (approach B): slots hold raw OHPKM records instead of a
    // single native format. No conversion on the way in; conversion happens only
    // when a mon is pulled out to a save. 32 boxes x 30 slots. gameType_ is set
    // to a pivot (Scarlet/PK9) purely so any fallthrough renders sanely.
    bool isCrossGen() const { return crossGen_; }
    void makeCrossGen();
    // OHPKM blob for a cross-gen slot ([] tag+section bytes as produced by
    // openhome_get_ohpkm_bytes). Empty vector = empty slot / out of range.
    const std::vector<uint8_t>& ohpkmAt(int box, int slot) const;
    void setOhpkmAt(int box, int slot, std::vector<uint8_t> blob);
    void clearOhpkmAt(int box, int slot);

    // Load bank from file. Returns true on success; creates empty bank if file missing.
    bool load(const std::string& path);

    // Lightweight header check: true only if the file exists and has a valid
    // PKHOUSE magic + supported version. Used to filter out stray .bin files
    // that users drop into the bank folder.
    static bool isValidFile(const std::string& path);

    // True if the file's header version is VERSION_CROSSGEN. Cheap header read.
    static bool isCrossGenFile(const std::string& path);

    // Save bank to file.
    bool save(const std::string& path);

    // Dirty-tracking (Round 1): true se il contenuto in memoria differisce
    // dall'ultimo load()/save() riuscito. Nessuno lo legge ancora (lo userà
    // la UI per saltare scritture inutili). Vive nei setter, non nella UI.
    bool isDirty() const { return dirty_; }

    Pokemon getSlot(int box, int slot) const;
    void setSlot(int box, int slot, const Pokemon& pkm);
    void clearSlot(int box, int slot);

    std::string getBoxName(int box) const;
    void setBoxName(int box, const std::string& name);

    int boxCount() const { return boxCount_; }
    int slotsPerBox() const { return slotsPerBox_; }
    int totalSlots() const { return boxCount_ * slotsPerBox_; }
    size_t fileSize() const {
        if (crossGen_)
            return HEADER_SIZE + 4 + (size_t)totalSlots() * 4 + (size_t)boxCount_ * BOX_NAME_SIZE;
        return HEADER_SIZE + (size_t)totalSlots() * slotSize_ + (size_t)boxCount_ * BOX_NAME_SIZE;
    }

private:
    // File format:
    //   [8 bytes]  Magic: "PKHOUSE\0"
    //   [4 bytes]  Version (u32 LE): 1 = 32 boxes, 2 = 40 boxes
    //   [4 bytes]  Reserved
    //   [N bytes]  totalSlots * slotSize_ decrypted data
    //   [M bytes]  boxCount_ * 16  box names (null-padded)
    //   [optional] "OHBKP\0\0\0" + u32 count + count * { u32 slotIdx, u32 len,
    //              <len bytes ohBackup_ blob = [tag u16 LE][record]] }
    //   The OHBKP section is only written when at least one slot carries an
    //   OHPKM OriginalBackup; older readers stop after the box names and ignore
    //   it, so the version field is left unchanged.
    static constexpr int HEADER_SIZE   = 16;
    static constexpr int SLOT_SIZE     = PokeCrypto::SIZE_9PARTY;
    static constexpr int BOX_NAME_SIZE = 16;

    static constexpr char MAGIC[8] = {'P','K','H','O','U','S','E','\0'};
    static constexpr char OHBKP_MAGIC[8] = {'O','H','B','K','P','\0','\0','\0'};
    static constexpr uint32_t OHBKP_MAX_BLOB = 512; // tag(2) + largest record
    static constexpr uint32_t VERSION_32BOX  = 1;
    static constexpr uint32_t VERSION_40BOX  = 2;
    static constexpr uint32_t VERSION_LA     = 3;
    static constexpr uint32_t VERSION_LGPE   = 4;
    static constexpr uint32_t VERSION_FRLG   = 5;
    static constexpr uint32_t VERSION_CROSSGEN = 6; // slots = len-prefixed OHPKM
    static constexpr uint32_t VERSION_GB     = 7; // Gen1 native (12 box x 20 x 55B)
    static constexpr uint32_t OHPKM_MAX_BLOB   = 8192; // serialized OhpkmV2 upper bound

    GameType gameType_ = GameType::ZA;
    int boxCount_ = 32;
    int slotsPerBox_ = 30;
    int slotSize_ = PokeCrypto::SIZE_9PARTY;
    bool crossGen_ = false;
    bool dirty_ = false;
    std::vector<Pokemon> slots_;
    std::vector<std::vector<uint8_t>> ohpkmSlots_; // used only when crossGen_
    std::vector<std::string> boxNames_;

    uint32_t fileVersion() const {
        if (crossGen_) return VERSION_CROSSGEN;
        if (isFRLG(gameType_) || isImportedFile(gameType_)) return VERSION_FRLG;
        if (isGen1File(gameType_)) return VERSION_GB;
        if (isLGPE(gameType_)) return VERSION_LGPE;
        if (gameType_ == GameType::LA) return VERSION_LA;
        return boxCount_ == 40 ? VERSION_40BOX : VERSION_32BOX;
    }

    int slotIndex(int box, int slot) const {
        return box * slotsPerBox_ + slot;
    }
};

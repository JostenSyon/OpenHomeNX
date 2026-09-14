#include "item_locations.h"

namespace ItemLocations {

std::string itemName(uint16_t modernId) {
    switch (modernId) {
        case 221: return "King's Rock";
        case 233: return "Metal Coat";
        case 235: return "Dragon Scale";
        case 252: return "Up-Grade";
        case 226: return "Deep Sea Tooth";
        case 227: return "Deep Sea Scale";
        case 321: return "Protector";
        case 322: return "Electirizer";
        case 323: return "Magmarizer";
        case 324: return "Dubious Disc";
        case 325: return "Reaper Cloth";
        case 537: return "Prism Scale";
        case 646: return "Whipped Dream";
        case 647: return "Sachet";
        default: return "Item " + std::to_string(modernId);
    }
}

std::vector<Where> locations(uint16_t modernId) {
    switch (modernId) {
        case 233: // Metal Coat
            return {
                {"O/A/C", "Magnemite 2% / Magneton 2% selvatico"},
                {"R/Z/S/E/FRLG", "Magnemite 2% selvatico"},
                {"D/P/PT/HG/SS", "Magnemite 5% / Bronzor 5%"},
                {"N/B/N2/B2", "Magnemite 5% / Klefki? no"},
                {"X/Y", "Magnemite 5% / Klefki 5%"},
                {"S/M/US/UM", "Magnezone SOS 5%"},
                {"Sp/Sc", "Porto Marinada asta"},
            };
        case 221: // King's Rock
            return {
                {"O/A/C", "Slowpoke 2% / Poliwhirl selvatico"},
                {"R/Z/S/E", "Hariyama 5%? no — Roccia di Re evento"},
                {"D/P/PT/HG/SS", "Poliwhirl 5% / Slowpoke 5%"},
                {"X/Y", "Poliwhirl SOS / Negozio pietre"},
                {"Sp/Sc", "Asta Porto / Picnic?"},
            };
        case 235: // Dragon Scale
            return {
                {"O/A/C", "Horsea 2%"},
                {"R/Z/S/E", "Horsea 5% / Bagon 2%"},
                {"D/P/PT/HG/SS", "Horsea 5% / Dratini 5%"},
                {"Sp/Sc", "Asta / Raid"},
            };
        case 252: // Up-Grade
            return {
                {"O/A/C", "Porygon selvatico evento?"},
                {"R/Z/S/E", "Silph? no — solo evento"},
                {"D/P/PT/HG/SS", "Porygon 5% / Azienda Silph"},
                {"Sp/Sc", "Asta / Negozio"},
            };
        case 226: // Deep Sea Tooth
            return {
                {"R/Z/S/E", "Clamperl 5% / Relicanth? no"},
                {"D/P/PT", "Clamperl 5%"},
                {"Sp/Sc", "Asta"},
            };
        case 227: // Deep Sea Scale
            return {
                {"R/Z/S/E", "Clamperl 5% / Chinchou?"},
                {"D/P/PT", "Clamperl 5%"},
                {"Sp/Sc", "Asta"},
            };
        case 321: // Protector
            return { {"D/P/PT/HG/SS", "Rhydon selvatico / Monte Corona"}, {"X/Y", "Rhydon 5%"}, {"Sp/Sc", "Asta"} };
        case 322: // Electirizer
            return { {"D/P/PT/HG/SS", "Electabuzz 5% / Valle Vento"}, {"X/Y", "Electabuzz 5%"}, {"Sp/Sc", "Asta"} };
        case 323: // Magmarizer
            return { {"D/P/PT/HG/SS", "Magmar 5% / Monte Ostile"}, {"X/Y", "Magmar 5%"}, {"Sp/Sc", "Asta"} };
        case 324: // Dubious Disc
            return { {"D/P/PT/HG/SS", "Porygon2 selvatico / Via Vittoria"}, {"X/Y", "Porygon2 5%"} };
        case 325: // Reaper Cloth
            return { {"D/P/PT/HG/SS", "Dusclops 5% / Via Vittoria"}, {"X/Y", "Dusclops 5%"} };
        case 647: // Sachet
            return { {"X/Y", "Spritzee 5% selvatico"}, {"S/M", "Spritzee 5% SOS"} };
        case 646: // Whipped Dream
            return { {"X/Y", "Swirlix 5% selvatico"}, {"S/M", "Swirlix 5% SOS"} };
        case 537: // Prism Scale
            return { {"N/B/N2/B2", "Feebas 5% / Relitto"}, {"X/Y", "Feebas 5%"} };
        default: return {};
    }
}

std::string footerLine(uint16_t modernId) {
    if (modernId == 0) return "";
    auto name = itemName(modernId);
    auto w = locations(modernId);
    if (w.empty()) return name;
    std::string out = name + " — ";
    for (size_t i = 0; i < w.size() && i < 3; i++) {
        if (i) out += " | ";
        out += w[i].gameTag + ": " + w[i].how;
    }
    if (w.size() > 3) out += " | ...";
    return out;
}

} // namespace ItemLocations

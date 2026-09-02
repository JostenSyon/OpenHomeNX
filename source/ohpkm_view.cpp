#include "ohpkm_view.h"

OhpkmView readOhpkmView(const std::vector<uint8_t>& ohpkmBlob) {
    if (ohpkmBlob.empty()) return {};
    PkmHandle* h = OpenHomeNX::loadOhpkm(ohpkmBlob);
    if (!h) return {};
    OhpkmView v;
    v.species = OpenHomeNX::ohpkmSpecies(h);
    v.form = OpenHomeNX::ohpkmForm(h);
    v.level = OpenHomeNX::ohpkmLevel(h);
    v.shiny = OpenHomeNX::ohpkmIsShiny(h);
    v.gender = OpenHomeNX::ohpkmGender(h);
    v.heldItem = OpenHomeNX::ohpkmHeldItem(h);
    v.originGen = OpenHomeNX::ohpkmOriginGen(h);
    v.nickname = OpenHomeNX::ohpkmNickname(h);
    OpenHomeNX::freePkm(h);
    v.valid = true;
    return v;
}

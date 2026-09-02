#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "openhome_ffi.h"

struct OhpkmView {
    bool valid = false;
    uint16_t species = 0;
    uint16_t form = 0;
    uint16_t heldItem = 0;
    uint8_t level = 0;
    uint8_t gender = 0;
    uint8_t originGen = 0;
    bool shiny = false;
    std::string nickname;
};

OhpkmView readOhpkmView(const std::vector<uint8_t>& ohpkmBlob);

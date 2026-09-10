#pragma once
#include <string>
#include <cstddef>

struct SDL_Surface;
// Copia RGBA con angoli arrotondati (per le texture delle card giochi).
SDL_Surface* roundCornersSurface(SDL_Surface* src, int radius);

inline std::string formatSize(size_t bytes) {
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / 1024) + " KB";
}

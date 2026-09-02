#pragma once
#include <string>

// Reads the DisplayVersion string from a devkitPro NRO's embedded NACP
// (the "ASET" asset section appended after the NRO image). Returns false if the
// file can't be opened or isn't a valid NRO0 + ASET with a NACP section.
bool readNroDisplayVersion(const std::string& nroPath, std::string& outVersion);

// Compares dotted numeric versions like "0.1.3" (up to 3 components; missing
// components are 0; any non-digit tail is ignored).
// Returns <0 if a < b, 0 if equal, >0 if a > b.
int compareVersionStrings(const std::string& a, const std::string& b);

// Byte-copies src -> dst (overwriting dst). Returns false on any read/write
// error, in which case dst may be left partially written by the caller's cleanup.
bool copyFileTo(const std::string& src, const std::string& dst);

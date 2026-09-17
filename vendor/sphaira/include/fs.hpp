// SHIM OpenHomeNX (non Sphaira): forward declaration per far parsare gli
// header yati (nca.hpp/ncm.hpp) senza trascinare fs.cpp dell'app Sphaira.
// Il percorso forwarder non chiama mai fs:: a runtime.
#pragma once

namespace sphaira::fs {

struct FsPath;
struct Fs;
struct FsNativeSd;

} // namespace sphaira::fs

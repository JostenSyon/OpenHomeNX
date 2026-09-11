#pragma once

// Debug logger — attivabile via flag OH_DEBUG_LOG e file di abilitazione runtime.
//
// Compilazione:
//   make DEFINES="-DOH_DEBUG_LOG"
//   oppure senza flag: tutte le funzioni diventano no-op inline.
//
// Abilitazione runtime (zero overhead se disattivato), due modi equivalenti:
//   1. creare il file  basePath + "debug.enable"  prima dell'avvio
//   2. dal menu in-app: voce "Debug log: on/off" (toggle con A)
//
// Output: basePath + "debug.log" in append, ogni riga con timestamp HH:MM:SS.

#include <cstdio>
#include <ctime>
#include <string>

#ifdef OH_DEBUG_LOG

namespace DebugLog {

// Inizializza il logger con la base path (es. "sdmc:/switch/pkHouse/").
// Abilita solo se esiste il file basePath + "debug.enable".
void init(const std::string& basePath);

// Restituisce true se il logger è attivo.
bool enabled();

// Accende/spegne il logger a runtime (dal menu). Alla prima accensione apre
// basePath + "debug.log" e scrive il banner di RUN. Richiede init() già fatto.
void setEnabled(bool on);

// Scrive una riga formattata in stile printf nel log, con timestamp e fflush.
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Per upload: chiude temporaneamente il file così curl può rileggerlo.
// Dopo l'upload richiama reopenAfterUpload() per continuare a loggare.
bool flushAndReopenForUpload(std::string& outPath);
void reopenAfterUpload();
std::string logPath();

} // namespace DebugLog

#else // OH_DEBUG_LOG

namespace DebugLog {

inline void init(const std::string&) {}
inline bool enabled() { return false; }
inline void setEnabled(bool) {}
inline void line(const char*, ...) {}
inline bool flushAndReopenForUpload(std::string&) { return false; }
inline void reopenAfterUpload() {}
inline std::string logPath() { return ""; }

} // namespace DebugLog

#endif // OH_DEBUG_LOG

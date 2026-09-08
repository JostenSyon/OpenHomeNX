#pragma once
#include <string>

// Auto-check update al boot, mai bloccante: start() lancia un singolo thread
// libnx che fa fetch+confronto versione; il main thread ritira con
// takeResult() (una sola volta) e decide la UX. Il worker non tocca SDL né
// DebugLog (non thread-safe): riporta solo stringhe, logga il main.
void autoUpdateStart(const std::string& url, const std::string& token,
                     const std::string& curVer);

// Ritorna true una sola volta, quando il worker ha trovato una versione più
// recente (outVersion = "x.y.z"). False altrimenti (ancora in corso, niente
// di nuovo, o già ritirato).
bool autoUpdateTakeResult(std::string& outVersion);

// Legge update.cfg (stessi due path di ui_selectors.cpp): ritorna true solo
// con `auto=1`; url/token restano vuoti se assenti (url vuoto = default GitHub).
bool readUpdateAutoCfg(const std::string& basePath, std::string& urlOut,
                       std::string& tokenOut);

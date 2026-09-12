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

// Sola lettura, NON consuma lo stato (a differenza di takeResult qui sopra,
// che lo resetta appena legge s_state==2, trovi o no qualcosa). True solo
// quando il worker ha finito e non ha trovato una versione piu' recente.
// Serve ad agganciare eventi "boot pulito, autocheck concluso senza
// aggiornamenti" -- va letta PRIMA che takeResult() giri nello stesso
// frame, altrimenti trova gia' lo stato consumato a 0.
bool autoUpdateFinishedWithoutUpdate();

// Logga una sola volta l'esito del worker appena è done (qualsiasi screen):
// serve a distinguere "fetch in corso" da "morto prima della fetch".
void autoUpdateLogOnceDone();

// Da chiamare in uscita prima di smontare rete/USB: ferma il worker e lo
// aspetta (mai due volte). Senza: fetch a metà teardown = crash in uscita.
void autoUpdateJoin();

// Legge update.cfg (stessi due path di ui_selectors.cpp): ritorna true solo
// con `auto=1`; url/token restano vuoti se assenti (url vuoto = default GitHub).
bool readUpdateAutoCfg(const std::string& basePath, std::string& urlOut,
                       std::string& tokenOut);

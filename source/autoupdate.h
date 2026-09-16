#pragma once
#include <string>

// Auto-check update al boot, mai bloccante: start() lancia un singolo thread
// libnx che fa fetch+confronto versione; il main thread ritira con
// takeResult() (una sola volta) e decide la UX. Il worker non tocca SDL né
// DebugLog (non thread-safe): riporta solo stringhe, logga il main.
// beta=true (solo se url vuoto): risolve la pre-release corrente via API
// invece del latest stabile. Custom url vince sempre sul canale.
void autoUpdateStart(const std::string& url, const std::string& token,
                     const std::string& curVer, bool beta = false);

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

// True se il worker autoupdate NON sta lavorando in questo momento: mai
// partito (update.cfg senza auto=1: niente da aspettare) oppure gia' finito,
// in entrambi i casi comunque sia andata (trovato, non trovato, errore).
// Unica differenza da autoUpdateFinishedWithoutUpdate(): quella e' vera solo
// nel caso "finito senza nulla di nuovo"; questa e' vera anche se non e' mai
// partito o se ha trovato un aggiornamento -- serve solo a sapere "e' sicuro
// avviare un'altra richiesta di rete in background adesso, senza competere
// con l'autoupdate", non a decidere la UX (quella resta alle due funzioni
// sopra). Usata da remoteSyncWorkerStart() in UI::run() per non fargli mai
// gareggiare con il check/download dell'aggiornamento per le stesse
// sessioni di rete.
bool autoUpdateSettled();

// Logga una sola volta l'esito del worker appena è done (qualsiasi screen):
// serve a distinguere "fetch in corso" da "morto prima della fetch".
void autoUpdateLogOnceDone();

// Da chiamare in uscita prima di smontare rete/USB: ferma il worker e lo
// aspetta (mai due volte). Senza: fetch a metà teardown = crash in uscita.
void autoUpdateJoin();

// Legge update.cfg (stessi due path di ui_selectors.cpp): ritorna true solo
// con `auto=1`; url/token restano vuoti se assenti (url vuoto = default GitHub).
// channelOut = "beta" se canale beta, "" altrimenti.
bool readUpdateAutoCfg(const std::string& basePath, std::string& urlOut,
                       std::string& tokenOut, std::string& channelOut);

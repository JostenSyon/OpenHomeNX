#pragma once
#include <string>
#include <functional>

// Forwarder on-device stile Sphaira: genera il title direttamente
// sulla console via spl/ncm in RAM — niente prod.keys, niente file
// template. Applet check resta fuori (qui solo generazione).
//
// onProgress (opzionale): richiamata in modo sincrono, dallo stesso
// thread chiamante, con brevi messaggi di stato durante l'operazione
// (owo.cpp puo' chiamarla piu' volte o mai). Serve solo a permettere
// alla UI di mostrare showWorking() invece di restare ferma per i
// secondi che servono a costruire il forwarder -- non e' garanzia di
// copertura completa dei passi interni di owo.cpp.
using ForwarderProgressFn = std::function<void(const std::string&)>;
bool forwarderInstall(const std::string& nroPath, std::string& err,
                       ForwarderProgressFn onProgress = nullptr);

// Inoltra un messaggio di stato al callback impostato dalla chiamata a
// forwarderInstall() attualmente in corso. Chiamata dallo shim
// ProgressBox (vendor/sphaira/source/shim.cpp); no-op se nessuna
// installazione e' in corso o se non e' stato passato un callback.
void forwarderNotifyProgress(const std::string& msg);

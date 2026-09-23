#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <switch.h>

// Lavoro in background su thread dedicato, riusabile da qualunque punto
// dell'app che oggi blocca il main thread su I/O (rete o disco).
//
// Regole (valgono per QUALUNQUE consumer, non solo il primo):
// - il worker fa SOLO rete/file/calcolo (mai SDL_Render*, texture,
//   showWorking, dialog, envSetNextLoad: il render resta sul main thread);
// - DebugLog::line dal worker e' ok (mutex interno);
// - curl: easy-handle per chiamata (come gia' fa update_net.cpp),
//   curl_global_init() solo al boot (vedi updateNetInitCurl) -- mai dal worker;
// - cancel e' cooperativo (niente kill): il worker lo controlla nei punti
//   giusti, il main lo alza su input utente;
// - mai detach: join() sempre prima di distruggere o riusare l'oggetto.
//
// Thread libnx (stessa convenzione di autoupdate.cpp/remote_sync.cpp, con
// stack esplicito allocato da libnx): funziona su Switch e, via shim, su
// R36S. Niente std::thread/SDL_thread per non riverificare altro.
//
// Esempio d'uso minimale (pump-loop modale nel chiamante):
//   BackgroundJob job;
//   bool cancel = false;
//   job.start([&](BackgroundJob& j) {
//       // ... lavoro con j.report(riga) e j.cancelled() nei punti giusti ...
//   }, 256 * 1024);
//   while (!job.done()) {
//       std::string line;
//       job.poll(line);
//       showWorking(line);          // draw sul main thread
//       pumpEvents();               // B -> cancel = true (condiviso col worker
//                                   // se il lavoro legge un flag esterno)
//       SDL_Delay(16);
//   }
//   job.join();
class BackgroundJob {
public:
    using Fn = std::function<void(BackgroundJob&)>;

    BackgroundJob() = default;
    ~BackgroundJob() { join(); }
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;

    // Lancia fn sul worker con stack esplicito allocato da libnx (lo
    // statico in .bss falliva su HW con OutOfMemory, vedi autoupdate.cpp).
    // Ritorna false se il thread non parte (in quel caso il chiamante
    // esegue fn in locale o fallisce esplicito, mai hang: done() e'
    // subito true).
    bool start(Fn fn, size_t stackBytes = 256 * 1024);

    // Ultima riga pubblicata dal worker ("" se nessuna). Thread-safe.
    void poll(std::string& out);

    // Il worker ha finito (o start mai riuscito/fallito). Thread-safe.
    // Non fa join: chiamare join() dopo.
    bool done() const { return finished_.load(std::memory_order_acquire); }

    // Attende il worker e libera le risorse (idempotente). Dal main thread.
    void join();

    // --- lato worker ---
    // Pubblica una riga di avanzamento (formato libero a carico del
    // chiamante, es. "(10%) 1/10\nnome" per showWorking).
    void report(const std::string& line);
    // true se il main ha chiesto l'annullamento.
    bool cancelled() const { return cancel_.load(std::memory_order_acquire); }

    // --- lato main ---
    void requestCancel() { cancel_.store(true, std::memory_order_release); }

private:
    static void workerEntry(void* arg);

    Fn fn_;
    Thread thread_{};
    bool threadLive_ = false;
    std::mutex mutex_;
    std::string line_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{true};
};

#include "job.h"
#include "debug_log.h"

void BackgroundJob::workerEntry(void* arg) {
    auto* self = static_cast<BackgroundJob*>(arg);
    self->fn_(*self);
    self->finished_.store(true, std::memory_order_release);
}

bool BackgroundJob::start(Fn fn, size_t stackBytes) {
    join(); // riuso dopo un giro precedente: chiudi prima
    fn_ = std::move(fn);
    cancel_.store(false, std::memory_order_release);
    finished_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        line_.clear();
    }
    Result rc = threadCreate(&thread_, workerEntry, this, nullptr,
                             (s64)stackBytes, 0x2C, -2);
    if (R_FAILED(rc)) {
        DebugLog::line("job: threadCreate 0x%08X", (unsigned)rc);
        finished_.store(true, std::memory_order_release);
        return false;
    }
    rc = threadStart(&thread_);
    if (R_FAILED(rc)) {
        DebugLog::line("job: threadStart 0x%08X", (unsigned)rc);
        threadClose(&thread_);
        finished_.store(true, std::memory_order_release);
        return false;
    }
    threadLive_ = true;
    return true;
}

void BackgroundJob::poll(std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out = line_;
}

void BackgroundJob::report(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    line_ = line;
}

void BackgroundJob::join() {
    if (!threadLive_) return;
    threadWaitForExit(&thread_);
    threadClose(&thread_);
    threadLive_ = false;
}

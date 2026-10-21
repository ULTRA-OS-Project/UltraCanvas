// Apps/UltraDBAdmin/engine/DbAdminSqlRunner.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSqlRunner.h"

#include <chrono>
#include <utility>

namespace UltraDBAdmin {

SqlRunner::SqlRunner(std::string connection, size_t historyLimit)
    : connection_(std::move(connection)), historyLimit_(historyLimit == 0 ? 1 : historyLimit) {}

SqlRunner::~SqlRunner() {
    if (running_) Cancel();
    if (worker_.joinable()) worker_.join();
}

bool SqlRunner::Start(const std::string& script, std::function<void(const RunOutcome&)> done) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    // The previous run has finished (running_ was false), so its thread only
    // has to be collected.
    if (worker_.joinable()) worker_.join();
    cancelRequested_ = false;
    Remember(script);
    worker_ = std::thread([this, script, done = std::move(done)] {
        RunOutcome outcome;
        outcome.script = script;
        const auto start = std::chrono::steady_clock::now();
        outcome.result = UltraDb_ExecBatch(connection_, script, outcome.statements);
        outcome.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        outcome.cancelled = outcome.result.code == UltraDbResultCode::Cancelled ||
                            (cancelRequested_ && !outcome.result.success);
        running_ = false;
        if (done) done(outcome);
    });
    return true;
}

void SqlRunner::Cancel() {
    if (!running_) return;
    cancelRequested_ = true;
    UltraDb_CancelQuery(connection_);
}

void SqlRunner::Remember(const std::string& script) {
    std::lock_guard<std::mutex> lock(historyMutex_);
    if (!history_.empty() && history_.front() == script) return;
    history_.insert(history_.begin(), script);
    if (history_.size() > historyLimit_) history_.resize(historyLimit_);
}

std::vector<std::string> SqlRunner::History() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return history_;
}

} // namespace UltraDBAdmin

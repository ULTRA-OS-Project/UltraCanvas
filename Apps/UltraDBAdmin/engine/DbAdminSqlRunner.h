// Apps/UltraDBAdmin/engine/DbAdminSqlRunner.h
// The SQL console's engine: one script at a time on a worker thread, so the
// window stays responsive while a query runs, and a Stop that reaches it
// (UltraDb_CancelQuery). The outcome - every statement with its own result -
// is handed to a callback on the worker thread; the window moves it to the UI
// thread itself. Keeps the history of what was run. Also runs an imported
// SQL file, which is not kept in the history and can run as one transaction.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraDBAdmin {

struct RunOutcome {
    std::string script;
    std::vector<UltraDbStatementResult> statements;   // up to and including the first failure
    UltraDbResult result;                              // that failure, or success
    double seconds = 0.0;
    bool cancelled = false;                            // stopped by Cancel()
    std::string label;                                 // RunOptions::label
    bool transaction = false;                          // ran as one transaction
    bool rolledBack = false;                           // ... which was rolled back: nothing stayed
};

struct RunOptions {
    // BEGIN before the script, COMMIT after it - or ROLLBACK when a statement
    // fails or Stop is pressed, so nothing of it stays. For a script that
    // does not BEGIN, COMMIT or ROLLBACK itself (ScriptSummary::ownTransactions).
    bool oneTransaction = false;
    bool remember = true;      // into the history; not an imported file
    std::string label;         // what the window calls it: the file's name
};

class SqlRunner {
public:
    explicit SqlRunner(std::string connection, size_t historyLimit = 200);
    ~SqlRunner();                       // waits for a running script, after cancelling it

    SqlRunner(const SqlRunner&) = delete;
    SqlRunner& operator=(const SqlRunner&) = delete;

    const std::string& Connection() const { return connection_; }

    // Start `script`. False (and nothing started) while one is still running.
    // `done` is called once, on the worker thread, when it ends.
    bool Start(const std::string& script, std::function<void(const RunOutcome&)> done,
               const RunOptions& options = RunOptions());

    // Ask the running script to stop. Harmless when none runs.
    void Cancel();

    bool IsRunning() const { return running_; }

    // Scripts run, newest first; the same script twice in a row is kept once.
    std::vector<std::string> History() const;

private:
    void Remember(const std::string& script);

    std::string connection_;
    size_t historyLimit_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::thread worker_;
    mutable std::mutex historyMutex_;
    std::vector<std::string> history_;
};

} // namespace UltraDBAdmin

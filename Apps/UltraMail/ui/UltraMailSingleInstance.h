// Apps/UltraMail/ui/UltraMailSingleInstance.h
// One UltraMail per user. Two copies on the one mailbox would sync the same
// accounts twice, write the same database from two processes and send the
// same outbox twice, so a second start does not open a window: it asks the
// copy already running to come forward, and quits.
//
// Who runs is decided by the framework's instance lock on
// `<data folder>/instance.lock` (UltraCanvasInstanceLock.h), which the system
// drops when the process ends, so a crashed UltraMail never blocks the next
// start. The "come forward" travels over UltraMessage, the per-user message
// bus: the topic `org.ultraos.ultramail.show`, sent as a request to the
// running copy, which answers before it acts.
//
//   SingleInstance instance;
//   switch (instance.Claim(dataDir)) {
//       case SingleInstance::Start::First: break;            // carry on
//       case SingleInstance::Start::HandedOff: return 0;     // the running one came forward
//       case SingleInstance::Start::AlreadyRunning: ...      // say so, then quit
//   }
//   ... create the window ...
//   instance.Listen([&] { /* raise the window */ });
//   ...
//   instance.Close();                                        // before the window goes
//
// Without UltraMessage (a build without it) the lock still stops a second
// copy; it just cannot bring the first one forward.
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasInstanceLock.h"

#include <cstdint>
#include <functional>
#include <string>

namespace UltraMail {

class SingleInstance {
public:
    enum class Start {
        First,           // this process is UltraMail: it holds the lock
        HandedOff,       // another copy runs and came forward: quit
        AlreadyRunning   // another copy runs but did not answer: say so, quit
    };
    using ShowHandler = std::function<void()>;

    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // Takes the lock in `dataDir`. When another UltraMail holds it, asks that
    // one to come forward, waiting a few seconds for one that is still
    // starting. A lock that cannot be taken for any other reason (an
    // unwritable folder, a platform without locks) does not stop the start.
    Start Claim(const std::string& dataDir);
    // From now on a later start's request arrives in `handler`, on the UI
    // thread (call after the application exists, from the First copy).
    void Listen(ShowHandler handler);
    // Stops listening and leaves the bus; no handler runs after it returns.
    // The lock is kept until the object goes, so no second copy slips in
    // while this one shuts down.
    void Close();

private:
    bool HandOff();

    UltraCanvas::InstanceLock lock_;
    uint64_t endpoint_ = 0;
    uint64_t subscription_ = 0;
    ShowHandler handler_;
};

} // namespace UltraMail

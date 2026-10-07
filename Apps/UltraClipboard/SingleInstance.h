// Apps/UltraClipboard/SingleInstance.h
// One UltraClipboard per user. A second start hands what it was asked for -
// a search, an entry to edit - to the one already running and quits, so the
// desktop's quick panel brings the open window forward instead of starting
// another. The hand-off travels over UltraMessage, the per-user message bus:
// the topic `org.ultraos.ultraclipboard.show` (`search`, `editId`), sent as a
// request to the running instance, which answers before it acts.
//
//   SingleInstance instance;
//   if (instance.HandOff({search, editId})) return 0;   // the running one has it
//   ... create the window ...
//   instance.Listen([&](const SingleInstance::Request& r) { window->Present(r.search, r.editId); });
//   ...
//   instance.Close();                                    // before the window goes
//
// The first instance hosts the bus when nothing else does, so the next start
// finds it. Without UltraMessage (a build without UltraDatabase) or when the
// bus cannot be reached, every start runs on its own, as before.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace UltraClipboard {

class SingleInstance {
public:
    struct Request {
        std::string search;
        int64_t editId = 0;
    };
    using Handler = std::function<void(const Request&)>;

    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // Joins the bus and offers `request` to a running UltraClipboard. True
    // when one took it: this process has nothing left to do.
    bool HandOff(const Request& request);
    // From now on the requests of later starts arrive in `handler`, on the UI
    // thread (call after the application exists).
    void Listen(Handler handler);
    // Stops listening and leaves the bus; no handler runs after it returns.
    void Close();

private:
    uint64_t endpoint_ = 0;
    uint64_t subscription_ = 0;
    Handler handler_;
};

} // namespace UltraClipboard

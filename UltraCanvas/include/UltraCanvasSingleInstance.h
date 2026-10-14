// include/UltraCanvasSingleInstance.h
// One running copy of an application per user. A second start does not open
// another window: it asks the copy already running to come forward - handing
// it whatever this start was asked for - and quits.
//
//   UltraCanvasSingleInstance instance("org.ultraos.deviceexplorer", "DeviceExplorer");
//   const auto start = instance.Claim();
//   if (start == UltraCanvasSingleInstance::Start::HandedOff) return 0;
//   UltraCanvasApplication app;
//   app.Initialize("DeviceExplorer");
//   if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
//       return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "DeviceExplorer");
//   ... create and show the window ...
//   instance.ListenRaise();      // a later start brings the main window forward
//   app.Run();
//   instance.Close();            // before the windows go
//
// An application that opens files hands the second start's files over
// instead, and the running copy opens them:
//
//   instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(paths));
//   ...
//   instance.ListenOpen([&](const std::vector<std::string>& paths) { window->OpenFiles(paths); });
//
// Who runs is decided by an operating-system lock (UltraCanvasInstanceLock.h)
// on `<settings folder>/instances/<appId>.lock`, which the system drops when
// the process ends, however it ends: two starts at the same moment can never
// both stay, and a crash never blocks the next start. The request to come
// forward travels over UltraMessage, the per-user message bus, as the topic
// `<appId>.show`; the first copy hosts the bus when nothing else does.
//
// NOT part of the core library: the source is compiled into each application
// that asks for it, with `ultracanvas_single_instance(<target>)`
// (cmake/UltraCanvasSingleInstance.cmake), which also links UltraMessage where
// the build has it. Without UltraMessage the lock still stops a second copy;
// it just cannot bring the first one forward, and Claim() answers
// AlreadyRunning.
// Version: 1.1.0 - MakeOpenRequest / ListenOpen: the second start's files go
//                  to the running copy, as absolute paths
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasInstanceLock.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasSingleInstance {
    public:
        enum class Start {
            First,           // this process is the application: it holds the lock
            HandedOff,       // another copy runs and took the request: quit
            AlreadyRunning   // another copy runs but did not answer: say so, quit
        };
        using Handler = std::function<void(const JSONValue& request)>;
        using OpenHandler = std::function<void(const std::vector<std::string>& paths)>;

        // `appId` is reverse-DNS ("org.ultraos.deviceexplorer"): the lock
        // file's name and the bus topic come from it. `displayName` is what
        // the "already running" alert and the bus call the application.
        UltraCanvasSingleInstance(std::string appId, std::string displayName);
        ~UltraCanvasSingleInstance();
        UltraCanvasSingleInstance(const UltraCanvasSingleInstance&) = delete;
        UltraCanvasSingleInstance& operator=(const UltraCanvasSingleInstance&) = delete;

        // Takes the lock - in `lockDirectory` when given, else in the per-user
        // settings folder. When another copy holds it, offers it `request`
        // (an object; empty means "just come forward"), waiting up to ten
        // seconds for a copy that is still starting. A lock that cannot be
        // taken for any other reason (an unwritable folder, a platform
        // without locks) does not stop the start: it answers First.
        Start Claim(const JSONValue& request = JSONValue::MakeObject(),
                    const std::string& lockDirectory = std::string());

        // From now on a later start's request arrives in `handler`, on the UI
        // thread, after the other start has been answered. Call after the
        // application exists, from the First copy.
        void Listen(Handler handler);
        // Listen() with the usual handler: BringToFront().
        void ListenRaise();
        // Listen() for an application that opens files: a later start's
        // files (MakeOpenRequest) arrive in `openPaths`, absolute and UTF-8,
        // and the application opens them - in a new window, a tab, whatever
        // it does with a file - and raises what it opened. A later start
        // without files gets BringToFront() instead.
        void ListenOpen(OpenHandler openPaths);

        // The request for Claim() that carries this start's files:
        // {"paths": [...]}. Each path is made absolute against this
        // process's working folder first - the running copy was started
        // somewhere else - and an argument with "://" in it (a URL) passes
        // through as it is.
        static JSONValue MakeOpenRequest(const std::vector<std::string>& paths);
        // The paths of such a request; empty when it carries none.
        static std::vector<std::string> RequestPaths(const JSONValue& request);
        // Stops listening and leaves the bus; no handler runs after it
        // returns. The lock is kept until the object goes, so no second copy
        // slips in while this one shuts down.
        void Close();

        // Restores and raises the application's main window - the first
        // window that is not a dialog - or, when a dialog is open, that
        // dialog, which keeps what is typed in it.
        static void BringToFront();
        // Says "<displayName> is already running" and runs the event loop
        // until the alert is dismissed. Returns EXIT_SUCCESS, for main().
        static int RunAlreadyRunningAlert(UltraCanvasApplication& app, const std::string& displayName);

        const std::string& GetAppId() const { return appId_; }

    private:
        bool HandOff(const JSONValue& request);

        std::string appId_;
        std::string displayName_;
        InstanceLock lock_;
        uint64_t endpoint_ = 0;
        uint64_t subscription_ = 0;
        Handler handler_;
    };

} // namespace UltraCanvas

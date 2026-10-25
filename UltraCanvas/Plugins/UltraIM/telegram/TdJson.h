// Plugins/UltraIM/telegram/TdJson.h
// Telegram provider - TDLib's JSON interface (libtdjson), loaded at run time:
// nothing of TDLib is linked, so the provider builds everywhere and works
// wherever a libtdjson is found. One thread receives for every client in the
// process (td_receive serves them all) and hands each its answers and
// updates.
// Version: 0.1.1 - SetLogFile (TDLib's own log); what is sent and received on the debug stream
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraIMTelegram {

using TdObject = UltraCanvas::JSONValue;
using TdHandler = std::function<void(const TdObject& object)>;

// The library itself: found once, never unloaded (TDLib does not support it).
class TdJsonLibrary {
public:
    static TdJsonLibrary& Instance();

    // Tries `candidates` in order (a full path, or a name the system's loader
    // searches for), then ULTRAIM_TDJSON_PATH, then the platform's usual name
    // (libtdjson.so / libtdjson.dylib / tdjson.dll). True once loaded.
    bool Load(const std::vector<std::string>& candidates, std::string& error);
    bool IsLoaded() const;
    std::string GetPath() const;
    std::string GetVersion();       // TDLib's, through td_execute
    // TDLib's own log (setLogStream / setLogVerbosityLevel): to `path`,
    // kept under maxBytes, at `verbosity` (0 fatal, 1 errors, 2 warnings,
    // 3 info, 4 debug). An empty path sends it back to stderr.
    bool SetLogFile(const std::string& path, int verbosity, int64_t maxBytes = 10 * 1024 * 1024);

    // A synchronous request (td_execute): only the methods TDLib marks so.
    TdObject Execute(const TdObject& request);

    // ---- clients ----------------------------------------------------------------
    // A new client id; `onObject` receives everything TDLib sends it, on the
    // receive thread, until Unregister. TDLib starts the client on its first
    // request.
    int CreateClient(TdHandler onObject);
    void Send(int clientId, const TdObject& request);
    // After this returns, `onObject` is not running and will not run again.
    void Unregister(int clientId);

private:
    TdJsonLibrary() = default;
    ~TdJsonLibrary();
    void ReceiveLoop();

    struct Api {
        int (*createClientId)() = nullptr;
        void (*send)(int, const char*) = nullptr;
        const char* (*receive)(double) = nullptr;
        const char* (*execute)(const char*) = nullptr;
    };

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    Api api_;
    std::string path_;
    std::map<int, TdHandler> clients_;
    int dispatching_ = 0;          // the client whose handler runs now (0 = none)
    std::thread receiver_;
    bool receiving_ = false;
    std::mutex executeMutex_;      // td_execute's answer lives until the next call
};

// One TDLib client, with requests answered by callback (matched by @extra).
class TdClient {
public:
    TdClient() = default;
    ~TdClient();
    TdClient(const TdClient&) = delete;
    TdClient& operator=(const TdClient&) = delete;

    // `onUpdate` gets every object that is not an answer to a request.
    bool Open(TdHandler onUpdate, std::string& error);
    bool IsOpen() const { return clientId_ != 0; }
    void Send(TdObject request, TdHandler onAnswer = nullptr);
    // Waits for the answer; never from the receive thread (an update handler).
    TdObject Await(TdObject request, int timeoutMs);
    // Ends the client: `close`, then waits (up to timeoutMs) for TDLib to
    // report it closed. No handler runs after this returns.
    void Close(int timeoutMs = 10000);

private:
    void OnObject(const TdObject& object);

    std::atomic<int> clientId_{0};
    std::mutex mutex_;
    std::condition_variable closed_;
    bool isClosed_ = false;
    uint64_t nextExtra_ = 1;
    std::map<std::string, TdHandler> waiting_;     // @extra -> handler
    TdHandler onUpdate_;
};

// "@type" of an object.
inline std::string TdType(const TdObject& o) { return o["@type"].GetString(); }
// An integer TDLib may send as a number (int32, int53) or a string (int64).
int64_t TdInt(const TdObject& o, int64_t fallback = 0);

} // namespace UltraIMTelegram

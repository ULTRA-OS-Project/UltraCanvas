// Plugins/UltraIM/telegram/TdJson.cpp
// Telegram provider - TDLib's JSON interface, loaded at run time
// Version: 0.1.1 - SetLogFile (TDLib's own log); what is sent and received on the debug stream
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework

#include "TdJson.h"

#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPluginLoader.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UltraIMTelegram {

namespace {

const char* PlatformName() {
#if defined(_WIN32)
    return "tdjson.dll";
#elif defined(__APPLE__)
    return "libtdjson.dylib";
#else
    return "libtdjson.so";
#endif
}

TdObject Parse(const char* text) {
    UltraCanvas::JSONParseResult parsed;
    return UltraCanvas::JSON::Parse(text ? std::string(text) : std::string(), &parsed);
}

// One line about an object for the debug stream: its type, the request it
// answers, and an error's code and message. Never the payload - a request
// carries the phone number, the code, the password and the messages.
std::string Describe(const TdObject& o) {
    std::string line = TdType(o);
    const std::string extra = o["@extra"].GetString();
    if (!extra.empty()) line += " (" + extra + ")";
    if (line.rfind("error", 0) == 0)
        line += " " + std::to_string(o["code"].GetInteger(0)) + " \"" + o["message"].GetString() + "\"";
    else if (TdType(o) == "updateAuthorizationState")
        line += " -> " + TdType(o["authorization_state"]);
    else if (TdType(o) == "updateConnectionState")
        line += " -> " + TdType(o["state"]);
    return line;
}

// Opens the library. On Windows a DLL loaded by its full path does not find
// the DLLs next to it (OpenSSL and zlib ship beside tdjson.dll), so its folder
// joins the search path for the duration of the load.
UltraCanvas::UCPluginLibHandle OpenLibrary(const std::string& path, std::string* why) {
#if defined(_WIN32)
    const std::filesystem::path full = UltraCanvas::PathFromUtf8(path);
    if (full.has_parent_path() && full.is_absolute()) {
        SetDllDirectoryW(full.parent_path().c_str());   // path-string-ok: wide path
        UltraCanvas::UCPluginLibHandle handle = UltraCanvas::UCPluginOpen(path, why);
        SetDllDirectoryW(nullptr);
        return handle;
    }
#endif
    return UltraCanvas::UCPluginOpen(path, why);
}

} // namespace

int64_t TdInt(const TdObject& o, int64_t fallback) {
    if (o.IsString()) {
        const std::string s = o.GetString();
        if (s.empty()) return fallback;
        char* end = nullptr;
        const long long v = std::strtoll(s.c_str(), &end, 10);
        return end && *end == '\0' ? static_cast<int64_t>(v) : fallback;
    }
    return o.IsNumber() ? o.GetInteger(fallback) : fallback;
}

// ============================================================================
// LIBRARY
// ============================================================================

TdJsonLibrary& TdJsonLibrary::Instance() {
    static TdJsonLibrary library;
    return library;
}

TdJsonLibrary::~TdJsonLibrary() {
    // At exit: a client still open keeps the thread in td_receive, and
    // TDLib's own statics may already be going - let it go with the process.
    std::lock_guard<std::mutex> lock(mutex_);
    receiving_ = false;
    if (receiver_.joinable()) receiver_.detach();
}

bool TdJsonLibrary::Load(const std::vector<std::string>& candidates, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (api_.send) return true;
    std::vector<std::string> paths = candidates;
    const std::string fromEnv = UltraCanvas::GetEnvUtf8("ULTRAIM_TDJSON_PATH");
    if (!fromEnv.empty()) paths.push_back(fromEnv);
    paths.push_back(PlatformName());
    std::string reasons;
    for (const auto& path : paths) {
        if (path.empty()) continue;
        std::string why;
        UltraCanvas::UCPluginLibHandle handle = OpenLibrary(path, &why);
        if (!handle) {
            debugOutput << "[Telegram] libtdjson not at " << path << ": " << why << std::endl;
            reasons += "\n  " + path + ": " + why;
            continue;
        }
        Api api;
        api.createClientId = reinterpret_cast<int (*)()>(UltraCanvas::UCPluginSymbol(handle, "td_create_client_id"));
        api.send = reinterpret_cast<void (*)(int, const char*)>(UltraCanvas::UCPluginSymbol(handle, "td_send"));
        api.receive = reinterpret_cast<const char* (*)(double)>(UltraCanvas::UCPluginSymbol(handle, "td_receive"));
        api.execute = reinterpret_cast<const char* (*)(const char*)>(UltraCanvas::UCPluginSymbol(handle, "td_execute"));
        if (!api.createClientId || !api.send || !api.receive || !api.execute) {
            // Too old a TDLib (before 1.7, no td_create_client_id) or not TDLib.
            debugOutput << "[Telegram] " << path << " is not TDLib's JSON interface (1.7 or later)" << std::endl;
            reasons += "\n  " + path + ": not TDLib's JSON interface (1.7 or later)";
            UltraCanvas::UCPluginClose(handle);
            continue;
        }
        api_ = api;
        path_ = path;
        // Quiet: TDLib logs to stderr at level 1 and up otherwise.
        const char* ok = api_.execute("{\"@type\":\"setLogVerbosityLevel\",\"new_verbosity_level\":1}");
        (void)ok;
        const char* version = api_.execute("{\"@type\":\"getOption\",\"name\":\"version\"}");
        debugOutput << "[Telegram] loaded " << path << ": TDLib "
                    << Parse(version)["value"].GetString("?") << std::endl;
        return true;
    }
    error = "TDLib (libtdjson) was not found - install it, or set ULTRAIM_TDJSON_PATH" + reasons;
    debugOutput << "[Telegram] " << error << std::endl;
    return false;
}

bool TdJsonLibrary::SetLogFile(const std::string& path, int verbosity, int64_t maxBytes) {
    if (!IsLoaded()) return false;
    TdObject stream = TdObject::MakeObject();
    if (path.empty()) {
        stream.Set("@type", TdObject("logStreamDefault"));
    } else {
        stream.Set("@type", TdObject("logStreamFile"));
        stream.Set("path", TdObject(path));
        stream.Set("max_file_size", TdObject(maxBytes));
        stream.Set("redirect_stderr", TdObject(false));
    }
    TdObject setStream = TdObject::MakeObject();
    setStream.Set("@type", TdObject("setLogStream"));
    setStream.Set("log_stream", stream);
    const TdObject streamAnswer = Execute(setStream);
    TdObject setLevel = TdObject::MakeObject();
    setLevel.Set("@type", TdObject("setLogVerbosityLevel"));
    setLevel.Set("new_verbosity_level", TdObject(static_cast<int64_t>(verbosity)));
    const TdObject levelAnswer = Execute(setLevel);
    const bool ok = TdType(streamAnswer) == "ok" && TdType(levelAnswer) == "ok";
    debugOutput << "[Telegram] TDLib's log: " << (path.empty() ? std::string("stderr") : path)
                << " at level " << verbosity << (ok ? "" : " - refused: " + Describe(streamAnswer) + " / " + Describe(levelAnswer))
                << std::endl;
    return ok;
}

bool TdJsonLibrary::IsLoaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return api_.send != nullptr;
}

std::string TdJsonLibrary::GetPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

std::string TdJsonLibrary::GetVersion() {
    TdObject request = TdObject::MakeObject();
    request.Set("@type", TdObject("getOption"));
    request.Set("name", TdObject("version"));
    return Execute(request)["value"].GetString();
}

TdObject TdJsonLibrary::Execute(const TdObject& request) {
    const char* (*execute)(const char*) = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        execute = api_.execute;
    }
    if (!execute) return TdObject();
    std::lock_guard<std::mutex> lock(executeMutex_);
    return Parse(execute(UltraCanvas::JSON::Serialize(request).c_str()));
}

int TdJsonLibrary::CreateClient(TdHandler onObject) {
    std::thread finished;
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!api_.createClientId) return 0;
        id = api_.createClientId();
        clients_[id] = std::move(onObject);
        if (!receiving_) {
            if (receiver_.joinable()) finished = std::move(receiver_);
            receiving_ = true;
            receiver_ = std::thread([this]() { ReceiveLoop(); });
        }
    }
    if (finished.joinable()) finished.join();
    return id;
}

void TdJsonLibrary::Send(int clientId, const TdObject& request) {
    void (*send)(int, const char*) = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        send = api_.send;
    }
    if (!send || !clientId) return;
    if (UltraCanvas::IsDebugOutputEnabled())
        debugOutput << "[Telegram] -> client " << clientId << ": " << Describe(request) << std::endl;
    send(clientId, UltraCanvas::JSON::Serialize(request).c_str());
}

void TdJsonLibrary::Unregister(int clientId) {
    std::thread stopped;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        clients_.erase(clientId);
        const bool onReceiver = std::this_thread::get_id() == receiver_.get_id();
        if (!onReceiver) idle_.wait(lock, [&]() { return dispatching_ != clientId; });
        // Nobody left to receive for: the thread ends (within a second).
        if (clients_.empty() && receiving_ && !onReceiver) {
            receiving_ = false;
            stopped = std::move(receiver_);
        }
    }
    if (stopped.joinable()) stopped.join();
}

void TdJsonLibrary::ReceiveLoop() {
    for (;;) {
        const char* (*receive)(double) = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!receiving_) return;
            receive = api_.receive;
        }
        const char* text = receive(1.0);
        if (!text) continue;
        const TdObject object = Parse(text);       // td_receive's text lives until its next call
        const int clientId = static_cast<int>(object["@client_id"].GetInteger(0));
        if (UltraCanvas::IsDebugOutputEnabled())
            debugOutput << "[Telegram] <- client " << clientId << ": " << Describe(object) << std::endl;
        TdHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = clients_.find(clientId);
            if (it == clients_.end()) continue;
            handler = it->second;
            dispatching_ = clientId;
        }
        handler(object);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            dispatching_ = 0;
        }
        idle_.notify_all();
    }
}

// ============================================================================
// CLIENT
// ============================================================================

TdClient::~TdClient() {
    Close(2000);
}

bool TdClient::Open(TdHandler onUpdate, std::string& error) {
    if (clientId_) return true;
    if (!TdJsonLibrary::Instance().IsLoaded()) {
        error = "TDLib is not loaded";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        onUpdate_ = std::move(onUpdate);
        isClosed_ = false;
        waiting_.clear();
    }
    clientId_ = TdJsonLibrary::Instance().CreateClient([this](const TdObject& o) { OnObject(o); });
    if (!clientId_) {
        error = "TDLib did not create a client";
        return false;
    }
    return true;
}

void TdClient::OnObject(const TdObject& object) {
    TdHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string extra = object["@extra"].GetString();
        auto it = extra.empty() ? waiting_.end() : waiting_.find(extra);
        if (it != waiting_.end()) {
            handler = std::move(it->second);
            waiting_.erase(it);
        } else {
            handler = onUpdate_;
        }
        if (TdType(object) == "updateAuthorizationState" &&
            TdType(object["authorization_state"]) == "authorizationStateClosed") {
            isClosed_ = true;
            closed_.notify_all();
        }
    }
    if (handler) handler(object);
}

void TdClient::Send(TdObject request, TdHandler onAnswer) {
    const int id = clientId_;
    if (!id) {
        if (onAnswer) {
            TdObject error = TdObject::MakeObject();
            error.Set("@type", TdObject("error"));
            error.Set("code", TdObject(500));
            error.Set("message", TdObject("Not connected to TDLib"));
            onAnswer(error);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string extra = "uim-" + std::to_string(nextExtra_++);
        request.Set("@extra", TdObject(extra));
        // Answers nobody waits for still need an @extra, or they would look
        // like updates.
        waiting_[extra] = onAnswer ? std::move(onAnswer) : [](const TdObject&) {};
    }
    TdJsonLibrary::Instance().Send(id, request);
}

TdObject TdClient::Await(TdObject request, int timeoutMs) {
    struct Wait {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        TdObject answer;
    };
    auto wait = std::make_shared<Wait>();
    Send(std::move(request), [wait](const TdObject& answer) {
        std::lock_guard<std::mutex> lock(wait->mutex);
        wait->answer = answer;
        wait->done = true;
        wait->cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(wait->mutex);
    if (!wait->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() { return wait->done; })) {
        TdObject error = TdObject::MakeObject();
        error.Set("@type", TdObject("error"));
        error.Set("code", TdObject(408));
        error.Set("message", TdObject("TDLib did not answer in time"));
        return error;
    }
    return wait->answer;
}

void TdClient::Close(int timeoutMs) {
    const int id = clientId_;
    if (!id) return;
    TdObject close = TdObject::MakeObject();
    close.Set("@type", TdObject("close"));
    Send(close);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return isClosed_; });
    }
    TdJsonLibrary::Instance().Unregister(id);
    clientId_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    waiting_.clear();
    onUpdate_ = nullptr;
}

} // namespace UltraIMTelegram

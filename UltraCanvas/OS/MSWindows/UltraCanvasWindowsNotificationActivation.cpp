// OS/MSWindows/UltraCanvasWindowsNotificationActivation.cpp
// Windows backend of UltraCanvasNotificationActivation.h: this program as the
// COM activator of the toasts shown under its AppUserModelID.
//
// A toast whose AppUserModelID names a CustomActivator CLSID is handed to that
// class when it is clicked: Windows calls INotificationActivationCallback::
// Activate on the object the running program registered with
// CoRegisterClassObject, or starts the program the CLSID's LocalServer32 names
// and waits for it to register. So a click in the Action Center opens the
// application even after the process that showed the toast (the broker host)
// has quit. Everything lives under HKCU\Software\Classes - per user, no
// administrator rights - and is listed under HKCU\Software\UltraCanvas\
// Notifications for the uninstaller.
//
// The class object is served from a thread of its own in the multithreaded
// apartment, so the activation never waits for the UI thread's message loop;
// the click itself is handed to the UI thread (PostToUIThread).
// INotificationActivationCallback is declared here from its documented ABI,
// so no Windows SDK header newer than the toolchain's is needed.
//
// From an MSIX package nothing is written: Windows reads a packaged
// application's activator from the package manifest (windows.comServer and
// windows.toastNotificationActivation, which scripts/make_msix_layout.py
// declares with the same CLSID), and a packaged program's HKEY_CURRENT_USER
// writes stay inside the package, where Windows would not look. The class
// object is served all the same.
// Version: 1.2.0 - from an MSIX package: the class served, nothing written
// Version: 1.1.0 - ListNotificationRegistrations; the removal works from it
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>

#include "../../include/UltraCanvasNotificationActivation.h"
#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasPathUtf8.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasWindowsPackage.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr const wchar_t* kIndexKey = L"Software\\UltraCanvas\\Notifications";
constexpr const wchar_t* kAppUserModelIdKey = L"Software\\Classes\\AppUserModelId\\";
constexpr const wchar_t* kClassKey = L"Software\\Classes\\CLSID\\";

// {53E31837-6600-4A81-9395-75CFFE746F94}: INotificationActivationCallback.
const IID kNotificationActivationCallbackId = {
    0x53e31837, 0x6600, 0x4a81, {0x93, 0x95, 0x75, 0xcf, 0xfe, 0x74, 0x6f, 0x94}};

struct NotificationUserInputData {
    LPCWSTR Key;
    LPCWSTR Value;
};

// The interface as notificationactivationcallback.h declares it.
struct INotificationActivationCallbackAbi : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Activate(LPCWSTR appUserModelId, LPCWSTR invokedArgs,
                                               const NotificationUserInputData* data, ULONG count) = 0;
};

std::wstring Wide(const std::string& utf8) {
    return PathUtf8Detail::Utf8ToUtf16<std::wstring>(utf8);
}

std::string Narrow(const wchar_t* wide) {
    return wide ? PathUtf8Detail::Utf16ToUtf8(std::wstring(wide)) : std::string();
}

bool SetText(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

// This program's full path, however long.
std::wstring OwnExecutable() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::wstring();
        if (length < buffer.size()) return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) return std::wstring();
        buffer.resize(buffer.size() * 2);
    }
}

using Handler = std::function<void(const NotificationActivation&)>;

class Activator final : public INotificationActivationCallbackAbi {
public:
    explicit Activator(Handler handler) : handler_(std::move(handler)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kNotificationActivationCallbackId)) {
            *object = static_cast<INotificationActivationCallbackAbi*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR, LPCWSTR invokedArgs, const NotificationUserInputData*,
                                       ULONG) override {
        const NotificationActivation activation = ParseNotificationArguments(Narrow(invokedArgs));
        Handler handler = handler_;
        if (!handler) return S_OK;
        // The click belongs to the UI; Windows does not wait for it.
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([handler, activation]() { handler(activation); });
        } else {
            handler(activation);
        }
        return S_OK;
    }

private:
    Handler handler_;
    std::atomic<ULONG> references_{1};
};

class ActivatorFactory final : public IClassFactory {
public:
    explicit ActivatorFactory(Handler handler) : handler_(std::move(handler)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    // The factory lives as long as the process: it is never freed.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* activator = new Activator(handler_);
        const HRESULT result = activator->QueryInterface(riid, object);
        activator->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }

private:
    Handler handler_;
};

// The registry entries that make this program the activator of `appId`.
bool WriteRegistration(const std::string& appId, const std::wstring& classId) {
    const std::wstring program = OwnExecutable();
    if (program.empty()) return false;
    const std::wstring wideAppId = Wide(appId);
    const std::wstring classKey = std::wstring(kClassKey) + classId;
    bool ok = SetText(HKEY_CURRENT_USER, classKey, nullptr, wideAppId + L" notification activator");
    ok = ok && SetText(HKEY_CURRENT_USER, classKey + L"\\LocalServer32", nullptr,
                       L"\"" + program + L"\" -ToastActivated");
    ok = ok && SetText(HKEY_CURRENT_USER, std::wstring(kAppUserModelIdKey) + wideAppId, L"CustomActivator", classId);
    // Listed for the uninstaller, with the class to remove beside it.
    ok = ok && SetText(HKEY_CURRENT_USER, kIndexKey, wideAppId.c_str(), classId);
    return ok;
}

// The thread that serves the class objects: it joins the multithreaded
// apartment, registers each class it is handed and keeps the apartment alive.
class ActivatorServer {
public:
    static ActivatorServer& Get() {
        static ActivatorServer* server = new ActivatorServer();   // lives as long as the process
        return *server;
    }

    bool Register(const CLSID& classId, Handler handler) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!started_) {
            started_ = true;
            std::thread([this]() { Run(); }).detach();
        }
        pending_.push_back({classId, std::move(handler), false, false});
        Request& request = pending_.back();
        wake_.notify_all();
        done_.wait_for(lock, std::chrono::seconds(10), [&request]() { return request.done; });
        return request.done && request.registered;
    }

private:
    struct Request {
        CLSID classId;
        Handler handler;
        bool done;
        bool registered;
    };

    void Run() {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this]() {
                for (const Request& r : pending_)
                    if (!r.done) return true;
                return false;
            });
            for (Request& r : pending_) {
                if (r.done) continue;
                DWORD cookie = 0;
                auto* factory = new ActivatorFactory(r.handler);
                r.registered = SUCCEEDED(apartment) &&
                               SUCCEEDED(CoRegisterClassObject(r.classId, factory, CLSCTX_LOCAL_SERVER,
                                                               REGCLS_MULTIPLEUSE, &cookie));
                if (!r.registered) delete factory;
                r.done = true;
            }
            done_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    std::list<Request> pending_;   // a list: Register holds a reference while another is added
    bool started_ = false;
};

void DeleteTree(const std::wstring& path) {
    RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
}

} // namespace

bool RegisterNotificationActivator(const std::string& appId,
                                   std::function<void(const NotificationActivation&)> onActivated) {
    if (appId.empty()) return false;
    const std::string classIdText = NotificationActivatorClassId(appId);
    const std::wstring classId = Wide(classIdText);
    CLSID clsid{};
    if (FAILED(CLSIDFromString(classId.c_str(), &clsid))) return false;
    if (WindowsPackage::IsRunningFromPackage()) {
        // The manifest names the activator; say so when it does not, since a
        // click after the broker host quit then reaches nobody.
        if (!WindowsPackage::ManifestMentions(classIdText.substr(1, classIdText.size() - 2)))
            debugOutput << "UltraCanvas: the package manifest declares no notification activator for " << appId
                        << " (" << classIdText << "): add it to NOTIFICATION_ACTIVATORS in "
                        << "scripts/make_msix_layout.py" << std::endl;
    } else if (!WriteRegistration(appId, classId)) {
        debugOutput << "UltraCanvas: could not register the notification activator of " << appId << std::endl;
        return false;
    }
    if (!ActivatorServer::Get().Register(clsid, std::move(onActivated))) {
        debugOutput << "UltraCanvas: could not serve the notification activator of " << appId << " ("
                    << classIdText << ")" << std::endl;
        return false;
    }
    return true;
}

std::vector<NotificationRegistration> ListNotificationRegistrations() {
    std::vector<NotificationRegistration> listed;
    HKEY index = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kIndexKey, 0, KEY_READ, &index) != ERROR_SUCCESS) return listed;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD nameLength = 512;
        wchar_t data[128];
        DWORD dataBytes = sizeof(data);
        DWORD type = 0;
        const LONG result = RegEnumValueW(index, i, name, &nameLength, nullptr, &type,
                                          reinterpret_cast<BYTE*>(data), &dataBytes);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS || nameLength == 0) continue;
        NotificationRegistration registration;
        registration.appId = PathUtf8Detail::Utf16ToUtf8(std::wstring(name, nameLength));
        if (type == REG_SZ && dataBytes >= sizeof(wchar_t)) {
            size_t length = dataBytes / sizeof(wchar_t);
            while (length > 0 && data[length - 1] == L'\0') --length;
            registration.activatorClassId = PathUtf8Detail::Utf16ToUtf8(std::wstring(data, length));
        }
        listed.push_back(std::move(registration));
    }
    RegCloseKey(index);
    std::sort(listed.begin(), listed.end(),
              [](const NotificationRegistration& a, const NotificationRegistration& b) { return a.appId < b.appId; });
    return listed;
}

size_t RemoveNotificationRegistrations() {
    const std::vector<NotificationRegistration> listed = ListNotificationRegistrations();
    for (const NotificationRegistration& registration : listed) {
        DeleteTree(std::wstring(kAppUserModelIdKey) + Wide(registration.appId));
        if (!registration.activatorClassId.empty())
            DeleteTree(std::wstring(kClassKey) + Wide(registration.activatorClassId));
    }
    DeleteTree(kIndexKey);
    return listed.size();
}

} // namespace UltraCanvas

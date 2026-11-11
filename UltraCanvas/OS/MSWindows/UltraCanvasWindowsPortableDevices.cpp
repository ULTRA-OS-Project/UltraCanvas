// OS/MSWindows/UltraCanvasWindowsPortableDevices.cpp
// Windows Portable Devices (WPD) backend for UltraCanvasPortableDevices: the
// phones, cameras and players Explorer lists under "This PC" without a drive
// letter, reached through the same COM API Explorer uses.
//
// Every WPD call runs on ONE thread of this backend's own, in the
// multi-threaded COM apartment. A caller's thread hands the work over and
// waits for it. That gives the backend one owner for its COM objects - the
// device manager and the devices it opened stay open between calls, which is
// what keeps a second folder on the same phone from costing a new MTP
// session - and one device answers one request at a time anyway. Callers can
// be any thread, including one that is in a single-threaded apartment of its
// own (a UI thread that did OleInitialize for drag and drop).
//
// WPD addresses an object by id. A path is resolved one name at a time from
// the device object ("DEVICE"), whose children are the storages; every
// listing records the ids of the names it saw, so walking down a phone's
// folders costs one listing per folder, as it would on a disk. A call that
// fails on an id the cache handed it - the file was deleted on the phone -
// forgets the device's ids and resolves the path afresh, once.
// Version: 1.2.0 - mass-storage drives (a USB disk, a stick) are left out: they
//                  are drives with a letter, and were listed twice
// Version: 1.1.0 - Space: the storages' capacity and free space
// Version: 1.0.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasPortableDevices.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide, WideToUtf8

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>     // VariantTimeToSystemTime
#include <propidl.h>
// The WPD property keys, content types and class ids are DEFINE_GUID /
// DEFINE_PROPERTYKEY declarations; initguid.h makes this file define them,
// so no WPD GUID import library has to be linked (mingw-w64 ships none).
#include <initguid.h>
#include <propkeydef.h>
#include <portabledeviceapi.h>
#include <portabledevice.h>

namespace UltraCanvas {

    namespace {

        // ===== SMALL COM HELPERS =====

        // An owning interface pointer: Release() on the way out, so every
        // early return in the code below leaves nothing behind.
        template <class T>
        class Com {
        public:
            Com() = default;
            Com(const Com&) = delete;
            Com& operator=(const Com&) = delete;
            Com(Com&& other) noexcept : p(other.p) { other.p = nullptr; }
            Com& operator=(Com&& other) noexcept {
                if (this != &other) { Reset(); p = other.p; other.p = nullptr; }
                return *this;
            }
            ~Com() { Reset(); }
            void Reset() { if (p) { p->Release(); p = nullptr; } }
            T** Out() { Reset(); return &p; }
            void** OutVoid() { return reinterpret_cast<void**>(Out()); }
            T* operator->() const { return p; }
            T* Get() const { return p; }
            explicit operator bool() const { return p != nullptr; }
        private:
            T* p = nullptr;
        };

        // A string WPD allocated with CoTaskMemAlloc, freed on the way out.
        struct TaskString {
            LPWSTR p = nullptr;
            ~TaskString() { if (p) CoTaskMemFree(p); }
            std::wstring Text() const { return p ? std::wstring(p) : std::wstring(); }
        };

        std::string Describe(HRESULT hr) {
            char code[16];
            std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
            LPWSTR text = nullptr;
            const DWORD n = FormatMessageW(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
            std::string words;
            if (n && text) {
                words = WideToUtf8(text);
                while (!words.empty() && (words.back() == '\n' || words.back() == '\r' ||
                                          words.back() == ' ' || words.back() == '.'))
                    words.pop_back();
            }
            if (text) LocalFree(text);
            return words.empty() ? std::string(code) : words + " (" + code + ")";
        }

        // The device went away or stopped answering: the open handle is dead
        // and the next call has to open the device again.
        bool DeviceGone(HRESULT hr) {
            return hr == E_WPD_DEVICE_NOT_OPEN || hr == E_WPD_DEVICE_IS_HUNG ||
                   hr == HRESULT_FROM_WIN32(ERROR_GEN_FAILURE) ||
                   hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) ||
                   hr == HRESULT_FROM_WIN32(ERROR_NOT_READY) ||
                   hr == HRESULT_FROM_WIN32(ERROR_SEM_TIMEOUT) ||
                   hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) ||
                   hr == HRESULT_FROM_WIN32(ERROR_BAD_COMMAND);
        }

        // A VT_DATE (local time, which is what MTP devices report) as seconds
        // since the epoch, UTC. 0 when it is not a date.
        std::time_t TimeFromVariantDate(double date) {
            SYSTEMTIME local{}, utc{};
            if (!VariantTimeToSystemTime(date, &local)) return 0;
            if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) utc = local;
            FILETIME ft{};
            if (!SystemTimeToFileTime(&utc, &ft)) return 0;
            ULARGE_INTEGER t;
            t.LowPart = ft.dwLowDateTime;
            t.HighPart = ft.dwHighDateTime;
            // 100 ns ticks since 1601 -> seconds since 1970.
            const unsigned long long seconds = t.QuadPart / 10000000ULL;
            const unsigned long long epoch = 11644473600ULL;
            return seconds > epoch ? static_cast<std::time_t>(seconds - epoch) : 0;
        }

        bool SameName(const std::wstring& a, const std::wstring& b, bool ignoreCase) {
            if (!ignoreCase) return a == b;
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (std::towlower(a[i]) != std::towlower(b[i])) return false;
            return true;
        }

        // What one listing learned about one child.
        struct Child {
            std::wstring id;
            std::wstring name;
            bool isDirectory = false;
            uint64_t size = 0;
            std::time_t modified = 0;
        };

        // ===== THE COM THREAD =====
        // Runs the backend's work items one after another in the MTA. Never
        // ends: the backend lives until the process does (see Backend() in
        // the core file), and Windows tears the thread down with it.
        class ComThread {
        public:
            ComThread() : worker([this]() { Run(); }) { worker.detach(); }

            template <class R>
            R Call(std::function<R()> work) {
                auto task = std::make_shared<std::packaged_task<R()>>(std::move(work));
                std::future<R> result = task->get_future();
                {
                    std::lock_guard<std::mutex> lk(mutex);
                    queue.push_back([task]() { (*task)(); });
                }
                wake.notify_one();
                return result.get();
            }

        private:
            void Run() {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                for (;;) {
                    std::function<void()> item;
                    {
                        std::unique_lock<std::mutex> lk(mutex);
                        wake.wait(lk, [this]() { return !queue.empty(); });
                        item = std::move(queue.front());
                        queue.pop_front();
                    }
                    item();
                }
            }

            std::mutex mutex;
            std::condition_variable wake;
            std::deque<std::function<void()>> queue;
            std::thread worker;
        };

        // ===== THE BACKEND =====
        class WpdBackend final : public IPortableDeviceBackend {
        public:
            std::vector<PortableDeviceInfo> List() override {
                return com.Call<std::vector<PortableDeviceInfo>>([this]() { return DoList(); });
            }

            bool ListFolder(const std::string& deviceId, const std::string& path,
                            std::vector<PortableDeviceEntry>& out, std::string& error) override {
                return com.Call<bool>([&]() { return DoListFolder(deviceId, path, out, error); });
            }

            bool Download(const std::string& deviceId, const std::string& path,
                          const std::string& localFile, const PortableDeviceProgress& progress,
                          std::string& error) override {
                return com.Call<bool>([&]() {
                    return DoDownload(deviceId, path, localFile, progress, error);
                });
            }

            bool Upload(const std::string& deviceId, const std::string& localFile,
                        const std::string& path, const PortableDeviceProgress& progress,
                        std::string& error) override {
                return com.Call<bool>([&]() {
                    return DoUpload(deviceId, localFile, path, progress, error);
                });
            }

            bool MakeFolder(const std::string& deviceId, const std::string& path,
                            std::string& error) override {
                return com.Call<bool>([&]() { return DoMakeFolder(deviceId, path, error); });
            }

            bool Delete(const std::string& deviceId, const std::string& path,
                        std::string& error) override {
                return com.Call<bool>([&]() { return DoDelete(deviceId, path, error); });
            }

            bool Rename(const std::string& deviceId, const std::string& path,
                        const std::string& newName, std::string& error) override {
                return com.Call<bool>([&]() { return DoRename(deviceId, path, newName, error); });
            }

            bool Space(const std::string& deviceId, uint64_t& totalBytes,
                       uint64_t& freeBytes, std::string& error) override {
                return com.Call<bool>([&]() {
                    return DoSpace(deviceId, totalBytes, freeBytes, error);
                });
            }

        private:
            // One opened device, COM thread only.
            struct Device {
                Com<IPortableDevice> device;
                Com<IPortableDeviceContent> content;
                Com<IPortableDeviceProperties> properties;
                // Path ("/Internal shared storage/DCIM") -> object id.
                std::map<std::string, std::wstring> ids;
            };

            // ---- Devices -----------------------------------------------------

            IPortableDeviceManager* Manager() {
                if (!manager)
                    CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IPortableDeviceManager, manager.OutVoid());
                return manager.Get();
            }

            // A string the manager reports through the "ask for the size,
            // then ask again" pattern.
            template <class Getter>
            static std::string ManagerString(Getter get) {
                DWORD size = 0;
                if (FAILED(get(nullptr, &size)) || size == 0) return {};
                std::wstring text(size, L'\0');
                if (FAILED(get(&text[0], &size))) return {};
                text.resize(wcsnlen(text.c_str(), text.size()));
                return WideToUtf8(text);
            }

            std::vector<PortableDeviceInfo> DoList() {
                std::vector<PortableDeviceInfo> out;
                IPortableDeviceManager* m = Manager();
                if (!m) return out;
                // Without this the manager answers from the list it took when
                // it was made, and a phone plugged in since is never seen.
                m->RefreshDeviceList();
                DWORD count = 0;
                if (FAILED(m->GetDevices(nullptr, &count)) || count == 0) {
                    ForgetDevicesNotIn({});
                    return out;
                }
                std::vector<LPWSTR> ids(count, nullptr);
                if (FAILED(m->GetDevices(ids.data(), &count))) count = 0;
                std::vector<std::wstring> present;
                for (DWORD i = 0; i < count; ++i) {
                    if (!ids[i]) continue;
                    const std::wstring wid = ids[i];
                    CoTaskMemFree(ids[i]);
                    present.push_back(wid);

                    // A USB stick or an external disk is a WPD device too
                    // (the "WPD file system" driver), but it is mounted as
                    // a drive with a letter, which the volume list already
                    // shows and which reads far faster - listing it here
                    // as well made it appear twice in a file manager.
                    if (IsMassStorage(wid)) {
                        open.erase(wid);
                        continue;
                    }

                    PortableDeviceInfo info;
                    info.id = WideToUtf8(wid);
                    info.name = ManagerString([&](WCHAR* b, DWORD* n) {
                        return m->GetDeviceFriendlyName(wid.c_str(), b, n);
                    });
                    info.manufacturer = ManagerString([&](WCHAR* b, DWORD* n) {
                        return m->GetDeviceManufacturer(wid.c_str(), b, n);
                    });
                    info.description = ManagerString([&](WCHAR* b, DWORD* n) {
                        return m->GetDeviceDescription(wid.c_str(), b, n);
                    });
                    if (info.name.empty()) info.name = info.description;
                    if (info.name.empty()) info.name = "Portable device";
                    info.kind = KindOf(wid);
                    out.push_back(std::move(info));
                }
                ForgetDevicesNotIn(present);
                return out;
            }

            // Pulled out: its handle is dead, and its ids mean nothing on
            // the next device that gets this port.
            void ForgetDevicesNotIn(const std::vector<std::wstring>& present) {
                for (auto it = open.begin(); it != open.end();) {
                    if (std::find(present.begin(), present.end(), it->first) == present.end())
                        it = open.erase(it);
                    else
                        ++it;
                }
                for (auto it = kinds.begin(); it != kinds.end();) {
                    if (std::find(present.begin(), present.end(), it->first) == present.end())
                        it = kinds.erase(it);
                    else
                        ++it;
                }
                for (auto it = massStorage.begin(); it != massStorage.end();) {
                    if (std::find(present.begin(), present.end(), it->first) == present.end())
                        it = massStorage.erase(it);
                    else
                        ++it;
                }
            }

            // A mass-storage drive (USB Mass Storage Class) rather than an
            // MTP / PTP device. The id says so for most of them - a volume
            // the WPD bus enumerator made from USBSTOR or STORAGE#Volume -
            // and the device's protocol ("MSC:...") says so for the rest.
            // Asked once per connection and remembered.
            bool IsMassStorage(const std::wstring& id) {
                auto known = massStorage.find(id);
                if (known != massStorage.end()) return known->second;
                std::wstring lower = id;
                for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
                bool msc = lower.find(L"usbstor") != std::wstring::npos ||
                           lower.find(L"storage#volume") != std::wstring::npos;
                if (!msc) {
                    std::string ignored;
                    if (Device* d = Open(id, ignored)) {
                        Com<IPortableDeviceKeyCollection> keys;
                        Com<IPortableDeviceValues> values;
                        if (SUCCEEDED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr,
                                                       CLSCTX_INPROC_SERVER,
                                                       IID_IPortableDeviceKeyCollection,
                                                       keys.OutVoid())) &&
                            SUCCEEDED(keys->Add(WPD_DEVICE_PROTOCOL)) &&
                            SUCCEEDED(d->properties->GetValues(WPD_DEVICE_OBJECT_ID, keys.Get(),
                                                               values.Out()))) {
                            LPWSTR protocol = nullptr;
                            if (SUCCEEDED(values->GetStringValue(WPD_DEVICE_PROTOCOL, &protocol)) &&
                                protocol) {
                                const std::wstring p(protocol);
                                msc = SameName(p.substr(0, 3), L"MSC", true);
                                CoTaskMemFree(protocol);
                            }
                        }
                    }
                }
                massStorage[id] = msc;
                return msc;
            }

            // What the device says it is. Asked once per connection - opening
            // a device for it is a round trip - and remembered.
            PortableDeviceKind KindOf(const std::wstring& id) {
                auto known = kinds.find(id);
                if (known != kinds.end()) return known->second;
                PortableDeviceKind kind = PortableDeviceKind::Other;
                std::string ignored;
                if (Device* d = Open(id, ignored)) {
                    Com<IPortableDeviceKeyCollection> keys;
                    Com<IPortableDeviceValues> values;
                    if (SUCCEEDED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr,
                                                   CLSCTX_INPROC_SERVER,
                                                   IID_IPortableDeviceKeyCollection,
                                                   keys.OutVoid())) &&
                        SUCCEEDED(keys->Add(WPD_DEVICE_TYPE)) &&
                        SUCCEEDED(d->properties->GetValues(WPD_DEVICE_OBJECT_ID, keys.Get(),
                                                           values.Out()))) {
                        ULONG type = 0;
                        if (SUCCEEDED(values->GetUnsignedIntegerValue(WPD_DEVICE_TYPE, &type))) {
                            switch (type) {
                                case WPD_DEVICE_TYPE_PHONE:        kind = PortableDeviceKind::Phone; break;
                                case WPD_DEVICE_TYPE_CAMERA:
                                case WPD_DEVICE_TYPE_VIDEO:        kind = PortableDeviceKind::Camera; break;
                                case WPD_DEVICE_TYPE_MEDIA_PLAYER: kind = PortableDeviceKind::MediaPlayer; break;
                                default: break;
                            }
                        }
                    }
                }
                kinds[id] = kind;
                return kind;
            }

            Device* Open(const std::wstring& id, std::string& error) {
                auto found = open.find(id);
                if (found != open.end()) return &found->second;

                Com<IPortableDeviceValues> client;
                HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr,
                                              CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues,
                                              client.OutVoid());
                if (FAILED(hr)) { error = "cannot reach Windows Portable Devices: " + Describe(hr); return nullptr; }
                client->SetStringValue(WPD_CLIENT_NAME, L"UltraCanvas");
                client->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
                client->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
                client->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
                client->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE,
                                                SECURITY_IMPERSONATION);
                client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS,
                                                GENERIC_READ | GENERIC_WRITE);

                Device d;
                // The free-threaded flavour: the device is used from this
                // thread only, but nothing then ties it to an apartment.
                hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IPortableDevice, d.device.OutVoid());
                if (FAILED(hr)) { error = "cannot reach Windows Portable Devices: " + Describe(hr); return nullptr; }
                hr = d.device->Open(id.c_str(), client.Get());
                if (hr == E_ACCESSDENIED) {
                    // A device that only lets us read is still worth browsing.
                    client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ);
                    hr = d.device->Open(id.c_str(), client.Get());
                }
                if (FAILED(hr)) { error = "the device could not be opened: " + Describe(hr); return nullptr; }
                hr = d.device->Content(d.content.Out());
                if (SUCCEEDED(hr)) hr = d.content->Properties(d.properties.Out());
                if (FAILED(hr)) { error = "the device could not be read: " + Describe(hr); return nullptr; }
                d.ids["/"] = WPD_DEVICE_OBJECT_ID;
                return &(open[id] = std::move(d));
            }

            // ---- Objects -----------------------------------------------------

            // The facts one listing needs about one object.
            bool ReadChild(Device& d, const std::wstring& objectId, bool storagesOnly, Child& out) {
                Com<IPortableDeviceKeyCollection> keys;
                if (FAILED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr,
                                            CLSCTX_INPROC_SERVER, IID_IPortableDeviceKeyCollection,
                                            keys.OutVoid())))
                    return false;
                keys->Add(WPD_OBJECT_NAME);
                keys->Add(WPD_OBJECT_ORIGINAL_FILE_NAME);
                keys->Add(WPD_OBJECT_CONTENT_TYPE);
                keys->Add(WPD_OBJECT_SIZE);
                keys->Add(WPD_OBJECT_DATE_MODIFIED);
                keys->Add(WPD_FUNCTIONAL_OBJECT_CATEGORY);
                Com<IPortableDeviceValues> values;
                // S_FALSE - some keys missing - is normal: a folder has no size.
                if (FAILED(d.properties->GetValues(objectId.c_str(), keys.Get(), values.Out())))
                    return false;

                GUID type = WPD_CONTENT_TYPE_UNSPECIFIED;
                values->GetGuidValue(WPD_OBJECT_CONTENT_TYPE, &type);
                if (type == WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT) {
                    // The device's children are functional objects: the
                    // storages, and on a phone also its contacts, its SMS -
                    // services that are not folders of files.
                    GUID category = GUID_NULL;
                    values->GetGuidValue(WPD_FUNCTIONAL_OBJECT_CATEGORY, &category);
                    if (category != WPD_FUNCTIONAL_CATEGORY_STORAGE) return false;
                } else if (storagesOnly) {
                    return false;
                }

                // The file name with its extension where the device has one;
                // the display name (a storage's "Internal shared storage")
                // where not.
                TaskString original, name;
                values->GetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &original.p);
                values->GetStringValue(WPD_OBJECT_NAME, &name.p);
                out.name = original.Text();
                if (out.name.empty()) out.name = name.Text();
                if (out.name.empty()) out.name = objectId;

                out.id = objectId;
                out.isDirectory = type == WPD_CONTENT_TYPE_FOLDER ||
                                  type == WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT;
                ULONGLONG size = 0;
                if (!out.isDirectory && SUCCEEDED(values->GetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, &size)))
                    out.size = size;
                PROPVARIANT when;
                PropVariantInit(&when);
                if (SUCCEEDED(values->GetValue(WPD_OBJECT_DATE_MODIFIED, &when)) && when.vt == VT_DATE)
                    out.modified = TimeFromVariantDate(when.date);
                PropVariantClear(&when);
                return true;
            }

            // The children of `parentId`, read from the device; `parentPath`
            // records their ids for the next resolve.
            bool Children(Device& d, const std::wstring& parentId, const std::string& parentPath,
                          std::vector<Child>& out, HRESULT& hr) {
                out.clear();
                Com<IEnumPortableDeviceObjectIDs> items;
                hr = d.content->EnumObjects(0, parentId.c_str(), nullptr, items.Out());
                if (FAILED(hr)) return false;
                const bool storagesOnly = parentPath == "/";
                // A listing replaces what an earlier one recorded under this
                // folder - names that are gone must not resolve any more.
                const std::string prefix = parentPath == "/" ? "/" : parentPath + "/";
                for (auto it = d.ids.lower_bound(prefix); it != d.ids.end() &&
                         it->first.compare(0, prefix.size(), prefix) == 0;) {
                    // Direct children only - not the folder itself ("/" is
                    // its own prefix) and not what lies deeper.
                    if (it->first.size() > prefix.size() &&
                        it->first.find('/', prefix.size()) == std::string::npos)
                        it = d.ids.erase(it);
                    else
                        ++it;
                }
                for (;;) {
                    LPWSTR batch[64] = {};
                    ULONG fetched = 0;
                    hr = items->Next(64, batch, &fetched);
                    for (ULONG i = 0; i < fetched; ++i) {
                        Child c;
                        const std::wstring objectId = batch[i];
                        CoTaskMemFree(batch[i]);
                        if (!ReadChild(d, objectId, storagesOnly, c)) continue;
                        d.ids[prefix + WideToUtf8(c.name)] = c.id;
                        out.push_back(std::move(c));
                    }
                    if (hr != S_OK || fetched == 0) break;
                }
                hr = S_OK;
                return true;
            }

            // The object at `path`, resolved from the cache or by listing the
            // folders on the way down.
            bool Resolve(Device& d, const std::string& path, std::wstring& id, std::string& error) {
                auto cached = d.ids.find(path);
                if (cached != d.ids.end()) { id = cached->second; return true; }
                std::wstring current = WPD_DEVICE_OBJECT_ID;
                std::string at = "/";
                for (const std::string& name : SplitPortableDevicePath(path)) {
                    const std::string next = at == "/" ? "/" + name : at + "/" + name;
                    auto known = d.ids.find(next);
                    if (known == d.ids.end()) {
                        std::vector<Child> children;
                        HRESULT hr = S_OK;
                        if (!Children(d, current, at, children, hr)) {
                            error = "cannot read " + at + " on the device: " + Describe(hr);
                            return false;
                        }
                        known = d.ids.find(next);
                        if (known == d.ids.end()) {
                            // Not as spelled: a device's file system can be
                            // case-insensitive, and a typed path may differ.
                            const std::wstring wanted = Utf8ToWide(name);
                            for (const Child& c : children) {
                                if (SameName(c.name, wanted, true)) {
                                    known = d.ids.emplace(next, c.id).first;
                                    break;
                                }
                            }
                        }
                        if (known == d.ids.end()) {
                            error = "\"" + name + "\" is not on the device (in " + at + ")";
                            return false;
                        }
                    }
                    current = known->second;
                    at = next;
                }
                id = current;
                d.ids[path] = id;
                return true;
            }

            // Runs `work` on the object at `path` of the device. Opens the
            // device as needed; when the work fails on an id the cache gave
            // it, resolves the path afresh and tries once more, and when the
            // device has gone away, drops the handle so the next call reopens.
            bool WithObject(const std::string& deviceId, const std::string& path, std::string& error,
                            const std::function<HRESULT(Device&, const std::wstring&)>& work,
                            const char* doing) {
                const std::wstring wid = Utf8ToWide(deviceId);
                for (int attempt = 0; attempt < 2; ++attempt) {
                    Device* d = Open(wid, error);
                    if (!d) return false;
                    const bool fromCache = d->ids.count(path) != 0;
                    std::wstring id;
                    if (!Resolve(*d, path, id, error)) return false;
                    const HRESULT hr = work(*d, id);
                    if (SUCCEEDED(hr)) return true;
                    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { error = "cancelled"; return false; }
                    if (DeviceGone(hr)) {
                        open.erase(wid);
                        if (attempt == 0) continue;   // reopened once
                    } else if (fromCache && attempt == 0) {
                        d->ids.clear();
                        d->ids["/"] = WPD_DEVICE_OBJECT_ID;
                        continue;
                    }
                    if (error.empty()) error = std::string("cannot ") + doing + ": " + Describe(hr);
                    return false;
                }
                return false;
            }

            static std::string ParentOf(const std::string& path) {
                const std::string::size_type slash = path.rfind('/');
                return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
            }
            static std::string NameOf(const std::string& path) {
                const std::string::size_type slash = path.rfind('/');
                return slash == std::string::npos ? path : path.substr(slash + 1);
            }

            // Forgets `path` and everything recorded below it.
            static void ForgetTree(Device& d, const std::string& path) {
                d.ids.erase(path);
                const std::string prefix = path + "/";
                for (auto it = d.ids.lower_bound(prefix);
                     it != d.ids.end() && it->first.compare(0, prefix.size(), prefix) == 0;)
                    it = d.ids.erase(it);
            }

            // ---- The verbs ---------------------------------------------------

            bool DoListFolder(const std::string& deviceId, const std::string& path,
                              std::vector<PortableDeviceEntry>& out, std::string& error) {
                std::vector<Child> children;
                const bool ok = WithObject(deviceId, path, error,
                        [&](Device& d, const std::wstring& id) {
                            HRESULT hr = S_OK;
                            Children(d, id, path, children, hr);
                            return hr;
                        }, "list the folder");
                if (!ok) return false;
                if (path == "/" && children.empty()) {
                    // What a locked phone, or one left on "charging only"
                    // after it was plugged in, looks like: the device is there
                    // and offers nothing. Saying so beats an empty folder.
                    error = "The device shows no storage. Unlock it and allow access to its "
                            "files (on Android: the USB notification, \"File transfer\").";
                    return false;
                }
                for (const Child& c : children) {
                    PortableDeviceEntry e;
                    e.name = WideToUtf8(c.name);
                    e.isDirectory = c.isDirectory;
                    e.size = c.size;
                    e.modifiedTime = c.modified;
                    out.push_back(std::move(e));
                }
                return true;
            }

            bool DoDownload(const std::string& deviceId, const std::string& path,
                            const std::string& localFile, const PortableDeviceProgress& progress,
                            std::string& error) {
                const std::wstring target = Utf8ToWide(localFile);
                bool cancelled = false;
                const bool ok = WithObject(deviceId, path, error,
                        [&](Device& d, const std::wstring& id) -> HRESULT {
                            // The size, for the progress bar; a device that
                            // does not say gets a busy bar instead.
                            Child facts;
                            ReadChild(d, id, false, facts);
                            if (facts.isDirectory) return HRESULT_FROM_WIN32(ERROR_DIRECTORY);

                            Com<IPortableDeviceResources> resources;
                            HRESULT hr = d.content->Transfer(resources.Out());
                            if (FAILED(hr)) return hr;
                            DWORD chunk = 0;
                            Com<IStream> stream;
                            hr = resources->GetStream(id.c_str(), WPD_RESOURCE_DEFAULT, STGM_READ,
                                                      &chunk, stream.Out());
                            if (FAILED(hr)) return hr;
                            if (chunk < 64 * 1024) chunk = 256 * 1024;

                            FILE* file = _wfopen(target.c_str(), L"wb");
                            if (!file) {
                                error = "cannot write " + localFile;
                                return E_FAIL;
                            }
                            std::vector<char> buffer(chunk);
                            uint64_t done = 0;
                            for (;;) {
                                ULONG got = 0;
                                hr = stream->Read(buffer.data(), chunk, &got);
                                if (FAILED(hr)) break;
                                if (got == 0) { hr = S_OK; break; }
                                if (std::fwrite(buffer.data(), 1, got, file) != got) {
                                    error = "cannot write " + localFile + " (is the disk full?)";
                                    hr = E_FAIL;
                                    break;
                                }
                                done += got;
                                if (progress && !progress(done, facts.size)) {
                                    cancelled = true;
                                    hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                                    break;
                                }
                                if (hr == S_FALSE) { hr = S_OK; break; }   // end of stream
                            }
                            std::fclose(file);
                            if (FAILED(hr)) _wremove(target.c_str());
                            return hr;
                        }, "copy the file off the device");
                if (!ok && cancelled) error = "cancelled";
                return ok;
            }

            // The id of the child `name` of the folder `parentPath`, listing
            // the folder afresh so a file put there on the phone is seen.
            bool ExistingChild(Device& d, const std::wstring& parentId, const std::string& parentPath,
                               const std::wstring& name, std::wstring& childId) {
                std::vector<Child> children;
                HRESULT hr = S_OK;
                if (!Children(d, parentId, parentPath, children, hr)) return false;
                for (const Child& c : children)
                    if (SameName(c.name, name, false)) { childId = c.id; return true; }
                return false;
            }

            HRESULT DeleteById(Device& d, const std::wstring& id) {
                Com<IPortableDevicePropVariantCollection> list;
                HRESULT hr = CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr,
                                              CLSCTX_INPROC_SERVER,
                                              IID_IPortableDevicePropVariantCollection,
                                              list.OutVoid());
                if (FAILED(hr)) return hr;
                PROPVARIANT item;
                PropVariantInit(&item);
                item.vt = VT_LPWSTR;
                item.pwszVal = const_cast<LPWSTR>(id.c_str());
                hr = list->Add(&item);   // copies; item is not cleared - it owns nothing
                if (FAILED(hr)) return hr;
                return d.content->Delete(PORTABLE_DEVICE_DELETE_WITH_RECURSION, list.Get(), nullptr);
            }

            bool DoUpload(const std::string& deviceId, const std::string& localFile,
                          const std::string& path, const PortableDeviceProgress& progress,
                          std::string& error) {
                const std::wstring source = Utf8ToWide(localFile);
                FILE* file = _wfopen(source.c_str(), L"rb");
                if (!file) { error = "cannot read " + localFile; return false; }
                _fseeki64(file, 0, SEEK_END);
                const long long length = _ftelli64(file);
                _fseeki64(file, 0, SEEK_SET);
                if (length < 0) { std::fclose(file); error = "cannot read " + localFile; return false; }

                const std::string parentPath = ParentOf(path);
                const std::wstring name = Utf8ToWide(NameOf(path));
                bool cancelled = false;
                const bool ok = WithObject(deviceId, parentPath, error,
                        [&](Device& d, const std::wstring& parentId) -> HRESULT {
                            // MTP keeps two files of one name side by side
                            // rather than replacing; the caller asked about the
                            // name already, so the old one goes first.
                            std::wstring existing;
                            if (ExistingChild(d, parentId, parentPath, name, existing)) {
                                const HRESULT hr = DeleteById(d, existing);
                                if (FAILED(hr)) return hr;
                            }

                            Com<IPortableDeviceValues> values;
                            HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr,
                                                          CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues,
                                                          values.OutVoid());
                            if (FAILED(hr)) return hr;
                            values->SetStringValue(WPD_OBJECT_PARENT_ID, parentId.c_str());
                            values->SetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE,
                                                                 static_cast<ULONGLONG>(length));
                            values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str());
                            values->SetStringValue(WPD_OBJECT_NAME, name.c_str());
                            values->SetGuidValue(WPD_OBJECT_CONTENT_TYPE, WPD_CONTENT_TYPE_GENERIC_FILE);
                            values->SetGuidValue(WPD_OBJECT_FORMAT, WPD_OBJECT_FORMAT_UNSPECIFIED);

                            Com<IStream> stream;
                            DWORD chunk = 0;
                            hr = d.content->CreateObjectWithPropertiesAndData(values.Get(), stream.Out(),
                                                                             &chunk, nullptr);
                            if (FAILED(hr)) return hr;
                            if (chunk < 64 * 1024) chunk = 256 * 1024;
                            std::vector<char> buffer(chunk);
                            uint64_t done = 0;
                            _fseeki64(file, 0, SEEK_SET);
                            for (;;) {
                                const size_t got = std::fread(buffer.data(), 1, chunk, file);
                                if (got == 0) break;
                                ULONG written = 0;
                                hr = stream->Write(buffer.data(), static_cast<ULONG>(got), &written);
                                if (FAILED(hr)) { stream->Revert(); return hr; }
                                done += got;
                                if (progress && !progress(done, static_cast<uint64_t>(length))) {
                                    cancelled = true;
                                    stream->Revert();
                                    return HRESULT_FROM_WIN32(ERROR_CANCELLED);
                                }
                            }
                            hr = stream->Commit(STGC_DEFAULT);
                            ForgetTree(d, path);
                            return hr;
                        }, "copy the file onto the device");
                std::fclose(file);
                if (!ok && cancelled) error = "cancelled";
                return ok;
            }

            bool DoMakeFolder(const std::string& deviceId, const std::string& path,
                              std::string& error) {
                const std::string parentPath = ParentOf(path);
                const std::wstring name = Utf8ToWide(NameOf(path));
                return WithObject(deviceId, parentPath, error,
                        [&](Device& d, const std::wstring& parentId) -> HRESULT {
                            Com<IPortableDeviceValues> values;
                            HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr,
                                                          CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues,
                                                          values.OutVoid());
                            if (FAILED(hr)) return hr;
                            values->SetStringValue(WPD_OBJECT_PARENT_ID, parentId.c_str());
                            values->SetStringValue(WPD_OBJECT_NAME, name.c_str());
                            values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str());
                            values->SetGuidValue(WPD_OBJECT_CONTENT_TYPE, WPD_CONTENT_TYPE_FOLDER);
                            values->SetGuidValue(WPD_OBJECT_FORMAT, WPD_OBJECT_FORMAT_PROPERTIES_ONLY);
                            TaskString created;
                            hr = d.content->CreateObjectWithPropertiesOnly(values.Get(), &created.p);
                            if (SUCCEEDED(hr) && created.p) d.ids[path] = created.Text();
                            return hr;
                        }, "create the folder");
            }

            bool DoDelete(const std::string& deviceId, const std::string& path, std::string& error) {
                return WithObject(deviceId, path, error,
                        [&](Device& d, const std::wstring& id) -> HRESULT {
                            const HRESULT hr = DeleteById(d, id);
                            if (SUCCEEDED(hr)) ForgetTree(d, path);
                            return hr;
                        }, "delete it");
            }

            bool DoRename(const std::string& deviceId, const std::string& path,
                          const std::string& newName, std::string& error) {
                const std::wstring name = Utf8ToWide(newName);
                return WithObject(deviceId, path, error,
                        [&](Device& d, const std::wstring& id) -> HRESULT {
                            Com<IPortableDeviceValues> values;
                            HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr,
                                                          CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues,
                                                          values.OutVoid());
                            if (FAILED(hr)) return hr;
                            // The file name is what an MTP device renames
                            // (ObjectFileName); some devices list the display
                            // name too, which may be read-only - its failure
                            // alone is not one.
                            values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str());
                            Com<IPortableDeviceValues> results;
                            hr = d.properties->SetValues(id.c_str(), values.Get(), results.Out());
                            if (FAILED(hr)) return hr;
                            if (results) {
                                HRESULT one = S_OK;
                                if (SUCCEEDED(results->GetErrorValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &one)) &&
                                    FAILED(one))
                                    return one;
                            }
                            Com<IPortableDeviceValues> display;
                            if (SUCCEEDED(CoCreateInstance(CLSID_PortableDeviceValues, nullptr,
                                                           CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues,
                                                           display.OutVoid()))) {
                                display->SetStringValue(WPD_OBJECT_NAME, name.c_str());
                                Com<IPortableDeviceValues> ignored;
                                d.properties->SetValues(id.c_str(), display.Get(), ignored.Out());
                            }
                            ForgetTree(d, path);
                            return S_OK;
                        }, "rename it");
            }

            // The storages' capacity and free space, added up. Read straight
            // off the storage objects - the device keeps the numbers current
            // and answers them without walking any folder.
            bool DoSpace(const std::string& deviceId, uint64_t& totalBytes,
                         uint64_t& freeBytes, std::string& error) {
                totalBytes = 0;
                freeBytes = 0;
                int storages = 0;
                const bool ok = WithObject(deviceId, "/", error,
                        [&](Device& d, const std::wstring& id) -> HRESULT {
                            totalBytes = 0;
                            freeBytes = 0;
                            storages = 0;
                            Com<IEnumPortableDeviceObjectIDs> items;
                            HRESULT hr = d.content->EnumObjects(0, id.c_str(), nullptr, items.Out());
                            if (FAILED(hr)) return hr;
                            Com<IPortableDeviceKeyCollection> keys;
                            hr = CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr,
                                                  CLSCTX_INPROC_SERVER, IID_IPortableDeviceKeyCollection,
                                                  keys.OutVoid());
                            if (FAILED(hr)) return hr;
                            keys->Add(WPD_OBJECT_CONTENT_TYPE);
                            keys->Add(WPD_FUNCTIONAL_OBJECT_CATEGORY);
                            keys->Add(WPD_STORAGE_CAPACITY);
                            keys->Add(WPD_STORAGE_FREE_SPACE_IN_BYTES);
                            for (;;) {
                                LPWSTR batch[16] = {};
                                ULONG fetched = 0;
                                hr = items->Next(16, batch, &fetched);
                                for (ULONG i = 0; i < fetched; ++i) {
                                    Com<IPortableDeviceValues> values;
                                    const HRESULT got = d.properties->GetValues(batch[i], keys.Get(),
                                                                               values.Out());
                                    CoTaskMemFree(batch[i]);
                                    if (FAILED(got)) continue;
                                    GUID category = GUID_NULL;
                                    values->GetGuidValue(WPD_FUNCTIONAL_OBJECT_CATEGORY, &category);
                                    if (category != WPD_FUNCTIONAL_CATEGORY_STORAGE) continue;
                                    ULONGLONG capacity = 0, free = 0;
                                    if (FAILED(values->GetUnsignedLargeIntegerValue(WPD_STORAGE_CAPACITY,
                                                                                    &capacity)))
                                        continue;
                                    values->GetUnsignedLargeIntegerValue(WPD_STORAGE_FREE_SPACE_IN_BYTES,
                                                                        &free);
                                    totalBytes += capacity;
                                    freeBytes += free < capacity ? free : capacity;
                                    ++storages;
                                }
                                if (hr != S_OK || fetched == 0) break;
                            }
                            return S_OK;
                        }, "read the device's free space");
                if (!ok) return false;
                if (storages == 0) {
                    error = "the device shows no storage";
                    return false;
                }
                return true;
            }

            ComThread com;
            Com<IPortableDeviceManager> manager;     // COM thread only
            std::map<std::wstring, Device> open;     // COM thread only
            std::map<std::wstring, PortableDeviceKind> kinds;   // COM thread only
            std::map<std::wstring, bool> massStorage;           // COM thread only
        };

    } // namespace

    std::unique_ptr<IPortableDeviceBackend> CreatePortableDeviceBackend() {
        return std::make_unique<WpdBackend>();
    }

} // namespace UltraCanvas

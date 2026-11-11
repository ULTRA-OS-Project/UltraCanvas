// Apps/UltraFiler/UltraFilerDeviceProvider.cpp
// See UltraFilerDeviceProvider.h. UltraCloud's provider calls mapped onto
// UltraCanvasPortableDevices: the account's serverUrl names the device, the
// provider path is the path on it.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraFilerDeviceProvider.h"

#include "UltraCanvasPortableDevices.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <vector>

#ifdef ULTRAFILER_HAS_ULTRACLOUD
#include <UltraCloud/UltraCloudProvider.h>
#include "UltraNet/UltraNetCore.h"   // the worker thread's abort check
#endif

namespace UltraCanvas {

std::string PortableDeviceAccountId(const std::string& deviceId) {
    // FNV-1a, 64 bits: stable across runs and platforms, which std::hash is
    // not, and a collision between two devices on one machine is not a
    // practical concern.
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : deviceId) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return std::string("device-") + text;
}

#ifndef ULTRAFILER_HAS_ULTRACLOUD

void RegisterPortableDeviceProvider() {}

#else

namespace {

using UltraCloud::Account;
using UltraCloud::Credentials;
using UltraCloud::Entry;
using UltraCloud::Result;
using UltraCloud::ResultCode;

// A device's time in the form UltraFiler's remote listing reads first
// (ParseRemoteFilerTime): MLSD's YYYYMMDDHHMMSS, UTC.
std::string FormatTime(std::time_t t) {
    if (t <= 0) return {};
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &t) != 0) return {};
#else
    if (!gmtime_r(&t, &utc)) return {};
#endif
    char text[20];
    std::snprintf(text, sizeof(text), "%04d%02d%02d%02d%02d%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return text;
}

Result Failed(const std::string& error) {
    if (error == "cancelled") return Result::Error(ResultCode::Cancelled, error);
    return Result::Error(ResultCode::Unknown, error);
}

// The worker thread's abort check, read here on that thread and asked from
// the device's thread as the transfer moves: the window closing, or a
// preview copy the selection moved on from, calls a copy off the phone the
// same way it calls an FTP download off.
PortableDeviceProgress AbortCheck() {
    std::function<bool()> check = UltraNet_SetThreadAbortCheck({});
    UltraNet_SetThreadAbortCheck(check);
    if (!check) return {};
    return [check](uint64_t, uint64_t) { return !check(); };
}

class PortableDeviceProvider final : public UltraCloud::ICloudProvider {
public:
    std::string Id() const override { return PortableDeviceProviderId(); }
    std::string DisplayName() const override { return "Phone or camera"; }

    UltraCloud::ProviderCapabilities Capabilities() const override {
        UltraCloud::ProviderCapabilities caps;
        caps.browse = true;
        caps.upload = true;
        caps.modify = true;
        // Nothing is left on the device by an upload called off part way:
        // the object is only made when the copy is committed.
        caps.partialUploadsRemain = false;
        return caps;
    }

    Result Verify(const Account& account, const Credentials&) override {
        for (const PortableDeviceInfo& d : UltraCanvasPortableDevices::List())
            if (d.id == account.serverUrl) return Result::Ok();
        return Result::Error(ResultCode::NotFound, "the device is not connected");
    }

    Result List(const Account& account, const Credentials&,
                const std::string& path, std::vector<Entry>& out) override {
        out.clear();
        std::vector<PortableDeviceEntry> entries;
        std::string error;
        if (!UltraCanvasPortableDevices::ListFolder(account.serverUrl, path, entries, error))
            return Failed(error);
        const std::string base = NormalizePortableDevicePath(path);
        for (const PortableDeviceEntry& e : entries) {
            Entry x;
            x.name = e.name;
            x.path = base == "/" ? "/" + e.name : base + "/" + e.name;
            x.isDirectory = e.isDirectory;
            x.size = static_cast<int64_t>(e.size);
            x.modified = FormatTime(e.modifiedTime);
            out.push_back(std::move(x));
        }
        return Result::Ok();
    }

    Result MakeDirectory(const Account& account, const Credentials&,
                         const std::string& path) override {
        std::string error;
        return UltraCanvasPortableDevices::MakeFolder(account.serverUrl, path, error)
               ? Result::Ok() : Failed(error);
    }

    Result Upload(const Account& account, const Credentials&,
                  const std::string& localPath, const std::string& remotePath) override {
        std::string error;
        return UltraCanvasPortableDevices::Upload(account.serverUrl, localPath, remotePath,
                                                  error, AbortCheck())
               ? Result::Ok() : Failed(error);
    }

    Result Download(const Account& account, const Credentials&,
                    const std::string& remotePath, const std::string& localPath) override {
        // A partial copy is never continued (DownloadContinues): MTP reads a
        // file from its start, so the whole file is fetched again.
        std::string error;
        return UltraCanvasPortableDevices::Download(account.serverUrl, remotePath, localPath,
                                                    error, AbortCheck())
               ? Result::Ok() : Failed(error);
    }

    Result Delete(const Account& account, const Credentials&,
                  const std::string& path, bool) override {
        std::string error;
        return UltraCanvasPortableDevices::Delete(account.serverUrl, path, error)
               ? Result::Ok() : Failed(error);
    }

    Result Rename(const Account& account, const Credentials&,
                  const std::string& path, const std::string& newName) override {
        std::string error;
        return UltraCanvasPortableDevices::Rename(account.serverUrl, path, newName, error)
               ? Result::Ok() : Failed(error);
    }

    Result CreateShareLink(const Account&, const Credentials&, const std::string&,
                           const UltraCloud::ShareLinkOptions&, UltraCloud::ShareLink&) override {
        return Result::Error(ResultCode::Unsupported, "a phone or camera cannot share a link");
    }
};

} // namespace

void RegisterPortableDeviceProvider() {
    static std::once_flag once;
    std::call_once(once, []() {
        UltraCloud::RegisterProvider(std::make_shared<PortableDeviceProvider>());
    });
}

#endif // ULTRAFILER_HAS_ULTRACLOUD

} // namespace UltraCanvas

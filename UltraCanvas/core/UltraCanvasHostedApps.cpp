// core/UltraCanvasHostedApps.cpp
// The registry of applications that run inside their host program.
// Version: 1.1.0 - IdForProgram
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasHostedApps.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace UltraCanvas {
namespace UltraCanvasHostedApps {

    namespace {
        constexpr const char* kPrefix = "hosted:";

        std::mutex& Mutex() {
            static std::mutex m;
            return m;
        }
        std::vector<HostedAppInfo>& Apps() {
            static std::vector<HostedAppInfo> apps;
            return apps;
        }
        std::string& Current() {
            static std::string id;
            return id;
        }
        // "/x/UltraFiler.exe", "UltraFiler" -> "ultrafiler".
        std::string ProgramKey(const std::string& program) {
            const size_t slash = program.find_last_of("/\\");
            std::string name = slash == std::string::npos ? program : program.substr(slash + 1);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
            return name;
        }
    }

    void Register(const HostedAppInfo& app) {
        if (app.id.empty()) return;
        std::lock_guard<std::mutex> lock(Mutex());
        auto& apps = Apps();
        auto it = std::find_if(apps.begin(), apps.end(), [&](const HostedAppInfo& a) { return a.id == app.id; });
        if (it != apps.end()) *it = app;
        else apps.push_back(app);
    }

    std::vector<HostedAppInfo> List() {
        std::lock_guard<std::mutex> lock(Mutex());
        return Apps();
    }

    bool Has(const std::string& id) {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto& apps = Apps();
        return std::any_of(apps.begin(), apps.end(), [&](const HostedAppInfo& a) { return a.id == id; });
    }

    bool Launch(const std::string& id, const std::vector<std::string>& files, std::string* error) {
        std::function<bool(const std::vector<std::string>&)> launch;
        std::string name;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            for (const auto& a : Apps()) {
                if (a.id == id) {
                    launch = a.launch;
                    name = a.name;
                    break;
                }
            }
        }
        if (!launch) {
            if (error) *error = "\"" + id + "\" is not an application of this program.";
            return false;
        }
        // Called on the UI thread; the lock is not held, so the application
        // may register or launch others from inside.
        const std::string outer = Current();
        Current() = id;
        bool ok = false;
        try {
            ok = launch(files);
        } catch (...) {
            ok = false;
        }
        Current() = outer;
        if (!ok && error) *error = "\"" + name + "\" did not start.";
        return ok;
    }

    std::string Launching() {
        return Current();
    }

    std::string PathFor(const std::string& id) {
        return std::string(kPrefix) + id;
    }

    std::string IdForProgram(const std::string& program) {
        const std::string key = ProgramKey(program);
        if (key.empty()) return std::string();
        std::lock_guard<std::mutex> lock(Mutex());
        for (const auto& a : Apps()) {
            for (const std::string& p : a.programs) {
                if (ProgramKey(p) == key) return a.id;
            }
        }
        return std::string();
    }

    std::string IdFromPath(const std::string& path) {
        const std::string prefix(kPrefix);
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) return std::string();
        return path.substr(prefix.size());
    }

} // namespace UltraCanvasHostedApps
} // namespace UltraCanvas

// Tests/UltraStore/helpers.h
// What the installer and engine tests share: a temporary store root, a zip
// made with python3's zipfile, a static HTTP server over a folder (python3
// -m http.server), both forked and reaped; SKIP where there is no python3
// or no fork (Windows), and where VirtualFS has no archive provider.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "test_framework.h"

#include "Installer.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ultrastore_test {

inline std::string TempRoot(const char* tag) {
    std::mt19937_64 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() / ("ultrastore-" + std::string(tag) + "-" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir.string();
}

inline bool HavePython() {
#if defined(_WIN32)
    return false;
#else
    static const bool have = std::system("command -v python3 > /dev/null 2>&1") == 0;
    return have;
#endif
}

inline void RequireUnpacking() {
    if (!HavePython()) SKIP("python3 (or fork) not available");
    if (!UltraStore::ArchiveInstaller::SupportsFormat("zip")) SKIP("VirtualFS has no zip provider here (libarchive)");
}

// Writes the files (name -> bytes) into a zip at zipPath. Names ending in
// '/' make folders; a name in `executables` gets the execute bit.
inline bool MakeZip(const std::string& zipPath, const std::vector<std::pair<std::string, std::string>>& files,
                    const std::vector<std::string>& executables = {}) {
#if defined(_WIN32)
    (void)zipPath; (void)files; (void)executables;
    return false;
#else
    const std::string dir = TempRoot("zipsrc");
    for (const auto& [name, bytes] : files) {
        const std::filesystem::path p = std::filesystem::path(dir) / name;
        std::filesystem::create_directories(name.back() == '/' ? p : p.parent_path());
        if (name.back() == '/') continue;
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    }
    std::string script = "import os, sys, zipfile, stat\n"
                         "src, dst = sys.argv[1], sys.argv[2]\n"
                         "exe = set(sys.argv[3:])\n"
                         "with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:\n"
                         "    for root, dirs, files in os.walk(src):\n"
                         "        for d in dirs:\n"
                         "            rel = os.path.relpath(os.path.join(root, d), src) + '/'\n"
                         "            z.writestr(zipfile.ZipInfo(rel), b'')\n"
                         "        for f in files:\n"
                         "            full = os.path.join(root, f)\n"
                         "            rel = os.path.relpath(full, src)\n"
                         "            info = zipfile.ZipInfo.from_file(full, rel)\n"
                         "            info.compress_type = zipfile.ZIP_DEFLATED\n"
                         "            mode = 0o755 if rel in exe else 0o644\n"
                         "            info.external_attr = (stat.S_IFREG | mode) << 16\n"
                         "            with open(full, 'rb') as fh:\n"
                         "                z.writestr(info, fh.read())\n";
    std::string cmd = "python3 -c \"" + script + "\" '" + dir + "' '" + zipPath + "'";
    for (const std::string& e : executables) cmd += " '" + e + "'";
    const int rc = std::system(cmd.c_str());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return rc == 0 && std::filesystem::exists(zipPath);
#endif
}

// python3 -m http.server over a folder, on a port of its own.
struct StaticServer {
    long pid = -1;
    int port = 0;
    std::string baseUrl;

    ~StaticServer() { Stop(); }

    bool Start(const std::string& folder, int wantedPort) {
#if defined(_WIN32)
        (void)folder; (void)wantedPort;
        return false;
#else
        if (!HavePython()) return false;
        port = wantedPort;
        std::fflush(stdout);   // or the child's freopen flushes a copy of the parent's buffered output
        std::fflush(stderr);
        const pid_t p = fork();
        if (p < 0) return false;
        if (p == 0) {
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
            const std::string portStr = std::to_string(port);
            execlp("python3", "python3", "-m", "http.server", portStr.c_str(), "--bind", "127.0.0.1",
                   "--directory", folder.c_str(), nullptr);
            _exit(127);
        }
        pid = p;
        baseUrl = "http://127.0.0.1:" + portStr(port);
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            UltraNetSocketOptions o;
            o.connectTimeoutMs = 200;
            const UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", port, o);
            if (h != UltraNetInvalidHandle) {
                UltraNet_SocketClose(h);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Stop();
        return false;
#endif
    }

    void Stop() {
#if !defined(_WIN32)
        if (pid > 0) {
            kill(static_cast<pid_t>(pid), SIGTERM);
            int status = 0;
            waitpid(static_cast<pid_t>(pid), &status, 0);
            pid = -1;
        }
#endif
    }

private:
    static std::string portStr(int p) { return std::to_string(p); }
};

inline std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

} // namespace ultrastore_test

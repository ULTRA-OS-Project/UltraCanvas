// OS/Linux/UltraCanvasLinuxDeviceProbes.cpp
// The procfs and sysfs device probes of UltraCanvasDesktopShell on Linux,
// shared by the X11 and the Wayland backends. See the header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxDeviceProbes.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace UltraCanvas {
namespace DesktopShellBackend {
namespace LinuxProbes {

    std::string ReadTextFile(const std::string& path) {
        std::ifstream in(PathFromUtf8(path));
        if (!in) return "";
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        return text;
    }

    std::vector<std::string> ListDirectory(const std::string& path) {
        std::vector<std::string> names;
        DIR* dir = ::opendir(path.c_str());
        if (!dir) return names;
        while (dirent* entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            names.push_back(name);
        }
        ::closedir(dir);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool StreamRunning(const std::string& pcmDir) {
        // pcm0c/sub0/status, pcm0c/sub1/status, ...
        for (const std::string& sub : ListDirectory(pcmDir)) {
            if (sub.rfind("sub", 0) != 0) continue;
            const std::string status = ReadTextFile(pcmDir + "/" + sub + "/status");
            if (status.find("state: RUNNING") != std::string::npos) return true;
        }
        return false;
    }

    void ProbeAudio(DesktopDeviceActivity& activity) {
        const std::string root = "/proc/asound";
        bool anyCard = false;
        for (const std::string& card : ListDirectory(root)) {
            if (card.rfind("card", 0) != 0) continue;
            anyCard = true;
            for (const std::string& pcm : ListDirectory(root + "/" + card)) {
                if (pcm.rfind("pcm", 0) != 0 || pcm.size() < 5) continue;
                const char kind = pcm.back();   // 'c' capture, 'p' playback
                if (kind != 'c' && kind != 'p') continue;
                if (!StreamRunning(root + "/" + card + "/" + pcm)) continue;
                if (kind == 'c') activity.microphoneInUse = true;
                else activity.speakerPlaying = true;
            }
        }
        if (!anyCard) {
            activity.warnings.push_back("No sound card is listed under /proc/asound, so the "
                                        "microphone and speaker state is unknown.");
        }
    }

    void ProbeCamera(DesktopDeviceActivity& activity) {
        // Any process of ours with a /dev/video* descriptor. Other users'
        // processes are not readable and cannot open our camera either.
        bool anyDevice = false;
        for (const std::string& dev : ListDirectory("/dev")) {
            if (dev.rfind("video", 0) == 0) { anyDevice = true; break; }
        }
        if (!anyDevice) return;
        const uid_t me = ::getuid();
        for (const std::string& pid : ListDirectory("/proc")) {
            if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
            const std::string fdDir = "/proc/" + pid + "/fd";
            struct stat st;
            if (::stat(fdDir.c_str(), &st) != 0 || st.st_uid != me) continue;
            DIR* dir = ::opendir(fdDir.c_str());
            if (!dir) continue;
            bool found = false;
            while (dirent* entry = ::readdir(dir)) {
                if (entry->d_name[0] == '.') continue;
                char target[256];
                const std::string link = fdDir + "/" + entry->d_name;
                const ssize_t n = ::readlink(link.c_str(), target, sizeof(target) - 1);
                if (n <= 0) continue;
                target[n] = '\0';
                if (std::strncmp(target, "/dev/video", 10) == 0) { found = true; break; }
            }
            ::closedir(dir);
            if (found) { activity.webcamInUse = true; return; }
        }
    }

    void ProbeBattery(DesktopDeviceActivity& activity) {
        const std::string root = "/sys/class/power_supply";
        for (const std::string& supply : ListDirectory(root)) {
            const std::string dir = root + "/" + supply;
            if (ReadTextFile(dir + "/type") != "Battery") continue;
            const std::string present = ReadTextFile(dir + "/present");
            if (present == "0") continue;
            activity.batteryPresent = true;
            const std::string capacity = ReadTextFile(dir + "/capacity");
            if (!capacity.empty()) {
                try { activity.batteryPercent = std::stoi(capacity); } catch (...) {}
            }
            const std::string status = ReadTextFile(dir + "/status");
            activity.batteryCharging = (status == "Charging" || status == "Full");
            return;   // the first battery is the one a panel shows
        }
    }


} // namespace LinuxProbes
} // namespace DesktopShellBackend
} // namespace UltraCanvas

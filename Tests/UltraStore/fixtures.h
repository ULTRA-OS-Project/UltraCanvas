// Tests/UltraStore/fixtures.h
// A catalog the tests agree on: a runtime, a library on the runtime, an app
// on both, an app for Linux only, and an x86_64-only macOS app for the
// Rosetta row; built with the model and serialized, so the fixture is what
// the admin side would write.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "PlatformInfo.h"

#include <string>

namespace ultrastore_test {

inline std::string Hash(char fill) { return std::string(64, fill); }

inline UltraStore::Package Pkg(UltraStore::OS os, UltraStore::Arch arch, const std::string& format,
                               const std::string& name, uint64_t size, char hash,
                               std::vector<std::string> variants = {}, const std::string& entry = {},
                               const std::string& minOs = {}) {
    UltraStore::Package p;
    p.minOs = minOs;
    p.target.os = os;
    p.target.arch = arch;
    p.target.variants = std::move(variants);
    p.format = format;
    p.url = "https://packages.ultraos.example/" + name;
    p.size = size;
    p.sha256 = Hash(hash);
    p.entry = entry;
    return p;
}

inline UltraStore::Catalog FixtureCatalog() {
    using namespace UltraStore;
    Catalog c;
    c.schema = 1;
    c.sequence = 42;
    c.generated = "2026-10-24T12:00:00Z";
    c.publishers.push_back({"ultra-os-project", "ULTRA OS Project", "https://ultraos.eu"});

    Component runtime;
    runtime.kind = ComponentKind::Runtime;
    runtime.id = "org.ultraos.runtime";
    runtime.name = "UltraCanvas runtime";
    runtime.summary.byLanguage["en"] = "The shared libraries every app runs on";
    for (const char* v : {"0.9.400", "0.9.410"}) {
        Release r;
        r.version = v;
        r.channel = "stable";
        r.date = "2026-10-20";
        r.packages = {
            Pkg(OS::Linux, Arch::X86_64, "7z", std::string("runtime-") + v + "-linux-x86_64.7z", 110000000, 'a', {"glibc-2.31"}),
            Pkg(OS::Linux, Arch::Arm64, "7z", std::string("runtime-") + v + "-linux-arm64.7z", 105000000, 'b', {"glibc-2.31"}),
            Pkg(OS::Windows, Arch::X86_64, "zip", std::string("runtime-") + v + "-windows-x86_64.zip", 90000000, 'c'),
            Pkg(OS::MacOS, Arch::Arm64, "zip", std::string("runtime-") + v + "-macos-arm64.zip", 95000000, 'd', {}, "", "14.0"),
            Pkg(OS::MacOS, Arch::X86_64, "zip", std::string("runtime-") + v + "-macos-x86_64.zip", 97000000, 'e', {}, "", "14.0"),
        };
        runtime.releases.push_back(r);
    }
    c.components.push_back(runtime);

    Component videofx;
    videofx.kind = ComponentKind::Library;
    videofx.id = "org.ultraos.lib.videofx";
    videofx.name = "VideoFX";
    {
        Release r;
        r.version = "0.12.0";
        r.channel = "stable";
        r.depends = {{"org.ultraos.runtime", "0.9.410"}};
        r.packages = {
            Pkg(OS::Linux, Arch::X86_64, "7z", "videofx-0.12.0-linux-x86_64.7z", 8000000, 'f'),
            Pkg(OS::Windows, Arch::X86_64, "zip", "videofx-0.12.0-windows-x86_64.zip", 7000000, 'g'),
            Pkg(OS::MacOS, Arch::Universal, "zip", "videofx-0.12.0-macos-universal.zip", 15000000, 'h'),
        };
        videofx.releases.push_back(r);
    }
    c.components.push_back(videofx);

    Component video;
    video.kind = ComponentKind::App;
    video.id = "org.ultraos.ultravideo";
    video.publisher = "ultra-os-project";
    video.name = "UltraVideo";
    video.summary.byLanguage["en"] = "The video editor";
    video.summary.byLanguage["de"] = "Der Videoeditor";
    video.categories = {"video"};
    video.tags = {"editor", "nle"};
    video.pricing.model = PricingModel::OneTime;
    video.pricing.price = "29.90";
    video.pricing.currency = "EUR";
    video.pricing.trialDays = 30;
    video.updates.period = UpdatePeriod::Until;
    video.updates.until = "2027-12-31";
    video.ageRating = "3";
    video.openSource = true;
    video.sourceUrl = "https://github.com/ULTRA-OS-Project/UltraCanvas";
    {
        Release r;
        r.version = "0.2.1";
        r.channel = "stable";
        r.date = "2026-10-22";
        r.depends = {{"org.ultraos.runtime", "0.9.410"}, {"org.ultraos.lib.videofx", "0.12.0"}};
        r.packages = {
            Pkg(OS::Linux, Arch::X86_64, "7z", "ultravideo-0.2.1-linux-x86_64.7z", 3000000, 'i', {}, "UltraVideo"),
            Pkg(OS::Windows, Arch::X86_64, "zip", "ultravideo-0.2.1-windows-x86_64.zip", 2500000, 'j', {}, "UltraVideo.exe"),
            Pkg(OS::MacOS, Arch::Universal, "zip", "ultravideo-0.2.1-macos-universal.zip", 6000000, 'k', {}, "UltraVideo.app"),
        };
        video.releases.push_back(r);
        Release beta;
        beta.version = "0.3.0-beta.1";
        beta.channel = "beta";
        beta.depends = {{"org.ultraos.runtime", "0.9.410"}, {"org.ultraos.lib.videofx", "0.12.0"}};
        beta.packages = {Pkg(OS::Linux, Arch::X86_64, "7z", "ultravideo-0.3.0-beta.1-linux-x86_64.7z", 3100000, 'l', {}, "UltraVideo")};
        video.releases.push_back(beta);
    }
    c.apps.push_back(video);

    Component linuxOnly;
    linuxOnly.id = "org.ultraos.linuxtool";
    linuxOnly.publisher = "ultra-os-project";
    linuxOnly.name = "Linux Tool";
    {
        Release r;
        r.version = "1.0.0";
        r.channel = "stable";
        r.depends = {{"org.ultraos.runtime", "0.9.400"}};
        r.packages = {Pkg(OS::Linux, Arch::X86_64, "7z", "linuxtool-1.0.0-linux-x86_64.7z", 1000000, 'm', {}, "linuxtool")};
        linuxOnly.releases.push_back(r);
    }
    c.apps.push_back(linuxOnly);

    Component intelMac;
    intelMac.id = "org.ultraos.intelonly";
    intelMac.publisher = "ultra-os-project";
    intelMac.name = "Intel Only";
    intelMac.visibility = Visibility::Unlisted;
    intelMac.lifecycle = Lifecycle::Deprecated;
    intelMac.successor = "org.ultraos.ultravideo";
    {
        Release r;
        r.version = "2.0.0";
        r.channel = "stable";
        r.depends = {{"org.ultraos.runtime", "0.9.410"}};
        r.packages = {Pkg(OS::MacOS, Arch::X86_64, "zip", "intelonly-2.0.0-macos-x86_64.zip", 4000000, 'n', {}, "IntelOnly.app")};
        intelMac.releases.push_back(r);
    }
    c.apps.push_back(intelMac);
    return c;
}

inline UltraStore::PlatformInfo Machine(UltraStore::OS os, UltraStore::Arch arch, const std::string& osVersion = "",
                                        std::vector<std::string> variants = {}, bool rosetta = false) {
    UltraStore::PlatformInfo p;
    p.target.os = os;
    p.target.arch = arch;
    p.target.variants = std::move(variants);
    p.osVersion = osVersion;
    p.canRunX86_64 = rosetta || arch == UltraStore::Arch::X86_64;
    return p;
}

} // namespace ultrastore_test

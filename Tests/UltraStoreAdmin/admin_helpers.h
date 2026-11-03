// Tests/UltraStoreAdmin/admin_helpers.h
// A temporary admin store per test, and a component the tests agree on.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "test_framework.h"

#include "AdminDatabase.h"
#include "Catalog.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace admin_test {

inline std::string TempDir(const char* tag) {
    std::mt19937_64 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() / ("ultrastoreadmin-" + std::string(tag) + "-" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return UltraCanvas::PathToUtf8(dir);
}

inline void RemoveDir(const std::string& dirUtf8) {
    std::error_code ec;
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dirUtf8), ec);
}

inline std::string Join(const std::string& dirUtf8, const std::string& name) {
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dirUtf8) / UltraCanvas::PathFromUtf8(name));
}

inline bool WriteFile(const std::string& pathUtf8, const std::string& bytes) {
    std::ofstream out(UltraCanvas::PathFromUtf8(pathUtf8), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

inline std::string ReadFile(const std::string& pathUtf8) {
    std::ifstream in(UltraCanvas::PathFromUtf8(pathUtf8), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// An app with a summary in English and German, one release with one package.
inline UltraStore::Component HelloApp() {
    UltraStore::Component c;
    c.kind = UltraStore::ComponentKind::App;
    c.id = "org.ultraos.hello";
    c.name = "Hello";
    c.publisher = "ultra-os-project";
    c.license = "MIT";
    c.homepage = "https://ultraos.eu";
    c.summary.byLanguage["en"] = "Says hello";
    c.summary.byLanguage["de"] = "Sagt Hallo";
    c.description.byLanguage["en"] = "A tiny program that prints a greeting.";
    c.sourceLanguage = "en";
    c.machineTranslated = {"de"};
    c.categories = {"Tools"};
    c.tags = {"demo"};
    c.pricing.model = UltraStore::PricingModel::Free;
    UltraStore::Release r;
    r.version = "1.0.0";
    r.channel = "stable";
    r.date = "2026-10-27";
    r.notes.byLanguage["en"] = "First.";
    r.depends = {{"org.ultraos.runtime", "0.9.410"}};
    UltraStore::Package p;
    p.target.os = UltraStore::OS::Linux;
    p.target.arch = UltraStore::Arch::X86_64;
    p.format = "zip";
    p.sha256 = std::string(64, 'a');
    p.size = 1234;
    p.entry = "bin/hello";
    r.packages.push_back(p);
    c.releases.push_back(r);
    return c;
}

inline UltraStore::Component Runtime() {
    UltraStore::Component c;
    c.kind = UltraStore::ComponentKind::Runtime;
    c.id = "org.ultraos.runtime";
    c.name = "UltraCanvas runtime";
    UltraStore::Release r;
    r.version = "0.9.410";
    r.channel = "stable";
    UltraStore::Package p;
    p.target.os = UltraStore::OS::Linux;
    p.target.arch = UltraStore::Arch::X86_64;
    p.format = "zip";
    p.sha256 = std::string(64, 'b');
    p.size = 5678;
    r.packages.push_back(p);
    c.releases.push_back(r);
    return c;
}

} // namespace admin_test

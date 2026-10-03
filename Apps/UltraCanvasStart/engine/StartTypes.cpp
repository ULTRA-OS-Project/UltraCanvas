// Apps/UltraCanvasStart/engine/StartTypes.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartTypes.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvasStart {

std::string PlatformName(Platform platform) {
    switch (platform) {
        case Platform::Linux:   return "Linux";
        case Platform::MacOS:   return "macOS";
        case Platform::Windows: return "Windows";
        default:                return "Unknown";
    }
}

std::string PackageManagerName(PackageManager manager) {
    switch (manager) {
        case PackageManager::Apt:         return "apt";
        case PackageManager::Dnf:         return "dnf";
        case PackageManager::Pacman:      return "pacman";
        case PackageManager::Zypper:      return "zypper";
        case PackageManager::Homebrew:    return "Homebrew";
        case PackageManager::Msys2Pacman: return "MSYS2 pacman";
        default:                          return "none";
    }
}

Platform PlatformOf(PackageManager manager) {
    switch (manager) {
        case PackageManager::Apt:
        case PackageManager::Dnf:
        case PackageManager::Pacman:
        case PackageManager::Zypper:      return Platform::Linux;
        case PackageManager::Homebrew:    return Platform::MacOS;
        case PackageManager::Msys2Pacman: return Platform::Windows;
        default:                          return Platform::Unknown;
    }
}

std::string NormalizeArchitecture(const std::string& raw) {
    std::string lower;
    lower.reserve(raw.size());
    for (unsigned char c : raw) lower.push_back(static_cast<char>(std::tolower(c)));
    if (lower == "x86_64" || lower == "amd64" || lower == "x64") return "x86_64";
    if (lower == "arm64" || lower == "aarch64") return "arm64";
    if (lower.empty()) return "unknown";
    return lower;
}

std::string DependencyGroupTitle(DependencyGroup group) {
    switch (group) {
        case DependencyGroup::Toolchain:  return "Toolchain";
        case DependencyGroup::Core:       return "Framework core";
        case DependencyGroup::Cdr:        return "CDR plug-in";
        case DependencyGroup::Pdf:        return "PDF plug-in";
        case DependencyGroup::Ocr:        return "OCR plug-in";
        case DependencyGroup::Vectorizer: return "Vectorizer plug-in";
        case DependencyGroup::Audio:      return "Audio codecs";
        case DependencyGroup::Barcode:    return "Barcode decoding";
        case DependencyGroup::Net:        return "Networking and cryptography extras";
    }
    return "Other";
}

std::string DependencyGroupDescription(DependencyGroup group) {
    switch (group) {
        case DependencyGroup::Toolchain:
            return "A C++20 compiler, CMake 3.16 or newer, pkg-config and git. "
                   "Needed whether you build the framework or use the SDK.";
        case DependencyGroup::Core:
            return "Cairo, Pango, HarfBuzz, FreeType, GLib, libvips, tinyxml2 "
                   "and fmt - the framework and every application need these.";
        case DependencyGroup::Cdr:
            return "libcdr and what it pulls in, for reading CorelDRAW files.";
        case DependencyGroup::Pdf:
            return "MuPDF, for the PDF plug-in.";
        case DependencyGroup::Ocr:
            return "Tesseract and Leptonica, for text recognition in images.";
        case DependencyGroup::Vectorizer:
            return "A Rust toolchain: the Vectorizer plug-in is built from Rust.";
        case DependencyGroup::Audio:
            return "FLAC, Vorbis, Opus and MP3 codecs for the audio player.";
        case DependencyGroup::Barcode:
            return "zbar, for reading barcodes and QR codes from images.";
        case DependencyGroup::Net:
            return "c-ares for UltraNet's DNS and libsodium for UltraCrypt.";
    }
    return "";
}

const std::string& Dependency::PackageFor(PackageManager manager) const {
    switch (manager) {
        case PackageManager::Apt:         return apt;
        case PackageManager::Dnf:         return dnf;
        case PackageManager::Pacman:      return pacman;
        case PackageManager::Zypper:      return zypper;
        case PackageManager::Homebrew:    return brew;
        case PackageManager::Msys2Pacman: return msys2;
        default: {
            static const std::string none;
            return none;
        }
    }
}

bool Choices::Has(DependencyGroup group) const {
    return std::find(groups.begin(), groups.end(), group) != groups.end();
}

void Choices::Set(DependencyGroup group, bool on) {
    auto found = std::find(groups.begin(), groups.end(), group);
    if (on && found == groups.end()) groups.push_back(group);
    if (!on && found != groups.end()) groups.erase(found);
}

size_t Plan::MissingCount() const {
    size_t count = 0;
    for (const auto& check : checks) {
        if (check.checked && (!check.present || !check.versionOk)) ++count;
    }
    return count;
}

namespace {

std::vector<long> VersionParts(const std::string& text) {
    std::vector<long> parts;
    long current = 0;
    bool inNumber = false;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current = current * 10 + (c - '0');
            inNumber = true;
        } else if (c == '.' && inNumber) {
            parts.push_back(current);
            current = 0;
            inNumber = false;
        } else {
            break;
        }
    }
    if (inNumber) parts.push_back(current);
    return parts;
}

} // namespace

int CompareVersions(const std::string& a, const std::string& b) {
    const auto pa = VersionParts(a);
    const auto pb = VersionParts(b);
    const size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        const long va = i < pa.size() ? pa[i] : 0;
        const long vb = i < pb.size() ? pb[i] : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

std::string ExtractVersion(const std::string& text) {
    // The first run of digits that is followed by a dot and another digit.
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
        if (i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) ||
                      text[i - 1] == '_')) {
            continue;   // part of a word such as "x86_64" or "clang17"
        }
        size_t j = i;
        bool sawDot = false;
        while (j < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[j])) ||
                (text[j] == '.' && j + 1 < text.size() &&
                 std::isdigit(static_cast<unsigned char>(text[j + 1]))))) {
            if (text[j] == '.') sawDot = true;
            ++j;
        }
        if (sawDot) return text.substr(i, j - i);
        i = j;
    }
    return {};
}

} // namespace UltraCanvasStart

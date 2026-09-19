// core/IODeviceManager/UltraCanvasIODevicePrinterGutenPrintModels.cpp
// Finding GutenPrint's tools, and working out which of its three and a half
// thousand models is the printer in front of us.
//
// Split from the renderer so it can be tested. Rasterising a page pulls in
// the whole imaging stack, and the printer tests deliberately link the
// platform-neutral printer sources and nothing else - but parsing a listing
// and matching a model name are ordinary string work that needs none of it,
// and they are where the fiddly decisions are.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterGutenPrint.h"

#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace UltraCanvas {

namespace {

// ============================================================================
// SMALL HELPERS
// ============================================================================

// Letters and digits, folded to lower case.
//
// Model names are written every way a marketing department can think of -
// "Stylus Photo R2400", "STYLUS_PHOTO_R2400", "Stylus-Photo R2400" - and the
// punctuation carries no information. Stripping it is what lets the printer's
// own name meet GutenPrint's.
std::string NormalizeName(const std::string& text) {
    std::string folded;
    folded.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            folded.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return folded;
}

// The value of one key in an IEEE-1284 device id ("MFG:EPSON;MDL:Stylus;").
// Both the long and short spellings are accepted, because printers use both.
std::string DeviceIdField(const std::string& deviceId,
                          const std::string& shortKey,
                          const std::string& longKey) {
    for (const std::string& key : {shortKey, longKey}) {
        size_t at = 0;
        while ((at = deviceId.find(key + ":", at)) != std::string::npos) {
            // Only at the start or just after a ';', so MDL: does not match
            // inside the middle of another field's value.
            if (at == 0 || deviceId[at - 1] == ';') {
                const size_t from = at + key.size() + 1;
                const size_t to = deviceId.find(';', from);
                return deviceId.substr(from, to == std::string::npos
                                                 ? std::string::npos
                                                 : to - from);
            }
            at += key.size() + 1;
        }
    }
    return std::string();
}

// "Epson Stylus Photo R2400 - CUPS+Gutenprint v5.3.4" -> the part before the
// " - ", which is the model as a person would write it.
std::string DescriptionModel(const std::string& description) {
    const size_t dash = description.find(" - ");
    return dash == std::string::npos ? description : description.substr(0, dash);
}

// Reads one "quoted field" starting at `at`, leaving `at` past the close.
bool ReadQuoted(const std::string& line, size_t& at, std::string& out) {
    while (at < line.size() && line[at] != '"') ++at;
    if (at >= line.size()) return false;
    const size_t from = ++at;
    while (at < line.size() && line[at] != '"') ++at;
    if (at >= line.size()) return false;
    out = line.substr(from, at - from);
    ++at;
    return true;
}

// ============================================================================
// WHERE THE TOOLS LIVE
// ============================================================================

bool IsRunnable(const std::string& path) {
    if (path.empty()) return false;
    std::error_code code;
    return std::filesystem::is_regular_file(PathFromUtf8(path), code);
}

std::string FromEnvironment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

// GutenPrint's version is in its file names, and more than one can be
// installed, so the newest known is tried first.
const char* const kSeries[] = {"5.3", "5.2"};

}  // namespace

IOGutenPrintTools FindGutenPrintTools() {
    IOGutenPrintTools tools;

    // An explicit path wins outright: a packaged build ships the binaries
    // beside the application and says where, and no search should second-guess
    // that.
    const std::string namedDriver = FromEnvironment("ULTRACANVAS_GUTENPRINT_DRIVER");
    const std::string namedFilter = FromEnvironment("ULTRACANVAS_GUTENPRINT_FILTER");
    if (IsRunnable(namedDriver)) tools.driver = namedDriver;
    if (IsRunnable(namedFilter)) tools.filter = namedFilter;
    if (tools.IsComplete()) return tools;

    // Otherwise the places a system install puts them. CUPS keeps its driver
    // interfaces and its filters in separate directories, and the directory
    // differs by distribution and by architecture.
    static const char* const kDriverDirs[] = {
        "/usr/lib/cups/driver", "/usr/libexec/cups/driver",
        "/usr/lib64/cups/driver", "/usr/local/lib/cups/driver",
        "/opt/homebrew/lib/cups/driver", "/usr/local/libexec/cups/driver"
    };
    static const char* const kFilterDirs[] = {
        "/usr/lib/cups/filter", "/usr/libexec/cups/filter",
        "/usr/lib64/cups/filter", "/usr/local/lib/cups/filter",
        "/opt/homebrew/lib/cups/filter", "/usr/local/libexec/cups/filter"
    };

    for (const char* series : kSeries) {
        for (const char* dir : kDriverDirs) {
            if (!tools.driver.empty()) break;
            const std::string candidate =
                std::string(dir) + "/gutenprint." + series;
            if (IsRunnable(candidate)) tools.driver = candidate;
        }
        for (const char* dir : kFilterDirs) {
            if (!tools.filter.empty()) break;
            const std::string candidate =
                std::string(dir) + "/rastertogutenprint." + series;
            if (IsRunnable(candidate)) tools.filter = candidate;
        }
        if (tools.IsComplete()) break;
    }
    return tools;
}

// ============================================================================
// MODELS
// ============================================================================

std::vector<IOGutenPrintModel> ParseGutenPrintModels(const std::string& listing) {
    std::vector<IOGutenPrintModel> models;

    size_t lineStart = 0;
    while (lineStart <= listing.size()) {
        const size_t lineEnd = listing.find('\n', lineStart);
        const std::string line = listing.substr(
            lineStart, lineEnd == std::string::npos ? std::string::npos
                                                    : lineEnd - lineStart);
        lineStart = lineEnd == std::string::npos ? listing.size() + 1 : lineEnd + 1;

        if (line.empty()) continue;

        IOGutenPrintModel model;
        size_t at = 0;
        std::string language;
        if (!ReadQuoted(line, at, model.uri)) continue;

        // The language sits between quoted fields, unquoted; skipped by
        // simply reading the next quoted field.
        if (!ReadQuoted(line, at, model.manufacturer)) continue;
        if (!ReadQuoted(line, at, model.description)) continue;

        // The device id is optional - GutenPrint leaves it empty for many of
        // the older models - so a line without one is still a usable model.
        ReadQuoted(line, at, model.deviceId);

        if (!model.uri.empty()) models.push_back(std::move(model));
    }
    return models;
}

std::string MatchGutenPrintModel(const std::vector<IOGutenPrintModel>& models,
                                 const std::string& manufacturer,
                                 const std::string& model) {
    const std::string wantedModel = NormalizeName(model);
    if (wantedModel.empty()) return std::string();

    const std::string wantedMaker = NormalizeName(manufacturer);
    const std::string wantedFull = wantedMaker + wantedModel;

    // Two passes rather than one scored loop, because the order is the rule:
    // the printer's own device id is a better answer than a description that
    // happens to read the same, and a run of near-matches must not let a
    // weaker kind of match on an earlier line beat a stronger one later.
    for (const IOGutenPrintModel& candidate : models) {
        if (candidate.deviceId.empty()) continue;
        const std::string maker =
            NormalizeName(DeviceIdField(candidate.deviceId, "MFG", "MANUFACTURER"));
        const std::string name =
            NormalizeName(DeviceIdField(candidate.deviceId, "MDL", "MODEL"));
        if (name.empty()) continue;

        // The manufacturer is checked when both sides state one, and ignored
        // when either does not: CUPS splits "make and model" on the first
        // space, so a printer whose make is two words arrives with half of it
        // in the model, and refusing the match over that would be pedantry.
        if (name == wantedModel &&
            (wantedMaker.empty() || maker.empty() || maker == wantedMaker)) {
            return candidate.uri;
        }
        if (maker + name == wantedFull) return candidate.uri;
    }

    for (const IOGutenPrintModel& candidate : models) {
        const std::string described = NormalizeName(DescriptionModel(candidate.description));
        if (described.empty()) continue;
        if (described == wantedFull || described == wantedModel) {
            return candidate.uri;
        }
        // "Epson" + "Stylus Photo R2400" against a description that already
        // begins with the maker.
        if (!wantedMaker.empty() &&
            described == NormalizeName(candidate.manufacturer) + wantedModel) {
            return candidate.uri;
        }
    }

    return std::string();
}

}  // namespace UltraCanvas

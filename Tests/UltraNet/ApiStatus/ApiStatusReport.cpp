// Tests/UltraNet/ApiStatus/ApiStatusReport.cpp
// Probe registry, execution, and the three report renderers (text table,
// Markdown table, JSON).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ultranet_apistatus {

namespace {

// Report order. Roughly "foundation first, then protocols, then helpers" —
// the order a reader would walk the headers in.
const char* const kAreaOrder[] = {
    "Core",
    "URL",
    "HTTP",
    "Session",
    "SSE",
    "WebSocket",
    "DNS",
    "Socket",
    "TLS",
    "FTP",
    "MIME",
    "Plugins",
};

bool g_networkEnabled = false;

bool IsTerminal(std::FILE* stream) {
#if defined(_WIN32) || defined(_WIN64)
    return _isatty(_fileno(stream)) != 0;
#else
    return ::isatty(::fileno(stream)) != 0;
#endif
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string JsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Markdown table cells: '|' would end the cell early.
std::string MarkdownEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

struct Row {
    std::string area;
    std::string symbol;
    Outcome     outcome;
};

struct Totals {
    int working = 0;
    int implemented = 0;
    int notImplemented = 0;
    int broken = 0;

    int Count() const { return working + implemented + notImplemented + broken; }

    void Add(Status s) {
        switch (s) {
            case Status::Working:        ++working;        break;
            case Status::Implemented:    ++implemented;    break;
            case Status::NotImplemented: ++notImplemented; break;
            case Status::Broken:         ++broken;         break;
        }
    }
};

// ---------------------------------------------------------------------------
// Renderers
// ---------------------------------------------------------------------------

void RenderText(std::ostream& os, const std::vector<Row>& rows,
                const Totals& totals,
                const std::map<std::string, Totals>& perArea) {
    std::size_t symbolWidth = 8;
    for (const Row& r : rows) {
        symbolWidth = std::max(symbolWidth, r.symbol.size());
    }
    const std::size_t statusWidth = 15;   // "NOT IMPLEMENTED"

    auto pad = [](const std::string& s, std::size_t w) {
        return s.size() >= w ? s : s + std::string(w - s.size(), ' ');
    };
    auto rule = [&](char c) {
        os << std::string(symbolWidth + statusWidth + 5, c) << '\n';
    };

    std::string currentArea;
    for (const Row& r : rows) {
        if (r.area != currentArea) {
            currentArea = r.area;
            const Totals& t = perArea.at(currentArea);
            os << '\n' << currentArea << "  ["
               << t.working << " working / " << t.Count() << " probed]\n";
            rule('-');
        }
        os << "  " << pad(r.symbol, symbolWidth) << "  "
           << pad(StatusLabel(r.outcome.status), statusWidth) << "  "
           << r.outcome.detail << '\n';
    }

    os << '\n';
    rule('=');
    os << "TOTAL " << totals.Count() << " API entries probed:  "
       << totals.working << " working, "
       << totals.implemented << " implemented (unverified here), "
       << totals.notImplemented << " not implemented, "
       << totals.broken << " broken\n";
}

void RenderMarkdown(std::ostream& os, const std::vector<Row>& rows,
                    const Totals& totals,
                    const std::map<std::string, Totals>& perArea) {
    os << "# UltraNet API status\n\n"
       << "| Status | Meaning |\n|---|---|\n"
       << "| WORKING | Probe drove the real code path and the result matched the API contract |\n"
       << "| IMPLEMENTED | Implementation present and reached, but unverifiable in this environment |\n"
       << "| NOT IMPLEMENTED | Documented stub / no-op, or the required backend is absent |\n"
       << "| BROKEN | Probe ran and the result contradicted the API |\n\n"
       << "**Totals:** " << totals.Count() << " entries — "
       << totals.working << " working, "
       << totals.implemented << " implemented, "
       << totals.notImplemented << " not implemented, "
       << totals.broken << " broken.\n";

    std::string currentArea;
    for (const Row& r : rows) {
        if (r.area != currentArea) {
            currentArea = r.area;
            const Totals& t = perArea.at(currentArea);
            os << "\n## " << currentArea << " ("
               << t.working << '/' << t.Count() << " working)\n\n"
               << "| API | Status | Notes |\n|---|---|---|\n";
        }
        os << "| `" << MarkdownEscape(r.symbol) << "` | "
           << StatusLabel(r.outcome.status) << " | "
           << MarkdownEscape(r.outcome.detail) << " |\n";
    }
}

void RenderJson(std::ostream& os, const std::vector<Row>& rows,
                const Totals& totals) {
    os << "{\n"
       << "  \"totals\": {\n"
       << "    \"probed\": "         << totals.Count()          << ",\n"
       << "    \"working\": "        << totals.working          << ",\n"
       << "    \"implemented\": "    << totals.implemented      << ",\n"
       << "    \"notImplemented\": " << totals.notImplemented   << ",\n"
       << "    \"broken\": "         << totals.broken           << "\n"
       << "  },\n"
       << "  \"entries\": [\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        os << "    {\"area\": \""   << JsonEscape(r.area)   << "\", "
           << "\"symbol\": \""      << JsonEscape(r.symbol) << "\", "
           << "\"status\": \""      << StatusKey(r.outcome.status) << "\", "
           << "\"detail\": \""      << JsonEscape(r.outcome.detail) << "\"}"
           << (i + 1 < rows.size() ? "," : "") << '\n';
    }
    os << "  ]\n}\n";
}

} // namespace

// ---------------------------------------------------------------------------

const char* StatusLabel(Status s) {
    switch (s) {
        case Status::Working:        return "WORKING";
        case Status::Implemented:    return "IMPLEMENTED";
        case Status::NotImplemented: return "NOT IMPLEMENTED";
        case Status::Broken:         return "BROKEN";
    }
    return "?";
}

const char* StatusKey(Status s) {
    switch (s) {
        case Status::Working:        return "working";
        case Status::Implemented:    return "implemented";
        case Status::NotImplemented: return "not_implemented";
        case Status::Broken:         return "broken";
    }
    return "unknown";
}

Outcome Working(std::string detail)        { return {Status::Working,        std::move(detail)}; }
Outcome Implemented(std::string detail)    { return {Status::Implemented,    std::move(detail)}; }
Outcome NotImplemented(std::string detail) { return {Status::NotImplemented, std::move(detail)}; }
Outcome Broken(std::string detail)         { return {Status::Broken,         std::move(detail)}; }

std::vector<Probe>& Registry() {
    static std::vector<Probe> v;
    return v;
}

int AreaOrder(const std::string& area) {
    const int count = static_cast<int>(sizeof kAreaOrder / sizeof kAreaOrder[0]);
    for (int i = 0; i < count; ++i) {
        if (area == kAreaOrder[i]) return i;
    }
    return count;
}

Reg::Reg(const char* area, const char* symbol, std::function<Outcome()> fn) {
    Registry().push_back({area, symbol, std::move(fn)});
}

bool NetworkProbesEnabled() { return g_networkEnabled; }

std::string EnvOverride(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

int Run(const RunOptions& options) {
    g_networkEnabled = options.allowNetwork ||
                       EnvOverride("ULTRANET_PROBE_NETWORK") == "1";

    // Stable sort by area only: within one area the registration order is the
    // static-init order of that area's single translation unit, i.e. the order
    // the probes are written — which mirrors the header.
    std::vector<Probe> probes = Registry();
    std::stable_sort(probes.begin(), probes.end(),
                     [](const Probe& a, const Probe& b) {
                         return AreaOrder(a.area) < AreaOrder(b.area);
                     });

    const std::string filter = ToLower(options.areaFilter);

    std::vector<Row> rows;
    Totals totals;
    std::map<std::string, Totals> perArea;

    // Live progress is a carriage-return ticker, which only makes sense on a
    // terminal — piping the tool into a log would otherwise fill it with one
    // line per probe. It goes to stderr either way so `--format=json > file`
    // stays valid JSON.
    const bool showProgress = IsTerminal(stderr);

    for (const Probe& p : probes) {
        if (!filter.empty() && ToLower(p.area) != filter) continue;

        if (showProgress) {
            std::fprintf(stderr, "  probing %-40s\r", p.symbol.c_str());
            std::fflush(stderr);
        }

        Outcome outcome;
        try {
            outcome = p.fn();
        } catch (const std::exception& e) {
            outcome = Broken(std::string("probe threw: ") + e.what());
        } catch (...) {
            outcome = Broken("probe threw a non-std exception");
        }

        totals.Add(outcome.status);
        perArea[p.area].Add(outcome.status);
        rows.push_back({p.area, p.symbol, std::move(outcome)});
    }
    if (showProgress) {
        std::fprintf(stderr, "%-60s\r", "");
        std::fflush(stderr);
    }

    if (rows.empty()) {
        std::fprintf(stderr, "no probes matched area filter '%s'\n",
                     options.areaFilter.c_str());
        return 2;
    }

    std::ostringstream body;
    if (options.format == "json")          RenderJson(body, rows, totals);
    else if (options.format == "markdown") RenderMarkdown(body, rows, totals, perArea);
    else                                   RenderText(body, rows, totals, perArea);

    if (options.outputPath.empty()) {
        std::cout << body.str();
        std::cout.flush();
    } else {
        std::ofstream out(options.outputPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "cannot write report to '%s'\n",
                         options.outputPath.c_str());
            return 2;
        }
        out << body.str();
        std::fprintf(stderr, "report written to %s\n", options.outputPath.c_str());
    }

    // Exit code: BROKEN always fails the run. --strict additionally fails when
    // anything is short of WORKING, for a "no regressions in coverage" gate.
    if (totals.broken > 0) return 1;
    if (options.strict && (totals.implemented || totals.notImplemented)) return 1;
    return 0;
}

} // namespace ultranet_apistatus

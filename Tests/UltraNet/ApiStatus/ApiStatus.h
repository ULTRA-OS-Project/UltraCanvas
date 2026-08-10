// Tests/UltraNet/ApiStatus/ApiStatus.h
// Framework for the UltraNet API status report (the UltraNetApiStatus tool).
//
// Where the UltraNetTests binary answers "does the code still behave?", this
// tool answers a different question: "for every function in the public
// UltraNet surface, what is its state right now on this machine?" Each entry
// in the public API gets exactly one probe, and each probe reports one of:
//
//   WORKING          the probe drove the real code path end-to-end and the
//                    observable result matched what the API promises
//   IMPLEMENTED      the implementation exists and was reached, but this
//                    environment cannot confirm the behaviour (no live peer,
//                    no optional backend, platform-gated)
//   NOT IMPLEMENTED  a documented stub / no-op, or the backend it needs is
//                    absent from this build
//   BROKEN           the probe ran and the result contradicted the API
//
// BROKEN exists so the report cannot quietly turn a regression into a
// reassuring "implemented" — a status tool that can only report good news is
// not worth running.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ultranet_apistatus {

enum class Status {
    Working,
    Implemented,
    NotImplemented,
    Broken
};

const char* StatusLabel(Status s);   // "WORKING", "IMPLEMENTED", ...
const char* StatusKey(Status s);     // "working", "implemented", ... (JSON)

struct Outcome {
    Status      status = Status::Broken;
    std::string detail;
};

// Outcome constructors. `detail` is the one-line "why" shown in the report:
// for Working it says what was verified, for the others what is missing.
Outcome Working(std::string detail);
Outcome Implemented(std::string detail);
Outcome NotImplemented(std::string detail);
Outcome Broken(std::string detail);

struct Probe {
    std::string              area;
    std::string              symbol;
    std::function<Outcome()> fn;
};

std::vector<Probe>& Registry();

// Areas are reported in a fixed order (declaration order inside each area is
// preserved, so a probe file reads top-to-bottom like its header). An area
// name not listed in ApiStatusReport.cpp sorts last.
int AreaOrder(const std::string& area);

struct Reg {
    Reg(const char* area, const char* symbol, std::function<Outcome()> fn);
};

// Runs every registered probe (optionally filtered) and renders the report.
struct RunOptions {
    std::string format = "text";   // "text" | "markdown" | "json"
    std::string areaFilter;        // empty = all areas (case-insensitive)
    std::string outputPath;        // empty = stdout
    bool        strict = false;    // exit non-zero if anything is not WORKING
    bool        allowNetwork = false;
};

int Run(const RunOptions& options);

// True when the caller opted into probes that need the public internet
// (--network, or ULTRANET_PROBE_NETWORK=1). Off by default so the tool is
// deterministic and CI-safe.
bool NetworkProbesEnabled();

// Value of an ULTRANET_PROBE_* environment override, or "" when unset.
std::string EnvOverride(const char* name);

} // namespace ultranet_apistatus

// ---------------------------------------------------------------------------
// Probe declaration. ULTRANET_PROBE takes the bare symbol name; the _NAMED
// form covers entries whose reported name is not a valid identifier (class
// methods, grouped entries).
// ---------------------------------------------------------------------------
#define ULTRANET_PROBE_NAMED(areaName, displayName, uniqueId)                 \
    static ::ultranet_apistatus::Outcome ultranet_probe_##uniqueId();         \
    static ::ultranet_apistatus::Reg ultranet_probe_reg_##uniqueId{           \
        areaName, displayName, ultranet_probe_##uniqueId};                    \
    static ::ultranet_apistatus::Outcome ultranet_probe_##uniqueId()

#define ULTRANET_PROBE(areaName, symbol)                                      \
    ULTRANET_PROBE_NAMED(areaName, #symbol, symbol)

// Assertion inside a probe body: a failed expectation means the API misbehaved,
// which is BROKEN, not "unverified".
#define PROBE_EXPECT(expr)                                                    \
    do {                                                                      \
        if (!(expr)) {                                                        \
            return ::ultranet_apistatus::Broken(                              \
                "expectation failed: " #expr);                                \
        }                                                                     \
    } while (0)

// Same, with a caller-supplied explanation appended.
#define PROBE_EXPECT_MSG(expr, msg)                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            return ::ultranet_apistatus::Broken(                              \
                std::string("expectation failed: " #expr " — ") + (msg));     \
        }                                                                     \
    } while (0)

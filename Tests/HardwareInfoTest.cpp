// Tests/HardwareInfoTest.cpp
// Unit tests for the hardware inventory (include/UltraCanvasHardwareInfo.h):
// the value formatters, identifier masking, the query selector, the display
// report and the JSON export, plus the invariants a capture must hold on the
// machine the test runs on.
//
// Headless — the module has no UI dependency; the panel that renders it is
// tested through the demo app.
//
// Version: 1.0.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

#include "UltraCanvasHardwareInfo.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

// =============================================================================

static void TestFormatters() {
    std::printf("Value formatting\n");
    CHECK(UltraCanvasHardwareInfo::FormatBytes(0) == "0 B", "zero bytes");
    CHECK(UltraCanvasHardwareInfo::FormatBytes(512) == "512 B", "bytes below a kilobyte");
    CHECK(UltraCanvasHardwareInfo::FormatBytes(32ull * 1024) == "32 KB", "kilobytes are whole");
    CHECK(UltraCanvasHardwareInfo::FormatBytes(1536ull * 1024 * 1024) == "1.50 GB",
          "gigabytes keep two decimals below ten");
    CHECK(UltraCanvasHardwareInfo::FormatBytes(16ull * 1024 * 1024 * 1024) == "16.0 GB",
          "and one decimal below a hundred");

    CHECK(UltraCanvasHardwareInfo::FormatFrequencyMHz(0) == "-", "an unknown clock is a dash");
    CHECK(UltraCanvasHardwareInfo::FormatFrequencyMHz(800) == "800 MHz", "megahertz stay megahertz");
    CHECK(UltraCanvasHardwareInfo::FormatFrequencyMHz(3600) == "3.60 GHz", "and roll over to GHz");

    CHECK(UltraCanvasHardwareInfo::FormatBitrateMbps(0) == "-", "an unknown rate is a dash");
    CHECK(UltraCanvasHardwareInfo::FormatBitrateMbps(100) == "100 Mb/s", "megabits");
    CHECK(UltraCanvasHardwareInfo::FormatBitrateMbps(2500) == "2.5 Gb/s", "and gigabits");

    CHECK(UltraCanvasHardwareInfo::FormatDuration(90) == "00:01", "a minute and a half");
    CHECK(UltraCanvasHardwareInfo::FormatDuration(3600 * 26) == "1 d 02:00", "over a day");
}

static void TestMasking() {
    std::printf("Identifier masking\n");
    CHECK(UltraCanvasHardwareInfo::MaskIdentifier("") == "", "an empty identifier stays empty");
    CHECK(UltraCanvasHardwareInfo::MaskIdentifier("S64ANS0T123456") == "**********3456",
          "a serial keeps only its last four characters");
    CHECK(UltraCanvasHardwareInfo::MaskIdentifier("ab:cd:ef:12:34:56") == "**:**:**:**:34:56",
          "a MAC address is masked by octet, not by character");
    CHECK(UltraCanvasHardwareInfo::MaskIdentifier("XY") == "*Y", "a short identifier keeps one char");
    CHECK(UltraCanvasHardwareInfo::MaskIdentifier("  spaced  ").find(' ') == std::string::npos,
          "surrounding whitespace is dropped");
}

static void TestQueryFlags() {
    std::printf("Query selector\n");
    const HardwareQuery pair = HardwareQuery::CPU | HardwareQuery::Memory;
    CHECK(HasQuery(pair, HardwareQuery::CPU), "a combined query contains its parts");
    CHECK(HasQuery(pair, HardwareQuery::Memory), "both of them");
    CHECK(!HasQuery(pair, HardwareQuery::USB), "and nothing else");
    CHECK(HasQuery(HardwareQuery::All, HardwareQuery::Bluetooth), "All contains every category");
    CHECK(HasQuery(HardwareQuery::All, HardwareQuery::Sensors), "including the sensor flag");
}

static void TestCacheHelpers() {
    std::printf("CPU cache helpers\n");
    CPUInfo cpu;
    CPUCacheInfo level1;
    level1.level = 1;
    level1.type = CPUCacheType::Data;
    level1.sizeBytes = 48ull * 1024;
    level1.instanceCount = 8;
    level1.associativity = 12;
    level1.lineSizeBytes = 64;
    cpu.caches.push_back(level1);

    CPUCacheInfo level3;
    level3.level = 3;
    level3.type = CPUCacheType::Unified;
    level3.sizeBytes = 24ull * 1024 * 1024;
    cpu.caches.push_back(level3);

    CHECK(cpu.TotalCacheSize(1) == 8 * 48ull * 1024, "L1 totals across every instance");
    CHECK(cpu.TotalCacheSize(3) == 24ull * 1024 * 1024, "a single L3 totals to itself");
    CHECK(cpu.TotalCacheSize(2) == 0, "a level the CPU does not have totals to zero");

    const std::string described = level1.Describe();
    CHECK(described.find("L1 data") == 0, "Describe names the level and the kind");
    CHECK(described.find("8 x 48 KB") != std::string::npos, "and the instance count");
    CHECK(described.find("12-way") != std::string::npos, "and the associativity");

    cpu.instructionSets = { "AVX2", "AES-NI" };
    CHECK(cpu.HasInstructionSet("avx2"), "instruction-set lookup ignores case");
    CHECK(!cpu.HasInstructionSet("AVX"), "and does not match a prefix");
}

static void TestReportShape() {
    std::printf("Report building\n");
    HardwareSnapshot snapshot;
    snapshot.captured = HardwareQuery::CPU | HardwareQuery::Memory;
    snapshot.cpu.model = "Test CPU";
    snapshot.cpu.physicalCores = 4;
    snapshot.cpu.logicalCores = 8;
    snapshot.cpu.socket = "";                 // absent: must not become a row
    snapshot.memory.totalBytes = 8ull * 1024 * 1024 * 1024;

    const auto groups = UltraCanvasHardwareInfo::BuildReport(snapshot);
    bool sawCPU = false, sawMemory = false, sawEmptyValue = false, sawSocket = false;
    for (const auto& group : groups) {
        if (group.id == "cpu") sawCPU = true;
        if (group.id == "memory") sawMemory = true;
        for (const auto& property : group.properties) {
            if (property.value.empty()) sawEmptyValue = true;
            if (property.name == "Socket") sawSocket = true;
        }
    }
    CHECK(sawCPU, "a captured CPU produces a processor group");
    CHECK(sawMemory, "and captured memory produces a memory group");
    CHECK(!sawEmptyValue, "no row carries an empty value");
    CHECK(!sawSocket, "a value the platform could not read produces no row at all");

    // Categories that were never captured must not appear as empty sections.
    for (const auto& group : groups)
        CHECK(group.id != "usb" && group.id != "bluetooth",
              "an uncaptured category produces no group");
}

static void TestLiveCapture() {
    std::printf("Live capture on this machine\n");
    const std::string backend = UltraCanvasHardwareInfo::GetBackendName();
    CHECK(!backend.empty(), "the backend names itself");
    std::printf("  (backend: %s, available: %s)\n", backend.c_str(),
                UltraCanvasHardwareInfo::IsAvailable() ? "yes" : "no");

    HardwareSnapshot snapshot = UltraCanvasHardwareInfo::Capture();
    CHECK(snapshot.capturedAtUnixSeconds > 0, "the snapshot is stamped");
    CHECK(snapshot.Has(HardwareQuery::CPU), "a full capture reports the CPU as captured");

    if (UltraCanvasHardwareInfo::IsAvailable()) {
        CHECK(snapshot.cpu.logicalCores >= 1, "at least one logical core is reported");
        CHECK(snapshot.cpu.physicalCores >= 1, "and at least one physical core");
        CHECK(snapshot.cpu.physicalCores <= snapshot.cpu.logicalCores,
              "physical cores never outnumber threads");
        CHECK(snapshot.memory.totalBytes > 0, "installed memory is reported");
        CHECK(snapshot.memory.availableBytes <= snapshot.memory.totalBytes,
              "available memory never exceeds installed memory");
    }

    // A narrowed capture must leave everything else alone.
    const HardwareSnapshot cpuOnly =
        UltraCanvasHardwareInfo::Capture(HardwareQuery::CPU, /*forceRefresh*/ true);
    CHECK(cpuOnly.Has(HardwareQuery::CPU), "a CPU query reports the CPU");
    CHECK(!cpuOnly.Has(HardwareQuery::USB), "and does not claim to have walked the USB tree");
    CHECK(cpuOnly.usbDevices.empty(), "which it did not");
    CHECK(cpuOnly.storage.empty(), "nor the drives");

    // The same must hold when the answer comes from the cache the full capture
    // above left behind, rather than from a fresh probe.
    const HardwareSnapshot cached =
        UltraCanvasHardwareInfo::Capture(HardwareQuery::CPU, /*forceRefresh*/ false);
    CHECK(cached.Has(HardwareQuery::CPU), "a cache-served query still reports the CPU");
    CHECK(!cached.Has(HardwareQuery::Storage), "and reports only what was asked for");
    CHECK(cached.storage.empty(), "carrying none of the broader snapshot it came from");
    CHECK(cached.cpu.logicalCores == cpuOnly.cpu.logicalCores,
          "while still answering the part that was asked for");

    // Refreshing sensors must never add or drop devices: callers hold indices.
    const size_t storageCount = snapshot.storage.size();
    const size_t networkCount = snapshot.network.size();
    UltraCanvasHardwareInfo::RefreshSensors(snapshot);
    CHECK(snapshot.storage.size() == storageCount, "a sensor refresh keeps the drive list");
    CHECK(snapshot.network.size() == networkCount, "and the interface list");

    const std::string text = UltraCanvasHardwareInfo::ToText(snapshot);
    CHECK(!text.empty(), "the text report is not empty");

    JSONParseResult result;
    const JSONValue parsed = JSON::Parse(UltraCanvasHardwareInfo::ToJSON(snapshot), &result);
    CHECK(result.success, "the JSON export parses back");
    CHECK(parsed.IsObject(), "as an object");
    CHECK(parsed.Get("backend").GetString() == backend, "carrying the backend name");
    CHECK(parsed.Get("cpu").IsObject(), "and the CPU block");
}

static void TestMaskingAppliesToCapture() {
    std::printf("Masking is applied to captured values\n");
    HardwareInfoOptions options;
    options.maskIdentifiers = true;
    UltraCanvasHardwareInfo::SetOptions(options);
    const HardwareSnapshot masked =
        UltraCanvasHardwareInfo::Capture(HardwareQuery::Network, /*forceRefresh*/ true);
    bool anyAddress = false, allMasked = true;
    for (const auto& adapter : masked.network) {
        if (adapter.macAddress.empty()) continue;
        anyAddress = true;
        if (adapter.macAddress.find('*') == std::string::npos) allMasked = false;
    }
    CHECK(!anyAddress || allMasked, "every MAC address comes back masked");

    options.maskIdentifiers = false;
    UltraCanvasHardwareInfo::SetOptions(options);
    const HardwareSnapshot plain =
        UltraCanvasHardwareInfo::Capture(HardwareQuery::Network, /*forceRefresh*/ true);
    bool anyPlain = false;
    for (const auto& adapter : plain.network)
        if (!adapter.macAddress.empty() && adapter.macAddress.find('*') == std::string::npos)
            anyPlain = true;
    CHECK(!anyAddress || anyPlain, "and unmasked once the caller opts out");

    options.maskIdentifiers = true;
    UltraCanvasHardwareInfo::SetOptions(options);
}

int main() {
    std::printf("=== UltraCanvasHardwareInfo tests ===\n");
    TestFormatters();
    TestMasking();
    TestQueryFlags();
    TestCacheHelpers();
    TestReportShape();
    TestLiveCapture();
    TestMaskingAppliesToCapture();
    std::printf("=== %s ===\n", g_failures == 0 ? "all tests passed" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}

// Tests/NetworkPlacesTest.cpp
// Unit tests for the platform-independent half of UltraCanvasNetworkPlaces:
// which paths name a computer and nothing below it, and the empty answers of
// a build without a backend. Built from the core file alone, with no define,
// so every CI row runs the same checks; the Windows backend needs a network
// and is not exercised here.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasNetworkPlaces.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    ++checks; \
    if (cond) { \
        std::printf("  PASS  %s\n", what); \
    } else { \
        ++failures; \
        std::printf("  FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__); \
    } \
} while (0)

int main() {
    std::printf("── UltraCanvasNetworkPlaces (core, no backend) ──\n");

    // ===== A COMPUTER, AND NOTHING BELOW IT =====
    CHECK(IsNetworkServerPath("\\\\NAS"), "\\\\NAS names a computer");
    CHECK(IsNetworkServerPath("\\\\NAS\\"), "so does \\\\NAS\\");
    CHECK(IsNetworkServerPath("//nas"), "and //nas");
    CHECK(NetworkServerName("\\\\NAS\\") == "NAS", "its name is NAS");
    CHECK(!IsNetworkServerPath("\\\\NAS\\photos"), "a share is a folder, not a computer");
    CHECK(!IsNetworkServerPath("\\\\NAS\\photos\\2026"), "nor is a folder in it");
    CHECK(!IsNetworkServerPath("\\\\?\\C:\\"), "\\\\?\\ is Windows' own prefix");
    CHECK(!IsNetworkServerPath("\\\\.\\pipe"), "and so is \\\\.\\");
    CHECK(!IsNetworkServerPath("\\\\wsl$"), "\\\\wsl$ is the WSL root, not a computer");
    CHECK(!IsNetworkServerPath("\\\\WSL.localhost"), "and so is \\\\wsl.localhost, in any case");
    CHECK(!IsNetworkServerPath("C:\\Users"), "a local path is no computer");
    CHECK(!IsNetworkServerPath("\\\\"), "two separators alone name nothing");

    CHECK(NetworkServerPath("NAS") == "\\\\NAS", "a bare name becomes \\\\NAS");
    CHECK(NetworkServerPath("//NAS/") == "\\\\NAS", "//NAS/ becomes \\\\NAS");
    CHECK(NetworkServerPath("\\\\NAS\\photos").empty(), "a share gives no computer path");

    // ===== NO BACKEND =====
    CHECK(!NetworkPlacesAvailable(), "no backend: not available");
    CHECK(ListNetworkComputers().empty(), "and no computers");
    CHECK(ListWslDistributions().empty(), "and no distributions");
    std::vector<NetworkShare> shares{NetworkShare{}};
    std::string error;
    CHECK(!ListNetworkShares("\\\\NAS", shares, error) && !error.empty() && shares.empty(),
          "listing shares fails with a reason and leaves nothing behind");

    std::printf("%s: %d checks, %d failure(s)\n",
                failures ? "FAILURE" : "OK", checks, failures);
    return failures ? 1 : 0;
}

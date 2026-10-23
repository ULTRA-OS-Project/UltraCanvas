// Tests/UltraAuthenticatorBrandColorTests.cpp
// Unit tests for the brand colour behind a card's monogram
// (Apps/UltraAuthenticator/BrandColors.h): how a service's name is matched to
// a known brand, and that the letter on the circle stays readable on every
// colour in the table. Header-only, no UI.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/BrandColors.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::Authenticator;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static bool Is(const std::string& name, uint32_t rgb) {
    Color c;
    if (!FindBrandColor(name, c)) return false;
    return ((uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b) == rgb;
}

static bool Known(const std::string& name) {
    Color c;
    return FindBrandColor(name, c);
}

static void TestMatching() {
    std::printf("Names are matched without regard to case, spaces or punctuation\n");
    Check(Is("GitHub", 0x181717), "GitHub");
    Check(Is("github", 0x181717), "github, lower case");
    Check(Is("Git-Hub", 0x181717), "punctuation is ignored");
    Check(Is("Amazon Web Services", 0xFF9900), "spaces are ignored");
    Check(Is("1Password", 0x0572EC), "digits are kept");
    Check(Is("Crypto.com", 0x002D74), "a dot is ignored");

    std::printf("A longer name matches the brand it starts with, the longest key winning\n");
    Check(Is("Google Workspace", 0x4285F4), "Google Workspace is Google");
    Check(Is("ProtonMail", 0x6D4AFF), "ProtonMail");
    Check(Is("Proton Pass", 0x6D4AFF), "Proton Pass is Proton");
    Check(Is("Gmail", 0xEA4335), "Gmail has its own colour, not Google's");

    std::printf("Short keys match only exactly\n");
    Check(Is("X", 0x000000), "X is X");
    Check(!Known("Xero"), "Xero is not X");
    Check(Is("AWS", 0xFF9900), "AWS");
    Check(!Known("Awsome Corp"), "a name starting with aws is not AWS");
    Check(Is("Meta", 0x0866FF), "Meta");
    Check(!Known("MetaMask"), "MetaMask is not Meta");
    Check(!Known("Boxcryptor"), "Boxcryptor is not Box");

    std::printf("Unknown and empty names have no brand colour\n");
    Check(!Known("my-homelab-server"), "a private server");
    Check(!Known(""), "empty");
    Check(!Known(" -- "), "punctuation only");
    Check(!Known("\xE0\xB8\x81\xE0\xB8\xA3\xE0\xB8\xB8\xE0\xB8\x87"), "a Thai name");
}

static double Contrast(const Color& a, const Color& b) {
    auto channel = [](uint8_t v) {
        const double c = v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto lum = [&](const Color& c) {
        return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
    };
    double la = lum(a), lb = lum(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

static void TestInk() {
    std::printf("The letter is white on dark brands and dark on light ones\n");
    const Color white(255, 255, 255, 255);
    Check(MonogramInkFor(Color(0x18, 0x17, 0x17, 255)) == white, "white on GitHub");
    Check(!(MonogramInkFor(Color(0xFF, 0xFC, 0x00, 255)) == white), "dark on Snapchat yellow");
    Check(!(MonogramInkFor(Color(0xFF, 0x99, 0x00, 255)) == white), "dark on AWS orange");

    std::printf("Every brand in the table gets a letter at large-text contrast\n");
    for (const BrandColor& brand : kBrandColors) {
        const Color bg(static_cast<uint8_t>(brand.rgb >> 16),
                       static_cast<uint8_t>(brand.rgb >> 8),
                       static_cast<uint8_t>(brand.rgb), 255);
        Check(Contrast(bg, MonogramInkFor(bg)) >= 3.0,
              std::string("readable letter on ") + brand.key);
        Check(NormaliseBrandName(brand.key) == brand.key,
              std::string("key is normalised: ") + brand.key);
    }
}

int main() {
    std::printf("UltraAuthenticator brand colour tests\n\n");

    TestMatching();
    TestInk();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

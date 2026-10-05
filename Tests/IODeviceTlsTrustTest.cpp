// Tests/IODeviceTlsTrustTest.cpp
// The bookkeeping behind trusting a network device's self-signed certificate
// on first use (UltraCanvasIODeviceTlsTrust.h): which address a key is kept
// under, the file it is kept in, and forgetting one. The connection side -
// learning a key, refusing a changed one - needs a TLS server and is
// exercised against one by hand (see the pull request); this needs nothing.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;
int g_passed = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (condition) ++g_passed; else ++g_failures;
}

void CheckEqual(const std::string& got, const std::string& want, const std::string& what) {
    Check(got == want, what + (got == want ? "" : " (got '" + got + "', want '" + want + "')"));
}

const std::string kPinA = "sha256//HahUgByHuT/fFR9cVuwV0IN2nMvwBP8GiAPFXj2V01U=";
const std::string kPinB = "sha256//EXZWCU5rn8MYG8MMbem9Op0MlXkL0YcPAEjXh0kPAOM=";

void TestAddresses() {
    std::cout << "\n=== The address a device's key is kept under ===\n";
    CheckEqual(IODeviceTlsAddress("https://Scanner.local:8443/eSCL/ScannerCapabilities"),
               "scanner.local:8443", "host lower-cased, path dropped");
    CheckEqual(IODeviceTlsAddress("https://scanner.local/eSCL"), "scanner.local:443",
               "the default port written out");
    CheckEqual(IODeviceTlsAddress("HTTPS://printer.local.:631/ipp/print"), "printer.local:631",
               "scheme in capitals, root dot dropped");
    CheckEqual(IODeviceTlsAddress("https://user@10.0.0.5:443/"), "10.0.0.5:443",
               "a user name is not part of the address");
    CheckEqual(IODeviceTlsAddress("https://[FE80::1]:9443/eSCL"), "[fe80::1]:9443",
               "an IPv6 literal keeps its brackets");
    CheckEqual(IODeviceTlsAddress("https://[fe80::1]/"), "[fe80::1]:443",
               "  and gets the default port too");
    CheckEqual(IODeviceTlsAddress("https://host:0443?x"), "host:443",
               "a padded port is the same port");
    CheckEqual(IODeviceTlsAddress("scanner.local:8443"), "scanner.local:8443",
               "a host:port is accepted as it is");

    CheckEqual(IODeviceTlsAddress("http://scanner.local/eSCL"), "",
               "plain http has no certificate to keep");
    CheckEqual(IODeviceTlsAddress("ipps://printer.local/"), "",
               "only https: ipps:// is turned into https:// before it gets here");
    CheckEqual(IODeviceTlsAddress("scanner.local"), "", "a bare host names no port");
    CheckEqual(IODeviceTlsAddress("https://host:99999/"), "", "a port out of range");
    CheckEqual(IODeviceTlsAddress("https://host:12ab/"), "", "a port that is not a number");
    CheckEqual(IODeviceTlsAddress("https://:443/"), "", "no host");
    CheckEqual(IODeviceTlsAddress("https://[::1/"), "", "an unclosed IPv6 literal");
    CheckEqual(IODeviceTlsAddress("a:b:c"), "", "colons that are not host:port");
    CheckEqual(IODeviceTlsAddress(""), "", "nothing");
}

void TestTheFile() {
    std::cout << "\n=== DeviceCertificates.conf ===\n";
    const std::string text =
        "# a comment\n"
        "\n"
        "Scanner.Local:8443 = " + kPinA + "\r\n"
        "printer.local:631=" + kPinB + "\n"
        "broken line without equals\n"
        "other.local:443=md5//nope\n"
        "empty.local:443=sha256//\n"
        "http://not-a-tls-address=" + kPinA + "\n";
    const auto pins = ParseIODeviceTrustedCertificates(text);
    Check(pins.size() == 2, "two good lines read, the rest skipped");
    CheckEqual(pins.count("scanner.local:8443") ? pins.at("scanner.local:8443") : "", kPinA,
               "an address is normalised as it is read, CRLF and spaces aside");
    CheckEqual(pins.count("printer.local:631") ? pins.at("printer.local:631") : "", kPinB,
               "the second device");

    const auto again = ParseIODeviceTrustedCertificates(FormatIODeviceTrustedCertificates(pins));
    Check(again == pins, "what is written reads back the same");
    Check(ParseIODeviceTrustedCertificates(FormatIODeviceTrustedCertificates({})).empty(),
          "an empty list is only the explanatory comment");
}

void TestForgetting(const std::filesystem::path& file) {
    std::cout << "\n=== Forgetting a device's key ===\n";
    {
        std::ofstream out(file, std::ios::binary);
        out << FormatIODeviceTrustedCertificates(
            {{"scanner.local:8443", kPinA}, {"printer.local:631", kPinB}});
    }
    CheckEqual(PathToUtf8(IODeviceTrustedCertificatesFile()), PathToUtf8(file),
               "ULTRACANVAS_DEVICE_CERTIFICATES names the file");
    Check(IODeviceTrustedCertificates().size() == 2, "both devices are listed");

    Check(IODeviceForgetCertificate("https://SCANNER.local:8443/eSCL"),
          "forgotten by any URL of the device");
    const auto left = IODeviceTrustedCertificates();
    Check(left.size() == 1 && left.front().address == "printer.local:631",
          "  and only that device");
    Check(!IODeviceForgetCertificate("scanner.local:8443"), "forgetting it twice says so");
    Check(!IODeviceForgetCertificate("http://printer.local:631/"),
          "a plain-http URL names no kept key");
    Check(!std::filesystem::exists(PathToUtf8(file) + ".tmp"), "no temporary file left behind");
}

}  // namespace

int main() {
    std::cout << "Device certificate trust tests\n";
    std::cout << "==============================\n";

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "IODeviceTlsTrustTest.conf";
#if defined(_WIN32)
    _putenv_s("ULTRACANVAS_DEVICE_CERTIFICATES", PathToUtf8(file).c_str());
#else
    setenv("ULTRACANVAS_DEVICE_CERTIFICATES", PathToUtf8(file).c_str(), 1);
#endif

    TestAddresses();
    TestTheFile();
    TestForgetting(file);

    std::error_code ec;
    std::filesystem::remove(file, ec);

    std::cout << "\n" << g_passed << " passed, " << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}

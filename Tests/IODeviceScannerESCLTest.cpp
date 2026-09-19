// Tests/IODeviceScannerESCLTest.cpp
// eSCL: the protocol arithmetic and the capability document.
//
// eSCL is how a modern network scanner works with no driver installed, and
// the two things easiest to get wrong in it are both pure data: the units
// (eSCL measures in three-hundredths of an inch, this module in hundredths of
// a millimetre) and the XML, whose elements are namespace-prefixed with
// prefixes the vendor chooses. Neither needs a scanner to test, which is why
// they live in their own translation unit.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODeviceScannerESCLProtocol.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Mentions(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// Membership in a capability vector, tested directly.
template <typename T>
bool Has(const std::vector<T>& list, const T& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// A capabilities document in the shape a real scanner sends: a flatbed and a
// duplex feeder, three colour modes, four resolutions. Deliberately uses the
// `scan:`/`pwg:` prefixes an HP or Canon device sends.
const char* const kCapabilities = R"(<?xml version="1.0" encoding="UTF-8"?>
<scan:ScannerCapabilities xmlns:scan="http://schemas.hp.com/imaging/escl/2011/05/03"
                          xmlns:pwg="http://www.pwg.org/schemas/2010/12/sm">
  <pwg:Version>2.63</pwg:Version>
  <pwg:MakeAndModel>Acme MegaScan 42</pwg:MakeAndModel>
  <pwg:SerialNumber>SN-0001</pwg:SerialNumber>
  <scan:Platen><scan:PlatenInputCaps>
    <scan:MinWidth>16</scan:MinWidth><scan:MaxWidth>2550</scan:MaxWidth>
    <scan:MinHeight>16</scan:MinHeight><scan:MaxHeight>3508</scan:MaxHeight>
    <scan:SettingProfiles><scan:SettingProfile>
      <scan:ColorModes>
        <scan:ColorMode>BlackAndWhite1</scan:ColorMode>
        <scan:ColorMode>Grayscale8</scan:ColorMode>
        <scan:ColorMode>RGB24</scan:ColorMode>
      </scan:ColorModes>
      <scan:DocumentFormats>
        <pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>
        <scan:DocumentFormatExt>application/pdf</scan:DocumentFormatExt>
      </scan:DocumentFormats>
      <scan:SupportedResolutions><scan:DiscreteResolutions>
        <scan:DiscreteResolution><scan:XResolution>75</scan:XResolution><scan:YResolution>75</scan:YResolution></scan:DiscreteResolution>
        <scan:DiscreteResolution><scan:XResolution>150</scan:XResolution><scan:YResolution>150</scan:YResolution></scan:DiscreteResolution>
        <scan:DiscreteResolution><scan:XResolution>300</scan:XResolution><scan:YResolution>300</scan:YResolution></scan:DiscreteResolution>
        <scan:DiscreteResolution><scan:XResolution>600</scan:XResolution><scan:YResolution>600</scan:YResolution></scan:DiscreteResolution>
        <scan:DiscreteResolution><scan:XResolution>300</scan:XResolution><scan:YResolution>600</scan:YResolution></scan:DiscreteResolution>
      </scan:DiscreteResolutions></scan:SupportedResolutions>
    </scan:SettingProfile></scan:SettingProfiles>
  </scan:PlatenInputCaps></scan:Platen>
  <scan:Adf>
    <scan:AdfSimplexInputCaps>
      <scan:MaxWidth>2550</scan:MaxWidth><scan:MaxHeight>4200</scan:MaxHeight>
      <scan:SettingProfiles><scan:SettingProfile>
        <scan:ColorModes><scan:ColorMode>RGB24</scan:ColorMode></scan:ColorModes>
      </scan:SettingProfile></scan:SettingProfiles>
    </scan:AdfSimplexInputCaps>
    <scan:AdfDuplexInputCaps>
      <scan:MaxWidth>2550</scan:MaxWidth><scan:MaxHeight>4200</scan:MaxHeight>
      <scan:SettingProfiles><scan:SettingProfile>
        <scan:ColorModes><scan:ColorMode>RGB24</scan:ColorMode></scan:ColorModes>
      </scan:SettingProfile></scan:SettingProfiles>
    </scan:AdfDuplexInputCaps>
  </scan:Adf>
</scan:ScannerCapabilities>)";

// ===== UNITS =====

void TestTheUnitConversion() {
    std::cout << "\n-- Three-hundredths of an inch, to hundredths of a millimetre --\n";

    // 300 units is one inch, which is 25.4 mm.
    Check(EsclUnitsToHundredthsMM(300) == 2540, "300 units is an inch");
    Check(HundredthsMMToEsclUnits(2540) == 300, "and an inch is 300 units");

    // 2550 units is 8.5 inches - the width every letter-size scanner reports.
    Check(EsclUnitsToHundredthsMM(2550) == 21590, "2550 units is 8.5 inches");

    // A4 is 210 x 297 mm. Rounding has to survive the round trip, because a
    // scan area is derived from a paper size and handed straight back; two
    // truncations would leave A4 a millimetre short.
    const int a4Width = HundredthsMMToEsclUnits(21000);
    const int a4Height = HundredthsMMToEsclUnits(29700);
    Check(std::abs(EsclUnitsToHundredthsMM(a4Width) - 21000) <= 10,
          "A4's width survives the round trip to within a tenth of a millimetre");
    Check(std::abs(EsclUnitsToHundredthsMM(a4Height) - 29700) <= 10,
          "and so does its height");

    Check(EsclUnitsToHundredthsMM(0) == 0 && EsclUnitsToHundredthsMM(-5) == 0,
          "nothing measures as nothing");
}

// ===== COLOUR MODES =====

void TestTheColourModes() {
    std::cout << "\n-- eSCL's colour-mode names --\n";

    int bits = 0;
    Check(EsclColorModeFromName("RGB24", &bits) == ScanColorMode::Color && bits == 8,
          "RGB24 is colour at 8 bits per channel");
    Check(EsclColorModeFromName("Grayscale8", &bits) == ScanColorMode::Grayscale && bits == 8,
          "Grayscale8 is grey at 8");
    Check(EsclColorModeFromName("BlackAndWhite1", &bits) == ScanColorMode::Lineart && bits == 1,
          "BlackAndWhite1 is lineart at 1");
    Check(EsclColorModeFromName("RGB48", &bits) == ScanColorMode::Color && bits == 16,
          "RGB48 is colour at 16");

    // The name carries the depth, so the two come back together rather than
    // the caller having to infer one from the other.
    Check(EsclColorModeFromName("Sepia", &bits) == ScanColorMode::Unknown,
          "a mode eSCL has no word for is Unknown, not a guess");

    Check(EsclColorModeToName(ScanColorMode::Color) == "RGB24", "colour asks for RGB24");
    Check(EsclColorModeToName(ScanColorMode::Grayscale) == "Grayscale8", "grey asks for Grayscale8");
    Check(EsclColorModeToName(ScanColorMode::Unknown).empty(), "and Unknown asks for nothing");
}

// ===== CAPABILITIES =====

void TestReadingTheCapabilities() {
    std::cout << "\n-- Reading a ScannerCapabilities document --\n";

    EsclScannerDescription scanner;
    const IODeviceResult parsed = ParseEsclCapabilities(kCapabilities, scanner);
    Check(parsed.success, "the document parses");
    if (!parsed.success) {
        std::cout << "         " << parsed.message << "\n";
        return;
    }

    Check(scanner.makeAndModel == "Acme MegaScan 42", "the model is read");
    Check(scanner.serialNumber == "SN-0001", "and the serial number");

    Check(scanner.HasSource(ScanSource::Flatbed), "the flatbed is found");
    Check(scanner.HasSource(ScanSource::ADF), "the feeder too");
    Check(scanner.HasSource(ScanSource::ADFDuplex),
          "and the duplex feeder, which is its own source here rather than a flag");

    // Asserted against the vectors, not through Supports()/SupportsResolution().
    // Those answer "would this be accepted", and an empty list means the
    // backend did not enumerate - so they say yes to everything and an
    // assertion built on them cannot fail. That is not hypothetical: the
    // first version of the parser used Supports() to deduplicate, dropped
    // every entry, and these checks still passed.
    Check(Has(scanner.capabilities.colorModes, ScanColorMode::Color) &&
              Has(scanner.capabilities.colorModes, ScanColorMode::Grayscale) &&
              Has(scanner.capabilities.colorModes, ScanColorMode::Lineart),
          "all three colour modes are collected");
    Check(scanner.capabilities.colorModes.size() == 3,
          "each exactly once, though RGB24 appears in three profiles");

    Check(Has(scanner.capabilities.resolutions, 75) &&
              Has(scanner.capabilities.resolutions, 150) &&
              Has(scanner.capabilities.resolutions, 300) &&
              Has(scanner.capabilities.resolutions, 600),
          "the discrete resolutions are collected");

    // 300x600 is in the document. ScanConfiguration carries a single dpi, so
    // an asymmetric resolution cannot be asked for and is not offered.
    Check(scanner.capabilities.resolutions.size() == 4,
          "and an asymmetric 300x600 is left out rather than half-read");

    // The feeder is longer than the bed; the larger of the two is the
    // device's maximum.
    Check(scanner.capabilities.maxArea.HeightHundredthsMM() ==
              EsclUnitsToHundredthsMM(4200),
          "the largest source sets the maximum area");

    Check(scanner.documentFormats.size() == 2 &&
              scanner.documentFormats[0] == "image/jpeg",
          "both DocumentFormat and DocumentFormatExt are collected");

    // Preview is not something eSCL advertises. Unknown, not No - the
    // distinction this module keeps everywhere.
    Check(scanner.capabilities.supportsPreview == IOSupport::Unknown,
          "preview is Unknown, because eSCL never says");
}

void TestOtherVendorsPrefixes() {
    std::cout << "\n-- A vendor that prefixes differently --\n";

    // tinyxml2 does not strip namespace prefixes, so a lookup for
    // "scan:ColorMode" misses a scanner that writes "escl:ColorMode". Every
    // lookup matches the local name instead. This is the same document with
    // every prefix changed and the namespaces renamed to match.
    std::string other(kCapabilities);
    for (std::string::size_type at = 0; (at = other.find("scan:", at)) != std::string::npos;) {
        other.replace(at, 5, "escl:");
    }
    for (std::string::size_type at = 0; (at = other.find("pwg:", at)) != std::string::npos;) {
        other.replace(at, 4, "sm:");
    }

    EsclScannerDescription scanner;
    const IODeviceResult parsed = ParseEsclCapabilities(other, scanner);
    Check(parsed.success, "it still parses");
    Check(scanner.makeAndModel == "Acme MegaScan 42", "the model is still read");
    Check(scanner.HasSource(ScanSource::Flatbed) && scanner.HasSource(ScanSource::ADFDuplex),
          "and the sources are still found");
    Check(Has(scanner.capabilities.resolutions, 600), "and the resolutions");
}

void TestDocumentsThatAreNotCapabilities() {
    std::cout << "\n-- Documents that are not what we asked for --\n";

    EsclScannerDescription scanner;
    Check(!ParseEsclCapabilities("", scanner).success, "nothing at all fails");
    Check(!ParseEsclCapabilities("<not xml", scanner).success, "malformed XML fails");

    // A device at the same address that is not a scanner: a printer's IPP
    // endpoint, a web page. Refused by name rather than half-read.
    const IODeviceResult wrong =
        ParseEsclCapabilities("<?xml version=\"1.0\"?><html><body>Hello</body></html>", scanner);
    Check(!wrong.success, "a document that is not ScannerCapabilities fails");
    Check(Mentions(wrong.message, "eSCL"), "and says so in a way that names the problem");

    // A well-formed capabilities document that offers no way to scan is not
    // a usable scanner.
    const IODeviceResult empty = ParseEsclCapabilities(
        "<?xml version=\"1.0\"?><scan:ScannerCapabilities "
        "xmlns:scan=\"x\"><pwg:Version xmlns:pwg=\"y\">2.63</pwg:Version>"
        "</scan:ScannerCapabilities>", scanner);
    Check(!empty.success, "a scanner with no input source is refused");
}

// ===== SCAN SETTINGS =====

void TestBuildingTheScanSettings() {
    std::cout << "\n-- Asking for a scan --\n";

    ScanConfiguration config;
    config.resolutionDpi = 300;
    config.colorMode = ScanColorMode::Color;
    config.source = ScanSource::Flatbed;

    const std::string flatbed = BuildEsclScanSettings(config, "image/jpeg");
    Check(Mentions(flatbed, "<pwg:InputSource>Platen</pwg:InputSource>"),
          "a flatbed scan asks for the Platen");
    Check(Mentions(flatbed, "<scan:ColorMode>RGB24</scan:ColorMode>"), "in colour");
    Check(Mentions(flatbed, "<scan:XResolution>300</scan:XResolution>"), "at 300 dpi");
    Check(Mentions(flatbed, "image/jpeg"), "and names the format it wants back");
    Check(!Mentions(flatbed, "ScanRegion"),
          "with no region, because none was asked for - the scanner uses its own default");
    Check(!Mentions(flatbed, "Duplex"),
          "and no duplex flag, which a flatbed has no meaning for");

    config.source = ScanSource::ADFDuplex;
    const std::string duplex = BuildEsclScanSettings(config, "image/jpeg");
    Check(Mentions(duplex, "<pwg:InputSource>Feeder</pwg:InputSource>"),
          "a duplex run asks for the Feeder");
    Check(Mentions(duplex, "<scan:Duplex>true</scan:Duplex>"),
          "with duplex on - eSCL splits into a source and a flag where this module has one source");

    config.source = ScanSource::ADF;
    Check(Mentions(BuildEsclScanSettings(config, "image/jpeg"),
                   "<scan:Duplex>false</scan:Duplex>"),
          "and a simplex run says so rather than leaving it out");

    // A4 at the origin, in the units eSCL wants.
    config.source = ScanSource::Flatbed;
    config.area = ScanArea::FromPaperSize(21000, 29700);
    const std::string region = BuildEsclScanSettings(config, "image/jpeg");
    Check(Mentions(region, "<pwg:Width>2480</pwg:Width>"),
          "an A4 region is converted into three-hundredths of an inch");
    Check(Mentions(region, "ThreeHundredthsOfInches"),
          "and says which units it used, as the protocol requires");
}

// ===== URLS =====

void TestFindingTheJob() {
    std::cout << "\n-- Where the scanner put the job --\n";

    const std::string base = "http://scanner.local/eSCL";

    // Firmware differs about what it puts in Location, and all three of these
    // are seen in the wild.
    Check(ResolveEsclJobUrl(base, "http://scanner.local/eSCL/ScanJobs/1") ==
              "http://scanner.local/eSCL/ScanJobs/1",
          "an absolute URL is used as given");
    Check(ResolveEsclJobUrl(base, "/eSCL/ScanJobs/1") ==
              "http://scanner.local/eSCL/ScanJobs/1",
          "an absolute path keeps the scheme and host");
    Check(ResolveEsclJobUrl(base, "ScanJobs/1") ==
              "http://scanner.local/eSCL/ScanJobs/1",
          "and a relative path hangs off the base");

    Check(ResolveEsclJobUrl(base + "/", "ScanJobs/1") ==
              "http://scanner.local/eSCL/ScanJobs/1",
          "a trailing slash on the base does not double up");

    // No Location means no job to collect, and pretending otherwise would
    // send the next request to the wrong place.
    Check(ResolveEsclJobUrl(base, "").empty(), "no location is no job");
}

void TestTheAddressFromDiscovery() {
    std::cout << "\n-- Building the address from an mDNS record --\n";

    const std::vector<std::string> txt = {"txtvers=1", "rs=eSCL", "ty=Acme MegaScan 42",
                                          "uuid=1234", "cs=color,grayscale"};

    Check(EsclBaseUrlFromMdns("scanner.local", 80, txt, false) ==
              "http://scanner.local/eSCL",
          "the default port is left off, as a person would write it");
    Check(EsclBaseUrlFromMdns("scanner.local", 8080, txt, false) ==
              "http://scanner.local:8080/eSCL",
          "and any other port is kept");
    Check(EsclBaseUrlFromMdns("scanner.local", 443, txt, true) ==
              "https://scanner.local/eSCL",
          "TLS gets its own scheme and default port");

    // `rs` is almost always "eSCL", but the protocol does not require it, so
    // assuming would strand a scanner that chose otherwise.
    const std::vector<std::string> odd = {"rs=Scan/Resource"};
    Check(EsclBaseUrlFromMdns("scanner.local", 80, odd, false) ==
              "http://scanner.local/Scan/Resource",
          "a scanner that uses a different resource path is followed");
    Check(EsclBaseUrlFromMdns("scanner.local", 80, {}, false) ==
              "http://scanner.local/eSCL",
          "and one that says nothing gets the usual default");

    Check(EsclTxtValue(txt, "ty") == "Acme MegaScan 42", "a TXT value is read by key");
    Check(EsclTxtValue(txt, "TY") == "Acme MegaScan 42",
          "case-insensitively, as DNS-SD requires");
    Check(EsclTxtValue(txt, "nope").empty(), "and a missing key reads as empty");

    Check(EsclBaseUrlFromMdns("", 80, txt, false).empty(), "no host is no address");
    Check(EsclBaseUrlFromMdns("scanner.local", 0, txt, false).empty(), "and neither is no port");
}

}  // namespace

int main() {
    std::cout << "eSCL protocol tests\n";
    std::cout << "===================\n";

    TestTheUnitConversion();
    TestTheColourModes();
    TestReadingTheCapabilities();
    TestOtherVendorsPrefixes();
    TestDocumentsThatAreNotCapabilities();
    TestBuildingTheScanSettings();
    TestFindingTheJob();
    TestTheAddressFromDiscovery();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All eSCL protocol tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " eSCL protocol test(s) FAILED.\n";
    return 1;
}

// core/IODeviceManager/UltraCanvasIODeviceScannerESCLProtocol.cpp
// Reading and writing eSCL's XML, and the unit arithmetic around it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODeviceScannerESCLProtocol.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace UltraCanvas {

namespace {

// ============================================================================
// NAMESPACE-TOLERANT XML
// ============================================================================

// eSCL documents are namespace-prefixed and vendors choose their own
// prefixes: `scan:ColorMode` from one scanner is `escl:ColorMode` from
// another, and the PWG-derived elements carry `pwg:`. tinyxml2 hands back the
// name exactly as written, so every lookup here compares the part after the
// last colon.
std::string LocalName(const char* qualified) {
    if (!qualified) return std::string();
    const std::string name(qualified);
    const size_t colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

const tinyxml2::XMLElement* FirstChild(const tinyxml2::XMLElement* parent,
                                       const char* localName) {
    if (!parent) return nullptr;
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        if (LocalName(child->Name()) == localName) return child;
    }
    return nullptr;
}

const tinyxml2::XMLElement* NextSibling(const tinyxml2::XMLElement* node,
                                        const char* localName) {
    for (const tinyxml2::XMLElement* next = node ? node->NextSiblingElement() : nullptr;
         next; next = next->NextSiblingElement()) {
        if (LocalName(next->Name()) == localName) return next;
    }
    return nullptr;
}

std::string ChildText(const tinyxml2::XMLElement* parent, const char* localName) {
    const tinyxml2::XMLElement* child = FirstChild(parent, localName);
    const char* text = child ? child->GetText() : nullptr;
    return text ? std::string(text) : std::string();
}

int ChildInt(const tinyxml2::XMLElement* parent, const char* localName,
             int fallback = 0) {
    const std::string text = ChildText(parent, localName);
    if (text.empty()) return fallback;
    try {
        return std::stoi(text);
    } catch (...) {
        // A scanner that writes something non-numeric here is not worth
        // failing the whole document over; the field simply goes unread.
        return fallback;
    }
}

std::string Lower(const std::string& text) {
    std::string folded = text;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return folded;
}

// ============================================================================
// ONE INPUT SOURCE
// ============================================================================

// Reads a *InputCaps element - PlatenInputCaps, AdfSimplexInputCaps,
// AdfDuplexInputCaps - which all share a shape: a size range plus a list of
// setting profiles naming colour modes, formats and resolutions.
void ReadInputCaps(const tinyxml2::XMLElement* caps, ScanSource source,
                   EsclScannerDescription& out) {
    if (!caps) return;

    if (std::find(out.capabilities.sources.begin(),
                  out.capabilities.sources.end(),
                  source) == out.capabilities.sources.end()) {
        out.capabilities.sources.push_back(source);
    }

    const int maxWidth = ChildInt(caps, "MaxWidth");
    const int maxHeight = ChildInt(caps, "MaxHeight");
    if (maxWidth > 0 && maxHeight > 0) {
        const ScanArea area = ScanArea::FromPaperSize(
            EsclUnitsToHundredthsMM(maxWidth), EsclUnitsToHundredthsMM(maxHeight));
        // The largest bed of any source is what the device can do; a flatbed
        // and a feeder routinely differ in length.
        if (area.WidthHundredthsMM() > out.capabilities.maxArea.WidthHundredthsMM() ||
            area.HeightHundredthsMM() > out.capabilities.maxArea.HeightHundredthsMM()) {
            out.capabilities.maxArea = area;
        }
    }

    const tinyxml2::XMLElement* profiles = FirstChild(caps, "SettingProfiles");
    for (const tinyxml2::XMLElement* profile = FirstChild(profiles, "SettingProfile");
         profile; profile = NextSibling(profile, "SettingProfile")) {

        const tinyxml2::XMLElement* modes = FirstChild(profile, "ColorModes");
        for (const tinyxml2::XMLElement* mode = FirstChild(modes, "ColorMode");
             mode; mode = NextSibling(mode, "ColorMode")) {
            const char* text = mode->GetText();
            if (!text) continue;
            int bits = 0;
            const ScanColorMode parsed = EsclColorModeFromName(text, &bits);
            if (parsed == ScanColorMode::Unknown) continue;
            // std::find on the vector, NOT capabilities.Supports(): that
            // answers "would this be accepted", and an empty list means the
            // backend has not enumerated yet, so it says yes to everything.
            // Using it to test membership while filling the list means the
            // first entry is always judged already-present and dropped - and
            // then the list never becomes non-empty, so every entry is.
            if (std::find(out.capabilities.colorModes.begin(),
                          out.capabilities.colorModes.end(),
                          parsed) == out.capabilities.colorModes.end()) {
                out.capabilities.colorModes.push_back(parsed);
            }
            if (bits > 0 &&
                std::find(out.capabilities.bitDepths.begin(),
                          out.capabilities.bitDepths.end(),
                          bits) == out.capabilities.bitDepths.end()) {
                out.capabilities.bitDepths.push_back(bits);
            }
        }

        const tinyxml2::XMLElement* formats = FirstChild(profile, "DocumentFormats");
        for (const tinyxml2::XMLElement* format = formats ? formats->FirstChildElement() : nullptr;
             format; format = format->NextSiblingElement()) {
            // Both DocumentFormat and DocumentFormatExt appear, often with
            // the same value; the local name covers either.
            const std::string local = LocalName(format->Name());
            if (local != "DocumentFormat" && local != "DocumentFormatExt") continue;
            const char* text = format->GetText();
            if (!text) continue;
            const std::string value(text);
            if (std::find(out.documentFormats.begin(), out.documentFormats.end(),
                          value) == out.documentFormats.end()) {
                out.documentFormats.push_back(value);
            }
        }

        const tinyxml2::XMLElement* resolutions =
            FirstChild(FirstChild(profile, "SupportedResolutions"), "DiscreteResolutions");
        for (const tinyxml2::XMLElement* entry = FirstChild(resolutions, "DiscreteResolution");
             entry; entry = NextSibling(entry, "DiscreteResolution")) {
            const int x = ChildInt(entry, "XResolution");
            const int y = ChildInt(entry, "YResolution");
            // Only square resolutions are offered: ScanConfiguration carries
            // one dpi, and a scanner asked for 300x600 through a field that
            // holds one number would be guesswork.
            if (x <= 0 || (y > 0 && y != x)) continue;
            if (std::find(out.capabilities.resolutions.begin(),
                          out.capabilities.resolutions.end(),
                          x) == out.capabilities.resolutions.end()) {
                out.capabilities.resolutions.push_back(x);
            }
        }
    }
}

}  // namespace

// ============================================================================
// UNITS
// ============================================================================

int EsclUnitsToHundredthsMM(int units) {
    if (units <= 0) return 0;
    return static_cast<int>((static_cast<long long>(units) * 2540 + 150) / 300);
}

int HundredthsMMToEsclUnits(int hundredthsMM) {
    if (hundredthsMM <= 0) return 0;
    return static_cast<int>((static_cast<long long>(hundredthsMM) * 300 + 1270) / 2540);
}

// ============================================================================
// COLOUR MODES
// ============================================================================

ScanColorMode EsclColorModeFromName(const std::string& name, int* outBitsPerSample) {
    const std::string folded = Lower(name);
    int bits = 0;
    ScanColorMode mode = ScanColorMode::Unknown;

    // eSCL spells the depth into the name, so both come from one string.
    if (folded == "blackandwhite1") {
        mode = ScanColorMode::Lineart;  bits = 1;
    } else if (folded == "grayscale8") {
        mode = ScanColorMode::Grayscale; bits = 8;
    } else if (folded == "grayscale16") {
        mode = ScanColorMode::Grayscale; bits = 16;
    } else if (folded == "rgb24") {
        mode = ScanColorMode::Color;    bits = 8;
    } else if (folded == "rgb48") {
        mode = ScanColorMode::Color;    bits = 16;
    }

    if (outBitsPerSample) *outBitsPerSample = bits;
    return mode;
}

std::string EsclColorModeToName(ScanColorMode mode) {
    switch (mode) {
        case ScanColorMode::Lineart:   return "BlackAndWhite1";
        case ScanColorMode::Halftone:  return "BlackAndWhite1";  // 1-bit either way
        case ScanColorMode::Grayscale: return "Grayscale8";
        case ScanColorMode::Color:     return "RGB24";
        case ScanColorMode::Unknown:
        default:                       return std::string();
    }
}

bool EsclScannerDescription::HasSource(ScanSource source) const {
    return capabilities.Supports(source);
}

// ============================================================================
// CAPABILITIES
// ============================================================================

IODeviceResult ParseEsclCapabilities(const std::string& xml,
                                     EsclScannerDescription& outDescription) {
    if (xml.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The scanner returned no capabilities document");
    }

    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            std::string("The scanner's capabilities are not valid XML: ") +
                (document.ErrorStr() ? document.ErrorStr() : "parse failed"));
    }

    const tinyxml2::XMLElement* root = document.RootElement();
    if (!root || LocalName(root->Name()) != "ScannerCapabilities") {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "The document is not a ScannerCapabilities; this may not be an "
            "eSCL scanner");
    }

    outDescription = EsclScannerDescription();
    outDescription.version = ChildText(root, "Version");
    outDescription.makeAndModel = ChildText(root, "MakeAndModel");
    outDescription.serialNumber = ChildText(root, "SerialNumber");

    ReadInputCaps(FirstChild(FirstChild(root, "Platen"), "PlatenInputCaps"),
                  ScanSource::Flatbed, outDescription);

    const tinyxml2::XMLElement* adf = FirstChild(root, "Adf");
    ReadInputCaps(FirstChild(adf, "AdfSimplexInputCaps"), ScanSource::ADF,
                  outDescription);
    ReadInputCaps(FirstChild(adf, "AdfDuplexInputCaps"), ScanSource::ADFDuplex,
                  outDescription);

    if (outDescription.capabilities.sources.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "The scanner listed no input source it can scan from");
    }

    std::sort(outDescription.capabilities.resolutions.begin(),
              outDescription.capabilities.resolutions.end());
    std::sort(outDescription.capabilities.bitDepths.begin(),
              outDescription.capabilities.bitDepths.end());

    // Preview is not something eSCL advertises, so it stays Unknown rather
    // than being reported as unsupported - the distinction this module keeps
    // everywhere between "did not say" and "said no".
    outDescription.capabilities.supportsPreview = IOSupport::Unknown;
    return IODeviceResult::Ok();
}

// ============================================================================
// SCAN SETTINGS
// ============================================================================

std::string BuildEsclScanSettings(const ScanConfiguration& configuration,
                                  const std::string& documentFormat) {
    const bool feeder = ScanSourceIsFeeder(configuration.source);
    const std::string colorMode = EsclColorModeToName(configuration.colorMode);

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<scan:ScanSettings"
        << " xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\""
        << " xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n"
        << "  <pwg:Version>2.63</pwg:Version>\n";

    // The region is sent only when one was asked for. Omitting it asks the
    // scanner for its own default, which beats guessing: a region larger than
    // the bed is rejected outright by some firmware.
    if (configuration.area.IsValid()) {
        xml << "  <pwg:ScanRegions>\n"
            << "    <pwg:ScanRegion>\n"
            << "      <pwg:XOffset>"
            << HundredthsMMToEsclUnits(configuration.area.leftHundredthsMM)
            << "</pwg:XOffset>\n"
            << "      <pwg:YOffset>"
            << HundredthsMMToEsclUnits(configuration.area.topHundredthsMM)
            << "</pwg:YOffset>\n"
            << "      <pwg:Width>"
            << HundredthsMMToEsclUnits(configuration.area.WidthHundredthsMM())
            << "</pwg:Width>\n"
            << "      <pwg:Height>"
            << HundredthsMMToEsclUnits(configuration.area.HeightHundredthsMM())
            << "</pwg:Height>\n"
            << "      <pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches"
               "</pwg:ContentRegionUnits>\n"
            << "    </pwg:ScanRegion>\n"
            << "  </pwg:ScanRegions>\n";
    }

    if (!documentFormat.empty()) {
        xml << "  <pwg:DocumentFormat>" << documentFormat << "</pwg:DocumentFormat>\n";
    }
    if (!colorMode.empty()) {
        xml << "  <scan:ColorMode>" << colorMode << "</scan:ColorMode>\n";
    }
    if (configuration.resolutionDpi > 0) {
        xml << "  <scan:XResolution>" << configuration.resolutionDpi
            << "</scan:XResolution>\n"
            << "  <scan:YResolution>" << configuration.resolutionDpi
            << "</scan:YResolution>\n";
    }

    // Duplex is a separate flag rather than a third source: eSCL knows
    // "Feeder" and a boolean, while this module spells the pair as
    // ScanSource::ADFDuplex.
    xml << "  <pwg:InputSource>" << (feeder ? "Feeder" : "Platen")
        << "</pwg:InputSource>\n";
    if (feeder) {
        xml << "  <scan:Duplex>"
            << (configuration.source == ScanSource::ADFDuplex ? "true" : "false")
            << "</scan:Duplex>\n";
    }

    xml << "</scan:ScanSettings>\n";
    return xml.str();
}

// ============================================================================
// URLS
// ============================================================================

std::string ResolveEsclJobUrl(const std::string& baseUrl,
                              const std::string& location) {
    if (location.empty()) return std::string();

    // Already absolute: the scanner named itself, and its own idea of its
    // address is better than ours - it may be behind a different name.
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0) {
        return location;
    }

    if (location.front() == '/') {
        // An absolute path: keep the scheme and authority, drop our path.
        const size_t schemeEnd = baseUrl.find("://");
        if (schemeEnd == std::string::npos) return std::string();
        const size_t authorityEnd = baseUrl.find('/', schemeEnd + 3);
        const std::string origin = authorityEnd == std::string::npos
                                       ? baseUrl
                                       : baseUrl.substr(0, authorityEnd);
        return origin + location;
    }

    std::string base = baseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/" + location;
}

std::string EsclTxtValue(const std::vector<std::string>& txtRecords,
                         const std::string& key) {
    const std::string wanted = Lower(key);
    for (const std::string& record : txtRecords) {
        const size_t equals = record.find('=');
        if (equals == std::string::npos) continue;
        if (Lower(record.substr(0, equals)) == wanted) {
            return record.substr(equals + 1);
        }
    }
    return std::string();
}

std::string EsclBaseUrlFromMdns(const std::string& host, int port,
                                const std::vector<std::string>& txtRecords,
                                bool useTls) {
    if (host.empty() || port <= 0) return std::string();

    // `rs` is the resource path. It is almost always "eSCL", but the protocol
    // does not require that and some firmware uses a different one, so it is
    // read rather than assumed.
    std::string resource = EsclTxtValue(txtRecords, "rs");
    if (resource.empty()) resource = "eSCL";
    while (!resource.empty() && resource.front() == '/') resource.erase(0, 1);
    while (!resource.empty() && resource.back() == '/') resource.pop_back();

    std::ostringstream url;
    url << (useTls ? "https://" : "http://") << host;

    // The default port for the scheme is left off, so the URL reads the way a
    // person would write it.
    const int defaultPort = useTls ? 443 : 80;
    if (port != defaultPort) url << ":" << port;

    url << "/" << resource;
    return url.str();
}

}  // namespace UltraCanvas

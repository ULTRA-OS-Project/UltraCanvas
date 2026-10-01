// Tests/IODevicePrinterIPPTest.cpp
// IPP driverless printing, everything short of the network: the message
// encoding in both directions, reading a printer's description, writing a
// job's attributes, deciding whether a document is sent as it is or drawn,
// and the PWG raster the drawn pages travel in.
//
// The PWG raster checks decode what the writer produced with a decoder
// written here from the specification (PWG 5102.4), not from the writer - a
// round trip through the writer's own idea of the format would pass however
// wrong that idea was.
// Version: 1.1.0
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterPwgRaster.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_passed = 0;
int g_failed = 0;

void Check(bool condition, const std::string& what) {
    if (condition) {
        ++g_passed;
        std::cout << "  [ OK ] " << what << "\n";
    } else {
        ++g_failed;
        std::cout << "  [FAIL] " << what << "\n";
    }
}

template <typename A, typename B>
void CheckEqual(const A& got, const B& want, const std::string& what) {
    const bool same = got == want;
    Check(same, what);
    if (!same) std::cout << "         got \"" << got << "\", wanted \"" << want << "\"\n";
}

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t at) {
    return (static_cast<uint32_t>(bytes[at]) << 24) | (static_cast<uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<uint32_t>(bytes[at + 2]) << 8) | static_cast<uint32_t>(bytes[at + 3]);
}

std::string ReadCString(const std::vector<uint8_t>& bytes, size_t at, size_t width) {
    std::string text;
    for (size_t i = at; i < at + width && bytes[i] != 0; ++i) text.push_back(static_cast<char>(bytes[i]));
    return text;
}

bool AllZero(const std::vector<uint8_t>& bytes, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

bool Has(const std::vector<IOPaperSize>& sizes, IOPaperSize size) {
    for (IOPaperSize s : sizes) {
        if (s == size) return true;
    }
    return false;
}

bool HasMedia(const std::vector<IOMediaType>& types, IOMediaType type) {
    for (IOMediaType t : types) {
        if (t == type) return true;
    }
    return false;
}

// ============================================================================
// PWG RASTER DECODER, FROM THE SPECIFICATION
// ============================================================================
//
// Per line: one byte, the number of times the line repeats minus one. Then
// runs until the line holds `width` pixels: a byte n; n <= 127 means the next
// pixel n+1 times, n >= 128 means 257-n pixels follow as they are.
bool DecodePwgLines(const std::vector<uint8_t>& data, size_t& at, int width, int height,
                    int bytesPerPixel, std::vector<uint8_t>& out) {
    const size_t lineBytes = static_cast<size_t>(width) * bytesPerPixel;
    int y = 0;
    while (y < height) {
        if (at >= data.size()) return false;
        const int repeats = data[at++] + 1;

        std::vector<uint8_t> line;
        while (line.size() < lineBytes) {
            if (at >= data.size()) return false;
            const int n = data[at++];
            if (n <= 127) {
                if (at + bytesPerPixel > data.size()) return false;
                for (int i = 0; i < n + 1; ++i) {
                    line.insert(line.end(), data.begin() + at, data.begin() + at + bytesPerPixel);
                }
                at += bytesPerPixel;
            } else {
                const size_t count = static_cast<size_t>(257 - n) * bytesPerPixel;
                if (at + count > data.size()) return false;
                line.insert(line.end(), data.begin() + at, data.begin() + at + count);
                at += count;
            }
        }
        if (line.size() != lineBytes) return false;     // a run overshot the line
        for (int r = 0; r < repeats; ++r) out.insert(out.end(), line.begin(), line.end());
        y += repeats;
    }
    return y == height;
}

// ============================================================================
// A PRINTER, AS IPPEVEPRINTER DESCRIBES ITSELF
// ============================================================================
//
// The values are the ones CUPS's reference IPP Everywhere printer
// (ippeveprinter, CUPS 2.4.7, run with -2) reports, so the mapping is tested
// against what a real printer says rather than what this file imagines one
// says.
IppGroup ReferencePrinter() {
    IppGroup printer;
    printer.tag = IppTag::PrinterGroup;
    auto keywords = [](std::vector<std::string> list) {
        std::vector<IppValue> values;
        for (const std::string& item : list) values.push_back(IppValue::Keyword(item));
        return values;
    };
    printer.Set("media-supported", keywords({"na_letter_8.5x11in", "na_legal_8.5x14in",
                                             "iso_a4_210x297mm", "na_number-10_4.125x9.5in",
                                             "iso_dl_110x220mm"}));
    printer.Set("media-type-supported",
                keywords({"auto", "cardstock", "envelope", "labels", "other", "stationery",
                          "stationery-letterhead", "transparency"}));
    printer.Set("color-supported", IppValue::Boolean(false));
    printer.Set("print-color-mode-supported", IppValue::Keyword("monochrome"));
    printer.Set("sides-supported",
                keywords({"one-sided", "two-sided-long-edge", "two-sided-short-edge"}));
    printer.Set("multiple-document-handling-supported",
                keywords({"separate-documents-uncollated-copies",
                          "separate-documents-collated-copies"}));
    printer.Set("copies-supported", IppValue::Range(1, 999));
    printer.Set("print-quality-supported",
                std::vector<IppValue>{IppValue::Enum(3), IppValue::Enum(4), IppValue::Enum(5)});
    printer.Set("page-ranges-supported", IppValue::Boolean(true));

    std::vector<IppValue> formats;
    for (const char* f : {"application/octet-stream", "application/pdf", "image/jpeg",
                          "image/pwg-raster"}) {
        formats.push_back(IppValue::MimeType(f));
    }
    printer.Set("document-format-supported", formats);
    printer.Set("pwg-raster-document-type-supported", keywords({"black_1", "sgray_8"}));
    printer.Set("pwg-raster-document-resolution-supported",
                std::vector<IppValue>{IppValue::Dpi(300, 300), IppValue::Dpi(600, 600)});
    printer.Set("pwg-raster-document-sheet-back", IppValue::Keyword("normal"));

    printer.Set("printer-state", IppValue::Enum(3));
    printer.Set("printer-state-reasons", keywords({"none"}));
    printer.Set("printer-is-accepting-jobs", IppValue::Boolean(true));
    printer.Set("queued-job-count", IppValue::Integer(0));

    std::vector<IppValue> supply;
    IppValue waste;
    waste.tag = IppTag::OctetString;
    waste.text = "index=1;class=receptacleThatIsFilled;type=wasteToner;unit=percent;"
                 "maxcapacity=100;level=25;colorantname=unknown;";
    IppValue toner;
    toner.tag = IppTag::OctetString;
    toner.text = "index=2;class=supplyThatIsConsumed;type=toner;unit=percent;"
                 "maxcapacity=100;level=100;colorantname=black;";
    supply.push_back(waste);
    supply.push_back(toner);
    printer.Set("printer-supply", supply);
    printer.Set("printer-supply-description",
                std::vector<IppValue>{IppValue::Text("Toner Waste Tank"),
                                      IppValue::Text("Black Toner")});
    return printer;
}

// ============================================================================
// ENCODING
// ============================================================================

void TestRequestBytes() {
    std::cout << "\n=== A request, byte for byte ===\n";

    // RFC 8010's shape: version, operation, request id, the operation group,
    // then end-of-attributes.
    IppMessage request =
        MakeIppRequest(IppOperation::GetPrinterAttributes, 7, "ipp://p/ipp/print", "");
    const std::vector<uint8_t> bytes = EncodeIppMessage(request);

    const std::vector<uint8_t> expected = {
        0x02, 0x00,             // IPP 2.0
        0x00, 0x0B,             // Get-Printer-Attributes
        0x00, 0x00, 0x00, 0x07, // request-id 7
        0x01,                   // operation-attributes-tag
        0x47, 0x00, 0x12, 'a', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's', '-',
        'c', 'h', 'a', 'r', 's', 'e', 't', 0x00, 0x05, 'u', 't', 'f', '-', '8',
        0x48, 0x00, 0x1B, 'a', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's', '-',
        'n', 'a', 't', 'u', 'r', 'a', 'l', '-', 'l', 'a', 'n', 'g', 'u', 'a', 'g', 'e',
        0x00, 0x02, 'e', 'n',
        0x45, 0x00, 0x0B, 'p', 'r', 'i', 'n', 't', 'e', 'r', '-', 'u', 'r', 'i',
        0x00, 0x11, 'i', 'p', 'p', ':', '/', '/', 'p', '/', 'i', 'p', 'p', '/',
        'p', 'r', 'i', 'n', 't',
        0x03                    // end-of-attributes-tag
    };
    Check(bytes == expected, "Get-Printer-Attributes encodes exactly as RFC 8010 lays it out");

    IppMessage withUser = MakeIppRequest(IppOperation::PrintJob, 1, "ipp://p/", "ann");
    const IppGroup& operation = withUser.groups.front();
    Check(operation.attributes.size() == 4 &&
              operation.attributes[0].name == "attributes-charset" &&
              operation.attributes[1].name == "attributes-natural-language" &&
              operation.attributes[2].name == "printer-uri" &&
              operation.attributes[3].name == "requesting-user-name",
          "the operation attributes come in the order RFC 8011 requires");
}

void TestRoundTrip() {
    std::cout << "\n=== Every value type survives a round trip ===\n";

    IppMessage message;
    message.code = 0x0001;
    message.requestId = 0x01020304;
    IppGroup& group = message.AddGroup(IppTag::PrinterGroup);
    group.Set("an-integer", IppValue::Integer(-42));
    group.Set("a-boolean", IppValue::Boolean(true));
    group.Set("an-enum", IppValue::Enum(5));
    group.Set("a-set", std::vector<IppValue>{IppValue::Keyword("one"), IppValue::Keyword("two"),
                                             IppValue::Keyword("three")});
    group.Set("a-range", IppValue::Range(1, 999));
    group.Set("a-resolution", IppValue::Dpi(600, 1200));
    IppValue withLanguage;
    withLanguage.tag = IppTag::TextWithLanguage;
    withLanguage.text = "Grüße";
    group.Set("a-text-with-language", withLanguage);
    group.Set("a-uri", IppValue::Uri("ipp://p/ipp/print"));
    group.Set("a-mime-type", IppValue::MimeType("image/pwg-raster"));

    std::vector<IppAttribute> inner;
    inner.emplace_back("x-dimension", IppValue::Integer(21000));
    inner.emplace_back("y-dimension", IppValue::Integer(29700));
    std::vector<IppAttribute> outer;
    outer.emplace_back("media-size", IppValue::Collection(inner));
    outer.emplace_back("media-type", IppValue::Keyword("stationery"));
    group.Set("a-collection", std::vector<IppValue>{IppValue::Collection(outer),
                                                    IppValue::Collection(outer)});
    group.Set("after-the-collection", IppValue::Keyword("still-read"));

    std::vector<uint8_t> bytes = EncodeIppMessage(message);
    const size_t messageSize = bytes.size();
    const std::vector<uint8_t> document = {'%', 'P', 'D', 'F'};
    bytes.insert(bytes.end(), document.begin(), document.end());

    IppMessage decoded;
    size_t consumed = 0;
    std::string error;
    Check(DecodeIppMessage(bytes.data(), bytes.size(), decoded, &consumed, &error),
          "the message decodes" + (error.empty() ? std::string() : " (" + error + ")"));
    CheckEqual(consumed, messageSize, "  and says where the document after it begins");
    CheckEqual(decoded.code, static_cast<uint16_t>(0x0001), "  status code");
    CheckEqual(decoded.requestId, static_cast<uint32_t>(0x01020304), "  request id");

    const IppGroup* printer = decoded.FindGroup(IppTag::PrinterGroup);
    Check(printer != nullptr, "  the printer group is there");
    if (!printer) return;

    CheckEqual(printer->Find("an-integer")->IntegerOr(0), -42, "  a negative integer");
    Check(printer->Find("a-boolean")->BooleanOr(false), "  a boolean");
    CheckEqual(printer->Find("an-enum")->IntegerOr(0), 5, "  an enum");
    CheckEqual(printer->Find("a-set")->values.size(), static_cast<size_t>(3),
               "  a 1setOf keeps all three values");
    Check(printer->Find("a-set")->Contains("TWO"), "  and matches a keyword ignoring case");
    const IppValue* range = printer->Find("a-range")->First();
    Check(range && range->integer == 1 && range->upper == 999, "  a rangeOfInteger");
    const IppValue* resolution = printer->Find("a-resolution")->First();
    Check(resolution && resolution->integer == 600 && resolution->upper == 1200 &&
              resolution->units == 3,
          "  a resolution, with its units");
    CheckEqual(printer->Find("a-text-with-language")->String(), std::string("Grüße"),
               "  textWithLanguage gives back its text, UTF-8 intact");
    CheckEqual(printer->Find("a-mime-type")->String(), std::string("image/pwg-raster"),
               "  a MIME type");

    const IppAttribute* collection = printer->Find("a-collection");
    Check(collection && collection->values.size() == 2, "  a 1setOf collection keeps both");
    if (collection && collection->values.size() == 2) {
        const IppAttribute* size = collection->Member("media-size", 1);
        const IppAttribute* x = size ? size->Member("x-dimension") : nullptr;
        Check(x && x->IntegerOr(0) == 21000, "  a collection inside a collection");
        const IppAttribute* type = collection->Member("media-type");
        Check(type && type->String() == "stationery", "  and a member after the nested one");
    }
    const IppAttribute* after = printer->Find("after-the-collection");
    Check(after && after->String() == "still-read",
          "  an attribute after a collection is read as its own, not as a member");
}

void TestMalformed() {
    std::cout << "\n=== Malformed replies are refused, not read past ===\n";

    IppMessage message = MakeIppRequest(IppOperation::GetJobs, 3, "ipp://p/", "someone");
    IppGroup& job = message.AddGroup(IppTag::JobGroup);
    std::vector<IppAttribute> members;
    members.emplace_back("media-type", IppValue::Keyword("stationery"));
    job.Set("media-col", IppValue::Collection(members));
    const std::vector<uint8_t> whole = EncodeIppMessage(message);

    bool everyCutRefused = true;
    for (size_t cut = 0; cut < whole.size(); ++cut) {
        IppMessage out;
        if (DecodeIppMessage(whole.data(), cut, out)) {
            everyCutRefused = false;
            std::cout << "         a message cut to " << cut << " bytes decoded\n";
        }
    }
    Check(everyCutRefused, "a message cut short anywhere is refused");

    std::vector<uint8_t> badInteger = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04,
                                       0x21, 0x00, 0x01, 'n', 0x00, 0x02, 0x00, 0x05, 0x03};
    IppMessage out;
    std::string error;
    Check(!DecodeIppMessage(badInteger.data(), badInteger.size(), out, nullptr, &error),
          "an integer two bytes long is refused");
    Check(!error.empty(), "  and the refusal says why: " + error);

    std::vector<uint8_t> noGroup = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1,
                                    0x44, 0x00, 0x01, 'k', 0x00, 0x01, 'v', 0x03};
    Check(!DecodeIppMessage(noGroup.data(), noGroup.size(), out),
          "a value before any group is refused");

    std::vector<uint8_t> orphan = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04,
                                   0x44, 0x00, 0x00, 0x00, 0x01, 'v', 0x03};
    Check(!DecodeIppMessage(orphan.data(), orphan.size(), out),
          "an additional value with no attribute before it is refused");

    std::vector<uint8_t> member = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04,
                                   0x4A, 0x00, 0x00, 0x00, 0x01, 'm', 0x03};
    Check(!DecodeIppMessage(member.data(), member.size(), out),
          "a collection member outside a collection is refused");

    // Collections nested a hundred deep, every one of them properly closed:
    // well-formed in every way but its depth, so the depth is the only thing
    // that can refuse it - before the stack is exhausted.
    std::vector<uint8_t> deep = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04};
    deep.insert(deep.end(), {0x34, 0x00, 0x01, 'c', 0x00, 0x00});
    for (int i = 0; i < 100; ++i) {
        deep.insert(deep.end(), {0x4A, 0x00, 0x00, 0x00, 0x01, 'm'});
        deep.insert(deep.end(), {0x34, 0x00, 0x00, 0x00, 0x00});
    }
    for (int i = 0; i < 101; ++i) deep.insert(deep.end(), {0x37, 0x00, 0x00, 0x00, 0x00});
    deep.push_back(0x03);
    error.clear();
    const bool deepDecoded = DecodeIppMessage(deep.data(), deep.size(), out, nullptr, &error);
    Check(!deepDecoded && error.find("nested too deeply") != std::string::npos,
          "collections nested a hundred deep are refused for their depth: " + error);

    // The same shape three deep is an ordinary message.
    std::vector<uint8_t> shallow = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04};
    shallow.insert(shallow.end(), {0x34, 0x00, 0x01, 'c', 0x00, 0x00});
    for (int i = 0; i < 3; ++i) {
        shallow.insert(shallow.end(), {0x4A, 0x00, 0x00, 0x00, 0x01, 'm'});
        shallow.insert(shallow.end(), {0x34, 0x00, 0x00, 0x00, 0x00});
    }
    for (int i = 0; i < 4; ++i) shallow.insert(shallow.end(), {0x37, 0x00, 0x00, 0x00, 0x00});
    shallow.push_back(0x03);
    Check(DecodeIppMessage(shallow.data(), shallow.size(), out),
          "  and the same shape three deep decodes, so it is the depth being refused");

    // A value type from a newer printer is kept, not refused.
    std::vector<uint8_t> newer = {0x02, 0x00, 0x00, 0x00, 0, 0, 0, 1, 0x04,
                                  0x39, 0x00, 0x01, 'n', 0x00, 0x02, 'a', 'b',
                                  0x44, 0x00, 0x01, 'k', 0x00, 0x01, 'v', 0x03};
    Check(DecodeIppMessage(newer.data(), newer.size(), out) &&
              out.Find(IppTag::PrinterGroup, "k") != nullptr,
          "a value tag this side does not know is kept, and reading goes on past it");
}

// ============================================================================
// STATUS CODES
// ============================================================================

void TestStatus() {
    std::cout << "\n=== Status codes ===\n";
    Check(IppStatusSucceeded(0x0000), "successful-ok is success");
    Check(IppStatusSucceeded(0x0001),
          "successful-ok-ignored-or-substituted-attributes is success too");
    Check(!IppStatusSucceeded(0x0400), "client-error-bad-request is not");
    CheckEqual(IppStatusToString(0x040A),
               std::string("client-error-document-format-not-supported"), "named by keyword");
    CheckEqual(IppStatusToString(0x0999), std::string("0x0999"), "an unnamed code as hex");
    Check(IppStatusToResultCode(0x0406) == IODeviceResultCode::DeviceNotFound,
          "not-found reads as DeviceNotFound");
    Check(IppStatusToResultCode(0x040A) == IODeviceResultCode::NotSupported,
          "document-format-not-supported reads as NotSupported");
    Check(IppStatusToResultCode(0x0507) == IODeviceResultCode::DeviceBusy,
          "busy reads as DeviceBusy");
    Check(IppStatusToResultCode(0x0402) == IODeviceResultCode::AccessDenied,
          "not-authenticated reads as AccessDenied");
    Check(IppStatusIsRetryable(0x0507) && IppStatusIsRetryable(0x0505),
          "busy and temporary-error mean 'not now', so a job is sent again");
    Check(!IppStatusIsRetryable(0x0506) && !IppStatusIsRetryable(0x040A),
          "  not-accepting-jobs and a format refusal mean 'no', so it is not");
}

// ============================================================================
// ADDRESSES
// ============================================================================

void TestAddresses() {
    std::cout << "\n=== Addresses ===\n";
    CheckEqual(IppHttpUrlFor("ipp://printer.local/ipp/print"),
               std::string("http://printer.local:631/ipp/print"),
               "ipp:// is POSTed to http on 631 when no port is given");
    CheckEqual(IppHttpUrlFor("ipps://printer.local:8443/ipp/print"),
               std::string("https://printer.local:8443/ipp/print"), "ipps:// to https");
    CheckEqual(IppHttpUrlFor("ipp://[fe80::1]:631/ipp/print"),
               std::string("http://[fe80::1]:631/ipp/print"), "an IPv6 literal keeps its brackets");
    CheckEqual(IppHttpUrlFor("http://10.0.0.5:631/ipp/print"),
               std::string("http://10.0.0.5:631/ipp/print"), "an http URL is left as it is");
    CheckEqual(IppHttpUrlFor("ftp://x/y"), std::string(), "anything else is refused");

    const std::vector<std::string> txt = {"txtvers=1", "rp=ipp/print", "UUID=ABC-123",
                                          "ty=Acme LaserJet", "pdl=application/pdf,image/pwg-raster"};
    CheckEqual(IppUriFromMdns("printer.local.", 631, txt, false),
               std::string("ipp://printer.local:631/ipp/print"),
               "a DNS-SD advertisement gives ipp://host:port/rp, root dot dropped");
    CheckEqual(IppUriFromMdns("printer.local", 443, txt, true),
               std::string("ipps://printer.local:443/ipp/print"), "and ipps:// for _ipps._tcp");
    CheckEqual(IppUriFromMdns("printer.local", 631, {"txtvers=1"}, false),
               std::string("ipp://printer.local:631/"),
               "no rp means the printer is at the root, as the Bonjour spec says");
    CheckEqual(IppUriFromMdns("fe80::1", 631, txt, false),
               std::string("ipp://[fe80::1]:631/ipp/print"), "a bare IPv6 host is bracketed");
    CheckEqual(IppUriFromMdns("printer.local", 0, txt, false), std::string(),
               "no port, no address");
    CheckEqual(IppTxtValue(txt, "uuid"), std::string("ABC-123"),
               "TXT keys match without regard to case");
    CheckEqual(IppTxtValue(txt, "nope"), std::string(), "an absent key is empty");

    CheckEqual(IppDeviceIdFor("ABC-123", "ipp://p/"), std::string("urn:uuid:abc-123"),
               "a known UUID becomes the device id, in printer-uuid's form");
    CheckEqual(IppDeviceIdFor("urn:uuid:ABC-123", "ipp://p/"), std::string("urn:uuid:abc-123"),
               "  whether or not it arrived with its prefix");
    CheckEqual(IppDeviceIdFor("", "ipp://p/ipp/print"), std::string("ipp:ipp://p/ipp/print"),
               "  and the address when there is none");

    CheckEqual(IppNormalizePrinterUri("  http://10.0.0.5/ipp/print/ "),
               std::string("ipp://10.0.0.5:80/ipp/print"),
               "a configured http URL becomes ipp://, keeping the port it implied");
    CheckEqual(IppNormalizePrinterUri("ipps://h:443/ipp/print"),
               std::string("ipps://h:443/ipp/print"), "an ipps URI is kept");
    CheckEqual(IppNormalizePrinterUri("ipp://h/ipp/print"), std::string("ipp://h/ipp/print"),
               "an ipp URI without a port is kept without one");
    CheckEqual(IppNormalizePrinterUri("smb://h/q"), std::string(), "an smb address is refused");
}

void CheckUris(const std::vector<std::string>& got, const std::vector<std::string>& want,
               const std::string& what) {
    const bool same = got == want;
    Check(same, what);
    if (!same) {
        std::cout << "         got";
        for (const std::string& uri : got) std::cout << " \"" << uri << "\"";
        std::cout << "\n";
    }
}

void TestWindowsPortAddresses() {
    std::cout << "\n=== Windows queue ports as IPP addresses ===\n";
    using Uris = std::vector<std::string>;
    const Uris guesses = {"ipp://10.0.0.5:631/ipp/print", "ipp://10.0.0.5:631/ipp",
                          "ipp://10.0.0.5:631/"};

    CheckUris(IppUrisForWindowsPort("http://10.0.0.5:631/ipp/print", ""),
               Uris{"ipp://10.0.0.5:631/ipp/print"}, "an IPP port is its own address");
    CheckUris(IppUrisForWindowsPort("IP_10.0.0.5", ""), guesses,
               "IP_<address> is guessed at /ipp/print, /ipp and the root, on 631");
    CheckUris(IppUrisForWindowsPort("10.0.0.5_1", ""), guesses,
               "  and so is <address>_<n>, the second port Windows makes for one host");
    CheckUris(IppUrisForWindowsPort("10.0.0.5", ""), guesses, "  and a bare address");
    CheckUris(IppUrisForWindowsPort("Office laser", "printer.example.com"),
               Uris{"ipp://printer.example.com:631/ipp/print",
                    "ipp://printer.example.com:631/ipp", "ipp://printer.example.com:631/"},
               "a configured host address wins over whatever the port is called");
    CheckUris(IppUrisForWindowsPort("IP_10.0.0.5", "10.0.0.9"),
               Uris{"ipp://10.0.0.9:631/ipp/print", "ipp://10.0.0.9:631/ipp",
                    "ipp://10.0.0.9:631/"},
               "  even when the name looks like an address");
    CheckEqual(IppUrisForWindowsPort("x", "fe80::1").front(),
               std::string("ipp://[fe80::1]:631/ipp/print"), "an IPv6 host is bracketed");
    CheckUris(IppUrisForWindowsPort("x", "bad host/path"), Uris{},
               "a host address that would bend the URI is ignored");

    for (const char* port : {"USB001", "LPT1:", "FILE:", "PORTPROMPT:", "nul:",
                             "WSD-6c3e2a1b-55d2-4b1f-9a9e-0a1b2c3d4e5f", "IP_10.0.0", "10.0.0.256",
                             "IP_printer", "TS001"}) {
        CheckUris(IppUrisForWindowsPort(port, ""), Uris{},
                   std::string("no address in '") + port + "'");
    }
}

void TestInstanceNames() {
    std::cout << "\n=== The instance, out of the name the mDNS plugin reports ===\n";
    // Every backend of the plugin reports the full service name; they differ
    // only in escaping. These are the three forms, as each one gives them.
    CheckEqual(IppInstanceFromServiceName("UC Test Printer._ipp._tcp.local"),
               std::string("UC Test Printer"), "Avahi's form: readable, type and domain cut");
    CheckEqual(IppInstanceFromServiceName("UC\\032Test\\032Printer._ipps._tcp.local."),
               std::string("UC Test Printer"), "Bonjour's form: escaped, root dot and all");
    CheckEqual(IppInstanceFromServiceName("Lab\\.Printer._ipp._tcp.local."),
               std::string("Lab.Printer"), "  an escaped dot in the instance is a dot");
    CheckEqual(IppInstanceFromServiceName("Lab.Printer._ipp._tcp.local"),
               std::string("Lab.Printer"), "a readable dot in the instance stays");
    CheckEqual(IppInstanceFromServiceName("Front Desk._IPP._TCP.local"),
               std::string("Front Desk"), "the service type in capitals");
    CheckEqual(IppInstanceFromServiceName("my._ipp._tcp printer._ipp._tcp.local"),
               std::string("my._ipp._tcp printer"),
               "  the last service type is the real one, whatever the instance contains");
    CheckEqual(IppInstanceFromServiceName("Just A Name"), std::string("Just A Name"),
               "a name with no service type is already the instance");
}

void TestCupsMatching() {
    std::cout << "\n=== Is this printer already a CUPS queue? ===\n";
    const std::string uuid = "08100DFC-2E86-3D01-41F2-FFD1D6E14E27";
    const std::string uri = "ipp://vm.local:8631/ipp/print";

    Check(IppCupsQueueReachesPrinter(
              "dnssd://UC%20Test%20Printer._ipp._tcp.local/?uuid=08100dfc-2e86-3d01-41f2-ffd1d6e14e27",
              "Someone Else", uuid, uri),
          "a dnssd:// queue carrying the printer's UUID is the same printer");
    Check(IppCupsQueueReachesPrinter("ipps://UC%20Test%20Printer._ipps._tcp.local/",
                                     "UC Test Printer", "", uri),
          "a queue CUPS discovered, by instance name - the form CUPS 2.4.7 gives");
    Check(IppCupsQueueReachesPrinter("ipps://UC%20Test%20Printer._ipps._tcp.local/",
                                     "UC\\032Test\\032Printer", "", uri),
          "  with the instance name as Bonjour escapes it");
    Check(IppCupsQueueReachesPrinter("ipps://UC%20Test%20Printer._ipps._tcp.local/",
                                     "UC Test Printer._ipp._tcp.local", "", uri),
          "  and given the full service name, as the mDNS plugin reports it on Linux");
    Check(IppCupsQueueReachesPrinter("ipps://UC%20Test%20Printer._ipps._tcp.local/",
                                     "UC\\032Test\\032Printer._ipp._tcp.local.", "", uri),
          "  and on macOS");
    Check(!IppCupsQueueReachesPrinter("ipps://Another%20One._ipps._tcp.local/",
                                      "UC Test Printer", "", uri),
          "  and not another printer's");
    Check(IppCupsQueueReachesPrinter("ipp://VM.local.:8631/ipp/print", "", "", uri),
          "a queue at the same address, host case and root dot aside");
    Check(IppCupsQueueReachesPrinter("ipp://printer.local:631/ipp/print", "", "",
                                     "ipp://printer.local/ipp/print"),
          "  with the default port written on one side only");
    Check(!IppCupsQueueReachesPrinter("ipp://vm.local:8631/ipp/other", "", "", uri),
          "  but not at another path on the same printer");
    Check(!IppCupsQueueReachesPrinter("usb://Acme/LaserJet?serial=1", "UC Test Printer",
                                      uuid, uri),
          "a USB queue never matches a network printer");
}

// ============================================================================
// WHAT A PRINTER SAYS ABOUT ITSELF
// ============================================================================

void TestMediaNames() {
    std::cout << "\n=== PWG media names carry their own size ===\n";
    IOPaperDimensions a4 = IppMediaDimensionsFromPwgName("iso_a4_210x297mm");
    Check(a4.widthHundredthsMM == 21000 && a4.heightHundredthsMM == 29700, "iso_a4_210x297mm");
    IOPaperDimensions letter = IppMediaDimensionsFromPwgName("na_letter_8.5x11in");
    Check(letter.widthHundredthsMM == 21590 && letter.heightHundredthsMM == 27940,
          "na_letter_8.5x11in, inches converted exactly");
    IOPaperDimensions env = IppMediaDimensionsFromPwgName("na_number-10_4.125x9.5in");
    Check(env.widthHundredthsMM == 10478 && env.heightHundredthsMM == 24130,
          "na_number-10_4.125x9.5in, three decimal places, rounded");
    Check(!IppMediaDimensionsFromPwgName("iso_a4").IsValid(), "a name with no size has none");
    Check(!IppMediaDimensionsFromPwgName("om_x_12,5x20mm").IsValid(),
          "a decimal comma is not a decimal point: refused, not misread");
}

void TestCapabilities() {
    std::cout << "\n=== Capabilities, from the reference printer ===\n";
    const IOPrinterCapabilities caps = IppCapabilitiesFromAttributes(ReferencePrinter());

    Check(Has(caps.paperSizes, IOPaperSize::A4) && Has(caps.paperSizes, IOPaperSize::Letter) &&
              Has(caps.paperSizes, IOPaperSize::Legal) &&
              Has(caps.paperSizes, IOPaperSize::Envelope10) &&
              Has(caps.paperSizes, IOPaperSize::EnvelopeDL),
          "every size in media-supported is recognised by its dimensions");
    CheckEqual(caps.paperSizes.size(), static_cast<size_t>(5), "  and nothing else is added");
    Check(caps.supportsColor == IOSupport::No,
          "a printer whose only colour mode is monochrome does not print colour");
    Check(caps.supportsDuplex == IOSupport::Yes, "sides-supported with two-sided: duplex");
    Check(caps.supportsCollate == IOSupport::Yes, "collated and uncollated copies: collation");
    CheckEqual(caps.maxCopies, 999, "copies-supported's upper bound is the most copies");
    CheckEqual(caps.qualities.size(), static_cast<size_t>(3), "three print qualities");
    Check(HasMedia(caps.mediaTypes, IOMediaType::CardStock) &&
              HasMedia(caps.mediaTypes, IOMediaType::Envelope) &&
              HasMedia(caps.mediaTypes, IOMediaType::Label) &&
              HasMedia(caps.mediaTypes, IOMediaType::Plain) &&
              HasMedia(caps.mediaTypes, IOMediaType::Letterhead) &&
              HasMedia(caps.mediaTypes, IOMediaType::Transparency),
          "media types with an equivalent here are listed");
    CheckEqual(caps.mediaTypes.size(), static_cast<size_t>(6),
               "  and 'auto' and 'other', which have none, are not");
    Check(caps.supportsBorderless == IOSupport::Unknown,
          "no margin lists reported: borderless is unknown, not no");

    IppGroup custom;
    custom.tag = IppTag::PrinterGroup;
    custom.Set("media-supported",
               std::vector<IppValue>{IppValue::Keyword("custom_min_76.2x127mm"),
                                     IppValue::Keyword("custom_max_215.9x355.6mm"),
                                     IppValue::Keyword("iso_a4_210x297mm")});
    custom.Set("media-bottom-margin-supported",
               std::vector<IppValue>{IppValue::Integer(0), IppValue::Integer(300)});
    custom.Set("media-top-margin-supported", IppValue::Integer(300));
    const IOPrinterCapabilities customCaps = IppCapabilitiesFromAttributes(custom);
    Check(customCaps.minCustomSize.widthHundredthsMM == 7620 &&
              customCaps.maxCustomSize.heightHundredthsMM == 35560,
          "custom_min_ and custom_max_ give the custom size range");
    CheckEqual(customCaps.paperSizes.size(), static_cast<size_t>(1),
               "  and are not taken for sizes of their own");
    Check(customCaps.supportsBorderless == IOSupport::No,
          "a margin list without zero on one edge: not borderless");
}

void TestStatusAndSupplies() {
    std::cout << "\n=== Status and supplies ===\n";
    const IOPrinterStatus status = IppStatusFromAttributes(ReferencePrinter());
    Check(status.state == IOPrinterState::Idle, "printer-state 3 is idle");
    CheckEqual(status.stateReason, std::string("none"), "state reasons");
    Check(status.acceptingJobs && status.IsReady(), "accepting jobs, so ready");
    CheckEqual(status.supplies.size(), static_cast<size_t>(2),
               "supplies from printer-supply, as IPP Everywhere reports them");
    if (status.supplies.size() == 2) {
        Check(status.supplies[0].type == IOSupplyType::WasteTank &&
                  status.supplies[0].percentRemaining == 25 &&
                  status.supplies[0].description == "Toner Waste Tank",
              "  a waste tank a quarter full, described");
        Check(status.supplies[1].type == IOSupplyType::Toner &&
                  status.supplies[1].color == IOSupplyColor::Black &&
                  status.supplies[1].percentRemaining == 100,
              "  black toner, full");
    }

    IppGroup markers;
    markers.tag = IppTag::PrinterGroup;
    markers.Set("marker-levels", std::vector<IppValue>{IppValue::Integer(50), IppValue::Integer(-3)});
    markers.Set("marker-names", std::vector<IppValue>{IppValue::Name("Black"), IppValue::Name("Cyan")});
    markers.Set("marker-types", std::vector<IppValue>{IppValue::Keyword("toner-cartridge"),
                                                      IppValue::Keyword("ink-cartridge")});
    markers.Set("marker-colors", std::vector<IppValue>{IppValue::Name("#000000"),
                                                       IppValue::Name("#00FFFF")});
    const std::vector<IOSupplyLevel> supplies = IppSuppliesFromAttributes(markers);
    Check(supplies.size() == 2 && supplies[0].type == IOSupplyType::Toner &&
              supplies[0].percentRemaining == 50 && supplies[0].color == IOSupplyColor::Black,
          "marker-* attributes, when present, are read the way CUPS relays them");
    Check(supplies.size() == 2 && supplies[1].type == IOSupplyType::Ink &&
              supplies[1].color == IOSupplyColor::Cyan && !supplies[1].IsKnown(),
          "  and a level of -3 ('some left') is unknown, not empty");

    IppGroup job;
    job.tag = IppTag::JobGroup;
    job.Set("job-id", IppValue::Integer(12));
    job.Set("job-state", IppValue::Enum(9));
    job.Set("job-state-reasons", std::vector<IppValue>{IppValue::Keyword("job-completed-successfully")});
    job.Set("job-name", IppValue::Name("Invoice"));
    job.Set("job-impressions-completed", IppValue::Integer(3));
    const IOPrintJobStatus jobStatus = IppJobStatusFromAttributes(job);
    Check(jobStatus.jobId == 12 && jobStatus.state == IOPrintJobState::Completed &&
              jobStatus.IsFinished() && jobStatus.jobName == "Invoice" &&
              jobStatus.pagesPrinted == 3 &&
              jobStatus.stateReason == "job-completed-successfully",
          "a job group: id, state 9 is completed, name, pages, reasons");
}

void TestDocumentSupport() {
    std::cout << "\n=== What the printer takes in ===\n";
    const IppDocumentSupport docs = IppDocumentSupportFromAttributes(ReferencePrinter());
    Check(docs.Accepts("application/pdf") && docs.Accepts("IMAGE/PWG-RASTER"),
          "document-format-supported, matched without regard to case");
    Check(!docs.Accepts("image/png"), "  and nothing it does not list");
    CheckEqual(docs.rasterResolutions.size(), static_cast<size_t>(2), "two raster resolutions");
    Check(docs.pageRanges, "page-ranges-supported");
    CheckEqual(docs.rasterSheetBack, std::string("normal"), "sheet-back");

    IppGroup metric;
    metric.tag = IppTag::PrinterGroup;
    IppValue dpcm = IppValue::Dpi(118, 118);
    dpcm.units = 4;
    metric.Set("pwg-raster-document-resolution-supported", dpcm);
    metric.Set("pwg-raster-document-sheet-back", IppValue::Keyword("Rotated"));
    const IppDocumentSupport metricDocs = IppDocumentSupportFromAttributes(metric);
    Check(metricDocs.rasterResolutions.size() == 1 &&
              metricDocs.rasterResolutions[0].dpiX == 300,
          "a resolution in dots per centimetre is converted: 118 dpcm is 300 dpi");
    CheckEqual(metricDocs.rasterSheetBack, std::string("rotated"), "a sheet-back keyword, lowered");
    Check(!metricDocs.pageRanges, "page ranges not said, so not assumed");

    std::cout << "\n=== Margins ===\n";
    // ippeveprinter's own lists: bottom 0,1168; sides 340,635; top 0,102.
    IppGroup margins;
    margins.tag = IppTag::PrinterGroup;
    margins.Set("media-bottom-margin-supported",
                std::vector<IppValue>{IppValue::Integer(0), IppValue::Integer(1168)});
    margins.Set("media-left-margin-supported",
                std::vector<IppValue>{IppValue::Integer(340), IppValue::Integer(635)});
    margins.Set("media-right-margin-supported",
                std::vector<IppValue>{IppValue::Integer(340), IppValue::Integer(635)});
    margins.Set("media-top-margin-supported",
                std::vector<IppValue>{IppValue::Integer(0), IppValue::Integer(102)});
    const IppDocumentSupport marginDocs = IppDocumentSupportFromAttributes(margins);
    Check(marginDocs.margins.leftHundredthsMM == 635 &&
              marginDocs.margins.rightHundredthsMM == 635 &&
              marginDocs.margins.topHundredthsMM == 102 &&
              marginDocs.margins.bottomHundredthsMM == 1168,
          "each edge's largest margin: the one every medium can print within");
    Check(!marginDocs.borderless, "zero on only two edges is not borderless");

    IOPageSetup page;
    IOPageMargins drawn = IppDrawingMargins(marginDocs, page);
    Check(drawn.leftHundredthsMM == 635 && drawn.bottomHundredthsMM == 1168,
          "a page with no margins of its own is drawn inside the printer's");
    page.margins = {2000, 0, 0, 0};
    drawn = IppDrawingMargins(marginDocs, page);
    Check(drawn.leftHundredthsMM == 2000 && drawn.rightHundredthsMM == 635,
          "  a wider margin asked for wins on its edge only");
    page = IOPageSetup();
    page.borderless = true;
    drawn = IppDrawingMargins(marginDocs, page);
    Check(drawn.leftHundredthsMM == 635,
          "  borderless on a printer that cannot print to every edge keeps its margins");

    for (const char* edge : {"media-bottom-margin-supported", "media-left-margin-supported",
                             "media-right-margin-supported", "media-top-margin-supported"}) {
        margins.Set(edge, std::vector<IppValue>{IppValue::Integer(0), IppValue::Integer(500)});
    }
    const IppDocumentSupport edgeToEdge = IppDocumentSupportFromAttributes(margins);
    Check(edgeToEdge.borderless, "zero on all four edges is borderless");
    drawn = IppDrawingMargins(edgeToEdge, page);
    Check(drawn.leftHundredthsMM == 0 && drawn.topHundredthsMM == 0 &&
              drawn.rightHundredthsMM == 0 && drawn.bottomHundredthsMM == 0,
          "  and a borderless page is drawn to the edge");

    IppGroup silent;
    silent.tag = IppTag::PrinterGroup;
    const IppDocumentSupport silentDocs = IppDocumentSupportFromAttributes(silent);
    Check(silentDocs.margins.topHundredthsMM == 1270 && silentDocs.margins.leftHundredthsMM == 635,
          "a printer that names no margins gets CUPS's default, not zero");
}

// ============================================================================
// WHAT A JOB CARRIES
// ============================================================================

void TestJobTemplate() {
    std::cout << "\n=== Job attributes ===\n";

    IppGroup plain;
    plain.tag = IppTag::JobGroup;
    AddIppJobTemplate(plain, IOPrintOptions::Default(), {}, false);
    Check(plain.Find("copies") && plain.Find("copies")->IntegerOr(0) == 1, "copies 1");
    CheckEqual(plain.Find("sides")->String(), std::string("one-sided"), "one-sided");
    CheckEqual(plain.Find("print-color-mode")->String(), std::string("auto"), "colour mode auto");
    CheckEqual(plain.Find("print-quality")->IntegerOr(0), 4, "normal quality is 4");
    CheckEqual(plain.Find("media")->String(), std::string("iso_a4_210x297mm"),
               "A4 as a media keyword");
    Check(!plain.Find("media-col") && !plain.Find("page-ranges") &&
              !plain.Find("orientation-requested") && !plain.Find("multiple-document-handling"),
          "  and nothing that was not asked for");

    IOPrintOptions options;
    options.copies = 3;
    options.collate = false;
    options.duplex = IODuplexMode::ShortEdge;
    options.colorMode = IOPrinterColorMode::Grayscale;
    options.page.orientation = IOPrintOrientation::Landscape;
    options.resolutionMode = IOResolutionMode::Custom;
    options.customResolution = {600, 600};

    IppGroup passed;
    passed.tag = IppTag::JobGroup;
    AddIppJobTemplate(passed, options, {9, 1, 2, 3, 5, 8, 9}, false);
    CheckEqual(passed.Find("multiple-document-handling")->String(),
               std::string("separate-documents-uncollated-copies"), "uncollated copies");
    CheckEqual(passed.Find("sides")->String(), std::string("two-sided-short-edge"),
               "short-edge binding");
    CheckEqual(passed.Find("print-color-mode")->String(), std::string("monochrome"),
               "greyscale is monochrome");
    CheckEqual(passed.Find("orientation-requested")->IntegerOr(0), 4,
               "landscape is orientation 4, for a document the printer renders");
    const IppAttribute* ranges = passed.Find("page-ranges");
    Check(ranges && ranges->values.size() == 3 && ranges->values[0].integer == 1 &&
              ranges->values[0].upper == 3 && ranges->values[1].integer == 5 &&
              ranges->values[1].upper == 5 && ranges->values[2].integer == 8 &&
              ranges->values[2].upper == 9,
          "pages 9,1,2,3,5,8,9 become the ranges 1-3, 5-5, 8-9");
    const IppAttribute* dpi = passed.Find("printer-resolution");
    Check(dpi && dpi->First()->integer == 600, "an explicit resolution is sent");

    IppGroup drawn;
    drawn.tag = IppTag::JobGroup;
    AddIppJobTemplate(drawn, options, {}, true);
    Check(!drawn.Find("orientation-requested") && !drawn.Find("printer-resolution"),
          "raster drawn here carries neither: they are already in the pixels");

    IOPrintOptions photo;
    photo.mediaType = IOMediaType::PhotoGlossy;
    IppGroup withType;
    withType.tag = IppTag::JobGroup;
    AddIppJobTemplate(withType, photo, {}, false);
    const IppAttribute* col = withType.Find("media-col");
    Check(col && !withType.Find("media"), "a media type needs media-col, and then no 'media'");
    if (col) {
        const IppAttribute* type = col->Member("media-type");
        const IppAttribute* size = col->Member("media-size");
        Check(type && type->String() == "photographic-glossy", "  photographic-glossy");
        Check(size && size->Member("x-dimension") &&
                  size->Member("x-dimension")->IntegerOr(0) == 21000,
              "  with the A4 size inside it");
    }

    IOPrintOptions custom;
    custom.page.paperSize = IOPaperSize::Custom;
    custom.page.customSize = {10000, 15000};
    IppGroup customGroup;
    customGroup.tag = IppTag::JobGroup;
    AddIppJobTemplate(customGroup, custom, {}, false);
    const IppAttribute* customCol = customGroup.Find("media-col");
    Check(customCol && customCol->Member("media-size") &&
              customCol->Member("media-size")->Member("y-dimension")->IntegerOr(0) == 15000,
          "a custom size goes as media-size, in hundredths of a millimetre");

    IOPrintOptions art;
    art.mediaType = IOMediaType::FineArt;
    IppGroup artGroup;
    artGroup.tag = IppTag::JobGroup;
    AddIppJobTemplate(artGroup, art, {}, false);
    Check(artGroup.Find("media") && !artGroup.Find("media-col"),
          "fine-art paper has no IPP keyword, so no near miss is sent");

    IOPrintOptions escape;
    escape.backendOptions["print-scaling"] = "fit";
    escape.backendOptions["copies"] = "2";
    escape.backendOptions["ipp-attribute-fidelity"] = "true";
    IppGroup escaped;
    escaped.tag = IppTag::JobGroup;
    AddIppJobTemplate(escaped, escape, {}, false);
    Check(escaped.Find("print-scaling")->First()->tag == IppTag::Keyword,
          "a backend option in words is a keyword");
    Check(escaped.Find("copies")->First()->tag == IppTag::Integer &&
              escaped.Find("copies")->IntegerOr(0) == 2,
          "  in digits an integer, replacing the derived one rather than doubling it");
    Check(escaped.Find("ipp-attribute-fidelity")->First()->tag == IppTag::Boolean,
          "  'true' a boolean");

    // And the whole group survives the wire, collections included.
    IppMessage request = MakeIppRequest(IppOperation::PrintJob, 1, "ipp://p/", "u");
    request.groups.push_back(withType);
    const std::vector<uint8_t> bytes = EncodeIppMessage(request);
    IppMessage decoded;
    Check(DecodeIppMessage(bytes.data(), bytes.size(), decoded) &&
              decoded.Find(IppTag::JobGroup, "media-col") &&
              decoded.Find(IppTag::JobGroup, "media-col")->Member("media-type")->String() ==
                  "photographic-glossy",
          "a job group with media-col encodes and decodes intact");
}

// ============================================================================
// AS IT IS, OR DRAWN
// ============================================================================

void TestDocumentPlan() {
    std::cout << "\n=== Sent as it is, or drawn here ===\n";

    IOPrintJob job;
    job.filePath = "/tmp/Report.PDF";
    CheckEqual(IppJobDocumentType(job), std::string("application/pdf"), "a .PDF file is a PDF");
    job.mimeType = "Application/PDF; version=1.7";
    CheckEqual(IppJobDocumentType(job), std::string("application/pdf"),
               "a declared type wins, lowered, parameters dropped");
    IOPrintJob unknown;
    unknown.filePath = "/tmp/a.b/report";
    CheckEqual(IppJobDocumentType(unknown), std::string(),
               "a dot in a folder name is not an extension");

    const IppDocumentSupport reference = IppDocumentSupportFromAttributes(ReferencePrinter());
    IppDocumentPlan plan;

    IODeviceResult r = PlanIppDocument("application/pdf", {}, reference, plan);
    Check(r.success && plan.passThrough && plan.documentFormat == "application/pdf" &&
              !plan.sendPageRange,
          "a PDF to a printer that renders PDF is sent as it is");

    r = PlanIppDocument("application/pdf", {2, 3}, reference, plan);
    Check(r.success && plan.passThrough && plan.sendPageRange,
          "  and a page range is left to the printer, which says it can");

    IppDocumentSupport noRanges = reference;
    noRanges.pageRanges = false;
    r = PlanIppDocument("application/pdf", {2}, noRanges, plan);
    Check(!r.success && r.code == IODeviceResultCode::NotSupported,
          "  a range to a printer that cannot select pages is refused, not ignored");

    r = PlanIppDocument("text/plain", {}, reference, plan);
    Check(r.success && !plan.passThrough && plan.documentFormat == "image/pwg-raster",
          "plain text is drawn here as PWG raster");
    IppDocumentSupport takesText = reference;
    takesText.documentFormats.push_back("text/plain");
    r = PlanIppDocument("text/plain", {1}, takesText, plan);
    Check(r.success && !plan.passThrough && !plan.sendPageRange,
          "  even to a printer that says it takes text: the page is the same everywhere");

    r = PlanIppDocument("image/jpeg", {1}, reference, plan);
    Check(r.success && plan.passThrough && !plan.sendPageRange,
          "a JPEG is sent as it is; page 1 of a one-page image is the image");
    r = PlanIppDocument("image/jpeg", {2}, reference, plan);
    Check(!r.success && r.code == IODeviceResultCode::InvalidArgument,
          "  and page 2 of it is not there");

    r = PlanIppDocument("image/png", {}, reference, plan);
    Check(r.success && !plan.passThrough, "a PNG the printer does not take is drawn");

    IppDocumentSupport pdfOnly;
    pdfOnly.documentFormats = {"application/pdf", "image/urf"};
    r = PlanIppDocument("image/png", {}, pdfOnly, plan);
    Check(!r.success && r.code == IODeviceResultCode::NotSupported &&
              r.message.find("application/pdf") != std::string::npos,
          "a printer taking no PWG raster refuses what must be drawn, naming what it takes");

    IppDocumentSupport onlyOneBit = reference;
    onlyOneBit.rasterTypes = {"black_1"};
    r = PlanIppDocument("text/plain", {}, onlyOneBit, plan);
    Check(!r.success, "PWG raster in 1-bit only is not a raster this side can draw");

    IppDocumentSupport noPdf = reference;
    noPdf.documentFormats = {"image/pwg-raster", "image/jpeg"};
    r = PlanIppDocument("application/pdf", {}, noPdf, plan);
    Check(!r.success && r.code == IODeviceResultCode::NotSupported,
          "a PDF to a printer that takes none is refused: nothing here paginates PDF yet");

    r = PlanIppDocument("", {}, reference, plan);
    Check(!r.success && r.code == IODeviceResultCode::InvalidArgument, "no type, no plan");
}

void TestRasterChoices() {
    std::cout << "\n=== Raster type and resolution ===\n";
    CheckEqual(ChoosePwgRasterType({"black_1", "sgray_8"}, IOPrinterColorMode::Color),
               std::string("sgray_8"), "a monochrome printer is drawn for in grey");
    CheckEqual(ChoosePwgRasterType({"srgb_8", "sgray_8"}, IOPrinterColorMode::Grayscale),
               std::string("sgray_8"), "greyscale asked for, grey given");
    CheckEqual(ChoosePwgRasterType({"sgray_8", "srgb_8"}, IOPrinterColorMode::Auto),
               std::string("srgb_8"), "colour where it can");
    CheckEqual(ChoosePwgRasterType({"srgb_8"}, IOPrinterColorMode::Monochrome),
               std::string("srgb_8"), "grey pixels in sRGB when that is all it takes");
    CheckEqual(ChoosePwgRasterType({"black_1", "cmyk_8"}, IOPrinterColorMode::Auto),
               std::string(), "neither: none");

    auto dpi = [](int x) { return IOResolution{x, x}; };
    const std::vector<IOResolution> common = {dpi(300), dpi(600)};
    CheckEqual(ChoosePwgRasterResolution(common, IOPrintQuality::Draft, {}).dpiX, 300, "draft: lowest");
    CheckEqual(ChoosePwgRasterResolution(common, IOPrintQuality::Normal, {}).dpiX, 300,
               "normal: nearest 300");
    CheckEqual(ChoosePwgRasterResolution(common, IOPrintQuality::High, {}).dpiX, 600,
               "high: highest");
    const std::vector<IOResolution> wide = {dpi(150), dpi(300), dpi(600), dpi(1200)};
    CheckEqual(ChoosePwgRasterResolution(wide, IOPrintQuality::Photo, {}).dpiX, 600,
               "photo: highest up to 600, not the 1200 that would take 400 MB a page");
    CheckEqual(ChoosePwgRasterResolution({dpi(1200)}, IOPrintQuality::High, {}).dpiX, 1200,
               "  unless nothing lower is offered");
    CheckEqual(ChoosePwgRasterResolution(wide, IOPrintQuality::Normal, dpi(1200)).dpiX, 1200,
               "an explicit resolution it supports is used as asked");
    CheckEqual(ChoosePwgRasterResolution(wide, IOPrintQuality::Normal, dpi(720)).dpiX, 300,
               "  one it does not falls back to the quality's choice");
    CheckEqual(ChoosePwgRasterResolution({dpi(400), dpi(200)}, IOPrintQuality::Normal, {}).dpiX,
               200, "a tie for nearest 300 goes to the cheaper page");
    Check(!ChoosePwgRasterResolution({}, IOPrintQuality::Normal, {}).IsValid(),
          "nothing offered, nothing chosen");
}

// ============================================================================
// PWG RASTER
// ============================================================================

void TestPwgHeader() {
    std::cout << "\n=== PWG raster header ===\n";

    std::vector<uint8_t> stream;
    AppendPwgRasterSync(stream);
    CheckEqual(std::string(stream.begin(), stream.end()), std::string("RaS2"),
               "the stream opens with RaS2: version 2, big-endian, compressed");

    IOPwgRasterPage page;
    page.widthPixels = 2480;
    page.heightPixels = 3508;
    page.dpiX = 300;
    page.dpiY = 300;
    page.pageWidthPoints = 595;
    page.pageHeightPoints = 842;
    page.pageSizeName = "iso_a4_210x297mm";
    page.colorSpace = IOPwgColorSpace::SRGB;
    page.duplex = true;
    page.tumble = true;
    page.crossFeedMirrored = true;
    page.totalPageCount = 7;
    page.printQuality = 5;

    std::vector<uint8_t> header;
    Check(AppendPwgRasterPageHeader(page, header), "a valid page writes a header");
    CheckEqual(header.size(), kPwgRasterHeaderBytes, "  of exactly 1796 bytes");
    if (header.size() != kPwgRasterHeaderBytes) return;

    CheckEqual(ReadCString(header, 0, 64), std::string("PwgRaster"), "  'PwgRaster' at 0");
    CheckEqual(ReadU32(header, 272), 1u, "  Duplex at 272");
    CheckEqual(ReadU32(header, 276), 300u, "  HWResolution at 276");
    CheckEqual(ReadU32(header, 280), 300u, "  and 280");
    Check(AllZero(header, 284, 300), "  ImagingBoundingBox reserved, zero");
    CheckEqual(ReadU32(header, 340), 1u, "  NumCopies 1 at 340: copies travel in the request");
    CheckEqual(ReadU32(header, 352), 595u, "  PageSize at 352");
    CheckEqual(ReadU32(header, 356), 842u, "  and 356");
    CheckEqual(ReadU32(header, 368), 1u, "  Tumble at 368");
    CheckEqual(ReadU32(header, 372), 2480u, "  Width at 372");
    CheckEqual(ReadU32(header, 376), 3508u, "  Height at 376");
    CheckEqual(ReadU32(header, 384), 8u, "  BitsPerColor at 384");
    CheckEqual(ReadU32(header, 388), 24u, "  BitsPerPixel at 388");
    CheckEqual(ReadU32(header, 392), 2480u * 3u, "  BytesPerLine at 392");
    CheckEqual(ReadU32(header, 396), 0u, "  ColorOrder chunky at 396");
    CheckEqual(ReadU32(header, 400), 19u, "  ColorSpace sRGB (19) at 400");
    CheckEqual(ReadU32(header, 420), 3u, "  NumColors at 420");
    CheckEqual(ReadU32(header, 452), 7u, "  TotalPageCount at 452");
    CheckEqual(ReadU32(header, 456), 0xFFFFFFFFu, "  CrossFeedTransform -1 at 456");
    CheckEqual(ReadU32(header, 460), 1u, "  FeedTransform 1 at 460");
    CheckEqual(ReadU32(header, 472), 2480u, "  ImageBoxRight at 472");
    CheckEqual(ReadU32(header, 476), 3508u, "  ImageBoxBottom at 476");
    CheckEqual(ReadU32(header, 484), 5u, "  PrintQuality at 484");
    Check(AllZero(header, 516, 1668), "  VendorData and the reserved field after it, zero");
    CheckEqual(ReadCString(header, 1732, 64), std::string("iso_a4_210x297mm"),
               "  PageSizeName at 1732");

    page.colorSpace = IOPwgColorSpace::SGray;
    header.clear();
    AppendPwgRasterPageHeader(page, header);
    CheckEqual(ReadU32(header, 400), 18u, "sGray is colour space 18");
    CheckEqual(ReadU32(header, 392), 2480u, "  one byte a pixel");

    IOPwgRasterPage bad = page;
    bad.widthPixels = 0;
    std::vector<uint8_t> none;
    Check(!AppendPwgRasterPageHeader(bad, none) && none.empty(),
          "an invalid page writes nothing at all");
}

void TestPwgCompression() {
    std::cout << "\n=== PWG raster lines, decoded from the specification ===\n";

    // A page with every shape of run: long repeats past 128, literal stretches
    // past 128, lone pixels between repeats, a line repeated past 256 times,
    // and a one-pixel-wide page.
    struct Case { int width; int height; int channels; const char* what; };
    for (const Case& c : {Case{300, 600, 3, "sRGB, 300 x 600"},
                          Case{257, 3, 1, "sGray, 257 wide"},
                          Case{1, 5, 3, "one pixel wide"},
                          Case{129, 2, 1, "129 wide: one past a literal run"}}) {
        std::vector<uint8_t> pixels(static_cast<size_t>(c.width) * c.height * c.channels);
        uint32_t seed = 12345;
        for (int y = 0; y < c.height; ++y) {
            for (int x = 0; x < c.width; ++x) {
                uint8_t value = 255;                                // background
                if (y >= 10 && y < 20) value = static_cast<uint8_t>(x % 7 ? 0 : 128);
                if (y >= 40 && y < 42) {                            // noise: literal runs
                    seed = seed * 1103515245u + 12345u;
                    value = static_cast<uint8_t>(seed >> 16);
                }
                if (y == 50 && x % 2) value = 0;                    // alternating: lone pixels
                for (int ch = 0; ch < c.channels; ++ch) {
                    pixels[(static_cast<size_t>(y) * c.width + x) * c.channels + ch] =
                        static_cast<uint8_t>(value ^ (ch * 40));
                }
            }
        }

        IOPwgRasterPage page;
        page.widthPixels = c.width;
        page.heightPixels = c.height;
        page.dpiX = page.dpiY = 300;
        page.pageWidthPoints = page.pageHeightPoints = 100;
        page.colorSpace = c.channels == 3 ? IOPwgColorSpace::SRGB : IOPwgColorSpace::SGray;

        std::vector<uint8_t> encoded;
        Check(AppendPwgRasterPageLines(page, pixels.data(), pixels.size(), encoded),
              std::string(c.what) + ": encodes");
        std::vector<uint8_t> decoded;
        size_t at = 0;
        const bool ok = DecodePwgLines(encoded, at, c.width, c.height, c.channels, decoded);
        Check(ok && decoded == pixels && at == encoded.size(),
              std::string("  decodes back to the same pixels, using every byte"));
    }

    // A blank A4 page at 300 dpi: 2480 x 3508 of white.
    IOPwgRasterPage blank;
    blank.widthPixels = 2480;
    blank.heightPixels = 3508;
    blank.dpiX = blank.dpiY = 300;
    blank.pageWidthPoints = 595;
    blank.pageHeightPoints = 842;
    blank.colorSpace = IOPwgColorSpace::SGray;
    std::vector<uint8_t> white(static_cast<size_t>(2480) * 3508, 255);
    std::vector<uint8_t> encoded;
    AppendPwgRasterPageLines(blank, white.data(), white.size(), encoded);
    Check(encoded.size() < 1000,
          "a blank A4 page compresses from 8.7 MB to " + std::to_string(encoded.size()) + " bytes");

    std::vector<uint8_t> nothing;
    Check(!AppendPwgRasterPageLines(blank, white.data(), white.size() - 1, nothing),
          "pixels that do not fill the page are refused");
}

void TestTransforms() {
    std::cout << "\n=== Turning a page ===\n";

    // 3 wide, 2 high, one byte a pixel:
    //   a b c
    //   d e f
    const std::vector<uint8_t> page = {'a', 'b', 'c', 'd', 'e', 'f'};
    auto turn = [&](int quarters, bool mx, bool my, int& w, int& h) {
        w = 3;
        h = 2;
        const std::vector<uint8_t> out = IOPwgTransformPixels(page, w, h, 1, quarters, mx, my);
        return std::string(out.begin(), out.end());
    };
    int w = 0;
    int h = 0;

    CheckEqual(turn(0, false, false, w, h), std::string("abcdef"), "no turn is the page");
    // Counter-clockwise: the top row becomes the left column, read upward.
    CheckEqual(turn(1, false, false, w, h), std::string("cfbead"),
               "a quarter turn counter-clockwise (landscape)");
    Check(w == 2 && h == 3, "  swaps width and height");
    CheckEqual(turn(3, false, false, w, h), std::string("daebfc"),
               "a quarter turn clockwise (reverse landscape)");
    CheckEqual(turn(2, false, false, w, h), std::string("fedcba"), "a half turn");
    CheckEqual(turn(0, true, false, w, h), std::string("cbafed"), "mirrored across");
    CheckEqual(turn(0, false, true, w, h), std::string("defabc"), "mirrored top to bottom");
    CheckEqual(turn(0, true, true, w, h), turn(2, false, false, w, h),
               "mirrored both ways is a half turn");
    CheckEqual(turn(1, false, true, w, h), std::string("adbecf"),
               "turned, then mirrored: the mirror applies to the turned page");
    CheckEqual(turn(4, false, false, w, h), std::string("abcdef"), "four quarter turns are none");

    // Placed on a 5 x 4 sheet one in from the left, two down.
    const std::vector<uint8_t> sheet = IOPwgPlaceOnSheet(page, 3, 2, 1, 5, 4, 1, 2);
    const std::string placed(sheet.begin(), sheet.end());
    const std::string white(5, static_cast<char>(255));
    const char W = static_cast<char>(255);
    CheckEqual(placed, white + white + std::string{W, 'a', 'b', 'c', W} +
                           std::string{W, 'd', 'e', 'f', W},
               "a page is placed on the sheet inside its margins, white around it");
    const std::vector<uint8_t> cut = IOPwgPlaceOnSheet(page, 3, 2, 1, 3, 2, 1, 1);
    CheckEqual(std::string(cut.begin(), cut.end()), std::string{W, W, W, W, 'a', 'b'},
               "  and cut at the sheet's edge rather than wrapped");

    std::cout << "\n=== The back of a sheet ===\n";
    auto back = [](const char* keyword, bool shortEdge) {
        const IOPwgBackSide b = IOPwgBackSideTransform(keyword, shortEdge);
        return std::string(b.mirrorCrossFeed ? "x" : "-") + (b.mirrorFeed ? "y" : "-");
    };
    CheckEqual(back("normal", false), std::string("--"), "normal: as drawn");
    CheckEqual(back("normal", true), std::string("--"), "  either edge");
    CheckEqual(back("flipped", false), std::string("-y"), "flipped, long edge: top to bottom");
    CheckEqual(back("flipped", true), std::string("x-"), "flipped, short edge: across");
    CheckEqual(back("rotated", false), std::string("xy"), "rotated, long edge: a half turn");
    CheckEqual(back("rotated", true), std::string("--"), "rotated, short edge: as drawn");
    CheckEqual(back("manual-tumble", false), std::string("--"), "manual-tumble, long edge: as drawn");
    CheckEqual(back("manual-tumble", true), std::string("xy"), "manual-tumble, short edge: a half turn");
    CheckEqual(back("Flipped", false), std::string("-y"), "the keyword is read without regard to case");
}

}  // namespace

int main() {
    std::cout << "IPP driverless printing: protocol, planning and PWG raster\n";

    TestRequestBytes();
    TestRoundTrip();
    TestMalformed();
    TestStatus();
    TestAddresses();
    TestWindowsPortAddresses();
    TestInstanceNames();
    TestCupsMatching();
    TestMediaNames();
    TestCapabilities();
    TestStatusAndSupplies();
    TestDocumentSupport();
    TestJobTemplate();
    TestDocumentPlan();
    TestRasterChoices();
    TestPwgHeader();
    TestPwgCompression();
    TestTransforms();

    std::cout << "\n" << g_passed << " passed, " << g_failed << " failed\n";
    if (g_failed == 0) {
        std::cout << "All IPP printer tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

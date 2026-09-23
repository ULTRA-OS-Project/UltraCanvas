// Tests/PixelFXMetadataDecodeTest.cpp
// PixelFX::Header::DecodeIPTC / DecodeXMP turn the IPTC and XMP blocks libvips
// hands over as raw bytes into the rows the media viewer's Details panel
// shows. Covered: bare IIM and the Photoshop "8BIM" wrapper a JPEG carries,
// repeated keywords, IIM dates and times, Latin-1 versus UTF-8 text, and the
// XMP forms writers use - attributes, language alternatives, bags and
// sequences, structures, resources - plus input that is truncated or not
// XML at all, which must give nothing rather than crash.
// Version: 1.0.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework

#include "PixelFX/PixelFXMetadataDecode.h"

#include <iostream>
#include <string>
#include <vector>

using PixelFX::Header::DecodedTag;
using PixelFX::Header::DecodeIPTC;
using PixelFX::Header::DecodeXMP;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string ValueOf(const std::vector<DecodedTag>& tags, const std::string& key) {
    for (const auto& t : tags) {
        if (t.first == key) return t.second;
    }
    return "<missing>";
}

void CheckValue(const std::vector<DecodedTag>& tags, const std::string& key, const std::string& expected) {
    const std::string got = ValueOf(tags, key);
    Check(got == expected, key + " = \"" + expected + "\"" + (got == expected ? "" : " (got \"" + got + "\")"));
}

// One IIM dataset: 0x1C, record, dataset, 16-bit big-endian length, value.
std::string Dataset(int record, int number, const std::string& value) {
    std::string d;
    d.push_back('\x1C');
    d.push_back(static_cast<char>(record));
    d.push_back(static_cast<char>(number));
    d.push_back(static_cast<char>((value.size() >> 8) & 0xFF));
    d.push_back(static_cast<char>(value.size() & 0xFF));
    return d + value;
}

// One Photoshop image resource: "8BIM", id, empty Pascal name (padded to
// two bytes), 32-bit size, data padded to an even length.
std::string Resource(int id, const std::string& data) {
    std::string r = "8BIM";
    r.push_back(static_cast<char>((id >> 8) & 0xFF));
    r.push_back(static_cast<char>(id & 0xFF));
    r.push_back('\0');
    r.push_back('\0');
    const size_t n = data.size();
    r.push_back(static_cast<char>((n >> 24) & 0xFF));
    r.push_back(static_cast<char>((n >> 16) & 0xFF));
    r.push_back(static_cast<char>((n >> 8) & 0xFF));
    r.push_back(static_cast<char>(n & 0xFF));
    r += data;
    if (n % 2) r.push_back('\0');
    return r;
}

std::vector<DecodedTag> Iptc(const std::string& bytes) { return DecodeIPTC(bytes.data(), bytes.size()); }
std::vector<DecodedTag> Xmp(const std::string& text) { return DecodeXMP(text.data(), text.size()); }

std::string SampleIim() {
    return Dataset(2, 0, std::string("\x00\x04", 2)) +   // record version: not shown
           Dataset(2, 5, "Sunset over the Thames") +
           Dataset(2, 25, "holiday") +
           Dataset(2, 25, "river") +
           Dataset(2, 25, "London") +
           Dataset(2, 90, "London") +
           Dataset(2, 101, "United Kingdom") +
           Dataset(2, 55, "20260920") +
           Dataset(2, 60, "143211+0100") +
           Dataset(2, 120, "Evening light\non the water") +
           Dataset(2, 199, "unnamed");
}

void TestBareIim() {
    std::cout << "\nBare IPTC-IIM records (TIFF):\n";
    auto tags = Iptc(SampleIim());
    CheckValue(tags, "Object Name", "Sunset over the Thames");
    CheckValue(tags, "Keywords", "holiday, river, London");
    CheckValue(tags, "City", "London");
    CheckValue(tags, "Country Name", "United Kingdom");
    CheckValue(tags, "Date Created", "2026-09-20");
    CheckValue(tags, "Time Created", "14:32:11+01:00");
    CheckValue(tags, "Caption/Abstract", "Evening light on the water");
    CheckValue(tags, "2:199", "unnamed");
    Check(ValueOf(tags, "2:0") == "<missing>", "record version dataset is not listed");
    Check(!tags.empty() && tags.front().first == "Object Name", "tags keep the order of the file");
}

void TestPhotoshopWrapper() {
    std::cout << "\nPhotoshop APP13 wrapper (JPEG):\n";
    // Another resource with an odd size first, so the padding is exercised.
    std::string block = std::string("Photoshop 3.0", 13) + '\0' +
                        Resource(0x03ED, "abc") + Resource(0x0404, SampleIim());
    auto tags = Iptc(block);
    CheckValue(tags, "Keywords", "holiday, river, London");
    CheckValue(tags, "City", "London");

    auto bare8bim = Iptc(Resource(0x0404, Dataset(2, 90, "Paris")));
    CheckValue(bare8bim, "City", "Paris");
}

void TestTextEncodings() {
    std::cout << "\nIPTC text encodings:\n";
    // Latin-1 u-umlaut (0xFC) without a character set declaration.
    auto latin1 = Iptc(Dataset(2, 90, "Z\xFCrich"));
    CheckValue(latin1, "City", "Z\xC3\xBCrich");

    // UTF-8 declared in 1:90 (ESC % G) is taken as it is.
    auto declared = Iptc(Dataset(1, 90, "\x1B%G") + Dataset(2, 90, "Z\xC3\xBCrich"));
    CheckValue(declared, "City", "Z\xC3\xBCrich");

    // UTF-8 without the declaration, as many writers produce it, is not
    // converted a second time.
    auto undeclared = Iptc(Dataset(2, 90, "Z\xC3\xBCrich"));
    CheckValue(undeclared, "City", "Z\xC3\xBCrich");
}

void TestBrokenIptc() {
    std::cout << "\nBroken IPTC data:\n";
    std::string full = SampleIim();
    auto truncated = Iptc(full.substr(0, full.size() - 3));
    Check(ValueOf(truncated, "Object Name") == "Sunset over the Thames",
          "a truncated block keeps the datasets before the cut");
    Check(Iptc("not iptc at all").empty(), "unrelated bytes decode to nothing");
    Check(DecodeIPTC(nullptr, 0).empty(), "no data decodes to nothing");
    std::string lying = std::string("Photoshop 3.0", 13) + '\0' + "8BIM\x04\x04\x00\x00\xFF\xFF\xFF\xFF";
    Check(Iptc(lying).empty(), "a resource claiming more bytes than exist decodes to nothing");
}

const char* kSampleXmp = R"(<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:xmp="http://ns.adobe.com/xap/1.0/"
    xmlns:dc="http://purl.org/dc/elements/1.1/"
    xmlns:photoshop="http://ns.adobe.com/photoshop/1.0/"
    xmlns:xmpRights="http://ns.adobe.com/xap/1.0/rights/"
    xmlns:Iptc4xmpCore="http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/"
    xmp:Rating="4"
    xmp:CreatorTool="UltraPaint 2.1"
    photoshop:City="London">
   <dc:title>
    <rdf:Alt>
     <rdf:li xml:lang="de">Sonnenuntergang</rdf:li>
     <rdf:li xml:lang="x-default">Sunset test</rdf:li>
    </rdf:Alt>
   </dc:title>
   <dc:subject>
    <rdf:Bag>
     <rdf:li>holiday</rdf:li>
     <rdf:li>river</rdf:li>
    </rdf:Bag>
   </dc:subject>
   <dc:creator>
    <rdf:Seq><rdf:li>S. F. Lau</rdf:li></rdf:Seq>
   </dc:creator>
   <xmpRights:WebStatement rdf:resource="https://example.org/licence"/>
   <Iptc4xmpCore:CreatorContactInfo rdf:parseType="Resource">
    <Iptc4xmpCore:CiAdrCity>London</Iptc4xmpCore:CiAdrCity>
    <Iptc4xmpCore:CiEmailWork>studio@example.org</Iptc4xmpCore:CiEmailWork>
   </Iptc4xmpCore:CreatorContactInfo>
  </rdf:Description>
  <rdf:Description rdf:about="" xmlns:exif="http://ns.adobe.com/exif/1.0/">
   <exif:Flash rdf:parseType="Resource"><exif:Fired>False</exif:Fired></exif:Flash>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>)";

void TestXmp() {
    std::cout << "\nXMP packet:\n";
    std::string packet = kSampleXmp;
    packet += std::string(64, ' ');   // writers pad the packet
    packet.push_back('\0');
    auto tags = Xmp(packet);
    CheckValue(tags, "xmp:Rating", "4");
    CheckValue(tags, "xmp:CreatorTool", "UltraPaint 2.1");
    CheckValue(tags, "photoshop:City", "London");
    CheckValue(tags, "dc:title", "Sunset test");
    CheckValue(tags, "dc:subject", "holiday, river");
    CheckValue(tags, "dc:creator", "S. F. Lau");
    CheckValue(tags, "xmpRights:WebStatement", "https://example.org/licence");
    CheckValue(tags, "Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiAdrCity", "London");
    CheckValue(tags, "Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiEmailWork", "studio@example.org");
    CheckValue(tags, "exif:Flash/exif:Fired", "False");
    Check(ValueOf(tags, "rdf:about") == "<missing>", "RDF syntax attributes are not listed");
    Check(ValueOf(tags, "xmlns:dc") == "<missing>", "namespace declarations are not listed");
}

void TestXmpStructArrays() {
    std::cout << "\nXMP arrays of structures:\n";
    auto tags = Xmp(R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
      <rdf:Description xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/" xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#">
       <xmpMM:History><rdf:Seq>
        <rdf:li stEvt:action="created" stEvt:when="2026-09-20T14:32:11"/>
        <rdf:li rdf:parseType="Resource"><stEvt:action>saved</stEvt:action></rdf:li>
       </rdf:Seq></xmpMM:History>
      </rdf:Description></rdf:RDF></x:xmpmeta>)");
    // Each item is a structure, so there is no joined "xmpMM:History" row.
    CheckValue(tags, "xmpMM:History", "<missing>");
    CheckValue(tags, "xmpMM:History[1]/stEvt:action", "created");
    CheckValue(tags, "xmpMM:History[1]/stEvt:when", "2026-09-20T14:32:11");
    CheckValue(tags, "xmpMM:History[2]/stEvt:action", "saved");
}

void TestBrokenXmp() {
    std::cout << "\nBroken XMP:\n";
    Check(Xmp("<x:xmpmeta><rdf:RDF><rdf:Description").empty(), "unterminated XML decodes to nothing");
    Check(Xmp("plain text").empty(), "text that is not XML decodes to nothing");
    Check(Xmp("<root><child>no rdf</child></root>").empty(), "XML without rdf:RDF decodes to nothing");
    Check(DecodeXMP(nullptr, 0).empty(), "no data decodes to nothing");

    std::string longValue(2000, 'a');
    auto tags = Xmp("<rdf:RDF xmlns:rdf=\"r\"><rdf:Description xmlns:t=\"t\" t:Long=\"" + longValue + "\"/></rdf:RDF>");
    const std::string v = ValueOf(tags, "t:Long");
    Check(v.size() <= 512 && v.size() > 400 && v.substr(v.size() - 3) == "...", "a very long value is cut to 512 bytes");
}

} // namespace

int main() {
    std::cout << "PixelFX IPTC / XMP decoding\n";
    std::cout << "===========================\n";

    TestBareIim();
    TestPhotoshopWrapper();
    TestTextEncodings();
    TestBrokenIptc();
    TestXmp();
    TestXmpStructArrays();
    TestBrokenXmp();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}

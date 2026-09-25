// Tests/PixelFXMetadataDecodeTest.cpp
// PixelFX::Header::DecodeIPTC / DecodeXMP turn the IPTC and XMP blocks libvips
// hands over as raw bytes into the rows the media viewer's Details panel
// shows. Covered: bare IIM and the Photoshop "8BIM" wrapper a JPEG carries,
// repeated keywords, IIM dates and times, Latin-1 versus UTF-8 text, and the
// XMP forms writers use - attributes, language alternatives, bags and
// sequences, structures, resources - plus input that is truncated or not
// XML at all, which must give nothing rather than crash. And the EXIF value
// formatting (HumanizeExif), fed the strings libvips 8.15 actually produces,
// the display names (FriendlyTagName), XMP value tidying, ImageMagick's PNG
// raw profiles and libvips' own fields (TidyOtherValue).
// Version: 1.3.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework

#include "PixelFX/PixelFXMetadataDecode.h"

#include <iostream>
#include <string>
#include <vector>

using PixelFX::Header::DecodedTag;
using PixelFX::Header::DecodeIPTC;
using PixelFX::Header::DecodeRawProfile;
using PixelFX::Header::DecodeXMP;
using PixelFX::Header::ExifField;
using PixelFX::Header::FriendlyTagName;
using PixelFX::Header::HumanizeExif;
using PixelFX::Header::SplitExifString;
using PixelFX::Header::TidyOtherValue;

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
    CheckValue(tags, "xmp:Rating", "4 of 5");
    CheckValue(tags, "xmp:CreatorTool", "UltraPaint 2.1");
    CheckValue(tags, "photoshop:City", "London");
    CheckValue(tags, "dc:title", "Sunset test");
    CheckValue(tags, "dc:subject", "holiday, river");
    CheckValue(tags, "dc:creator", "S. F. Lau");
    CheckValue(tags, "xmpRights:WebStatement", "https://example.org/licence");
    CheckValue(tags, "Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiAdrCity", "London");
    CheckValue(tags, "Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiEmailWork", "studio@example.org");
    CheckValue(tags, "exif:Flash/exif:Fired", "No");
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
    CheckValue(tags, "xmpMM:History[1]/stEvt:when", "2026-09-20 14:32:11");
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


// ===== EXIF =====

std::vector<ExifField> SampleExif() {
    return {
        {0, "Make", "Canon (Canon, ASCII, 6 components, 6 bytes)"},
        {0, "Orientation", "6 (Right-top, Short, 1 components, 2 bytes)"},
        {0, "XResolution", "300/1 (300, Rational, 1 components, 8 bytes)"},
        {0, "YResolution", "300/1 (300, Rational, 1 components, 8 bytes)"},
        {0, "ResolutionUnit", "2 (Inch, Short, 1 components, 2 bytes)"},
        {0, "Copyright", "(c) 2026 RISC OS Cloverleaf ((c) 2026 RISC OS Cloverleaf (Photographer) - [None] (Editor), ASCII, 28 components, 28 bytes)"},
        {1, "XResolution", "0/1 ( 0, Rational, 1 components, 8 bytes)"},
        {1, "Compression", "6 (JPEG compression, Short, 1 components, 2 bytes)"},
        {1, "JPEGInterchangeFormat", "1234 (1234, Long, 1 components, 4 bytes)"},
        {2, "ExposureTime", "1/250 (1/250 sec., Rational, 1 components, 8 bytes)"},
        {2, "FNumber", "28/5 (f/5.6, Rational, 1 components, 8 bytes)"},
        {2, "ExposureProgram", "3 (Aperture priority, Short, 1 components, 2 bytes)"},
        {2, "ISOSpeedRatings", "400 (400, Short, 1 components, 2 bytes)"},
        {2, "ExifVersion", "Exif Version 2.32 (Exif Version 2.32, Undefined, 4 components, 4 bytes)"},
        {2, "DateTimeOriginal", "2026:09:20 14:32:11 (2026:09:20 14:32:11, ASCII, 20 components, 20 bytes)"},
        {2, "ComponentsConfiguration", "Y Cb Cr - (Y Cb Cr -, Undefined, 4 components, 4 bytes)"},
        {2, "ShutterSpeedValue", "56573/7102 (7.97 EV (1/250 sec.), SRational, 1 components, 8 bytes)"},
        {2, "ApertureValue", "40761/8200 (4.97 EV (f/5.6), Rational, 1 components, 8 bytes)"},
        {2, "BrightnessValue", "73/10 (7.30 EV (539.93 cd/m^2), SRational, 1 components, 8 bytes)"},
        {2, "ExposureBiasValue", "-2/3 (-0.67 EV, SRational, 1 components, 8 bytes)"},
        {2, "MaxApertureValue", "13166/7763 (1.70 EV (f/1.8), Rational, 1 components, 8 bytes)"},
        {2, "MeteringMode", "5 (Pattern, Short, 1 components, 2 bytes)"},
        {2, "Flash", "16 (Flash did not fire, compulsory flash mode., Short, 1 components, 2 bytes)"},
        {2, "FocalLength", "50/1 (50.0 mm, Rational, 1 components, 8 bytes)"},
        {2, "FlashpixVersion", "FlashPix Version 1.0 (FlashPix Version 1.0, Undefined, 4 components, 4 bytes)"},
        {2, "ColorSpace", "65535 (Uncalibrated, Short, 1 components, 2 bytes)"},
        {2, "DigitalZoomRatio", "0/1 ( 0, Rational, 1 components, 8 bytes)"},
        {2, "LensSpecification", "50/1 50/1 9/5 9/5 (50, 50, 1.8, 1.8, Rational, 4 components, 32 bytes)"},
        {2, "LightSource", "99 (Internal error (unknown value 99), Short, 1 components, 2 bytes)"},
        {3, "GPSLatitudeRef", "N (N, ASCII, 2 components, 2 bytes)"},
        {3, "GPSLatitude", "51/1 30/1 0/1 (51, 30,  0, Rational, 3 components, 24 bytes)"},
        {3, "GPSLongitudeRef", "W (W, ASCII, 2 components, 2 bytes)"},
        {3, "GPSLongitude", "0/1 7/1 12/1 ( 0,  7, 12, Rational, 3 components, 24 bytes)"},
        {3, "GPSAltitudeRef", "Sea level (Sea level, Byte, 1 components, 1 bytes)"},
        {3, "GPSAltitude", "35/1 (35, Rational, 1 components, 8 bytes)"},
        {3, "GPSTimeStamp", "13/1 32/1 11/1 (13:32:11.00, Rational, 3 components, 24 bytes)"},
        {3, "GPSImgDirectionRef", "T (T, ASCII, 2 components, 2 bytes)"},
        {3, "GPSImgDirection", "617/5 (123.4, Rational, 1 components, 8 bytes)"},
        {3, "GPSDateStamp", "2026:09:20 (2026:09:20, ASCII, 11 components, 11 bytes)"},
    };
}

void TestExifSplit() {
    std::cout << "\nlibvips EXIF strings:\n";
    std::string value, reading;
    SplitExifString("28/5 (f/5.6, Rational, 1 components, 8 bytes)", value, reading);
    Check(value == "28/5" && reading == "f/5.6", "value and reading are separated");
    SplitExifString("51/1 30/1 0/1 (51, 30,  0, Rational, 3 components, 24 bytes)", value, reading);
    Check(value == "51/1 30/1 0/1" && reading == "51, 30,  0", "a reading with commas stays whole");
    SplitExifString("(c) ACME ((c) ACME, ASCII, 9 components, 9 bytes)", value, reading);
    Check(value == "(c) ACME" && reading == "(c) ACME", "parentheses inside the value are not the annotation");
    SplitExifString("plain", value, reading);
    Check(value == "plain" && reading.empty(), "a string without annotation is all value");
}

void TestExifValues() {
    std::cout << "\nEXIF values for a person:\n";
    auto tags = HumanizeExif(SampleExif());
    CheckValue(tags, "Make", "Canon");
    CheckValue(tags, "Orientation", "Rotated 90\xC2\xB0 clockwise");
    CheckValue(tags, "XResolution", "300 dpi");
    CheckValue(tags, "Copyright", "(c) 2026 RISC OS Cloverleaf");
    CheckValue(tags, "ExposureTime", "1/250 s");
    CheckValue(tags, "ShutterSpeedValue", "1/250 s");
    CheckValue(tags, "FNumber", "f/5.6");
    CheckValue(tags, "ApertureValue", "f/5.6");
    CheckValue(tags, "MaxApertureValue", "f/1.8");
    CheckValue(tags, "ExposureBiasValue", "-0.67 EV");
    CheckValue(tags, "BrightnessValue", "+7.3 EV");
    CheckValue(tags, "ExposureProgram", "Aperture priority");
    CheckValue(tags, "MeteringMode", "Pattern");
    CheckValue(tags, "Flash", "Flash did not fire, compulsory flash mode");
    CheckValue(tags, "ColorSpace", "Uncalibrated");
    CheckValue(tags, "ISOSpeedRatings", "ISO 400");
    CheckValue(tags, "FocalLength", "50 mm");
    CheckValue(tags, "LensSpecification", "50 mm f/1.8");
    CheckValue(tags, "ExifVersion", "2.32");
    CheckValue(tags, "FlashpixVersion", "1.0");
    CheckValue(tags, "ComponentsConfiguration", "Y Cb Cr -");
    CheckValue(tags, "DateTimeOriginal", "2026-09-20 14:32:11");
    CheckValue(tags, "LightSource", "99");
    CheckValue(tags, "Thumbnail Compression", "JPEG compression");
}

void TestExifGps() {
    std::cout << "\nEXIF GPS:\n";
    auto tags = HumanizeExif(SampleExif());
    CheckValue(tags, "GPSLatitude", "51\xC2\xB0 30\xE2\x80\xB2 0\xE2\x80\xB3 N (51.5\xC2\xB0)");
    CheckValue(tags, "GPSLongitude", "0\xC2\xB0 7\xE2\x80\xB2 12\xE2\x80\xB3 W (-0.12\xC2\xB0)");
    CheckValue(tags, "GPSAltitude", "35 m");
    CheckValue(tags, "GPSTimeStamp", "13:32:11 UTC");
    CheckValue(tags, "GPSImgDirection", "123.4\xC2\xB0 (true north)");
    CheckValue(tags, "GPSDateStamp", "2026-09-20");

    auto below = HumanizeExif({{3, "GPSAltitudeRef", "Sea level reference (Sea level reference, Byte, 1 components, 1 bytes)"},
                               {3, "GPSAltitude", "12/1 (12, Rational, 1 components, 8 bytes)"}});
    CheckValue(below, "GPSAltitude", "12 m below sea level");

    // Minutes written as a decimal, seconds zero: 51 deg 30.5 min.
    auto decimalMinutes = HumanizeExif({{3, "GPSLatitude", "51/1 3050/100 0/1 (51, 30.50,  0, Rational, 3 components, 24 bytes)"}});
    CheckValue(decimalMinutes, "GPSLatitude", "51\xC2\xB0 30\xE2\x80\xB2 30\xE2\x80\xB3 (51.508333\xC2\xB0)");
}

void TestExifLeftOut() {
    std::cout << "\nEXIF rows left out:\n";
    auto tags = HumanizeExif(SampleExif());
    CheckValue(tags, "Thumbnail XResolution", "<missing>");
    CheckValue(tags, "Thumbnail JPEGInterchangeFormat", "<missing>");
    CheckValue(tags, "DigitalZoomRatio", "<missing>");
    CheckValue(tags, "ResolutionUnit", "<missing>");
    CheckValue(tags, "GPSLatitudeRef", "<missing>");
    CheckValue(tags, "GPSAltitudeRef", "<missing>");
    CheckValue(tags, "GPSImgDirectionRef", "<missing>");

    auto odd = HumanizeExif({{2, "FNumber", "5/0 (?, Rational, 1 components, 8 bytes)"},
                             {2, "ExposureTime", "garbage"},
                             {0, "Orientation", "99999999999 (?, Short, 1 components, 2 bytes)"}});
    CheckValue(odd, "FNumber", "5/0");
    CheckValue(odd, "ExposureTime", "garbage");
    CheckValue(odd, "Orientation", "99999999999");
}


// ===== DISPLAY NAMES =====

void CheckName(const std::string& group, const std::string& key, const std::string& expected) {
    const std::string got = FriendlyTagName(group, key);
    Check(got == expected, group + " " + key + " -> \"" + expected + "\"" +
                               (got == expected ? "" : " (got \"" + got + "\")"));
}

void TestFriendlyNames() {
    std::cout << "\nDisplay names:\n";
    CheckName("EXIF", "FNumber", "F-number");
    CheckName("EXIF", "DateTimeOriginal", "Date taken");
    CheckName("EXIF", "ExposureBiasValue", "Exposure compensation");
    CheckName("EXIF", "ISOSpeedRatings", "ISO");
    CheckName("EXIF", "GPSLatitude", "Latitude");
    CheckName("EXIF", "Make", "Camera make");
    CheckName("EXIF", "Thumbnail Compression", "Thumbnail Compression");
    CheckName("EXIF", "Thumbnail XResolution", "Thumbnail Horizontal resolution");
    // Not in the table: split into words, acronyms kept.
    CheckName("EXIF", "CompositeImage", "Composite image");
    CheckName("EXIF", "GPSHPositioningErrorX", "GPSH positioning error X");
    CheckName("EXIF", "FocalLengthIn35mmFilmX", "Focal length in 35 mm film X");
    CheckName("EXIF", "SourceExposureTimesOfCompositeImage", "Source exposure times of composite image");

    CheckName("IPTC", "By-line", "Author");
    CheckName("IPTC", "Caption/Abstract", "Caption");
    CheckName("IPTC", "Keywords", "Keywords");
    CheckName("IPTC", "2:199", "2:199");
    CheckName("IPTC", "Date Created", "Date created");
    CheckName("IPTC", "Content Location Name", "Content location name");
    CheckName("IPTC", "Country Code", "Country code");

    CheckName("XMP", "dc:subject", "Keywords");
    CheckName("XMP", "xmp:CreatorTool", "Created with");
    CheckName("XMP", "Iptc4xmpCore:CreatorContactInfo/Iptc4xmpCore:CiEmailWork",
              "Creator contact \xE2\x80\xBA Email");
    CheckName("XMP", "xmpMM:History[2]/stEvt:action", "History 2 \xE2\x80\xBA Action");
    CheckName("XMP", "exif:Flash/exif:Fired", "Flash \xE2\x80\xBA Fired");
    CheckName("XMP", "crs:WhiteBalance", "White balance");
    CheckName("XMP", "noprefix", "Noprefix");

    CheckName("Other", "jpeg-chroma-subsample", "Chroma subsampling");
    CheckName("Other", "png-comment-0-Title", "Title");
    CheckName("Other", "png-comment-3-Creation Time", "Creation Time");
    CheckName("Other", "heif-bitdepth", "Heif bitdepth");
    CheckName("Colour", "icc-profile-data", "ICC profile");
    CheckName("Image", "Dimensions", "Dimensions");
    CheckName("EXIF", "", "");
}


// ===== XMP VALUES, RAW PROFILES, LIBVIPS FIELDS =====

std::string XmpValue(const std::string& name, const std::string& value) {
    const std::string packet =
        "<rdf:RDF xmlns:rdf=\"r\"><rdf:Description xmlns:xmp=\"x\" xmp:" + name + "=\"" + value + "\"/></rdf:RDF>";
    return ValueOf(Xmp(packet), "xmp:" + name);
}

void TestXmpValues() {
    std::cout << "\nXMP values:\n";
    Check(XmpValue("CreateDate", "2026-09-20T14:32:11+01:00") == "2026-09-20 14:32:11 +01:00", "date with offset");
    Check(XmpValue("CreateDate", "2026-09-20T14:32:11.25Z") == "2026-09-20 14:32:11 UTC", "UTC date, fraction dropped");
    Check(XmpValue("CreateDate", "2026-09-20T14:32") == "2026-09-20 14:32", "date without seconds");
    Check(XmpValue("CreateDate", "2026-09-20T14:32:11+0100") == "2026-09-20 14:32:11 +01:00", "offset without colon");
    Check(XmpValue("CreateDate", "2026-09-20") == "2026-09-20", "a date alone is left as it is");
    Check(XmpValue("Label", "2026-09-20Tea time") == "2026-09-20Tea time", "text that starts like a date is left alone");
    Check(XmpValue("Rating", "5") == "5 of 5", "rating");
    Check(XmpValue("Rating", "0") == "Not rated", "rating 0");
    Check(XmpValue("Rating", "-1") == "Rejected", "rating -1");
    Check(XmpValue("Marked", "True") == "Yes", "True -> Yes");
}

std::string RawProfileText(const std::string& name, const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string text = "\n" + name + "\n";
    std::string length = std::to_string(bytes.size());
    text += std::string(8 - length.size(), ' ') + length + "\n";
    for (size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        text.push_back(hex[c >> 4]);
        text.push_back(hex[c & 15]);
        if (i % 36 == 35) text.push_back('\n');
    }
    return text + "\n";
}

void TestRawProfiles() {
    std::cout << "\nPNG raw profiles (ImageMagick):\n";
    const std::string iim = SampleIim();
    std::string name, bytes;
    Check(DecodeRawProfile(RawProfileText("iptc", iim), name, bytes) && name == "iptc" && bytes == iim,
          "iptc profile decodes to its bytes");
    auto tags = Iptc(bytes);
    CheckValue(tags, "Keywords", "holiday, river, London");

    std::string lying = RawProfileText("xmp", "abc");
    lying.replace(lying.find("3\n"), 1, "9");
    Check(!DecodeRawProfile(lying, name, bytes), "a length the hex does not reach is refused");
    Check(!DecodeRawProfile("\nxmp\n   4\n12zz", name, bytes), "non-hex digits are refused");
    Check(!DecodeRawProfile("plain comment", name, bytes), "an ordinary comment is not a profile");
}

std::string Tidied(const std::string& field, std::string value, bool hasExif = true, bool* kept = nullptr) {
    const bool k = TidyOtherValue(field, value, hasExif);
    if (kept) *kept = k;
    return k ? value : "<dropped>";
}

void TestOtherFields() {
    std::cout << "\nlibvips' own fields:\n";
    Check(Tidied("jpeg-multiscan", "0") == "No", "progressive 0 -> No");
    Check(Tidied("interlaced", "1") == "Yes", "interlaced 1 -> Yes");
    Check(Tidied("loop", "0") == "Forever", "loop 0 -> Forever");
    Check(Tidied("loop", "1") == "Once", "loop 1 -> Once");
    Check(Tidied("loop", "3") == "3 times", "loop 3 -> 3 times");
    Check(Tidied("delay", "100 100 100") == "100 ms per frame", "equal delays");
    Check(Tidied("delay", "40 100 60") == "40\xE2\x80\x93" "100 ms per frame", "varying delays");
    Check(Tidied("delay", "0 ") == "0 ms", "one frame");
    Check(Tidied("gif-palette", "-8484711 -9072721 -9922353 ") == "3 colours", "palette by size");
    Check(Tidied("background", "255 255 255 ") == "RGB 255, 255, 255", "background colour");
    Check(Tidied("page-height", "480") == "480 px", "page height");
    Check(Tidied("resolution-unit", "in") == "<dropped>", "resolution unit is left out");
    Check(Tidied("orientation", "6", true) == "<dropped>", "orientation is left out when EXIF has it");
    Check(Tidied("orientation", "6", false) == "Rotated 90\xC2\xB0 clockwise", "orientation without EXIF");
    Check(Tidied("jpeg-chroma-subsample", "4:4:4") == "4:4:4", "other fields unchanged");
    Check(Tidied("delay", "a b") == "a b", "unexpected delays left as they are");
}

} // namespace

int main() {
    std::cout << "PixelFX IPTC / XMP decoding, EXIF formatting, display names\n";
    std::cout << "=============================================================\n";

    TestBareIim();
    TestPhotoshopWrapper();
    TestTextEncodings();
    TestBrokenIptc();
    TestXmp();
    TestXmpStructArrays();
    TestBrokenXmp();
    TestExifSplit();
    TestExifValues();
    TestExifGps();
    TestExifLeftOut();
    TestFriendlyNames();
    TestXmpValues();
    TestRawProfiles();
    TestOtherFields();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}

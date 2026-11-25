// core/UltraCanvasPresentationOdpWriter.cpp
// OpenDocument presentation (.odp / .otp / .fodp) writer - see
// UltraCanvasPresentationOdp.h: a PptxPresentation written as an Impress deck
// LibreOffice and the reader open.
// Version: 1.1.0 - the deck's masters as master pages (name, background, placeholder frames, their
//                  presentation styles), its layouts as presentation page layouts, each slide on its
//                  layout's master; transitions (smil type, speed, auto-advance, the exact length
//                  as the page's anim:transitionFilter)
// Version: 1.0.1 - a turned shape's gradient is turned with it (ODF's angle is on the page), and a
//                  radial gradient starts at the edge, as ODF reads it
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasOdfChart.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasZipPackage.h"
#include "Documents/Word/UltraCanvasWordFormatInternal.h"   // SymbolFontCharToUnicode

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr int64_t kDefaultSlideWidth = 12192000;   // 16:9
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr double kEmuPerPoint = 12700.0;
constexpr double kPi = 3.14159265358979323846;
constexpr const char* kPresentationType = "application/vnd.oasis.opendocument.presentation";
constexpr const char* kTemplateType = "application/vnd.oasis.opendocument.presentation-template";
constexpr const char* kXmlHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

const char* Namespaces() {
    return "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
           "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
           "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
           "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
           "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
           "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
           "xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\" "
           "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" "
           "xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\" "
           "xmlns:anim=\"urn:oasis:names:tc:opendocument:xmlns:animation:1.0\" "
           "office:version=\"1.3\"";
}

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;   // no place in XML 1.0
                out += ch;
        }
    }
    return out;
}

std::string Fixed(double value, int decimals) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

// EMU as ODF centimetres.
std::string Cm(double emu) { return Fixed(emu / 360000.0, 4) + "cm"; }
std::string Pt(double points) { return Fixed(points, 2) + "pt"; }

// "RRGGBB" (or "#RRGGBB") as ODF's "#rrggbb"; empty for anything else.
std::string Colour(const std::string& colour) {
    std::string digits = !colour.empty() && colour[0] == '#' ? colour.substr(1) : colour;
    if (digits.size() != 6) return "";
    for (char& c : digits) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "";
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return "#" + digits;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string PictureExtension(const std::string& target, const std::vector<uint8_t>& bytes) {
    const size_t dot = target.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : Lower(target.substr(dot + 1));
    if (ext == "jpg" || ext == "jpeg") return "jpg";
    if (ext == "png" || ext == "gif" || ext == "bmp" || ext == "svg" || ext == "emf" || ext == "wmf" || ext == "tif" ||
        ext == "tiff")
        return ext;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) return "jpg";
    if (bytes.size() >= 4 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') return "gif";
    return "png";
}

std::string PictureMediaType(const std::string& ext) {
    if (ext == "jpg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "emf") return "image/x-emf";
    if (ext == "wmf") return "image/x-wmf";
    if (ext == "tif") return "image/tiff";
    return "image/" + ext;
}

// A bullet character as it shows: a symbol font's code (Wingdings' "§" is a
// square) as the Unicode character it draws, since the ODF reader of the
// deck may not have the font.
std::string BulletText(const PptxParagraph& p) {
    if (p.bulletChar.empty()) return p.level % 2 ? "\xE2\x80\x93" : "\xE2\x80\xA2";   // – / •
    const auto* s = reinterpret_cast<const unsigned char*>(p.bulletChar.data());
    uint32_t cp = s[0];
    if ((s[0] & 0xE0) == 0xC0 && p.bulletChar.size() >= 2) cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
    else if ((s[0] & 0xF0) == 0xE0 && p.bulletChar.size() >= 3)
        cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
    if (!p.bulletFont.empty()) {
        if (const uint32_t mapped = WordFormatInternal::SymbolFontCharToUnicode(p.bulletFont, cp)) {
            std::string out;
            if (mapped < 0x80) out += static_cast<char>(mapped);
            else if (mapped < 0x800) {
                out += static_cast<char>(0xC0 | (mapped >> 6));
                out += static_cast<char>(0x80 | (mapped & 0x3F));
            } else if (mapped < 0x10000) {
                out += static_cast<char>(0xE0 | (mapped >> 12));
                out += static_cast<char>(0x80 | ((mapped >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (mapped & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (mapped >> 18));
                out += static_cast<char>(0x80 | ((mapped >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((mapped >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (mapped & 0x3F));
            }
            return out;
        }
    }
    return p.bulletChar;
}

// An a:buAutoNum scheme as ODF's number format, prefix and suffix.
void NumberFormat(const std::string& scheme, std::string& format, std::string& prefix, std::string& suffix) {
    format = scheme.rfind("alphaLc", 0) == 0   ? "a"
           : scheme.rfind("alphaUc", 0) == 0   ? "A"
           : scheme.rfind("romanLc", 0) == 0   ? "i"
           : scheme.rfind("romanUc", 0) == 0   ? "I"
                                               : "1";
    auto endsWith = [&](const char* tail) {
        const std::string t = tail;
        return scheme.size() >= t.size() && scheme.compare(scheme.size() - t.size(), t.size(), t) == 0;
    };
    prefix.clear();
    if (endsWith("ParenBoth")) {
        prefix = "(";
        suffix = ")";
    } else if (endsWith("ParenR")) {
        suffix = ")";
    } else if (endsWith("Plain")) {
        suffix = "";
    } else {
        suffix = ".";
    }
}

// The outline a shape is drawn in, as an enhanced-geometry path in a
// 21600-unit box; empty for the plain kinds ODF names (rectangle, ellipse).
std::string EnhancedPath(const std::vector<RichShape::PathPoint>& path, bool& open) {
    using Op = RichShape::PathPoint::Op;
    auto v = [](float f) { return std::to_string(static_cast<long long>(std::llround(f * 21600.0))); };
    std::string out;
    bool closed = false;
    for (const RichShape::PathPoint& p : path) {
        switch (p.op) {
            case Op::Move: out += (out.empty() ? "M " : " M ") + v(p.x) + " " + v(p.y); break;
            case Op::Line: out += " L " + v(p.x) + " " + v(p.y); break;
            case Op::Curve:
                out += " C " + v(p.c1x) + " " + v(p.c1y) + " " + v(p.c2x) + " " + v(p.c2y) + " " + v(p.x) + " " + v(p.y);
                break;
            case Op::Close:
                out += " Z";
                closed = true;
                break;
        }
    }
    open = !closed;
    // An open outline (a bent connector) is not filled.
    return out.empty() ? out : out + (open ? " F N" : " N");
}

// ----- Automatic styles, each written once for its properties -----

class Styles {
public:
    // `prefix` goes before every name: a master page's styles ("M...") share
    // a flat file's automatic styles with the slides'.
    explicit Styles(std::string prefix = std::string()) : prefix_(std::move(prefix)) {}

    // A named style of `family` for `properties` (the elements inside it).
    std::string Get(const std::string& family, const std::string& prefix, const std::string& properties,
                    const std::string& extra = std::string()) {
        const std::string key = family + "|" + extra + "|" + properties;
        auto found = names_.find(key);
        if (found != names_.end()) return found->second;
        const std::string name = prefix_ + prefix + std::to_string(++counts_[prefix]);
        names_.emplace(key, name);
        xml_ += "<style:style style:name=\"" + name + "\" style:family=\"" + family + "\"" + extra + ">" + properties +
                "</style:style>";
        return name;
    }
    std::string List(const std::string& levels) {
        auto found = lists_.find(levels);
        if (found != lists_.end()) return found->second;
        const std::string name = prefix_ + "L" + std::to_string(lists_.size() + 1);
        lists_.emplace(levels, name);
        xml_ += "<text:list-style style:name=\"" + name + "\">" + levels + "</text:list-style>";
        return name;
    }
    std::string Gradient(const std::string& from, const std::string& to, double angleDeg, bool radial) {
        const std::string key = from + to + Fixed(angleDeg, 1) + (radial ? "r" : "l");
        auto found = gradients_.find(key);
        if (found != gradients_.end()) return found->second;
        const std::string name = prefix_ + "Gradient_" + std::to_string(gradients_.size() + 1);
        gradients_.emplace(key, name);
        // ODF turns a gradient counter-clockwise from top-to-bottom;
        // DrawingML clockwise from left-to-right.
        const double odf = std::fmod(std::fmod(90.0 - angleDeg, 360.0) + 360.0, 360.0);
        common_ += "<draw:gradient draw:name=\"" + name + "\" draw:style=\"" + (radial ? "radial" : "linear") +
                   "\" draw:cx=\"50%\" draw:cy=\"50%\" draw:start-color=\"" + from + "\" draw:end-color=\"" + to +
                   "\" draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:angle=\"" + Fixed(odf, 1) +
                   "deg\" draw:border=\"0%\"/>";
        return name;
    }
    const std::string& Automatic() const { return xml_; }
    const std::string& Common() const { return common_; }

private:
    std::string prefix_;
    std::map<std::string, std::string> names_;
    std::map<std::string, int> counts_;
    std::map<std::string, std::string> lists_;
    std::map<std::string, std::string> gradients_;
    std::string xml_;
    std::string common_;
};

// ----- The deck -----

class OdpWriter {
public:
    OdpWriter(const PptxPresentation& deck, bool flat) : deck_(deck), flat_(flat) {}

    // The master pages and page layouts, then content.xml's automatic
    // styles and body (or a flat file's).
    void Build() {
        BuildDesign();
        for (size_t i = 0; i < deck_.slides.size(); ++i) body_ += Page(deck_.slides[i], static_cast<int>(i + 1));
    }

    std::string Content() const {
        return std::string(kXmlHead) + "<office:document-content " + Namespaces() + "><office:automatic-styles>" +
               contentStyles_.Automatic() + "</office:automatic-styles><office:body><office:presentation>" + body_ +
               "</office:presentation></office:body></office:document-content>";
    }

    // The common styles: the defaults, gradients, markers and dashes.
    std::string CommonStyles() const {
        return "<office:styles><style:default-style style:family=\"graphic\"><style:graphic-properties "
               "draw:shadow=\"hidden\"/><style:paragraph-properties fo:text-align=\"start\"/><style:text-properties "
               "fo:font-size=\"18pt\"/></style:default-style>"
               "<draw:marker draw:name=\"Arrow\" svg:viewBox=\"0 0 20 30\" svg:d=\"M10 0 L20 30 L0 30 Z\"/>"
               "<draw:stroke-dash draw:name=\"Dash\" draw:style=\"rect\" draw:dots1=\"1\" draw:dots1-length=\"300%\" "
               "draw:distance=\"200%\"/><draw:stroke-dash draw:name=\"Dot\" draw:style=\"round\" draw:dots1=\"1\" "
               "draw:dots1-length=\"100%\" draw:distance=\"150%\"/>" + contentStyles_.Common() + masterStyles_.Common() +
               designCommon_ + "</office:styles>";
    }

    // The page layouts and the master page every slide is on.
    std::string MasterAutomatic() const {
        return "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:margin-top=\"0cm\" "
               "fo:margin-bottom=\"0cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:page-width=\"" + Cm(Width()) +
               "\" fo:page-height=\"" + Cm(Height()) + "\" style:print-orientation=\"" +
               (Width() >= Height() ? "landscape" : "portrait") + "\"/></style:page-layout>"
               "<style:page-layout style:name=\"PM2\"><style:page-layout-properties fo:margin-top=\"0cm\" "
               "fo:margin-bottom=\"0cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:page-width=\"21cm\" "
               "fo:page-height=\"29.7cm\" style:print-orientation=\"portrait\"/></style:page-layout>" +
               masterStyles_.Automatic();
    }

    std::string MasterStyles() const { return "<office:master-styles>" + masterPages_ + "</office:master-styles>"; }

    // The package's other parts: pictures and chart objects.
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& Pictures() const { return pictures_; }
    const std::vector<std::string>& Charts() const { return charts_; }

private:
    const PptxPresentation& deck_;
    bool flat_;
    Styles contentStyles_;
    Styles masterStyles_{"M"};
    Styles* styles_ = &contentStyles_;   // the slides', or the master pages' while they are written
    std::string body_;
    std::string masterPages_;            // office:master-styles' master pages
    std::string designCommon_;           // the masters' presentation styles and the page layouts (office:styles)
    std::vector<std::string> masterNames_;   // each master page's style:name
    bool ownMasters_ = false;            // the deck's masters are written (else one plain "Default")
    std::string pageMaster_;             // the master page of the slide being written
    std::map<std::string, std::string> pictureParts_;                      // target -> Pictures/imageN.ext
    std::vector<std::pair<std::string, std::vector<uint8_t>>> pictures_;   // part -> bytes
    std::vector<std::string> charts_;                                       // Object N/content.xml
    int shapeCount_ = 0;

    int64_t Width() const { return deck_.slideWidth > 0 ? deck_.slideWidth : kDefaultSlideWidth; }
    int64_t Height() const { return deck_.slideHeight > 0 ? deck_.slideHeight : kDefaultSlideHeight; }

    // ----- Text -----

    std::string TextProperties(const PptxRun& run) {
        std::string props;
        if (run.sizePt > 0) props += " fo:font-size=\"" + Pt(run.sizePt) + "\"";
        if (const std::string c = Colour(run.color); !c.empty()) props += " fo:color=\"" + c + "\"";
        if (run.bold) props += " fo:font-weight=\"bold\"";
        if (run.italic) props += " fo:font-style=\"italic\"";
        if (run.underline || !run.hyperlink.empty())
            props += " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" "
                     "style:text-underline-color=\"font-color\"";
        if (run.strike) props += " style:text-line-through-style=\"solid\"";
        if (run.baseline > 0) props += " style:text-position=\"super 58%\"";
        else if (run.baseline < 0) props += " style:text-position=\"sub 58%\"";
        if (run.allCaps) props += " fo:text-transform=\"uppercase\"";
        else if (run.smallCaps) props += " fo:font-variant=\"small-caps\"";
        if (const std::string c = Colour(run.highlight); !c.empty()) props += " fo:background-color=\"" + c + "\"";
        if (!run.font.empty()) props += " fo:font-family=\"" + Escape(run.font) + "\"";
        return props;
    }

    // Text with ODF's white space kept: a second space, a leading one and
    // a tab are elements of their own.
    static std::string Characters(const std::string& text) {
        std::string out;
        size_t spaces = 0;
        auto flush = [&](bool atEnd) {
            if (spaces == 0) return;
            const bool lead = out.empty();
            if (lead || atEnd) {
                out += "<text:s" + (spaces > 1 ? " text:c=\"" + std::to_string(spaces) + "\"" : std::string()) + "/>";
            } else {
                out += ' ';
                if (spaces > 1) out += "<text:s" + (spaces > 2 ? " text:c=\"" + std::to_string(spaces - 1) + "\"" : std::string()) + "/>";
            }
            spaces = 0;
        };
        for (char c : text) {
            if (c == ' ') {
                ++spaces;
                continue;
            }
            flush(false);
            if (c == '\t') out += "<text:tab/>";
            else if (c == '\n') out += "<text:line-break/>";
            else out += Escape(std::string(1, c));
        }
        flush(true);
        return out;
    }

    std::string Runs(const PptxParagraph& p) {
        std::string out;
        for (const PptxRun& run : p.runs) {
            if (run.text == "\n") {
                out += "<text:line-break/>";
                continue;
            }
            if (run.text.empty()) continue;
            const std::string props = TextProperties(run);
            std::string span = Characters(run.text);
            // A link inside the run's span, as Impress keeps it (a span
            // inside a link is dropped).
            if (!run.hyperlink.empty()) {
                std::string href = run.hyperlink;
                int slide = 0;
                if (std::sscanf(href.c_str(), "#slide%d", &slide) == 1) href = "#page" + std::to_string(slide);
                span = "<text:a xlink:type=\"simple\" xlink:href=\"" + Escape(href) + "\">" + span + "</text:a>";
            }
            if (!props.empty()) {
                const std::string name = styles_->Get("text", "T", "<style:text-properties" + props + "/>");
                span = "<text:span text:style-name=\"" + name + "\">" + span + "</text:span>";
            }
            out += span;
        }
        return out;
    }

    static std::string Spacing(const PptxSpacing& spacing, double sizePt) {
        if (spacing.points > 0) return Pt(spacing.points);
        if (spacing.percent > 0) return Pt(spacing.percent * sizePt * 1.2);
        return "";
    }

    std::string ParagraphStyle(const PptxParagraph& p, double sizePt) {
        std::string props;
        static const char* const kAlign[] = {"start", "center", "end", "justify"};
        props += " fo:text-align=\"" + std::string(kAlign[static_cast<int>(p.align)]) + "\"";
        if (!p.bullet) {
            if (p.marginLeft > 0) props += " fo:margin-left=\"" + Cm(static_cast<double>(p.marginLeft)) + "\"";
            if (p.indent != 0) props += " fo:text-indent=\"" + Cm(static_cast<double>(p.indent)) + "\"";
        }
        if (const std::string before = Spacing(p.spaceBefore, sizePt); !before.empty()) props += " fo:margin-top=\"" + before + "\"";
        if (const std::string after = Spacing(p.spaceAfter, sizePt); !after.empty()) props += " fo:margin-bottom=\"" + after + "\"";
        if (p.lineSpacing.percent > 0) props += " fo:line-height=\"" + Fixed(p.lineSpacing.percent * 100.0, 0) + "%\"";
        else if (p.lineSpacing.points > 0) props += " fo:line-height=\"" + Pt(p.lineSpacing.points) + "\"";
        std::string text;
        if (!p.runs.empty()) {
            // The paragraph's own size: what an empty line and the label take.
            PptxRun first = p.runs.front();
            first.bold = first.italic = first.underline = first.strike = false;
            first.baseline = 0;
            first.hyperlink.clear();
            first.highlight.clear();
            const std::string t = TextProperties(first);
            if (!t.empty()) text = "<style:text-properties" + t + "/>";
        }
        return styles_->Get("paragraph", "P", "<style:paragraph-properties" + props + "/>" + text);
    }

    // A list style for one paragraph's label at its level: its character or
    // number format, colour, size and where it hangs.
    std::string ListStyle(const PptxParagraph& p, double sizePt) {
        const int level = std::clamp(p.level, 0, 8) + 1;
        const int64_t margin = p.marginLeft >= 0 ? p.marginLeft : 457200LL * level;
        const int64_t hang = p.indent < 0 ? -p.indent : 228600;
        std::string text = " fo:font-size=\"" + (p.bulletSizePercent > 0 ? Fixed(p.bulletSizePercent, 0) + "%" : std::string("100%")) + "\"";
        if (const std::string c = Colour(p.bulletColor); !c.empty()) text += " fo:color=\"" + c + "\"";
        (void)sizePt;
        const std::string position = "<style:list-level-properties text:space-before=\"" +
                                     Cm(static_cast<double>(std::max<int64_t>(0, margin - hang))) +
                                     "\" text:min-label-width=\"" + Cm(static_cast<double>(hang)) + "\"/>";
        std::string levels;
        for (int l = 1; l <= 9; ++l) {
            if (l != level) {
                levels += "<text:list-level-style-bullet text:level=\"" + std::to_string(l) +
                          "\" text:bullet-char=\"\xE2\x80\xA2\"><style:list-level-properties/></text:list-level-style-bullet>";
                continue;
            }
            if (p.numbered) {
                std::string format, prefix, suffix;
                NumberFormat(p.numberScheme, format, prefix, suffix);
                levels += "<text:list-level-style-number text:level=\"" + std::to_string(l) + "\" style:num-format=\"" +
                          format + "\"" + (prefix.empty() ? "" : " style:num-prefix=\"" + Escape(prefix) + "\"") +
                          " style:num-suffix=\"" + Escape(suffix) + "\">" + position + "<style:text-properties" + text +
                          "/></text:list-level-style-number>";
            } else {
                levels += "<text:list-level-style-bullet text:level=\"" + std::to_string(l) + "\" text:bullet-char=\"" +
                          Escape(BulletText(p)) + "\">" + position + "<style:text-properties" + text +
                          "/></text:list-level-style-bullet>";
            }
        }
        return styles_->List(levels);
    }

    // The paragraphs of a text box, shape or cell; a bulleted one in a list
    // of its own at its level, numbered from where its list has got to.
    std::string Paragraphs(const std::vector<PptxParagraph>& paragraphs, PptxShapeKind kind) {
        std::string out;
        int numbers[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (const PptxParagraph& p : paragraphs) {
            const int level = std::clamp(p.level, 0, 8);
            const double sizePt = !p.runs.empty() && p.runs.front().sizePt > 0 ? p.runs.front().sizePt
                                : kind == PptxShapeKind::Title                  ? 44.0
                                                                                : 18.0;
            const std::string para = "<text:p text:style-name=\"" + ParagraphStyle(p, sizePt) + "\">" + Runs(p) + "</text:p>";
            if (!p.bullet) {
                for (int at = level; at < 9; ++at) numbers[at] = 0;
                out += para;
                continue;
            }
            int number = 0;
            if (p.numbered) {
                number = p.numberStartAt + numbers[level];
                ++numbers[level];
                for (int deeper = level + 1; deeper < 9; ++deeper) numbers[deeper] = 0;
            } else {
                for (int at = level; at < 9; ++at) numbers[at] = 0;
            }
            // Nested to its level: ODF counts a list's level by its depth, an
            // outer item holding only the inner list.
            std::string open, close;
            for (int l = 0; l < level; ++l) {
                open += "<text:list><text:list-item>";
                close += "</text:list-item></text:list>";
            }
            const std::string start = p.numbered ? " text:start-value=\"" + std::to_string(number) + "\"" : std::string();
            if (level == 0) {
                out += "<text:list text:style-name=\"" + ListStyle(p, sizePt) + "\"><text:list-item" + start + ">" + para +
                       "</text:list-item></text:list>";
            } else {
                // The innermost item carries the number.
                open = open.substr(0, open.size() - std::string("<text:list-item>").size()) + "<text:list-item" + start + ">";
                out += "<text:list text:style-name=\"" + ListStyle(p, sizePt) + "\"><text:list-item>" + open + para + close +
                       "</text:list-item></text:list>";
            }
        }
        return out;
    }

    // ----- Shapes -----

    // Where a shape stands, or how it is turned: ODF turns a shape about its
    // top-left corner and then moves it there, so the corner is worked out
    // from the turn about the centre.
    std::string Place(const PptxShape& shape) {
        const double w = static_cast<double>(std::max<int64_t>(0, shape.width));
        const double h = static_cast<double>(std::max<int64_t>(0, shape.height));
        std::string out = " svg:width=\"" + Cm(w) + "\" svg:height=\"" + Cm(h) + "\"";
        const double turn = std::fmod(shape.rotationDeg, 360.0);
        if (std::abs(turn) < 0.01) return out + " svg:x=\"" + Cm(static_cast<double>(shape.x)) + "\" svg:y=\"" +
                                              Cm(static_cast<double>(shape.y)) + "\"";
        const double a = turn * kPi / 180.0;   // clockwise on the page
        const double cx = static_cast<double>(shape.x) + w / 2, cy = static_cast<double>(shape.y) + h / 2;
        const double x = cx + (-w / 2) * std::cos(a) - (-h / 2) * std::sin(a);
        const double y = cy + (-w / 2) * std::sin(a) + (-h / 2) * std::cos(a);
        return out + " draw:transform=\"rotate (" + Fixed(-a, 6) + ") translate (" + Cm(x) + " " + Cm(y) + ")\"";
    }

    std::string GraphicProperties(const PptxShape& shape, bool text) {
        std::string props;
        if (shape.gradientStops.size() >= 2) {
            const std::string from = Colour(shape.gradientStops.front().color), to = Colour(shape.gradientStops.back().color);
            // Mirrored with the shape, as PowerPoint mirrors it (ODF's mirror
            // would leave the fill alone).
            double angle = shape.gradientAngleDeg;
            if (shape.flipH) angle = 180.0 - angle;
            if (shape.flipV) angle = -angle;
            // ODF's angle is on the page: the shape's turn goes into it, as
            // LibreOffice reads an Office deck.
            if (!shape.gradientRadial) angle += shape.rotationDeg;
            // A radial gradient starts at the edge in ODF, at the centre in the model.
            if (!from.empty() && !to.empty())
                props += " draw:fill=\"gradient\" draw:fill-gradient-name=\"" +
                         (shape.gradientRadial ? styles_->Gradient(to, from, angle, true)
                                               : styles_->Gradient(from, to, angle, false)) + "\"";
        } else if (const std::string fill = Colour(shape.fill); !fill.empty()) {
            props += " draw:fill=\"solid\" draw:fill-color=\"" + fill + "\"";
        } else {
            props += " draw:fill=\"none\"";
        }
        if (const std::string line = Colour(shape.line); !line.empty()) {
            props += std::string(" draw:stroke=\"") + (shape.dash == RichShape::Dash::Solid ? "solid" : "dash") + "\"";
            if (shape.dash == RichShape::Dash::Dashed) props += " draw:stroke-dash=\"Dash\"";
            else if (shape.dash == RichShape::Dash::Dotted) props += " draw:stroke-dash=\"Dot\"";
            props += " svg:stroke-color=\"" + line + "\" svg:stroke-width=\"" +
                     Cm((shape.lineWidthPt > 0 ? shape.lineWidthPt : 0.75) * kEmuPerPoint) + "\"";
            const std::string arrow = Cm(std::max(0.75, shape.lineWidthPt) * kEmuPerPoint * 3.5);
            if (shape.arrowStart) props += " draw:marker-start=\"Arrow\" draw:marker-start-width=\"" + arrow + "\"";
            if (shape.arrowEnd) props += " draw:marker-end=\"Arrow\" draw:marker-end-width=\"" + arrow + "\"";
        } else {
            props += " draw:stroke=\"none\"";
        }
        if (text) {
            static const char* const kAnchor[] = {"top", "middle", "bottom"};
            props += " draw:textarea-vertical-align=\"" + std::string(kAnchor[static_cast<int>(shape.anchor)]) + "\"";
            auto inset = [&](const char* side, int64_t value, int64_t fallback) {
                props += std::string(" fo:padding-") + side + "=\"" + Cm(static_cast<double>(value >= 0 ? value : fallback)) + "\"";
            };
            inset("left", shape.insetLeft, 91440);
            inset("right", shape.insetRight, 91440);
            inset("top", shape.insetTop, 45720);
            inset("bottom", shape.insetBottom, 45720);
            props += " draw:auto-grow-height=\"false\" draw:auto-grow-width=\"false\"";
            if (!shape.wrap) props += " fo:wrap-option=\"no-wrap\"";
            // Shrunk to fit only where the deck says so (Impress shrinks an
            // outline on its own unless told not to).
            if (shape.fontScale > 0 && shape.fontScale < 0.999f)
                props += " draw:fit-to-size=\"shrink-to-fit\" style:shrink-to-fit=\"true\"";
            else
                props += " draw:fit-to-size=\"false\" style:shrink-to-fit=\"false\"";
        }
        return "<style:graphic-properties" + props + "/>";
    }

    std::string Name(const PptxShape& shape, const char* kind) {
        ++shapeCount_;
        return " draw:name=\"" + Escape(shape.name.empty() ? std::string(kind) + " " + std::to_string(shapeCount_) : shape.name) + "\"";
    }

    std::string Shape(const PptxShape& shape) {
        switch (shape.kind) {
            case PptxShapeKind::Title:
            case PptxShapeKind::Subtitle:
            case PptxShapeKind::Body: return PresentationFrame(shape);
            case PptxShapeKind::Picture: return Picture(shape);
            case PptxShapeKind::Table: return Table(shape);
            case PptxShapeKind::Chart: return Chart(shape);
            case PptxShapeKind::Connector: return Connector(shape);
            default: return Drawn(shape);
        }
    }

    // A title, subtitle or outline: a presentation frame holding a text box.
    std::string PresentationFrame(const PptxShape& shape) {
        const char* cls = shape.kind == PptxShapeKind::Title ? "title"
                        : shape.kind == PptxShapeKind::Subtitle ? "subtitle"
                                                                : "outline";
        const std::string style = styles_->Get("presentation", "pr", GraphicProperties(shape, true), ParentStyle(cls));
        PptxShape placed = shape;
        if (placed.width <= 0 || placed.height <= 0) {
            // No place anywhere: where PowerPoint's own layout puts it.
            const int64_t w = Width(), h = Height();
            placed.x = w / 13;
            placed.width = w - 2 * (w / 13);
            placed.y = shape.kind == PptxShapeKind::Title ? h / 19 : h / 4;
            placed.height = shape.kind == PptxShapeKind::Title ? h / 6 : h * 2 / 3;
        }
        return std::string("<draw:frame presentation:style-name=\"") + style + "\" draw:layer=\"layout\"" + Name(shape, cls) +
               Place(placed) + " presentation:class=\"" + cls + "\" presentation:user-transformed=\"true\"><draw:text-box>" +
               Paragraphs(shape.paragraphs, shape.kind) + "</draw:text-box></draw:frame>";
    }

    // A text box, or a shape with a fill, an outline or an outline of its own.
    std::string Drawn(const PptxShape& shape) {
        const bool plainBox = shape.customPath.empty() && (shape.geometry.empty() || shape.geometry == "rect") &&
                              Colour(shape.fill).empty() && shape.gradientStops.size() < 2 && Colour(shape.line).empty() &&
                              !shape.flipH && !shape.flipV && shape.textRotation == 0;
        const std::string style = styles_->Get("graphic", "gr", GraphicProperties(shape, true));
        if (plainBox) {
            return "<draw:frame draw:style-name=\"" + style + "\" draw:layer=\"layout\"" + Name(shape, "TextBox") +
                   Place(shape) + "><draw:text-box>" + Paragraphs(shape.paragraphs, shape.kind) + "</draw:text-box></draw:frame>";
        }
        std::string geometry;
        RichShape outline;
        if (!shape.customPath.empty()) {
            outline.kind = RichShape::Kind::Path;
            outline.path = shape.customPath;
        } else if (!outline.ApplyPreset(shape.geometry.empty() ? "rect" : shape.geometry)) {
            outline.kind = RichShape::Kind::Rectangle;
        }
        switch (outline.kind) {
            case RichShape::Kind::Ellipse: geometry = " draw:type=\"ellipse\""; break;
            case RichShape::Kind::RoundRectangle: geometry = " draw:type=\"round-rectangle\""; break;
            case RichShape::Kind::Line: {
                // From the corner its mirroring puts it at.
                const std::string x0 = shape.flipH ? "21600" : "0", x1 = shape.flipH ? "0" : "21600";
                const std::string y0 = shape.flipV ? "21600" : "0", y1 = shape.flipV ? "0" : "21600";
                geometry = " draw:type=\"non-primitive\" draw:enhanced-path=\"M " + x0 + " " + y0 + " L " + x1 + " " + y1 + " F N\"";
                break;
            }
            case RichShape::Kind::Path: {
                // Mirrored in the shape's own space before it is turned, as
                // PowerPoint mirrors it - ODF's mirror comes after the turn.
                std::vector<RichShape::PathPoint> path = outline.path;
                for (RichShape::PathPoint& point : path) {
                    if (shape.flipH) {
                        point.x = 1.0f - point.x;
                        point.c1x = 1.0f - point.c1x;
                        point.c2x = 1.0f - point.c2x;
                    }
                    if (shape.flipV) {
                        point.y = 1.0f - point.y;
                        point.c1y = 1.0f - point.c1y;
                        point.c2y = 1.0f - point.c2y;
                    }
                }
                bool open = false;
                geometry = " draw:type=\"non-primitive\" draw:enhanced-path=\"" + EnhancedPath(path, open) + "\"";
                break;
            }
            default: geometry = " draw:type=\"rectangle\""; break;
        }
        // Vertical text: the text area turned (counter-clockwise; PowerPoint's
        // vert reads downwards, vert270 upwards).
        std::string textTurn;
        if (shape.textRotation == 90) textTurn = " draw:text-rotate-angle=\"270\"";
        else if (shape.textRotation == 270) textTurn = " draw:text-rotate-angle=\"90\"";
        return "<draw:custom-shape draw:style-name=\"" + style + "\" draw:layer=\"layout\"" + Name(shape, "Shape") +
               Place(shape) + ">" + Paragraphs(shape.paragraphs, shape.kind) +
               "<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\"" + geometry + textTurn + "/></draw:custom-shape>";
    }

    // A line or connector: a straight one as a line between its ends, any
    // other as a path, its arrowheads as markers.
    std::string Connector(const PptxShape& shape) {
        PptxShape lined = shape;
        if (Colour(lined.line).empty()) lined.line = "000000";
        const std::string style = styles_->Get("graphic", "gr", GraphicProperties(lined, false));
        RichShape outline;
        if (!shape.customPath.empty()) {
            outline.kind = RichShape::Kind::Path;
            outline.path = shape.customPath;
        } else if (!outline.ApplyPreset(shape.geometry.empty() ? "line" : shape.geometry) ||
                   (outline.kind != RichShape::Kind::Path && outline.kind != RichShape::Kind::Line)) {
            outline.kind = RichShape::Kind::Line;
        }
        // The ends on the page: mirrored, then turned about the centre.
        const double w = static_cast<double>(shape.width), h = static_cast<double>(shape.height);
        const double cx = static_cast<double>(shape.x) + w / 2, cy = static_cast<double>(shape.y) + h / 2;
        const double a = shape.rotationDeg * kPi / 180.0;
        auto onPage = [&](double fx, double fy, double& px, double& py) {
            if (shape.flipH) fx = 1.0 - fx;
            if (shape.flipV) fy = 1.0 - fy;
            const double dx = (fx - 0.5) * w, dy = (fy - 0.5) * h;
            px = cx + dx * std::cos(a) - dy * std::sin(a);
            py = cy + dx * std::sin(a) + dy * std::cos(a);
        };
        if (outline.kind == RichShape::Kind::Line) {
            double x1, y1, x2, y2;
            onPage(0, 0, x1, y1);
            onPage(1, 1, x2, y2);
            return "<draw:line draw:style-name=\"" + style + "\" draw:layer=\"layout\"" + Name(shape, "Line") +
                   " svg:x1=\"" + Cm(x1) + "\" svg:y1=\"" + Cm(y1) + "\" svg:x2=\"" + Cm(x2) + "\" svg:y2=\"" + Cm(y2) +
                   "\"><text:p/></draw:line>";
        }
        // A path through its points on the page, in a box round them.
        std::vector<std::pair<double, double>> points;
        std::string d;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        using Op = RichShape::PathPoint::Op;
        struct Step {
            Op op;
            double c1x, c1y, c2x, c2y, x, y;
        };
        std::vector<Step> steps;
        for (const RichShape::PathPoint& p : outline.path) {
            Step s{p.op, 0, 0, 0, 0, 0, 0};
            onPage(p.x, p.y, s.x, s.y);
            if (p.op == Op::Curve) {
                onPage(p.c1x, p.c1y, s.c1x, s.c1y);
                onPage(p.c2x, p.c2y, s.c2x, s.c2y);
            }
            if (p.op != Op::Close) {
                minX = std::min(minX, s.x);
                minY = std::min(minY, s.y);
                maxX = std::max(maxX, s.x);
                maxY = std::max(maxY, s.y);
            }
            steps.push_back(s);
        }
        if (steps.empty()) return "";
        const double bw = std::max(1.0, maxX - minX), bh = std::max(1.0, maxY - minY);
        // The path in 1/100 mm from the box's corner, as svg:d is read.
        auto v = [](double emu) { return std::to_string(static_cast<long long>(std::llround(emu / 360.0))); };
        for (const Step& s : steps) {
            switch (s.op) {
                case Op::Move: d += "M" + v(s.x - minX) + " " + v(s.y - minY); break;
                case Op::Line: d += " L" + v(s.x - minX) + " " + v(s.y - minY); break;
                case Op::Curve:
                    d += " C" + v(s.c1x - minX) + " " + v(s.c1y - minY) + " " + v(s.c2x - minX) + " " + v(s.c2y - minY) +
                         " " + v(s.x - minX) + " " + v(s.y - minY);
                    break;
                case Op::Close: d += " Z"; break;
            }
        }
        return "<draw:path draw:style-name=\"" + style + "\" draw:layer=\"layout\"" + Name(shape, "Connector") +
               " svg:x=\"" + Cm(minX) + "\" svg:y=\"" + Cm(minY) + "\" svg:width=\"" + Cm(bw) + "\" svg:height=\"" + Cm(bh) +
               "\" svg:viewBox=\"0 0 " + v(bw) + " " + v(bh) + "\" svg:d=\"" + d + "\"><text:p/></draw:path>";
    }

    std::string Picture(const PptxShape& shape) {
        const auto bytes = deck_.pictures.find(shape.pictureTarget);
        if (bytes == deck_.pictures.end() || bytes->second.empty()) {
            // Its bytes were not kept: a grey box with its description, where it stood.
            PptxShape box = shape;
            box.kind = PptxShapeKind::Text;
            box.fill = "ECEEF1";
            box.line = "C8CCD2";
            box.anchor = PptxAnchor::Middle;
            PptxParagraph p;
            p.align = PptxAlign::Center;
            PptxRun run;
            run.text = shape.altText.empty() ? std::string("Picture") : shape.altText;
            run.italic = true;
            run.color = "6B7280";
            p.runs.push_back(run);
            box.paragraphs = {p};
            return Drawn(box);
        }
        PptxShape outlined = shape;
        outlined.fill.clear();
        outlined.gradientStops.clear();
        const std::string style = styles_->Get("graphic", "gr", GraphicProperties(outlined, false));
        std::string image;
        if (flat_) {
            image = "<draw:image><office:binary-data>" + Base64Encode(bytes->second, false) + "</office:binary-data></draw:image>";
        } else {
            std::string& part = pictureParts_[shape.pictureTarget];
            if (part.empty()) {
                part = "Pictures/image" + std::to_string(pictures_.size() + 1) + "." +
                       PictureExtension(shape.pictureTarget, bytes->second);
                pictures_.push_back({part, bytes->second});
            }
            image = "<draw:image xlink:href=\"" + part +
                    "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"><text:p/></draw:image>";
        }
        std::string desc;
        if (!shape.altText.empty()) desc = "<svg:desc>" + Escape(shape.altText) + "</svg:desc>";
        return "<draw:frame draw:style-name=\"" + style + "\" draw:layer=\"layout\"" + Name(shape, "Picture") +
               Place(shape) + ">" + image + desc + "</draw:frame>";
    }

    std::string Chart(const PptxShape& shape) {
        const float wPt = static_cast<float>(shape.width / kEmuPerPoint), hPt = static_cast<float>(shape.height / kEmuPerPoint);
        // A series without a colour of its own takes the one PowerPoint
        // showed it in (the Office palette), not the reader's palette.
        RichChart chart = shape.chart;
        static const char* const kOffice[] = {"#4472C4", "#ED7D31", "#A5A5A5", "#FFC000", "#5B9BD5", "#70AD47"};
        const bool pie = chart.kind == RichChart::Kind::Pie || chart.kind == RichChart::Kind::Doughnut;
        for (size_t i = 0; i < chart.series.size(); ++i)
            if (chart.series[i].color.empty() && !pie) chart.series[i].color = kOffice[i % 6];
        std::string object = WriteOdfChartObject(chart, wPt, hPt);
        std::string inner;
        if (flat_) {
            // The chart document inline: office:document-content as office:document.
            const size_t start = object.find("<office:document-content");
            if (start == std::string::npos) return "";
            object = object.substr(start);
            object.replace(0, std::string("<office:document-content").size(),
                           "<office:document office:mimetype=\"application/vnd.oasis.opendocument.chart\"");
            const size_t end = object.rfind("</office:document-content>");
            if (end != std::string::npos) object.replace(end, std::string("</office:document-content>").size(), "</office:document>");
            inner = "<draw:object>" + object + "</draw:object>";
        } else {
            charts_.push_back(object);
            inner = "<draw:object xlink:href=\"./Object " + std::to_string(charts_.size()) +
                    "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>";
        }
        return "<draw:frame draw:layer=\"layout\"" + Name(shape, "Chart") + Place(shape) + ">" + inner + "</draw:frame>";
    }

    std::string Border(const PptxCellStyle::Border& border) {
        const std::string c = Colour(border.color);
        if (c.empty() || border.widthPt <= 0) return "none";
        return Pt(border.widthPt) + " solid " + c;
    }

    std::string Table(const PptxShape& shape) {
        size_t columns = shape.columnWidths.size();
        for (const auto& row : shape.rows) columns = std::max(columns, row.size());
        if (columns == 0 || shape.rows.empty()) return "";
        std::vector<int64_t> widths = shape.columnWidths;
        if (widths.size() != columns) widths.assign(columns, std::max<int64_t>(1, shape.width) / static_cast<int64_t>(columns));
        const bool styled = shape.cellStyles.size() == shape.rows.size();
        std::vector<std::vector<bool>> covered(shape.rows.size(), std::vector<bool>(columns, false));
        if (styled) {
            for (size_t r = 0; r < shape.rows.size(); ++r)
                for (size_t c = 0; c < columns && c < shape.cellStyles[r].size(); ++c) {
                    const PptxCellStyle& look = shape.cellStyles[r][c];
                    if (look.mergedAway || covered[r][c]) continue;
                    for (int dr = 0; dr < look.rowSpan && r + dr < shape.rows.size(); ++dr)
                        for (int dc = 0; dc < look.gridSpan && c + dc < columns; ++dc)
                            if (dr || dc) covered[r + dr][c + dc] = true;
                }
        }
        // A table without cell looks (from another reader): PowerPoint's
        // default - a dark header row and banded rows in the first accent.
        const auto accentFound = deck_.themeColors.find("accent1");
        const std::string accent = Colour(accentFound == deck_.themeColors.end() ? "4472C4" : accentFound->second).empty()
                                       ? "#4472c4"
                                       : Colour(accentFound->second);
        std::string xml = "<table:table>";
        for (size_t c = 0; c < columns; ++c) {
            const std::string col = styles_->Get("table-column", "co",
                                                "<style:table-column-properties style:column-width=\"" +
                                                    Cm(static_cast<double>(std::max<int64_t>(1, widths[c]))) + "\"/>");
            xml += "<table:table-column table:style-name=\"" + col + "\"/>";
        }
        for (size_t r = 0; r < shape.rows.size(); ++r) {
            const int64_t h = r < shape.rowHeights.size() && shape.rowHeights[r] > 0 ? shape.rowHeights[r] : 370840;
            const std::string row = styles_->Get("table-row", "ro",
                                                "<style:table-row-properties style:row-height=\"" + Cm(static_cast<double>(h)) + "\"/>");
            xml += "<table:table-row table:style-name=\"" + row + "\">";
            for (size_t c = 0; c < columns; ++c) {
                if (covered[r][c]) {
                    xml += "<table:covered-table-cell/>";
                    continue;
                }
                const PptxCellStyle* look = styled && c < shape.cellStyles[r].size() ? &shape.cellStyles[r][c] : nullptr;
                std::string fill, borders, padding = " fo:padding=\"" + Cm(91440) + "\"", anchor = "top";
                if (look) {
                    fill = Colour(look->fill);
                    borders = " fo:border-left=\"" + Border(look->left) + "\" fo:border-right=\"" + Border(look->right) +
                              "\" fo:border-top=\"" + Border(look->top) + "\" fo:border-bottom=\"" + Border(look->bottom) + "\"";
                    padding = " fo:padding-left=\"" + Cm(static_cast<double>(look->insetLeft)) + "\" fo:padding-right=\"" +
                              Cm(static_cast<double>(look->insetRight)) + "\" fo:padding-top=\"" +
                              Cm(static_cast<double>(look->insetTop)) + "\" fo:padding-bottom=\"" +
                              Cm(static_cast<double>(look->insetBottom)) + "\"";
                    static const char* const kAnchor[] = {"top", "middle", "bottom"};
                    anchor = kAnchor[static_cast<int>(look->anchor)];
                } else {
                    fill = r == 0 ? accent : (r % 2 ? "#cfd5ea" : "#e9ebf5");
                    borders = " fo:border=\"0.75pt solid #ffffff\"";
                }
                const std::string cell = styles_->Get(
                    "table-cell", "ce",
                    "<style:graphic-properties draw:fill=\"" + std::string(fill.empty() ? "none" : "solid") + "\"" +
                        (fill.empty() ? "" : " draw:fill-color=\"" + fill + "\"") + "/><style:table-cell-properties" +
                        borders + padding + " style:vertical-align=\"" + anchor + "\"" +
                        (fill.empty() ? "" : " fo:background-color=\"" + fill + "\"") + "/>");
                xml += "<table:table-cell table:style-name=\"" + cell + "\"";
                if (look && look->gridSpan > 1) xml += " table:number-columns-spanned=\"" + std::to_string(look->gridSpan) + "\"";
                if (look && look->rowSpan > 1) xml += " table:number-rows-spanned=\"" + std::to_string(look->rowSpan) + "\"";
                xml += ">";
                std::vector<PptxParagraph> text = c < shape.rows[r].size() ? shape.rows[r][c] : std::vector<PptxParagraph>{};
                if (!look && r == 0) {
                    for (PptxParagraph& p : text)
                        for (PptxRun& run : p.runs) {
                            run.bold = true;
                            if (run.color.empty()) run.color = "FFFFFF";
                        }
                }
                xml += text.empty() ? "<text:p/>" : Paragraphs(text, PptxShapeKind::Text);
                xml += "</table:table-cell>";
            }
            xml += "</table:table-row>";
        }
        xml += "</table:table>";
        PptxShape frame = shape;
        if (frame.width <= 0)
            for (int64_t w : widths) frame.width += w;
        if (frame.height <= 0) frame.height = 370840 * static_cast<int64_t>(shape.rows.size());
        return "<draw:frame draw:layer=\"layout\"" + Name(shape, "Table") + Place(frame) + ">" + xml + "</draw:frame>";
    }

    // ----- Masters and layouts -----

    // A style name from a display name, as Impress makes one: what is not a
    // letter, digit, '-' or '.' as _xx_ (its UTF-8 bytes in hex).
    static std::string StyleName(const std::string& display) {
        std::string out;
        for (unsigned char c : display) {
            if (std::isalnum(c) || ((c == '-' || c == '.') && !out.empty())) {
                out += static_cast<char>(c);
            } else {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "_%x_", c);
                out += hex;
            }
        }
        return out;
    }

    // A placeholder's type: the model's, else its kind's; empty for a shape that is none.
    static std::string PlaceholderType(const PptxShape& shape) {
        if (!shape.placeholder.empty()) return shape.placeholder;
        switch (shape.kind) {
            case PptxShapeKind::Title: return "title";
            case PptxShapeKind::Subtitle: return "subTitle";
            case PptxShapeKind::Body: return "body";
            default: return "";
        }
    }

    // A placeholder type as the presentation class of a master page's frame
    // ("title", "subtitle", "outline", "date-time", "footer", "page-number").
    static std::string PresentationClass(const std::string& type) {
        if (type == "title" || type == "ctrTitle") return "title";
        if (type == "subTitle") return "subtitle";
        if (type == "dt") return "date-time";
        if (type == "ftr") return "footer";
        if (type == "sldNum") return "page-number";
        return type.empty() ? "" : "outline";
    }

    // A placeholder type as the object of a presentation page layout's placeholder.
    static std::string LayoutObject(const std::string& type) {
        if (type == "title" || type == "ctrTitle") return "title";
        if (type == "subTitle") return "subtitle";
        if (type == "dt" || type == "ftr" || type == "sldNum" || type.empty()) return "";
        if (type == "pic") return "graphic";
        if (type == "chart") return "chart";
        if (type == "tbl") return "table";
        if (type == "dgm") return "orgchart";
        return "outline";
    }

    // The presentation style a slide's frame of `presentationClass` takes
    // its master's look from ("<master>-title", "-subtitle", "-outline1").
    std::string ParentStyle(const std::string& presentationClass) const {
        if (!ownMasters_ || pageMaster_.empty()) return "";
        const std::string suffix = presentationClass == "title"      ? "title"
                                 : presentationClass == "subtitle"   ? "subtitle"
                                 : presentationClass == "outline"    ? "outline1"
                                                                     : "";
        return suffix.empty() ? "" : " style:parent-style-name=\"" + pageMaster_ + "-" + suffix + "\"";
    }

    // The common presentation styles of master `name`: its title's,
    // subtitle's and outline levels' look, from its placeholders' text.
    std::string MasterPresentationStyles(const std::string& name, const std::string& display, const PptxMaster& master) {
        const PptxShape* title = nullptr;
        const PptxShape* subtitle = nullptr;
        const PptxShape* body = nullptr;
        for (const PptxShape& shape : master.placeholders) {
            const std::string cls = PresentationClass(PlaceholderType(shape));
            if (cls == "title" && !title) title = &shape;
            else if (cls == "subtitle" && !subtitle) subtitle = &shape;
            else if (cls == "outline" && !body) body = &shape;
        }
        auto look = [&](const PptxParagraph* p) {
            if (!p) return std::string();
            static const char* const kAlign[] = {"start", "center", "end", "justify"};
            std::string xml = "<style:paragraph-properties fo:text-align=\"" +
                              std::string(kAlign[static_cast<int>(p->align)]) + "\"/>";
            if (!p->runs.empty()) {
                PptxRun run = p->runs.front();
                run.hyperlink.clear();
                xml += "<style:text-properties" + TextProperties(run) + "/>";
            }
            return xml;
        };
        auto first = [](const PptxShape* shape, int level) -> const PptxParagraph* {
            if (!shape) return nullptr;
            for (const PptxParagraph& p : shape->paragraphs)
                if (p.level == level) return &p;
            return nullptr;
        };
        auto style = [&](const std::string& suffix, const std::string& parent, const std::string& graphic,
                         const PptxParagraph* p) {
            return "<style:style style:name=\"" + name + "-" + suffix + "\" style:display-name=\"" + Escape(display) + "-" +
                   suffix + "\" style:family=\"presentation\"" +
                   (parent.empty() ? std::string() : " style:parent-style-name=\"" + name + "-" + parent + "\"") + ">" +
                   graphic + look(p) + "</style:style>";
        };
        const std::string box = "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\" "
                                "draw:textarea-vertical-align=\"";
        std::string xml = style("background", "", "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>",
                                nullptr) +
                          style("backgroundobjects", "", "", nullptr) +
                          style("title", "", box + "middle\"/>", first(title, 0)) +
                          style("subtitle", "", box + "middle\"/>", first(subtitle ? subtitle : body, 0)) +
                          style("notes", "", "", nullptr);
        for (int level = 0; level < 9; ++level)
            xml += style("outline" + std::to_string(level + 1), level ? "outline" + std::to_string(level) : "",
                         level ? "" : box + "top\"/>", first(body, level));
        return xml;
    }

    // A master page's frame for one of its placeholders, in the master's
    // styles: the title, subtitle and outline with their (prompt) text, the
    // date, footer and number as Impress's fields.
    std::string MasterFrame(const PptxShape& shape, const std::string& masterName) {
        const std::string type = PlaceholderType(shape);
        const std::string cls = PresentationClass(type);
        if (cls.empty() || shape.kind == PptxShapeKind::Picture || shape.kind == PptxShapeKind::Table ||
            shape.kind == PptxShapeKind::Chart || shape.kind == PptxShapeKind::Connector) {
            std::string xml = Shape(shape);   // decoration on the master
            const size_t layer = xml.find("draw:layer=\"layout\"");
            if (layer != std::string::npos) xml.replace(layer, 19, "draw:layer=\"backgroundobjects\"");
            return xml;
        }
        const std::string parent = cls == "title" || cls == "subtitle" || cls == "outline"
                                       ? " style:parent-style-name=\"" + masterName + "-" +
                                             (cls == "outline" ? std::string("outline1") : cls) + "\""
                                       : std::string();
        const std::string style = styles_->Get("presentation", "pr", GraphicProperties(shape, true), ownMasters_ ? parent : "");
        std::string text;
        const char* field = cls == "date-time" ? "<presentation:date-time/>"
                          : cls == "footer"    ? "<presentation:footer/>"
                          : cls == "page-number" ? "<text:page-number>&lt;number&gt;</text:page-number>"
                                                 : nullptr;
        if (field) {
            const PptxParagraph p = shape.paragraphs.empty() ? PptxParagraph() : shape.paragraphs.front();
            std::string span = field;
            for (const PptxRun& run : p.runs) {
                if (run.text.empty() || run.text == "\n") continue;
                PptxRun look = run;
                look.hyperlink.clear();
                if (const std::string props = TextProperties(look); !props.empty())
                    span = "<text:span text:style-name=\"" +
                           styles_->Get("text", "T", "<style:text-properties" + props + "/>") + "\">" + field + "</text:span>";
                break;
            }
            const double sizePt = !p.runs.empty() && p.runs.front().sizePt > 0 ? p.runs.front().sizePt : 12.0;
            text = "<text:p text:style-name=\"" + ParagraphStyle(p, sizePt) + "\">" + span + "</text:p>";
        } else {
            text = Paragraphs(shape.paragraphs, shape.kind);
        }
        PptxShape placed = shape;
        if (placed.width <= 0 || placed.height <= 0) {
            const int64_t w = Width(), h = Height();
            placed.x = w / 13;
            placed.width = w - 2 * (w / 13);
            placed.y = cls == "title" ? h / 19 : cls == "outline" || cls == "subtitle" ? h / 4 : h * 11 / 12;
            placed.height = cls == "title" ? h / 6 : cls == "outline" || cls == "subtitle" ? h * 2 / 3 : h / 16;
        }
        return "<draw:frame presentation:style-name=\"" + style + "\" draw:layer=\"backgroundobjects\"" +
               Name(shape, cls.c_str()) + Place(placed) + " presentation:class=\"" + cls + "\"><draw:text-box>" + text +
               "</draw:text-box></draw:frame>";
    }

    // The master pages (the deck's masters, else one plain "Default") with
    // their presentation styles, and the deck's layouts as presentation
    // page layouts ("UCL1" ...) named as the deck names them.
    void BuildDesign() {
        styles_ = &masterStyles_;
        const int shapeCount = shapeCount_;
        ownMasters_ = !deck_.masters.empty();
        const size_t count = std::max<size_t>(1, deck_.masters.size());
        std::set<std::string> taken;
        for (size_t m = 0; m < count; ++m) {
            const PptxMaster master = ownMasters_ ? deck_.masters[m] : PptxMaster();
            std::string display = master.name.empty() ? (ownMasters_ ? "Master " + std::to_string(m + 1) : "Default")
                                                      : master.name;
            std::string name = StyleName(display);
            for (int n = 2; taken.count(name); ++n) name = StyleName(display) + "_" + std::to_string(n);
            taken.insert(name);
            masterNames_.push_back(name);
            const std::string bg = Colour(master.background);
            const std::string pageStyle = styles_->Get(
                "drawing-page", "dp", "<style:drawing-page-properties draw:background-size=\"full\" draw:fill=\"solid\" "
                                      "draw:fill-color=\"" + (bg.empty() ? std::string("#ffffff") : bg) + "\"/>");
            std::string frames;
            for (const PptxShape& shape : master.placeholders) frames += MasterFrame(shape, name);
            masterPages_ += "<style:master-page style:name=\"" + name + "\"" +
                            (name != display ? " style:display-name=\"" + Escape(display) + "\"" : std::string()) +
                            " style:page-layout-name=\"PM1\" draw:style-name=\"" + pageStyle + "\">" + frames +
                            "<presentation:notes style:page-layout-name=\"PM2\"/></style:master-page>";
            if (ownMasters_) designCommon_ += MasterPresentationStyles(name, display, master);
        }
        for (size_t i = 0; i < deck_.layouts.size(); ++i) {
            const PptxLayout& layout = deck_.layouts[i];
            std::string xml = "<style:presentation-page-layout style:name=\"UCL" + std::to_string(i + 1) + "\"" +
                              (layout.name.empty() ? std::string() : " style:display-name=\"" + Escape(layout.name) + "\"") +
                              ">";
            for (const PptxShape& shape : layout.placeholders) {
                const std::string object = LayoutObject(PlaceholderType(shape));
                if (object.empty()) continue;
                xml += "<presentation:placeholder presentation:object=\"" + object + "\" svg:x=\"" +
                       Cm(static_cast<double>(shape.x)) + "\" svg:y=\"" + Cm(static_cast<double>(shape.y)) +
                       "\" svg:width=\"" + Cm(static_cast<double>(std::max<int64_t>(0, shape.width))) + "\" svg:height=\"" +
                       Cm(static_cast<double>(std::max<int64_t>(0, shape.height))) + "\"/>";
            }
            designCommon_ += xml + "</style:presentation-page-layout>";
        }
        shapeCount_ = shapeCount;
        styles_ = &contentStyles_;
    }

    // The slide's transition as drawing-page properties (smil:type and
    // subtype, the speed, automatic advance after its duration), and its
    // exact length for the page's timing root; empty type for none.
    struct TransitionOut {
        std::string properties, type, subtype;
        double seconds = 0;
    };
    static TransitionOut Transition(const PptxTransition& t) {
        TransitionOut out;
        switch (t.kind) {
            case PptxTransitionKind::Fade: out.type = "fade"; out.subtype = "crossfade"; break;
            case PptxTransitionKind::Push: out.type = "pushWipe"; out.subtype = "fromRight"; break;
            case PptxTransitionKind::Wipe: out.type = "barWipe"; out.subtype = "leftToRight"; break;
            case PptxTransitionKind::Split: out.type = "barnDoorWipe"; out.subtype = "vertical"; break;
            case PptxTransitionKind::Cover: out.type = "slideWipe"; out.subtype = "fromRight"; break;
            case PptxTransitionKind::Zoom: out.type = "zoom"; break;
            case PptxTransitionKind::NoTransition: break;
        }
        out.seconds = t.durationSec > 0 ? t.durationSec : 0.5;
        if (!out.type.empty()) {
            const char* speed = out.seconds <= 0.5 ? "fast" : out.seconds <= 0.75 ? "medium" : "slow";
            out.properties += std::string(" presentation:transition-speed=\"") + speed + "\" smil:type=\"" + out.type + "\"";
            if (!out.subtype.empty()) out.properties += " smil:subtype=\"" + out.subtype + "\"";
        }
        if (t.advanceAfterSec >= 0)
            out.properties += " presentation:transition-type=\"automatic\" presentation:duration=\"PT" +
                              Fixed(t.advanceAfterSec, 3) + "S\"";
        return out;
    }

    std::string Page(const PptxSlide& slide, int number) {
        // Its layout (the first when it names none) and that layout's master.
        int layout = -1;
        if (!deck_.layouts.empty())
            layout = slide.layoutIndex >= 0 && slide.layoutIndex < static_cast<int>(deck_.layouts.size()) ? slide.layoutIndex : 0;
        size_t master = 0;
        if (layout >= 0 && ownMasters_)
            master = static_cast<size_t>(std::clamp(deck_.layouts[static_cast<size_t>(layout)].masterIndex, 0,
                                                    static_cast<int>(masterNames_.size()) - 1));
        pageMaster_ = masterNames_.empty() ? std::string("Default") : masterNames_[master];
        const TransitionOut transition = Transition(slide.transition);
        std::string page = "<style:drawing-page-properties presentation:background-visible=\"true\" "
                           "presentation:background-objects-visible=\"true\"";
        if (const std::string bg = Colour(slide.background); !bg.empty())
            page += " draw:background-size=\"full\" draw:fill=\"solid\" draw:fill-color=\"" + bg + "\"";
        if (slide.hidden) page += " presentation:visibility=\"hidden\"";
        page += transition.properties;
        const std::string style = styles_->Get("drawing-page", "dp", page + "/>");
        const std::string id = "page" + std::to_string(number);
        std::string xml = "<draw:page draw:name=\"" + id + "\" draw:style-name=\"" + style +
                          "\" draw:master-page-name=\"" + pageMaster_ + "\"";
        if (layout >= 0) xml += " presentation:presentation-page-layout-name=\"UCL" + std::to_string(layout + 1) + "\"";
        if (!transition.type.empty()) xml += " draw:id=\"" + id + "\" xml:id=\"" + id + "\"";
        xml += ">";
        for (const PptxShape& shape : slide.shapes) xml += Shape(shape);
        if (!transition.type.empty()) {
            // The exact length, where Impress keeps it: the page's timing root.
            xml += "<anim:par presentation:node-type=\"timing-root\"><anim:par smil:begin=\"" + id +
                   ".begin\"><anim:transitionFilter smil:dur=\"" + Fixed(transition.seconds, 3) + "s\" smil:type=\"" +
                   transition.type + "\"" +
                   (transition.subtype.empty() ? std::string() : " smil:subtype=\"" + transition.subtype + "\"") +
                   "/></anim:par></anim:par>";
        }
        // The speaker notes: the notes page's notes frame.
        bool notes = false;
        for (const PptxParagraph& p : slide.notes) notes = notes || !p.Text().empty();
        if (notes) {
            xml += "<presentation:notes><draw:page-thumbnail draw:layer=\"layout\" svg:width=\"14.848cm\" "
                   "svg:height=\"8.35cm\" svg:x=\"3.075cm\" svg:y=\"2.257cm\" draw:page-number=\"" + std::to_string(number) +
                   "\" presentation:class=\"page\"/><draw:frame draw:layer=\"layout\" svg:width=\"16.799cm\" "
                   "svg:height=\"13.364cm\" svg:x=\"2.1cm\" svg:y=\"11.731cm\" presentation:class=\"notes\" "
                   "presentation:user-transformed=\"true\"><draw:text-box>" +
                   Paragraphs(slide.notes, PptxShapeKind::Text) + "</draw:text-box></draw:frame></presentation:notes>";
        }
        return xml + "</draw:page>";
    }
};

std::string Manifest(const std::string& mediaType, const OdpWriter& writer) {
    std::string xml = std::string(kXmlHead) +
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"" + mediaType + "\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
    for (const auto& [part, bytes] : writer.Pictures()) {
        const std::string ext = part.substr(part.rfind('.') + 1);
        xml += "<manifest:file-entry manifest:full-path=\"" + part + "\" manifest:media-type=\"" + PictureMediaType(ext) + "\"/>";
    }
    for (size_t i = 0; i < writer.Charts().size(); ++i) {
        const std::string object = "Object " + std::to_string(i + 1);
        xml += "<manifest:file-entry manifest:full-path=\"" + object +
               "/\" manifest:media-type=\"application/vnd.oasis.opendocument.chart\"/><manifest:file-entry "
               "manifest:full-path=\"" + object + "/content.xml\" manifest:media-type=\"text/xml\"/>";
    }
    return xml + "</manifest:manifest>";
}

std::string Meta(const PptxPresentation& deck) {
    const std::string title = deck.slides.empty() ? std::string() : deck.slides.front().title;
    return std::string(kXmlHead) + "<office:document-meta " + Namespaces() + "><office:meta>"
        "<meta:generator>UltraCanvas</meta:generator>" + (title.empty() ? "" : "<dc:title>" + Escape(title) + "</dc:title>") +
        "</office:meta></office:document-meta>";
}

} // namespace

bool WriteOdpPresentationToMemory(const PptxPresentation& deck, std::vector<uint8_t>& out, std::string& error,
                                  const std::string& extension) {
    out.clear();
    error.clear();
    const std::string ext = Lower(extension);
    const bool flat = ext == "fodp";
    const std::string mediaType = ext == "otp" ? kTemplateType : kPresentationType;
    OdpWriter writer(deck, flat);
    writer.Build();
    const std::string content = writer.Content();

    if (flat) {
        // One XML document: the styles, the master pages and the content together.
        const std::string body = content.substr(content.find("<office:automatic-styles>"));
        const size_t autoEnd = body.find("</office:automatic-styles>");
        std::string xml = std::string(kXmlHead) + "<office:document " + Namespaces() + " office:mimetype=\"" + mediaType +
                          "\"><office:meta><meta:generator>UltraCanvas</meta:generator></office:meta>" +
                          writer.CommonStyles() + "<office:automatic-styles>" + writer.MasterAutomatic() +
                          body.substr(std::string("<office:automatic-styles>").size(),
                                      autoEnd - std::string("<office:automatic-styles>").size()) +
                          "</office:automatic-styles>" + writer.MasterStyles() +
                          body.substr(autoEnd + std::string("</office:automatic-styles>").size());
        const std::string closing = "</office:document-content>";
        const size_t end = xml.rfind(closing);
        if (end != std::string::npos) xml.replace(end, closing.size(), "</office:document>");
        out.assign(xml.begin(), xml.end());
        return true;
    }

    const std::string styles = std::string(kXmlHead) + "<office:document-styles " + Namespaces() + ">" +
                               writer.CommonStyles() + "<office:automatic-styles>" + writer.MasterAutomatic() +
                               "</office:automatic-styles>" + writer.MasterStyles() + "</office:document-styles>";
    UCZipPackageWriter zip;
    if (!zip.OpenInMemory()) {
        error = "The presentation could not be written: no memory for the package";
        return false;
    }
    // The media type first and stored, as ODF wants it.
    bool ok = zip.AddEntry("mimetype", mediaType, false) && zip.AddEntry("content.xml", content) &&
              zip.AddEntry("styles.xml", styles) && zip.AddEntry("meta.xml", Meta(deck));
    for (const auto& [part, bytes] : writer.Pictures())
        ok = ok && zip.AddEntry(part, std::string(bytes.begin(), bytes.end()), false);
    for (size_t i = 0; i < writer.Charts().size(); ++i)
        ok = ok && zip.AddEntry("Object " + std::to_string(i + 1) + "/content.xml", writer.Charts()[i]);
    ok = ok && zip.AddEntry("META-INF/manifest.xml", Manifest(mediaType, writer)) && zip.Finalize();
    if (!ok) {
        error = "The presentation could not be written: the package did not close";
        return false;
    }
    out = zip.TakeBytes();
    return !out.empty();
}

bool WriteOdpPresentation(const PptxPresentation& deck, const std::string& filePath, std::string& error) {
    const size_t dot = filePath.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string("odp") : filePath.substr(dot + 1);
    std::vector<uint8_t> bytes;
    if (!WriteOdpPresentationToMemory(deck, bytes, error, IsOdpExtension(extension) ? extension : "odp")) {
        error += ": " + filePath;
        return false;
    }
    std::ofstream file(PathFromUtf8(filePath), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "The presentation could not be saved (the file could not be created): " + filePath;
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "The presentation could not be saved (the disk refused the write): " + filePath;
        return false;
    }
    return true;
}

} // namespace UltraCanvas

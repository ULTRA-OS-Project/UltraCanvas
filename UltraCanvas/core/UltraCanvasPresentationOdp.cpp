// core/UltraCanvasPresentationOdp.cpp
// OpenDocument presentation (.odp / .otp / .fodp) reader - see UltraCanvasPresentationOdp.h.
// Version: 1.1.0 - a password-protected deck opened with its password (UltraCanvasOdfEncryption.h)
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasOdfEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasZipPackage.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace UltraCanvas {

namespace {

constexpr const char* kPresentationMediaType = "application/vnd.oasis.opendocument.presentation";

const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e->Name();
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

bool Is(const tinyxml2::XMLElement* e, const char* localName) {
    return e && std::strcmp(LocalName(e), localName) == 0;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* parent, const char* localName) {
    for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement())
        if (Is(e, localName)) return e;
    return nullptr;
}

// The attribute by its local name. ODF writers use the standard prefixes,
// and `prefix` picks the one meant where an element could carry the same
// local name twice (draw:style-name beside presentation:style-name); a
// writer with prefixes of its own still finds the attribute by local name.
std::string Attr(const tinyxml2::XMLElement* e, const char* localName, const char* prefix = nullptr) {
    const char* anyPrefix = nullptr;
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strrchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, localName) != 0) continue;
        if (!prefix) return a->Value();
        const size_t length = colon ? static_cast<size_t>(colon - name) : 0;
        if (length == std::strlen(prefix) && std::strncmp(name, prefix, length) == 0) return a->Value();
        if (!anyPrefix) anyPrefix = a->Value();
    }
    return anyPrefix ? anyPrefix : "";
}

// An ODF length ("2.54cm", "10mm", "1in", "72pt", "6pc", "96px") in EMU
// (914400 per inch); 0 for none or one it cannot read.
int64_t LengthToEmu(const std::string& value) {
    size_t unit = 0;
    while (unit < value.size() && (std::isdigit(static_cast<unsigned char>(value[unit])) ||
                                   value[unit] == '.' || value[unit] == '-' || value[unit] == '+'))
        ++unit;
    double number = 0.0;
    if (unit == 0 || !TryParseFloat(value.substr(0, unit), number)) return 0;
    const std::string suffix = value.substr(unit);
    double perUnit = 0.0;
    if (suffix == "cm") perUnit = 360000.0;
    else if (suffix == "mm") perUnit = 36000.0;
    else if (suffix == "in" || suffix == "inch") perUnit = 914400.0;
    else if (suffix == "pt") perUnit = 12700.0;
    else if (suffix == "pc") perUnit = 152400.0;
    else if (suffix == "px") perUnit = 9525.0;
    else return 0;
    return static_cast<int64_t>(std::llround(number * perUnit));
}

// What a style says about a run: -1 where it says nothing, so a parent's or
// the paragraph's value shows through.
struct TextLook {
    int bold = -1, italic = -1, underline = -1;
    void Over(const TextLook& under) {
        if (bold < 0) bold = under.bold;
        if (italic < 0) italic = under.italic;
        if (underline < 0) underline = under.underline;
    }
};

struct NamedTextStyle {
    TextLook look;
    std::string parent;
};

enum class ListLabel { Bullet, Number, None };

struct OdpContext {
    std::map<std::string, NamedTextStyle> textStyles;               // "<family>:<name>"
    std::map<std::string, std::map<int, ListLabel>> listStyles;     // name -> level (1-based) -> label
    std::set<std::string> hiddenPageStyles;                          // drawing-page styles
    std::map<std::string, std::string> masterLayouts;                // master page -> page layout
    std::map<std::string, std::pair<int64_t, int64_t>> pageLayouts;  // page layout -> width, height
    std::string firstMaster;
};

TextLook LookOf(const tinyxml2::XMLElement* textProperties) {
    TextLook look;
    if (!textProperties) return look;
    const std::string weight = Attr(textProperties, "font-weight", "fo");
    if (weight == "bold") look.bold = 1;
    else if (weight == "normal") look.bold = 0;
    else if (!weight.empty()) {
        double numeric = 0.0;
        if (TryParseFloat(weight, numeric)) look.bold = numeric >= 600.0 ? 1 : 0;
    }
    const std::string style = Attr(textProperties, "font-style", "fo");
    if (style == "italic" || style == "oblique") look.italic = 1;
    else if (style == "normal") look.italic = 0;
    const std::string underline = Attr(textProperties, "text-underline-style", "style");
    if (!underline.empty()) look.underline = underline == "none" ? 0 : 1;
    return look;
}

// The styles a container holds (office:styles, office:automatic-styles):
// paragraph and text styles, list styles, drawing-page styles and page
// layouts. A later container's style of the same name replaces an earlier
// one's, which is how content.xml's automatic styles sit over styles.xml.
void CollectStyles(const tinyxml2::XMLElement* container, OdpContext& context, bool textStyles) {
    for (auto* s = container ? container->FirstChildElement() : nullptr; s; s = s->NextSiblingElement()) {
        if (Is(s, "style")) {
            const std::string family = Attr(s, "family", "style");
            const std::string name = Attr(s, "name", "style");
            if (name.empty()) continue;
            if (family == "drawing-page") {
                if (Attr(Child(s, "drawing-page-properties"), "visibility", "presentation") == "hidden")
                    context.hiddenPageStyles.insert(name);
                else
                    context.hiddenPageStyles.erase(name);
            } else if (textStyles && (family == "paragraph" || family == "text")) {
                NamedTextStyle style;
                style.look = LookOf(Child(s, "text-properties"));
                style.parent = Attr(s, "parent-style-name", "style");
                context.textStyles[family + ":" + name] = std::move(style);
            }
        } else if (Is(s, "list-style") && textStyles) {
            std::map<int, ListLabel> levels;
            for (auto* level = s->FirstChildElement(); level; level = level->NextSiblingElement()) {
                double number = 0.0;
                const std::string levelText = Attr(level, "level", "text");
                if (!TryParseFloat(levelText, number)) continue;
                ListLabel label = ListLabel::Bullet;
                if (Is(level, "list-level-style-number"))
                    label = Attr(level, "num-format", "style").empty() ? ListLabel::None : ListLabel::Number;
                else if (Is(level, "list-level-style-bullet"))
                    label = Attr(level, "bullet-char", "text").empty() ? ListLabel::None : ListLabel::Bullet;
                levels[static_cast<int>(number)] = label;
            }
            context.listStyles[Attr(s, "name", "style")] = std::move(levels);
        } else if (Is(s, "page-layout")) {
            const tinyxml2::XMLElement* props = Child(s, "page-layout-properties");
            context.pageLayouts[Attr(s, "name", "style")] = {LengthToEmu(Attr(props, "page-width", "fo")),
                                                            LengthToEmu(Attr(props, "page-height", "fo"))};
        }
    }
}

void CollectMasters(const tinyxml2::XMLElement* masterStyles, OdpContext& context) {
    for (auto* m = Child(masterStyles, "master-page"); m; m = m->NextSiblingElement()) {
        if (!Is(m, "master-page")) continue;
        const std::string name = Attr(m, "name", "style");
        if (context.firstMaster.empty()) context.firstMaster = name;
        context.masterLayouts[name] = Attr(m, "page-layout-name", "style");
    }
}

class OdpReader {
public:
    explicit OdpReader(const OdpContext& context) : context_(context) {}

    void ReadPresentation(const tinyxml2::XMLElement* presentation, PptxPresentation& out) {
        std::string sizeMaster;
        int number = 0;
        for (auto* page = Child(presentation, "page"); page; page = page->NextSiblingElement()) {
            if (!Is(page, "page")) continue;
            if (sizeMaster.empty()) sizeMaster = Attr(page, "master-page-name", "draw");
            PptxSlide slide;
            slide.number = ++number;
            slide.hidden = context_.hiddenPageStyles.count(Attr(page, "style-name", "draw")) > 0;
            ReadShapes(page, slide.shapes);
            for (const PptxShape& shape : slide.shapes) {
                if (shape.kind != PptxShapeKind::Title || !slide.title.empty()) continue;
                for (const PptxParagraph& p : shape.paragraphs) {
                    const std::string text = p.Text();
                    if (text.empty()) continue;
                    slide.title += (slide.title.empty() ? "" : " ") + text;
                }
            }
            // The speaker notes: the notes frame of the page's notes page.
            if (const tinyxml2::XMLElement* notes = Child(page, "notes")) {
                std::vector<PptxShape> shapes;
                ReadShapes(notes, shapes);
                for (PptxShape& shape : shapes)
                    if (shape.kind == PptxShapeKind::Body && shape.name == kNotesFrame)
                        for (PptxParagraph& p : shape.paragraphs) slide.notes.push_back(std::move(p));
            }
            out.slides.push_back(std::move(slide));
        }
        // The slide size: the page layout of the first slide's master page.
        auto master = context_.masterLayouts.find(sizeMaster);
        if (master == context_.masterLayouts.end()) master = context_.masterLayouts.find(context_.firstMaster);
        if (master != context_.masterLayouts.end()) {
            const auto layout = context_.pageLayouts.find(master->second);
            if (layout != context_.pageLayouts.end()) {
                out.slideWidth = layout->second.first;
                out.slideHeight = layout->second.second;
            }
        }
    }

private:
    // A notes frame is told apart from the rest of a notes page by this
    // name while the shapes are read; the slide gets only its paragraphs.
    static constexpr const char* kNotesFrame = "\x01notes";

    const OdpContext& context_;

    static bool IsTextShape(const tinyxml2::XMLElement* e) {
        static const char* const kShapes[] = {"custom-shape", "rect", "ellipse", "circle", "polygon",
                                              "polyline", "path", "regular-polygon", "caption",
                                              "connector", "line", "measure"};
        for (const char* name : kShapes)
            if (Is(e, name)) return true;
        return false;
    }

    static void ReadPlace(const tinyxml2::XMLElement* e, PptxShape& shape) {
        shape.x = LengthToEmu(Attr(e, "x", "svg"));
        shape.y = LengthToEmu(Attr(e, "y", "svg"));
        shape.width = LengthToEmu(Attr(e, "width", "svg"));
        shape.height = LengthToEmu(Attr(e, "height", "svg"));
    }

    static bool HasText(const PptxShape& shape) {
        for (const PptxParagraph& p : shape.paragraphs)
            if (!p.Text().empty()) return true;
        return false;
    }

    void ReadShapes(const tinyxml2::XMLElement* parent, std::vector<PptxShape>& out) {
        for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            if (Is(e, "g") || Is(e, "a")) {
                ReadShapes(e, out);   // a group, or shapes behind a hyperlink
            } else if (Is(e, "frame")) {
                ReadFrame(e, out);
            } else if (IsTextShape(e)) {
                PptxShape shape;
                shape.kind = PptxShapeKind::Text;
                shape.name = Attr(e, "name", "draw");
                ReadPlace(e, shape);
                ReadParagraphs(e, shape.paragraphs);
                if (HasText(shape)) out.push_back(std::move(shape));
            }
        }
    }

    void ReadFrame(const tinyxml2::XMLElement* frame, std::vector<PptxShape>& out) {
        const std::string presentationClass = Attr(frame, "class", "presentation");
        // Slide furniture, a notes page's slide picture, and a placeholder
        // still showing its prompt.
        if (presentationClass == "header" || presentationClass == "footer" ||
            presentationClass == "date-time" || presentationClass == "page-number" ||
            presentationClass == "page" || presentationClass == "handout" ||
            Attr(frame, "placeholder", "presentation") == "true")
            return;

        PptxShape shape;
        shape.name = Attr(frame, "name", "draw");
        ReadPlace(frame, shape);
        if (const tinyxml2::XMLElement* textBox = Child(frame, "text-box")) {
            shape.kind = presentationClass == "title"                                  ? PptxShapeKind::Title
                       : presentationClass == "subtitle"                               ? PptxShapeKind::Subtitle
                       : presentationClass == "outline" || presentationClass == "notes" ? PptxShapeKind::Body
                                                                                        : PptxShapeKind::Text;
            if (presentationClass == "notes") shape.name = kNotesFrame;
            ReadParagraphs(textBox, shape.paragraphs);
            if (HasText(shape)) out.push_back(std::move(shape));
        } else if (const tinyxml2::XMLElement* table = Child(frame, "table")) {
            shape.kind = PptxShapeKind::Table;
            ReadRows(table, shape.rows);
            out.push_back(std::move(shape));
        } else if (Child(frame, "object") || Child(frame, "object-ole") || Child(frame, "plugin") ||
                   Child(frame, "applet") || Child(frame, "floating-frame")) {
            return;   // a chart or another embedded object: not read
        } else if (const tinyxml2::XMLElement* image = Child(frame, "image")) {
            shape.kind = PptxShapeKind::Picture;
            shape.pictureTarget = Attr(image, "href", "xlink");   // empty for a flat file's inline picture
            const tinyxml2::XMLElement* desc = Child(frame, "desc");
            const tinyxml2::XMLElement* title = Child(frame, "title");
            shape.altText = desc && desc->GetText() ? desc->GetText()
                          : title && title->GetText() ? title->GetText() : "";
            out.push_back(std::move(shape));
        }
    }

    void ReadRows(const tinyxml2::XMLElement* container,
                  std::vector<std::vector<std::vector<PptxParagraph>>>& rows) {
        for (auto* e = container->FirstChildElement(); e; e = e->NextSiblingElement()) {
            if (Is(e, "table-header-rows") || Is(e, "table-rows") || Is(e, "table-row-group")) {
                ReadRows(e, rows);
            } else if (Is(e, "table-row")) {
                std::vector<std::vector<PptxParagraph>> row;
                for (auto* cell = e->FirstChildElement(); cell; cell = cell->NextSiblingElement()) {
                    // A cell another one spans over stays, empty, so the
                    // columns keep their places.
                    if (!Is(cell, "table-cell") && !Is(cell, "covered-table-cell")) continue;
                    std::vector<PptxParagraph> paragraphs;
                    ReadParagraphs(cell, paragraphs);
                    row.push_back(std::move(paragraphs));
                }
                rows.push_back(std::move(row));
            }
        }
    }

    // ---- paragraphs and lists ----

    ListLabel LabelAt(const std::string& listStyle, int level) const {
        const auto style = context_.listStyles.find(listStyle);
        if (style == context_.listStyles.end()) return ListLabel::Bullet;
        const auto entry = style->second.find(level + 1);
        return entry == style->second.end() ? ListLabel::Bullet : entry->second;
    }

    // The paragraphs a text box, shape or cell holds, lists included. A
    // paragraph outside a list has no bullet.
    void ReadParagraphs(const tinyxml2::XMLElement* container, std::vector<PptxParagraph>& out) {
        for (auto* e = container->FirstChildElement(); e; e = e->NextSiblingElement()) {
            if (Is(e, "p") || Is(e, "h")) {
                out.push_back(ReadParagraph(e, 0, ListLabel::None));
            } else if (Is(e, "list")) {
                ReadList(e, 0, std::string(), out);
            } else if (Is(e, "section")) {
                ReadParagraphs(e, out);
            }
        }
    }

    void ReadList(const tinyxml2::XMLElement* list, int level, std::string listStyle,
                  std::vector<PptxParagraph>& out) {
        const std::string own = Attr(list, "style-name", "text");
        if (!own.empty()) listStyle = own;   // else the enclosing list's
        const int clamped = std::min(level, 8);
        for (auto* item = list->FirstChildElement(); item; item = item->NextSiblingElement()) {
            const bool header = Is(item, "list-header");
            if (!header && !Is(item, "list-item")) continue;
            const ListLabel label = header ? ListLabel::None : LabelAt(listStyle, clamped);
            for (auto* e = item->FirstChildElement(); e; e = e->NextSiblingElement()) {
                if (Is(e, "p") || Is(e, "h")) out.push_back(ReadParagraph(e, clamped, label));
                else if (Is(e, "list")) ReadList(e, level + 1, listStyle, out);
            }
        }
    }

    TextLook ResolveLook(const std::string& family, const std::string& name) const {
        TextLook look;
        std::string current = name;
        for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
            const auto style = context_.textStyles.find(family + ":" + current);
            if (style == context_.textStyles.end()) break;
            look.Over(style->second.look);
            current = style->second.parent;
        }
        return look;
    }

    // ODF's white space: a run of spaces, tabs and line ends counts as one
    // space, and none at a paragraph's start or end; <text:s>, <text:tab>
    // and <text:line-break> are the ones that stay.
    struct Inline {
        PptxParagraph* paragraph = nullptr;
        bool afterSpace = true;          // drops a space at the start / after a collapsed one
        bool trailingSpace = false;      // the last character is a collapsed space
    };

    static void Append(Inline& state, const std::string& text, const TextLook& look) {
        if (text.empty()) return;
        std::vector<PptxRun>& runs = state.paragraph->runs;
        const bool bold = look.bold > 0, italic = look.italic > 0, underline = look.underline > 0;
        if (runs.empty() || runs.back().text == "\n" || runs.back().bold != bold ||
            runs.back().italic != italic || runs.back().underline != underline)
            runs.push_back({std::string(), bold, italic, underline});
        runs.back().text += text;
    }

    static void AppendCollapsed(Inline& state, const char* text, const TextLook& look) {
        std::string piece;
        for (const char* c = text; *c; ++c) {
            if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
                if (state.afterSpace) continue;
                piece += ' ';
                state.afterSpace = true;
                state.trailingSpace = true;
            } else {
                piece += *c;
                state.afterSpace = false;
                state.trailingSpace = false;
            }
        }
        Append(state, piece, look);
    }

    static void AppendLiteral(Inline& state, const std::string& text, const TextLook& look) {
        Append(state, text, look);
        state.afterSpace = false;
        state.trailingSpace = false;
    }

    void ReadInline(const tinyxml2::XMLNode* parent, const TextLook& look, Inline& state) {
        for (const tinyxml2::XMLNode* node = parent->FirstChild(); node; node = node->NextSibling()) {
            if (const tinyxml2::XMLText* text = node->ToText()) {
                AppendCollapsed(state, text->Value() ? text->Value() : "", look);
                continue;
            }
            const tinyxml2::XMLElement* e = node->ToElement();
            if (!e) continue;
            if (Is(e, "span")) {
                TextLook spanLook = ResolveLook("text", Attr(e, "style-name", "text"));
                spanLook.Over(look);
                ReadInline(e, spanLook, state);
            } else if (Is(e, "s")) {
                double count = 1.0;
                const std::string c = Attr(e, "c", "text");
                if (!c.empty() && !TryParseFloat(c, count)) count = 1.0;
                AppendLiteral(state, std::string(static_cast<size_t>(std::clamp(count, 1.0, 1000.0)), ' '), look);
            } else if (Is(e, "tab")) {
                AppendLiteral(state, "\t", look);
            } else if (Is(e, "line-break")) {
                state.paragraph->runs.push_back({"\n", false, false, false});
                state.afterSpace = true;
                state.trailingSpace = false;
            } else if (Is(e, "annotation") || Is(e, "annotation-end") || Is(e, "frame") ||
                       Is(e, "bookmark") || Is(e, "bookmark-start") || Is(e, "bookmark-end") ||
                       Is(e, "reference-mark") || Is(e, "reference-mark-start") ||
                       Is(e, "reference-mark-end") || Is(e, "soft-page-break")) {
                continue;   // comments, anchored pictures and markers carry no text of the paragraph
            } else {
                // A link (text:a), a field (page number, date, author...) and
                // anything else: the text it holds.
                ReadInline(e, look, state);
            }
        }
    }

    PptxParagraph ReadParagraph(const tinyxml2::XMLElement* p, int level, ListLabel label) {
        PptxParagraph paragraph;
        paragraph.level = level;
        paragraph.bullet = label != ListLabel::None;
        paragraph.numbered = label == ListLabel::Number;
        Inline state;
        state.paragraph = &paragraph;
        ReadInline(p, ResolveLook("paragraph", Attr(p, "style-name", "text")), state);
        // A space collapsed at the end goes.
        if (state.trailingSpace && !paragraph.runs.empty()) {
            std::string& last = paragraph.runs.back().text;
            if (!last.empty() && last.back() == ' ') last.pop_back();
            if (last.empty()) paragraph.runs.pop_back();
        }
        return paragraph;
    }
};

const tinyxml2::XMLElement* PresentationBody(const tinyxml2::XMLElement* root) {
    return Child(Child(root, "body"), "presentation");
}

bool ReadDocuments(const tinyxml2::XMLElement* contentRoot, const tinyxml2::XMLElement* stylesRoot,
                   PptxPresentation& out, std::string& error) {
    const tinyxml2::XMLElement* presentation = PresentationBody(contentRoot);
    if (!presentation) {
        error = "The file is not an OpenDocument presentation - it has no presentation body";
        return false;
    }
    OdpContext context;
    // Common styles first, then the automatic ones the content uses. A
    // separate styles.xml's automatic styles belong to its master pages:
    // only their page layouts are taken, since their names may repeat the
    // content's own.
    if (stylesRoot && stylesRoot != contentRoot) {
        CollectStyles(Child(stylesRoot, "styles"), context, true);
        CollectStyles(Child(stylesRoot, "automatic-styles"), context, false);
        CollectMasters(Child(stylesRoot, "master-styles"), context);
    }
    CollectStyles(Child(contentRoot, "styles"), context, true);
    CollectStyles(Child(contentRoot, "automatic-styles"), context, true);
    CollectMasters(Child(contentRoot, "master-styles"), context);

    OdpReader reader(context);
    reader.ReadPresentation(presentation, out);
    return true;
}

bool IsZip(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4;
}

// The media type a package declares: its mimetype entry, else the
// manifest's entry for the package itself.
std::string PackageMediaType(const UCZipPackageReader& zip) {
    std::string mimetype;
    if (zip.ReadEntry("mimetype", mimetype) && !mimetype.empty()) {
        while (!mimetype.empty() && std::isspace(static_cast<unsigned char>(mimetype.back()))) mimetype.pop_back();
        return mimetype;
    }
    std::string xml;
    tinyxml2::XMLDocument manifest;
    if (!zip.ReadEntry("META-INF/manifest.xml", xml) ||
        manifest.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !manifest.RootElement())
        return "";
    for (auto* entry = manifest.RootElement()->FirstChildElement(); entry; entry = entry->NextSiblingElement())
        if (Is(entry, "file-entry") && Attr(entry, "full-path") == "/") return Attr(entry, "media-type");
    return "";
}

bool IsPresentationMediaType(const std::string& mediaType) {
    return mediaType.rfind(kPresentationMediaType, 0) == 0;   // and "...-template"
}

bool ReadPackage(const UCZipPackageReader& zip, PptxPresentation& out, std::string& error,
                 const std::string& password) {
    // A password-protected deck: decrypted into an ordinary package first.
    std::vector<uint8_t> decrypted;
    switch (DecryptOdfPackage(zip, password, decrypted, error)) {
        case OfficeDecryptStatus::NotEncrypted:
            error.clear();
            break;
        case OfficeDecryptStatus::Decrypted: {
            UCZipPackageReader plain;
            if (!plain.OpenFromMemory(std::move(decrypted))) {
                error = "The decrypted OpenDocument presentation is not valid";
                return false;
            }
            return ReadPackage(plain, out, error, std::string());
        }
        default:
            return false;   // `error` says why: no password, a wrong one, an unsupported cipher
    }
    const std::string mediaType = PackageMediaType(zip);
    if (!mediaType.empty() && !IsPresentationMediaType(mediaType)) {
        error = "The file is an OpenDocument file of another kind (" + mediaType + "), not a presentation";
        return false;
    }
    std::string contentXml, stylesXml;
    tinyxml2::XMLDocument content, styles;
    if (!zip.ReadEntry("content.xml", contentXml) ||
        content.Parse(contentXml.c_str(), contentXml.size()) != tinyxml2::XML_SUCCESS || !content.RootElement()) {
        error = "The OpenDocument package has no readable content.xml";
        return false;
    }
    const bool hasStyles = zip.ReadEntry("styles.xml", stylesXml) &&
                           styles.Parse(stylesXml.c_str(), stylesXml.size()) == tinyxml2::XML_SUCCESS &&
                           styles.RootElement();
    return ReadDocuments(content.RootElement(), hasStyles ? styles.RootElement() : nullptr, out, error);
}

bool ReadFlat(const char* xml, size_t size, PptxPresentation& out, std::string& error) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml, size) != tinyxml2::XML_SUCCESS || !doc.RootElement()) {
        error = "The file is not a valid OpenDocument presentation (.fodp) - the XML does not parse";
        return false;
    }
    const std::string mediaType = Attr(doc.RootElement(), "mimetype", "office");
    if (!mediaType.empty() && !IsPresentationMediaType(mediaType)) {
        error = "The file is an OpenDocument file of another kind (" + mediaType + "), not a presentation";
        return false;
    }
    return ReadDocuments(doc.RootElement(), nullptr, out, error);
}

bool ReadBytes(const std::string& filePath, std::vector<uint8_t>& out, size_t limit) {
    std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
    if (!file.is_open()) return false;
    out.resize(limit);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(limit));
    out.resize(static_cast<size_t>(file.gcount()));
    return true;
}

} // namespace

bool IsOdpExtension(const std::string& extensionOrPath) {
    const size_t dot = extensionOrPath.rfind('.');
    std::string ext = dot == std::string::npos ? extensionOrPath : extensionOrPath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == "odp" || ext == "otp" || ext == "fodp";
}

bool IsOdpPresentationFile(const std::string& filePath) {
    std::vector<uint8_t> head;
    // A flat file names its type on the root element, after the namespaces
    // it declares - LibreOffice's run to some 2.5 KB.
    if (!ReadBytes(filePath, head, 16384) || head.empty()) return false;
    if (IsZip(head.data(), head.size())) {
        UCZipPackageReader zip;
        return zip.Open(filePath) && IsPresentationMediaType(PackageMediaType(zip));
    }
    const std::string text(head.begin(), head.end());
    return text.find(std::string("mimetype=\"") + kPresentationMediaType) != std::string::npos;
}

bool ReadOdpPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   const std::string& password) {
    out = PptxPresentation{};
    error.clear();
    if (IsZip(data.data(), data.size())) {
        UCZipPackageReader zip;
        if (!zip.OpenFromMemory(std::move(data))) {
            error = "The file is not a valid OpenDocument presentation (.odp)";
            return false;
        }
        return ReadPackage(zip, out, error, password);
    }
    return ReadFlat(reinterpret_cast<const char*>(data.data()), data.size(), out, error);
}

bool ReadOdpPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                         const std::string& password) {
    out = PptxPresentation{};
    error.clear();
    std::vector<uint8_t> head;
    if (!ReadBytes(filePath, head, 4)) {
        error = "Cannot open file: " + filePath;
        return false;
    }
    bool ok = false;
    if (IsZip(head.data(), head.size())) {
        UCZipPackageReader zip;
        if (!zip.Open(filePath)) {
            error = "The file is not a valid OpenDocument presentation (.odp)";
        } else {
            ok = ReadPackage(zip, out, error, password);
        }
    } else {
        std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
        const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ok = ReadFlat(xml.c_str(), xml.size(), out, error);
    }
    if (!ok) error += ": " + filePath;
    return ok;
}

} // namespace UltraCanvas

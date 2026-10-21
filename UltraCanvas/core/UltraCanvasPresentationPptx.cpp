// core/UltraCanvasPresentationPptx.cpp
// PowerPoint (.pptx) reader - see UltraCanvasPresentationPptx.h.
// Version: 1.0.0
// Last Modified: 2026-10-18
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasZipPackage.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace UltraCanvas {

namespace {

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

// The attribute, whatever prefix the writer gave it ("r:id", "r:embed").
std::string Attr(const tinyxml2::XMLElement* e, const char* localName) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strrchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, localName) == 0) return a->Value();
    }
    return "";
}

// A relationship attribute (r:id, r:embed): prefixed, whatever the prefix -
// <p:sldId> carries a plain id="256" beside its r:id, which Attr would take.
std::string RelAttr(const tinyxml2::XMLElement* e, const char* localName) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* colon = std::strrchr(a->Name(), ':');
        if (colon && std::strcmp(colon + 1, localName) == 0) return a->Value();
    }
    return "";
}

int64_t AttrInt(const tinyxml2::XMLElement* e, const char* localName) {
    const std::string v = Attr(e, localName);
    if (v.empty()) return 0;
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int64_t>(n) : 0;
}

// A part's path from a relationship target, relative to the part that names
// it ("../media/image1.png" from ppt/slides/slide1.xml is ppt/media/image1.png).
std::string ResolvePart(const std::string& fromPart, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::vector<std::string> parts;
    const size_t slash = fromPart.rfind('/');
    std::string path = (slash == std::string::npos ? std::string() : fromPart.substr(0, slash + 1)) + target;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string piece = path.substr(start, end - start);
        if (piece == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!piece.empty() && piece != ".") {
            parts.push_back(piece);
        }
        start = end + 1;
    }
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

class PptxReader {
public:
    explicit PptxReader(UCZipPackageReader& zip) : zip_(zip) {}

    bool Read(PptxPresentation& out, std::string& error) {
        tinyxml2::XMLDocument presentation;
        if (!LoadXml("ppt/presentation.xml", presentation)) {
            error = "The package has no ppt/presentation.xml - not a PowerPoint presentation";
            return false;
        }
        const tinyxml2::XMLElement* root = presentation.RootElement();
        if (const tinyxml2::XMLElement* size = Child(root, "sldSz")) {
            out.slideWidth = AttrInt(size, "cx");
            out.slideHeight = AttrInt(size, "cy");
        }
        const std::map<std::string, std::string> rels = Relationships("ppt/presentation.xml");
        int number = 0;
        for (auto* id = Child(Child(root, "sldIdLst"), "sldId"); id; id = id->NextSiblingElement()) {
            if (!Is(id, "sldId")) continue;
            const auto target = rels.find(RelAttr(id, "id"));
            if (target == rels.end()) continue;
            PptxSlide slide;
            slide.number = ++number;
            if (ReadSlide(target->second, slide)) out.slides.push_back(std::move(slide));
        }
        return true;
    }

private:
    UCZipPackageReader& zip_;

    bool LoadXml(const std::string& part, tinyxml2::XMLDocument& doc) {
        std::string xml;
        return zip_.ReadEntry(part, xml) && doc.Parse(xml.c_str(), xml.size()) == tinyxml2::XML_SUCCESS &&
               doc.RootElement();
    }

    // Relationship id -> resolved part, and type suffix -> part for lookups by type.
    std::map<std::string, std::string> Relationships(const std::string& part,
                                                     std::map<std::string, std::string>* byType = nullptr) {
        std::map<std::string, std::string> out;
        const size_t slash = part.rfind('/');
        const std::string relsPart = part.substr(0, slash + 1) + "_rels/" + part.substr(slash + 1) + ".rels";
        tinyxml2::XMLDocument doc;
        if (!LoadXml(relsPart, doc)) return out;
        for (auto* r = doc.RootElement()->FirstChildElement(); r; r = r->NextSiblingElement()) {
            if (!Is(r, "Relationship") || Attr(r, "TargetMode") == "External") continue;
            const std::string resolved = ResolvePart(part, Attr(r, "Target"));
            out[Attr(r, "Id")] = resolved;
            if (byType) {
                const std::string type = Attr(r, "Type");
                (*byType)[type.substr(type.rfind('/') + 1)] = resolved;
            }
        }
        return out;
    }

    bool ReadSlide(const std::string& part, PptxSlide& slide) {
        tinyxml2::XMLDocument doc;
        if (!LoadXml(part, doc)) return false;
        const tinyxml2::XMLElement* root = doc.RootElement();
        slide.hidden = Attr(root, "show") == "0";
        std::map<std::string, std::string> byType;
        const std::map<std::string, std::string> rels = Relationships(part, &byType);
        ReadTree(Child(Child(root, "cSld"), "spTree"), rels, slide.shapes);
        for (const PptxShape& shape : slide.shapes) {
            if (shape.kind == PptxShapeKind::Title && slide.title.empty()) {
                for (const PptxParagraph& p : shape.paragraphs) {
                    const std::string text = p.Text();
                    if (text.empty()) continue;
                    slide.title += (slide.title.empty() ? "" : " ") + text;
                }
            }
        }
        // The speaker notes: the notes slide's body placeholder.
        const auto notes = byType.find("notesSlide");
        tinyxml2::XMLDocument notesDoc;
        if (notes != byType.end() && LoadXml(notes->second, notesDoc)) {
            std::vector<PptxShape> shapes;
            ReadTree(Child(Child(notesDoc.RootElement(), "cSld"), "spTree"), {}, shapes);
            for (PptxShape& shape : shapes)
                if (shape.kind == PptxShapeKind::Body)
                    for (PptxParagraph& p : shape.paragraphs) slide.notes.push_back(std::move(p));
        }
        return true;
    }

    void ReadTree(const tinyxml2::XMLElement* tree, const std::map<std::string, std::string>& rels,
                  std::vector<PptxShape>& out) {
        for (auto* e = tree ? tree->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            if (Is(e, "sp")) {
                PptxShape shape;
                if (ReadShape(e, shape)) out.push_back(std::move(shape));
            } else if (Is(e, "grpSp")) {
                ReadTree(e, rels, out);
            } else if (Is(e, "graphicFrame")) {
                const tinyxml2::XMLElement* table = Child(Child(Child(e, "graphic"), "graphicData"), "tbl");
                if (!table) continue;
                PptxShape shape;
                shape.kind = PptxShapeKind::Table;
                shape.name = Attr(Child(Child(e, "nvGraphicFramePr"), "cNvPr"), "name");
                ReadPlace(Child(e, "xfrm"), shape);
                for (auto* tr = Child(table, "tr"); tr; tr = tr->NextSiblingElement()) {
                    if (!Is(tr, "tr")) continue;
                    std::vector<std::vector<PptxParagraph>> row;
                    for (auto* tc = Child(tr, "tc"); tc; tc = tc->NextSiblingElement()) {
                        if (!Is(tc, "tc")) continue;
                        std::vector<PptxParagraph> cell;
                        ReadText(Child(tc, "txBody"), false, cell);
                        row.push_back(std::move(cell));
                    }
                    shape.rows.push_back(std::move(row));
                }
                out.push_back(std::move(shape));
            } else if (Is(e, "pic")) {
                PptxShape shape;
                shape.kind = PptxShapeKind::Picture;
                const tinyxml2::XMLElement* props = Child(Child(e, "nvPicPr"), "cNvPr");
                shape.name = Attr(props, "name");
                shape.altText = Attr(props, "descr");
                ReadPlace(Child(Child(e, "spPr"), "xfrm"), shape);
                const auto target = rels.find(RelAttr(Child(Child(e, "blipFill"), "blip"), "embed"));
                if (target != rels.end()) shape.pictureTarget = target->second;
                out.push_back(std::move(shape));
            }
        }
    }

    static void ReadPlace(const tinyxml2::XMLElement* xfrm, PptxShape& shape) {
        if (const tinyxml2::XMLElement* off = Child(xfrm, "off")) {
            shape.x = AttrInt(off, "x");
            shape.y = AttrInt(off, "y");
        }
        if (const tinyxml2::XMLElement* ext = Child(xfrm, "ext")) {
            shape.width = AttrInt(ext, "cx");
            shape.height = AttrInt(ext, "cy");
        }
    }

    // A shape with text; false for slide furniture (date, footer, slide
    // number) and for a shape without any.
    static bool ReadShape(const tinyxml2::XMLElement* sp, PptxShape& shape) {
        const tinyxml2::XMLElement* nv = Child(sp, "nvSpPr");
        shape.name = Attr(Child(nv, "cNvPr"), "name");
        if (const tinyxml2::XMLElement* ph = Child(Child(nv, "nvPr"), "ph")) {
            const std::string type = Attr(ph, "type");
            if (type == "dt" || type == "ftr" || type == "sldNum" || type == "hdr" || type == "sldImg")
                return false;
            shape.kind = type == "title" || type == "ctrTitle" ? PptxShapeKind::Title
                       : type == "subTitle"                    ? PptxShapeKind::Subtitle
                       : PptxShapeKind::Body;   // body, obj or no type: content
        }
        ReadPlace(Child(Child(sp, "spPr"), "xfrm"), shape);
        ReadText(Child(sp, "txBody"), shape.kind == PptxShapeKind::Body, shape.paragraphs);
        for (const PptxParagraph& p : shape.paragraphs)
            if (!p.Text().empty()) return true;
        return false;
    }

    static void ReadText(const tinyxml2::XMLElement* body, bool bulletedByDefault,
                         std::vector<PptxParagraph>& out) {
        for (auto* p = Child(body, "p"); p; p = p->NextSiblingElement()) {
            if (!Is(p, "p")) continue;
            PptxParagraph para;
            const tinyxml2::XMLElement* pPr = Child(p, "pPr");
            para.level = static_cast<int>(std::clamp<int64_t>(AttrInt(pPr, "lvl"), 0, 8));
            para.bullet = bulletedByDefault;
            if (Child(pPr, "buNone")) para.bullet = false;
            if (Child(pPr, "buChar")) para.bullet = true;
            if (Child(pPr, "buAutoNum")) para.bullet = para.numbered = true;
            for (auto* r = p->FirstChildElement(); r; r = r->NextSiblingElement()) {
                if (Is(r, "br")) {
                    para.runs.push_back({"\n", false, false, false});
                } else if (Is(r, "r") || Is(r, "fld")) {
                    PptxRun run;
                    const tinyxml2::XMLElement* t = Child(r, "t");
                    run.text = t && t->GetText() ? t->GetText() : "";
                    const tinyxml2::XMLElement* rPr = Child(r, "rPr");
                    run.bold = Attr(rPr, "b") == "1" || Attr(rPr, "b") == "true";
                    run.italic = Attr(rPr, "i") == "1" || Attr(rPr, "i") == "true";
                    const std::string u = Attr(rPr, "u");
                    run.underline = !u.empty() && u != "none";
                    if (!run.text.empty()) para.runs.push_back(std::move(run));
                }
            }
            out.push_back(std::move(para));
        }
    }
};

std::vector<RichTextRun> RichRuns(const PptxParagraph& p) {
    std::vector<RichTextRun> out;
    bool breakNext = false;
    for (const PptxRun& run : p.runs) {
        if (run.text == "\n") {
            breakNext = true;
            continue;
        }
        RichTextRun r;
        r.text = run.text;
        r.bold = run.bold;
        r.italic = run.italic;
        r.underline = run.underline;
        r.lineBreakBefore = breakNext;
        breakNext = false;
        out.push_back(std::move(r));
    }
    return out;
}

RichDocBlock TextBlock(RichBlockType type, const std::string& text, bool italic = false) {
    RichDocBlock block;
    block.type = type;
    RichTextRun run;
    run.text = text;
    run.italic = italic;
    block.runs.push_back(std::move(run));
    return block;
}

} // namespace

std::string PptxParagraph::Text() const {
    std::string out;
    for (const PptxRun& run : runs) out += run.text;
    return out;
}

bool IsPptxExtension(const std::string& extensionOrPath) {
    const size_t dot = extensionOrPath.rfind('.');
    std::string ext = dot == std::string::npos ? extensionOrPath : extensionOrPath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == "pptx" || ext == "pptm" || ext == "ppsx" || ext == "ppsm" || ext == "potx" || ext == "potm";
}

bool IsPptxPackage(const std::string& filePath) {
    UCZipPackageReader zip;
    return zip.Open(filePath) && zip.HasEntry("ppt/presentation.xml");
}

bool ReadPptxPresentationFromPackage(std::vector<uint8_t> package, PptxPresentation& out,
                                     std::string& error) {
    out = PptxPresentation{};
    error.clear();
    UCZipPackageReader zip;
    if (!zip.OpenFromMemory(std::move(package))) {
        error = "The file is not a valid PowerPoint presentation (.pptx)";
        return false;
    }
    PptxReader reader(zip);
    return reader.Read(out, error);
}

bool ReadPptxPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                          const std::string& password) {
    out = PptxPresentation{};
    error.clear();
    // A password-protected deck is a compound file holding the package
    // encrypted (UltraCanvasOfficeEncryption.h), not a ZIP.
    std::vector<uint8_t> package;
    switch (DecryptOfficePackageFile(filePath, password, package, error)) {
        case OfficeDecryptStatus::NotEncrypted: {
            UCZipPackageReader zip;
            if (!zip.Open(filePath)) {
                error = "The file is not a valid PowerPoint presentation (.pptx): " + filePath;
                return false;
            }
            PptxReader reader(zip);
            if (!reader.Read(out, error)) {
                error += ": " + filePath;
                return false;
            }
            return true;
        }
        case OfficeDecryptStatus::Decrypted:
            if (!ReadPptxPresentationFromPackage(std::move(package), out, error)) {
                error += ": " + filePath;
                return false;
            }
            return true;
        default:
            error += ": " + filePath;
            return false;
    }
}

UCRichDocument PptxPresentationToRichDocument(const PptxPresentation& presentation) {
    UCRichDocument doc;
    for (const PptxSlide& slide : presentation.slides) {
        if (!doc.blocks.empty()) {
            RichDocBlock rule;
            rule.type = RichBlockType::HorizontalRule;
            doc.blocks.push_back(std::move(rule));
        }
        RichDocBlock heading = TextBlock(RichBlockType::Heading,
            std::to_string(slide.number) + ". " + (slide.title.empty() ? "Slide " + std::to_string(slide.number)
                                                                        : slide.title) +
            (slide.hidden ? " (hidden)" : ""));
        heading.headingLevel = 1;
        doc.blocks.push_back(std::move(heading));

        for (const PptxShape& shape : slide.shapes) {
            switch (shape.kind) {
                case PptxShapeKind::Title:
                    break;   // the heading
                case PptxShapeKind::Picture:
                    doc.blocks.push_back(TextBlock(RichBlockType::Paragraph,
                        "[Picture" + (shape.altText.empty() ? std::string() : ": " + shape.altText) + "]",
                        true));
                    break;
                case PptxShapeKind::Table: {
                    RichDocBlock table;
                    table.type = RichBlockType::Table;
                    for (const auto& row : shape.rows) {
                        RichTableRow out;
                        for (const auto& cell : row) {
                            RichTableCell c;
                            for (const PptxParagraph& p : cell) {
                                std::vector<RichTextRun> runs = RichRuns(p);
                                if (!c.runs.empty() && !runs.empty()) runs.front().lineBreakBefore = true;
                                c.runs.insert(c.runs.end(), runs.begin(), runs.end());
                            }
                            out.cells.push_back(std::move(c));
                        }
                        table.tableRows.push_back(std::move(out));
                    }
                    if (!table.tableRows.empty()) table.tableRows.front().header = true;
                    doc.blocks.push_back(std::move(table));
                    break;
                }
                default:
                    for (const PptxParagraph& p : shape.paragraphs) {
                        std::vector<RichTextRun> runs = RichRuns(p);
                        if (runs.empty()) continue;
                        RichDocBlock block;
                        block.type = p.bullet ? RichBlockType::ListItem : RichBlockType::Paragraph;
                        block.orderedList = p.numbered;
                        block.listLevel = p.level;
                        if (shape.kind == PptxShapeKind::Subtitle)
                            for (RichTextRun& r : runs) r.italic = true;
                        block.runs = std::move(runs);
                        doc.blocks.push_back(std::move(block));
                    }
                    break;
            }
        }

        bool first = true;
        for (const PptxParagraph& p : slide.notes) {
            std::vector<RichTextRun> runs = RichRuns(p);
            if (runs.empty()) continue;
            RichDocBlock quote;
            quote.type = RichBlockType::BlockQuote;
            if (first) {
                RichTextRun label;
                label.text = "Notes: ";
                label.bold = true;
                quote.runs.push_back(std::move(label));
                first = false;
            }
            quote.runs.insert(quote.runs.end(), runs.begin(), runs.end());
            doc.blocks.push_back(std::move(quote));
        }
    }
    return doc;
}

} // namespace UltraCanvas

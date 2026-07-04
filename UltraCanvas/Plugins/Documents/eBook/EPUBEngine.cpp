// Plugins/Documents/eBook/EPUBEngine.cpp
// EPUB 2/3 engine: container.xml → OPF → spine/TOC over EBookArchive.
// XML documents are parsed with the tolerant HTMLReader parser (namespace
// prefixes are stripped from tag names: dc:title → title, navPoint →
// navpoint; attribute names keep their prefixes).
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "EPUBEngine.h"

#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/CSSStyleSheet.h"   // HTML::Trim

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

std::string LowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string StripFragment(const std::string& href) {
    size_t hash = href.find('#');
    return hash == std::string::npos ? href : href.substr(0, hash);
}

std::string FileStem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// First direct or nested child element with the given (namespace-stripped)
// tag, searching the whole subtree.
HTML::Node* Find(HTML::Node* root, const std::string& tag) {
    return root ? root->FindFirst(tag) : nullptr;
}

} // namespace

// ============================================================================
// PATH UTILITIES
// ============================================================================

std::string EPUBEngine::NormalizePath(const std::string& path) {
    return NormalizeEBookPath(path);
}

std::string EPUBEngine::ResolveHref(const std::string& baseFile,
                                    const std::string& relative) {
    return ResolveEBookHref(baseFile, relative);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

std::string EPUBEngine::GetFormatName() const {
    if (versionString.rfind("3", 0) == 0) return "EPUB 3";
    if (versionString.rfind("2", 0) == 0) return "EPUB 2";
    return "EPUB";
}

bool EPUBEngine::LoadFromMemory(std::vector<uint8_t> data,
                                const std::string& password) {
    Close();

    ReportProgress(0.05f, "Opening archive...");
    if (!archive.OpenFromMemory(std::move(data))) {
        Fail("Not an EPUB: " + archive.GetLastError());
        return false;
    }

    ReportProgress(0.2f, "Reading container...");
    if (!ParseContainer()) return false;

    ReportProgress(0.4f, "Reading package document...");
    if (!ParseOPF(password)) return false;

    ReportProgress(0.9f, "Building navigation...");
    AssignChapterTitles();

    metadata.format = EBookFormat::EPUB;
    metadata.formatVersion = versionString;
    metadata.chapterCount = static_cast<int>(chapters.size());
    metadata.hasCover = !coverPath.empty();

    loaded = true;
    ReportProgress(1.0f, "Complete");
    return true;
}

void EPUBEngine::Close() {
    ResetBaseState();
    archive.Close();
    opfPath.clear();
    manifest.clear();
    manifestById.clear();
    chapters.clear();
    coverPath.clear();
    stylesheetPaths.clear();
    versionString.clear();
}

// ============================================================================
// CONTAINER / OPF
// ============================================================================

bool EPUBEngine::ParseContainer() {
    std::string containerXml = archive.ReadTextFile("META-INF/container.xml");
    if (containerXml.empty()) {
        Fail("META-INF/container.xml missing");
        return false;
    }

    HTML::Parser parser;
    HTML::Document doc = parser.Parse(containerXml);

    // <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
    HTML::Node* rootfile = Find(doc.root.get(), "rootfile");
    while (rootfile) {
        std::string mediaType = rootfile->GetAttribute("media-type");
        if (mediaType.empty() ||
            mediaType == "application/oebps-package+xml") {
            opfPath = NormalizePath(rootfile->GetAttribute("full-path"));
            break;
        }
        rootfile = nullptr;   // only the first matching rootfile is used
    }

    if (opfPath.empty() || !archive.Contains(opfPath)) {
        Fail("OPF package document not found");
        return false;
    }
    return true;
}

bool EPUBEngine::ParseOPF(const std::string& /*password*/) {
    std::string opfXml = archive.ReadTextFile(opfPath);
    if (opfXml.empty()) {
        Fail("Failed to read " + opfPath);
        return false;
    }

    HTML::Parser parser;
    HTML::Document doc = parser.Parse(opfXml);

    HTML::Node* pkg = Find(doc.root.get(), "package");
    if (pkg) versionString = pkg->GetAttribute("version");

    // ---- metadata (Dublin Core; dc: prefix is stripped by the parser) ----
    if (HTML::Node* meta = Find(doc.root.get(), "metadata")) {
        std::string coverId;
        meta->ForEachElement([&](HTML::Node& e) {
            std::string text = HTML::Trim(e.TextContent());
            if (e.tag == "title" && metadata.title.empty()) {
                metadata.title = text;
            } else if (e.tag == "creator" && !text.empty()) {
                metadata.authors.push_back(text);
            } else if (e.tag == "language" && metadata.language.empty()) {
                metadata.language = text;
            } else if (e.tag == "publisher") {
                metadata.publisher = text;
            } else if (e.tag == "description") {
                metadata.description = text;
            } else if (e.tag == "date" && metadata.publicationDate.empty()) {
                metadata.publicationDate = text;
            } else if (e.tag == "subject" && !text.empty()) {
                metadata.subjects.push_back(text);
            } else if (e.tag == "rights") {
                metadata.rights = text;
            } else if (e.tag == "identifier") {
                std::string lower = LowerCopy(text);
                if (metadata.isbn.empty() &&
                    (lower.rfind("isbn", 0) == 0 ||
                     LowerCopy(e.GetAttribute("opf:scheme")) == "isbn")) {
                    metadata.isbn = text;
                }
            } else if (e.tag == "meta" &&
                       LowerCopy(e.GetAttribute("name")) == "cover") {
                coverId = e.GetAttribute("content");   // EPUB 2 cover
            }
            return true;
        });
        if (!coverId.empty()) {
            // Resolved after the manifest is read; stash in coverPath field
            // temporarily via the id — handled below.
            coverPath = "\x01" + coverId;   // marker: id, not a path yet
        }
    }

    // ---- manifest ----
    std::string ncxId, navHref;
    if (HTML::Node* man = Find(doc.root.get(), "manifest")) {
        for (const auto& child : man->children) {
            if (!child->IsElement("item")) continue;
            ManifestItem item;
            item.id = child->GetAttribute("id");
            item.href = ResolveHref(opfPath, child->GetAttribute("href"));
            item.mediaType = child->GetAttribute("media-type");
            item.properties = child->GetAttribute("properties");

            if (item.mediaType == "text/css") {
                stylesheetPaths.push_back(item.href);
            }
            if (item.properties.find("cover-image") != std::string::npos) {
                coverPath = item.href;                 // EPUB 3 cover
            }
            if (item.properties.find("nav") != std::string::npos) {
                navHref = item.href;                   // EPUB 3 nav doc
            }
            if (item.mediaType == "application/x-dtbncx+xml") {
                ncxId = item.id;
            }

            manifestById[item.id] = manifest.size();
            manifest.push_back(std::move(item));
        }
    }

    // EPUB 2 cover id → path.
    if (!coverPath.empty() && coverPath[0] == '\x01') {
        const ManifestItem* item = ItemById(coverPath.substr(1));
        coverPath = item ? item->href : "";
    }
    // Fallback: a manifest image whose id mentions "cover".
    if (coverPath.empty()) {
        for (const auto& item : manifest) {
            if (item.mediaType.rfind("image/", 0) == 0 &&
                LowerCopy(item.id).find("cover") != std::string::npos) {
                coverPath = item.href;
                break;
            }
        }
    }

    // ---- spine ----
    if (HTML::Node* spine = Find(doc.root.get(), "spine")) {
        std::string tocAttr = spine->GetAttribute("toc");
        if (!tocAttr.empty()) ncxId = tocAttr;

        for (const auto& child : spine->children) {
            if (!child->IsElement("itemref")) continue;
            if (LowerCopy(child->GetAttribute("linear")) == "no") continue;

            const ManifestItem* item = ItemById(child->GetAttribute("idref"));
            if (!item) continue;
            if (item->mediaType != "application/xhtml+xml" &&
                item->mediaType != "text/html") {
                continue;
            }
            chapters.push_back({item->href, ""});
        }
    }

    if (chapters.empty()) {
        Fail("EPUB spine contains no readable chapters");
        return false;
    }

    ParseNavigation(ncxId, navHref);
    return true;
}

// ============================================================================
// NAVIGATION
// ============================================================================

void EPUBEngine::ParseNavigation(const std::string& ncxId, const std::string& navHref) {
    // Prefer the EPUB 3 nav document, fall back to NCX.
    if (!navHref.empty() && archive.Contains(navHref)) {
        ParseNavDoc(navHref);
        if (!tableOfContents.empty()) return;
    }
    if (!ncxId.empty()) {
        if (const ManifestItem* item = ItemById(ncxId)) {
            ParseNCX(item->href);
        }
    }
    // Last resort: one TOC entry per chapter, named after the file.
    if (tableOfContents.empty()) {
        for (size_t i = 0; i < chapters.size(); ++i) {
            EBookTOCEntry entry;
            entry.title = FileStem(chapters[i].href);
            entry.href = chapters[i].href;
            entry.pageNumber = static_cast<int>(i);
            entry.index = static_cast<int>(i);
            tableOfContents.push_back(std::move(entry));
        }
    }
}

void EPUBEngine::ParseNCX(const std::string& ncxPath) {
    std::string ncxXml = archive.ReadTextFile(ncxPath);
    if (ncxXml.empty()) return;

    HTML::Parser parser;
    HTML::Document doc = parser.Parse(ncxXml);

    int index = 0;
    // navPoint → navpoint after namespace/lowercase normalization.
    std::function<void(HTML::Node&, int, std::vector<EBookTOCEntry>&)> walk =
        [&](HTML::Node& node, int level, std::vector<EBookTOCEntry>& out) {
            for (const auto& child : node.children) {
                if (!child->IsElement("navpoint")) continue;

                EBookTOCEntry entry;
                entry.level = level;
                entry.index = index++;

                if (HTML::Node* label = child->FindFirst("text")) {
                    entry.title = HTML::Trim(label->TextContent());
                }
                if (HTML::Node* content = child->FindFirst("content")) {
                    entry.href = ResolveHref(ncxPath, content->GetAttribute("src"));
                }
                entry.pageNumber = ChapterIndexForHref(entry.href);

                walk(*child, level + 1, entry.children);
                out.push_back(std::move(entry));
            }
        };

    if (HTML::Node* navMap = Find(doc.root.get(), "navmap")) {
        walk(*navMap, 0, tableOfContents);
    }
}

void EPUBEngine::ParseNavDoc(const std::string& navPath) {
    std::string navXml = archive.ReadTextFile(navPath);
    if (navXml.empty()) return;

    HTML::Parser parser;
    HTML::Document doc = parser.Parse(navXml);

    // Find <nav epub:type="toc">, else the first <nav>.
    HTML::Node* tocNav = nullptr;
    doc.root->ForEachElement([&](HTML::Node& e) {
        if (e.tag != "nav") return true;
        if (!tocNav) tocNav = &e;
        if (LowerCopy(e.GetAttribute("epub:type")) == "toc") {
            tocNav = &e;
            return false;
        }
        return true;
    });
    if (!tocNav) return;

    int index = 0;
    std::function<void(HTML::Node&, int, std::vector<EBookTOCEntry>&)> walkList =
        [&](HTML::Node& listNode, int level, std::vector<EBookTOCEntry>& out) {
            for (const auto& li : listNode.children) {
                if (!li->IsElement("li")) continue;

                EBookTOCEntry entry;
                entry.level = level;
                entry.index = index++;

                for (const auto& part : li->children) {
                    if (part->IsElement("a")) {
                        entry.title = HTML::Trim(part->TextContent());
                        entry.href = ResolveHref(navPath, part->GetAttribute("href"));
                    } else if (part->IsElement("span") && entry.title.empty()) {
                        entry.title = HTML::Trim(part->TextContent());
                    } else if (part->IsElement("ol") || part->IsElement("ul")) {
                        walkList(*part, level + 1, entry.children);
                    }
                }
                entry.pageNumber = ChapterIndexForHref(entry.href);
                if (!entry.title.empty() || !entry.children.empty()) {
                    out.push_back(std::move(entry));
                }
            }
        };

    if (HTML::Node* list = tocNav->FindFirst("ol")) {
        walkList(*list, 0, tableOfContents);
    } else if (HTML::Node* ulist = tocNav->FindFirst("ul")) {
        walkList(*ulist, 0, tableOfContents);
    }
}

void EPUBEngine::AssignChapterTitles() {
    // Give each chapter the title of the first TOC entry pointing at it.
    std::function<void(const std::vector<EBookTOCEntry>&)> visit =
        [&](const std::vector<EBookTOCEntry>& entries) {
            for (const auto& entry : entries) {
                int chapter = entry.pageNumber;
                if (chapter >= 0 && chapter < static_cast<int>(chapters.size()) &&
                    chapters[static_cast<size_t>(chapter)].title.empty()) {
                    chapters[static_cast<size_t>(chapter)].title = entry.title;
                }
                visit(entry.children);
            }
        };
    visit(tableOfContents);

    for (auto& chapter : chapters) {
        if (chapter.title.empty()) chapter.title = FileStem(chapter.href);
    }
}

// ============================================================================
// CONTENT ACCESS
// ============================================================================

EBookChapter EPUBEngine::GetChapter(int index) const {
    EBookChapter chapter;
    if (index < 0 || index >= static_cast<int>(chapters.size())) return chapter;

    const SpineChapter& spine = chapters[static_cast<size_t>(index)];
    chapter.href = spine.href;
    chapter.title = spine.title;
    chapter.content = archive.ReadTextFile(spine.href);
    return chapter;
}

std::string EPUBEngine::GetStylesheets() const {
    std::string combined;
    for (const auto& path : stylesheetPaths) {
        std::string css = archive.ReadTextFile(path);
        if (!css.empty()) {
            combined += css;
            combined += '\n';
        }
    }
    return combined;
}

std::vector<uint8_t> EPUBEngine::GetResource(const std::string& href) const {
    std::vector<uint8_t> data;
    if (href.empty()) return data;

    std::string path = NormalizePath(StripFragment(href));
    if (archive.ReadFile(path, data)) return data;

    // OPF-relative fallback for hrefs that were not pre-resolved.
    std::string viaOpf = ResolveHref(opfPath, StripFragment(href));
    if (viaOpf != path) archive.ReadFile(viaOpf, data);
    return data;
}

std::vector<uint8_t> EPUBEngine::GetCoverImage() const {
    std::vector<uint8_t> data;
    if (!coverPath.empty()) archive.ReadFile(coverPath, data);
    return data;
}

// ============================================================================
// HELPERS
// ============================================================================

const EPUBEngine::ManifestItem* EPUBEngine::ItemById(const std::string& id) const {
    auto it = manifestById.find(id);
    if (it == manifestById.end()) return nullptr;
    return &manifest[it->second];
}

int EPUBEngine::ChapterIndexForHref(const std::string& href) const {
    std::string path = StripFragment(href);
    for (size_t i = 0; i < chapters.size(); ++i) {
        if (chapters[i].href == path) return static_cast<int>(i);
    }
    return -1;
}

// ============================================================================
// REGISTRATION
// ============================================================================

void RegisterEPUBEngine() {
    RegisterEBookEngine({"epub"}, [] {
        return std::static_pointer_cast<IEBookEngine>(std::make_shared<EPUBEngine>());
    });
}

} // namespace UltraCanvas

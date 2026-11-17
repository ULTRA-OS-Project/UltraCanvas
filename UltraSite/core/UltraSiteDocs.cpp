// UltraSite/core/UltraSiteDocs.cpp
// Builder::AddDocs: a folder of Markdown files as doc pages, the way
// Docusaurus reads its docs/ - a page per file, a folder's index.md or
// README.md as the folder's page, the sidebar after the folders - with
// every link between the files turned into the pages' addresses, and the
// same tree in other languages joined page by page.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSite.h"
#include "UltraSite/UltraSiteMarkdown.h"
#include "UltraSiteInternal.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace UltraSite {

namespace {

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool IsMarkdown(const fs::path& path) {
    const std::string extension = Lower(UltraCanvas::PathToUtf8(path.extension()));
    return extension == ".md" || extension == ".markdown";
}

// A path segment as an address has it: a space a '-'.
std::string UrlSegment(const std::string& segment) {
    std::string out;
    for (char c : segment) out += c == ' ' ? '-' : c;
    return out;
}

std::string Prettify(const std::string& stem) {
    std::string out;
    for (char c : stem) out += (c == '-' || c == '_') ? ' ' : c;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

int ToInt(const std::string& text, int fallback) {
    if (text.empty()) return fallback;
    int value = 0;
    bool negative = false;
    size_t i = 0;
    if (text[0] == '-') { negative = true; i = 1; }
    if (i >= text.size()) return fallback;
    for (; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return fallback;
        value = value * 10 + (text[i] - '0');
        if (value > 1000000) return fallback;
    }
    return negative ? -value : value;
}

// The first paragraph's text, cut at a word near 160 characters.
std::string FirstParagraph(const std::string& html) {
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    if (!document.root) return std::string();
    UltraCanvas::HTML::Node* paragraph = document.root->FindFirst("p");
    if (!paragraph) return std::string();
    std::string text = paragraph->TextContent();
    std::string collapsed;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!collapsed.empty() && collapsed.back() != ' ') collapsed += ' ';
        } else {
            collapsed += c;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    if (collapsed.size() <= 160) return collapsed;
    size_t cut = collapsed.rfind(' ', 157);
    if (cut == std::string::npos || cut < 80) cut = 157;
    while (cut > 0 && (static_cast<unsigned char>(collapsed[cut]) & 0xC0) == 0x80) --cut;   // not inside a character
    return collapsed.substr(0, cut) + "\xE2\x80\xA6";
}

bool Excluded(const fs::path& relative, const std::vector<std::string>& exclude) {
    for (const fs::path& part : relative) {
        const std::string name = UltraCanvas::PathToUtf8(part);
        if (!name.empty() && name[0] == '.') return true;
        if (std::find(exclude.begin(), exclude.end(), name) != exclude.end()) return true;
    }
    return false;
}

// One doc file on its way to a route.
struct DocFile {
    fs::path source;
    std::string relative;              // "guide/install.md"
    fs::path folder;                   // its folder, relative ("guide")
    bool folderPage = false;           // index.md / README.md
    FrontMatter matter;
    std::string path;                  // the route's
    std::string title, label, description;
    int position = INT_MAX;
    MarkdownResult content;
};

struct Category {
    std::string label;
    int position = INT_MAX;
    bool hasJsonPosition = false;
};

// A folder with no index.md of its own: its page lists what it holds.
struct GeneratedIndex {
    std::string path, title;
    std::vector<NavItem> items;
};

// One language's tree, loaded.
struct Tree {
    std::string language, prefix, sidebar;
    std::string version;               // a version's label; empty for a translation
    fs::path root;
    std::vector<DocFile> files;
    std::vector<NavItem> navigation;
    std::vector<GeneratedIndex> generated;
};

struct SortItem {
    int position;
    std::string label;
    NavItem item;
};

std::vector<NavItem> Sorted(std::vector<SortItem> items) {
    std::stable_sort(items.begin(), items.end(), [](const SortItem& a, const SortItem& b) {
        if (a.position != b.position) return a.position < b.position;
        return Lower(a.label) < Lower(b.label);
    });
    std::vector<NavItem> out;
    for (SortItem& entry : items) out.push_back(std::move(entry.item));
    return out;
}

} // namespace

DocsReport Builder::AddDocs(const DocsOptions& options) {
    DocsReport report;
    const std::string mainLanguage = options.language.empty() ? config.language : options.language;
    std::vector<Tree> trees;
    const bool versioned = !options.versions.empty();
    trees.push_back({mainLanguage, options.routePrefix, options.sidebar, versioned ? options.currentVersion : std::string(),
                     UltraCanvas::PathFromUtf8(options.folder), {}, {}, {}});
    for (const DocsTranslation& translation : options.translations) {
        trees.push_back({translation.language, translation.routePrefix, options.sidebar + "-" + translation.language, std::string(),
                         UltraCanvas::PathFromUtf8(translation.folder), {}, {}, {}});
    }
    for (const DocsVersion& version : options.versions) {
        trees.push_back({mainLanguage, version.routePrefix, options.sidebar + "-v-" + version.label, version.label,
                         UltraCanvas::PathFromUtf8(version.folder), {}, {}, {}});
    }

    const fs::path sourceRoot = options.sourceRoot.empty() ? fs::path() : fs::absolute(UltraCanvas::PathFromUtf8(options.sourceRoot)).lexically_normal();
    std::error_code ec;
    for (Tree& tree : trees) {
        std::string prefix = tree.prefix.empty() ? "/" : tree.prefix;
        if (prefix.front() != '/') prefix.insert(prefix.begin(), '/');
        if (prefix.back() != '/') prefix += '/';
        tree.prefix = prefix;
        if (!fs::is_directory(tree.root, ec)) {
            report.errors.push_back("no docs folder " + UltraCanvas::PathToUtf8(tree.root));
            continue;
        }
        tree.root = fs::absolute(tree.root).lexically_normal();

        // The files, in a stable order.
        std::vector<fs::path> sources;
        for (fs::recursive_directory_iterator it(tree.root, ec), end; it != end; it.increment(ec)) {
            const fs::path relative = it->path().lexically_relative(tree.root);
            if (Excluded(relative, options.exclude)) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && IsMarkdown(it->path())) sources.push_back(it->path());
        }
        std::sort(sources.begin(), sources.end());

        std::map<std::string, std::string> seenPaths;   // route -> file
        for (const fs::path& source : sources) {
            DocFile file;
            file.source = source;
            const fs::path relative = source.lexically_relative(tree.root);
            file.relative = OutputPath(relative);
            file.folder = relative.parent_path();
            std::string text;
            if (!ReadFile(UltraCanvas::PathToUtf8(source), text)) {
                report.errors.push_back("cannot read " + file.relative);
                continue;
            }
            file.matter = ParseFrontMatter(text);
            if (Lower(file.matter.values["draft"]) == "true") continue;
            const std::string stem = UltraCanvas::PathToUtf8(source.stem());
            file.folderPage = Lower(stem) == "index" || Lower(stem) == "readme";
            std::vector<std::string> segments;
            for (const fs::path& part : file.folder) segments.push_back(UrlSegment(UltraCanvas::PathToUtf8(part)));
            if (!file.folderPage) segments.push_back(UrlSegment(stem));
            const std::string slug = file.matter.values["slug"];
            if (!slug.empty() && slug[0] == '/') {
                segments.clear();
                size_t start = 1;
                while (start < slug.size()) {
                    size_t end = slug.find('/', start);
                    if (end == std::string::npos) end = slug.size();
                    if (end > start) segments.push_back(UrlSegment(slug.substr(start, end - start)));
                    start = end + 1;
                }
            } else if (!slug.empty()) {
                if (segments.empty()) segments.push_back(UrlSegment(slug));
                else segments.back() = UrlSegment(slug);
            }
            file.path = tree.prefix;
            for (const std::string& segment : segments) file.path += segment + "/";
            auto [found, inserted] = seenPaths.emplace(file.path, file.relative);
            if (!inserted) {
                report.errors.push_back(file.relative + " and " + found->second + " are both " + file.path);
                continue;
            }
            file.position = ToInt(file.matter.values["sidebar_position"], INT_MAX);
            tree.files.push_back(std::move(file));
        }

        // Where a link from one file to another goes.
        std::map<fs::path, std::string> pageOfFile, pageOfFolder;
        for (const DocFile& file : tree.files) {
            pageOfFile[file.source] = file.path;
            if (file.folderPage) pageOfFolder[file.source.parent_path()] = file.path;
        }

        for (DocFile& file : tree.files) {
            MarkdownOptions markdown;
            markdown.baseDirectory = UltraCanvas::PathToUtf8(file.source.parent_path());
            const Frame frame = FrameFor(tree.language);
            markdown.copyLabel = frame.labels.copy;
            markdown.copiedLabel = frame.labels.copied;
            markdown.image = [this](const std::string& name, const std::vector<uint8_t>& data) {
                return AssetFromText(name, std::string(data.begin(), data.end()));
            };
            const fs::path folder = file.source.parent_path();
            const std::string relativeName = file.relative;
            markdown.link = [&, folder, relativeName](const std::string& target) -> std::string {
                std::string pathPart = target, anchor;
                const size_t hash = pathPart.find('#');
                if (hash != std::string::npos) {
                    anchor = pathPart.substr(hash);
                    pathPart = pathPart.substr(0, hash);
                }
                const size_t query = pathPart.find('?');
                if (query != std::string::npos) pathPart = pathPart.substr(0, query);
                const fs::path resolved = (folder / UltraCanvas::PathFromUtf8(pathPart)).lexically_normal();
                fs::path withoutSlash = resolved;
                if (!withoutSlash.has_filename()) withoutSlash = withoutSlash.parent_path();
                auto page = pageOfFile.find(withoutSlash);
                if (page != pageOfFile.end()) return Url(page->second) + anchor;
                auto folderPage = pageOfFolder.find(withoutSlash);
                if (folderPage != pageOfFolder.end()) return Url(folderPage->second) + anchor;
                std::error_code exists;
                const bool insideTree = !withoutSlash.lexically_relative(tree.root).empty() &&
                                        withoutSlash.lexically_relative(tree.root).begin()->string() != "..";
                if (insideTree && fs::is_regular_file(withoutSlash, exists) && !IsMarkdown(withoutSlash)) {
                    return Asset(UltraCanvas::PathToUtf8(withoutSlash));
                }
                if (!options.sourceUrl.empty() && !sourceRoot.empty() && fs::exists(withoutSlash, exists)) {
                    const fs::path underRoot = withoutSlash.lexically_relative(sourceRoot);
                    if (!underRoot.empty() && underRoot.begin()->string() != "..") {
                        return options.sourceUrl + OutputPath(underRoot) + anchor;
                    }
                }
                report.warnings.push_back(relativeName + ": the link to " + target + " leads nowhere the site knows");
                return target;
            };
            file.content = MarkdownToHtml(file.matter.body, markdown);
            const std::string stem = UltraCanvas::PathToUtf8(file.source.stem());
            file.title = file.matter.values["title"];
            if (file.title.empty()) file.title = file.content.title;
            if (file.title.empty()) {
                file.title = Prettify(file.folderPage ? UltraCanvas::PathToUtf8(file.source.parent_path().filename()) : stem);
            }
            file.label = file.matter.values["sidebar_label"];
            if (file.label.empty()) file.label = file.title;
            file.description = file.matter.values["description"];
            if (file.description.empty()) file.description = FirstParagraph(file.content.html);
        }

        // The sidebar: the folders as categories, each ordered.
        std::function<std::vector<NavItem>(const fs::path&)> folderItems = [&](const fs::path& folder) {
            std::vector<SortItem> items;
            std::set<fs::path> subfolders;
            for (const DocFile& file : tree.files) {
                if (file.folder == folder) {
                    if (!file.folderPage) items.push_back({file.position, file.label, {file.label, file.path, {}}});
                } else {
                    const fs::path under = file.folder.lexically_relative(folder);
                    if (!folder.empty() && (under.empty() || under.begin()->string() == "..")) continue;
                    const fs::path first = folder.empty() ? *file.folder.begin() : *under.begin();
                    if (!file.folder.empty()) subfolders.insert(folder / first);
                }
            }
            for (const fs::path& sub : subfolders) {
                Category category;
                category.label = Prettify(UltraCanvas::PathToUtf8(sub.filename()));
                std::string href;
                for (const DocFile& file : tree.files) {
                    if (file.folderPage && file.folder == sub) {
                        category.label = file.label;
                        category.position = file.position;
                        href = file.path;
                    }
                }
                const fs::path json = tree.root / sub / "_category_.json";
                if (fs::is_regular_file(json, ec)) {
                    UltraCanvas::JSONParseResult parsed;
                    const UltraCanvas::JSONValue value = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(json), &parsed);
                    if (parsed.success) {
                        if (!value["label"].GetString().empty()) category.label = value["label"].GetString();
                        if (value["position"].IsNumber()) category.position = static_cast<int>(value["position"].GetNumber());
                    } else {
                        report.warnings.push_back(OutputPath(sub / "_category_.json") + ": " + parsed.errorMessage);
                    }
                }
                NavItem item{category.label, href, folderItems(sub)};
                if (item.children.empty() && href.empty()) continue;
                if (href.empty()) {
                    // Docusaurus's generated index: the folder's page lists its pages.
                    std::string path = tree.prefix;
                    for (const fs::path& part : sub) path += UrlSegment(UltraCanvas::PathToUtf8(part)) + "/";
                    item.href = path;
                    tree.generated.push_back({path, category.label, item.children});
                }
                items.push_back({category.position, category.label, std::move(item)});
            }
            return Sorted(std::move(items));
        };
        tree.navigation = folderItems(fs::path());
        bool hasRootPage = false;
        for (const DocFile& file : tree.files) {
            if (file.folderPage && file.folder.empty()) {
                tree.navigation.insert(tree.navigation.begin(), NavItem{file.label, file.path, {}});
                hasRootPage = true;
            }
        }
        if (!hasRootPage) tree.generated.push_back({tree.prefix, config.name, tree.navigation});
    }

    // The same file in the other languages (the latest version's), and in
    // the other versions.
    std::map<std::string, std::map<std::string, std::string>> alternates;   // relative file -> language -> route
    std::map<std::string, std::map<std::string, std::string>> inVersion;    // relative file -> version -> route
    for (const Tree& tree : trees) {
        const bool earlier = !tree.version.empty() && &tree != &trees.front();
        for (const DocFile& file : tree.files) {
            if (!earlier) alternates[file.relative][tree.language] = file.path;
            if (!tree.version.empty()) inVersion[file.relative][tree.version] = file.path;
        }
    }
    // A route's versions: each version's label and this page's address in
    // it, else that version's root, newest first.
    auto versionsOf = [&](const std::string& relative) {
        std::vector<std::pair<std::string, std::string>> out;
        if (!versioned) return out;
        for (const Tree& tree : trees) {
            if (tree.version.empty()) continue;
            auto found = inVersion.find(relative);
            std::string path = tree.prefix;
            if (found != inVersion.end() && found->second.count(tree.version)) path = found->second.at(tree.version);
            out.emplace_back(tree.version, path);
        }
        return out;
    };

    for (Tree& tree : trees) {
        AddSidebar(tree.sidebar, tree.navigation);
        for (DocFile& file : tree.files) {
            Route route;
            route.path = file.path;
            route.language = tree.language == config.language ? std::string() : tree.language;
            route.layout = "doc";
            route.title = file.title;
            route.description = file.description;
            route.bodyHtml = std::move(file.content.html);
            route.toc = std::move(file.content.toc);
            route.sidebar = tree.sidebar;
            if (!options.editUrl.empty() && &tree == &trees.front()) route.editUrl = options.editUrl + file.relative;
            route.date = file.matter.values["last_update"];
            const bool earlier = !tree.version.empty() && &tree != &trees.front();
            if (!earlier) {
                auto other = alternates.find(file.relative);
                if (other != alternates.end() && other->second.size() > 1) route.alternates = other->second;
            }
            if (!tree.version.empty()) {
                route.version = tree.version;
                route.versions = versionsOf(file.relative);
                if (earlier) {
                    route.latestPath = route.versions.front().second;
                    route.searchable = false;   // the search finds the latest
                }
            }
            AddRoute(std::move(route));
            ++report.pages;
        }
        // The generated index pages: each item with what its page says of itself.
        std::map<std::string, std::string> descriptionOf;
        for (const DocFile& file : tree.files) descriptionOf[file.path] = file.description;
        for (const GeneratedIndex& index : tree.generated) {
            Route route;
            route.path = index.path;
            route.language = tree.language == config.language ? std::string() : tree.language;
            route.layout = "doc";
            route.title = index.title;
            route.sidebar = tree.sidebar;
            if (!tree.version.empty()) {
                route.version = tree.version;
                route.versions = versionsOf(std::string());   // each version's root
                if (&tree != &trees.front()) {
                    route.latestPath = route.versions.front().second;
                    route.searchable = false;
                }
            }
            route.bodyHtml = "<h1>" + UltraCanvas::HTML::EscapeText(index.title) + "</h1>\n<ul class=\"us-cards\">\n";
            for (const NavItem& item : index.items) {
                route.bodyHtml += "<li><a href=\"" + UltraCanvas::HTML::EscapeText(Url(item.href)) + "\"><strong>" +
                                  UltraCanvas::HTML::EscapeText(item.label) + "</strong>";
                auto description = descriptionOf.find(item.href);
                if (description != descriptionOf.end() && !description->second.empty()) {
                    route.bodyHtml += "<span>" + UltraCanvas::HTML::EscapeText(description->second) + "</span>";
                } else if (!item.children.empty()) {
                    route.bodyHtml += "<span>" + std::to_string(item.children.size()) + " \xE2\x86\x92</span>";
                }
                route.bodyHtml += "</a></li>\n";
            }
            route.bodyHtml += "</ul>\n";
            AddRoute(std::move(route));
        }
    }
    return report;
}

} // namespace UltraSite

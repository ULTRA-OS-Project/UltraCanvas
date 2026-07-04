// Plugins/Documents/eBook/IEBookEngine.cpp
// Shared engine functionality: file loading, text extraction, search,
// and the extension → engine registry.
// Version: 2.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "IEBookEngine.h"

#include "HTMLReader/HTMLDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// BASE IMPLEMENTATION
// ============================================================================

bool EBookEngineBase::LoadFromFile(const std::string& filePath,
                                   const std::string& password) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Fail("Failed to open file: " + filePath);
        return false;
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        Fail("File is empty: " + filePath);
        return false;
    }
    file.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        Fail("Failed to read file: " + filePath);
        return false;
    }

    documentPath = filePath;
    return LoadFromMemory(std::move(data), password);
}

void EBookEngineBase::ResetBaseState() {
    loaded = false;
    lastError.clear();
    documentPath.clear();
    metadata = EBookMetadata();
    tableOfContents.clear();
}

void EBookEngineBase::Close() {
    ResetBaseState();
}

std::string EBookEngineBase::ExtractChapterText(int index) const {
    if (index < 0 || index >= GetChapterCount()) return {};
    return HTML::ExtractPlainText(GetChapter(index).content);
}

std::vector<EBookSearchResult> EBookEngineBase::Search(const std::string& query,
                                                       bool caseSensitive) const {
    std::vector<EBookSearchResult> results;
    if (query.empty() || !loaded) return results;

    auto fold = [caseSensitive](std::string s) {
        if (!caseSensitive) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }
        return s;
    };
    const std::string needle = fold(query);

    const int chapters = GetChapterCount();
    for (int chapter = 0; chapter < chapters; ++chapter) {
        std::string text = ExtractChapterText(chapter);
        std::string haystack = fold(text);

        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            EBookSearchResult result;
            result.chapterIndex = chapter;
            result.offsetInPage = static_cast<int>(pos);
            result.matchedText = text.substr(pos, query.size());

            const size_t kContext = 60;
            size_t before = pos > kContext ? pos - kContext : 0;
            size_t afterEnd = std::min(text.size(), pos + query.size() + kContext);
            result.contextBefore = text.substr(before, pos - before);
            result.contextAfter = text.substr(pos + query.size(),
                                              afterEnd - pos - query.size());

            if (chapter < static_cast<int>(tableOfContents.size())) {
                result.chapterTitle = tableOfContents[chapter].title;
            }

            results.push_back(std::move(result));
            pos += query.size();

            if (results.size() >= 500) return results;   // sanity cap
        }
    }
    return results;
}

EBookFormat EBookEngineBase::FormatFromExtension(const std::string& filePath) {
    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) return EBookFormat::Unknown;

    std::string ext = filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == "epub") return EBookFormat::EPUB;
    if (ext == "fb2") return EBookFormat::FB2;
    if (ext == "mobi" || ext == "prc") return EBookFormat::MOBI;
    if (ext == "azw") return EBookFormat::AZW;
    if (ext == "azw3" || ext == "kf8") return EBookFormat::AZW3;
    if (ext == "oeb" || ext == "opf") return EBookFormat::OEB;
    if (ext == "lit") return EBookFormat::LIT;
    if (ext == "lrf") return EBookFormat::LRF;
    if (ext == "rb") return EBookFormat::RB;
    if (ext == "tcr") return EBookFormat::TCR;
    if (ext == "cbz") return EBookFormat::CBZ;
    if (ext == "txt") return EBookFormat::TXT;
    if (ext == "html" || ext == "htm") return EBookFormat::HTML;
    if (ext == "xhtml") return EBookFormat::XHTML;
    return EBookFormat::Unknown;
}

// ============================================================================
// HREF UTILITIES
// ============================================================================

std::string NormalizeEBookPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;

    auto flush = [&]() {
        if (current.empty() || current == ".") {
            current.clear();
            return;
        }
        if (current == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(current);
        }
        current.clear();
    };

    for (char c : path) {
        if (c == '/' || c == '\\') flush();
        else current += c;
    }
    flush();

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) result += '/';
        result += parts[i];
    }
    return result;
}

std::string ResolveEBookHref(const std::string& baseFile,
                             const std::string& relative) {
    if (relative.empty()) return {};
    // A fragment refers into the same document (FB2 "#binaryId" resources).
    if (relative[0] == '#') return relative;
    if (relative[0] == '/') return NormalizeEBookPath(relative.substr(1));

    size_t slash = baseFile.find_last_of('/');
    std::string baseDir = (slash == std::string::npos) ? "" : baseFile.substr(0, slash + 1);
    return NormalizeEBookPath(baseDir + relative);
}

// ============================================================================
// REGISTRY
// ============================================================================

namespace {

std::map<std::string, EBookEngineFactory>& Registry() {
    static std::map<std::string, EBookEngineFactory> registry;
    return registry;
}

std::string ExtensionOf(const std::string& filePath) {
    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

} // namespace

void RegisterEBookEngine(const std::vector<std::string>& extensions,
                         EBookEngineFactory factory) {
    for (const auto& ext : extensions) {
        Registry()[ext] = factory;
    }
}

std::shared_ptr<IEBookEngine> CreateEBookEngineForFile(const std::string& filePath) {
    // Compound extensions first so "book.fb2.zip" finds the FB2 engine, not
    // a hypothetical "zip" one.
    std::string ext = ExtensionOf(filePath);
    size_t lastDot = filePath.find_last_of('.');
    if (lastDot != std::string::npos) {
        std::string inner = ExtensionOf(filePath.substr(0, lastDot));
        if (!inner.empty()) {
            auto it = Registry().find(inner + "." + ext);
            if (it != Registry().end()) return it->second();
        }
    }
    auto it = Registry().find(ext);
    if (it == Registry().end()) return nullptr;
    return it->second();
}

std::vector<std::string> GetRegisteredEBookExtensions() {
    std::vector<std::string> extensions;
    extensions.reserve(Registry().size());
    for (const auto& [ext, factory] : Registry()) {
        extensions.push_back(ext);
    }
    return extensions;
}

} // namespace UltraCanvas

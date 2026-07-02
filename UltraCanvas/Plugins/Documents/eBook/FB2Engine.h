// Plugins/Documents/eBook/FB2Engine.h
// FictionBook 2 (FB2) format engine.
//
// FB2 is a single-XML-file format: <description> holds the metadata,
// one or more <body> elements hold the text as nested <section>s, and
// <binary> elements carry base64 images. This engine parses the XML with
// the tolerant HTMLReader parser, converts FB2 markup to simple HTML per
// section, and hands out the top-level sections of the main body as
// chapters. Extra bodies (notes, comments) become one trailing chapter
// each. Plain .fb2 and zipped .fb2.zip files are both accepted.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"

#include <unordered_map>

namespace UltraCanvas {

namespace HTML { struct Node; }

class FB2Engine : public EBookEngineBase {
public:
    // ===== FORMAT =====
    EBookFormat GetFormat() const override { return EBookFormat::FB2; }
    std::string GetFormatName() const override { return "FictionBook 2"; }
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"fb2", "fb2.zip"};
    }

    // ===== LIFECYCLE =====
    bool LoadFromMemory(std::vector<uint8_t> data,
                        const std::string& password = "") override;
    void Close() override;

    // ===== CONTENT =====
    int GetChapterCount() const override {
        return static_cast<int>(chapters.size());
    }
    EBookChapter GetChapter(int index) const override;
    std::string GetStylesheets() const override;
    // href is "#binaryId" (as chapter <img src> references it) or a bare id.
    std::vector<uint8_t> GetResource(const std::string& href) const override;
    std::vector<uint8_t> GetCoverImage() const override;

    // ===== ENCODING (public for tests) =====
    // FB2 predates UTF-8 dominance: Russian files are commonly windows-1251.
    // Converts windows-1251 / iso-8859-1 (per the XML declaration) and
    // UTF-16 (per BOM) input to UTF-8; UTF-8 input passes through.
    static std::string ToUTF8(const std::vector<uint8_t>& data);

    // Decodes base64 ignoring embedded whitespace (public for tests).
    static std::vector<uint8_t> DecodeBase64(const std::string& encoded);

private:
    std::vector<EBookChapter> chapters;
    std::unordered_map<std::string, std::vector<uint8_t>> binaries;
    std::string coverId;   // binary id of the coverpage image (no '#')

    bool ParseXML(const std::string& xml);
    void ParseDescription(HTML::Node& description);
    void ParseBinaries(HTML::Node& fictionBook);
    void BuildChapters(HTML::Node& fictionBook);
    void BuildTOC();
};

// Registers the FB2 engine with the extension registry.
void RegisterFB2Engine();

} // namespace UltraCanvas

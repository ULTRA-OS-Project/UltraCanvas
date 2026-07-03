// Plugins/Documents/eBook/MOBIEngine.h
// Mobipocket / Kindle (MOBI, PRC, AZW, AZW3) format engine.
//
// A MOBI file is a Palm Database (PDB): a header, a record list, then the
// records themselves. Record 0 holds the PalmDOC header (compression + text
// length), the MOBI header (metadata offsets, image index, encoding) and an
// optional EXTH block (author, publisher, cover, ...). Text records follow,
// PalmDOC-compressed, and concatenate into one HTML blob; image records come
// after. This engine decompresses that HTML, splits it into chapters on the
// Mobipocket page-break markers, and hands the pieces to HTML::ElementBuilder
// — it performs no rendering itself.
//
// Scope: MOBI6 (.mobi/.prc/.azw and the MOBI6 half of combo .azw3 files) with
// no-compression and PalmDOC. HUFF/CDIC and KF8-only .azw3 are detected and
// reported rather than mis-rendered.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#pragma once

#include "IEBookEngine.h"

#include <cstdint>
#include <map>

namespace UltraCanvas {

class MOBIEngine : public EBookEngineBase {
public:
    // ===== FORMAT =====
    EBookFormat GetFormat() const override { return format; }
    std::string GetFormatName() const override { return formatName; }
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"mobi", "prc", "azw", "azw3"};
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
    // href is "mobiimg/N" (1-based image index, as chapter <img src> uses).
    std::vector<uint8_t> GetResource(const std::string& href) const override;
    std::vector<uint8_t> GetCoverImage() const override;

    // ===== HELPERS (public for tests) =====
    static uint16_t ReadBE16(const uint8_t* p);
    static uint32_t ReadBE32(const uint8_t* p);
    // PalmDOC (LZ77) decompression of one text record's payload.
    static std::vector<uint8_t> DecompressPalmDOC(const uint8_t* data, size_t size);
    // windows-1252 → UTF-8; UTF-8 input (textEncoding 65001) passes through.
    static std::string Cp1252ToUTF8(const std::string& in);

private:
    EBookFormat format = EBookFormat::MOBI;
    std::string formatName = "Mobipocket";

    std::vector<std::vector<uint8_t>> records;   // all PDB records
    std::vector<EBookChapter> chapters;
    std::vector<std::vector<uint8_t>> images;    // in record order, 1-based via GetResource
    int coverImageNumber = -1;                   // 1-based into images, or -1
    std::string fullHtml;                        // decompressed book HTML

    // Record-0 derived state.
    uint16_t compression = 1;
    uint32_t textRecordCount = 0;
    uint32_t firstImageIndex = 0;
    uint32_t textEncoding = 1252;
    uint32_t exthCoverOffset = 0xFFFFFFFF;       // EXTH 201, relative to firstImageIndex
    uint16_t extraDataFlags = 0;
    bool hasExth = false;
    std::string fullName;
    std::string pdbName;   // PDB database name (title fallback)
    std::map<uint32_t, std::vector<std::string>> exth;

    bool ParseRecords(const uint8_t* data, size_t size);
    bool ParseRecord0();
    void ParseExth(const uint8_t* data, size_t size);
    bool DecompressText();
    void ExtractImages();
    void BuildMetadata();
    void BuildChapters();
    void BuildTOC();

    std::string GetExthString(uint32_t type) const;
    std::vector<std::string> GetExthStrings(uint32_t type) const;

    // Size of one text record after trailing multibyte/index entries are
    // stripped (driven by extraDataFlags).
    size_t TextRecordPayloadSize(const std::vector<uint8_t>& record) const;
};

// Registers the MOBI engine with the extension registry.
void RegisterMOBIEngine();

} // namespace UltraCanvas

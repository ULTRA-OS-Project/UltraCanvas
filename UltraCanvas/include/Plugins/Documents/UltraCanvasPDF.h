// include/Plugins/Documents/UltraCanvasPDF.h
// PDF document interface — read, write, page operations, content editing.
// Backed by MuPDF when ULTRACANVAS_PLUGIN_PDF is enabled.
// Version: 1.0.0
// Last Modified: 2026-05-18
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_PDF_H
#define ULTRACANVAS_PDF_H

#include "UltraCanvasCommonTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== METADATA =====

struct PDFDocumentInfo {
    std::string title;
    std::string author;
    std::string subject;
    std::string keywords;
    std::string creator;
    std::string producer;
    std::string creationDate;     // raw PDF date string (D:YYYYMMDDHHmmSS...)
    std::string modificationDate;
    std::string pdfVersion;       // e.g. "1.7"
    int  pageCount = 0;
    bool isEncrypted = false;
    bool needsPassword = false;
    long fileSize = 0;            // -1 if loaded from memory
};

struct PDFPageInfo {
    int   pageNumber = 0;         // 1-based
    float widthPt   = 0.0f;       // in PDF user units (1pt = 1/72 inch)
    float heightPt  = 0.0f;
    int   rotationDegrees = 0;    // 0, 90, 180, 270
    std::string label;            // e.g. "iv", "12", may differ from pageNumber
};

// ===== RENDER SETTINGS =====

enum class PDFColorMode {
    RGBA,        // 4 bytes/pixel, premultiplied alpha (Cairo-compatible)
    Gray         // 1 byte/pixel
};

struct PDFRenderSettings {
    float dpi = 96.0f;                  // 72 = PDF native, 96 = screen default
    float zoom = 1.0f;                  // multiplier on top of dpi
    int   rotationDegrees = 0;          // additional rotation applied on render
    bool  antialias = true;
    PDFColorMode colorMode = PDFColorMode::RGBA;
};

struct PDFRenderedPage {
    int width = 0;          // in pixels
    int height = 0;
    int stride = 0;         // bytes per row
    PDFColorMode colorMode = PDFColorMode::RGBA;
    std::vector<uint8_t> pixels;

    bool IsValid() const { return !pixels.empty() && width > 0 && height > 0; }
};

// ===== TEXT EXTRACTION & SEARCH =====

struct PDFTextRun {
    int     pageNumber = 0;
    Rect2Df bbox;            // in PDF user units (origin top-left)
    std::string text;
};

struct PDFSearchOptions {
    bool caseSensitive = false;
    bool wholeWord = false;
    int  pageStart = 1;       // 1-based, inclusive; 0 = "from start"
    int  pageEnd = 0;         // 1-based, inclusive; 0 = "to end"
    int  maxHits = 0;         // 0 = unlimited
};

// ===== IMAGES =====

struct PDFImageRef {
    int     pageNumber = 0;
    int     indexOnPage = 0;
    Rect2Df bboxOnPage;       // placement on the page, in PDF user units
    int     widthPx = 0;
    int     heightPx = 0;
    std::string mimeType;     // "image/jpeg", "image/png", "image/x-raw"
};

// ===== ANNOTATIONS =====

struct PDFAnnotation {
    int     pageNumber = 0;
    int     indexOnPage = 0;
    Rect2Df bbox;
    std::string type;         // "FreeText", "Redact", "Text", "Highlight", …
    std::string contents;
    std::string author;
};

// ===== SAVE OPTIONS =====

struct PDFSaveOptions {
    bool linearize = false;            // produce a "web-optimized" PDF
    bool garbageCollect = true;        // strip orphan objects
    bool deflateStreams = true;        // re-compress streams
    bool cleanContentStreams = false;  // mutool-style content stream cleanup
};

// ===== ENGINE ENUM =====

enum class PDFEngineKind {
    Auto,
    MuPDF
};

// ===== MAIN INTERFACE =====

class IPDFDocument {
public:
    virtual ~IPDFDocument() = default;

    // ----- I/O -----
    virtual bool Open(const std::string& path, const std::string& password = "") = 0;
    virtual bool OpenFromBytes(const std::vector<uint8_t>& data,
                               const std::string& password = "") = 0;
    virtual bool Save(const std::string& path, const PDFSaveOptions& opts = {}) = 0;
    virtual bool SaveIncremental(const std::string& path) = 0;
    virtual void Close() = 0;

    virtual bool IsOpen()  const = 0;
    virtual bool IsDirty() const = 0;
    virtual const std::string& GetSourcePath() const = 0;

    // ----- Metadata -----
    virtual PDFDocumentInfo GetInfo() const = 0;
    virtual int             GetPageCount() const = 0;
    virtual PDFPageInfo     GetPageInfo(int pageNumber) const = 0;

    // ----- Rendering -----
    virtual PDFRenderedPage RenderPage(int pageNumber,
                                       const PDFRenderSettings& settings) = 0;
    virtual PDFRenderedPage RenderThumbnail(int pageNumber, int maxDim) = 0;

    // ----- Text -----
    virtual std::string             GetPageText(int pageNumber) = 0;
    virtual std::vector<PDFTextRun> ExtractTextRuns(int pageNumber) = 0;
    virtual std::vector<PDFTextRun> Search(const std::string& query,
                                           const PDFSearchOptions& opts = {}) = 0;

    // ----- Images -----
    virtual std::vector<PDFImageRef> ListImages(int pageNumber) = 0;
    virtual std::vector<uint8_t>     ExtractImageBytes(const PDFImageRef& ref) = 0;

    // ----- Page operations (writes; Milestone 2) -----
    virtual bool DeletePage(int pageNumber) = 0;
    virtual bool MovePage(int fromPage, int toPage) = 0;
    virtual bool InsertBlankPage(int at, float widthPt, float heightPt) = 0;
    virtual bool MergeFrom(IPDFDocument& other, int srcStart, int srcEnd,
                           int insertAt) = 0;

    // ----- Content editing (writes; Milestone 3) -----
    virtual bool AddTextAnnotation(int pageNumber, const Rect2Df& at,
                                   const std::string& text) = 0;
    virtual bool RedactRect(int pageNumber, const Rect2Df& area) = 0;

    // High-level: remove the underlying text in `target.bbox` (via a redaction
    // that is applied immediately) and overlay a free-text annotation showing
    // `newText` at the same rect. Lossless w.r.t. the rest of the page.
    // Note: the bbox is interpreted in PDF user units; the redaction wipes
    // *any* content intersecting that rect, so callers should pass a tight
    // bbox (typically from PDFTextRun::bbox returned by ExtractTextRuns).
    virtual bool ReplaceText(const PDFTextRun& target,
                             const std::string& newText) = 0;

    // Burn all pending redaction annotations on a page into the page content.
    virtual bool ApplyPendingRedactions(int pageNumber) = 0;

    // Read / remove annotations (FreeText, Redact, Text, …).
    virtual std::vector<PDFAnnotation> ListAnnotations(int pageNumber) = 0;
    virtual bool DeleteAnnotation(int pageNumber, int indexOnPage) = 0;

    // ----- Engine identity -----
    virtual std::string GetEngineName()    const = 0;
    virtual std::string GetEngineVersion() const = 0;
};

// ===== FACTORY =====

class PDFEngineFactory {
public:
    static std::unique_ptr<IPDFDocument> Create(PDFEngineKind kind = PDFEngineKind::Auto);
    static std::vector<PDFEngineKind>    Available();
    static bool                          IsAvailable(PDFEngineKind kind);
    static std::string                   GetKindName(PDFEngineKind kind);
};

// Convenience helper: open a path with the auto-selected engine.
// Returns nullptr on failure.
std::unique_ptr<IPDFDocument> OpenPDF(const std::string& path,
                                      const std::string& password = "");

} // namespace UltraCanvas

#endif // ULTRACANVAS_PDF_H

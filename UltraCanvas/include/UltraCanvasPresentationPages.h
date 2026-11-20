// include/UltraCanvasPresentationPages.h
// A presentation's slides as the pages of a document, for the page views:
// UltraCanvasPDFView shows a deck through this as it shows a PDF - each slide
// drawn by RenderPptxSlide at the view's zoom, the thumbnail strip, page
// navigation, and search over the slides' text (each hit framed on its
// shape). Read-only: every write answers false.
//
// Reads a PowerPoint deck (.pptx and kin, UltraCanvasPresentationPptx.h) or
// an OpenDocument presentation (.odp, .otp, .fodp,
// UltraCanvasPresentationOdp.h), a password-protected one with its password;
// GetLastOpenError tells NeedsPassword and WrongPassword from a damaged file,
// as the PDF engine does.
//
//   auto pages = std::make_unique<UltraCanvasPresentationPages>();
//   if (pages->OpenInMemory(path)) pdfView->SetDocument(std::move(pages));
//
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Documents/UltraCanvasPDF.h"
#include "UltraCanvasPresentationPptx.h"

#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasPresentationPages : public IPDFDocument {
public:
    // ----- I/O -----
    bool Open(const std::string& path, const std::string& password = "") override;
    bool OpenInMemory(const std::string& path, const std::string& password = "") override {
        return Open(path, password);   // the reader holds no file handle
    }
    // A .pptx package (or a protected one) in memory.
    bool OpenFromBytes(const std::vector<uint8_t>& data, const std::string& password = "") override;
    bool Save(const std::string&, const PDFSaveOptions& = {}) override { return false; }
    bool SaveIncremental(const std::string&) override { return false; }
    void Close() override;
    bool IsOpen() const override { return open_; }
    bool IsDirty() const override { return false; }
    PDFOpenError GetLastOpenError() const override { return lastError_; }
    const std::string& GetSourcePath() const override { return path_; }
    // Why the last open failed, in words a user can be shown.
    const std::string& GetLastErrorText() const { return errorText_; }

    // ----- Metadata -----
    PDFDocumentInfo GetInfo() const override;
    int GetPageCount() const override { return static_cast<int>(deck_.slides.size()); }
    PDFPageInfo GetPageInfo(int pageNumber) const override;

    // ----- Rendering -----
    PDFRenderedPage RenderPage(int pageNumber, const PDFRenderSettings& settings) override;
    PDFRenderedPage RenderThumbnail(int pageNumber, int maxDim) override;

    // ----- Text -----
    // The slide's text in reading order: its shapes' paragraphs, a table's
    // cells by row, then the speaker notes after a blank line.
    std::string GetPageText(int pageNumber) override;
    // One run per paragraph (and table cell), framed on its shape.
    std::vector<PDFTextRun> ExtractTextRuns(int pageNumber) override;
    std::vector<PDFTextChar> ExtractTextChars(int) override { return {}; }
    std::vector<PDFTextRun> Search(const std::string& query, const PDFSearchOptions& opts = {}) override;

    // ----- Images -----
    std::vector<PDFImageRef> ListImages(int pageNumber) override;
    std::vector<uint8_t> ExtractImageBytes(const PDFImageRef& ref) override;

    // ----- Writes: none -----
    bool DeletePage(int) override { return false; }
    bool MovePage(int, int) override { return false; }
    bool InsertBlankPage(int, float, float) override { return false; }
    bool MergeFrom(IPDFDocument&, int, int, int) override { return false; }
    bool AddTextAnnotation(int, const Rect2Df&, const std::string&) override { return false; }
    bool RedactRect(int, const Rect2Df&) override { return false; }
    bool ReplaceText(const PDFTextRun&, const std::string&) override { return false; }
    bool ApplyPendingRedactions(int) override { return false; }
    std::vector<PDFAnnotation> ListAnnotations(int) override { return {}; }
    bool DeleteAnnotation(int, int) override { return false; }

    std::string GetEngineName() const override { return "UltraCanvas presentations"; }
    std::string GetEngineVersion() const override { return "1.0.0"; }

    // The deck as read, for what pages do not carry: speaker notes, hidden slides.
    const PptxPresentation& GetPresentation() const { return deck_; }
    // A slide's speaker notes as plain text, paragraphs on their own lines.
    std::string GetNotes(int pageNumber) const;

private:
    bool Finish(bool read, const std::string& error, const std::string& password, bool checkPassword);
    PDFRenderedPage Render(int pageNumber, int widthPx);

    PptxPresentation deck_;
    std::string path_;
    std::string errorText_;
    PDFOpenError lastError_ = PDFOpenError::NoError;
    bool open_ = false;
};

} // namespace UltraCanvas

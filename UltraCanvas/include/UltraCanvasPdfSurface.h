// include/UltraCanvasPdfSurface.h
// A PDF file drawn page by page through the ordinary IRenderContext: whatever
// an element can draw on screen it can draw into a PDF, as vectors, with its
// text kept as selectable, searchable text.
//
//     std::string error;
//     auto pdf = UltraCanvasPdfSurface::CreateFile("/out/report.pdf", 595.28, 841.89, error);
//     IRenderContext* ctx = pdf->GetContext();      // units: points (1/72 inch)
//     ... draw page 1 ...
//     pdf->NextPage();
//     ... draw page 2 ...
//     if (!pdf->Finish(error)) { ... }
//
// The engine behind it is the render backend's own PDF writer (Cairo's), so
// no PDF library is involved and every platform gets the same output. Paths
// are UTF-8 on every platform.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class IRenderContext;

class UltraCanvasPdfSurface {
public:
    virtual ~UltraCanvasPdfSurface() = default;

    // A PDF written to `utf8Path`, its first page `widthPt` x `heightPt`.
    // Null (with `error` saying why) when the file cannot be created.
    static std::unique_ptr<UltraCanvasPdfSurface> CreateFile(const std::string& utf8Path,
                                                             double widthPt, double heightPt,
                                                             std::string& error);
    // A PDF kept in memory; GetBytes() after Finish() hands it over (to a
    // printer, an upload, an attachment).
    static std::unique_ptr<UltraCanvasPdfSurface> CreateInMemory(double widthPt, double heightPt,
                                                                 std::string& error);

    // What to draw the current page with. Its units are points, its origin
    // the page's top-left corner. Valid until Finish().
    virtual IRenderContext* GetContext() = 0;
    // Ends the current page and starts another, the same size unless one is
    // given.
    virtual void NextPage(double widthPt = 0.0, double heightPt = 0.0) = 0;
    // Document information written into the file.
    virtual void SetMetadata(const std::string& title, const std::string& author,
                             const std::string& subject = "") = 0;
    // Ends the last page and completes the file. False when writing failed.
    virtual bool Finish(std::string& error) = 0;
    // The finished PDF of CreateInMemory(); empty for a file.
    virtual const std::vector<uint8_t>& GetBytes() const = 0;
};

} // namespace UltraCanvas

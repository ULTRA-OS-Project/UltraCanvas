// core/UltraCanvasPresentationPages.cpp
// A presentation's slides as a document's pages - see UltraCanvasPresentationPages.h.
// Version: 1.1.0 - read through UltraCanvasFileLoader::LoadPresentation: a PowerPoint 97-2003 deck
//                  (.ppt, .pps, .pot) too
// Version: 1.0.1 - an .odp deck is read with its pictures, so they show on its slides
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPresentationPages.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasFileLoader.h"           // LoadPresentation: the reader the file's content asks for

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace UltraCanvas {

namespace {

constexpr double kEmuPerPoint = 12700.0;
constexpr int64_t kDefaultSlideWidth = 12192000;   // 16:9
constexpr int64_t kDefaultSlideHeight = 6858000;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// A shape's place in points, the page's user units.
Rect2Df PlaceOf(const PptxShape& shape) {
    return Rect2Df(static_cast<float>(shape.x / kEmuPerPoint), static_cast<float>(shape.y / kEmuPerPoint),
                   static_cast<float>(shape.width / kEmuPerPoint), static_cast<float>(shape.height / kEmuPerPoint));
}

// The text a page shows, one entry per paragraph or cell, with the shape it is in.
void ForEachText(const PptxSlide& slide, const std::function<void(const PptxShape&, const std::string&)>& visit) {
    for (const PptxShape& shape : slide.shapes) {
        for (const PptxParagraph& p : shape.paragraphs) {
            const std::string text = p.Text();
            if (!text.empty()) visit(shape, text);
        }
        for (const auto& row : shape.rows)
            for (const auto& cell : row)
                for (const PptxParagraph& p : cell) {
                    const std::string text = p.Text();
                    if (!text.empty()) visit(shape, text);
                }
        if (shape.kind == PptxShapeKind::Chart && !shape.chart.title.empty()) visit(shape, shape.chart.title);
    }
}

} // namespace

bool UltraCanvasPresentationPages::Open(const std::string& path, const std::string& password) {
    Close();
    path_ = path;
    std::string error;
    const bool read = UltraCanvasFileLoader::LoadPresentation(path, deck_, error, password, true);
    return Finish(read, error, password, true);
}

bool UltraCanvasPresentationPages::OpenFromBytes(const std::vector<uint8_t>& data, const std::string& password) {
    Close();
    std::string error;
    const bool read = ReadPptxPresentationFromPackage(data, deck_, error, true);
    (void)password;   // a package in memory is read as it is
    return Finish(read, error, password, false);
}

bool UltraCanvasPresentationPages::Finish(bool read, const std::string& error, const std::string& password,
                                          bool checkPassword) {
    if (read) {
        open_ = true;
        lastError_ = PDFOpenError::NoError;
        errorText_.clear();
        return true;
    }
    deck_ = PptxPresentation{};
    errorText_ = error;
    lastError_ = PDFOpenError::Failed;
    if (checkPassword) {
        // A protected deck says why, so a viewer asks for (another) password.
        std::string unused;
        switch (CheckOfficePassword(path_, password, unused)) {
            case OfficeDecryptStatus::PasswordRequired: lastError_ = PDFOpenError::NeedsPassword; break;
            case OfficeDecryptStatus::WrongPassword:
                lastError_ = password.empty() ? PDFOpenError::NeedsPassword : PDFOpenError::WrongPassword;
                break;
            default: break;
        }
    }
    return false;
}

void UltraCanvasPresentationPages::Close() {
    deck_ = PptxPresentation{};
    open_ = false;
}

PDFDocumentInfo UltraCanvasPresentationPages::GetInfo() const {
    PDFDocumentInfo info;
    info.pageCount = GetPageCount();
    if (!deck_.slides.empty()) info.title = deck_.slides.front().title;
    info.creator = IsOdpExtension(path_) ? "OpenDocument presentation" : "PowerPoint presentation";
    info.fileSize = -1;
    return info;
}

PDFPageInfo UltraCanvasPresentationPages::GetPageInfo(int pageNumber) const {
    PDFPageInfo info;
    if (pageNumber < 1 || pageNumber > GetPageCount()) return info;
    info.pageNumber = pageNumber;
    const int64_t w = deck_.slideWidth > 0 ? deck_.slideWidth : kDefaultSlideWidth;
    const int64_t h = deck_.slideHeight > 0 ? deck_.slideHeight : kDefaultSlideHeight;
    info.widthPt = static_cast<float>(w / kEmuPerPoint);
    info.heightPt = static_cast<float>(h / kEmuPerPoint);
    info.label = std::to_string(deck_.slides[static_cast<size_t>(pageNumber - 1)].number);
    return info;
}

PDFRenderedPage UltraCanvasPresentationPages::Render(int pageNumber, int widthPx) {
    PDFRenderedPage out;
    if (pageNumber < 1 || pageNumber > GetPageCount() || widthPx <= 0) return out;
    const std::shared_ptr<UCPixmap> pixmap = RenderPptxSlide(deck_, static_cast<size_t>(pageNumber - 1), widthPx);
    if (!pixmap) return out;
    out.width = pixmap->GetRawWidth();
    out.height = pixmap->GetRawHeight();
    out.stride = out.width * 4;
    out.colorMode = PDFColorMode::RGBA;
    out.pixels.resize(static_cast<size_t>(out.stride) * static_cast<size_t>(out.height));
    // The pixmap's premultiplied ARGB as the engine's plain R, G, B, A bytes.
    uint8_t* dst = out.pixels.data();
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const uint32_t argb = pixmap->GetPixel(x, y);
            const uint32_t a = argb >> 24;
            auto channel = [a](uint32_t c) { return static_cast<uint8_t>(a == 0 ? 0 : std::min<uint32_t>(255, (c * 255 + a / 2) / a)); };
            dst[0] = channel((argb >> 16) & 0xFF);
            dst[1] = channel((argb >> 8) & 0xFF);
            dst[2] = channel(argb & 0xFF);
            dst[3] = static_cast<uint8_t>(a);
            dst += 4;
        }
    }
    return out;
}

PDFRenderedPage UltraCanvasPresentationPages::RenderPage(int pageNumber, const PDFRenderSettings& settings) {
    const PDFPageInfo info = GetPageInfo(pageNumber);
    const double scale = std::max(0.01, static_cast<double>(settings.dpi) / 72.0 * settings.zoom);
    const int width = static_cast<int>(std::lround(info.widthPt * scale));
    return Render(pageNumber, std::clamp(width, 1, 16384));
}

PDFRenderedPage UltraCanvasPresentationPages::RenderThumbnail(int pageNumber, int maxDim) {
    const PDFPageInfo info = GetPageInfo(pageNumber);
    if (info.widthPt <= 0 || info.heightPt <= 0 || maxDim <= 0) return {};
    const double fit = std::min(maxDim / info.widthPt, maxDim / info.heightPt);
    return Render(pageNumber, std::max(1, static_cast<int>(std::lround(info.widthPt * fit))));
}

std::string UltraCanvasPresentationPages::GetPageText(int pageNumber) {
    if (pageNumber < 1 || pageNumber > GetPageCount()) return "";
    std::string out;
    ForEachText(deck_.slides[static_cast<size_t>(pageNumber - 1)],
                [&out](const PptxShape&, const std::string& text) { out += text + "\n"; });
    const std::string notes = GetNotes(pageNumber);
    if (!notes.empty()) out += "\n" + notes;
    return out;
}

std::string UltraCanvasPresentationPages::GetNotes(int pageNumber) const {
    if (pageNumber < 1 || pageNumber > GetPageCount()) return "";
    std::string out;
    for (const PptxParagraph& p : deck_.slides[static_cast<size_t>(pageNumber - 1)].notes) {
        const std::string text = p.Text();
        if (!text.empty()) out += (out.empty() ? "" : "\n") + text;
    }
    return out;
}

std::vector<PDFTextRun> UltraCanvasPresentationPages::ExtractTextRuns(int pageNumber) {
    std::vector<PDFTextRun> runs;
    if (pageNumber < 1 || pageNumber > GetPageCount()) return runs;
    ForEachText(deck_.slides[static_cast<size_t>(pageNumber - 1)], [&](const PptxShape& shape, const std::string& text) {
        PDFTextRun run;
        run.pageNumber = pageNumber;
        run.bbox = PlaceOf(shape);
        run.text = text;
        runs.push_back(std::move(run));
    });
    return runs;
}

std::vector<PDFTextRun> UltraCanvasPresentationPages::Search(const std::string& query, const PDFSearchOptions& opts) {
    std::vector<PDFTextRun> hits;
    if (query.empty()) return hits;
    const int first = std::max(1, opts.pageStart);
    const int last = opts.pageEnd > 0 ? std::min(opts.pageEnd, GetPageCount()) : GetPageCount();
    const std::string needle = opts.caseSensitive ? query : Lower(query);
    auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; };
    for (int page = first; page <= last; ++page) {
        for (const PDFTextRun& run : ExtractTextRuns(page)) {
            const std::string haystack = opts.caseSensitive ? run.text : Lower(run.text);
            for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
                if (opts.wholeWord) {
                    const bool before = at > 0 && isWord(haystack[at - 1]);
                    const size_t end = at + needle.size();
                    const bool after = end < haystack.size() && isWord(haystack[end]);
                    if (before || after) continue;
                }
                PDFTextRun hit = run;
                hit.text = run.text.substr(at, query.size());
                hits.push_back(std::move(hit));
                if (opts.maxHits > 0 && static_cast<int>(hits.size()) >= opts.maxHits) return hits;
            }
        }
    }
    return hits;
}

std::vector<PDFImageRef> UltraCanvasPresentationPages::ListImages(int pageNumber) {
    std::vector<PDFImageRef> images;
    if (pageNumber < 1 || pageNumber > GetPageCount()) return images;
    int index = 0;
    for (const PptxShape& shape : deck_.slides[static_cast<size_t>(pageNumber - 1)].shapes) {
        if (shape.kind != PptxShapeKind::Picture || !deck_.pictures.count(shape.pictureTarget)) continue;
        PDFImageRef ref;
        ref.pageNumber = pageNumber;
        ref.indexOnPage = index++;
        ref.bboxOnPage = PlaceOf(shape);
        const std::string lower = Lower(shape.pictureTarget);
        auto endsWith = [&lower](const std::string& tail) {
            return lower.size() >= tail.size() && lower.compare(lower.size() - tail.size(), tail.size(), tail) == 0;
        };
        ref.mimeType = endsWith(".jpg") || endsWith(".jpeg") ? "image/jpeg"
                     : endsWith(".png")                      ? "image/png"
                                                             : "application/octet-stream";
        images.push_back(ref);
    }
    return images;
}

std::vector<uint8_t> UltraCanvasPresentationPages::ExtractImageBytes(const PDFImageRef& ref) {
    if (ref.pageNumber < 1 || ref.pageNumber > GetPageCount()) return {};
    int index = 0;
    for (const PptxShape& shape : deck_.slides[static_cast<size_t>(ref.pageNumber - 1)].shapes) {
        if (shape.kind != PptxShapeKind::Picture) continue;
        const auto bytes = deck_.pictures.find(shape.pictureTarget);
        if (bytes == deck_.pictures.end()) continue;
        if (index++ == ref.indexOnPage) return bytes->second;
    }
    return {};
}

} // namespace UltraCanvas

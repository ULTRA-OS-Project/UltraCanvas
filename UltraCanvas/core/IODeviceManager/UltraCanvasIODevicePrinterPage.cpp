// core/IODeviceManager/UltraCanvasIODevicePrinterPage.cpp
// Page layout for the drawing-session print path: fitting, wrapping and
// pagination. Nothing here knows about any platform - the device is reached
// only through IPrintPageTarget - so all of it is testable against a fake.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterPage.h"

#include "UltraCanvasTextWrapping.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace UltraCanvas {

namespace {

// Splits on '\n', dropping a trailing '\r' so CRLF text does not wrap with an
// invisible carriage return measured into every line's width.
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> paragraphs;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        const size_t stop = (end == std::string::npos) ? text.size() : end;
        std::string line = text.substr(start, stop - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        paragraphs.push_back(std::move(line));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paragraphs;
}

}  // namespace

// ============================================================================
// LAYOUT HELPERS
// ============================================================================

IOPrintRect FitPreservingAspect(int sourceWidth, int sourceHeight,
                                const IOPrintRect& page) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || page.IsEmpty()) {
        return IOPrintRect{};
    }

    // Compared as a cross-multiplication rather than a ratio: at 600 dpi a
    // page is tens of thousands of dots, and float rounding there is a
    // visible misalignment, not a rounding error.
    const long long widthLimited =
        static_cast<long long>(page.width) * sourceHeight;
    const long long heightLimited =
        static_cast<long long>(page.height) * sourceWidth;

    int width = 0;
    int height = 0;
    if (widthLimited <= heightLimited) {
        width = page.width;
        height = static_cast<int>(widthLimited / sourceWidth);
    } else {
        height = page.height;
        width = static_cast<int>(heightLimited / sourceHeight);
    }

    width = std::max(width, 1);
    height = std::max(height, 1);

    return IOPrintRect{page.x + (page.width - width) / 2,
                       page.y + (page.height - height) / 2, width, height};
}

std::vector<std::string> WrapTextToWidth(const std::string& text,
                                         const IPrintPageTarget& target,
                                         int pixelHeight, int maxWidth) {
    std::vector<std::string> wrapped;
    if (text.empty() || pixelHeight <= 0 || maxWidth <= 0) {
        return wrapped;
    }

    // The line breaking itself is TextWrapping::WrapGreedy, which already
    // does the hard parts - UTF-8 boundaries, a binary-searched fit, and a
    // rule for when a word has to be split. It takes a measure callable
    // precisely so it can run against a device it knows nothing about, which
    // is what a printer is here. All this function adds is paragraphs, which
    // WrapGreedy does not handle because a caption has none.
    TextWrapping::Options options;
    options.lineWidth = maxWidth;

    // Page flow, not a caption: there is no line budget to truncate against,
    // so nothing is ever dropped and no line opens with an ellipsis.
    options.maxLines = std::numeric_limits<int>::max();

    // A printer clips at the hardware margin and says nothing, so unlike a
    // caption's inset there is no slack to spend keeping a word whole.
    options.overflowSlack = 0;
    options.breakTolerance = 0;

    // Off for prose: the camel-case rule exists to break file names well
    // ("UltraCanvas" / "Texter.exe"), and applying it to a document would
    // break ordinary capitalised words down the middle.
    options.camelCaseBreaks = false;

    const auto measure = [&target, pixelHeight](const std::string& piece) {
        return target.MeasureTextWidth(piece, pixelHeight);
    };

    for (const std::string& paragraph : SplitLines(text)) {
        if (paragraph.empty()) {
            // A blank line in the source is a blank line on the page: it is
            // paragraph separation, not noise to be collapsed.
            wrapped.push_back(std::string());
            continue;
        }
        const std::vector<std::string> paragraphLines =
            TextWrapping::WrapGreedy(measure, paragraph, options);
        wrapped.insert(wrapped.end(), paragraphLines.begin(),
                       paragraphLines.end());
    }

    return wrapped;
}

// ============================================================================
// IMAGE SOURCE
// ============================================================================

ImagePageSource::ImagePageSource(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        return;
    }
    const size_t bytes = static_cast<size_t>(width) *
                         static_cast<size_t>(height) * 4u;
    pixels.resize(bytes);
    std::memcpy(pixels.data(), rgba, bytes);
    imageWidth = width;
    imageHeight = height;
}

IODeviceResult ImagePageSource::Prepare(IPrintPageTarget& target) {
    if (pixels.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Image page source has no pixels");
    }

    const IOPrintPageMetrics metrics = target.GetMetrics();
    if (!metrics.IsValid()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "The printer reported no usable page geometry");
    }

    placement = FitPreservingAspect(imageWidth, imageHeight,
                                    metrics.PrintableArea());
    if (placement.IsEmpty()) {
        return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                     "The image does not fit the page at all");
    }
    return IODeviceResult::Ok();
}

IODeviceResult ImagePageSource::DrawPage(int pageIndex,
                                         IPrintPageTarget& target) {
    if (pageIndex != 0 || pixels.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Image page source has only page 0");
    }
    if (placement.IsEmpty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                     "DrawPage() before a successful Prepare()");
    }
    if (!target.DrawImage(pixels.data(), imageWidth, imageHeight, placement)) {
        return IODeviceResult::Error(IODeviceResultCode::IOError,
                                     "The device rejected the image");
    }
    return IODeviceResult::Ok();
}

// ============================================================================
// TEXT SOURCE
// ============================================================================

TextPageSource::TextPageSource(std::string utf8, int pixelHeight)
    : text(std::move(utf8)), requestedPixelHeight(pixelHeight) {}

IODeviceResult TextPageSource::Prepare(IPrintPageTarget& target) {
    const IOPrintPageMetrics metrics = target.GetMetrics();
    if (!metrics.IsValid()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "The printer reported no usable page geometry");
    }

    // A size in dots means nothing across devices - 12 dots is a readable
    // line on a 96 dpi screen and a scratch on a 1200 dpi printer - so the
    // default is stated in points and converted against the device's own
    // resolution.
    pixelHeight = requestedPixelHeight > 0
                      ? requestedPixelHeight
                      : std::max(1, (12 * metrics.dpiY) / 72);

    lines = WrapTextToWidth(text, target, pixelHeight, metrics.widthDots);

    const int lineHeight = target.GetLineHeight(pixelHeight);
    if (lineHeight <= 0) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "The device reported a non-positive line height");
    }

    linesPerPage = std::max(1, metrics.heightDots / lineHeight);
    pageCount = lines.empty()
                    ? 0
                    : static_cast<int>((lines.size() + linesPerPage - 1) /
                                       static_cast<size_t>(linesPerPage));
    return IODeviceResult::Ok();
}

IODeviceResult TextPageSource::DrawPage(int pageIndex,
                                        IPrintPageTarget& target) {
    if (pageIndex < 0 || pageIndex >= pageCount) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Page index outside the document");
    }

    const int lineHeight = target.GetLineHeight(pixelHeight);
    const int ascent = target.GetAscent(pixelHeight);

    const size_t first = static_cast<size_t>(pageIndex) *
                         static_cast<size_t>(linesPerPage);
    const size_t last = std::min(lines.size(),
                                 first + static_cast<size_t>(linesPerPage));

    for (size_t i = first; i < last; ++i) {
        if (lines[i].empty()) {
            continue;   // a blank line advances the baseline and draws nothing
        }
        const int baseline =
            ascent + static_cast<int>(i - first) * lineHeight;
        if (!target.DrawTextLine(lines[i], 0, baseline, pixelHeight)) {
            return IODeviceResult::Error(IODeviceResultCode::IOError,
                                         "The device rejected a line of text");
        }
    }
    return IODeviceResult::Ok();
}

}  // namespace UltraCanvas

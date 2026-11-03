// Apps/UltraVideo/UltraVideoTitles.cpp
// Titles drawn by UltraCanvas's text engine: see UltraVideoTitles.h.
// Version: 0.1.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraVideoTitles.h"

#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "UltraCanvasRasterText.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace UltraVideo {

using namespace UltraCanvas;
using namespace VideoFX;

namespace {
    RichTextAlign AlignOf(int align) {
        return align == 0 ? RichTextAlign::Left : align == 2 ? RichTextAlign::Right : RichTextAlign::Center;
    }

    double SizePx(const TitleSpan& t, int frameHeight) {
        return std::max(4.0, t.look.fontSize * frameHeight);
    }

    // `src` over `dst` (straight alpha), `src` placed at (ox, oy)
    void Over(UCRasterLayer& dst, const UCRasterLayer& src, int ox, int oy) {
        for (int y = 0; y < src.GetHeight(); ++y) {
            const int dy = y + oy;
            if (dy < 0 || dy >= dst.GetHeight()) continue;
            const uint8_t* s = src.Row(y);
            uint8_t* d = dst.Row(dy);
            for (int x = 0; x < src.GetWidth(); ++x) {
                const int dx = x + ox;
                if (dx < 0 || dx >= dst.GetWidth()) continue;
                const uint8_t* sp = s + x * 4;
                uint8_t* dp = d + dx * 4;
                const double sa = sp[3] / 255.0;
                if (sa <= 0.0) continue;
                const double da = dp[3] / 255.0;
                const double oa = sa + da * (1.0 - sa);
                for (int c = 0; c < 3; ++c)
                    dp[c] = static_cast<uint8_t>(std::lround((sp[c] * sa + dp[c] * da * (1.0 - sa)) / oa));
                dp[3] = static_cast<uint8_t>(std::lround(oa * 255.0));
            }
        }
    }

    // The rightmost column with anything drawn in it, +1; 0 for nothing
    int InkRight(const UCRasterLayer& layer) {
        int right = 0;
        for (int y = 0; y < layer.GetHeight(); ++y) {
            const uint8_t* row = layer.Row(y);
            for (int x = layer.GetWidth() - 1; x >= right; --x)
                if (row[x * 4 + 3] != 0) { right = x + 1; break; }
        }
        return right;
    }
}

std::shared_ptr<UCRichDocument> TitleWords(const TitleSpan& title, int frameHeight) {
    auto words = std::make_shared<UCRichDocument>();
    if (!title.words.empty()) {
        *words = ImportHTMLToRichDocument(title.words);
        // A word's own size was set for the frame the words were typed on:
        // it scales with the frame, as the title's size does.
        const double scale = static_cast<double>(std::max(16, frameHeight)) / std::max(16, title.wordsHeight);
        if (std::fabs(scale - 1.0) > 1e-9) {
            for (auto& block : words->blocks)
                for (auto& run : block.runs)
                    if (run.fontSizePt > 0.0f) run.fontSizePt = static_cast<float>(run.fontSizePt * scale);
        }
    }
    if (words->blocks.empty()) {
        // Plain text: a paragraph a line
        words->blocks.clear();
        std::istringstream lines(title.look.text);
        std::string line;
        while (std::getline(lines, line)) {
            RichDocBlock block;
            if (!line.empty()) { RichTextRun run; run.text = line; block.runs.push_back(run); }
            words->blocks.push_back(block);
        }
        if (words->blocks.empty()) words->blocks.emplace_back();
    }
    for (auto& block : words->blocks) {
        block.align = AlignOf(title.align);
        block.spaceBeforePt = 0.0f;
        block.spaceAfterPt = 0.0f;
    }
    SetTextLineSpacing(*words, static_cast<float>(SizePx(title, frameHeight)),
                       static_cast<float>(std::max(10.0, title.lineSpacing)));
    return words;
}

std::string TitleWordsToHtml(const UCRichDocument& words) {
    return words.ToHTML();
}

RasterTextContent TitleContent(const TitleSpan& title, int frameHeight, double boxWidth) {
    RasterTextContent c;
    c.document = TitleWords(title, frameHeight);
    c.x = 0.0;
    c.y = 0.0;
    c.width = std::max(8.0, boxWidth);
    c.fontFamily = title.fontFamily.empty() ? std::string("Sans") : title.fontFamily;
    c.fontSizePx = static_cast<float>(SizePx(title, frameHeight));
    const uint32_t rgb = title.look.textColor;
    c.colour = RasterPixel(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                           static_cast<uint8_t>(rgb & 0xFF), 255);
    c.letterSpacingPx = static_cast<float>(title.letterSpacing * frameHeight);
    return c;
}

TitlePicture RenderTitle(const TitleSpan& title, int frameHeight) {
    TitlePicture out;
    const int height = std::max(16, frameHeight);
    const double size = SizePx(title, height);
    std::string error;

    // The widest line, laid out with room to spare: the text's own width.
    RasterTextContent measure = TitleContent(title, height, size * 200.0 + 64.0);
    auto leftAligned = std::make_shared<UCRichDocument>(*measure.document);
    for (auto& block : leftAligned->blocks) block.align = RichTextAlign::Left;
    measure.document = leftAligned;
    const int measureW = static_cast<int>(std::ceil(measure.width));
    auto probe = RenderTextLayer(measure, measureW, static_cast<int>(std::ceil(size * 40.0)), error);
    if (!probe || !probe->text) return out;
    const int textH = std::max(1, static_cast<int>(std::ceil(probe->text->height)));
    const int textW = std::max(1, InkRight(*probe) + static_cast<int>(std::ceil(size * 0.1)));

    // The text in a box that wide, aligned as the title says
    RasterTextContent content = TitleContent(title, height, textW);
    auto text = RenderTextLayer(content, textW, textH, error);
    if (!text) return out;

    // The band behind it and the shadow under it, as the text filter drew them
    const int band = title.look.box ? std::max(2, static_cast<int>(std::lround(size / 3.0))) : 0;
    const int drop = title.look.shadow ? std::max(1, static_cast<int>(std::lround(size / 18.0))) : 0;
    const int pad = std::max(band, drop);
    const int w = textW + 2 * pad, h = textH + 2 * pad;
    UCRasterLayer picture(w, h, RasterPixel(0, 0, 0, 0));
    if (title.look.box) {
        const uint32_t rgb = title.look.boxColor;
        const auto alpha = static_cast<uint8_t>(std::lround(std::clamp(title.look.boxOpacity, 0.0, 1.0) * 255.0));
        picture.FillRect(Rect2Di(pad - band, pad - band, textW + 2 * band, textH + 2 * band),
                         RasterPixel(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                                     static_cast<uint8_t>(rgb & 0xFF), alpha));
    }
    if (title.look.shadow) {
        RasterTextContent dark = content;
        dark.colour = RasterPixel(0, 0, 0, 153);   // black at 0.6
        if (auto shadow = RenderTextLayer(dark, textW, textH, error)) Over(picture, *shadow, pad + drop, pad + drop);
    }
    Over(picture, *text, pad, pad);

    out.image.width = w;
    out.image.height = h;
    out.image.pixels.assign(picture.Data(), picture.Data() + picture.DataSize());
    out.textX = pad;
    out.textY = pad;
    out.textWidth = textW;
    out.textHeight = textH;
    return out;
}

TitlePicture CachedTitlePicture(const TitleSpan& title, int frameHeight) {
    // Everything that changes the picture makes the key
    std::ostringstream key;
    key << frameHeight << '|' << title.look.text << '|' << title.words << '|' << title.wordsHeight << '|'
        << title.fontFamily << '|'
        << title.align << '|' << title.lineSpacing << '|' << title.letterSpacing << '|' << title.look.fontSize << '|'
        << title.look.textColor << '|' << title.look.shadow << '|' << title.look.box << '|' << title.look.boxColor
        << '|' << title.look.boxOpacity;
    static std::mutex mutex;
    static std::list<std::pair<std::string, TitlePicture>> kept;   // most recent first
    static const size_t kKeep = 48;
    const std::string k = key.str();
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = kept.begin(); it != kept.end(); ++it) {
            if (it->first != k) continue;
            kept.splice(kept.begin(), kept, it);
            return kept.front().second;
        }
    }
    TitlePicture picture = RenderTitle(title, frameHeight);
    std::lock_guard<std::mutex> lock(mutex);
    kept.emplace_front(k, picture);
    if (kept.size() > kKeep) kept.pop_back();
    return picture;
}

Rect2Dd TitlePictureRect(const TitleSpan& title, int frameW, int frameH, int pictureW, int pictureH) {
    const VideoFXOverlay& o = title.look;
    if (o.anchor == VideoFXAnchor::Custom)
        return Rect2Dd(o.x * frameW, o.y * frameH, pictureW, pictureH);
    const double m = std::lround(o.margin * frameH);
    int col = 1, row = 1;
    switch (o.anchor) {
        case VideoFXAnchor::TopLeft:     col = 0; row = 0; break;
        case VideoFXAnchor::Top:         col = 1; row = 0; break;
        case VideoFXAnchor::TopRight:    col = 2; row = 0; break;
        case VideoFXAnchor::Left:        col = 0; row = 1; break;
        case VideoFXAnchor::Center:      col = 1; row = 1; break;
        case VideoFXAnchor::Right:       col = 2; row = 1; break;
        case VideoFXAnchor::BottomLeft:  col = 0; row = 2; break;
        case VideoFXAnchor::Bottom:      col = 1; row = 2; break;
        case VideoFXAnchor::BottomRight: col = 2; row = 2; break;
        case VideoFXAnchor::Custom:      break;
    }
    const double x = col == 0 ? m : col == 1 ? (frameW - pictureW) / 2.0 : frameW - pictureW - m;
    const double y = row == 0 ? m : row == 1 ? (frameH - pictureH) / 2.0 : frameH - pictureH - m;
    return Rect2Dd(std::floor(x), std::floor(y), pictureW, pictureH);
}

} // namespace UltraVideo

// core/UltraCanvasMemoryStatsPanel.cpp
// Implementation of the memory-usage "task manager" widget.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#include "UltraCanvasMemoryStatsPanel.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>

namespace UltraCanvas {

namespace {
    const Color kHeaderBg   (60, 63, 70, 255);
    const Color kHeaderText (235, 235, 235, 255);
    const Color kScopeText  (30, 30, 30, 255);
    const Color kCatText    (90, 90, 90, 255);
    const Color kBarTrack   (225, 225, 225, 255);
    const Color kBarFill    (0, 120, 215, 255);
    const Color kRowDivider (230, 230, 230, 255);

    // Distinct, stable colors per category for the breakdown bars/legend.
    Color CategoryColor(MemoryCategory c) {
        switch (c) {
            case MemoryCategory::UIElement:      return Color(120, 144, 156, 255);
            case MemoryCategory::ImagePixels:    return Color(33, 150, 243, 255);
            case MemoryCategory::VectorGeometry: return Color(0, 188, 212, 255);
            case MemoryCategory::TextBuffer:     return Color(76, 175, 80, 255);
            case MemoryCategory::VideoFrame:     return Color(233, 30, 99, 255);
            case MemoryCategory::AudioBuffer:    return Color(156, 39, 176, 255);
            case MemoryCategory::DocumentData:   return Color(255, 152, 0, 255);
            case MemoryCategory::GLResource:     return Color(121, 85, 72, 255);
            case MemoryCategory::Cache:          return Color(158, 158, 158, 255);
            default:                             return Color(189, 189, 189, 255);
        }
    }
} // namespace

UltraCanvasMemoryStatsPanel::UltraCanvasMemoryStatsPanel(
        const std::string& id, float x, float y, float w, float h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    SetBackgroundColor(Color(248, 248, 248, 255));
}

void UltraCanvasMemoryStatsPanel::RefreshIfStale() {
    uint64_t gen = MemoryStats::Generation();
    if (forceRefresh || gen != lastGeneration) {
        cached = MemoryStats::Snapshot();
        lastGeneration = cached.generation;
        forceRefresh = false;
    }
}

void UltraCanvasMemoryStatsPanel::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    // Base draws background + borders.
    UltraCanvasUIElement::Render(ctx, dirtyRect);

    RefreshIfStale();

    // ctx is already translated to the element origin, so draw in local coords.
    const float W = GetWidth();
    const float H = GetHeight();
    if (W <= 0 || H <= 0) return;

    const float pad = 8.0f;
    const float valueColW = 90.0f;   // right-aligned byte column width
    float y = 0.0f;

    FontStyle font;
    font.fontSize = 12.0;

    // ---- Header ----
    const float headerH = rowHeight + 6.0f;
    ctx->SetFillPaint(kHeaderBg);
    ctx->FillRectangle(Rect2Dd(0, 0, W, headerH));

    font.fontSize = 13.0;
    ctx->SetFontStyle(font);
    ctx->SetCurrentPaint(kHeaderText);
    ctx->DrawText("Memory Usage", Point2Dd(pad, 7));

    std::string totals = MemoryStats::FormatBytes(cached.hostTotalBytes);
    if (cached.gpuTotalBytes > 0)
        totals += "  (+" + MemoryStats::FormatBytes(cached.gpuTotalBytes) + " GPU)";
    int tw = ctx->GetTextLineWidth(totals);
    ctx->DrawText(totals, Point2Dd(W - pad - tw, 7));
    y = headerH + 4.0f;

    if (!MemoryStats::IsEnabled()) {
        font.fontSize = 12.0;
        ctx->SetFontStyle(font);
        ctx->SetCurrentPaint(kCatText);
        ctx->DrawText("Memory statistics are disabled in this build "
                      "(UC_ENABLE_MEMORY_STATS=0).", Point2Dd(pad, y + 4));
        return;
    }

    // Largest scope total drives the usage-bar scale.
    int64_t maxScope = 1;
    for (const auto& s : cached.scopes)
        maxScope = std::max<int64_t>(maxScope, s.totalBytes + s.gpuBytes);

    // ---- One block per scope ----
    for (const auto& scope : cached.scopes) {
        if (y > H) break;

        int64_t scopeTotal = scope.totalBytes + scope.gpuBytes;

        // Skip the empty Global scope to reduce noise, unless it's the only one.
        if (scope.id == GlobalScope && scopeTotal == 0 && cached.scopes.size() > 1)
            continue;

        // Scope row: name + total + usage bar.
        font.fontSize = 12.0;
        font.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(font);
        ctx->SetCurrentPaint(kScopeText);
        ctx->DrawText(scope.name, Point2Dd(pad, y + 2));

        std::string bytesStr = MemoryStats::FormatBytes(scopeTotal);
        int bw = ctx->GetTextLineWidth(bytesStr);
        ctx->DrawText(bytesStr, Point2Dd(W - pad - bw, y + 2));
        font.fontWeight = FontWeight::Normal;
        y += rowHeight;

        // Usage bar (stacked by category so the color shows WHAT it is).
        const float barH = 8.0f;
        const float barW = std::max(0.0f, W - 2 * pad);
        ctx->SetFillPaint(kBarTrack);
        ctx->FillRectangle(Rect2Dd(pad, y, barW, barH));
        float bx = pad;
        for (const auto& cat : scope.byCategory) {
            if (cat.bytes <= 0) continue;
            float segW = barW * (static_cast<float>(cat.bytes) / static_cast<float>(maxScope));
            ctx->SetFillPaint(CategoryColor(cat.category));
            ctx->FillRectangle(Rect2Dd(bx, y, segW, barH));
            bx += segW;
        }
        y += barH + 4.0f;

        // Per-category breakdown (what the memory is used for).
        font.fontSize = 11.0;
        ctx->SetFontStyle(font);
        for (const auto& cat : scope.byCategory) {
            if (y > H) break;
            if (cat.bytes == 0 && cat.liveCount == 0) continue;
            ctx->SetFillPaint(CategoryColor(cat.category));
            ctx->FillRectangle(Rect2Dd(pad + 4, y + 4, 8, 8));

            ctx->SetCurrentPaint(kCatText);
            std::string label = MemoryStats::CategoryName(cat.category) +
                                "  (" + std::to_string(cat.liveCount) + ")";
            ctx->DrawText(label, Point2Dd(pad + 18, y + 1));

            std::string cb = MemoryStats::FormatBytes(cat.bytes);
            int cbw = ctx->GetTextLineWidth(cb);
            ctx->DrawText(cb, Point2Dd(W - pad - cbw, y + 1));
            y += rowHeight - 2.0f;
        }

        // Optional concrete-type breakdown.
        if (showTypes) {
            for (const auto& t : scope.byType) {
                if (y > H) break;
                if (t.bytes == 0) continue;
                ctx->SetCurrentPaint(Color(140, 140, 140, 255));
                ctx->DrawText(t.typeName, Point2Dd(pad + 18, y + 1));
                std::string tb = MemoryStats::FormatBytes(t.bytes);
                int tbw = ctx->GetTextLineWidth(tb);
                ctx->DrawText(tb, Point2Dd(W - pad - tbw, y + 1));
                y += rowHeight - 4.0f;
            }
        }

        // Divider.
        y += 4.0f;
        ctx->SetStrokePaint(kRowDivider);
        ctx->DrawLine(Point2Dd(pad, y), Point2Dd(W - pad, y));
        y += 6.0f;
    }
}

} // namespace UltraCanvas

// UltraCanvasWaveformElement.cpp
// Amplitude waveform display implementation
// Version: 1.0.0
// Last Modified: 2026-06-22
// Author: UltraCanvas Framework

#include "UltraCanvasWaveformElement.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasWaveformElement::UltraCanvasWaveformElement(
            const std::string& id, int x, int y, int w, int h)
        : UltraCanvasUIElement(id, x, y, w, h) {
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasWaveformElement::SetData(const WaveformChannelData& data) {
        waveformData = data;
        // Re-clamp playhead in case duration shrank.
        SetPlayheadTime(playheadSeconds);
        RequestRedraw();
    }

    void UltraCanvasWaveformElement::ClearData() {
        waveformData.Clear();
        playheadSeconds = 0.0;
        RequestRedraw();
    }

// =============================================================================
// PLAYHEAD
// =============================================================================

    void UltraCanvasWaveformElement::SetPlayheadTime(double seconds) {
        double dur = waveformData.duration;
        double clamped = seconds;
        if (dur > 0.0) {
            clamped = std::max(0.0, std::min(seconds, dur));
        } else {
            clamped = std::max(0.0, seconds);
        }
        if (clamped != playheadSeconds) {
            playheadSeconds = clamped;
            if (onPlayheadMove) onPlayheadMove(playheadSeconds);
        }
        RequestRedraw();
    }

    void UltraCanvasWaveformElement::SetPlayheadVisible(bool visible) {
        showPlayhead = visible;
        RequestRedraw();
    }

// =============================================================================
// STYLING
// =============================================================================

    void UltraCanvasWaveformElement::SetRenderStyle(WaveformRenderStyle style) {
        renderStyle = style; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetOverlay(WaveformOverlay o) {
        overlay = o; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetWaveColor(const Color& c) {
        waveColor = c; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetRMSColor(const Color& c) {
        rmsColor = c; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetBackgroundColor(const Color& c) {
        bgColor = c; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetPlayheadColor(const Color& c) {
        playheadColor = c; RequestRedraw();
    }
    void UltraCanvasWaveformElement::SetZeroAxisColor(const Color& c) {
        zeroAxisColor = c; RequestRedraw();
    }

// =============================================================================
// COORDINATE HELPERS
// =============================================================================

    double UltraCanvasWaveformElement::TimeFromX(float localX) const {
        Rect2Df b = GetLocalBounds();
        if (b.width <= 0 || waveformData.duration <= 0.0) return 0.0;
        float t = (localX - b.x) / b.width;
        t = std::max(0.0f, std::min(t, 1.0f));
        return t * waveformData.duration;
    }

    float UltraCanvasWaveformElement::XFromTime(double seconds) const {
        Rect2Df b = GetLocalBounds();
        if (waveformData.duration <= 0.0) return b.x;
        double frac = seconds / waveformData.duration;
        frac = std::max(0.0, std::min(frac, 1.0));
        return b.x + static_cast<float>(frac * b.width);
    }

    void UltraCanvasWaveformElement::ApplySeekFromX(float localX) {
        double t = TimeFromX(localX);
        playheadSeconds = t;
        if (onSeek) onSeek(t);
        if (onPlayheadMove) onPlayheadMove(t);
        RequestRedraw();
    }

// =============================================================================
// RENDER
// =============================================================================

    void UltraCanvasWaveformElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;
        Rect2Df b = GetLocalBounds();
        if (b.width <= 0 || b.height <= 0) return;

        const float midY   = b.y + b.height * 0.5f;
        const float halfH  = b.height * 0.5f;

        // ----- Background -----
        ctx->SetFillPaint(bgColor);
        ctx->FillRectangle(Rect2Di(static_cast<int>(b.x), static_cast<int>(b.y),
                                   static_cast<int>(b.width), static_cast<int>(b.height)));

        // ----- Zero axis (behind the wave) -----
        bool drawZero = (overlay == WaveformOverlay::ZeroAxis ||
                         overlay == WaveformOverlay::RMSAndZeroAxis);
        if (drawZero) {
            ctx->SetStrokePaint(zeroAxisColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(b.x, midY), Point2Dd(b.x + b.width, midY));
        }

        const int blocks = static_cast<int>(waveformData.maxValues.size());
        if (blocks <= 0) return;

        // Map each pixel column to a block range so we render at most one
        // column per device pixel (cheap + crisp regardless of block count).
        const int columns = std::max(1, static_cast<int>(b.width));

        auto blockRangeForColumn = [&](int col, int& startBlk, int& endBlk) {
            double frac0 = static_cast<double>(col) / columns;
            double frac1 = static_cast<double>(col + 1) / columns;
            startBlk = static_cast<int>(frac0 * blocks);
            endBlk   = static_cast<int>(frac1 * blocks);
            if (endBlk <= startBlk) endBlk = startBlk + 1;
            if (endBlk > blocks)    endBlk = blocks;
        };

        bool drawRMS = (waveformData.HasRMS() &&
                        (overlay == WaveformOverlay::RMS ||
                         overlay == WaveformOverlay::RMSAndZeroAxis));

        // ----- Peak envelope -----
        if (renderStyle == WaveformRenderStyle::Filled) {
            // Build a single filled path: top edge (max) left->right,
            // then bottom edge (min) right->left.
            ctx->SetFillPaint(waveColor);
            ctx->ClearPath();
            bool started = false;
            // Top edge
            for (int col = 0; col < columns; ++col) {
                int s, e; blockRangeForColumn(col, s, e);
                float mx = waveformData.maxValues[s];
                for (int i = s + 1; i < e; ++i) mx = std::max(mx, waveformData.maxValues[i]);
                float x = b.x + col;
                float y = midY - mx * halfH;
                if (!started) { ctx->MoveTo(x, y); started = true; }
                else          { ctx->LineTo(x, y); }
            }
            // Bottom edge (reverse)
            for (int col = columns - 1; col >= 0; --col) {
                int s, e; blockRangeForColumn(col, s, e);
                float mn = waveformData.minValues.empty() ? 0.0f : waveformData.minValues[s];
                for (int i = s + 1; i < e && i < (int)waveformData.minValues.size(); ++i)
                    mn = std::min(mn, waveformData.minValues[i]);
                float x = b.x + col;
                float y = midY - mn * halfH;
                ctx->LineTo(x, y);
            }
            ctx->ClosePath();
            ctx->Fill();
        }
        else { // Outline or Bars — both stroke vertical lines per column
            ctx->SetStrokePaint(waveColor);
            ctx->SetStrokeWidth(1.0f);
            for (int col = 0; col < columns; ++col) {
                int s, e; blockRangeForColumn(col, s, e);
                float mx = waveformData.maxValues[s];
                float mn = waveformData.minValues.empty() ? 0.0f : waveformData.minValues[s];
                for (int i = s + 1; i < e; ++i) {
                    mx = std::max(mx, waveformData.maxValues[i]);
                    if (i < (int)waveformData.minValues.size())
                        mn = std::min(mn, waveformData.minValues[i]);
                }
                float x = b.x + col + 0.5f;
                float yTop = midY - mx * halfH;
                float yBot = midY - mn * halfH;
                ctx->DrawLine(Point2Dd(x, yTop), Point2Dd(x, yBot));
            }
        }

        // ----- RMS band (drawn on top of peak envelope) -----
        if (drawRMS) {
            ctx->SetStrokePaint(rmsColor);
            ctx->SetStrokeWidth(1.0f);
            for (int col = 0; col < columns; ++col) {
                int s, e; blockRangeForColumn(col, s, e);
                float r = waveformData.rmsValues[s];
                for (int i = s + 1; i < e; ++i) r = std::max(r, waveformData.rmsValues[i]);
                float x = b.x + col + 0.5f;
                float yTop = midY - r * halfH;
                float yBot = midY + r * halfH;
                ctx->DrawLine(Point2Dd(x, yTop), Point2Dd(x, yBot));
            }
        }

        // ----- Playhead -----
        if (showPlayhead && waveformData.duration > 0.0) {
            float px = XFromTime(playheadSeconds);
            ctx->SetStrokePaint(playheadColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(px, b.y), Point2Dd(px, b.y + b.height));
        }
    }

// =============================================================================
// EVENTS
// =============================================================================

    bool UltraCanvasWaveformElement::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (!interactiveSeek) return false;

        switch (event.type) {
            case UCEventType::MouseDown: {
                Point2Df p(static_cast<float>(event.pointer.x),
                           static_cast<float>(event.pointer.y));
                if (Contains(p)) {
                    isSeeking = true;
                    Rect2Df b = GetLocalBounds();
                    // event coords are window-local; convert to element-local x.
                    Point2Df posInWindow = GetPositionInWindow();
                    float localX = b.x + (p.x - posInWindow.x);
                    ApplySeekFromX(localX);
                    return true;
                }
                break;
            }
            case UCEventType::MouseMove: {
                if (isSeeking) {
                    Rect2Df b = GetLocalBounds();
                    Point2Df posInWindow = GetPositionInWindow();
                    float localX = b.x + (static_cast<float>(event.pointer.x) - posInWindow.x);
                    ApplySeekFromX(localX);
                    return true;
                }
                break;
            }
            case UCEventType::MouseUp: {
                if (isSeeking) {
                    isSeeking = false;
                    return true;
                }
                break;
            }
            default:
                break;
        }
        return false;
    }

// =============================================================================
// FACTORY
// =============================================================================

    std::shared_ptr<UltraCanvasWaveformElement>
    CreateWaveformElement(const std::string& id, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasWaveformElement>(id, x, y, w, h);
    }

} // namespace UltraCanvas

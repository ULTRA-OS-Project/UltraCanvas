// UltraCanvasWaveformElement.h
// Amplitude waveform display with min/max envelope, optional RMS overlay, and playhead
// Version: 1.0.0
// Last Modified: 2026-06-22
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <functional>
#include <string>

// X11 defines `None` as a macro that clobbers the WaveformOverlay::None
// enumerator (UltraCanvasCommonTypes.h pulls in <X11/Xlib.h> on Linux).
#ifdef None
#undef None
#endif

// NOTE: This header intentionally does NOT include any AudioFX header.
// The element is module-agnostic. The optional SetFromAudioFX() adapter is
// declared as a template so AudioFXWaveformData is only required at the call
// site that actually uses it (in code that already includes AudioFX), keeping
// UltraCanvas UI core free of an AudioFX compile dependency.

namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

    enum class WaveformRenderStyle {
        Filled,     // Solid filled envelope between min and max (default)
        Outline,    // Stroked outline of the envelope only
        Bars        // One vertical bar per block (oscilloscope-style)
    };

    enum class WaveformOverlay {
        None,
        RMS,                // Draw RMS band inside the peak envelope
        ZeroAxis,           // Draw horizontal zero/silence line
        RMSAndZeroAxis      // Both
    };

// =============================================================================
// NEUTRAL DATA STRUCT
// =============================================================================

    // Module-agnostic amplitude data. One entry per "block" of samples.
    // Values are normalized to [-1, 1] (min/max) and [0, 1] (rms).
    // This is structurally compatible with AudioFXWaveformData but carries no
    // dependency on the AudioFX module.
    struct WaveformChannelData {
        std::vector<float> minValues;   // Minimum sample value per block (<= 0 typically)
        std::vector<float> maxValues;   // Maximum sample value per block (>= 0 typically)
        std::vector<float> rmsValues;   // RMS per block (>= 0), optional
        int    samplesPerBlock = 256;
        int    totalBlocks = 0;
        double duration = 0.0;          // Total duration in seconds

        bool IsValid() const {
            return !maxValues.empty() &&
                   minValues.size() == maxValues.size();
        }
        bool HasRMS() const {
            return rmsValues.size() == maxValues.size() && !rmsValues.empty();
        }
        void Clear() {
            minValues.clear();
            maxValues.clear();
            rmsValues.clear();
            totalBlocks = 0;
            duration = 0.0;
        }
    };

// =============================================================================
// WAVEFORM ELEMENT
// =============================================================================

    class UltraCanvasWaveformElement : public UltraCanvasUIElement {
    public:
        // Standard (id, x, y, w, h) constructor — no uid param, per base contract.
        UltraCanvasWaveformElement(const std::string& id, int x, int y, int w, int h);
        ~UltraCanvasWaveformElement() override = default;

        // ===== DATA =====
        void SetData(const WaveformChannelData& data);
        const WaveformChannelData& GetData() const { return waveformData; }
        void ClearData();

        // Optional adapter — templated so the AudioFX type is only needed where
        // this is actually called. AudioFXWaveformData has matching member names
        // (minValues / maxValues / rmsValues / samplesPerBlock / totalBlocks /
        // duration), so the copy is field-by-field.
        template <typename AudioFXWaveformDataT>
        void SetFromAudioFX(const AudioFXWaveformDataT& fx) {
            WaveformChannelData d;
            d.minValues      = fx.minValues;
            d.maxValues      = fx.maxValues;
            d.rmsValues      = fx.rmsValues;
            d.samplesPerBlock = fx.samplesPerBlock;
            d.totalBlocks    = fx.totalBlocks;
            d.duration       = fx.duration;
            SetData(d);
        }

        // ===== PLAYHEAD =====
        // Position in seconds. Clamped to [0, duration]. Fires onPlayheadMove.
        void SetPlayheadTime(double seconds);
        double GetPlayheadTime() const { return playheadSeconds; }
        void SetPlayheadVisible(bool visible);
        bool IsPlayheadVisible() const { return showPlayhead; }

        // ===== STYLING =====
        void SetRenderStyle(WaveformRenderStyle style);
        WaveformRenderStyle GetRenderStyle() const { return renderStyle; }
        void SetOverlay(WaveformOverlay overlay);
        WaveformOverlay GetOverlay() const { return overlay; }

        void SetWaveColor(const Color& c);
        void SetRMSColor(const Color& c);
        void SetBackgroundColor(const Color& c);
        void SetPlayheadColor(const Color& c);
        void SetZeroAxisColor(const Color& c);

        // Allow seeking by clicking/dragging the waveform.
        void SetInteractiveSeek(bool enabled) { interactiveSeek = enabled; }
        bool IsInteractiveSeek() const { return interactiveSeek; }

        // ===== CALLBACKS (base-verb form) =====
        std::function<void(double)> onSeek;          // user-initiated seek (seconds)
        std::function<void(double)> onPlayheadMove;  // playhead position changed (seconds)

        // ===== UIElement OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // Data
        WaveformChannelData waveformData;

        // Playhead
        double playheadSeconds = 0.0;
        bool   showPlayhead = true;

        // Style
        WaveformRenderStyle renderStyle = WaveformRenderStyle::Filled;
        WaveformOverlay     overlay     = WaveformOverlay::ZeroAxis;
        Color waveColor      = Color(64, 224, 160, 255);   // teal-green, like the reference
        Color rmsColor       = Color(40, 160, 110, 255);
        Color bgColor        = Color(16, 16, 16, 255);     // near-black
        Color playheadColor  = Color(220, 40, 40, 255);    // red cursor
        Color zeroAxisColor  = Color(80, 80, 80, 255);

        // Interaction
        bool interactiveSeek = true;
        bool isSeeking = false;

        // Helpers
        double TimeFromX(float localX) const;     // map x within bounds -> seconds
        float  XFromTime(double seconds) const;   // map seconds -> local x
        void   ApplySeekFromX(float localX);      // compute seek time + fire onSeek
    };

// =============================================================================
// FACTORY — Create[Name]Element() convention
// =============================================================================

    std::shared_ptr<UltraCanvasWaveformElement>
    CreateWaveformElement(const std::string& id, int x, int y, int w, int h);

} // namespace UltraCanvas

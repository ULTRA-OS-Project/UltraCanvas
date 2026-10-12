// UltraCanvasTimecodeField.h
// A field for entering and showing `HH:MM:SS:FF` timecode, and the large
// read-only readout a transport bar shows. The value is a count of whole
// frames at a frame rate. A click selects one of the four segments, the
// arrow keys step it, Tab moves to the next; typed digits fill the timecode
// from the right with the untyped leading zeros dimmed, a leading `+` or `-`
// enters an offset in frames from the value, Enter commits, Esc restores the
// last valid value, and an entry that is not a timecode (seconds of 75, a
// frame past the rate) shows a danger border until it is corrected. It is an
// input element in its own right - four segments, no caret - so it owns its
// entry buffer; it is not built over the text input.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasTheme.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

struct TimecodeFieldStyle {
    Color background    = Color(255, 255, 255);
    Color border        = Color(122, 130, 139);
    Color borderFocused = Color(11, 106, 166);     // the focus ring
    Color borderInvalid = Color(192, 50, 31);      // danger
    Color ink           = Color(22, 24, 27);
    Color inkFaint      = Color(95, 102, 110);     // leading zeros, untyped digits
    Color selection     = Color(166, 90, 0);       // the selected segment's fill (accent)
    Color onSelection   = Color(255, 255, 255);
    Color sign          = Color(166, 90, 0);       // the + or - of a relative entry
    std::string fontFamily = "Ubuntu Mono";        // tabular figures, so the digits never jitter
    float fontSize    = 12.0f;                     // pixels; the field
    float readoutSize = 18.0f;                     // pixels; the readout variant
    float cornerRadius = 4.0f;
    float paddingX    = 8.0f;

    static TimecodeFieldStyle LightPalette();
    static TimecodeFieldStyle DarkPalette();
    // The colours from a theme's shared roles: the well behind the digits,
    // the control border, the focus ring, danger for an entry that is not a
    // timecode, the ink, the accent for the selected segment and the sign.
    // Fonts and sizes are the defaults.
    static TimecodeFieldStyle FromTheme(const UltraCanvasTheme& theme);
    static TimecodeFieldStyle FromTheme() { return FromTheme(UltraCanvasTheme::Current()); }
};

class UltraCanvasTimecodeField : public UltraCanvasUIElement {
public:
    UltraCanvasTimecodeField(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasTimecodeField() override = default;

    // ===== VALUE =====
    void SetFrames(long long frames);              // clamped at 0 and the maximum; ends an entry
    long long GetFrames() const { return frames; }
    void SetSeconds(double seconds);               // rounded to the frame grid
    double GetSeconds() const;
    void SetFrameRate(double frameRate);           // 29.97 counts 30 frames a second
    double GetFrameRate() const { return frameRate; }
    void SetMaximumFrames(long long maximum);      // 0 = none; steps and entries stop there
    long long GetMaximumFrames() const { return maximumFrames; }

    // ===== PRESENTATION =====
    // The readout: the large, read-only variant for a transport bar. It
    // takes no focus and no input.
    void SetReadout(bool readout);
    bool IsReadout() const { return readout; }
    void SetStyle(const TimecodeFieldStyle& s);
    const TimecodeFieldStyle& GetStyle() const { return style; }

    // ===== ENTRY =====
    std::string GetText() const;                   // what is shown: the value, or the entry so far
    bool IsEditing() const { return !entry.empty() || sign != 0; }
    bool IsInvalid() const { return invalid; }
    void SelectSegment(int segment);               // 0 hours .. 3 frames; -1 none
    int GetSelectedSegment() const { return selectedSegment; }
    bool Commit();                                 // apply the entry: true when it was a timecode
    void Cancel();                                 // drop the entry, keep the value
    void Step(int segment, int direction);         // +1 / -1 of that segment's unit; commits

    // ===== EVENTS =====
    std::function<void(long long frames)> onCommit;   // Enter, a step, focus leaving with a valid entry

    // ===== UIElement OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return !readout; }
    AccessibleRole GetAccessibleRole() const override { return AccessibleRole::TextField; }
    std::string GetAccessibleValueText() const override { return GetText(); }
    bool SetAccessibleValueText(const std::string& text) override;

private:
    bool EntryFrames(long long& result) const;     // the entry as frames, false when not a timecode
    void Refresh();                                // recompute `invalid`, redraw
    long long Clamped(long long value) const;
    int SegmentAt(float localX) const;

    long long frames = 0;
    double frameRate = 25.0;
    long long maximumFrames = 0;
    bool readout = false;
    TimecodeFieldStyle style;
    int selectedSegment = -1;
    std::string entry;                             // typed digits, newest last, at most eight
    char sign = 0;                                 // '+', '-' or 0: a relative entry
    bool invalid = false;
    float charAdvance = 0.0f;                      // measured at the last render
    float textLeft = 0.0f;
    float textTop = 0.0f;
    float textHeight = 0.0f;
};

std::shared_ptr<UltraCanvasTimecodeField> CreateTimecodeField(const std::string& id, int x, int y, int w, int h);

} // namespace UltraCanvas

// Apps/UltraClipboard/ui/ClipboardDialogs.h
// The edit dialog (Docs/Research/UltraClipboard/UltraClipboard-Edit.svg) and
// the settings dialog.
//
// Edit, for the text kinds (text, code, formatted text, links, colours): the
// text in a text area, the tools Plain text, Trim, Case and Join lines, and
// "Keep the original entry as well" (on). Save replaces the entry or adds the
// edited copy beside it; Save and copy also puts it on the clipboard. Images
// are edited in UltraPaint, files where they are.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>

namespace UltraCanvas {
    class UltraCanvasClipboardHistory;
    class UltraCanvasWindowBase;
    struct ClipboardHistoryEntry;
}

namespace UltraClipboard {

// `onSaved(newEntryId, copyIt)` after Save / Save and copy; nothing on Cancel.
void ShowEditDialog(UltraCanvas::UltraCanvasWindowBase* parent, UltraCanvas::UltraCanvasClipboardHistory& history,
                    const UltraCanvas::ClipboardHistoryEntry& entry,
                    std::function<void(int64_t newEntryId, bool copyIt)> onSaved);

// The history's settings: limits, thumbnails, excluded programs, and what
// the encryption protects. `onChanged` after OK.
void ShowSettingsDialog(UltraCanvas::UltraCanvasWindowBase* parent, UltraCanvas::UltraCanvasClipboardHistory& history,
                        std::function<void()> onChanged);

} // namespace UltraClipboard

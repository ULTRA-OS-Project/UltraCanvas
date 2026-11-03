// Apps/UltraAIApp/UltraAISettingsWidgets.h
// The settings window's one type scale and its page skeleton - a bold title,
// a one-line caption, the controls, and the notes block at the foot of the
// page - with the control factories every page builds from. Lifted out of
// UltraAISettingsWindow.cpp so the Local models page (UltraAILocalModelsPage)
// and the window's own pages are built from the same parts.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraAI Module
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraAIApp {
namespace SettingsWidgets {

// ----- one type scale for the whole window (as UltraFiler's) -----
inline constexpr float kTitleFontSize = 15.0f;
inline constexpr float kTextFontSize  = 10.0f;
inline constexpr float kNoteFontSize  = 9.0f;
inline constexpr float kTreeFontSize  = 9.0f;

inline const UltraCanvas::Color kTextColor      = UltraCanvas::Color(40, 40, 44, 255);
inline const UltraCanvas::Color kNoteTextColor  = UltraCanvas::Color(96, 96, 104, 255);
inline const UltraCanvas::Color kNoteBackground = UltraCanvas::Color(244, 245, 248, 255);
inline const UltraCanvas::Color kNoteAccent     = UltraCanvas::Color(160, 176, 200, 255);
inline const UltraCanvas::Color kLocalColor     = UltraCanvas::Color(30, 120, 60, 255);
inline const UltraCanvas::Color kCloudColor     = UltraCanvas::Color(170, 90, 20, 255);

// Page geometry: the window is 720 wide, the tree takes 180, so a page
// has 540; with its padding that leaves ~500 for content. Texts are
// wrapped at fixed widths because content measuring needs a render
// context, which the window does not have while it is first laid out.
inline constexpr int kPagePadding  = 20;
inline constexpr int kTextWidth    = 480;
inline constexpr int kNoteWidth    = 470;
inline constexpr int kControlHeight = 22;


// ===== TEXT =====

std::shared_ptr<UltraCanvas::UltraCanvasLabel> MakeLabel(
    const std::string& id, const std::string& text,
    float fontSize = kTextFontSize, const UltraCanvas::Color& color = kTextColor);

// Wrapped text at a fixed width (content measuring needs a render context,
// which the window does not have while it is first laid out).
std::shared_ptr<UltraCanvas::UltraCanvasLabel> MakeText(
    const std::string& id, const std::string& text, int width = kTextWidth,
    float fontSize = kTextFontSize, const UltraCanvas::Color& color = kTextColor);

// ===== PAGE SKELETON =====

struct PageParts {
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> body;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> notes;
};

PageParts MakePage(const std::string& id, const std::string& title,
                   const std::string& caption);
void AddNote(PageParts& parts, const std::string& id, const std::string& text);

// ===== CONTROLS =====

std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeButton(
    const std::string& id, const std::string& label, int width,
    std::function<void()> onClick);

std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> MakeCheckbox(
    const std::string& id, const std::string& text, int width, bool checked,
    std::function<void(bool)> onChange);

// A "label above input" field group; the input is handed back through `out`.
std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeField(
    const std::string& id, const std::string& label, const std::string& placeholder,
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& out);

} // namespace SettingsWidgets
} // namespace UltraAIApp

// Apps/ChatCube/ui/ChatCubeControls.h
// The framework's elements in ChatCube's look (ChatCubeTheme): a label, a
// button and an icon button, a text field, the logo, a row - what the main
// window and the Edit profile window are built of, so both look the same.
// Version: 0.2.0 - the look follows a change while ChatCube runs: every element styled here is
//                  styled again (OnLook, ChatCubeTheme.h); a label's colour is a role
// Version: 0.1.0 - out of ChatCubeWindow.cpp, for the Edit profile window too
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ChatCubeTheme.h"

#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasContainer;
class UltraCanvasImageElement;
class UltraCanvasLabel;
class UltraCanvasTextInput;
}

namespace ChatCube {

inline constexpr float kTextFont = 10.0f;
inline constexpr int   kControlHeight = 32;
inline constexpr float kFormWidth = 340.0f;
inline constexpr float kControlRadius = 8.0f;
inline constexpr int   kIconButton = 34;       // a round icon button's side

UltraCanvas::Color Darker(const UltraCanvas::Color& c, int by);
std::string Trim(const std::string& s);

std::shared_ptr<UltraCanvas::UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                                         float fontSize = kTextFont,
                                                         LookRole role = &Look::ink);

// A button in the window's look: rounded, a hairline border at rest, the
// accent fill when primary, no fill at rest when ghost (the icon buttons).
void StyleButton(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b, bool primary, bool ghost = false);

std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, int width,
                                                           bool primary = false);

// A button with an icon from media/icons/chatcube, drawn in the text colour;
// `text` beside it when given, else the icon alone on a round button.
std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeIconButton(const std::string& id, const std::string& icon,
                                                               const std::string& text = std::string(),
                                                               int width = kIconButton, bool primary = false,
                                                               bool ghost = true);

std::shared_ptr<UltraCanvas::UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& placeholder,
                                                             int width, bool pill = false);

// The ChatCube logo, `side` pixels square.
std::shared_ptr<UltraCanvas::UltraCanvasImageElement> MakeLogo(const std::string& id, int side);

std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeRow(const std::string& id, int gap = 8);

} // namespace ChatCube

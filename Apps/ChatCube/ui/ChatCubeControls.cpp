// Apps/ChatCube/ui/ChatCubeControls.cpp
// The window's controls in ChatCube's look - see the header.
// Version: 0.1.0 - out of ChatCubeWindow.cpp, for the Edit profile window too
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeControls.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <algorithm>

using namespace UltraCanvas;

namespace ChatCube {

// The window's colours, from the theme set before it was built (main.cpp:
// ChatCube::ThemeFor, light or --dark). Read once: the look does not change
// while the window is up.
const Look& L() {
    static const Look look = CurrentLook();
    return look;
}

Color Darker(const Color& c, int by) {
    auto d = [by](uint8_t v) { return static_cast<uint8_t>(std::max(0, v - by)); };
    return Color(d(c.r), d(c.g), d(c.b), c.a);
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                            float fontSize, const Color& color) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

// A button in the window's look: rounded, a hairline border at rest, the
// accent fill when primary, no fill at rest when ghost (the icon buttons).
void StyleButton(const std::shared_ptr<UltraCanvasButton>& b, bool primary, bool ghost) {
    ButtonStyle st = b->GetStyle();
    st.cornerRadius = kControlRadius;
    st.fontSize = kTextFont;
    st.borderWidth = ghost || primary ? 0.0f : 1.0f;
    st.borderColor = L().line;
    if (primary) {
        st.normalColor = L().accent;
        st.hoverColor = L().accentHover;
        st.pressedColor = Darker(L().accentHover, 16);
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().onAccent;
        st.disabledColor = L().pressed;
        st.disabledTextColor = L().inkFaint;
    } else {
        st.normalColor = ghost ? Colors::Transparent : L().panel;
        st.hoverColor = L().hover;
        st.pressedColor = L().pressed;
        st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().ink;
        st.disabledColor = ghost ? Colors::Transparent : L().well;
        st.disabledTextColor = L().inkFaint;
    }
    b->SetStyle(st);
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, int width,
                                              bool primary) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kControlHeight, text);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    StyleButton(b, primary);
    return b;
}

// A button with an icon from media/icons/chatcube, drawn in the text colour;
// `text` beside it when given, else the icon alone on a round button.
std::shared_ptr<UltraCanvasButton> MakeIconButton(const std::string& id, const std::string& icon,
                                                  const std::string& text, int width,
                                                  bool primary, bool ghost) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kIconButton, text);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    StyleButton(b, primary, ghost && !primary);
    if (text.empty()) b->GetStyle().cornerRadius = kIconButton / 2.0f;
    b->SetIcon(IconPath(icon));
    b->SetIconSize(18, 18);
    b->SetUseIconAsMask(true);
    b->SetIconSpacing(6);
    return b;
}

std::shared_ptr<UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& placeholder, int width,
                                                bool pill) {
    auto t = std::make_shared<UltraCanvasTextInput>(id, 0, 0, width, kControlHeight);
    t->SetPlaceholder(placeholder);
    t->SetFontSize(kTextFont);
    t->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    TextInputStyle st = t->GetStyle();
    st.backgroundColor = L().panel;
    st.borderColor = L().line;
    st.focusBorderColor = L().accent;
    st.textColor = L().ink;
    st.placeholderColor = L().inkFaint;
    st.caretColor = L().ink;
    st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
    st.borderRadius = pill ? kControlHeight / 2 : static_cast<int>(kControlRadius);
    st.paddingLeft = st.paddingRight = pill ? 14 : 10;
    t->SetStyle(st);
    return t;
}

// The ChatCube logo, `side` pixels square.
std::shared_ptr<UltraCanvasImageElement> MakeLogo(const std::string& id, int side) {
    auto logo = std::make_shared<UltraCanvasImageElement>(id, 0, 0, side, side);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/ChatCube.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    logo->size.width = CSSLayout::Dimension::Px(static_cast<float>(side));
    logo->size.height = CSSLayout::Dimension::Px(static_cast<float>(side));
    return logo;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, int gap) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return row;
}

} // namespace ChatCube

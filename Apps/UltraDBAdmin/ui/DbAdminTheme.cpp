// Apps/UltraDBAdmin/ui/DbAdminTheme.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminTheme.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

// The blues of media/appicon/UltraDBAdmin.svg.
const Color kAccent(47, 111, 214);
const Color kAccentDeep(29, 79, 163);
const Color kDanger(190, 38, 38);
const Color kDangerSoft(252, 236, 236);

constexpr float kCornerRadius = 6.0f;
constexpr float kCardRadius   = 8.0f;
constexpr int   kIconSize     = 16;

ButtonStyle Shape() {
    ButtonStyle shape;
    shape.cornerRadius = kCornerRadius;
    shape.useIconAsMask = true;
    shape.iconSpacing = 6;
    shape.fontSize = 12.0f;
    return shape;
}

} // namespace

UltraCanvasTheme AppTheme() {
    UltraCanvasTheme theme = UltraCanvasTheme::Light();
    theme.name = "UltraDBAdmin";
    ThemeRoles& r = theme.roles;
    r.ground     = Color(240, 242, 246);
    r.panel      = Colors::White;
    r.well       = Color(248, 249, 251);
    r.raised     = Colors::White;
    r.hover      = Color(236, 241, 250);
    r.pressed    = Color(216, 228, 248);
    r.line       = Color(223, 227, 234);
    r.lineStrong = Color(192, 198, 208);
    r.ink        = Color(31, 35, 41);
    r.inkMuted   = Color(88, 96, 110);
    r.inkFaint   = Color(148, 154, 165);
    r.accent      = kAccent;
    r.accentHover = kAccentDeep;
    r.accentSoft  = Color(222, 233, 251);
    r.onAccent    = Colors::White;
    r.focus       = kAccent;
    r.danger      = kDanger;
    return theme;
}

std::string IconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/dbadmin/" + name + ".svg");
}

void StyleButton(UltraCanvasButton& button, ButtonRole role) {
    const UltraCanvasTheme& theme = UltraCanvasTheme::Current();
    ButtonStyle style = ButtonStyle::FromTheme(theme, Shape(),
                                               role == ButtonRole::Primary ? ButtonVariant::Primary
                                                                           : ButtonVariant::Standard);
    if (role == ButtonRole::Primary) {
        // Disabled, the accent goes: a grey fill and the faint ink, so an
        // idle Commit does not read as one waiting to be pressed.
        style.fontWeight = FontWeight::Bold;
        style.disabledColor = Color(232, 235, 240);
    } else {
        style.borderColor = theme.roles.line;
        style.disabledColor = theme.roles.well;
    }
    if (role == ButtonRole::Danger) {
        style.normalTextColor = style.hoverTextColor = style.pressedTextColor = theme.roles.danger;
        style.hoverColor = kDangerSoft;
        style.pressedColor = Color(246, 214, 214);
    }
    button.SetStyle(style);
}

std::shared_ptr<UltraCanvasButton> MakeActionButton(const std::string& id, const std::string& text,
                                                    const std::string& icon, const std::string& tooltip,
                                                    ButtonRole role) {
    // As wide as its icon and label: given no width, the layout measures them.
    auto button = CreateButton(id, 0, 0, 0, kButtonHeight, text);
    StyleButton(*button, role);
    if (!icon.empty()) {
        button->SetIcon(IconPath(icon));
        button->SetIconSize(kIconSize, kIconSize);
    }
    button->SetPadding(4.0f, 11.0f);
    button->SetTooltip(tooltip);
    if (text.empty()) button->SetAccessibleName(tooltip);   // an icon alone: a reader hears the tooltip
    button->layoutItem.SetFlexShrink(0);
    return button;
}

std::shared_ptr<UltraCanvasButton> MakeDialogButton(const std::string& id, const std::string& text,
                                                    float width, ButtonRole role) {
    auto button = CreateButton(id, 0, 0, width, kButtonHeight, text);
    StyleButton(*button, role);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

void MakeCard(UltraCanvasContainer& container) {
    const ThemeRoles& r = UltraCanvasTheme::Current().roles;
    container.SetBackgroundColor(r.panel);
    container.SetBorders(1.0f, r.line, kCardRadius);
}

std::shared_ptr<UltraCanvasContainer> MakeBarDivider(const std::string& id) {
    auto divider = CreateContainer(id, 0, 0, 1, 20);
    divider->SetBackgroundColor(UltraCanvasTheme::Current().roles.line);
    divider->layoutItem.SetFlexShrink(0);
    divider->SetMargin(0.0f, 2.0f);
    return divider;
}

Color ErrorColor() {
    return UltraCanvasTheme::Current().roles.danger;
}

Color StatusColor() {
    return UltraCanvasTheme::Current().roles.inkMuted;
}

void StyleList(UltraCanvasListView& view) {
    const ThemeRoles& r = UltraCanvasTheme::Current().roles;
    ListViewStyle style = view.GetStyle();
    style.backgroundColor = r.panel;
    style.headerBackgroundColor = Color(246, 247, 250);
    style.headerTextColor = r.inkMuted;
    style.headerHeight = 28;
    style.rowHeight = 26;
    style.gridLineColor = Color(236, 239, 244);
    style.alternateRowColors = true;
    style.alternateRowColor = Color(250, 251, 253);
    style.selectionBackgroundColor = r.accentSoft;
    style.hoverBackgroundColor = Color(243, 246, 252);
    view.SetStyle(style);
    view.SetBorders(1.0f, r.line, kCornerRadius);
    // The selection is the soft accent, so its text stays the ink (the
    // delegate's own white would vanish on it).
    if (auto* cells = dynamic_cast<UltraCanvasDefaultListDelegate*>(view.GetDelegate())) {
        cells->SetTextColor(r.ink);
        cells->SetSelectedTextColor(r.ink);
    }
}

void StyleTree(UltraCanvasTreeView& tree) {
    const ThemeRoles& r = UltraCanvasTheme::Current().roles;
    tree.SetRowHeight(26);
    tree.SetLineStyle(TreeLineStyle::NoLine);
    tree.SetExpanderStyle(TreeExpanderStyle::Chevron);
    tree.SetExpanderArrowColor(r.inkMuted);
    tree.SetSelectionColor(r.accentSoft);
    tree.SetHoverColor(Color(243, 246, 252));
    tree.SetTextColor(r.ink);
    tree.SetBackgroundColor(r.panel);
    tree.SetBorders(0.0f);
    tree.SetVerticalScrollbarStyle(ScrollbarStyle::Modern());   // the lists' slim one
}

} // namespace UltraDBAdmin

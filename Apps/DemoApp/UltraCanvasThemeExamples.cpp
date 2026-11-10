// Apps/DemoApp/UltraCanvasThemeExamples.cpp
// The theme (UltraCanvasTheme): the roles every themed element reads, as
// swatches of the current theme; the policy the theme follows and what the
// host said, with a pick between following the host, Light and Dark; the
// fonts by text role; a row of elements built plainly, which follow the
// theme by default, beside one styled by hand, which keeps its look; and
// Appearance.conf, the file UOS-Settings' Look page writes over the host.
// The page re-reads itself on every theme change, so a pick here, a change
// of the desktop, or a save on the Look page shows at once.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasAppearanceSettings.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

namespace {

// A role of the theme: its name as the element readers call it, and where
// it sits in ThemeRoles.
struct RoleEntry { const char* name; Color ThemeRoles::*member; };
const RoleEntry kRoles[] = {
    {"ground", &ThemeRoles::ground}, {"panel", &ThemeRoles::panel}, {"well", &ThemeRoles::well},
    {"raised", &ThemeRoles::raised}, {"hover", &ThemeRoles::hover}, {"pressed", &ThemeRoles::pressed},
    {"line", &ThemeRoles::line}, {"lineStrong", &ThemeRoles::lineStrong},
    {"ink", &ThemeRoles::ink}, {"inkMuted", &ThemeRoles::inkMuted}, {"inkFaint", &ThemeRoles::inkFaint},
    {"accent", &ThemeRoles::accent}, {"accentHover", &ThemeRoles::accentHover},
    {"accentSoft", &ThemeRoles::accentSoft}, {"onAccent", &ThemeRoles::onAccent},
    {"focus", &ThemeRoles::focus}, {"danger", &ThemeRoles::danger},
};

struct TextRoleEntry { const char* name; ThemeTextRole role; };
const TextRoleEntry kTextRoles[] = {
    {"Title", ThemeTextRole::Title}, {"Heading", ThemeTextRole::Heading}, {"Body", ThemeTextRole::Body},
    {"Caption", ThemeTextRole::Caption}, {"Small", ThemeTextRole::Small}, {"Mono", ThemeTextRole::Mono},
};

std::string Number(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

// One swatch: the colour as a filled box, its role name and hex under it.
struct Swatch {
    std::shared_ptr<UltraCanvasContainer> box;
    std::shared_ptr<UltraCanvasLabel> hex;
};

// The labels and swatches the theme fills, held by the page's handler.
struct ThemeDemoState {
    std::vector<Swatch> swatches;
    std::vector<std::shared_ptr<UltraCanvasLabel>> fontSamples;
    std::shared_ptr<UltraCanvasLabel> themeText, hostText, settingsText, fileText;
    std::shared_ptr<UltraCanvasSegmentedControl> policyPick;
    int handlerId = 0;
    bool picking = false;
};

std::string YesNo(bool value) { return value ? Tr("yes") : Tr("no"); }

}  // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateThemeExamples() {
    auto page = DemoKit::CreatePage("ThemePage", Tr("Theme"),
        Tr("One look for every element: UltraCanvasTheme is a set of named roles - surfaces, "
        "lines, inks, the accent - and the interface fonts, built from what the desktop says "
        "(light or dark, the accent colour, the fonts, the text scale, high contrast) and from "
        "Appearance.conf, the choices UOS-Settings' Look page makes over it. Every element built "
        "plainly follows the current theme; one styled by hand keeps its look."));

    auto state = std::make_shared<ThemeDemoState>();

    // ===== THE POLICY AND THE HOST =====
    auto policyCard = DemoKit::AddCard(page, "ThemePolicyCard", Tr("The policy and the host"),
        Tr("What the theme follows: the host (the default - the desktop's look, read again on every "
        "change), or the framework's Light or Dark preset with the host's fonts. The pick here is "
        "the demo's for this run; an application picks once at start, or leaves the default."));
    auto policyRow = DemoKit::AddRow(policyCard, "ThemePolicyRow", 10.0f);
    DemoKit::AddText(policyRow, "ThemePolicyCaption", Tr("The theme follows"));
    state->policyPick = std::make_shared<UltraCanvasSegmentedControl>("ThemePolicyPick", 0, 0, 330, 28);
    state->policyPick->AddSegment(Tr("The host"));
    state->policyPick->AddSegment(Tr("Light"));
    state->policyPick->AddSegment(Tr("Dark"));
    DemoKit::AddFixed(policyRow, state->policyPick);
    state->themeText = DemoKit::AddText(policyCard, "ThemeCurrentText", "");
    state->hostText = DemoKit::AddCaption(policyCard, "ThemeHostText", "");

    // ===== THE ROLES =====
    auto rolesCard = DemoKit::AddCard(page, "ThemeRolesCard", Tr("The roles"),
        Tr("Surfaces back to front (the window ground, a panel, an inset well, a raised surface, "
        "the hover and pressed fills), the hairline and the control border, the three inks, the "
        "accent with its hover, its soft tint and the text on it, the focus ring and danger. "
        "The accent is the desktop's; the rest derive from light or dark and the contrast."));
    auto swatchRow = DemoKit::AddRow(rolesCard, "ThemeSwatchRow", 8.0f);
    for (const RoleEntry& entry : kRoles) {
        auto column = DemoKit::CreateColumn(std::string("ThemeSwatch-") + entry.name, 2.0f);
        column->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        column->size.width = CSSLayout::Dimension::Px(92);
        Swatch swatch;
        swatch.box = std::make_shared<UltraCanvasContainer>(std::string("ThemeSwatchBox-") + entry.name);
        swatch.box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        swatch.box->size.height = CSSLayout::Dimension::Px(36);
        swatch.box->SetBorders(1, DemoKit::CardBorder(), 4);
        column->AddChild(swatch.box);
        auto name = DemoKit::AddCaption(column, std::string("ThemeSwatchName-") + entry.name, entry.name);
        name->SetTextColor(DemoKit::BodyText());
        swatch.hex = DemoKit::AddCaption(column, std::string("ThemeSwatchHex-") + entry.name, "");
        state->swatches.push_back(swatch);
        DemoKit::AddFixed(swatchRow, column);
    }

    // ===== THE FONTS =====
    auto fontsCard = DemoKit::AddCard(page, "ThemeFontsCard", Tr("The fonts by role"),
        Tr("The interface font at the host's size, and a text role's size as a ratio of it: Body 1.0, "
        "Caption 0.92, Small 0.85, Heading 1.25, Title 1.5; Mono is the monospace font at the body "
        "size. The host's text scale multiplies them all. An element reads the role its text plays: "
        "a button's caption is Body, a tooltip Caption, a card's title Heading."));
    for (const TextRoleEntry& entry : kTextRoles) {
        auto sample = DemoKit::AddText(fontsCard, std::string("ThemeFont-") + entry.name, "");
        sample->SetTextColor(DemoKit::TitleText());
        state->fontSamples.push_back(sample);
    }

    // ===== THE ELEMENTS =====
    auto elementsCard = DemoKit::AddCard(page, "ThemeElementsCard", Tr("Elements following the theme"),
        Tr("Built plainly, each takes the theme as it is built and again on every change: the "
        "button, the field, the dropdown, the checkbox and the slider here. The last button was "
        "given a style by hand (SetStyle), which stops the following - it keeps its colours "
        "whatever the theme does."));
    auto elementsRow = DemoKit::AddRow(elementsCard, "ThemeElementsRow", 12.0f);
    auto followingButton = std::make_shared<UltraCanvasButton>("ThemeFollowingButton", 0, 0, 110, 28, Tr("A button"));
    DemoKit::AddFixed(elementsRow, followingButton);
    auto followingInput = std::make_shared<UltraCanvasTextInput>("ThemeFollowingInput", 0, 0, 150, 28);
    followingInput->SetText(Tr("A field"));
    DemoKit::AddFixed(elementsRow, followingInput);
    auto followingPick = std::make_shared<UltraCanvasDropdown>("ThemeFollowingPick", 0, 0, 130, 28);
    followingPick->AddItem(DropdownItem(Tr("A choice")));
    followingPick->AddItem(DropdownItem(Tr("Another")));
    followingPick->SetSelectedIndex(0);
    DemoKit::AddFixed(elementsRow, followingPick);
    auto followingCheck = std::make_shared<UltraCanvasCheckbox>("ThemeFollowingCheck", 0, 0, 120, 28, Tr("A checkbox"));
    followingCheck->SetChecked(true);
    DemoKit::AddFixed(elementsRow, followingCheck);
    auto followingSlider = std::make_shared<UltraCanvasSlider>("ThemeFollowingSlider", 0, 0, 160, 28);
    followingSlider->SetValue(40.0f);
    DemoKit::AddFixed(elementsRow, followingSlider);
    auto handStyled = std::make_shared<UltraCanvasButton>("ThemeHandStyledButton", 0, 0, 130, 28, Tr("Styled by hand"));
    ButtonStyle hand = handStyled->GetStyle();
    hand.normalColor = Color(122, 76, 182, 255);
    hand.hoverColor = Color(140, 96, 198, 255);
    hand.pressedColor = Color(104, 60, 160, 255);
    hand.normalTextColor = Colors::White;
    hand.hoverTextColor = Colors::White;
    hand.pressedTextColor = Colors::White;
    hand.borderColor = Color(104, 60, 160, 255);
    handStyled->SetStyle(hand);
    DemoKit::AddFixed(elementsRow, handStyled);

    // ===== APPEARANCE.CONF =====
    auto settingsCard = DemoKit::AddCard(page, "ThemeSettingsCard", Tr("Appearance.conf"),
        Tr("A person's choices over the desktop for every UltraCanvas application at once, written "
        "by UOS-Settings' Look page into the settings folder: the look, the accent, the font and "
        "its size, the text scale, the contrast - each \"system\" when the host decides. Every "
        "application reads it with the host and watches the folder, so a save there reaches this "
        "page within a moment, as a change of the desktop does."));
    state->settingsText = DemoKit::AddCaption(settingsCard, "ThemeSettingsPath", "");
    state->fileText = DemoKit::AddText(settingsCard, "ThemeSettingsFile", "");
    auto* app = UltraCanvasApplication::GetInstance();
    state->fileText->SetFont(app ? app->GetDefaultMonospacedFontStyle().fontFamily : std::string("monospace"), 12.0f);
    state->fileText->SetTextColor(DemoKit::BodyText());

    DemoKit::AddNotes(page, "ThemeNotes", Tr("Implementation notes"), {
        Tr("UltraCanvasTheme::Current() is the theme; FromHost builds one from a HostAppearance, Light() and Dark() are the presets, the high-contrast ones every ink at 7:1."),
        Tr("A style struct reads it: ButtonStyle::FromTheme(theme, base, variant) and so on for every element; ApplyTheme on an element sets that style on it."),
        Tr("Following by default: UltraCanvasUIElement::SetFollowThemeByDefault, set by the policy; SetStyle by hand stops it, SetFollowTheme(true) after it keeps both."),
        Tr("UltraCanvasApplication::SetThemePolicy picks what the theme follows; SetThemeProvider gives an Own application's theme from the host."),
        Tr("AppearanceSettings (UltraCanvasAppearanceSettings.h) is the file; UltraCanvasSettingsWatcher watches the folder for every application."),
        Tr("Docs: Docs/UltraCanvas/UltraCanvasTheme.md, UltraCanvasHostAppearance.md and the proposal UltraCanvasHostThemeProposal.md."),
    });

    // ===== FILLING =====
    auto fill = [state, app](const UltraCanvasTheme& theme) {
        for (size_t i = 0; i < state->swatches.size() && i < std::size(kRoles); ++i) {
            const Color color = theme.roles.*(kRoles[i].member);
            state->swatches[i].box->SetBackgroundColor(color);
            state->swatches[i].hex->SetText(HexOfColor(color));
        }
        for (size_t i = 0; i < state->fontSamples.size() && i < std::size(kTextRoles); ++i) {
            const FontStyle font = theme.Font(kTextRoles[i].role);
            state->fontSamples[i]->SetFont(font.fontFamily, static_cast<float>(font.fontSize));
            state->fontSamples[i]->SetText(TrF("{0} - {1} {2} pt", kTextRoles[i].name, font.fontFamily, Number(font.fontSize)));
        }
        state->themeText->SetText(TrF("The theme: {0} ({1}, {2}); the accent {3}, the interface font {4} {5} pt, scale {6}",
            theme.name, theme.dark ? Tr("dark") : Tr("light"),
            theme.highContrast ? Tr("high contrast") : Tr("normal contrast"),
            HexOfColor(theme.roles.accent), theme.fonts.ui.fontFamily, Number(theme.fonts.ui.fontSize),
            Number(theme.fonts.scale)));
        if (app) {
            const HostAppearance& host = app->GetHostAppearance();
            state->hostText->SetText(TrF("The host said: source {0}, dark {1}, high contrast {2}, accent {3}, font {4} {5} pt, text scale {6}",
                host.source, YesNo(host.dark), YesNo(host.highContrast),
                host.accent ? HexOfColor(*host.accent) : Tr("none"),
                host.uiFont.fontFamily, Number(host.uiFont.fontSize), Number(host.textScale)));
            state->picking = true;
            switch (app->GetThemePolicy()) {
                case ThemePolicy::Light: state->policyPick->SetSelectedIndex(1); break;
                case ThemePolicy::Dark:  state->policyPick->SetSelectedIndex(2); break;
                default:                 state->policyPick->SetSelectedIndex(0); break;
            }
            state->picking = false;
        }
        const std::filesystem::path path = AppearanceSettings::FilePath();
        if (path.empty()) {
            state->settingsText->SetText(Tr("No settings folder in this environment: the host's look."));
            state->fileText->SetText("");
        } else {
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::ostringstream text;
                text << in.rdbuf();
                state->settingsText->SetText(PathToUtf8(path));
                state->fileText->SetText(text.str());
            } else {
                state->settingsText->SetText(TrF("{0} - not written yet: the host's look.", PathToUtf8(path)));
                state->fileText->SetText(AppearanceSettings().Serialize());
            }
        }
    };
    fill(UltraCanvasTheme::Current());

    state->policyPick->onSegmentSelected = [state, app](int index) {
        if (state->picking || !app) return;
        app->SetThemePolicy(index == 1 ? ThemePolicy::Light : index == 2 ? ThemePolicy::Dark : ThemePolicy::FollowHost);
    };

    // The page follows every theme change while it lives; the handler
    // takes itself out once the page is gone.
    std::weak_ptr<UltraCanvasContainer> alive = page;
    state->handlerId = UltraCanvasTheme::AddChangeHandler([state, alive, fill](const UltraCanvasTheme& theme) {
        if (alive.expired()) { UltraCanvasTheme::RemoveChangeHandler(state->handlerId); return; }
        fill(theme);
    });
    return page;
}

}  // namespace UltraCanvas

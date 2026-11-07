// Apps/UOSSettings/ui/UOSSettingsWindow.cpp
// The UOS-Settings window: page tree | page, Close at the foot - the layout
// of UltraFiler's settings window. See UOSSettingsWindow.h.
// Version: 0.10.0 - on UltraCanvasSettingsPageKit (the window, the tree, the bottom bar,
//                   the page switching, MakeLabel); the logo as the kit's sidebar header
// Version: 0.9.0 - Desktop: the Sticker board's card display and hover preview size
// Version: 0.8.1 - the Language and Translations pages, the page tree and the window's buttons go through Tr
// Version: 0.8.0 - Language > Translations: the localisation editor (UltraCanvasLocalisationEditor)
// Version: 0.7.0 - the Language page: the language every application shows, Language.conf
// Version: 0.6.1 - the ULTRA OS logo over the page tree
// Version: 0.6.0 - Desktop: "Start with the desktop" (ChatCube, UltraMail)
// Version: 0.5.1 - Desktop: from an MSIX package, why "At sign-in" is missing
// Version: 0.5.0 - Notifications > Registrations: listed, and removed with one button
// Version: 0.4.0 - Appearance > Emoji: colour, black and white, or the system's
// Version: 0.3.0 - Desktop: the Windows desktop, beside Explorer, or instead of it
// Version: 0.2.0 - Desktop: UltraDesktop in place of the Windows desktop
// Version: 0.1.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework / ULTRA OS

#include "UOSSettingsWindow.h"

#include "UltraCanvasCheckbox.h"
#include "UltraDesktopSettings.h"   // Apps/UltraDesktop: the desktop's settings file

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasEmojiSettings.h"
#include "UltraCanvasNotificationActivation.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasFileDialogSettings.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasUtils.h"   // NormalizePath
#include "UltraCanvasConfig.h"  // GetResourcesDir
#include "UltraLocalisation/UltraLocalisation.h"
#include "IODeviceManager/UltraCanvasIODeviceTlsTrust.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"   // PathToUtf8
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UOSSettings {

namespace {
    // The look of UltraFiler's settings window, so the two read as one
    // family: the framework's settings page kit.
    using SettingsPageKit::kTitleFontSize;
    // The Apps window's choices: when a category is split (0 = never) and
    // how many applications Recent shows.
    constexpr int kAppsSplitChoices[] = {0, 4, 6, 8, 10, 12, 16, 20};
    constexpr int kRecentShownChoices[] = {4, 6, 8, 10, 12, 16, 24};

    template <size_t N>
    int NearestChoice(const int (&choices)[N], int value) {
        int best = 0;
        for (size_t i = 0; i < N; ++i) {
            if (std::abs(choices[i] - value) < std::abs(choices[best] - value)) best = static_cast<int>(i);
        }
        return best;
    }

    template <size_t N>
    int ChoiceAt(const int (&choices)[N], int index, int fallback) {
        return index >= 0 && static_cast<size_t>(index) < N ? choices[index] : fallback;
    }
    // The type scale and the colours are the kit's; the widths are this
    // window's own (its pages are wider than a dialog's).
    using SettingsPageKit::kTextFontSize;
    using SettingsPageKit::kNoteFontSize;
    using SettingsPageKit::kTextColor;
    using SettingsPageKit::kNoteTextColor;
    const Color kMutedTextColor = Color(120, 120, 128, 255);
    const Color kRuleColor      = Color(225, 225, 230, 255);
    const Color kHeaderBackground = Color(243, 243, 246, 255);
    constexpr int kSidebarWidth = 180;
    // The ULTRA OS logo over the page tree: ultraos-logo-light.png, 384 x 200.
    constexpr int kLogoWidth = 132;
    constexpr int kLogoHeight = 132 * 200 / 384;
    using SettingsPageKit::kPagePadding;
    constexpr int kTextWidth   = 520;
    constexpr int kNoteWidth   = 510;

    // Table geometry: the application column and the switch column are
    // fixed, the folder column takes the rest.
    constexpr int kAppColumnWidth    = 170;
    constexpr int kScopeColumnWidth  = 180;
    constexpr int kRowHeight         = 34;
    constexpr int kScopeSwitchHeight = 24;
    // Room at the right of every row for the rows' vertical scrollbar, which
    // is drawn over them; the header keeps the same so the columns line up.
    constexpr int kScrollbarRoom = 22;

    constexpr const char* kPageDesktop        = "desktop";
    constexpr const char* kSectionFileDialogs = "file-dialogs";
    constexpr const char* kPageLastFolder     = "file-dialogs/last-folder";
    constexpr const char* kSectionAppearance  = "appearance";
    constexpr const char* kPageEmoji          = "appearance/emoji";
    constexpr const char* kPageLanguage       = "language";
    constexpr const char* kPageTranslations   = "translations";
    constexpr const char* kSectionNotifications = "notifications";
    constexpr const char* kPageRegistrations  = "notifications/registrations";
    constexpr const char* kSectionDevices     = "devices";
    constexpr const char* kPageCertificates   = "devices/certificates";

    // Notification registrations table: the application column is fixed, the
    // click handler takes the rest.
    constexpr int kRegistrationAppColumnWidth = 230;

    // Trusted certificates table: the name, address and button columns are
    // fixed, the key takes the rest.
    constexpr int kDeviceColumnWidth  = 150;
    constexpr int kAddressColumnWidth = 170;
    constexpr int kForgetColumnWidth  = 90;

    // Segment order of the emoji switch.
    constexpr int kSegmentEmojiColour     = 0;
    constexpr int kSegmentEmojiMonochrome = 1;
    constexpr int kSegmentEmojiSystem     = 2;

    // Segment order of every Global | Individual switch.
    constexpr int kSegmentGlobal = 0;
    constexpr int kSegmentIndividual = 1;

    using SettingsPageKit::MakeLabel;

    // A paragraph wrapped at this window's text width (the kit's is a
    // dialog's, narrower)
    std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id,
                                               const std::string& text,
                                               int width = kTextWidth,
                                               float fontSize = kTextFontSize,
                                               const Color& color = kTextColor) {
        return SettingsPageKit::MakeText(id, text, width, fontSize, color);
    }

    std::shared_ptr<UltraCanvasSegmentedControl> MakeScopeSwitch(const std::string& id,
                                                                 int width, int height) {
        auto sw = std::make_shared<UltraCanvasSegmentedControl>(id, 0, 0, width, height);
        SegmentedControlStyle style = sw->GetStyle();
        style.fontSize = kTextFontSize;
        style.paddingVertical = 2;
        style.paddingHorizontal = 8;
        sw->SetStyle(style);
        sw->AddSegment("Global");
        sw->AddSegment("Individual");
        sw->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return sw;
    }

    // A column of a settings table: its header text and its width, 0 for the
    // one column that takes what the others leave.
    struct TableColumn {
        std::string id;
        std::string title;
        int width = 0;
    };

    // A bordered table: a header row over a container that holds the rows and
    // scrolls vertically, handed back in `rows`. The table grows to the room
    // the page leaves it.
    std::shared_ptr<UltraCanvasContainer> MakeTable(const std::string& id,
                                                    const std::vector<TableColumn>& columns,
                                                    std::shared_ptr<UltraCanvasContainer>& rows) {
        auto table = std::make_shared<UltraCanvasContainer>(id);
        table->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        table->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        table->size.height = CSSLayout::Dimension::Px(120);   // basis; it grows
        table->SetBorders(1.0f, kRuleColor);
        table->SetBackgroundColor(Color(255, 255, 255, 255));

        auto header = std::make_shared<UltraCanvasContainer>(id + "-header");
        header->layout.SetFlexRow().SetFlexGap(12)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        header->size.height = CSSLayout::Dimension::Px(28);
        header->SetPadding(0, kScrollbarRoom, 0, 10);
        header->SetBackgroundColor(kHeaderBackground);
        header->SetBorderBottom(1, kRuleColor);
        for (const TableColumn& column : columns) {
            auto l = MakeLabel(id + "-h-" + column.id, column.title);
            l->SetFontWeight(FontWeight::Bold);
            if (column.width > 0) {
                l->size.width = CSSLayout::Dimension::Px(static_cast<float>(column.width));
            } else {
                l->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                l->size.width = CSSLayout::Dimension::Px(0);
            }
            header->AddChild(l);
        }
        table->AddChild(header);

        rows = std::make_shared<UltraCanvasContainer>(id + "-rows");
        rows->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        rows->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        rows->size.height = CSSLayout::Dimension::Px(60);   // basis; it grows
        // Opaque, like the table around it: a scroll repaints only these
        // containers, and without backgrounds of their own the strip under the
        // last row kept the pixels of the rows that were there before.
        rows->SetBackgroundColor(Color(255, 255, 255, 255));
        {
            // Vertically only: a vertical bar narrows the rows, which must not
            // turn into a horizontal overflow of its own width.
            ContainerStyle cs;
            cs.autoShowScrollbars = true;
            cs.autoShowHorizontalScrollbar = false;
            rows->SetContainerStyle(cs);
        }
        // A scroll repaints the rows' own box; the strip of the table just
        // below it kept the pixels of the row that was there before the
        // scroll. The whole table is repainted instead.
        rows->SetScrollChangedCallback([t = table.get()](int, int) { t->RequestRedraw(); });
        table->AddChild(rows);
        return table;
    }

    // One row of a settings table, its cells added by the caller.
    std::shared_ptr<UltraCanvasContainer> MakeTableRow(const std::string& id) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(12)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.height = CSSLayout::Dimension::Px(kRowHeight);
        row->SetPadding(0, kScrollbarRoom, 0, 10);
        row->SetBorderBottom(1, kRuleColor);
        return row;
    }

    // The shaded box of notes at the foot of a page.
    std::shared_ptr<UltraCanvasContainer> MakeNotes(const std::string& id,
                                                    const std::vector<std::string>& texts) {
        auto notes = SettingsPageKit::MakeNotesBlock(id);
        for (size_t i = 0; i < texts.size(); ++i) {
            notes->AddChild(MakeText(id + "-" + std::to_string(i + 1), texts[i],
                                     kNoteWidth, kNoteFontSize, kNoteTextColor));
        }
        return notes;
    }
} // namespace

UOSSettingsWindow::UOSSettingsWindow(std::string version) : version_(std::move(version)) {}

UOSSettingsWindow::~UOSSettingsWindow() = default;

bool UOSSettingsWindow::Create() {
    // The ULTRA OS logo over the page tree - the version drawn for a light
    // ground, as the desktop's taskbar shows the one for a dark ground.
    auto logo = std::make_shared<UltraCanvasImageElement>("uos-logo", 0, 0, kLogoWidth, kLogoHeight);
    logo->SetFitMode(ImageFitMode::Contain);
    logo->LoadFromFile(GetResourcesDir() + "media/icons/desktop/ultraos-logo-light.png");
    logo->SetElementSize(Size2Df(static_cast<float>(kLogoWidth), static_cast<float>(kLogoHeight)));
    logo->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Center);
    logo->SetMargin(14, 0, 10, 0);
    logo->SetTooltip("ULTRA OS");

    SettingsPageKit::SettingsWindowOptions options;
    options.width = 860;
    options.height = 580;
    options.treeWidth = kSidebarWidth;
    options.sidebarHeader = logo;
    options.dialog = false;              // the settings are the application: its main window
    options.closeLabel = Tr("Close");
    // Every app shows its version in its main window title (AGENTS.md).
    if (!UltraCanvasSettingsWindow::Create(nullptr, "UOS-Settings " + version_, options)) return false;

    AddTreeNode("settings", kPageDesktop, Tr("Desktop"));
    AddTreeNode("settings", kPageLanguage, Tr("Language"));
    AddTreeNode(kPageLanguage, kPageTranslations, Tr("Translations"));
    AddTreeNode("settings", kSectionFileDialogs, Tr("File dialogs"));
    AddTreeNode(kSectionFileDialogs, kPageLastFolder, Tr("Last used folder"));
    AddTreeNode("settings", kSectionAppearance, Tr("Appearance"));
    AddTreeNode(kSectionAppearance, kPageEmoji, Tr("Emoji"));
    AddTreeNode("settings", kSectionNotifications, Tr("Notifications"));
    AddTreeNode(kSectionNotifications, kPageRegistrations, Tr("Registrations"));
    AddTreeNode("settings", kSectionDevices, Tr("Devices"));
    AddTreeNode(kSectionDevices, kPageCertificates, Tr("Trusted certificates"));

    BuildDesktopPage();
    BuildLastFolderPage();
    BuildLanguagePage();
    BuildTranslationsPage();
    BuildEmojiPage();
    BuildNotificationsPage();
    BuildCertificatesPage();

    // Another application may have written a page's file since it was
    // built: a page is re-read on the way in, shown as it is now.
    onPageShown = [this](const std::string& pageId) {
        if (pageId == kPageLastFolder) RefreshLastFolderPage();
        if (pageId == kPageDesktop) RefreshDesktopPage();
        if (pageId == kPageCertificates) RefreshCertificatesPage();
        if (pageId == kPageEmoji) RefreshEmojiPage();
        if (pageId == kPageLanguage) RefreshLanguagePage();
        if (pageId == kPageTranslations) RefreshTranslationsPage();
        if (pageId == kPageRegistrations) RefreshNotificationsPage();
    };
    // Opens on the desktop's page - what the desktop's settings button is
    // usually pressed for - with the File dialogs section open below it.
    // Show() shows the window.
    Finish(kPageDesktop, /*show=*/false);
    if (TreeNode* section = tree->FindNode(kSectionFileDialogs)) tree->ExpandNode(section);
    SelectPage(kPageDesktop);
    return true;
}

void UOSSettingsWindow::Show() {
    if (window) window->Show();
}

// ===== DESKTOP =====

void UOSSettingsWindow::BuildDesktopPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-desktop");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-dt-title", "Desktop", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-dt-caption",
            "The ULTRA OS desktop (UltraDesktop): where its taskbar sits, the "
            "picture behind it, and what its buttons open.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    auto body = std::make_shared<UltraCanvasContainer>("uos-dt-body");
    body->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    body->SetMargin(14, 0, 0, 0);

    // caption | field [| extra], the captions in one column.
    auto addRow = [&](const std::string& id, const std::string& captionText,
                      const std::shared_ptr<UltraCanvasUIElement>& field,
                      const std::shared_ptr<UltraCanvasUIElement>& extra = nullptr) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.height = CSSLayout::Dimension::Px(30);
        auto label = MakeLabel(id + "-label", captionText);
        label->size.width = CSSLayout::Dimension::Px(130);
        row->AddChild(label);
        field->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        field->size.width = CSSLayout::Dimension::Px(0);
        row->AddChild(field);
        if (extra) {
            extra->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            row->AddChild(extra);
        }
        body->AddChild(row);
    };
    // A text field is written when it is left or Return is pressed - not per
    // keystroke, which would rebuild the desktop for every letter.
    auto wireText = [this](const std::shared_ptr<UltraCanvasTextInput>& input) {
        input->onEnterPressed = [this](const std::string&) {
            SaveDesktopPage();
            return true;
        };
        input->onFocusLost = [this]() { SaveDesktopPage(); };
    };

    edge_ = std::make_shared<UltraCanvasDropdown>("uos-dt-edge", 0, 0, 200, 28);
    edge_->AddItem("Left", "left");
    edge_->AddItem("Top", "top");
    edge_->AddItem("Bottom", "bottom");
    edge_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-edge-row", "Taskbar", edge_);

    wallpaper_ = std::make_shared<UltraCanvasTextInput>("uos-dt-wallpaper", 0, 0, 260, 28);
    wallpaper_->SetPlaceholder(Tr("ULTRA OS A (the default picture)"));
    wireText(wallpaper_);
    auto browse = std::make_shared<UltraCanvasButton>("uos-dt-browse", 0, 0, 90, 28, Tr("Browse..."));
    browse->SetFontSize(kTextFontSize);
    browse->SetOnClick([this]() { BrowseWallpaper(); });
    addRow("uos-dt-wallpaper-row", "Wallpaper", wallpaper_, browse);

    ramDisc_ = std::make_shared<UltraCanvasTextInput>("uos-dt-ramdisc", 0, 0, 260, 28);
    ramDisc_->SetPlaceholder(Tr("None: no RAM disc button"));
    wireText(ramDisc_);
    addRow("uos-dt-ramdisc-row", "RAM disc", ramDisc_);

    filer_ = std::make_shared<UltraCanvasTextInput>("uos-dt-filer", 0, 0, 260, 28);
    wireText(filer_);
    addRow("uos-dt-filer-row", "File manager", filer_);

    desktops_ = std::make_shared<UltraCanvasDropdown>("uos-dt-desktops", 0, 0, 120, 28);
    for (int i = 1; i <= 9; ++i) desktops_->AddItem(std::to_string(i), std::to_string(i));
    desktops_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-desktops-row", "Virtual desktops", desktops_);

    // The Apps window: its view, when a category is split into its
    // sub-categories, and how many applications Recent shows.
    appsView_ = std::make_shared<UltraCanvasDropdown>("uos-dt-appsview", 0, 0, 200, 28);
    appsView_->AddItem("Tiles", "tiles");
    appsView_->AddItem("Categories and a list", "tree");
    appsView_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-appsview-row", "Apps window", appsView_);

    appsSplit_ = std::make_shared<UltraCanvasDropdown>("uos-dt-appssplit", 0, 0, 200, 28);
    for (int above : kAppsSplitChoices) {
        appsSplit_->AddItem(above == 0 ? "Never" : "Above " + std::to_string(above) + " applications",
                            std::to_string(above));
    }
    appsSplit_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-appssplit-row", "Sub-categories", appsSplit_);

    recentShown_ = std::make_shared<UltraCanvasDropdown>("uos-dt-recent", 0, 0, 120, 28);
    for (int shown : kRecentShownChoices) recentShown_->AddItem(std::to_string(shown), std::to_string(shown));
    recentShown_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-recent-row", "Recent shows", recentShown_);

    // The Sticker board: how a card shows its file, and the size of the
    // preview that opens when the pointer rests on one.
    boardDisplay_ = std::make_shared<UltraCanvasDropdown>("uos-dt-boarddisplay", 0, 0, 220, 28);
    boardDisplay_->AddItem(Tr("Text only"), "text");
    boardDisplay_->AddItem(Tr("Thumbnail and text"), "thumbnail");
    boardDisplay_->AddItem(Tr("Thumbnail and text in a row"), "row");
    boardDisplay_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-boarddisplay-row", "Sticker board cards", boardDisplay_);

    boardPreview_ = std::make_shared<UltraCanvasDropdown>("uos-dt-boardpreview", 0, 0, 120, 28);
    boardPreview_->AddItem(Tr("Small"), "small");
    boardPreview_->AddItem(Tr("Medium"), "medium");
    boardPreview_->AddItem(Tr("Large"), "large");
    boardPreview_->onSelectionChanged = [this](int, const DropdownItem&) {
        if (!refreshing_) SaveDesktopPage();
    };
    addRow("uos-dt-boardpreview-row", "Hover preview", boardPreview_);

    // Only where there is a system desktop to take the place of (Windows).
    if (UltraCanvasDesktopShell::CanReplaceSystemDesktop()) {
        systemDesktop_ = std::make_shared<UltraCanvasDropdown>("uos-dt-replace", 0, 0, 300, 28);
        systemDesktop_->AddItem("The Windows desktop", "none");
        systemDesktop_->AddItem("UltraDesktop beside Explorer", "beside");
        if (UltraCanvasDesktopShell::CanReplaceSystemDesktop(DesktopReplacement::Instead)) {
            systemDesktop_->AddItem("UltraDesktop instead of Explorer", "instead");
        }
        systemDesktop_->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (refreshing_) return;
            SetSystemDesktop(index == 2 ? DesktopReplacement::Instead
                             : index == 1 ? DesktopReplacement::Beside
                                          : DesktopReplacement::Off);
        };
        addRow("uos-dt-replace-row", "At sign-in", systemDesktop_);
    }
    page->AddChild(body);

    // What starts with the desktop, unseen (UltraDesktop's own list):
    // ChatCube and UltraMail minimized, UltraClipboard and
    // UltraNotifications with no window, the Sticker board made ready.
    auto startupTitle = MakeLabel("uos-dt-startup-title", "Start with the desktop", kTextFontSize);
    startupTitle->SetFontWeight(FontWeight::Bold);
    startupTitle->SetMargin(16, 0, 4, 0);
    page->AddChild(startupTitle);
    startup_.clear();
    for (const UltraDesktop::StartupProgram& program : UltraDesktop::DesktopSettings::StartupPrograms()) {
        const std::string id = std::string("uos-dt-startup-") + program.program;
        auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, 520, 26, program.label);
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) {
            if (!refreshing_) SaveDesktopPage();
        };
        startup_.emplace_back(program.program, box);
        page->AddChild(box);
    }

    // Windows, but a copy that cannot switch (the Microsoft Store one, whose
    // registry changes sign-in never sees): say why the choice is missing.
    if (const std::string why = UltraCanvasDesktopShell::GetSystemDesktopSwitchUnavailableReason();
        !why.empty()) {
        auto note = MakeText("uos-dt-replace-note", "At sign-in: " + why, kTextWidth, kTextFontSize,
                             kMutedTextColor);
        note->SetMargin(10, 0, 0, 0);
        page->AddChild(note);
    }

    desktopStatus_ = MakeLabel("uos-dt-status", "", kTextFontSize, kMutedTextColor);
    desktopStatus_->SetMargin(10, 0, 0, 0);
    page->AddChild(desktopStatus_);

    auto spacer = std::make_shared<UltraCanvasContainer>("uos-dt-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    spacer->size.height = CSSLayout::Dimension::Px(24);
    page->AddChild(spacer);

    std::vector<std::string> noteTexts = {
            "Changes are saved at once - a text field when you leave it or press "
            "Return - and a running desktop takes them over within a second.",
            "The RAM disc is the folder the desktop's drive button opens - empty, "
            "no button; the file manager is the program its folder buttons start. "
            "Virtual desktops are asked of the window manager, which may offer a "
            "different number.",
            "The Apps window sorts the applications into Office, Internet, "
            "Graphics, Video, Audio, CAD, Games, Tools and System, after the "
            "ones started last (Recent). A category with more applications than "
            "the Sub-categories choice is split into its kinds - Web browsers, "
            "Painting, Board games - and the window's two icons at its top left "
            "switch between the tiles and the categories beside a list.",
            "A program that starts with the desktop opens minimized a few seconds "
            "after it - unless it is running already - and keeps working in the "
            "background: ChatCube receives messages, UltraMail fetches mail and "
            "shows the unread count on the desktop's mail icon. The desktop's "
            "settings button offers the same choice on its right-click menu."};
    if (systemDesktop_) {
        noteTexts.push_back(
            "At sign-in, for this account: beside Explorer, UltraDesktop starts "
            "with the session and Explorer's taskbar hides itself, while "
            "Explorer's tray, notifications and Start menu keep working - from "
            "now on. Instead of Explorer, UltraDesktop is the whole desktop from "
            "the next sign-in: it shows the programs' tray icons and offers "
            "sign out, restart and shut down; Explorer does not start, and with "
            "it go the parts of Windows it hosts (the Start menu, the quick "
            "settings, the notification centre). Should UltraDesktop fail, "
            "Explorer starts in its place for that session.");
        noteTexts.push_back(
            "\"The Windows desktop\", or \"Switch back to the Windows desktop\" on "
            "the desktop's settings button, brings Explorer back at once. If the "
            "desktop ever does not respond: Ctrl+Shift+Esc, \"Run new task\", "
            "UltraDesktop --restore-desktop.");
    }
    auto notes = MakeNotes("uos-dt-note", noteTexts);
    page->AddChild(notes);

    pages[kPageDesktop] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::RefreshDesktopPage() {
    UltraDesktop::DesktopSettings settings;
    std::string error;
    const std::string path = UltraDesktop::DesktopSettings::DefaultPath();
    const bool read = settings.Load(path, &error);

    refreshing_ = true;
    if (edge_) {
        edge_->SetSelectedIndex(settings.taskbarEdge == UltraDesktop::TaskbarEdge::Top ? 1
                                : settings.taskbarEdge == UltraDesktop::TaskbarEdge::Bottom ? 2 : 0,
                                false);
    }
    if (wallpaper_) wallpaper_->SetText(settings.wallpaper);
    if (ramDisc_) ramDisc_->SetText(settings.ramDiscPath);
    if (filer_) filer_->SetText(settings.filerProgram);
    if (desktops_) desktops_->SetSelectedIndex(std::clamp(settings.virtualDesktops, 1, 9) - 1, false);
    if (appsView_) appsView_->SetSelectedIndex(settings.appsView == "tree" ? 1 : 0, false);
    if (appsSplit_) appsSplit_->SetSelectedIndex(NearestChoice(kAppsSplitChoices, settings.appsSplitAbove), false);
    if (recentShown_) recentShown_->SetSelectedIndex(NearestChoice(kRecentShownChoices, settings.recentAppsShown), false);
    if (boardDisplay_) {
        const std::string& display = settings.categoryBoard.display;
        boardDisplay_->SetSelectedIndex(display == "text" ? 0 : display == "row" ? 2 : 1, false);
    }
    if (boardPreview_) {
        const std::string& preview = settings.categoryBoard.hoverPreview;
        boardPreview_->SetSelectedIndex(preview == "small" ? 0 : preview == "large" ? 2 : 1, false);
    }
    for (auto& [program, box] : startup_) box->SetChecked(settings.StartsWithDesktop(program));
    if (systemDesktop_) {
        const DesktopReplacement mode = UltraCanvasDesktopShell::GetSystemDesktopReplacement();
        systemDesktop_->SetSelectedIndex(mode == DesktopReplacement::Instead ? 2
                                         : mode == DesktopReplacement::Beside ? 1 : 0,
                                         false);
    }
    refreshing_ = false;
    if (desktopStatus_) {
        desktopStatus_->SetText(read ? "Settings file: " + path
                                     : "The settings file could not be read: " + error);
    }
}

void UOSSettingsWindow::SaveDesktopPage() {
    if (refreshing_) return;
    const std::string path = UltraDesktop::DesktopSettings::DefaultPath();
    if (path.empty()) return;
    // Read, change, write: the sticky notes in the same file are the
    // desktop's, and whatever it wrote last is kept.
    UltraDesktop::DesktopSettings settings;
    std::string error;
    settings.Load(path, &error);
    if (edge_) {
        const int index = edge_->GetSelectedIndex();
        settings.taskbarEdge = index == 1 ? UltraDesktop::TaskbarEdge::Top
                             : index == 2 ? UltraDesktop::TaskbarEdge::Bottom
                                          : UltraDesktop::TaskbarEdge::Left;
    }
    if (wallpaper_) settings.wallpaper = wallpaper_->GetText();
    if (ramDisc_) settings.ramDiscPath = ramDisc_->GetText();   // "" = no RAM disc button
    if (filer_ && !filer_->GetText().empty()) settings.filerProgram = filer_->GetText();
    if (desktops_) settings.virtualDesktops = std::clamp(desktops_->GetSelectedIndex() + 1, 1, 9);
    if (appsView_) settings.appsView = appsView_->GetSelectedIndex() == 1 ? "tree" : "tiles";
    if (appsSplit_) settings.appsSplitAbove = ChoiceAt(kAppsSplitChoices, appsSplit_->GetSelectedIndex(), 8);
    if (recentShown_) settings.recentAppsShown = ChoiceAt(kRecentShownChoices, recentShown_->GetSelectedIndex(), 8);
    if (boardDisplay_) {
        const int index = boardDisplay_->GetSelectedIndex();
        settings.categoryBoard.display = index == 0 ? "text" : index == 2 ? "row" : "thumbnail";
    }
    if (boardPreview_) {
        const int index = boardPreview_->GetSelectedIndex();
        settings.categoryBoard.hoverPreview = index == 0 ? "small" : index == 2 ? "large" : "medium";
    }
    for (const auto& [program, box] : startup_) settings.SetStartsWithDesktop(program, box->IsChecked());
    if (!settings.Save(path, &error)) {
        if (desktopStatus_) desktopStatus_->SetText("Not saved: " + error);
        return;
    }
    if (desktopStatus_) desktopStatus_->SetText("Saved to " + path);
}

void UOSSettingsWindow::SetSystemDesktop(DesktopReplacement how) {
    std::string error;
    std::string status;
    const std::string program = UltraCanvasDesktopShell::FindProgram("UltraDesktop");
    if (how == DesktopReplacement::Beside) {
        if (UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--session"}, &error, how)) {
            status = "UltraDesktop is the desktop now and at every sign-in; Explorer's taskbar hides itself.";
            // Now, not only from the next sign-in - unless it is open already.
            bool running = false;
            for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
                if (w.appClass == "UltraDesktop") running = true;
            }
            if (!running && !UltraCanvasDesktopShell::LaunchProgram("UltraDesktop", {"--session"}, &error)) {
                status = "UltraDesktop is the desktop from the next sign-in; it could not be started now: " + error;
            }
        }
    } else if (how == DesktopReplacement::Instead) {
        if (UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--shell"}, &error, how)) {
            status = "From the next sign-in UltraDesktop is the desktop and Explorer does not start. Sign out "
                     "and in again to switch now.";
        }
    } else if (UltraCanvasDesktopShell::RestoreSystemDesktop(&error)) {
        status = UltraCanvasDesktopShell::IsSystemDesktopRunning()
                     ? "The Windows desktop is back. A desktop started at sign-in closes within a second."
                     : "The Windows desktop is back: UltraDesktop closes, and Explorer starts in its place.";
    }
    if (status.empty()) {
        status = "Not changed: " + error;
        // The choice shows what is true, not what was asked for.
        refreshing_ = true;
        if (systemDesktop_) {
            const DesktopReplacement mode = UltraCanvasDesktopShell::GetSystemDesktopReplacement();
            systemDesktop_->SetSelectedIndex(mode == DesktopReplacement::Instead ? 2
                                             : mode == DesktopReplacement::Beside ? 1 : 0,
                                             false);
        }
        refreshing_ = false;
    }
    if (desktopStatus_) desktopStatus_->SetText(status);
}

void UOSSettingsWindow::BrowseWallpaper() {
    FileDialogOptions options;
    options.title = Tr("Choose a wallpaper");
    options.parentWindow = window.get();
    options.AddFilter("Pictures", std::vector<std::string>{"jpg", "jpeg", "png", "webp",
                                                           "avif", "heic", "bmp"});
    UltraCanvasFileLoader::OpenFileDialog(options, [this](DialogResult result,
                                                          const std::string& path) {
        if (result != DialogResult::OK || path.empty() || !wallpaper_) return;
        wallpaper_->SetText(path);
        SaveDesktopPage();
    });
}

// ===== FILE DIALOGS > LAST USED FOLDER =====

void UOSSettingsWindow::BuildLastFolderPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-last-folder");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-lf-title", "Last used folder", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-lf-caption",
            "Where the file dialog of an application opens when the application "
            "does not ask for a folder itself - \"Attach file\" in UltraMail, for "
            "one.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    // ----- the switch -----
    auto modeRow = std::make_shared<UltraCanvasContainer>("uos-lf-mode-row");
    modeRow->layout.SetFlexRow().SetFlexGap(12)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    modeRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    modeRow->size.height = CSSLayout::Dimension::Px(32);
    modeRow->SetMargin(14, 0, 0, 0);
    modeRow->AddChild(MakeLabel("uos-lf-mode-label", "Applications use:"));
    modeSwitch_ = MakeScopeSwitch("uos-lf-mode", 320, 28);
    modeSwitch_->SetSegmentText(kSegmentGlobal, "One common folder");
    modeSwitch_->SetSegmentText(kSegmentIndividual, "Their own folders");
    modeSwitch_->onSegmentSelected = [this](int index) {
        if (refreshing_) return;
        SetMode(index == kSegmentIndividual);
    };
    modeRow->AddChild(modeSwitch_);
    page->AddChild(modeRow);

    // ----- the table: header + scrolling rows -----
    tableNote_ = MakeText("uos-lf-table-note", "", kTextWidth, kTextFontSize, kNoteTextColor);
    tableNote_->SetMargin(14, 0, 6, 0);
    page->AddChild(tableNote_);

    auto table = MakeTable("uos-lf-table",
                           {{"app", "Application", kAppColumnWidth},
                            {"folder", "Opens in", 0},
                            {"scope", "Last used folder", kScopeColumnWidth}},
                           tableRows_);
    page->AddChild(table);

    // ----- notes -----
    auto notes = MakeNotes("uos-lf-note", {
            "One common folder: whichever application last used a file dialog, "
            "the next one opens there.",
            "Their own folders: each application set to Individual keeps its own "
            "last folder, starting from the common one until it has used a folder "
            "of its own; one set to Global still shares the common folder. "
            "Changes are saved at once and apply the next time a file dialog opens."});
    notes->SetMargin(14, 0, 0, 0);
    page->AddChild(notes);

    // The applications that use the framework's file dialog, listed before
    // any of them has opened one.
    for (const std::string& app : KnownFileDialogApplications()) AddAppRow(app);

    pages[kPageLastFolder] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::AddAppRow(const std::string& appName) {
    if (rows_.count(appName) || !tableRows_) return;
    const std::string id = "uos-lf-row-" + std::to_string(rows_.size());

    auto row = MakeTableRow(id);

    auto name = MakeLabel(id + "-app", appName);
    name->size.width = CSSLayout::Dimension::Px(kAppColumnWidth);
    row->AddChild(name);

    AppRow entry;
    entry.folder = MakeLabel(id + "-folder", "");
    entry.folder->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    entry.folder->size.width = CSSLayout::Dimension::Px(0);
    row->AddChild(entry.folder);

    entry.scope = MakeScopeSwitch(id + "-scope", kScopeColumnWidth, kScopeSwitchHeight);
    entry.scope->onSegmentSelected = [this, appName](int index) {
        if (refreshing_) return;
        SetAppScope(appName, index == kSegmentIndividual);
    };
    row->AddChild(entry.scope);

    rows_[appName] = entry;
    tableRows_->AddChild(row);
}

void UOSSettingsWindow::RefreshLastFolderPage() {
    const FileDialogSettings settings = FileDialogSettings::Load();
    // An application that used a file dialog but is not in the known list
    // (a newer one) gets its row too.
    for (const auto& [name, app] : settings.apps) AddAppRow(name);

    refreshing_ = true;
    const bool individual = settings.lastFolderMode == LastFolderScope::Individual;
    if (modeSwitch_)
        modeSwitch_->SetSelectedIndex(individual ? kSegmentIndividual : kSegmentGlobal);
    if (tableNote_) {
        tableNote_->SetText(individual
                ? "Choose for each application whether it shares the common folder "
                  "(Global) or keeps its own (Individual):"
                : "Every application shares the common folder. Choose \"Their own "
                  "folders\" above to set this per application.");
    }
    for (auto& [name, row] : rows_) {
        const std::string folder = settings.LastFolderFor(name);
        row.folder->SetText(folder.empty() ? "(not used yet)" : folder);
        row.folder->SetTextColor(folder.empty() ? kMutedTextColor : kTextColor);
        row.folder->SetTooltip(folder);
        // Under one common folder the per-application choice does not apply:
        // both halves greyed out (the control draws "disabled" per segment).
        // Enabled before the selection is set - a disabled segment cannot be
        // selected.
        row.scope->SetSegmentEnabled(kSegmentGlobal, true);
        row.scope->SetSegmentEnabled(kSegmentIndividual, true);
        const LastFolderScope own = settings.apps.count(name)
                ? settings.apps.at(name).scope : LastFolderScope::Individual;
        row.scope->SetSelectedIndex(own == LastFolderScope::Individual
                                    ? kSegmentIndividual : kSegmentGlobal);
        row.scope->SetSegmentEnabled(kSegmentGlobal, individual);
        row.scope->SetSegmentEnabled(kSegmentIndividual, individual);
        row.scope->SetDisabled(!individual);
    }
    refreshing_ = false;
    if (window) window->RequestRedraw();
}

void UOSSettingsWindow::SetMode(bool individual) {
    FileDialogSettings::Update([individual](FileDialogSettings& s) {
        s.lastFolderMode = individual ? LastFolderScope::Individual : LastFolderScope::Global;
    });
    RefreshLastFolderPage();
}

void UOSSettingsWindow::SetAppScope(const std::string& appName, bool individual) {
    FileDialogSettings::Update([&appName, individual](FileDialogSettings& s) {
        s.apps[appName].scope = individual ? LastFolderScope::Individual
                                           : LastFolderScope::Global;
    });
    RefreshLastFolderPage();
}

// ===== APPEARANCE > EMOJI =====

void UOSSettingsWindow::BuildEmojiPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-emoji");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-em-title", "Emoji", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-em-caption",
            "How the applications draw emoji - in a mail's subject, a chat, a file "
            "name. ULTRA OS brings two emoji fonts of its own, so they show on "
            "every computer, whatever fonts it has.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    auto styleRow = std::make_shared<UltraCanvasContainer>("uos-em-style-row");
    styleRow->layout.SetFlexRow().SetFlexGap(12)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    styleRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    styleRow->size.height = CSSLayout::Dimension::Px(32);
    styleRow->SetMargin(14, 0, 0, 0);
    styleRow->AddChild(MakeLabel("uos-em-style-label", "Emoji:"));
    emojiStyle_ = std::make_shared<UltraCanvasSegmentedControl>("uos-em-style", 0, 0, 380, 28);
    {
        SegmentedControlStyle style = emojiStyle_->GetStyle();
        style.fontSize = kTextFontSize;
        style.paddingVertical = 2;
        style.paddingHorizontal = 8;
        emojiStyle_->SetStyle(style);
    }
    emojiStyle_->AddSegment("Colour");            // kSegmentEmojiColour
    emojiStyle_->AddSegment("Black and white");   // kSegmentEmojiMonochrome
    emojiStyle_->AddSegment("System");            // kSegmentEmojiSystem
    emojiStyle_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    emojiStyle_->onSegmentSelected = [this](int index) {
        if (refreshing_) return;
        EmojiSettings settings;
        settings.style = index == kSegmentEmojiMonochrome ? EmojiStyle::Monochrome
                       : index == kSegmentEmojiSystem     ? EmojiStyle::System
                                                          : EmojiStyle::Colour;
        if (emojiStatus_) {
            emojiStatus_->SetText(settings.Save()
                    ? "Saved. Applications started from now on draw emoji this way."
                    : "Could not write " + PathToUtf8(EmojiSettings::FilePath()) + ".");
        }
    };
    styleRow->AddChild(emojiStyle_);
    page->AddChild(styleRow);

    // In the style this window started with: the setting takes effect when
    // an application starts, this one included.
    auto sample = MakeLabel("uos-em-sample",
            "\xF0\x9F\x91\xA4 \xF0\x9F\x98\x80 \xF0\x9F\x8E\x89 \xE2\x9D\xA4\xEF\xB8\x8F "
            "\xF0\x9F\x91\x8D \xF0\x9F\x93\xA7 \xF0\x9F\x8C\x8D",
            18.0f);
    sample->SetMargin(14, 0, 0, 0);
    page->AddChild(sample);

    emojiStatus_ = MakeLabel("uos-em-status", "", kTextFontSize, kMutedTextColor);
    emojiStatus_->SetMargin(10, 0, 0, 0);
    page->AddChild(emojiStatus_);

    auto spacer = std::make_shared<UltraCanvasContainer>("uos-em-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    page->AddChild(spacer);

    auto notes = MakeNotes("uos-em-note", {
            "Colour: Noto Color Emoji, the same pictures on every computer.",
            "Black and white: Noto Emoji, drawn in the colour of the text around "
            "them - quieter, and the smaller font of the two.",
            "System: the computer's own emoji font (Segoe UI Emoji on Windows); "
            "Noto Color Emoji fills in where it has none.",
            "The sample above shows the setting this window started with. An "
            "application uses a change the next time it starts. On macOS the "
            "applications always draw Apple's own emoji."});
    notes->SetMargin(14, 0, 0, 0);
    page->AddChild(notes);

    pages[kPageEmoji] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::RefreshEmojiPage() {
    if (!emojiStyle_) return;
    const EmojiStyle style = EmojiSettings::Load().style;
    refreshing_ = true;
    emojiStyle_->SetSelectedIndex(style == EmojiStyle::Monochrome ? kSegmentEmojiMonochrome
                                : style == EmojiStyle::System     ? kSegmentEmojiSystem
                                                                  : kSegmentEmojiColour);
    refreshing_ = false;
}

// ===== NOTIFICATIONS > REGISTRATIONS =====

void UOSSettingsWindow::BuildNotificationsPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-registrations");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-nr-title", "Notification registrations", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-nr-caption",
            "What Windows keeps for each application whose notifications were shown: "
            "its name and icon, and the program a click on one of its notifications "
            "starts. The uninstaller removes it; a folder unpacked from the ZIP "
            "package has no uninstaller, so remove it here before deleting the folder.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    registrationNote_ = MakeText("uos-nr-table-note", "", kTextWidth, kTextFontSize, kNoteTextColor);
    registrationNote_->SetMargin(14, 0, 6, 0);
    page->AddChild(registrationNote_);

    page->AddChild(MakeTable("uos-nr-table",
                             {{"app", "Application", kRegistrationAppColumnWidth},
                              {"activator", "Click starts", 0}},
                             registrationRows_));

    auto actions = std::make_shared<UltraCanvasContainer>("uos-nr-actions");
    actions->layout.SetFlexRow().SetFlexGap(12)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    actions->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    actions->size.height = CSSLayout::Dimension::Px(36);
    actions->SetMargin(10, 0, 0, 0);
    removeRegistrations_ = std::make_shared<UltraCanvasButton>("uos-nr-remove", 0, 0, 120, 28, Tr("Remove all"));
    removeRegistrations_->SetFontSize(kTextFontSize);
    removeRegistrations_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    removeRegistrations_->SetOnClick([this]() { RemoveNotificationRegistrations(); });
    actions->AddChild(removeRegistrations_);
    registrationStatus_ = MakeLabel("uos-nr-status", "", kTextFontSize, kMutedTextColor);
    actions->AddChild(registrationStatus_);
    page->AddChild(actions);

    auto notes = MakeNotes("uos-nr-note", {
            "An application registers again the next time it starts or shows a "
            "notification, so removing does no harm to one you keep.",
            "From a command prompt or a script: UOS-Settings "
            "--remove-notification-registrations - which the ZIP package's "
            "uc-remove-registrations.cmd runs."});
    notes->SetMargin(14, 0, 0, 0);
    page->AddChild(notes);

    pages[kPageRegistrations] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::RefreshNotificationsPage() {
    if (!registrationRows_) return;
    const std::vector<NotificationRegistration> listed = ListNotificationRegistrations();
    registrationRows_->ClearChildren();
    for (size_t i = 0; i < listed.size(); ++i) {
        const std::string id = "uos-nr-row-" + std::to_string(i);
        auto row = MakeTableRow(id);
        auto app = MakeLabel(id + "-app", listed[i].appId);
        app->size.width = CSSLayout::Dimension::Px(kRegistrationAppColumnWidth);
        app->SetTooltip(listed[i].appId);
        row->AddChild(app);
        auto activator = MakeLabel(id + "-activator",
                listed[i].activatorClassId.empty() ? "(nothing: a click reaches it only while the bus runs)"
                                                   : "the application itself, " + listed[i].activatorClassId,
                kTextFontSize, listed[i].activatorClassId.empty() ? kMutedTextColor : kTextColor);
        activator->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        activator->size.width = CSSLayout::Dimension::Px(0);
        activator->SetTooltip(listed[i].activatorClassId);
        row->AddChild(activator);
        registrationRows_->AddChild(row);
    }
#if defined(_WIN32) || defined(_WIN64)
    const bool windows = true;
#else
    const bool windows = false;
#endif
    if (registrationNote_) {
        registrationNote_->SetText(!windows ? "Only Windows keeps notification registrations; there is nothing here to remove."
                                   : listed.empty() ? "Nothing is registered: no application has shown a notification yet, "
                                                      "or the registrations were removed."
                                                    : "Registered for this Windows account:");
    }
    if (removeRegistrations_) removeRegistrations_->SetDisabled(listed.empty());
    if (pageArea) pageArea->InvalidateLayout();
}

void UOSSettingsWindow::RemoveNotificationRegistrations() {
    const size_t removed = UltraCanvas::RemoveNotificationRegistrations();
    RefreshNotificationsPage();
    if (registrationStatus_) {
        registrationStatus_->SetText(removed == 0 ? "Nothing to remove."
                                     : removed == 1 ? "Removed 1 registration."
                                                    : "Removed " + std::to_string(removed) + " registrations.");
    }
}

// ===== LANGUAGE =====

void UOSSettingsWindow::BuildLanguagePage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-language");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-lang-title", Tr("Language"), kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-lang-caption",
            Tr("The language every application shows its menus, buttons and messages "
               "in. \"System\" follows the computer's own language setting; a language "
               "is offered here when a translation of the standard set ships for it."));
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    auto row = std::make_shared<UltraCanvasContainer>("uos-lang-row");
    row->layout.SetFlexRow().SetFlexGap(12)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.height = CSSLayout::Dimension::Px(32);
    row->SetMargin(14, 0, 0, 0);
    row->AddChild(MakeLabel("uos-lang-label", Tr("Language:")));
    language_ = std::make_shared<UltraCanvasDropdown>("uos-lang-dropdown", 0, 0, 300, 28);
    language_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    language_->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (refreshing_ || index < 0 || index >= static_cast<int>(languageTags_.size())) return;
        LanguageSettings settings = LanguageSettings::Load(PathToUtf8(UltraCanvasSettingsFolder()));
        settings.language = languageTags_[static_cast<size_t>(index)];
        if (languageStatus_) {
            languageStatus_->SetText(settings.Save(PathToUtf8(UltraCanvasSettingsFolder()))
                    ? Tr("Saved. Applications started from now on use this language.")
                    : TrF("Could not write {0}.", LanguageSettings::FilePath(PathToUtf8(UltraCanvasSettingsFolder()))));
        }
    };
    row->AddChild(language_);
    page->AddChild(row);

    auto translatorRow = std::make_shared<UltraCanvasContainer>("uos-lang-translator-row");
    translatorRow->layout.SetFlexRow().SetFlexGap(12)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    translatorRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    translatorRow->size.height = CSSLayout::Dimension::Px(32);
    translatorRow->SetMargin(10, 0, 0, 0);
    translatorRow->AddChild(MakeLabel("uos-lang-translator-label", Tr("Translator:")));
    translator_ = std::make_shared<UltraCanvasTextInput>("uos-lang-translator", 0, 0, 300, 28);
    translator_->SetPlaceholder(Tr("Your name, for the catalogues you edit"));
    translator_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    translator_->onFocusLost = [this]() {
        if (refreshing_ || !translator_) return;
        LanguageSettings settings = LanguageSettings::Load(PathToUtf8(UltraCanvasSettingsFolder()));
        if (settings.translator == translator_->GetText()) return;
        settings.translator = translator_->GetText();
        if (languageStatus_) {
            languageStatus_->SetText(settings.Save(PathToUtf8(UltraCanvasSettingsFolder()))
                    ? Tr("Saved.")
                    : TrF("Could not write {0}.", LanguageSettings::FilePath(PathToUtf8(UltraCanvasSettingsFolder()))));
        }
    };
    translatorRow->AddChild(translator_);
    page->AddChild(translatorRow);

    languageStatus_ = MakeLabel("uos-lang-status", "", kTextFontSize, kMutedTextColor);
    languageStatus_->SetMargin(10, 0, 0, 0);
    page->AddChild(languageStatus_);

    auto spacer = std::make_shared<UltraCanvasContainer>("uos-lang-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    page->AddChild(spacer);

    auto notes = MakeNotes("uos-lang-note", {
            Tr("The languages come from media/locale/ in the applications' resources "
               "and from locale/ in the UltraCanvas settings folder, where a translator "
               "puts a draft to see it in the real window."),
            Tr("An application uses a change the next time it starts. The command line "
               "(--lang) and the ULTRACANVAS_LANG variable override this setting for "
               "one start."),
            Tr("The translator's name goes into the catalogues the localisation editor "
               "saves, so a draft says who reviewed it.")});
    notes->SetMargin(14, 0, 0, 0);
    page->AddChild(notes);

    pages[kPageLanguage] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::BuildTranslationsPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-translations");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-translations-title", Tr("Translations"), kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-translations-caption",
            Tr("Every set an application's texts come from in one grid - its own set, the base sets "
               "it takes and the standard set - for any application and any language. A save goes "
               "into the source tree when the catalogues are writable, else into the UltraCanvas "
               "settings folder, where a running application picks it up; the status line says where."));
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    translations_ = std::make_shared<UltraCanvasLocalisationEditor>("uos-translations-editor");
    translations_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    translations_->SetMargin(12, 0, 0, 0);
    page->AddChild(translations_);

    pages[kPageTranslations] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::RefreshTranslationsPage() {
    if (!translations_) return;
    const std::string settingsFolder = PathToUtf8(UltraCanvasSettingsFolder());
    const std::string shipped = UltraLocalisation::ShippedFolder().empty()
            ? NormalizePath(GetResourcesDir() + "media/locale") : UltraLocalisation::ShippedFolder();
    translations_->SetFolders(shipped, settingsFolder);
    translations_->SetTranslatorName(LanguageSettings::Load(settingsFolder).translator);
    if (translations_->GetWorkspace().Application().empty()) {
        const LanguageSettings settings = LanguageSettings::Load(settingsFolder);
        const std::string language = settings.language == "system" || settings.language.empty()
                ? UltraLocalisation::CurrentLanguage() : settings.language;
        translations_->Open("", language.empty() ? "en" : language);
    }
}

void UOSSettingsWindow::RefreshLanguagePage() {
    if (!language_) return;
    const std::string settingsFolder = PathToUtf8(UltraCanvasSettingsFolder());
    const LanguageSettings settings = LanguageSettings::Load(settingsFolder);
    refreshing_ = true;
    language_->ClearItems();
    languageTags_.clear();
    language_->AddItem(Tr("System"), "system");
    languageTags_.push_back("system");
    int selected = 0;
    for (const std::string& tag : UltraLocalisation::AvailableLanguages()) {
        const std::string name = UltraLocalisation::NativeName(tag);
        language_->AddItem(name == tag ? tag : name + "  (" + tag + ")", tag);
        languageTags_.push_back(tag);
        if (tag == settings.language) selected = static_cast<int>(languageTags_.size()) - 1;
    }
    language_->SetSelectedIndex(selected, false);
    if (translator_) translator_->SetText(settings.translator);
    refreshing_ = false;
}

// ===== DEVICES > TRUSTED CERTIFICATES =====

void UOSSettingsWindow::BuildCertificatesPage() {
    auto page = std::make_shared<UltraCanvasContainer>("uos-page-certificates");
    page->layout.SetFlexColumn().SetFlexGap(0)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = MakeLabel("uos-tc-title", "Trusted certificates", kTitleFontSize);
    title->SetFontWeight(FontWeight::Bold);
    page->AddChild(title);
    auto caption = MakeText("uos-tc-caption",
            "Network scanners and printers that are reached over HTTPS with a "
            "certificate they signed themselves. Each was trusted the first time "
            "it was reached; from then on it is reached only while it presents "
            "the same key - shown here as the SHA-256 of its public key.");
    caption->SetMargin(6, 0, 0, 0);
    page->AddChild(caption);

    certificateNote_ = MakeText("uos-tc-table-note", "", kTextWidth, kTextFontSize,
                                kNoteTextColor);
    certificateNote_->SetMargin(14, 0, 6, 0);
    page->AddChild(certificateNote_);

    page->AddChild(MakeTable("uos-tc-table",
                             {{"device", "Device", kDeviceColumnWidth},
                              {"address", "Address", kAddressColumnWidth},
                              {"key", "Key", 0},
                              {"forget", "", kForgetColumnWidth}},
                             certificateRows_));

    certificateStatus_ = MakeLabel("uos-tc-status", "", kTextFontSize, kMutedTextColor);
    certificateStatus_->SetMargin(10, 0, 0, 0);
    page->AddChild(certificateStatus_);

    auto notes = MakeNotes("uos-tc-note", {
            "Forget a device only when you know it was reset or replaced and so "
            "presents a new certificate - the connection to it is refused until "
            "then. Its new key is trusted the next time it is reached, on the same "
            "terms as the first time: whatever answers at that address then.",
            "A device with a certificate a certificate authority vouches for is "
            "never listed: it is checked the ordinary way."});
    notes->SetMargin(14, 0, 0, 0);
    page->AddChild(notes);

    pages[kPageCertificates] = page;
    pageArea->AddChild(page);
}

void UOSSettingsWindow::AddCertificateRow(const std::string& address) {
    if (certificates_.count(address) || !certificateRows_) return;
    const std::string id = "uos-tc-row-" + std::to_string(certificates_.size());

    CertificateRow entry;
    entry.row = MakeTableRow(id);

    entry.name = MakeLabel(id + "-device", "");
    entry.name->size.width = CSSLayout::Dimension::Px(kDeviceColumnWidth);
    entry.row->AddChild(entry.name);

    auto where = MakeLabel(id + "-address", address);
    where->size.width = CSSLayout::Dimension::Px(kAddressColumnWidth);
    where->SetTooltip(address);
    entry.row->AddChild(where);

    entry.key = MakeLabel(id + "-key", "", kTextFontSize, kMutedTextColor);
    entry.key->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    entry.key->size.width = CSSLayout::Dimension::Px(0);
    entry.row->AddChild(entry.key);

    auto forget = std::make_shared<UltraCanvasButton>(id + "-forget", 0, 0,
                                                      kForgetColumnWidth, 24, Tr("Forget"));
    forget->SetFontSize(kTextFontSize);
    forget->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    forget->SetOnClick([this, address]() { ForgetCertificate(address); });
    entry.row->AddChild(forget);

    certificates_[address] = entry;
    certificateRows_->AddChild(entry.row);
}

void UOSSettingsWindow::RefreshCertificatesPage() {
    const std::vector<IODeviceTrustedCertificate> trusted = IODeviceTrustedCertificates();
    std::map<std::string, IODeviceTrustedCertificate> byAddress;
    for (const IODeviceTrustedCertificate& certificate : trusted) {
        byAddress[certificate.address] = certificate;
        AddCertificateRow(certificate.address);
    }
    // A forgotten device's row stays, hidden, so it can come back in place
    // when the device is trusted again.
    for (auto& [address, row] : certificates_) {
        auto found = byAddress.find(address);
        row.row->SetVisible(found != byAddress.end());
        if (found == byAddress.end()) continue;
        const IODeviceTrustedCertificate& certificate = found->second;
        // A key learned before the device's name was known, or for one only
        // ever named by its address, has none to show.
        row.name->SetText(certificate.name.empty() ? "(name not known)" : certificate.name);
        row.name->SetTextColor(certificate.name.empty() ? kMutedTextColor : kTextColor);
        row.name->SetTooltip(certificate.name);
        // "sha256//" is the form the pin is stored in, not part of the key.
        std::string shown = certificate.pin;
        if (shown.rfind("sha256//", 0) == 0) shown.erase(0, 8);
        row.key->SetText(shown);
        row.key->SetTooltip(certificate.pin);
    }
    if (certificateNote_) {
        certificateNote_->SetText(trusted.empty()
                ? "No device's certificate has been trusted yet. A scanner or printer "
                  "is listed here once it has been reached over HTTPS with a "
                  "certificate it signed itself."
                : "Forget a device to have its key learned again the next time it is "
                  "reached:");
    }
    if (certificateStatus_) {
        const std::string file = PathToUtf8(IODeviceTrustedCertificatesFile());
        certificateStatus_->SetText(file.empty() ? "There is no settings folder to keep keys in."
                                                 : "Kept in " + file);
    }
    if (window) window->RequestRedraw();
}

void UOSSettingsWindow::ForgetCertificate(const std::string& address) {
    // "Office Scanner (192.168.1.20:443)" when its name is known.
    std::string device = address;
    for (const IODeviceTrustedCertificate& certificate : IODeviceTrustedCertificates()) {
        if (certificate.address == address && !certificate.name.empty())
            device = certificate.name + " (" + address + ")";
    }
    const bool forgotten = IODeviceForgetCertificate(address);
    RefreshCertificatesPage();
    // After the refresh, which shows where the keys are kept: what was done
    // matters more just now.
    if (certificateStatus_) {
        certificateStatus_->SetText(forgotten
                ? "Forgot the key of " + device + "; it is learned again the next "
                  "time the device is reached."
                : "Could not forget the key of " + device + " - " +
                  PathToUtf8(IODeviceTrustedCertificatesFile()) + " could not be written.");
    }
}

} // namespace UOSSettings

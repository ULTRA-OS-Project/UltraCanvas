// core/UltraCanvasFontPicker.cpp
// The font list (see the header): the closed box, the open list in a popup
// (an UltraCanvasListView with a delegate that draws each row), the styles
// list in a second popup beside it, and the pictures of the names kept and
// prepared in idle slices.
// Version: 1.0.1 - Escape left to the window (closeByEscapeKey), which now closes one popup per
//                  press: the list's own Escape, the workaround for the release, is gone;
//                  SetFamilies makes the names' pictures again
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework

#include "UltraCanvasFontPicker.h"

#include "UltraCanvasApplication.h"     // StartTimer, GetFontSetGeneration
#include "UltraCanvasConfig.h"          // GetResourcesDir
#include "UltraCanvasFontCoverage.h"    // FontFamilySubstitute
#include "UltraCanvasImage.h"
#include "UltraCanvasListDelegate.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListSelection.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasUtils.h"           // NormalizePath
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace UltraCanvas {

namespace {

std::string LowerText(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "{0}" (and "{1}") in a host's translated text.
std::string Fill(std::string text, const std::string& first, const std::string& second = std::string()) {
    auto put = [&text](const std::string& slot, const std::string& value) {
        const size_t at = text.find(slot);
        if (at != std::string::npos) text.replace(at, slot.size(), value);
    };
    put("{0}", first);
    put("{1}", second);
    return text;
}

constexpr int kIconLeft = 6;
constexpr int kTextLeft = 28;      // after the format icon
constexpr int kArrowRoom = 22;     // the styles arrow at the right
constexpr int kHoverOpenMs = 280;  // resting on a row opens its styles after this

} // namespace

std::string FontFormatIconPath(FontFileFormat format) {
    const char* name = nullptr;
    switch (format) {
        case FontFileFormat::OpenType:
        case FontFileFormat::WOFF:      // a web font: OpenType inside
        case FontFileFormat::WOFF2:    name = "opentype.svg"; break;
        case FontFileFormat::TrueType: name = "truetype.svg"; break;
        case FontFileFormat::Type1:    name = "type1.svg"; break;
        case FontFileFormat::BitmapFont:   name = "bitmap.svg"; break;
        default:                       return std::string();
    }
    return NormalizePath(GetResourcesDir() + "media/icons/fonts/" + name);
}

// ===== THE MODELS AND DELEGATES =====

class FontPickerListModel : public IListModel {
public:
    explicit FontPickerListModel(UltraCanvasFontPicker* owner) : picker(owner) {}
    int GetRowCount() const override { return picker->GetRowCount(); }
    int GetColumnCount() const override { return 1; }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        const UltraCanvasFontPicker::Row* row = picker->GetRow(index.row);
        if (!row) return std::monostate{};
        if (role == ListDataRole::DisplayRole) return row->text;
        if (role == ListDataRole::ToolTipRole) {
            if (row->kind == UltraCanvasFontPicker::RowKind::Missing) return row->text;
            if (row->familyIndex >= 0) {
                const InstalledFontFamily& family = picker->GetFamilies()[static_cast<size_t>(row->familyIndex)];
                std::string tip = family.name;
                if (family.format != FontFileFormat::Unknown) tip += std::string(" - ") + FontFormatName(family.format);
                return tip;
            }
        }
        return std::monostate{};
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }
    void Changed() { NotifyDataChanged(); }

private:
    UltraCanvasFontPicker* picker;
};

class FontPickerRowDelegate : public IItemDelegate {
public:
    explicit FontPickerRowDelegate(UltraCanvasFontPicker* owner) : picker(owner) {}
    void RenderItem(IRenderContext* ctx, const IListModel*, int row, int, const ListItemStyleOption& option) override {
        picker->RenderRow(ctx, row, option.rect, option.isSelected || option.isHovered);
    }
    int GetRowHeight(const IListModel*, int) const override { return static_cast<int>(picker->style.rowHeight); }

private:
    UltraCanvasFontPicker* picker;
};

class FontPickerStylesModel : public IListModel {
public:
    explicit FontPickerStylesModel(UltraCanvasFontPicker* owner) : picker(owner) {}
    int GetRowCount() const override { return static_cast<int>(picker->GetStylesShown().size()); }
    int GetColumnCount() const override { return 1; }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        const auto& styles = picker->GetStylesShown();
        if (index.row < 0 || index.row >= static_cast<int>(styles.size())) return std::monostate{};
        if (role == ListDataRole::DisplayRole) return styles[static_cast<size_t>(index.row)].name;
        return std::monostate{};
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }
    void Changed() { NotifyDataChanged(); }

private:
    UltraCanvasFontPicker* picker;
};

class FontPickerStylesDelegate : public IItemDelegate {
public:
    explicit FontPickerStylesDelegate(UltraCanvasFontPicker* owner) : picker(owner) {}
    void RenderItem(IRenderContext* ctx, const IListModel*, int row, int, const ListItemStyleOption& option) override {
        picker->RenderStyleRow(ctx, row, option.rect);
    }
    int GetRowHeight(const IListModel*, int) const override { return static_cast<int>(picker->style.rowHeight); }

private:
    UltraCanvasFontPicker* picker;
};

// ===== STYLE =====

FontPickerStyle FontPickerStyle::FromTheme(const UltraCanvasTheme& theme, const FontPickerStyle& base) {
    FontPickerStyle s = base;
    const ThemeRoles& r = theme.roles;
    const FontStyle body = theme.Font(ThemeTextRole::Body);
    s.fontFamily = body.fontFamily;
    s.fontSize = static_cast<float>(body.fontSize);
    s.previewSize = std::max(s.fontSize + 3.0f, base.previewSize);
    s.normalColor = r.raised;
    s.hoverColor = r.hover;
    s.pressedColor = r.pressed;
    s.disabledColor = r.panel;
    s.borderColor = r.lineStrong;
    s.focusBorderColor = r.focus;
    s.textColor = r.ink;
    s.faintTextColor = r.inkMuted;
    s.listBackgroundColor = r.raised;
    s.listBorderColor = r.line;
    s.itemHoverColor = r.hover;
    s.itemSelectedColor = r.accentSoft;
    s.dividerColor = r.line;
    s.scrollbarStyle = ScrollbarStyle::FromTheme(theme, base.scrollbarStyle);
    return s;
}

// ===== CONSTRUCTION =====

UltraCanvasFontPicker::UltraCanvasFontPicker(const std::string& identifier, float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    captionDocument = Tr("In this document");
    captionRecent = Tr("Recent");
    captionAll = Tr("All fonts");
    missingText = Tr("{0} (not installed)");
    substituteText = Tr("{0} (shown in {1})");
    ApplyThemeIfFollowing();
}

UltraCanvasFontPicker::~UltraCanvasFontPicker() {
    StopPreviewTimer();
    StopHoverTimer();
}

std::string UltraCanvasFontPicker::GetAccessibleValueText() const {
    if (mixed) return std::string();
    if (currentFamily.empty()) return defaultLabel;
    return currentStyle.empty() ? currentFamily : currentFamily + " " + currentStyle;
}

void UltraCanvasFontPicker::ApplyTheme(const UltraCanvasTheme& theme) {
    style = FontPickerStyle::FromTheme(theme, style);
    StyleChanged();
}

void UltraCanvasFontPicker::StyleChanged() {
    nameLayouts.clear();
    plainLayouts.clear();
    styleLayouts.clear();
    boxLayout.Reset();
    previewsDirty = true;
    if (listView) {
        ListViewStyle lv;
        lv.backgroundColor = style.listBackgroundColor;
        lv.rowHeight = static_cast<int>(style.rowHeight);
        lv.showHeader = false;
        lv.showGridLines = false;
        lv.alternateRowColors = false;
        lv.hoverBackgroundColor = style.itemHoverColor;
        lv.selectionBackgroundColor = style.itemSelectedColor;
        lv.scrollbarStyle = style.scrollbarStyle;
        for (auto* view : {listView.get(), stylesView.get()}) {
            if (!view) continue;
            view->SetStyle(lv);
            view->SetBorders(static_cast<int>(style.borderWidth), style.listBorderColor);
        }
    }
    RequestRedraw();
}

void UltraCanvasFontPicker::SetStyle(const FontPickerStyle& newStyle) {
    StyleSetByHand();
    style = newStyle;
    StyleChanged();
}

void UltraCanvasFontPicker::SetListWidth(float width) { style.listWidth = std::max(80.0f, width); }

void UltraCanvasFontPicker::SetPreviewSize(float size) {
    style.previewSize = std::clamp(size, 6.0f, 48.0f);
    nameLayouts.clear();
    previewsDirty = true;
    RequestRedraw();
}

// ===== WHAT IT SHOWS =====

void UltraCanvasFontPicker::SetCurrentFont(const std::string& family, const std::string& styleName) {
    if (!mixed && family == currentFamily && styleName == currentStyle) return;
    mixed = false;
    currentFamily = family;
    currentStyle = styleName;
    boxLayout.Reset();
    NotifyAccessibility(AccessibilityEventType::ValueChanged);
    RequestRedraw();
}

void UltraCanvasFontPicker::SetMixed() {
    if (mixed) return;
    mixed = true;
    currentFamily.clear();
    currentStyle.clear();
    boxLayout.Reset();
    RequestRedraw();
}

bool UltraCanvasFontPicker::IsCurrentInstalled() const {
    if (currentFamily.empty()) return true;
    return FindInstalledFontFamilyOfName(GetFamilies(), currentFamily) != nullptr;
}

void UltraCanvasFontPicker::SetDefaultFontEntry(const std::string& label) {
    defaultLabel = label;
    rowsDirty = true;
}

void UltraCanvasFontPicker::SetDocumentFonts(const std::vector<std::string>& list) {
    std::vector<std::string> cleaned;
    for (const std::string& family : list) {
        if (family.empty()) continue;
        const std::string key = LowerText(family);
        if (std::none_of(cleaned.begin(), cleaned.end(), [&](const std::string& f) { return LowerText(f) == key; }))
            cleaned.push_back(family);
    }
    if (cleaned == documentFonts) return;
    documentFonts = std::move(cleaned);
    rowsDirty = true;
}

void UltraCanvasFontPicker::SetRecentFonts(const std::vector<std::string>& list) {
    std::vector<std::string> cleaned;
    for (const std::string& family : list) {
        if (family.empty()) continue;
        const std::string key = LowerText(family);
        if (std::none_of(cleaned.begin(), cleaned.end(), [&](const std::string& f) { return LowerText(f) == key; }))
            cleaned.push_back(family);
        if (static_cast<int>(cleaned.size()) >= recentLimit) break;
    }
    if (cleaned == recentFonts) return;
    recentFonts = std::move(cleaned);
    rowsDirty = true;
}

void UltraCanvasFontPicker::SetRecentLimit(int limit) {
    recentLimit = std::clamp(limit, 0, 50);
    if (static_cast<int>(recentFonts.size()) > recentLimit) {
        recentFonts.resize(static_cast<size_t>(recentLimit));
        rowsDirty = true;
    }
}

void UltraCanvasFontPicker::SetSectionCaptions(const std::string& document, const std::string& recent,
                                               const std::string& all) {
    captionDocument = document;
    captionRecent = recent;
    captionAll = all;
    rowsDirty = true;
}

void UltraCanvasFontPicker::SetMissingTexts(const std::string& notInstalled, const std::string& shownIn) {
    missingText = notInstalled;
    substituteText = shownIn;
    plainLayouts.clear();
}

void UltraCanvasFontPicker::SetFamilies(std::shared_ptr<const std::vector<InstalledFontFamily>> list) {
    familiesGiven = list != nullptr;
    families = std::move(list);
    rowsDirty = true;
    previewsDirty = true;
    nameLayouts.clear();   // a host hands new ones when the fonts changed
}

const std::vector<InstalledFontFamily>& UltraCanvasFontPicker::GetFamilies() const {
    static const std::vector<InstalledFontFamily> none;
    if (!families) const_cast<UltraCanvasFontPicker*>(this)->RefreshFamilies();
    return families ? *families : none;
}

void UltraCanvasFontPicker::RefreshFamilies() {
    if (familiesGiven) return;
    const unsigned long generation = GetFontSetGeneration();
    if (families && familiesGeneration == generation) return;
    families = GetInstalledFontCatalogue();
    familiesGeneration = generation;
    rowsDirty = true;
    previewsDirty = true;
    nameLayouts.clear();
}

void UltraCanvasFontPicker::InvalidateLayout() {
    UltraCanvasUIElement::InvalidateLayout();
    // The fonts may have changed (the windows are laid out again then).
    if (!familiesGiven && families && familiesGeneration != GetFontSetGeneration()) {
        families.reset();
        rowsDirty = true;
        previewsDirty = true;
        boxLayout.Reset();
    }
}

void UltraCanvasFontPicker::SetStylesSubmenu(FontPickerStylesSubmenu rule) {
    stylesRule = rule;
    RequestRedraw();
}

bool UltraCanvasFontPicker::HasStylesList(const InstalledFontFamily& family) const {
    if (family.styles.size() < 2) return false;
    switch (stylesRule) {
        case FontPickerStylesSubmenu::Off:  return false;
        case FontPickerStylesSubmenu::EveryMultiStyleFamily: return true;
        default:                              return family.HasStylesBeyondBoldItalic();
    }
}

// ===== THE ROWS =====

void UltraCanvasFontPicker::BuildRows() {
    RefreshFamilies();
    const std::vector<InstalledFontFamily>& all = GetFamilies();
    rows.clear();
    auto familyRow = [&](const std::string& name, Section section) {
        Row row;
        row.section = section;
        row.text = name;
        // A face's own name ("Ubuntu Light") is its family's row, under
        // the name the document gives it.
        if (const InstalledFontFamily* found = FindInstalledFontFamilyOfName(all, name)) {
            row.kind = RowKind::Family;
            if (FindInstalledFontFamily(all, name)) row.text = found->name;
            row.familyIndex = static_cast<int>(found - all.data());
        } else {
            row.kind = RowKind::Missing;
        }
        rows.push_back(row);
    };
    auto caption = [&](const std::string& text, Section section) {
        Row row;
        row.kind = RowKind::Caption;
        row.section = section;
        row.text = text;
        row.selectable = false;
        rows.push_back(row);
    };
    if (!defaultLabel.empty()) {
        Row row;
        row.kind = RowKind::DefaultFont;
        row.section = Section::Top;
        row.text = defaultLabel;
        rows.push_back(row);
    }
    if (!documentFonts.empty()) {
        caption(captionDocument, Section::Document);
        for (const std::string& family : documentFonts) familyRow(family, Section::Document);
    }
    std::vector<std::string> recent;
    for (const std::string& family : recentFonts) {
        const std::string key = LowerText(family);
        // A recent font already named by the document is not named twice.
        if (std::none_of(documentFonts.begin(), documentFonts.end(), [&](const std::string& d) { return LowerText(d) == key; }))
            recent.push_back(family);
    }
    if (!recent.empty()) {
        caption(captionRecent, Section::Recent);
        for (const std::string& family : recent) familyRow(family, Section::Recent);
    }
    caption(captionAll, Section::AllFonts);
    for (size_t i = 0; i < all.size(); ++i) {
        Row row;
        row.kind = RowKind::Family;
        row.section = Section::AllFonts;
        row.text = all[i].name;
        row.familyIndex = static_cast<int>(i);
        rows.push_back(row);
    }
    rowsDirty = false;
    previewsDirty = true;
    if (listModel) listModel->Changed();
}

const UltraCanvasFontPicker::Row* UltraCanvasFontPicker::GetRow(int index) const {
    if (index < 0 || index >= static_cast<int>(rows.size())) return nullptr;
    return &rows[static_cast<size_t>(index)];
}

const InstalledFontFamily* UltraCanvasFontPicker::FamilyOfRow(int row) const {
    const Row* r = GetRow(row);
    if (!r || r->familyIndex < 0) return nullptr;
    const auto& all = GetFamilies();
    return r->familyIndex < static_cast<int>(all.size()) ? &all[static_cast<size_t>(r->familyIndex)] : nullptr;
}

int UltraCanvasFontPicker::FindRow(const std::string& family, Section section) const {
    auto find = [this, section](const std::string& name) {
        const std::string key = LowerText(name);
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& row = rows[i];
            if (row.section != section || !row.selectable) continue;
            if (row.kind == RowKind::DefaultFont ? name.empty() : LowerText(row.text) == key) return static_cast<int>(i);
        }
        return -1;
    };
    const int at = find(family);
    if (at >= 0 || section != Section::AllFonts || family.empty()) return at;
    // A face's own name: its family's row.
    const InstalledFontFamily* owner = FindInstalledFontFamilyOfName(GetFamilies(), family);
    return owner ? find(owner->name) : -1;
}

std::string UltraCanvasFontPicker::RowFontFamily(int row) const {
    const Row* r = GetRow(row);
    if (!r) return style.fontFamily;
    const InstalledFontFamily* family = FamilyOfRow(row);
    if (!family || family->symbol) return style.fontFamily;
    return family->name;
}

std::string UltraCanvasFontPicker::MissingRowText(const std::string& family) const {
    const std::string substitute = FontFamilySubstitute(family);
    return substitute.empty() ? Fill(missingText, family) : Fill(substituteText, family, substitute);
}

// ===== THE LIST =====

void UltraCanvasFontPicker::CreateListViews() {
    if (listView) return;
    listModel = std::make_shared<FontPickerListModel>(this);
    listDelegate = std::make_shared<FontPickerRowDelegate>(this);
    listView = std::make_shared<UltraCanvasListView>(GetIdentifier() + "_fonts", 0, 0, 300, 200);
    listView->SetModel(listModel);
    listView->SetDelegate(listDelegate);
    listView->SetShowHeader(false);
    listView->SetAcceptsFocus(false);   // the picker keeps the keyboard, as a dropdown does
    listView->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    listView->SetShowItemTooltips(true);
    listView->onCellClicked = [this](int row, int, const Point2Di& at) {
        const InstalledFontFamily* family = FamilyOfRow(row);
        if (family && HasStylesList(*family) && listView && at.x >= listView->GetCellRect(row, 0).width - kArrowRoom) {
            OpenStyles(row);
            return;
        }
        ChooseRow(row);
    };
    listView->onCellHovered = [this](int row, int, const Point2Di& at) { hoverX = at.x; (void)row; };
    listView->onItemHovered = [this](int row) { HoverChanged(row); };
    listView->onKeyDown = [this](const UCEvent& event) { return HandleListKey(event); };
    listView->onPopupClosed = [this](ClosePopupReason) {
        listOpen = false;
        StopHoverTimer();
        if (stylesOpen) CloseStyles();
        RequestRedraw();
        if (onListClosed) onListClosed();
    };

    stylesModel = std::make_shared<FontPickerStylesModel>(this);
    stylesDelegate = std::make_shared<FontPickerStylesDelegate>(this);
    stylesView = std::make_shared<UltraCanvasListView>(GetIdentifier() + "_styles", 0, 0, 220, 100);
    stylesView->SetModel(stylesModel);
    stylesView->SetDelegate(stylesDelegate);
    stylesView->SetShowHeader(false);
    stylesView->SetAcceptsFocus(false);
    stylesView->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    stylesView->onItemClicked = [this](int row) { ChooseStyle(row); };
    stylesView->onItemActivated = [this](int row) { ChooseStyle(row); };
    stylesView->onKeyDown = [this](const UCEvent& event) { return HandleStylesKey(event); };
    stylesView->onPopupClosed = [this](ClosePopupReason reason) {
        stylesOpen = false;
        stylesRow = -1;
        // A click outside both lists closes them both, as a menu and its submenu.
        if (reason == ClosePopupReason::ClickOutside && listOpen) CloseList();
    };
    if (window) {
        listView->SetWindow(window);
        stylesView->SetWindow(window);
    }
    StyleChanged();
}

void UltraCanvasFontPicker::SetWindow(UltraCanvasWindowBase* win) {
    UltraCanvasUIElement::SetWindow(win);
    if (listView) listView->SetWindow(win);
    if (stylesView) stylesView->SetWindow(win);
}

int UltraCanvasFontPicker::VisibleRowCount() const {
    if (!listView) return 1;
    return std::max(1, static_cast<int>(listView->GetHeight() / std::max(1.0f, style.rowHeight)));
}

void UltraCanvasFontPicker::PlaceList() {
    const float width = std::max(style.listWidth, GetWidth());
    const int count = static_cast<int>(rows.size());
    const int visible = std::min(count, std::max(1, style.maxVisibleRows));
    float height = visible * style.rowHeight + style.borderWidth * 2;
    const Point2Df at = GetPositionInWindow();
    const float windowWidth = window ? static_cast<float>(window->GetWidth()) : 9999.0f;
    const float windowHeight = window ? static_cast<float>(window->GetHeight()) : 9999.0f;
    const float below = windowHeight - (at.y + GetHeight());
    const float above = at.y;
    float y = at.y + GetHeight();
    if (height > below) {
        if (above > below && height > below) {
            height = std::min(height, above);
            y = at.y - height;
        } else {
            height = std::max(style.rowHeight + 2, below);
        }
    }
    float x = at.x;
    if (x + width > windowWidth) x = std::max(0.0f, windowWidth - width);
    listView->SetElementSize(Size2Df(width, height));
    listPosition = Point2Di(static_cast<int>(x), static_cast<int>(y));
}

void UltraCanvasFontPicker::OpenList() {
    if (listOpen || isPopup || !window) return;
    if (onListOpening) onListOpening();
    CreateListViews();
    BuildRows();
    typed.clear();
    PlaceList();
    PopupElementSettings settings;
    settings.popupOwner = shared_from_this();
    // Escape: the window closes the popup on top, one per press - the
    // styles list first, then this one (onPopupClosed keeps up).
    settings.closeByEscapeKey = true;
    settings.closeByClickOutside = true;
    listOpen = true;
    window->OpenPopup(listPosition, *listView, settings);
    // The current font: in the top sections where it is there, else in
    // "All fonts", scrolled to.
    int at = -1;
    if (!mixed) {
        for (Section section : {Section::Top, Section::Document, Section::Recent, Section::AllFonts}) {
            at = FindRow(currentFamily, section);
            if (at >= 0) break;
        }
    }
    if (at >= 0) {
        listView->SetFocusedRow(at);
        listView->EnsureRowVisible(at);
    } else if (auto* selection = listView->GetSelection()) {
        selection->Clear();
    }
    QueuePreviews();
    RequestRedraw();
    if (onListOpened) onListOpened();
}

void UltraCanvasFontPicker::CloseList() {
    if (stylesOpen) CloseStyles();
    if (window && listView && listOpen) window->ClosePopup(*listView);
    listOpen = false;
}

int UltraCanvasFontPicker::GetHighlightedRow() const {
    if (!listView) return -1;
    auto* selection = listView->GetSelection();
    return selection ? selection->GetCurrentRow() : -1;
}

void UltraCanvasFontPicker::HighlightRow(int row) {
    if (!listView || row < 0 || row >= static_cast<int>(rows.size())) return;
    listView->SetFocusedRow(row);
    listView->EnsureRowVisible(row);
    listView->RequestRedraw();
}

void UltraCanvasFontPicker::MoveHighlight(int step) {
    const int count = static_cast<int>(rows.size());
    if (count == 0) return;
    int row = GetHighlightedRow();
    if (row < 0) row = step > 0 ? -1 : count;
    int next = std::clamp(row + step, 0, count - 1);
    // Over the captions, in the direction of travel (back the other way at an end).
    const int direction = step >= 0 ? 1 : -1;
    int probe = next;
    while (probe >= 0 && probe < count && !rows[static_cast<size_t>(probe)].selectable) probe += direction;
    if (probe < 0 || probe >= count) {
        probe = next;
        while (probe >= 0 && probe < count && !rows[static_cast<size_t>(probe)].selectable) probe -= direction;
    }
    if (probe >= 0 && probe < count) HighlightRow(probe);
}

void UltraCanvasFontPicker::RememberRecent(const std::string& family) {
    if (family.empty() || recentLimit <= 0) return;
    std::vector<std::string> list = {family};
    const std::string key = LowerText(family);
    for (const std::string& f : recentFonts)
        if (LowerText(f) != key && static_cast<int>(list.size()) < recentLimit) list.push_back(f);
    if (list == recentFonts) return;
    recentFonts = std::move(list);
    rowsDirty = true;
    if (onRecentFontsChanged) onRecentFontsChanged(recentFonts);
}

void UltraCanvasFontPicker::Chosen(const FontPickerChoice& choice) {
    SetCurrentFont(choice.family, choice.style);
    RememberRecent(choice.family);
    CloseList();
    if (onFontChosen) onFontChosen(choice);
}

bool UltraCanvasFontPicker::ChooseRow(int index) {
    const Row* row = GetRow(index);
    if (!row || !row->selectable) return false;
    FontPickerChoice choice;
    if (row->kind != RowKind::DefaultFont) choice.family = row->text;
    Chosen(choice);
    return true;
}

// ===== THE STYLES LIST =====

bool UltraCanvasFontPicker::OpenStyles(int row) {
    const InstalledFontFamily* family = FamilyOfRow(row);
    if (!family || !HasStylesList(*family) || !window || !listOpen) return false;
    if (stylesOpen && stylesRow == row) return true;
    if (stylesOpen) CloseStyles();
    CreateListViews();
    stylesShown = family->styles;
    styleLayouts.clear();
    styleLayouts.resize(stylesShown.size());
    stylesRow = row;
    stylesModel->Changed();

    // As wide as its widest style needs, beside the list at the row.
    float width = style.stylesWidth;
    if (IRenderContext* ctx = GetRenderContext()) {
        for (size_t i = 0; i < stylesShown.size(); ++i) {
            TextLayoutKey key;
            key.markup = true;
            key.text = FontStyleMarkup(family->name, stylesShown[i], stylesShown[i].name);
            key.font.fontFamily = family->name;
            key.font.fontSize = style.previewSize;
            if (ITextLayout* layout = styleLayouts[i].Get(ctx, key))
                width = std::max(width, static_cast<float>(layout->GetLayoutWidth()) + 24.0f);
        }
    }
    width = std::min(width, 420.0f);
    const int count = static_cast<int>(stylesShown.size());
    const int visible = std::min(count, std::max(1, style.maxVisibleRows));
    float height = visible * style.rowHeight + style.borderWidth * 2;
    const Rect2Di cell = listView->GetCellRect(row, 0);
    const float windowWidth = static_cast<float>(window->GetWidth());
    const float windowHeight = static_cast<float>(window->GetHeight());
    float x = listPosition.x + listView->GetWidth() - 2;
    if (x + width > windowWidth) x = std::max(0.0f, listPosition.x - width + 2);
    float y = static_cast<float>(listPosition.y + cell.y) - style.borderWidth;
    if (y + height > windowHeight) y = std::max(0.0f, windowHeight - height);
    stylesView->SetElementSize(Size2Df(width, height));

    PopupElementSettings settings;
    settings.popupOwner = listView;   // the pointer over the list stays the list's
    settings.closeByEscapeKey = true;    // Escape closes this list alone, the families' stays
    settings.closeByClickOutside = true;
    stylesOpen = true;
    window->OpenPopup(Point2Di(static_cast<int>(x), static_cast<int>(y)), *stylesView, settings);
    // The current style, else the regular one.
    int at = -1;
    std::string wanted = currentStyle;
    const InstalledFontStyle* face = nullptr;
    const InstalledFontFamily* current = FindInstalledFontFamilyOfName(GetFamilies(), currentFamily, &face);
    if (wanted.empty() && face) wanted = face->name;
    const bool sameFamily = !wanted.empty() && current == family;
    for (int i = 0; i < count && sameFamily && at < 0; ++i)
        if (LowerText(stylesShown[static_cast<size_t>(i)].name) == LowerText(wanted)) at = i;
    for (int i = 0; i < count && at < 0; ++i)
        if (stylesShown[static_cast<size_t>(i)].IsRegular()) at = i;
    at = std::max(0, at);
    stylesView->SetFocusedRow(at);
    stylesView->EnsureRowVisible(at);
    return true;
}

void UltraCanvasFontPicker::CloseStyles() {
    if (window && stylesView && stylesOpen) window->ClosePopup(*stylesView);
    stylesOpen = false;
    stylesRow = -1;
}

bool UltraCanvasFontPicker::ChooseStyle(int index) {
    const InstalledFontFamily* family = FamilyOfRow(stylesRow);
    if (!family || index < 0 || index >= static_cast<int>(stylesShown.size())) return false;
    const InstalledFontStyle& chosen = stylesShown[static_cast<size_t>(index)];
    FontPickerChoice choice;
    choice.family = family->name;
    choice.style = chosen.name;
    choice.weight = chosen.weight;
    choice.slant = chosen.slant;
    choice.stretch = chosen.stretch;
    if (chosen.faceFamilyReach != InstalledFontStyle::FaceFamilyReach::Unreachable) {
        choice.faceFamily = chosen.faceFamily;
        choice.faceFamilyBold = chosen.faceFamilyReach == InstalledFontStyle::FaceFamilyReach::WithBold;
    }
    choice.fromStyles = true;
    Chosen(choice);
    return true;
}

void UltraCanvasFontPicker::StopHoverTimer() {
    if (!hoverTimer) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(hoverTimer);
    hoverTimer = 0;
}

void UltraCanvasFontPicker::HoverChanged(int row) {
    hoverRow = row;
    StopHoverTimer();
    if (stylesOpen && row == stylesRow) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    // Resting on a row opens its styles (or closes another's): a pointer
    // passing over the list opens nothing.
    std::weak_ptr<UltraCanvasUIElement> self = weak_from_this();
    hoverTimer = app->StartTimer(kHoverOpenMs, false, [self](TimerId) {
        auto element = self.lock();
        if (!element) return;
        auto* picker = static_cast<UltraCanvasFontPicker*>(element.get());
        picker->hoverTimer = 0;
        if (!picker->listOpen) return;
        const InstalledFontFamily* family = picker->FamilyOfRow(picker->hoverRow);
        if (family && picker->HasStylesList(*family)) picker->OpenStyles(picker->hoverRow);
        else if (picker->stylesOpen) picker->CloseStyles();
    });
}

// ===== THE KEYS =====

bool UltraCanvasFontPicker::TypeAhead(const std::string& text) {
    if (text.empty()) return false;
    if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
    const auto now = std::chrono::steady_clock::now();
    const bool fresh = typed.empty() || now - typedAt > std::chrono::seconds(1);
    if (text == " " && fresh) return false;
    if (fresh) typed.clear();
    typedAt = now;
    const std::string add = LowerText(text);
    const bool repeat = typed == add;
    if (!repeat) typed += add;
    // In "All fonts": from the row after the highlight for a fresh letter or
    // one repeated, from the highlight itself as a name grows.
    int first = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].section == Section::AllFonts && rows[i].selectable) { first = static_cast<int>(i); break; }
    if (first < 0) return true;
    const int count = static_cast<int>(rows.size()) - first;
    const int current = GetHighlightedRow();
    int start = current >= first ? current - first : 0;
    if (repeat || typed.size() == add.size()) start = current >= first ? start + 1 : 0;
    for (int n = 0; n < count; ++n) {
        const int row = first + (start + n) % count;
        if (LowerText(rows[static_cast<size_t>(row)].text).compare(0, typed.size(), typed) == 0) {
            if (stylesOpen) CloseStyles();
            HighlightRow(row);
            return true;
        }
    }
    return true;   // taken; nothing starts with it
}

bool UltraCanvasFontPicker::HandleListKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || !listOpen) return false;
    switch (event.virtualKey) {
        case UCKeys::Up:       MoveHighlight(-1); return true;
        case UCKeys::Down:     MoveHighlight(1); return true;
        case UCKeys::PageUp:   MoveHighlight(-VisibleRowCount()); return true;
        case UCKeys::PageDown: MoveHighlight(VisibleRowCount()); return true;
        case UCKeys::Home:     MoveHighlight(-static_cast<int>(rows.size())); return true;
        case UCKeys::End:      MoveHighlight(static_cast<int>(rows.size())); return true;
        case UCKeys::Return:   ChooseRow(GetHighlightedRow()); return true;
        case UCKeys::Right:
            OpenStyles(GetHighlightedRow());
            return true;
        case UCKeys::Left:
            if (stylesOpen) CloseStyles();
            return true;
        case UCKeys::Backspace:
            if (!typed.empty()) typed.pop_back();
            return true;
        default:
            break;
    }
    if (event.ctrl || event.alt || event.meta) return false;
    std::string text = event.text;
    if (text.empty() && static_cast<unsigned char>(event.character) >= 32 && event.character != 127)
        text = std::string(1, event.character);
    return TypeAhead(text);
}

bool UltraCanvasFontPicker::HandleStylesKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || !stylesOpen || !stylesView) return false;
    switch (event.virtualKey) {
        case UCKeys::Left:
            CloseStyles();
            return true;
        case UCKeys::Return: {
            auto* selection = stylesView->GetSelection();
            ChooseStyle(selection ? selection->GetCurrentRow() : -1);
            return true;
        }
        case UCKeys::Up:
        case UCKeys::Down:
        case UCKeys::PageUp:
        case UCKeys::PageDown:
        case UCKeys::Home:
        case UCKeys::End:
            return false;   // the styles list moves itself
        default:
            break;
    }
    // A letter goes back to the families.
    if (event.ctrl || event.alt || event.meta) return false;
    std::string text = event.text;
    if (text.empty() && static_cast<unsigned char>(event.character) > 32 && event.character != 127)
        text = std::string(1, event.character);
    if (text.empty()) return true;
    return TypeAhead(text);
}

bool UltraCanvasFontPicker::OnEvent(const UCEvent& event) {
    if (IsDisabled()) {
        if (listOpen) CloseList();
        return false;
    }
    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::MouseDoubleClick:
            if (!Rect2Di(GetLocalBounds()).Contains(event.pointer)) return false;
            buttonPressed = true;
            if (acceptsFocus) SetFocus(true);
            if (listOpen) CloseList();
            else OpenList();
            return true;
        case UCEventType::MouseUp:
            buttonPressed = false;
            return false;
        case UCEventType::MouseMove:
            SetMouseCursor(Rect2Di(GetLocalBounds()).Contains(event.pointer) ? UCMouseCursor::ContextMenu
                                                                              : UCMouseCursor::Default);
            return false;
        case UCEventType::KeyDown:
            if (stylesOpen) return HandleStylesKey(event) || (stylesView && stylesView->OnEvent(event));
            if (listOpen) return HandleListKey(event);
            if (event.virtualKey == UCKeys::Down || event.virtualKey == UCKeys::Space ||
                (event.virtualKey == UCKeys::Return && event.alt)) {
                OpenList();
                return true;
            }
            return false;
        case UCEventType::FocusLost:
            return false;
        default:
            return false;
    }
}

// ===== THE PICTURES =====

ITextLayout* UltraCanvasFontPicker::NameLayout(IRenderContext* ctx, const std::string& family, float size) {
    TextLayoutKey key;
    key.text = family;
    key.font.fontFamily = family;
    key.font.fontSize = size;
    key.ellipsize = EllipsizeMode::EllipsizeEnd;
    key.width = std::max(40.0f, std::max(style.listWidth, GetWidth()) - kTextLeft - kArrowRoom - 6);
    return nameLayouts[family].Get(ctx, key);
}

ITextLayout* UltraCanvasFontPicker::PlainLayout(IRenderContext* ctx, const std::string& text, bool italic, float size,
                                                const std::string& fontFamily) {
    TextLayoutKey key;
    key.text = text;
    key.font.fontFamily = fontFamily.empty() ? style.fontFamily : fontFamily;
    key.font.fontSize = size;
    key.font.fontSlant = italic ? FontSlant::Italic : FontSlant::Normal;
    key.ellipsize = EllipsizeMode::EllipsizeEnd;
    key.width = std::max(40.0f, std::max(style.listWidth, GetWidth()) - kTextLeft - kArrowRoom - 6);
    const std::string slot = (italic ? "i|" : "n|") + key.font.fontFamily + "|" +
                             std::to_string(static_cast<int>(std::lround(size * 10))) + "|" + text;
    return plainLayouts[slot].Get(ctx, key);
}

void UltraCanvasFontPicker::StopPreviewTimer() {
    if (!previewTimer) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(previewTimer);
    previewTimer = 0;
}

void UltraCanvasFontPicker::QueuePreviews() {
    previewsDirty = false;
    if (rowsDirty) BuildRows();
    previewsDirty = false;
    previewQueue.clear();
    previewNext = 0;
    const int count = static_cast<int>(rows.size());
    std::vector<bool> queued(static_cast<size_t>(count), false);
    // The rows the list opens on, then those around the highlight, then the rest.
    const int reach = std::max(1, style.maxVisibleRows);
    auto add = [&](int row) {
        if (row < 0 || row >= count || queued[static_cast<size_t>(row)]) return;
        queued[static_cast<size_t>(row)] = true;
        if (rows[static_cast<size_t>(row)].familyIndex >= 0) previewQueue.push_back(row);
    };
    for (int row = 0; row < std::min(count, reach); ++row) add(row);
    const int around = std::max(0, GetHighlightedRow());
    for (int row = around - reach; row <= around + reach; ++row) add(row);
    for (int row = 0; row < count; ++row) add(row);
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app || previewQueue.empty()) return;
    StopPreviewTimer();
    std::weak_ptr<UltraCanvasUIElement> self = weak_from_this();
    // Every 15 ms a slice of about 6 ms: the UI stays responsive meanwhile.
    previewTimer = app->StartTimer(15, true, [self](TimerId) {
        auto element = self.lock();
        if (!element) return;
        static_cast<UltraCanvasFontPicker*>(element.get())->PrepareNextPreviews();
    });
}

void UltraCanvasFontPicker::PrepareNextPreviews() {
    IRenderContext* ctx = GetRenderContext();
    if (!ctx) return;   // not on a window yet: the next tick
    const auto start = std::chrono::steady_clock::now();
    while (previewNext < previewQueue.size()) {
        const int row = previewQueue[previewNext++];
        if (const InstalledFontFamily* family = FamilyOfRow(row)) {
            ITextLayout* layout = family->symbol ? PlainLayout(ctx, family->sample, false, style.previewSize, family->name)
                                                 : NameLayout(ctx, rows[static_cast<size_t>(row)].text, style.previewSize);
            if (layout) (void)layout->GetLayoutHeight();   // measuring shapes it: the font is loaded now
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(6)) return;
    }
    StopPreviewTimer();
}

// ===== DRAWING =====

void UltraCanvasFontPicker::RenderFormatIcon(IRenderContext* ctx, FontFileFormat format, const Rect2Di& rect) {
    const std::string path = FontFormatIconPath(format);
    if (path.empty()) return;
    auto icon = UCImage::Get(path);
    if (icon && icon->IsValid()) ctx->DrawImage(*icon, Rect2Dd(rect), ImageFitMode::Contain);
}

void UltraCanvasFontPicker::RenderArrow(IRenderContext* ctx, const Rect2Di& rect, bool down, const Color& colour) {
    const float s = style.arrowSize;
    const float cx = rect.x + rect.width / 2.0f, cy = rect.y + rect.height / 2.0f;
    ctx->SetStrokePaint(colour);
    ctx->SetStrokeWidth(1.0f);
    if (down) {
        ctx->DrawLine({cx - s / 2, cy - s / 4}, {cx, cy + s / 4});
        ctx->DrawLine({cx, cy + s / 4}, {cx + s / 2, cy - s / 4});
    } else {
        ctx->DrawLine({cx - s / 4, cy - s / 2}, {cx + s / 4, cy});
        ctx->DrawLine({cx + s / 4, cy}, {cx - s / 4, cy + s / 2});
    }
}

void UltraCanvasFontPicker::RenderRow(IRenderContext* ctx, int index, const Rect2Di& rect, bool highlighted) {
    const Row* row = GetRow(index);
    if (!row) return;
    (void)highlighted;
    auto drawCentred = [&](ITextLayout* layout, int x, const Color& colour) -> int {
        if (!layout) return x;
        ctx->SetTextPaint(colour);
        const int y = rect.y + (rect.height - static_cast<int>(std::lround(layout->GetLayoutHeight()))) / 2;
        ctx->DrawTextLayout(*layout, Point2Dd(x, y));
        return x + static_cast<int>(std::ceil(layout->GetLayoutWidth()));
    };
    if (row->kind == RowKind::Caption) {
        // Not a choice: the hover's and the selection's fill are covered,
        // and a rule above every caption but the first divides the sections.
        ctx->SetFillPaint(style.listBackgroundColor);
        ctx->FillRectangle(Rect2Dd(rect));
        if (index > 0) {
            ctx->SetStrokePaint(style.dividerColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine({static_cast<float>(rect.x + 4), rect.y + 0.5f},
                          {static_cast<float>(rect.x + rect.width - 4), rect.y + 0.5f});
        }
        drawCentred(PlainLayout(ctx, row->text, false, std::max(7.0f, style.fontSize - 1.0f)), rect.x + kIconLeft,
                    style.faintTextColor);
        return;
    }
    if (row->kind == RowKind::DefaultFont) {
        drawCentred(PlainLayout(ctx, row->text, false, style.previewSize), rect.x + kTextLeft, style.textColor);
        return;
    }
    if (row->kind == RowKind::Missing) {
        drawCentred(PlainLayout(ctx, MissingRowText(row->text), true, style.fontSize + 2.0f), rect.x + kTextLeft,
                    style.faintTextColor);
        return;
    }
    const InstalledFontFamily* family = FamilyOfRow(index);
    if (!family) return;
    const int iconSize = static_cast<int>(style.iconSize);
    RenderFormatIcon(ctx, family->format,
                     Rect2Di(rect.x + kIconLeft, rect.y + (rect.height - iconSize) / 2, iconSize, iconSize));
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(rect.x, rect.y, rect.width - kArrowRoom, rect.height));
    if (family->symbol) {
        // The name in the interface's font, then the font's own characters.
        const int after = drawCentred(PlainLayout(ctx, family->name, false, style.fontSize + 1.0f), rect.x + kTextLeft,
                                      style.textColor);
        if (!family->sample.empty())
            drawCentred(PlainLayout(ctx, family->sample, false, style.previewSize, family->name), after + 10,
                        style.textColor);
    } else {
        drawCentred(NameLayout(ctx, row->text, style.previewSize), rect.x + kTextLeft, style.textColor);
    }
    ctx->PopState();
    if (HasStylesList(*family))
        RenderArrow(ctx, Rect2Di(rect.x + rect.width - kArrowRoom, rect.y, kArrowRoom, rect.height), false,
                    style.faintTextColor);
}

void UltraCanvasFontPicker::RenderStyleRow(IRenderContext* ctx, int index, const Rect2Di& rect) {
    const InstalledFontFamily* family = FamilyOfRow(stylesRow);
    if (!family || index < 0 || index >= static_cast<int>(stylesShown.size())) return;
    if (styleLayouts.size() < stylesShown.size()) styleLayouts.resize(stylesShown.size());
    const InstalledFontStyle& s = stylesShown[static_cast<size_t>(index)];
    TextLayoutKey key;
    key.markup = true;
    key.text = FontStyleMarkup(family->symbol ? style.fontFamily : family->name, s, s.name);
    key.font.fontFamily = family->symbol ? style.fontFamily : family->name;
    key.font.fontSize = style.previewSize;
    if (ITextLayout* layout = styleLayouts[static_cast<size_t>(index)].Get(ctx, key)) {
        ctx->SetTextPaint(style.textColor);
        const int y = rect.y + (rect.height - static_cast<int>(std::lround(layout->GetLayoutHeight()))) / 2;
        ctx->DrawTextLayout(*layout, Point2Dd(rect.x + 12, y));
    }
}

void UltraCanvasFontPicker::RenderBox(IRenderContext* ctx) {
    const Rect2Di box = GetLocalBounds();
    Color background = style.normalColor;
    Color ink = style.textColor;
    if (IsDisabled()) {
        background = style.disabledColor;
        ink = style.faintTextColor;
    } else if (buttonPressed || listOpen) {
        background = style.pressedColor;
    } else if (IsHovered()) {
        background = style.hoverColor;
    }
    const bool focused = IsFocused() && !listOpen;
    ctx->DrawFilledRectangle(box, background, style.borderWidth, focused ? style.focusBorderColor : style.borderColor);
    const int arrowSpace = static_cast<int>(style.arrowSize * 2 + 2);
    if (!mixed) {
        // The current family in its own font; a missing one in grey
        // italics, the default font and a symbol font in the interface's.
        TextLayoutKey key;
        key.font.fontSize = style.fontSize + 1.0f;
        key.ellipsize = EllipsizeMode::EllipsizeEnd;
        key.width = std::max(10, box.width - 8 - arrowSpace);
        Color colour = ink;
        if (currentFamily.empty()) {
            key.text = defaultLabel;
            key.font.fontFamily = style.fontFamily;
        } else {
            key.text = currentFamily;
            const InstalledFontFamily* family = FindInstalledFontFamilyOfName(GetFamilies(), currentFamily);
            if (!family) {
                key.font.fontFamily = style.fontFamily;
                key.font.fontSlant = FontSlant::Italic;
                colour = style.faintTextColor;
            } else {
                key.font.fontFamily = family->symbol ? style.fontFamily : currentFamily;
            }
        }
        if (!key.text.empty()) {
            if (ITextLayout* layout = boxLayout.Get(ctx, key)) {
                ctx->PushState();
                ctx->ClipRect(Rect2Dd(box.x + 1, box.y, box.width - arrowSpace - 1, box.height));
                ctx->SetTextPaint(colour);
                const int y = box.y + (box.height - static_cast<int>(std::lround(layout->GetLayoutHeight()))) / 2;
                ctx->DrawTextLayout(*layout, Point2Dd(box.x + 6, y));
                ctx->PopState();
            }
        }
    }
    RenderArrow(ctx, Rect2Di(box.x + box.width - arrowSpace, box.y, arrowSpace, box.height), true, ink);
    if (focused) {
        ctx->DrawFilledRectangle(Rect2Di(box.x + 1, box.y + 1, box.width - 2, box.height - 2), Colors::Transparent, 1.0,
                                 style.focusBorderColor);
    }
}

void UltraCanvasFontPicker::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    RenderBox(ctx);
    // On screen: the pictures of the list are prepared while idle, so it
    // opens at once.
    if (previewsDirty) QueuePreviews();
}

} // namespace UltraCanvas

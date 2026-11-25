// core/UltraCanvasSpreadsheetChartGallery.cpp
// The chart gallery (ShowSpreadsheetChartGallery, UltraCanvasSpreadsheetChart.h):
// every chart type by category, each a picture of the selection's data drawn
// by the chart itself - or of a small sample where the data does not fit the
// type, faded, with the reason under it - its name and description.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasRichDocumentChart.h"
#include "UltraCanvasGalleryPicker.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasChipGroup.h"
#include "UltraCanvasLabel.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraCanvas {

namespace {

constexpr int kDialogWidth = 880;
constexpr int kDialogHeight = 660;
constexpr int kTileWidth = 156;
constexpr int kTileHeight = 128;
constexpr int kPictureHeight = 96;
// The pictures are laid out twice the size and drawn scaled down: a chart
// keeps the proportions of its text and plot.
constexpr int kPictureScale = 2;

// The type's look over sample data (a column chart's stacking, a scatter's
// lines).
RichChart SampleOfType(const SpreadsheetChartType& type) {
    RichChart chart = SampleSpreadsheetChartData(type.kind);
    chart.stacked = type.stacked;
    chart.percent = type.percent;
    chart.filled = type.filled;
    if (type.kind == RichChart::Kind::Scatter) {
        for (RichChartSeries& one : chart.series) one.joined = type.lines;
    }
    return chart;
}

} // namespace

void ShowSpreadsheetChartGallery(const SpreadsheetSheet& sheet, const CellRange& data, int current,
                                 std::function<void(size_t type)> onChosen, UltraCanvasWindowBase* parent) {
    const auto& types = SpreadsheetChartTypes();
    std::vector<GalleryPickerItem> items;
    std::vector<std::string> descriptions;
    std::vector<std::string> sections;
    for (size_t i = 0; i < types.size(); ++i) {
        const SpreadsheetChart made = MakeSpreadsheetChartOfType(sheet, data, i);
        const std::string problem = SpreadsheetChartFitMessage(made);
        RichChart picture = problem.empty() ? made.chart : SampleOfType(types[i]);
        picture.title.clear();
        picture.legend = false;
        GalleryPickerItem item;
        item.title = SpreadsheetChartTypeName(i);
        item.section = SpreadsheetChartCategoryName(types[i].category);
        item.picture = CreateRichChartElement(picture, (kTileWidth - 12) * kPictureScale, kPictureHeight * kPictureScale);
        if (!item.picture && problem.empty()) {
            // The selection draws nothing for this kind: its sample does.
            item.picture = CreateRichChartElement(SampleOfType(types[i]), (kTileWidth - 12) * kPictureScale,
                                                  kPictureHeight * kPictureScale);
            item.faded = true;
        }
        item.faded = item.faded || !problem.empty();
        item.description = SpreadsheetChartTypeDescription(i);
        if (!problem.empty()) item.description += "\n" + problem;
        descriptions.push_back(item.description);
        if (sections.empty() || sections.back() != item.section) sections.push_back(item.section);
        items.push_back(std::move(item));
    }

    DialogConfig config;
    config.title = Tr("Chart type");
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::OKCancel;
    config.defaultButton = DialogButton::OK;
    config.width = kDialogWidth;
    config.height = kDialogHeight;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetIconVisible(false);
    dialog->SetMessageVisible(false);

    const int contentWidth = kDialogWidth - 48;
    const int viewHeight = kDialogHeight - 250;
    auto content = std::make_shared<UltraCanvasContainer>("ChartGalleryContent");
    content->layout.SetFlex(CSSLayout::FlexDirection::Column);
    content->layout.SetFlexGap(8.f);
    content->size.width = CSSLayout::Dimension::Px(static_cast<float>(contentWidth));
    content->size.height = CSSLayout::Dimension::Px(static_cast<float>(viewHeight + 72 + 52 + 16));

    // The categories: a click shows the category's types.
    auto categories = std::make_shared<UltraCanvasChipGroup>("ChartGalleryCategories", static_cast<float>(contentWidth), 72.f);
    categories->size.height = CSSLayout::Dimension::Px(72.f);
    categories->layoutItem.SetFlexShrink(0);
    for (const std::string& section : sections) categories->AddChip(section);
    content->AddChild(categories);

    auto view = CreateScrollableContainer("ChartGalleryView", 0, 0, contentWidth, viewHeight);
    view->size.height = CSSLayout::Dimension::Px(static_cast<float>(viewHeight));
    view->layoutItem.SetFlexShrink(0);
    const int galleryWidth = contentWidth - 18;
    auto gallery = std::make_shared<UltraCanvasGalleryPicker>("ChartGallery", 0.f, 0.f, static_cast<float>(galleryWidth),
                                                               static_cast<float>(viewHeight));
    gallery->SetTileSize(kTileWidth, kTileHeight, kPictureHeight);
    gallery->SetItems(items);
    const float galleryHeight = static_cast<float>(gallery->ContentHeightFor(galleryWidth));
    gallery->SetBounds(Rect2Df(0.f, 0.f, static_cast<float>(galleryWidth), galleryHeight));
    gallery->size.width = CSSLayout::Dimension::Px(static_cast<float>(galleryWidth));
    gallery->size.height = CSSLayout::Dimension::Px(galleryHeight);
    view->AddChild(gallery);
    content->AddChild(view);

    auto description = std::make_shared<UltraCanvasLabel>("ChartGalleryDescription", -1, -1, -1, 52, "");
    description->SetWrap(TextWrap::WrapWord);
    description->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    description->size.height = CSSLayout::Dimension::Px(52.f);
    description->layoutItem.SetFlexShrink(0);
    content->AddChild(description);
    dialog->AddDialogElement(content);

    // What the controls hold, read by the result.
    struct Picked {
        int type = -1;
    };
    auto picked = std::make_shared<Picked>();
    UltraCanvasLabel* text = description.get();
    UltraCanvasGalleryPicker* tiles = gallery.get();
    UltraCanvasModalDialog* box = dialog.get();
    auto describe = [text, picked, descriptions](int index) {
        picked->type = index;
        text->SetText(index >= 0 && index < static_cast<int>(descriptions.size()) ? descriptions[static_cast<size_t>(index)]
                                                                                   : std::string());
    };
    gallery->onSelectionChanged = describe;
    gallery->onItemActivated = [describe, box](int index) {
        describe(index);
        box->ActivateButton(DialogButton::OK);
    };
    categories->onSelectionChanged = [tiles, sections](int index) {
        if (index >= 0 && index < static_cast<int>(sections.size())) tiles->ScrollToSection(sections[static_cast<size_t>(index)]);
    };
    if (current >= 0 && current < static_cast<int>(items.size())) {
        gallery->SetSelectedIndex(current);
        describe(current);
    }

    UltraCanvasDialogManager::ShowDialog(dialog, [picked, onChosen](DialogResult result) {
        if (result != DialogResult::OK || picked->type < 0 || !onChosen) return;
        onChosen(static_cast<size_t>(picked->type));
    }, parent);
    gallery->SetFocus(true);
    if (current >= 0) gallery->ScrollIntoView(current);
}

} // namespace UltraCanvas

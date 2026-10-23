// Apps/DemoApp/UltraCanvasBubbleChartExamples.cpp
// Bubble chart showcase: the three 2D bubble chart families (bubble matrix,
// scatter bubbles with a 4th colour dimension, packed bubbles) plus an
// OpenGL-rendered 3D bubble chart. The matrix and scatter tabs recreate two
// classic reference charts: the CBS News "Mrs. President" survey graphic and
// the "Concerns when traveling" cost/safety/health/food chart.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): a page title
//                  and description, the tabs in a card that fills the area,
//                  every tab a flex page whose notes wrap beside or under the chart
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasBubbleChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasGLDemoSupport.h"
#ifdef ULTRACANVAS_ENABLE_GL
#include "UltraCanvasGLSurface.h"
#endif

#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <random>

namespace UltraCanvas {

namespace {

// A tab's page: the chart's title (and description) over a row holding the
// chart and its notes. The notes sit beside the chart while both fit and move
// under it when not. The tab gives the page its exact size; content taller
// than that scrolls.
struct TabPage {
    std::shared_ptr<UltraCanvasContainer> tab;
    std::shared_ptr<UltraCanvasContainer> content;   // the chart + notes row
};

TabPage MakeTabPage(const std::string& id, const std::string& titleId,
                    const std::string& title, float titleSize) {
    TabPage page;
    page.tab = std::make_shared<UltraCanvasContainer>(id);
    page.tab->SetPadding(10, 14, 10, 14);
    page.tab->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The chart title keeps the size of the reference chart it recreates.
    DemoKit::AddHeading(page.tab, titleId, title)->SetFontSize(titleSize);
    page.content = DemoKit::CreateRow(id + "Content", 30);
    page.content->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    return page;
}

// The chart's notes: a column that takes the row's spare width (at least
// 300 px, or it wraps under the chart).
std::shared_ptr<UltraCanvasLabel> AddInfo(const std::shared_ptr<UltraCanvasContainer>& content,
                                          const std::string& id, const std::string& text) {
    auto column = DemoKit::CreateColumn(id + "Column");
    DemoKit::Add(content, column, 1);
    column->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));
    return DemoKit::AddText(column, id, text);
}

// =============================================================================
// TAB 1: "MRS. PRESIDENT" — BUBBLE MATRIX (survey style)
// =============================================================================
// Recreation of the CBS News (June 2008) graphic: percentage of respondents
// who say it is likely that a woman will be president in their lifetime,
// split by age group and gender, with Yes/No bubble columns.

std::shared_ptr<UltraCanvasUIElement> BuildMatrixTab() {
    auto page = MakeTabPage("BubbleMatrixTab", "MrsTitle", "MRS. PRESIDENT", 18);
    DemoKit::AddCaption(page.tab, "MrsSubtitle",
                        "Percentage of respondents who say it is likely that "
                        "a woman will be president in their lifetime.")->SetFontSize(11);
    DemoKit::Add(page.tab, page.content);

    std::vector<std::string> rows = {"Under 45", "45-64", "65+", "Men", "Women"};
    std::vector<std::string> cols = {"Yes", "No"};
    auto matrix = CreateBubbleMatrixChart("mrsPresident", 0, 0, 440, 560, rows, cols);

    matrix->AddMatrixBubble("Under 45", "Yes", 79);
    matrix->AddMatrixBubble("Under 45", "No", 16);
    matrix->AddMatrixBubble("45-64", "Yes", 69);
    matrix->AddMatrixBubble("45-64", "No", 22);
    matrix->AddMatrixBubble("65+", "Yes", 44);
    matrix->AddMatrixBubble("65+", "No", 39);
    matrix->AddMatrixBubble("Men", "Yes", 65);
    matrix->AddMatrixBubble("Men", "No", 25);
    matrix->AddMatrixBubble("Women", "Yes", 72);
    matrix->AddMatrixBubble("Women", "No", 20);

    // Age rows near-black, Men purple, Women orange-red — as in the original.
    matrix->SetRowColorMap({
        {"Under 45", Color(28, 26, 27, 255)},
        {"45-64",    Color(28, 26, 27, 255)},
        {"65+",      Color(28, 26, 27, 255)},
        {"Men",      Color(150, 42, 141, 255)},
        {"Women",    Color(240, 94, 66, 255)}
    });

    matrix->SetValueLabelMode(BubbleValueLabelMode::Inside);
    matrix->SetNameLabelMode(BubbleNameLabelMode::Hidden);
    matrix->SetLabelFontSize(13);
    matrix->SetMinRadiusForInsideLabels(9.0f);
    matrix->SetMatrixHeaderStyle(13.0f, Color(40, 40, 40, 255), true);
    matrix->SetMatrixRowLabelStyle(13.0f, Color(50, 50, 50, 255));
    matrix->SetFootnote("Source: CBS News, June 2008");
    matrix->SetEnableTooltips(true);
    DemoKit::AddFixed(page.content, matrix);

    AddInfo(page.content, "MrsInfo",
        "Bubble Matrix (BubbleChartMode::BubbleMatrix)\n\n"
        "A categorical rows x columns grid where each cell holds a bubble whose "
        "AREA is proportional to its value — the classic infographic/survey "
        "presentation of a small table of percentages.\n\n"
        "API used here:\n"
        "  • CreateBubbleMatrixChart(id, x, y, w, h, rows, columns)\n"
        "  • AddMatrixBubble(row, column, value)\n"
        "  • SetRowColorMap() — colour per row (Men purple, Women orange)\n"
        "  • SetValueLabelMode(Inside) — values centred in the bubbles with "
        "automatic contrast (white on dark, dark on light)\n"
        "  • SetFootnote() — small source caption\n\n"
        "The size scale is zero-anchored and area-true in this mode, so the "
        "\"79\" bubble really covers ~5x the area of the \"16\" bubble. "
        "Hover any bubble for a tooltip.");

    return page.tab;
}

// =============================================================================
// TAB 2: "CONCERNS WHEN TRAVELING" — SCATTER BUBBLES (3rd + 4th dimension)
// =============================================================================
// Recreation of the classic travel-concerns chart: X = cost concern,
// Y = safety concern, bubble size = health concern, darkness = food concern.

std::shared_ptr<UltraCanvasUIElement> BuildScatterTab() {
    auto page = MakeTabPage("BubbleScatterTab", "TravelTitle", "Concerns when traveling", 17);
    DemoKit::Add(page.tab, page.content);

    auto chart = CreateBubbleChartElement("travelConcerns", 0, 0, 680, 560);

    //          cost  safety health food
    chart->AddBubble( 9,  79, 46.1, 82, "Mexico");
    chart->AddBubble(44,  78, 41.0, 76, "Egypt");
    chart->AddBubble(52,  46, 42.5, 71, "China");
    chart->AddBubble(60,  15, 10.5, 22, "France");
    chart->AddBubble(57,  11,  8.0, 16, "Australia");
    chart->AddBubble(52,   7,  5.1, 10, "Great Britain");

    chart->SetSizeScale(BubbleSizeScale::Area);
    chart->SetRadiusRange(8.0f, 34.0f);

    // "Darkness relates to Food concerns": pale blue -> dark warm gray.
    chart->SetColorMode(BubbleColorMode::ColormapByValue);
    chart->SetCustomColormap({Color(201, 218, 240, 255), Color(148, 160, 173, 255),
                              Color(128, 128, 128, 255)});

    chart->SetNameLabelMode(BubbleNameLabelMode::Auto);
    chart->SetNameLabelColor(Color(130, 150, 175, 255));
    chart->SetLabelFontSize(11.0f);
    chart->SetValueLabelMode(BubbleValueLabelMode::Hidden);

    chart->SetShowSizeLegend(true);
    chart->SetSizeLegendTitle("Health");
    chart->SetSizeLegendValues({46.1, 20.5, 5.1});
    chart->SetValueDecimals(1);
    chart->SetFootnote("Darkness relates to Food concerns");

    chart->SetGridColor(Color(225, 225, 225, 255));
    chart->SetPlotAreaColor(Color(255, 255, 255, 255));
    chart->SetEnableTooltips(true);

    // The axis titles around the chart: "Safety" left of it, "Cost" centred
    // under it.
    auto chartBlock = DemoKit::CreateColumn("TravelChartBlock", 4);
    DemoKit::Add(page.content, chartBlock);
    auto chartRow = DemoKit::CreateRow("TravelChartRow", 8, false);
    chartRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    DemoKit::Add(chartBlock, chartRow);

    auto yAxis = DemoKit::CreateText("TravelYAxis", "Safety");
    yAxis->SetTextColor(Color(80, 80, 80, 255));
    DemoKit::Add(chartRow, yAxis);
    DemoKit::AddFixed(chartRow, chart);

    auto xAxis = DemoKit::AddText(chartBlock, "TravelXAxis", "Cost");
    xAxis->SetTextColor(Color(80, 80, 80, 255));
    xAxis->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);

    AddInfo(page.content, "TravelInfo",
        "Scatter Bubbles (BubbleChartMode::ScatterBubbles)\n\n"
        "A scatter plot whose marks encode four dimensions:\n"
        "  • X position — cost concern\n"
        "  • Y position — safety concern\n"
        "  • bubble AREA — health concern (3rd dimension)\n"
        "  • darkness — food concern (4th dimension)\n\n"
        "API used here:\n"
        "  • AddBubble(x, y, size, colorValue, name)\n"
        "  • SetColorMode(ColormapByValue) + "
        "SetCustomColormap({pale blue, ..., dark gray})\n"
        "  • SetSizeScale(Area) — perceptually honest sizing\n"
        "  • SetShowSizeLegend(true) — the nested-circles "
        "\"Health 46.1 / 20.5 / 5.1\" legend\n"
        "  • SetNameLabelMode(Auto) — names go inside big "
        "bubbles (Mexico) and under small ones (France)\n"
        "  • SetFootnote() — the colour-meaning caption\n\n"
        "Any continuous HeatmapColormap (Viridis, Blues, "
        "Turbo, ...) can replace the custom gray ramp.");

    return page.tab;
}

// =============================================================================
// TAB 3: PACKED BUBBLES (Tableau-style bubble cloud)
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildPackedTab() {
    auto page = MakeTabPage("BubblePackedTab", "PackedTitle",
                            "Music Streaming Subscribers (millions) — Packed Bubble Chart", 16);
    DemoKit::Add(page.tab, page.content);

    auto packed = CreatePackedBubbleChart("streamingShare", 0, 0, 640, 600);

    struct Entry { const char* name; double value; };
    const std::vector<Entry> entries = {
        {"Spotify", 236}, {"Tencent Music", 106}, {"Apple Music", 93},
        {"Amazon Music", 82}, {"YouTube Music", 80}, {"NetEase", 44},
        {"Yandex Plus", 28}, {"Melon", 13}, {"Deezer", 10},
        {"iHeart", 9}, {"Tidal", 5}, {"Anghami", 4}
    };
    for (const auto& e : entries) {
        packed->AddBubble(0, 0, e.value, 0, e.name);
    }

    packed->SetRadiusRange(10.0f, 95.0f);
    packed->SetPackedSortOrder(PackedSortOrder::SizeDescending);
    packed->SetPackedPadding(3.0f);
    packed->SetColorMode(BubbleColorMode::Palette);
    packed->SetBubbleStyle(BubbleRenderStyle::Shaded);
    packed->SetNameLabelMode(BubbleNameLabelMode::Auto);
    packed->SetValueLabelMode(BubbleValueLabelMode::Auto);
    packed->SetLabelFontSize(11.0f);
    packed->SetEnableTooltips(true);
    DemoKit::AddFixed(page.content, packed);

    // ---- interactive controls ----
    // A column beside the chart: label + dropdown rows, the button, the notes.
    auto controls = DemoKit::CreateColumn("PackedControls", 12);
    DemoKit::Add(page.content, controls, 1);
    controls->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));
    auto addControlRow = [&](const std::string& labelId, const std::string& text,
                             const std::shared_ptr<UltraCanvasDropdown>& dropdown) {
        auto row = DemoKit::AddRow(controls, labelId + "Row", 10);
        row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(row, DemoKit::CreateText(labelId, text));
        DemoKit::AddFixed(row, dropdown);
        dropdown->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    };

    auto styleDrop = std::make_shared<UltraCanvasDropdown>("PackedStyleDD", 0, 0, 150, 30);
    styleDrop->AddItem("Shaded");
    styleDrop->AddItem("Flat");
    styleDrop->AddItem("Glossy");
    styleDrop->SetSelectedIndex(0);
    styleDrop->onSelectionChanged = [packed](int index, const DropdownItem&) {
        BubbleRenderStyle s = BubbleRenderStyle::Shaded;
        if (index == 1) s = BubbleRenderStyle::Flat;
        else if (index == 2) s = BubbleRenderStyle::Glossy;
        packed->SetBubbleStyle(s);
    };
    addControlRow("PackedStyleLbl", "Bubble style:", styleDrop);

    auto sortDrop = std::make_shared<UltraCanvasDropdown>("PackedSortDD", 0, 0, 150, 30);
    sortDrop->AddItem("Biggest first");
    sortDrop->AddItem("Smallest first");
    sortDrop->AddItem("Data order");
    sortDrop->SetSelectedIndex(0);
    sortDrop->onSelectionChanged = [packed](int index, const DropdownItem&) {
        PackedSortOrder o = PackedSortOrder::SizeDescending;
        if (index == 1) o = PackedSortOrder::SizeAscending;
        else if (index == 2) o = PackedSortOrder::DataOrder;
        packed->SetPackedSortOrder(o);
    };
    addControlRow("PackedSortLbl", "Placement:", sortDrop);

    auto colorDrop = std::make_shared<UltraCanvasDropdown>("PackedColorDD", 0, 0, 150, 30);
    colorDrop->AddItem("Palette");
    colorDrop->AddItem("Value (Viridis)");
    colorDrop->AddItem("Value (Blues)");
    colorDrop->SetSelectedIndex(0);
    colorDrop->onSelectionChanged = [packed](int index, const DropdownItem&) {
        if (index == 0) {
            packed->SetColorMode(BubbleColorMode::Palette);
        } else {
            packed->SetColorMode(BubbleColorMode::ColormapBySize);
            packed->SetColormap(index == 1 ? HeatmapColormap::Viridis
                                           : HeatmapColormap::Blues,
                                index == 2 /* light-to-dark for Blues */);
        }
    };
    addControlRow("PackedColorLbl", "Colour by:", colorDrop);

    // As wide as its text, which changes when it is clicked.
    auto encBtn = std::make_shared<UltraCanvasButton>("PackedEncBtn", 0, 0, 0, 30);
    encBtn->SetText("Show Enclosure Circle");
    encBtn->onClick = [encBtn = encBtn.get(), packed]() {
        static bool enclosed = false;
        enclosed = !enclosed;
        packed->SetPackedEnclosure(enclosed, Color(45, 55, 85, 255), 1.5f);
        encBtn->SetText(enclosed ? "Hide Enclosure Circle" : "Show Enclosure Circle");
    };
    DemoKit::Add(controls, encBtn);
    encBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);

    DemoKit::AddText(controls, "PackedInfo",
        "Packed Bubbles (BubbleChartMode::PackedBubbles)\n\n"
        "The Tableau-style \"bubble cloud\" / circle packing: "
        "position carries no meaning — relative size (and "
        "optionally colour) carries the whole message. Best for "
        "quick part-to-whole impressions across many "
        "categories, not for precise comparisons.\n\n"
        "The layout is a greedy tangent packing: circles are "
        "placed one at a time at the tightest spot touching two "
        "already-placed circles, then the whole cluster is "
        "scaled to fit the plot area.\n\n"
        "API used here:\n"
        "  • CreatePackedBubbleChart(...)\n"
        "  • SetPackedSortOrder() / SetPackedPadding()\n"
        "  • SetBubbleStyle(Flat | Shaded | Glossy)\n"
        "  • SetColorMode(Palette or ColormapBySize)\n"
        "  • SetPackedEnclosure() — outlined boundary circle, "
        "packing scaled to fill it (toggle above)\n"
        "  • Auto labels: names + values inside when they fit");

    return page.tab;
}

// =============================================================================
// TAB 4: HIERARCHICAL PACKED BUBBLES (nested circle packing)
// =============================================================================
// d3-style two-level circle packing: orbital launches by rocket family,
// grouped into labelled country/region parent circles.

std::shared_ptr<UltraCanvasUIElement> BuildHierarchicalTab() {
    auto page = MakeTabPage("BubbleHierTab", "HierTitle",
                            "Orbital Launches by Rocket Family — Hierarchical Packed Bubbles", 16);
    DemoKit::Add(page.tab, page.content);

    auto hier = CreateHierarchicalBubbleChart("launchFamilies", 0, 0, 720, 620);

    struct Entry { const char* group; const char* name; double value; };
    const std::vector<Entry> entries = {
        {"The United States", "Falcon",    96},
        {"The United States", "Electron",  10},
        {"The United States", "Atlas",      5},
        {"The United States", "Delta",      3},
        {"The United States", "Antares",    2},
        {"The United States", "Astra",      2},
        {"China",             "Long March", 48},
        {"China",             "Kuaizhou",    6},
        {"China",             "Ceres",       3},
        {"Russia",            "R-7",        19},
        {"Russia",            "Proton",      2},
        {"Russia",            "Angara",      1},
        {"Europe",            "Ariane",      5},
        {"Europe",            "Vega",        3},
        {"Japan",             "H-II",        5},
        {"Japan",             "Epsilon",     1},
        {"India",             "PSLV",        4},
        {"India",             "GSLV",        2},
        {"Iran",              "Qased",       2},
        {"Iran",              "Simorgh",     1},
    };
    for (const auto& e : entries) {
        hier->AddBubble(0, 0, e.value, 0, e.name, e.group);
    }

    // Saturated child colour per group; the parent circle is drawn in the
    // same colour lightened by the group tint.
    hier->SetCategoryColorMap({
        {"The United States", Color( 86, 146, 208, 255)},
        {"China",             Color(247, 148,  51, 255)},
        {"Russia",            Color( 63, 132, 130, 255)},
        {"Europe",            Color(226, 113,  29, 255)},
        {"Japan",             Color( 76, 140,  74, 255)},
        {"India",             Color(141, 127,  36, 255)},
        {"Iran",              Color(118, 183, 110, 255)},
    });
    hier->SetGroupTint(0.55f);
    hier->SetGroupPadding(6.0f);
    hier->SetGroupLabelStyle(11.0f, Colors::Transparent /* auto contrast */, true);

    hier->SetRadiusRange(7.0f, 64.0f);
    hier->SetNameLabelMode(BubbleNameLabelMode::Auto);
    hier->SetHideNamesBelowRadius(13.0f);   // tiny bubbles: tooltip only
    hier->SetValueLabelMode(BubbleValueLabelMode::Hidden);
    hier->SetLabelFontSize(10.0f);
    hier->SetEnableTooltips(true);
    DemoKit::AddFixed(page.content, hier);

    AddInfo(page.content, "HierInfo",
        "Hierarchical Packing\n"
        "(BubbleChartMode::HierarchicalPacked)\n\n"
        "Two-level circle packing: bubbles sharing a "
        "category are packed inside a parent circle, and "
        "the parent circles are packed against each other "
        "(the d3.pack() hierarchy look).\n\n"
        "  • Parent fill = group colour lightened by "
        "SetGroupTint(); children keep the saturated "
        "colour, so groups read at a glance\n"
        "  • The group name sits in the headroom ring "
        "between the children and the parent rim\n"
        "  • Parent size derives automatically from the "
        "packed children plus SetGroupPadding()\n\n"
        "API used here:\n"
        "  • CreateHierarchicalBubbleChart(...)\n"
        "  • AddBubble(0, 0, value, 0, name, group)\n"
        "  • SetCategoryColorMap() — colour per group\n"
        "  • SetGroupTint() / SetGroupPadding() / "
        "SetGroupLabelStyle()\n\n"
        "Hover any child bubble for name, group and value.");

    return page.tab;
}

// =============================================================================
// TAB 5: TIMELINE BUBBLES (named rows x time, size = value)
// =============================================================================
// Recreation of the Tableau "Which months had the highest number of orders?"
// chart: region rows, monthly X axis, bubble area = order count.

std::shared_ptr<UltraCanvasUIElement> BuildTimelineTab() {
    auto page = MakeTabPage("BubbleTimelineTab", "TlTitle",
                            "Which months had the highest number of orders?", 16);
    DemoKit::AddCaption(page.tab, "TlSubtitle", "Bubble size represents number of orders")
            ->SetFontSize(11);
    DemoKit::Add(page.tab, page.content);

    std::vector<std::string> regions = {
        "Africa", "Canada", "Caribbean", "Central Asia",
        "EMEA", "North Asia", "Oceania", "Southeast Asia"
    };
    auto timeline = CreateTimelineBubbleChart("monthlyOrders", 0, 0, 900, 600, regions);

    // Month index 0 = November 2011 ... 48 = November 2015.
    auto monthLabel = [](double v) {
        static const char* names[12] = {
            "January", "February", "March", "April", "May", "June", "July",
            "August", "September", "October", "November", "December"
        };
        int idx = static_cast<int>(std::lround(v));
        int m = (10 + idx) % 12;
        int year = 2011 + (10 + idx) / 12;
        return std::string(names[m]) + " " + std::to_string(year);
    };

    // Synthetic order counts: per-region base volume, a gentle growth trend
    // and seasonal noise (Canada intentionally tiny, like the original).
    std::mt19937 gen(2015);
    std::uniform_real_distribution<> noise(0.55, 1.45);
    const std::vector<double> baseVolume = {120, 8, 60, 55, 130, 90, 100, 95};
    for (size_t r = 0; r < regions.size(); ++r) {
        for (int m = 0; m <= 48; ++m) {
            double trend = 1.0 + m * 0.022;                      // slow growth
            double season = 1.0 + 0.25 * std::sin((m % 12) * 0.52);
            double orders = std::max(1.0, baseVolume[r] * trend * season * noise(gen));
            timeline->AddTimelineBubble(regions[r], m, std::round(orders));
        }
    }

    timeline->SetXAxisLabelFormatter(monthLabel);
    timeline->SetColorMode(BubbleColorMode::Uniform);
    timeline->SetUniformColor(Color(114, 158, 206, 255));
    timeline->SetBubbleAlpha(0.62f);
    timeline->SetRadiusRange(1.5f, 26.0f);
    timeline->SetSizeScale(BubbleSizeScale::Area);
    timeline->SetNameLabelMode(BubbleNameLabelMode::Hidden);
    timeline->SetValueLabelMode(BubbleValueLabelMode::Hidden);
    timeline->SetMatrixRowLabelStyle(11.0f, Color(100, 100, 100, 255));
    timeline->SetGridColor(Color(232, 232, 232, 255));
    timeline->SetEnableTooltips(true);

    // Tableau-style tooltip: "EMEA / 173 order(s) in June 2014".
    auto* timelinePtr = timeline.get();
    timeline->SetCustomTooltipGenerator(
        [timelinePtr, monthLabel](const ChartDataPoint&, size_t index) {
            const auto& b = timelinePtr->GetBubble(index);
            return b.category + "\n" +
                   std::to_string(static_cast<long>(b.size)) +
                   " order(s) in " + monthLabel(b.x);
        });
    DemoKit::AddFixed(page.content, timeline);

    AddInfo(page.content, "TlInfo",
        "Timeline Bubbles (BubbleChartMode::TimelineBubbles)\n\n"
        "Named categorical rows on the Y axis, a continuous "
        "(time) value on X, bubble AREA = the measure. Zero-anchored "
        "area scale, so a month with twice the orders "
        "shows twice the ink.\n\n"
        "API used here:\n"
        " • CreateTimelineBubbleChart(id, ..., rows)\n"
        " • AddTimelineBubble(row, monthIndex, orders)\n"
        " • SetXAxisLabelFormatter — month index to "
        "\"June 2014\" labels\n"
        " • SetCustomTooltipGenerator — Tableau-style order tooltips\n\n"
        "392 bubbles (8 regions x 49 months). Hover any "
        "bubble for its exact count.");

    return page.tab;
}

// =============================================================================
// TAB 6: 3D BUBBLE CHART — OpenGL surface
// =============================================================================

#ifdef ULTRACANVAS_ENABLE_GL

using namespace gldemo;

struct Bubble3D {
    float x, y, z;      // normalized positions in [-1, 1]
    float radius;       // world-space sphere radius
    Color color;
    std::string name;
};

struct Bubble3DState {
    std::vector<Bubble3D> bubbles;

    float yaw = 0.7f;
    float pitch = 0.42f;
    float distance = 4.4f;
    bool autoRotate = true;
    float spin = 0.0f;
};

struct Bubble3DGL {
    GLuint sphereProgram = 0;
    GLuint lineProgram = 0;
    GLuint sphereVao = 0, sphereVbo = 0, sphereEbo = 0;
    GLuint lineVao = 0, lineVbo = 0;
    GLsizei sphereIndexCount = 0;
    GLsizei lineVertexCount = 0;
    GLint uModel = -1, uView = -1, uProj = -1, uColor = -1, uLightDir = -1, uEye = -1;
    GLint uLineMvp = -1, uLineColor = -1;
    bool ready = false;
};

const char* kBubbleVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    gl_Position = uProj * uView * world;
}
)";

const char* kBubbleFS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform vec3 uEye;
void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uEye - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 40.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);
    vec3 base = uColor.rgb;
    vec3 color = base * (0.30 + 0.70 * diff) + vec3(1.0) * spec * 0.35 + base * rim * 0.25;
    FragColor = vec4(color, uColor.a);
}
)";

const char* kLineVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kLineFS = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; }
)";

// GLSurface subclass: drag orbits the camera, wheel zooms.
class Bubble3DSurface : public UltraCanvasGLSurface {
public:
    Bubble3DSurface(const GLSurfaceConfig& cfg, const std::string& id,
                    float x, float y, float w, float h,
                    std::shared_ptr<Bubble3DState> state)
        : UltraCanvasGLSurface(cfg, id, x, y, w, h), state_(std::move(state)) {}

    bool OnEvent(const UCEvent& event) override {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    dragging_ = true;
                    lastX_ = event.pointer.x;
                    lastY_ = event.pointer.y;
                    return true;
                }
                break;
            case UCEventType::MouseUp:
                if (event.button == UCMouseButton::Left) {
                    dragging_ = false;
                    return true;
                }
                break;
            case UCEventType::MouseMove:
                if (dragging_) {
                    state_->yaw += float(event.pointer.x - lastX_) * 0.01f;
                    state_->pitch += float(event.pointer.y - lastY_) * 0.01f;
                    const float lim = kPi * 0.49f;
                    state_->pitch = std::max(-lim, std::min(lim, state_->pitch));
                    lastX_ = event.pointer.x;
                    lastY_ = event.pointer.y;
                    RequestRender();
                    return true;
                }
                break;
            case UCEventType::MouseWheel: {
                float step = (event.wheelDelta > 0) ? 0.9f : 1.1f;
                state_->distance = std::max(2.2f, std::min(9.0f, state_->distance * step));
                RequestRender();
                return true;
            }
            default:
                break;
        }
        return UltraCanvasGLSurface::OnEvent(event);
    }

private:
    std::shared_ptr<Bubble3DState> state_;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
};

std::shared_ptr<UltraCanvasUIElement> Build3DTab() {
    auto page = MakeTabPage("Bubble3DTab", "B3DTitle",
                            "3D Bubble Chart — R&D portfolio: risk / reward / time-to-market, size = investment", 15);
    DemoKit::Add(page.tab, page.content);

    auto state = std::make_shared<Bubble3DState>();
    // x = risk, y = reward, z = time-to-market (normalized to [-1, 1]),
    // radius from investment volume; colour by business unit.
    auto add = [&](float x, float y, float z, float invest, Color c, const char* name) {
        // Sphere VOLUME proportional to investment: r = cbrt(v), scaled.
        float r = 0.16f * std::cbrt(invest);
        state->bubbles.push_back({x, y, z, r, c, name});
    };
    Color blue(78, 121, 167, 210), orange(242, 142, 43, 210),
          red(225, 87, 89, 210), teal(118, 183, 178, 210),
          green(89, 161, 79, 210), purple(176, 122, 161, 210);

    add(-0.70f,  0.15f, -0.55f, 1.9f, blue,   "Core platform");
    add(-0.35f, -0.40f,  0.10f, 1.1f, blue,   "Maintenance");
    add( 0.10f,  0.35f, -0.20f, 2.6f, orange, "New market entry");
    add( 0.45f,  0.75f,  0.55f, 3.2f, red,    "Moonshot AI");
    add( 0.75f,  0.55f,  0.80f, 1.4f, red,    "Quantum pilot");
    add(-0.10f,  0.65f, -0.70f, 2.2f, teal,   "Premium tier");
    add( 0.30f, -0.15f,  0.35f, 0.9f, green,  "Cost automation");
    add(-0.55f,  0.80f,  0.25f, 1.6f, purple, "Brand refresh");
    add( 0.60f,  0.05f, -0.35f, 0.7f, green,  "Tooling");
    add(-0.20f, -0.70f,  0.65f, 0.5f, purple, "Legacy sunset");

    auto glRes = std::make_shared<Bubble3DGL>();

    GLSurfaceConfig cfg;
    cfg.glVersionMajor = 3; cfg.glVersionMinor = 3; cfg.coreProfile = true;
    cfg.depthBits = 24; cfg.samples = 4;

    auto surface = std::make_shared<Bubble3DSurface>(cfg, "Bubble3DSurface",
                                                     0, 0, 760, 600, state);
    surface->SetRenderMode(RenderMode::Continuous);

    surface->SetInitCallback([glRes]() {
        glRes->sphereProgram = LinkProgram(kBubbleVS, kBubbleFS);
        glRes->lineProgram = LinkProgram(kLineVS, kLineFS);
        if (!glRes->sphereProgram || !glRes->lineProgram) return;

        glRes->uModel    = glGetUniformLocation(glRes->sphereProgram, "uModel");
        glRes->uView     = glGetUniformLocation(glRes->sphereProgram, "uView");
        glRes->uProj     = glGetUniformLocation(glRes->sphereProgram, "uProj");
        glRes->uColor    = glGetUniformLocation(glRes->sphereProgram, "uColor");
        glRes->uLightDir = glGetUniformLocation(glRes->sphereProgram, "uLightDir");
        glRes->uEye      = glGetUniformLocation(glRes->sphereProgram, "uEye");
        glRes->uLineMvp   = glGetUniformLocation(glRes->lineProgram, "uMvp");
        glRes->uLineColor = glGetUniformLocation(glRes->lineProgram, "uColor");

        // Unit sphere mesh shared by every bubble.
        Mesh sphere = MakeIcosphere(3);
        glGenVertexArrays(1, &glRes->sphereVao);
        glGenBuffers(1, &glRes->sphereVbo);
        glGenBuffers(1, &glRes->sphereEbo);
        glBindVertexArray(glRes->sphereVao);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->sphereVbo);
        glBufferData(GL_ARRAY_BUFFER, sphere.vertices.size() * sizeof(float),
                     sphere.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glRes->sphereEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphere.indices.size() * sizeof(uint32_t),
                     sphere.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        glRes->sphereIndexCount = (GLsizei)sphere.indices.size();

        // Axis box: the 12 edges of the [-1, 1] cube plus floor grid lines.
        std::vector<float> lines;
        auto edge = [&lines](float ax, float ay, float az, float bx, float by, float bz) {
            lines.insert(lines.end(), {ax, ay, az, bx, by, bz});
        };
        const float e = 1.15f;
        // bottom face
        edge(-e, -e, -e,  e, -e, -e); edge( e, -e, -e,  e, -e,  e);
        edge( e, -e,  e, -e, -e,  e); edge(-e, -e,  e, -e, -e, -e);
        // top face
        edge(-e,  e, -e,  e,  e, -e); edge( e,  e, -e,  e,  e,  e);
        edge( e,  e,  e, -e,  e,  e); edge(-e,  e,  e, -e,  e, -e);
        // verticals
        edge(-e, -e, -e, -e,  e, -e); edge( e, -e, -e,  e,  e, -e);
        edge( e, -e,  e,  e,  e,  e); edge(-e, -e,  e, -e,  e,  e);
        // floor grid
        for (int i = 1; i < 4; ++i) {
            float t = -e + i * (2.0f * e / 4.0f);
            edge(t, -e, -e, t, -e, e);
            edge(-e, -e, t, e, -e, t);
        }
        glGenVertexArrays(1, &glRes->lineVao);
        glGenBuffers(1, &glRes->lineVbo);
        glBindVertexArray(glRes->lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, glRes->lineVbo);
        glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float),
                     lines.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        glRes->lineVertexCount = (GLsizei)(lines.size() / 3);

        glEnable(GL_DEPTH_TEST);
        glRes->ready = true;
    });

    surface->SetRenderCallback([glRes, state](const RenderSurfaceInfo& info) {
        if (!glRes->ready) return;

        glClearColor(0.965f, 0.97f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (state->autoRotate) state->spin += float(info.deltaTime) * 0.25f;

        float aspect = info.height > 0 ? float(info.width) / float(info.height) : 1.0f;
        Mat4 proj = Perspective(40.0f * kPi / 180.0f, aspect, 0.05f, 100.0f);

        float yaw = state->yaw + state->spin;
        float cp = std::cos(state->pitch), sp = std::sin(state->pitch);
        Vec3 eye{state->distance * cp * std::sin(yaw),
                 state->distance * sp,
                 state->distance * cp * std::cos(yaw)};
        Mat4 view = LookAt(eye, {0, 0, 0}, {0, 1, 0});
        Mat4 viewProj = proj * view;

        // Axis box behind everything.
        glUseProgram(glRes->lineProgram);
        glUniformMatrix4fv(glRes->uLineMvp, 1, GL_FALSE, viewProj.m);
        glUniform4f(glRes->uLineColor, 0.62f, 0.65f, 0.70f, 1.0f);
        glBindVertexArray(glRes->lineVao);
        glDrawArrays(GL_LINES, 0, glRes->lineVertexCount);
        glBindVertexArray(0);

        // Semi-transparent spheres: sort back-to-front along the view ray.
        std::vector<size_t> order(state->bubbles.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        auto depthOf = [&](size_t i) {
            const auto& b = state->bubbles[i];
            // larger dot(eye - p, eye dir) = farther from the camera
            float dx = b.x - eye.x, dy = b.y - eye.y, dz = b.z - eye.z;
            return dx * dx + dy * dy + dz * dz;
        };
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return depthOf(a) > depthOf(b); });

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        // Cull back faces so the inside of a translucent sphere never shows
        // through as darker patches.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glUseProgram(glRes->sphereProgram);
        glUniformMatrix4fv(glRes->uView, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glRes->uProj, 1, GL_FALSE, proj.m);
        glUniform3f(glRes->uLightDir, 0.45f, 0.85f, 0.55f);
        glUniform3f(glRes->uEye, eye.x, eye.y, eye.z);
        glBindVertexArray(glRes->sphereVao);
        for (size_t i : order) {
            const auto& b = state->bubbles[i];
            Mat4 model = Translate(b.x, b.y, b.z) * Scale(b.radius, b.radius, b.radius);
            glUniformMatrix4fv(glRes->uModel, 1, GL_FALSE, model.m);
            glUniform4f(glRes->uColor, b.color.r / 255.0f, b.color.g / 255.0f,
                        b.color.b / 255.0f, b.color.a / 255.0f);
            glDrawElements(GL_TRIANGLES, glRes->sphereIndexCount, GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glUseProgram(0);
    });

    surface->SetCleanupCallback([glRes]() {
        if (glRes->sphereEbo) glDeleteBuffers(1, &glRes->sphereEbo);
        if (glRes->sphereVbo) glDeleteBuffers(1, &glRes->sphereVbo);
        if (glRes->sphereVao) glDeleteVertexArrays(1, &glRes->sphereVao);
        if (glRes->lineVbo) glDeleteBuffers(1, &glRes->lineVbo);
        if (glRes->lineVao) glDeleteVertexArrays(1, &glRes->lineVao);
        if (glRes->sphereProgram) glDeleteProgram(glRes->sphereProgram);
        if (glRes->lineProgram) glDeleteProgram(glRes->lineProgram);
        *glRes = Bubble3DGL{};
    });

    DemoKit::AddFixed(page.content, surface);

    // Beside the surface: the spin toggle (as wide as its text), then the notes.
    auto side = DemoKit::CreateColumn("B3DSide", 12);
    DemoKit::Add(page.content, side, 1);
    side->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));

    auto spinBtn = std::make_shared<UltraCanvasButton>("B3DSpinBtn", 0, 0, 0, 32);
    spinBtn->SetText("Pause Spin");
    spinBtn->onClick = [spinBtn = spinBtn.get(), state]() {
        state->autoRotate = !state->autoRotate;
        spinBtn->SetText(state->autoRotate ? "Pause Spin" : "Resume Spin");
    };
    DemoKit::Add(side, spinBtn);
    spinBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);

    DemoKit::AddText(side, "B3DInfo",
        "3D Bubble Chart on UltraCanvasGLSurface\n\n"
        "Each bubble is a real sphere in a 3-axis data "
        "space, rendered with OpenGL 3.3 through the "
        "framework's GL surface element:\n"
        "  • X — project risk\n"
        "  • Y — expected reward\n"
        "  • Z — time to market\n"
        "  • sphere VOLUME — investment\n"
        "  • colour — business unit\n\n"
        "Rendering notes:\n"
        "  • One shared icosphere mesh, per-bubble "
        "model matrix (translate + scale)\n"
        "  • Blinn-Phong shading + rim light\n"
        "  • Semi-transparent spheres sorted "
        "back-to-front each frame\n"
        "  • Axis cube + floor grid line pass\n\n"
        "Interaction:\n"
        "  • Drag to orbit the camera\n"
        "  • Mouse wheel to zoom\n"
        "  • Auto-rotation toggle above");

    return page.tab;
}

#else // !ULTRACANVAS_ENABLE_GL

std::shared_ptr<UltraCanvasUIElement> Build3DTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("Bubble3DTab");
    tab->SetPadding(10, 14, 10, 14);
    tab->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    auto placeholder = DemoKit::AddText(tab, "B3DPlaceholder",
        "3D Bubble Chart - NOT AVAILABLE\n\n"
        "This build was compiled without OpenGL support. "
        "Rebuild with -DULTRACANVAS_ENABLE_GL=ON to see the OpenGL "
        "3D bubble chart (spheres with risk/reward/time axes).");
    placeholder->SetTextColor(Color(120, 60, 60, 255));
    return tab;
}

#endif // ULTRACANVAS_ENABLE_GL

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBubbleChartExamples() {
    auto page = DemoKit::CreatePage("BubbleChartExamples", "Bubble Chart Examples",
        "Bubble matrix, scatter bubbles with a 4th colour dimension, packed and "
        "hierarchical packed bubbles, a bubble timeline, and an OpenGL-rendered 3D "
        "bubble chart. Hover any bubble for its tooltip.");
    // The page is the display area's height and the tabs take what the title
    // and description leave; a tab whose chart does not fit scrolls.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("BubbleChartTabsCard");
    card->SetPadding(4);
    DemoKit::Add(page, card, 1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("BubbleChartTabs", 0, 0, 0, 0);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("Bubble Matrix (Mrs. President)", BuildMatrixTab());
    tabs->AddTab("Scatter Bubbles (Travel Concerns)", BuildScatterTab());
    tabs->AddTab("Packed Bubbles", BuildPackedTab());
    tabs->AddTab("Hierarchical Packing", BuildHierarchicalTab());
    tabs->AddTab("Bubble Timeline", BuildTimelineTab());
    tabs->AddTab("3D Bubbles (OpenGL)", Build3DTab());
    tabs->SetActiveTab(0);

    DemoKit::Add(card, tabs, 1);
    return page;
}

} // namespace UltraCanvas

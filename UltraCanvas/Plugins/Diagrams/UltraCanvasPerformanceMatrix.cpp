// Plugins/Diagrams/UltraCanvasPerformanceMatrix.cpp
// The performance matrix element: layout, text fitting, the four designs,
// hover, selection and tooltips, moving items by drag and by key.
// Version: 1.2.0 - drag an item to another cell with a veto, the keyboard, the
//   selected item, the overflow list callback
// Version: 1.1.0 - the population, the group legend, ByCount, level bars, totals
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrix.h"
#include "UltraCanvasTextWrapping.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace UltraCanvas {

    namespace {

        constexpr float kMinFontSize = 9.0f;
        constexpr double kLineSpacing = 1.3;

        Color Mix(const Color& a, const Color& b, float t) {
            auto channel = [t](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>(std::lround(x + (y - x) * static_cast<double>(t)));
            };
            return Color(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), 255);
        }

        double Luminance(const Color& c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

        // Dark text on a light fill, white on a dark one.
        Color ContrastText(const Color& fill, const Color& dark) {
            return Luminance(fill) > 150.0 ? dark : Color(255, 255, 255, 255);
        }

        Color SampleRamp(const std::vector<Color>& ramp, int index, int count) {
            if (ramp.empty()) return Color(200, 200, 200, 255);
            if (count <= 1 || ramp.size() == 1) return ramp.front();
            const double t = static_cast<double>(index) / (count - 1);
            const size_t i = static_cast<size_t>(std::lround(t * static_cast<double>(ramp.size() - 1)));
            return ramp[std::min(i, ramp.size() - 1)];
        }

        std::string PercentText(double share) {
            char text[16];
            std::snprintf(text, sizeof(text), "%.0f%%", share * 100.0);   // locale-ok: a label a person reads
            return text;
        }

        std::string Score(double v) {
            char text[16];
            std::snprintf(text, sizeof(text), "%.1f", v);   // locale-ok: a tooltip a person reads
            return text;
        }

        Rect2Dd Inset(const Rect2Dd& r, double by) {
            return Rect2Dd(r.x + by, r.y + by, std::max(0.0, r.width - 2 * by), std::max(0.0, r.height - 2 * by));
        }

    } // namespace

// =============================================================================
// PALETTES
// =============================================================================

    PerformanceMatrixPalette PerformanceMatrixPalette::TrafficLight() {
        PerformanceMatrixPalette p;
        p.rankColors = {Color(66, 133, 214, 255), Color(76, 175, 80, 255), Color(247, 201, 52, 255),
                        Color(243, 140, 48, 255), Color(222, 62, 62, 255)};
        p.levelColors = {Color(222, 62, 62, 255), Color(243, 140, 48, 255), Color(76, 175, 80, 255)};
        return p;
    }

    PerformanceMatrixPalette PerformanceMatrixPalette::Vivid() {
        PerformanceMatrixPalette p = TrafficLight();
        p.levelColors = {Color(226, 61, 61, 255), Color(243, 146, 32, 255), Color(46, 164, 79, 255)};
        p.gridLine = Color(196, 202, 210, 255);
        return p;
    }

    PerformanceMatrixPalette PerformanceMatrixPalette::Monochrome(const Color& hue) {
        const Color white(255, 255, 255, 255);
        PerformanceMatrixPalette p;
        p.levelColors = {Mix(hue, white, 0.45f), Mix(hue, white, 0.2f), hue};
        p.rankColors = {hue, Mix(hue, white, 0.2f), Mix(hue, white, 0.4f), Mix(hue, white, 0.6f),
                        Mix(hue, white, 0.78f)};
        p.forceLevelColors = true;
        p.panelBackground = Mix(hue, white, 0.9f);
        p.gridLine = Mix(hue, white, 0.6f);
        return p;
    }

    PerformanceMatrixPalette PerformanceMatrixPalette::Pastel() {
        PerformanceMatrixPalette p;
        p.levelColors = {Color(232, 120, 160, 255), Color(240, 150, 60, 255), Color(232, 190, 40, 255)};
        p.rankColors = {Color(140, 196, 230, 255), Color(150, 210, 160, 255), Color(245, 222, 140, 255),
                        Color(245, 185, 140, 255), Color(238, 150, 170, 255)};
        p.panelBackground = Color(250, 249, 247, 255);
        p.gridLine = Color(222, 220, 216, 255);
        return p;
    }

// =============================================================================
// CONSTRUCTION AND OPTIONS
// =============================================================================

    UltraCanvasPerformanceMatrix::UltraCanvasPerformanceMatrix(const std::string& id, int x, int y,
                                                               int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        legend.SetPosition(ChartLegendPosition::RightStart);
        legend.SetOrientation(LegendOrientation::Vertical);
    }

    void UltraCanvasPerformanceMatrix::SetModel(const PerformanceMatrixModel& m) {
        model = m;
        chartTitle = model.title;
        hoveredCell = PerformanceCellRef();
        hoveredItem = -1;
        if (!model.Contains(selectedCell.row, selectedCell.col)) selectedCell = PerformanceCellRef();
        if (selectedItem >= static_cast<int>(model.items.size())) selectedItem = -1;
        EndDrag();
        // A group that no longer exists cannot stay hidden.
        const std::vector<std::string> groups = model.Groups();
        hiddenGroups.erase(std::remove_if(hiddenGroups.begin(), hiddenGroups.end(),
                                          [&groups](const std::string& g) {
                                              return std::find(groups.begin(), groups.end(), g) == groups.end();
                                          }),
                           hiddenGroups.end());
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetDesign(PerformanceMatrixDesign d) {
        design = d;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetCellColoring(PerformanceCellColoring c) {
        coloring = c;
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetPalette(const PerformanceMatrixPalette& p) {
        palette = p;
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetColumnLabels(PerformanceColumnLabels placement) {
        columnLabels = placement;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetShowAxisArrows(bool show) {
        showAxisArrows = show;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetShowContainerPanel(bool show) {
        showContainerPanel = show;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetCellFontSize(float size) {
        cellFontSize = std::clamp(size, kMinFontSize, 28.0f);
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetShowLevelBars(bool show) {
        showLevelBars = show;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetCountColormap(HeatmapColormap map) {
        countColormap = map;
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetPopulationDisplay(PerformancePopulationDisplay display) {
        populationDisplay = display;
        hoveredItem = -1;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetAnonymised(bool on) {
        anonymised = on;
        hoveredItem = -1;
        // No one is singled out in front of the room.
        if (on) {
            selectedItem = -1;
            EndDrag();
        }
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetShowLegend(bool show) {
        showLegend = show;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetShowTotals(bool show) {
        showTotals = show;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetGroupVisible(const std::string& group, bool visible) {
        const auto it = std::find(hiddenGroups.begin(), hiddenGroups.end(), group);
        if (visible && it != hiddenGroups.end()) hiddenGroups.erase(it);
        else if (!visible && it == hiddenGroups.end()) hiddenGroups.push_back(group);
        else return;
        hoveredItem = -1;
        DropLayoutCache();
        RequestRedraw();
    }

    bool UltraCanvasPerformanceMatrix::IsGroupVisible(const std::string& group) const {
        return std::find(hiddenGroups.begin(), hiddenGroups.end(), group) == hiddenGroups.end();
    }

    int UltraCanvasPerformanceMatrix::GetCellCount(const PerformanceCellRef& ref) const {
        if (!model.Contains(ref.row, ref.col)) return 0;
        const std::vector<int> counts = model.CountsPerCell(hiddenGroups);
        return counts[static_cast<size_t>(ref.row * model.Columns() + ref.col)];
    }

    std::vector<size_t> UltraCanvasPerformanceMatrix::GetCellItems(const PerformanceCellRef& ref) const {
        std::vector<size_t> members;
        if (!model.Contains(ref.row, ref.col)) return members;
        for (size_t i = 0; i < model.items.size(); ++i) {
            const PerformanceMatrixItem& item = model.items[i];
            if (IsGroupVisible(item.group) && model.CellOf(item) == ref) members.push_back(i);
        }
        return members;
    }

    void UltraCanvasPerformanceMatrix::SetAllowItemDrag(bool allow) {
        allowItemDrag = allow;
        if (!allow) EndDrag();
    }

    void UltraCanvasPerformanceMatrix::SetSelectedItem(int index) {
        const int next = index >= 0 && index < static_cast<int>(model.items.size()) && !anonymised ? index : -1;
        if (next == selectedItem) return;
        selectedItem = next;
        RequestRedraw();
    }

    bool UltraCanvasPerformanceMatrix::MoveItem(size_t index, const PerformanceCellRef& to) {
        if (index >= model.items.size() || !model.Contains(to.row, to.col)) return false;
        const PerformanceCellRef from = model.CellOf(model.items[index]);
        if (from == to) return false;
        if (onItemMove && !onItemMove(model.items[index], from, to)) return false;
        // The callback may have replaced the model; check again before writing.
        if (index >= model.items.size() || !model.Contains(to.row, to.col)) return false;
        PerformanceMatrixItem& item = model.items[index];
        item.row = to.row;
        item.col = to.col;
        hoveredItem = -1;
        DropLayoutCache();
        RequestRedraw();
        return true;
    }

    void UltraCanvasPerformanceMatrix::EndDrag() {
        const bool wasDragging = dragging;
        pressedItem = -1;
        dragging = false;
        dropTarget = PerformanceCellRef();
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            if (app->GetCapturedElement() == this) app->ReleaseMouse();
        }
        if (wasDragging) RequestRedraw();
    }

    void UltraCanvasPerformanceMatrix::SetSelectedCell(const PerformanceCellRef& ref) {
        const PerformanceCellRef next = model.Contains(ref.row, ref.col) ? ref : PerformanceCellRef();
        if (next == selectedCell) return;
        selectedCell = next;
        RequestRedraw();
    }

// =============================================================================
// GEOMETRY HELPERS
// =============================================================================

    int UltraCanvasPerformanceMatrix::DisplayRow(int row) const {
        // Top to bottom: high first, as on a chart, unless reversed.
        return model.yAxis.reversed ? row : model.Rows() - 1 - row;
    }

    int UltraCanvasPerformanceMatrix::DisplayColumn(int col) const {
        return model.xAxis.reversed ? model.Columns() - 1 - col : col;
    }

    const UltraCanvasPerformanceMatrix::CellLayout*
    UltraCanvasPerformanceMatrix::FindLayout(const PerformanceCellRef& ref) const {
        if (!layout.valid || !model.Contains(ref.row, ref.col)) return nullptr;
        const size_t index = static_cast<size_t>(ref.row * model.Columns() + ref.col);
        return index < layout.cells.size() ? &layout.cells[index] : nullptr;
    }

    Rect2Dd UltraCanvasPerformanceMatrix::GetCellRect(int row, int col) const {
        const CellLayout* cell = FindLayout(PerformanceCellRef(row, col));
        return cell ? cell->rect : Rect2Dd(0, 0, 0, 0);
    }

    PerformanceCellRef UltraCanvasPerformanceMatrix::CellAt(const Point2Di& local) const {
        const Point2Dd p(local.x, local.y);
        for (const CellLayout& cell : layout.cells) {
            if (cell.rect.Contains(p)) return cell.ref;
        }
        return PerformanceCellRef();
    }

    int UltraCanvasPerformanceMatrix::ItemAt(const Point2Di& local) const {
        if (anonymised || !layout.valid) return -1;
        const Point2Dd p(local.x, local.y);
        for (const CellLayout& cell : layout.cells) {
            if (!cell.population.Contains(p)) continue;
            for (const ItemMark& mark : cell.marks) {
                if (mark.item >= 0 && mark.rect.Contains(p)) return mark.item;
            }
        }
        return -1;
    }

    PerformanceCellRef UltraCanvasPerformanceMatrix::OverflowAt(const Point2Di& local) const {
        if (anonymised || !layout.valid) return PerformanceCellRef();
        const Point2Dd p(local.x, local.y);
        for (const CellLayout& cell : layout.cells) {
            if (!cell.population.Contains(p)) continue;
            for (const ItemMark& mark : cell.marks) {
                if (mark.item < 0 && mark.rect.Contains(p)) return cell.ref;
            }
        }
        return PerformanceCellRef();
    }

    Rect2Dd UltraCanvasPerformanceMatrix::GetItemRect(size_t index) const {
        return index < layout.itemRects.size() ? layout.itemRects[index] : Rect2Dd(0, 0, 0, 0);
    }

    size_t UltraCanvasPerformanceMatrix::LegendEntryAt(const Point2Di& local) const {
        if (!layout.legendShown) return SIZE_MAX;
        return legend.HitTest(Point2Dd(local.x, local.y));
    }

    bool UltraCanvasPerformanceMatrix::AnyCellTruncated() const {
        return std::any_of(layout.cells.begin(), layout.cells.end(),
                           [](const CellLayout& c) { return c.truncated; });
    }

// =============================================================================
// COLOURS
// =============================================================================

    Color UltraCanvasPerformanceMatrix::LevelColor(const PerformanceAxis& axis, int level) const {
        if (level < 0 || level >= static_cast<int>(axis.levels.size())) return palette.gridLine;
        const Color& own = axis.levels[static_cast<size_t>(level)].accent;
        if (!palette.forceLevelColors && own.a > 0) return own;
        return SampleRamp(palette.levelColors, level, static_cast<int>(axis.levels.size()));
    }

    Color UltraCanvasPerformanceMatrix::CellAccent(int row, int col) const {
        switch (coloring) {
            case PerformanceCellColoring::Explicit: {
                const PerformanceCell* cell = model.FindCell(row, col);
                return (cell && cell->fill.a > 0) ? cell->fill : palette.gridLine;
            }
            case PerformanceCellColoring::DiagonalBands:
                return SampleRamp(palette.rankColors, model.Rank(row, col), model.MaxRank() + 1);
            case PerformanceCellColoring::ByRow:    return LevelColor(model.yAxis, row);
            case PerformanceCellColoring::ByColumn: return LevelColor(model.xAxis, col);
            case PerformanceCellColoring::ByCount: {
                const CellLayout* cell = FindLayout(PerformanceCellRef(row, col));
                const double t = (cell && layout.maxCount > 0) ? static_cast<double>(cell->count) / layout.maxCount : 0.0;
                // From the light end, so an empty cell is near white and text reads on every shade.
                return SampleColormap(countColormap, {}, 0.08 + 0.72 * t, false);
            }
            case PerformanceCellColoring::Plain:     break;
        }
        return palette.gridLine;
    }

    Color UltraCanvasPerformanceMatrix::CellFill(int row, int col) const {
        // Cards keep a white face and show their colour as the border.
        if (design == PerformanceMatrixDesign::Cards || coloring == PerformanceCellColoring::Plain) {
            return palette.cellBackground;
        }
        if (coloring == PerformanceCellColoring::Explicit) {
            const PerformanceCell* cell = model.FindCell(row, col);
            return (cell && cell->fill.a > 0) ? cell->fill : palette.cellBackground;
        }
        const Color accent = CellAccent(row, col);
        // A level colour is a header colour: as a cell fill it is tinted, so
        // a whole row in it does not shout.
        if (coloring == PerformanceCellColoring::ByRow || coloring == PerformanceCellColoring::ByColumn) {
            return Mix(accent, palette.cellBackground, 0.55f);
        }
        return accent;
    }

// =============================================================================
// TEXT
// =============================================================================

    std::vector<std::string> UltraCanvasPerformanceMatrix::Wrap(IRenderContext* ctx, const std::string& text,
                                                                double width, float size, bool bold) const {
        ctx->SetFontSize(size);
        ctx->SetFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
        TextWrapping::Options options;
        options.lineWidth = std::max(1, static_cast<int>(width));
        options.maxLines = 40;
        options.camelCaseBreaks = false;     // prose, not file names
        const auto measure = [ctx](const std::string& piece) { return ctx->GetTextLineDimensions(piece).width; };
        std::vector<std::string> lines;
        std::stringstream paragraphs(text);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            if (paragraph.empty()) continue;
            const std::vector<std::string> wrapped = TextWrapping::WrapGreedy(measure, paragraph, options);
            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }
        return lines;
    }

    std::vector<UltraCanvasPerformanceMatrix::TextLine> UltraCanvasPerformanceMatrix::BuildLines(
            IRenderContext* ctx, const PerformanceCell& cell, double width, float size, int drop,
            size_t bulletLimit) const {
        std::vector<TextLine> lines;
        auto add = [&](const std::string& text, float s, bool bold, bool muted) {
            for (const std::string& line : Wrap(ctx, text, width, s, bold)) {
                TextLine t;
                t.text = line;
                t.size = s;
                t.bold = bold;
                t.muted = muted;
                lines.push_back(t);
            }
        };
        if (!cell.code.empty()) {
            TextLine code;
            code.text = cell.code;
            code.size = size * 1.9f;
            code.bold = true;
            lines.push_back(code);
        }
        if (!cell.title.empty()) add(cell.title, size * 1.08f, true, false);
        if (drop < 1 && !cell.description.empty()) add(cell.description, size, false, false);
        const size_t kept = std::min(bulletLimit, cell.bullets.size());
        const float bulletSize = size * 0.95f;
        for (size_t i = 0; i < kept; ++i) {
            const std::vector<std::string> wrapped = Wrap(ctx, cell.bullets[i], width - 14.0, bulletSize, false);
            for (size_t k = 0; k < wrapped.size(); ++k) {
                TextLine t;
                t.text = wrapped[k];
                t.size = bulletSize;
                t.bullet = (k == 0);
                t.continuation = (k > 0);
                lines.push_back(t);
            }
        }
        if (kept < cell.bullets.size()) {
            TextLine more;
            more.text = "\xE2\x80\xA6";          // an ellipsis: more in the tooltip
            more.size = bulletSize;
            more.muted = true;
            more.continuation = true;
            lines.push_back(more);
        }
        if (drop < 2 && !cell.action.empty()) add(cell.action, size * 0.95f, false, true);
        return lines;
    }

    double UltraCanvasPerformanceMatrix::LinesHeight(const std::vector<TextLine>& lines) const {
        double h = 0.0;
        for (const TextLine& line : lines) h += line.size * kLineSpacing;
        return h;
    }

    double UltraCanvasPerformanceMatrix::FixedContentHeight(const PerformanceCell& cell, float size) const {
        double h = 0.0;
        if (!cell.iconPath.empty()) h += size * 2.2 + 4.0;
        if (cell.badge > 0) h += size * 2.1 + 6.0;
        return h;
    }

    void UltraCanvasPerformanceMatrix::FitCell(IRenderContext* ctx, CellLayout& cell, float size) const {
        cell.lines.clear();
        cell.truncated = false;
        cell.textHeight = 0.0;
        const PerformanceCell* data = model.FindCell(cell.ref.row, cell.ref.col);
        if (!data) return;
        const double available = cell.content.height - FixedContentHeight(*data, size);
        const double width = cell.content.width;

        // What a crowded cell gives up, in order: bullets from the end, then
        // the description, then the action. The code, badge and title stay.
        std::vector<std::pair<int, size_t>> attempts = {{0, data->bullets.size()}};
        for (size_t k = data->bullets.size(); k-- > 0;) attempts.push_back({0, k});
        attempts.push_back({1, 0});
        attempts.push_back({2, 0});
        for (size_t i = 0; i < attempts.size(); ++i) {
            std::vector<TextLine> lines = BuildLines(ctx, *data, width, size, attempts[i].first, attempts[i].second);
            const double height = LinesHeight(lines);
            if (height <= available || i + 1 == attempts.size()) {
                cell.lines = std::move(lines);
                cell.textHeight = height;
                cell.truncated = (i > 0) || height > available;
                return;
            }
        }
    }

    double UltraCanvasPerformanceMatrix::HeaderBlockHeight(IRenderContext* ctx, const PerformanceLevel& level,
                                                           double width) const {
        double h = 0.0;
        if (design == PerformanceMatrixDesign::HeaderTabs && !level.iconPath.empty()) h += 26.0 + 4.0;
        h += Wrap(ctx, level.label, width, 12.5f, true).size() * 12.5 * kLineSpacing;
        if (!level.caption.empty()) h += Wrap(ctx, level.caption, width, 11.0f, false).size() * 11.0 * kLineSpacing;
        if (showTotals && HasPopulation()) h += 11.0 * kLineSpacing;
        return h;
    }

// =============================================================================
// POPULATION
// =============================================================================

    PerformancePopulationDisplay UltraCanvasPerformanceMatrix::EffectiveDisplay() const {
        if (!HasPopulation()) return PerformancePopulationDisplay::Hidden;
        if (anonymised && populationDisplay == PerformancePopulationDisplay::Names) {
            return PerformancePopulationDisplay::Count;
        }
        return populationDisplay;
    }

    Color UltraCanvasPerformanceMatrix::GroupColor(const std::string& group) const {
        const std::vector<std::string> groups = model.Groups();
        const auto it = std::find(groups.begin(), groups.end(), group);
        const size_t index = static_cast<size_t>(it - groups.begin());
        if (palette.groupColors.empty()) return palette.text;
        return palette.groupColors[index % palette.groupColors.size()];
    }

    Color UltraCanvasPerformanceMatrix::ItemColor(const PerformanceMatrixItem& item) const {
        return item.color.a > 0 ? item.color : GroupColor(item.group);
    }

    std::string UltraCanvasPerformanceMatrix::TotalsText(int count) const {
        return "n = " + std::to_string(count);
    }

    void UltraCanvasPerformanceMatrix::LayoutPopulation(IRenderContext* ctx, CellLayout& cell) {
        const PerformancePopulationDisplay display = EffectiveDisplay();
        if (display == PerformancePopulationDisplay::Hidden) return;
        if (display == PerformancePopulationDisplay::Count || display == PerformancePopulationDisplay::Percent) {
            // A chip in the top corner: the text starts below it.
            const double chip = std::min(20.0, cell.content.height * 0.3);
            cell.content.y += chip;
            cell.content.height = std::max(0.0, cell.content.height - chip);
            return;
        }

        const double popHeight = std::clamp(cell.content.height * 0.45, std::min(18.0, cell.content.height),
                                            std::max(18.0, cell.content.height * 0.6));
        cell.population = Rect2Dd(cell.content.x, cell.content.y + cell.content.height - popHeight,
                                  cell.content.width, popHeight);
        cell.content.height = std::max(0.0, cell.content.height - popHeight - 4.0);

        std::vector<int> members;
        for (size_t i = 0; i < model.items.size(); ++i) {
            const PerformanceMatrixItem& item = model.items[i];
            if (IsGroupVisible(item.group) && model.CellOf(item) == cell.ref) members.push_back(static_cast<int>(i));
        }
        const Rect2Dd& area = cell.population;
        const int n = static_cast<int>(members.size());
        if (n == 0 || area.width < 8.0 || area.height < 8.0) return;

        if (display == PerformancePopulationDisplay::Dots) {
            // A fixed grid, so the drawing is stable and dots never overlap.
            const double step = 13.0;
            const int columns = std::max(1, static_cast<int>(area.width / step));
            const int rows = std::max(1, static_cast<int>(area.height / step));
            const int capacity = columns * rows;
            // "+N" takes the last three slots when not everyone fits.
            const int shown = n <= capacity ? n : std::max(0, capacity - 3);
            for (int k = 0; k < shown; ++k) {
                const Rect2Dd slot(area.x + (k % columns) * step, area.y + (k / columns) * step, step, step);
                cell.marks.push_back({members[static_cast<size_t>(k)], slot, 0});
                layout.itemRects[static_cast<size_t>(members[static_cast<size_t>(k)])] = slot;
            }
            if (shown < n) {
                const Rect2Dd more(area.x + (shown % columns) * step, area.y + (shown / columns) * step,
                                   step * 3.0, step);
                cell.marks.push_back({-1, more, n - shown});
            }
            return;
        }

        // Names, in columns, filled top to bottom.
        ctx->SetFontSize(10.5f);
        ctx->SetFontWeight(FontWeight::Normal);
        double widest = 0.0;
        for (int i : members) {
            widest = std::max(widest, static_cast<double>(
                ctx->GetTextLineDimensions(model.items[static_cast<size_t>(i)].label).width));
        }
        const double lineHeight = 14.0;
        const double wanted = std::max(60.0, widest + 14.0);
        const int columns = std::max(1, static_cast<int>(area.width / std::min(area.width, wanted)));
        const double columnWidth = area.width / columns;
        const int rows = std::max(1, static_cast<int>(area.height / lineHeight));
        const int capacity = columns * rows;
        const int shown = n <= capacity ? n : std::max(0, capacity - 1);
        for (int k = 0; k < shown; ++k) {
            const Rect2Dd slot(area.x + (k / rows) * columnWidth, area.y + (k % rows) * lineHeight,
                               columnWidth, lineHeight);
            cell.marks.push_back({members[static_cast<size_t>(k)], slot, 0});
            layout.itemRects[static_cast<size_t>(members[static_cast<size_t>(k)])] = slot;
        }
        if (shown < n) {
            const Rect2Dd more(area.x + (shown / rows) * columnWidth, area.y + (shown % rows) * lineHeight,
                               columnWidth, lineHeight);
            cell.marks.push_back({-1, more, n - shown});
        }
    }

// =============================================================================
// LAYOUT
// =============================================================================

    std::string UltraCanvasPerformanceMatrix::FitTitle(IRenderContext* ctx) const {
        const double room = std::max(20.0, GetWidth() - 28.0);
        ctx->SetFontWeight(FontWeight::Bold);
        float size = 18.0f;
        for (; size > 11.0f; size -= 0.5f) {
            ctx->SetFontSize(size);
            if (ctx->GetTextLineDimensions(chartTitle).width <= room) return chartTitle;
        }
        ctx->SetFontSize(size);
        std::string text = chartTitle;
        const std::string ellipsis = "\xE2\x80\xA6";
        while (!text.empty() && ctx->GetTextLineDimensions(text + ellipsis).width > room) {
            // Drop whole UTF-8 characters, never half of one.
            size_t cut = text.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            text.erase(cut);
        }
        return text + ellipsis;
    }

    void UltraCanvasPerformanceMatrix::UpdateLayout(IRenderContext* ctx) {
        layout = Layout();
        layout.valid = true;
        const double W = GetWidth(), H = GetHeight();
        double left = 14.0, top = 12.0, right = W - 14.0, bottom = H - 12.0;

        if (!chartTitle.empty()) {
            const std::string title = FitTitle(ctx);
            top += ctx->GetTextLineDimensions(title).height + 6.0;
        }
        if (!model.subtitle.empty()) {
            ctx->SetFontSize(12.0f);
            ctx->SetFontWeight(FontWeight::Normal);
            top += ctx->GetTextLineDimensions(model.subtitle).height + 4.0;
        }
        if (!chartTitle.empty() || !model.subtitle.empty()) top += 6.0;
        if (showContainerPanel) {
            layout.panel = Rect2Dd(left, top, std::max(0.0, right - left), std::max(0.0, bottom - top));
            left += 14.0;
            top += 14.0;
            right -= 14.0;
            bottom -= 14.0;
        }

        const int rows = model.Rows(), cols = model.Columns();
        if (rows == 0 || cols == 0 || right - left < 40.0 || bottom - top < 40.0) return;

        // ---- the population: counts, and the group legend at the right -----
        const std::vector<int> cellCounts = model.CountsPerCell(hiddenGroups);
        layout.rowCounts = model.CountsPerRow(hiddenGroups);
        layout.columnCounts = model.CountsPerColumn(hiddenGroups);
        for (int n : cellCounts) {
            layout.total += n;
            layout.maxCount = std::max(layout.maxCount, n);
        }
        layout.itemRects.assign(model.items.size(), Rect2Dd(0, 0, 0, 0));
        const std::vector<std::string> groups = model.Groups();
        const bool named = groups.size() > 1 || (groups.size() == 1 && !groups.front().empty());
        if (showLegend && HasPopulation() && named) {
            std::vector<ChartLegendEntry> entries;
            for (const std::string& group : groups) {
                int members = 0;
                for (const PerformanceMatrixItem& item : model.items) members += item.group == group;
                ChartLegendEntry entry(group.empty() ? std::string("(no group)") : group, GroupColor(group),
                                       LegendSwatch::Circle);
                entry.valueText = std::to_string(members);
                entry.enabled = IsGroupVisible(group);
                entries.push_back(entry);
            }
            legend.SetEntries(entries);
            layout.legendArea = Rect2Dd(left, top, right - left, bottom - top);
            legend.Measure(ctx, layout.legendArea);
            const Rect2Dd rest = legend.RemainingArea(layout.legendArea);
            left = rest.x;
            top = rest.y;
            right = rest.x + rest.width;
            bottom = rest.y + rest.height;
            layout.legendShown = true;
            if (right - left < 40.0 || bottom - top < 40.0) return;
        }

        layout.columnLabelsOnTop =
            columnLabels == PerformanceColumnLabels::Top ||
            (columnLabels == PerformanceColumnLabels::Auto &&
             (design == PerformanceMatrixDesign::Tiles || design == PerformanceMatrixDesign::HeaderTabs));

        double gridLeft, gridTop, gridRight = right, gridBottom = bottom;
        if (design == PerformanceMatrixDesign::HeaderTabs) {
            // Coloured tabs carry the levels: no arrows, no axis titles.
            const double rowTab = std::clamp((right - left) * 0.2, 90.0, 180.0);
            gridLeft = left + rowTab + 8.0;
            const double cw = (gridRight - gridLeft) / cols;
            double tab = 0.0;
            for (const PerformanceLevel& level : model.xAxis.levels) {
                tab = std::max(tab, HeaderBlockHeight(ctx, level, cw - 20.0));
            }
            tab = std::min(tab + 20.0, (bottom - top) * 0.4);
            gridTop = top + tab + 8.0;
            layout.rowLabelWidth = rowTab;
            const double ch = (gridBottom - gridTop) / rows;
            for (int c = 0; c < cols; ++c) {
                const double x = gridLeft + DisplayColumn(c) * cw;
                layout.columnHeaders.push_back({Rect2Dd(x + 3.0, top, cw - 6.0, tab), c});
            }
            for (int r = 0; r < rows; ++r) {
                const double y = gridTop + DisplayRow(r) * ch;
                layout.rowHeaders.push_back({Rect2Dd(left, y + 3.0, rowTab, ch - 6.0), r});
            }
        } else {
            double yTitle = 0.0, xTitle = 0.0;
            if (!model.yAxis.title.empty() || !model.xAxis.title.empty()) {
                ctx->SetFontSize(13.0f);
                ctx->SetFontWeight(FontWeight::Bold);
                if (!model.yAxis.title.empty()) yTitle = ctx->GetTextLineDimensions(model.yAxis.title).height + 8.0;
                if (!model.xAxis.title.empty()) xTitle = ctx->GetTextLineDimensions(model.xAxis.title).height + 6.0;
            }
            const double arrow = showAxisArrows ? 16.0 : 0.0;
            double labelWidth = 0.0;
            for (const PerformanceLevel& level : model.yAxis.levels) {
                ctx->SetFontSize(12.5f);
                ctx->SetFontWeight(FontWeight::Bold);
                labelWidth = std::max(labelWidth, static_cast<double>(ctx->GetTextLineDimensions(level.label).width));
                if (!level.caption.empty()) {
                    ctx->SetFontSize(11.0f);
                    ctx->SetFontWeight(FontWeight::Normal);
                    labelWidth = std::max(labelWidth,
                                          static_cast<double>(ctx->GetTextLineDimensions(level.caption).width));
                }
            }
            layout.rowLabelWidth = std::clamp(labelWidth + 16.0, 40.0, (right - left) * 0.28);
            gridLeft = left + yTitle + arrow + layout.rowLabelWidth;
            const double cw = (gridRight - gridLeft) / cols;
            double labels = 0.0;
            for (const PerformanceLevel& level : model.xAxis.levels) {
                labels = std::max(labels, HeaderBlockHeight(ctx, level, cw - 8.0));
            }
            labels += 10.0;
            const double bar = showLevelBars ? 10.0 : 0.0;   // the colour strip between grid and labels
            labels += bar;
            const double xBand = xTitle + arrow;
            gridTop = top + (layout.columnLabelsOnTop ? labels : 0.0);
            gridBottom = bottom - xBand - (layout.columnLabelsOnTop ? 0.0 : labels);
            const double ch = (gridBottom - gridTop) / rows;
            for (int c = 0; c < cols; ++c) {
                const double x = gridLeft + DisplayColumn(c) * cw;
                const double y = layout.columnLabelsOnTop ? top : gridBottom + bar;
                layout.columnHeaders.push_back({Rect2Dd(x, y, cw, labels - bar), c});
            }
            for (int r = 0; r < rows; ++r) {
                const double y = gridTop + DisplayRow(r) * ch;
                layout.rowHeaders.push_back({Rect2Dd(gridLeft - layout.rowLabelWidth, y,
                                                     layout.rowLabelWidth - 8.0, ch), r});
            }
            layout.yTitleBox = Rect2Dd(left, gridTop, yTitle, gridBottom - gridTop);
            layout.yArrowX = left + yTitle + arrow * 0.5;
            const double belowGrid = gridBottom + (layout.columnLabelsOnTop ? 0.0 : labels);
            layout.xArrowY = belowGrid + arrow * 0.5;
            layout.xTitleBox = Rect2Dd(gridLeft, belowGrid + arrow, gridRight - gridLeft, xTitle);
        }
        layout.grid = Rect2Dd(gridLeft, gridTop, std::max(1.0, gridRight - gridLeft),
                              std::max(1.0, gridBottom - gridTop));

        // ---- cells ----------------------------------------------------------
        const double cw = layout.grid.width / cols, ch = layout.grid.height / rows;
        const double gap = design == PerformanceMatrixDesign::Tiles ? 8.0
                         : design == PerformanceMatrixDesign::Cards ? 12.0 : 0.0;
        const double padding = design == PerformanceMatrixDesign::Cards ? 12.0
                             : design == PerformanceMatrixDesign::Tiles ? 10.0 : 8.0;
        layout.cells.resize(static_cast<size_t>(rows * cols));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                CellLayout& cell = layout.cells[static_cast<size_t>(r * cols + c)];
                cell.ref = PerformanceCellRef(r, c);
                cell.rect = Rect2Dd(layout.grid.x + DisplayColumn(c) * cw + gap * 0.5,
                                    layout.grid.y + DisplayRow(r) * ch + gap * 0.5,
                                    std::max(1.0, cw - gap), std::max(1.0, ch - gap));
                cell.content = Inset(cell.rect, padding);
                cell.count = cellCounts[static_cast<size_t>(r * cols + c)];
                LayoutPopulation(ctx, cell);
            }
        }

        // ---- one font size for every cell: the largest at which all fit ------
        float size = kMinFontSize;
        for (float s = cellFontSize; s >= kMinFontSize; s -= 0.5f) {
            bool all = true;
            for (CellLayout& cell : layout.cells) {
                const PerformanceCell* data = model.FindCell(cell.ref.row, cell.ref.col);
                if (!data) continue;
                const std::vector<TextLine> lines = BuildLines(ctx, *data, cell.content.width, s, 0,
                                                               std::numeric_limits<size_t>::max());
                if (LinesHeight(lines) > cell.content.height - FixedContentHeight(*data, s)) {
                    all = false;
                    break;
                }
            }
            if (all) { size = s; break; }
        }
        layout.fontSize = size;
        for (CellLayout& cell : layout.cells) FitCell(ctx, cell, size);
        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasPerformanceMatrix::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        (void)dirtyRect;
        if (!ctx) return;
        UpdateLayout(ctx);
        ctx->DrawFilledRectangle(GetLocalBounds(), palette.background);

        double y = 12.0;
        if (!chartTitle.empty()) {
            const std::string title = FitTitle(ctx);
            ctx->SetTextPaint(palette.text);
            const Size2Di s = ctx->GetTextLineDimensions(title);
            ctx->DrawText(title, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
            y += s.height + 6.0;
        }
        if (!model.subtitle.empty()) {
            ctx->SetFontSize(12.0f);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(palette.mutedText);
            const Size2Di s = ctx->GetTextLineDimensions(model.subtitle);
            ctx->DrawText(model.subtitle, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
        }
        ctx->SetFontWeight(FontWeight::Normal);
        RenderChart(ctx);
    }

    void UltraCanvasPerformanceMatrix::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);
        if (showContainerPanel && layout.panel.width > 0.0) {
            ctx->SetFillPaint(palette.panelBackground);
            ctx->FillRoundedRectangle(layout.panel, 14.0);
        }
        if (layout.legendShown) legend.Render(ctx, layout.legendArea);
        if (layout.cells.empty()) return;
        DrawHeaders(ctx);
        if (design != PerformanceMatrixDesign::HeaderTabs) DrawAxes(ctx);
        if (showLevelBars && design != PerformanceMatrixDesign::HeaderTabs) DrawLevelBars(ctx);
        DrawCells(ctx);
        DrawDrag(ctx);
    }

    void UltraCanvasPerformanceMatrix::DrawLevelBars(IRenderContext* ctx) {
        // One segment per level, in the level colour, between the grid and its labels.
        const Rect2Dd& g = layout.grid;
        const double cw = g.width / model.Columns(), ch = g.height / model.Rows();
        const double barY = layout.columnLabelsOnTop ? g.y - 8.0 : g.y + g.height + 2.0;
        for (int c = 0; c < model.Columns(); ++c) {
            ctx->SetFillPaint(LevelColor(model.xAxis, c));
            ctx->FillRectangle(Rect2Dd(g.x + DisplayColumn(c) * cw + 1.0, barY, cw - 2.0, 6.0));
        }
        for (int r = 0; r < model.Rows(); ++r) {
            ctx->SetFillPaint(LevelColor(model.yAxis, r));
            ctx->FillRectangle(Rect2Dd(g.x - 7.0, g.y + DisplayRow(r) * ch + 1.0, 5.0, ch - 2.0));
        }
    }

    void UltraCanvasPerformanceMatrix::DrawPopulation(IRenderContext* ctx, const CellLayout& cell,
                                                      const Color& fill) const {
        const PerformancePopulationDisplay display = EffectiveDisplay();
        if (display == PerformancePopulationDisplay::Hidden) return;
        const bool darkFill = Luminance(fill) <= 150.0;
        const Color ink = ContrastText(fill, palette.text);

        if (display == PerformancePopulationDisplay::Count || display == PerformancePopulationDisplay::Percent) {
            std::string text = std::to_string(cell.count);
            if (display == PerformancePopulationDisplay::Percent) {
                text = PercentText(layout.total > 0 ? static_cast<double>(cell.count) / layout.total : 0.0);
                const size_t index = static_cast<size_t>(cell.ref.row * model.Columns() + cell.ref.col);
                if (model.targetDistribution.size() == static_cast<size_t>(model.Rows() * model.Columns())) {
                    text += " / " + PercentText(model.targetDistribution[index]);
                }
            }
            ctx->SetFontSize(11.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            const Size2Di size = ctx->GetTextLineDimensions(text);
            const double w = size.width + 12.0, h = 17.0;
            const Rect2Dd chip(cell.rect.x + cell.rect.width - w - 6.0, cell.rect.y + 5.0, w, h);
            // A dark chip on a light cell, a light one on a dark cell.
            const Color chipFill = darkFill ? Color(255, 255, 255, 225) : Color(38, 42, 50, 215);
            ctx->SetFillPaint(cell.count == 0 ? chipFill.WithAlpha(110) : chipFill);
            ctx->FillRoundedRectangle(chip, h * 0.5);
            ctx->SetTextPaint(darkFill ? palette.text : Color(255, 255, 255, 255));
            ctx->DrawText(text, Point2Dd(chip.x + 6.0, chip.y + (h - size.height) * 0.5));
            ctx->SetFontWeight(FontWeight::Normal);
            return;
        }

        ctx->PushState();
        ctx->ClipRect(cell.population);
        for (const ItemMark& mark : cell.marks) {
            if (mark.item < 0) {
                const std::string more = "+" + std::to_string(mark.more) +
                                         (display == PerformancePopulationDisplay::Names ? " more" : "");
                ctx->SetFontSize(10.5f);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(ink);
                const Size2Di size = ctx->GetTextLineDimensions(more);
                ctx->DrawText(more, Point2Dd(mark.rect.x + 2.0, mark.rect.y + (mark.rect.height - size.height) * 0.5));
                ctx->SetFontWeight(FontWeight::Normal);
                continue;
            }
            const PerformanceMatrixItem& item = model.items[static_cast<size_t>(mark.item)];
            // The item being dragged stays in place, faded, until it lands.
            const bool lifted = dragging && mark.item == pressedItem;
            const Color color = lifted ? Mix(fill, ItemColor(item), 0.35f) : ItemColor(item);
            const bool hovered = mark.item == hoveredItem && !dragging;
            const bool selected = mark.item == selectedItem;
            if (display == PerformancePopulationDisplay::Dots) {
                const Point2Dd centre(mark.rect.x + mark.rect.width * 0.5, mark.rect.y + mark.rect.height * 0.5);
                ctx->SetFillPaint(color);
                ctx->FillCircle(centre, hovered ? 6.0 : 4.6);
                ctx->SetStrokePaint(darkFill ? Color(255, 255, 255, 230) : Color(255, 255, 255, 255));
                ctx->SetStrokeWidth(hovered ? 2.0f : 1.0f);
                ctx->DrawCircle(centre, hovered ? 6.0 : 4.6);
                if (selected) {
                    ctx->SetStrokePaint(ink);
                    ctx->SetStrokeWidth(1.5f);
                    ctx->DrawCircle(centre, 6.2);
                }
            } else {
                if (selected) {
                    ctx->SetStrokePaint(ink);
                    ctx->SetStrokeWidth(1.2f);
                    ctx->DrawRoundedRectangle(Inset(mark.rect, 0.8), 3.0);
                }
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(mark.rect.x + 1.0, mark.rect.y + (mark.rect.height - 7.0) * 0.5, 7.0, 7.0));
                ctx->SetFontSize(10.5f);
                ctx->SetFontWeight(hovered ? FontWeight::Bold : FontWeight::Normal);
                ctx->SetTextPaint(ink);
                const Size2Di size = ctx->GetTextLineDimensions(item.label);
                ctx->PushState();
                ctx->ClipRect(Rect2Dd(mark.rect.x, mark.rect.y, mark.rect.width - 2.0, mark.rect.height));
                ctx->DrawText(item.label, Point2Dd(mark.rect.x + 12.0, mark.rect.y + (mark.rect.height - size.height) * 0.5));
                ctx->PopState();
            }
        }
        ctx->PopState();
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasPerformanceMatrix::DrawCells(IRenderContext* ctx) {
        const bool rounded = design == PerformanceMatrixDesign::Tiles || design == PerformanceMatrixDesign::Cards;
        const double radius = design == PerformanceMatrixDesign::Cards ? 10.0 : 12.0;
        for (const CellLayout& cell : layout.cells) {
            const Color fill = CellFill(cell.ref.row, cell.ref.col);
            const Color accent = CellAccent(cell.ref.row, cell.ref.col);
            ctx->SetFillPaint(fill);
            if (rounded) ctx->FillRoundedRectangle(cell.rect, radius);
            else ctx->FillRectangle(cell.rect);

            if (design == PerformanceMatrixDesign::Cards) {
                ctx->SetStrokePaint(accent);
                ctx->SetStrokeWidth(2.5f);
                ctx->DrawRoundedRectangle(cell.rect, radius);
            } else if (!rounded) {
                ctx->SetStrokePaint(palette.gridLine);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(cell.rect);
            }
            DrawCellText(ctx, cell, ContrastText(fill, palette.text), accent);
            DrawPopulation(ctx, cell, fill);
        }

        // Hover and selection on top, so a neighbour never paints over them.
        auto outline = [&](const PerformanceCellRef& ref, const Color& color, float width) {
            const CellLayout* cell = FindLayout(ref);
            if (!cell) return;
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(width);
            const Rect2Dd r = Inset(cell->rect, width * 0.5);
            if (rounded) ctx->DrawRoundedRectangle(r, radius);
            else ctx->DrawRectangle(r);
        };
        if (hoveredCell.IsValid() && hoveredCell != selectedCell) {
            outline(hoveredCell, CellAccent(hoveredCell.row, hoveredCell.col).Darken(0.35f), 2.0f);
        }
        if (selectedCell.IsValid()) outline(selectedCell, palette.text, 3.0f);
    }

    void UltraCanvasPerformanceMatrix::DrawDrag(IRenderContext* ctx) const {
        if (!dragging || pressedItem < 0 || pressedItem >= static_cast<int>(model.items.size())) return;
        const PerformanceMatrixItem& item = model.items[static_cast<size_t>(pressedItem)];
        const bool rounded = design == PerformanceMatrixDesign::Tiles || design == PerformanceMatrixDesign::Cards;
        const double radius = design == PerformanceMatrixDesign::Cards ? 10.0 : 12.0;

        // The cell it would land in: a dashed outline, so a drop that changes
        // nothing (its own cell) shows none.
        if (dropTarget.IsValid() && dropTarget != model.CellOf(item)) {
            if (const CellLayout* target = FindLayout(dropTarget)) {
                ctx->PushState();
                ctx->SetStrokePaint(ItemColor(item));
                ctx->SetStrokeWidth(3.0f);
                ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
                const Rect2Dd r = Inset(target->rect, 2.5);
                if (rounded) ctx->DrawRoundedRectangle(r, radius);
                else ctx->DrawRectangle(r);
                ctx->PopState();
            }
        }

        // The item under the pointer: its dot and its label on a pill.
        const Point2Dd at(dragPoint.x, dragPoint.y);
        ctx->SetFillPaint(ItemColor(item));
        ctx->FillCircle(at, 6.0);
        ctx->SetStrokePaint(Color(255, 255, 255, 255));
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawCircle(at, 6.0);
        ctx->SetFontSize(11.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        const Size2Di size = ctx->GetTextLineDimensions(item.label);
        const Rect2Dd pill(at.x + 10.0, at.y - size.height * 0.5 - 3.0, size.width + 12.0, size.height + 6.0);
        ctx->SetFillPaint(Color(38, 42, 50, 225));
        ctx->FillRoundedRectangle(pill, pill.height * 0.5);
        ctx->SetTextPaint(Color(255, 255, 255, 255));
        ctx->DrawText(item.label, Point2Dd(pill.x + 6.0, pill.y + 3.0));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasPerformanceMatrix::DrawCellText(IRenderContext* ctx, const CellLayout& cell,
                                                    const Color& textColor, const Color& accent) const {
        const PerformanceCell* data = model.FindCell(cell.ref.row, cell.ref.col);
        if (!data) return;
        const float size = layout.fontSize;
        const bool leftAligned = design == PerformanceMatrixDesign::Cards;
        const Rect2Dd& box = cell.content;
        const double total = FixedContentHeight(*data, size) + cell.textHeight;
        double y = leftAligned ? box.y : box.y + std::max(0.0, (box.height - total) * 0.5);
        const Color muted = Luminance(textColor) > 150.0 ? textColor.WithAlpha(210) : palette.mutedText;

        ctx->PushState();
        ctx->ClipRect(cell.rect);
        if (!data->iconPath.empty()) {
            const double s = size * 2.2;
            const double x = leftAligned ? box.x : box.x + (box.width - s) * 0.5;
            ctx->DrawImage(data->iconPath, Rect2Dd(x, y, s, s), ImageFitMode::Contain);
            y += s + 4.0;
        }
        if (data->badge > 0) {
            const double d = size * 2.1;
            const Point2Dd centre(leftAligned ? box.x + d * 0.5 : box.x + box.width * 0.5, y + d * 0.5);
            ctx->SetFillPaint(accent);
            ctx->FillCircle(centre, d * 0.5);
            const std::string number = std::to_string(data->badge);
            ctx->SetFontSize(size * 1.05f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(ContrastText(accent, palette.text));
            const Size2Di s = ctx->GetTextLineDimensions(number);
            ctx->DrawText(number, Point2Dd(centre.x - s.width * 0.5, centre.y - s.height * 0.5));
            y += d + 6.0;
        }
        for (const TextLine& line : cell.lines) {
            ctx->SetFontSize(line.size);
            ctx->SetFontWeight(line.bold ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(line.muted ? muted : textColor);
            const Size2Di s = ctx->GetTextLineDimensions(line.text);
            const double lineHeight = line.size * kLineSpacing;
            const bool indented = line.bullet || line.continuation;
            double x = (leftAligned || indented) ? box.x + (indented ? 14.0 : 0.0)
                                                 : box.x + (box.width - s.width) * 0.5;
            if (line.bullet) {
                ctx->SetFillPaint(design == PerformanceMatrixDesign::Cards ? accent : textColor);
                ctx->FillCircle(Point2Dd(box.x + 5.0, y + lineHeight * 0.5), 2.4);
            }
            ctx->DrawText(line.text, Point2Dd(x, y + (lineHeight - s.height) * 0.5));
            y += lineHeight;
        }
        ctx->PopState();
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasPerformanceMatrix::DrawLevelLabel(IRenderContext* ctx, const PerformanceLevel& level,
                                                      const Rect2Dd& box, const Color& textColor,
                                                      bool withIcon, const std::string& extra) const {
        const double width = box.width - 8.0;
        const std::vector<std::string> label = Wrap(ctx, level.label, width, 12.5f, true);
        const std::vector<std::string> caption =
            level.caption.empty() ? std::vector<std::string>() : Wrap(ctx, level.caption, width, 11.0f, false);
        const bool icon = withIcon && !level.iconPath.empty();
        const double height = (icon ? 30.0 : 0.0) + label.size() * 12.5 * kLineSpacing +
                              caption.size() * 11.0 * kLineSpacing + (extra.empty() ? 0.0 : 11.0 * kLineSpacing);
        double y = box.y + std::max(0.0, (box.height - height) * 0.5);
        const double cx = box.x + box.width * 0.5;
        if (icon) {
            ctx->DrawImage(level.iconPath, Rect2Dd(cx - 13.0, y, 26.0, 26.0), ImageFitMode::Contain);
            y += 30.0;
        }
        auto draw = [&](const std::vector<std::string>& lines, float size, bool bold, const Color& color) {
            ctx->SetFontSize(size);
            ctx->SetFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(color);
            for (const std::string& line : lines) {
                const Size2Di s = ctx->GetTextLineDimensions(line);
                ctx->DrawText(line, Point2Dd(cx - s.width * 0.5, y + (size * kLineSpacing - s.height) * 0.5));
                y += size * kLineSpacing;
            }
        };
        draw(label, 12.5f, true, textColor);
        const Color secondary = Luminance(textColor) > 150.0 ? textColor : palette.mutedText;
        draw(caption, 11.0f, false, secondary);
        if (!extra.empty()) draw({extra}, 11.0f, false, secondary);
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasPerformanceMatrix::DrawHeaders(IRenderContext* ctx) {
        const bool tabs = design == PerformanceMatrixDesign::HeaderTabs;
        auto header = [&](const HeaderLayout& h, const PerformanceAxis& axis, bool hovered,
                          const std::vector<int>& counts) {
            if (h.level < 0 || h.level >= static_cast<int>(axis.levels.size())) return;
            const PerformanceLevel& level = axis.levels[static_cast<size_t>(h.level)];
            const std::string extra = (showTotals && HasPopulation() && h.level < static_cast<int>(counts.size()))
                                          ? TotalsText(counts[static_cast<size_t>(h.level)]) : std::string();
            if (tabs) {
                const Color color = LevelColor(axis, h.level);
                ctx->SetFillPaint(color);
                ctx->FillRoundedRectangle(h.rect, 8.0);
                if (hovered) {
                    ctx->SetStrokePaint(color.Darken(0.35f));
                    ctx->SetStrokeWidth(2.0f);
                    ctx->DrawRoundedRectangle(Inset(h.rect, 1.0), 8.0);
                }
                DrawLevelLabel(ctx, level, h.rect, ContrastText(color, palette.text), true, extra);
            } else {
                // A hovered row or column reads in its level colour.
                DrawLevelLabel(ctx, level, h.rect, hovered ? LevelColor(axis, h.level).Darken(0.2f) : palette.text,
                               false, extra);
            }
        };
        for (const HeaderLayout& h : layout.columnHeaders) {
            header(h, model.xAxis, hoveredCell.IsValid() && hoveredCell.col == h.level, layout.columnCounts);
        }
        for (const HeaderLayout& h : layout.rowHeaders) {
            header(h, model.yAxis, hoveredCell.IsValid() && hoveredCell.row == h.level, layout.rowCounts);
        }
    }

    void UltraCanvasPerformanceMatrix::DrawAxes(IRenderContext* ctx) {
        const Rect2Dd& g = layout.grid;
        const Color ink = palette.mutedText;
        auto arrowhead = [&](const Point2Dd& tip, double dx, double dy) {
            // dx, dy: the unit direction the arrow points in.
            const double s = 7.0;
            ctx->SetFillPaint(ink);
            ctx->FillLinePath({tip,
                               Point2Dd(tip.x - dx * s * 1.4 - dy * s * 0.7, tip.y - dy * s * 1.4 + dx * s * 0.7),
                               Point2Dd(tip.x - dx * s * 1.4 + dy * s * 0.7, tip.y - dy * s * 1.4 - dx * s * 0.7)});
        };
        ctx->SetStrokePaint(ink);
        ctx->SetStrokeWidth(2.0f);
        if (showAxisArrows) {
            // Each arrow points to its axis's high end.
            const bool upward = !model.yAxis.reversed;
            ctx->DrawLine(Point2Dd(layout.yArrowX, g.y + g.height), Point2Dd(layout.yArrowX, g.y));
            arrowhead(Point2Dd(layout.yArrowX, upward ? g.y : g.y + g.height), 0.0, upward ? -1.0 : 1.0);
            const bool rightward = !model.xAxis.reversed;
            ctx->DrawLine(Point2Dd(g.x, layout.xArrowY), Point2Dd(g.x + g.width, layout.xArrowY));
            arrowhead(Point2Dd(rightward ? g.x + g.width : g.x, layout.xArrowY), rightward ? 1.0 : -1.0, 0.0);
        }
        ctx->SetFontSize(13.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(palette.text);
        if (!model.yAxis.title.empty() && layout.yTitleBox.width > 0.0) {
            const Size2Di s = ctx->GetTextLineDimensions(model.yAxis.title);
            ctx->PushState();
            ctx->Translate(layout.yTitleBox.x + layout.yTitleBox.width * 0.5,
                           layout.yTitleBox.y + layout.yTitleBox.height * 0.5);
            ctx->Rotate(-M_PI / 2.0);
            ctx->DrawText(model.yAxis.title, Point2Dd(-s.width * 0.5, -s.height * 0.5));
            ctx->PopState();
        }
        if (!model.xAxis.title.empty() && layout.xTitleBox.height > 0.0) {
            const Size2Di s = ctx->GetTextLineDimensions(model.xAxis.title);
            ctx->DrawText(model.xAxis.title,
                          Point2Dd(layout.xTitleBox.x + (layout.xTitleBox.width - s.width) * 0.5,
                                   layout.xTitleBox.y + (layout.xTitleBox.height - s.height) * 0.5));
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// INTERACTION
// =============================================================================

    std::string UltraCanvasPerformanceMatrix::BuildCellTooltip(const PerformanceCellRef& ref) const {
        if (!model.Contains(ref.row, ref.col)) return std::string();
        std::string text;
        auto addLine = [&text](const std::string& line) {
            if (line.empty()) return;
            if (!text.empty()) text += "\n";
            text += line;
        };
        if (const PerformanceCell* cell = model.FindCell(ref.row, ref.col)) {
            std::string head = cell->code;
            if (!cell->title.empty()) head += (head.empty() ? "" : "  ") + cell->title;
            if (cell->badge > 0 && head.empty()) head = std::to_string(cell->badge);
            addLine(head);
            addLine(cell->description);
            for (const std::string& bullet : cell->bullets) addLine("\xE2\x80\xA2 " + bullet);
            addLine(cell->action);
            addLine(cell->note);
        }
        const auto& y = model.yAxis.levels[static_cast<size_t>(ref.row)];
        const auto& x = model.xAxis.levels[static_cast<size_t>(ref.col)];
        addLine((model.yAxis.title.empty() ? std::string() : model.yAxis.title + ": ") + y.label + "  \xC2\xB7  " +
                (model.xAxis.title.empty() ? std::string() : model.xAxis.title + ": ") + x.label);
        if (HasPopulation()) {
            const std::vector<int> counts = model.CountsPerCell(hiddenGroups);
            const int count = counts[static_cast<size_t>(ref.row * model.Columns() + ref.col)];
            int total = 0;
            for (int n : counts) total += n;
            total = std::max(1, total);
            std::string line = std::to_string(count) + (count == 1 ? " item, " : " items, ") +
                               PercentText(static_cast<double>(count) / total);
            const size_t index = static_cast<size_t>(ref.row * model.Columns() + ref.col);
            if (model.targetDistribution.size() == static_cast<size_t>(model.Rows() * model.Columns())) {
                line += " (target " + PercentText(model.targetDistribution[index]) + ")";
            }
            addLine(line);
        }
        return text;
    }

    std::string UltraCanvasPerformanceMatrix::BuildItemTooltip(size_t index) const {
        if (index >= model.items.size()) return std::string();
        const PerformanceMatrixItem& item = model.items[index];
        std::string text = item.label;
        if (!item.group.empty()) text += "\n" + item.group;
        const PerformanceCellRef cell = model.CellOf(item);
        if (model.Contains(cell.row, cell.col)) {
            const PerformanceCell* data = model.FindCell(cell.row, cell.col);
            if (data && !data->title.empty()) text += "\n" + data->title;
            text += "\n" + (model.yAxis.title.empty() ? std::string() : model.yAxis.title + ": ") +
                    model.yAxis.levels[static_cast<size_t>(cell.row)].label + "  \xC2\xB7  " +
                    (model.xAxis.title.empty() ? std::string() : model.xAxis.title + ": ") +
                    model.xAxis.levels[static_cast<size_t>(cell.col)].label;
        }
        if (std::isfinite(item.xScore) && std::isfinite(item.yScore)) {
            text += "\nScores: " + Score(item.xScore) + " / " + Score(item.yScore);
        }
        if (!item.note.empty()) text += "\n" + item.note;
        return text;
    }

    bool UltraCanvasPerformanceMatrix::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;
        const PerformanceCellRef found = CellAt(mousePos);
        if (found != hoveredCell) {
            hoveredCell = found;
            if (onCellHover) onCellHover(hoveredCell);
            RequestRedraw();
        }
        const int item = ItemAt(mousePos);
        if (item != hoveredItem) {
            hoveredItem = item;
            RequestRedraw();
        }
        if (enableTooltips) {
            if (hoveredItem >= 0 || hoveredCell.IsValid()) {
                const Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                std::string tip = hoveredItem >= 0 ? BuildItemTooltip(static_cast<size_t>(hoveredItem))
                                                   : BuildCellTooltip(hoveredCell);
                if (hoveredItem < 0 && onOverflowClick && OverflowAt(mousePos).IsValid()) {
                    tip += "\nClick for everyone in this box";
                } else if (hoveredItem >= 0 && CanMoveItems()) {
                    tip += "\nDrag to another box to move";
                }
                UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tip, MapFromLocal(tipPos, nullptr));
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }
        return hoveredCell.IsValid() || hoveredItem >= 0;
    }

    PerformanceCellRef UltraCanvasPerformanceMatrix::Neighbour(const PerformanceCellRef& from, int dx, int dy) const {
        if (!model.Contains(from.row, from.col)) return from;
        // DisplayRow and DisplayColumn are their own inverses: screen order
        // back to model order.
        const int screenCol = std::clamp(DisplayColumn(from.col) + dx, 0, model.Columns() - 1);
        const int screenRow = std::clamp(DisplayRow(from.row) + dy, 0, model.Rows() - 1);
        return PerformanceCellRef(DisplayRow(screenRow), DisplayColumn(screenCol));
    }

    bool UltraCanvasPerformanceMatrix::HandleKeyDown(const UCEvent& event) {
        if (model.Rows() == 0 || model.Columns() == 0) return false;
        int dx = 0, dy = 0;
        switch (event.virtualKey) {
            case UCKeys::Left:  dx = -1; break;
            case UCKeys::Right: dx = 1;  break;
            case UCKeys::Up:    dy = -1; break;
            case UCKeys::Down:  dy = 1;  break;
            case UCKeys::Return:
            case UCKeys::NumPadEnter:
            case UCKeys::Space:
                // Activate: the selected item if there is one, else the cell.
                if (selectedItem >= 0) {
                    if (onItemClick) onItemClick(model.items[static_cast<size_t>(selectedItem)]);
                    return true;
                }
                if (selectedCell.IsValid()) {
                    if (onCellClick) onCellClick(selectedCell);
                    return true;
                }
                return false;
            case UCKeys::Escape:
                // One step back at a time: the drag, the item, the cell.
                if (pressedItem >= 0) { EndDrag(); return true; }
                if (selectedItem >= 0) { SetSelectedItem(-1); return true; }
                if (selectedCell.IsValid()) { SetSelectedCell(PerformanceCellRef()); return true; }
                return false;
            default:
                return false;
        }

        if ((event.ctrl || event.meta) && selectedItem >= 0 && CanMoveItems()) {
            const PerformanceCellRef from = model.CellOf(model.items[static_cast<size_t>(selectedItem)]);
            const PerformanceCellRef to = Neighbour(from, dx, dy);
            if (to != from && MoveItem(static_cast<size_t>(selectedItem), to)) SetSelectedCell(to);
            return true;
        }
        if (!enableSelection) return false;
        PerformanceCellRef start = selectedCell;
        if (!start.IsValid() && selectedItem >= 0) start = model.CellOf(model.items[static_cast<size_t>(selectedItem)]);
        // The first arrow press selects the top-left cell on screen.
        SetSelectedCell(start.IsValid() ? Neighbour(start, dx, dy)
                                        : PerformanceCellRef(DisplayRow(0), DisplayColumn(0)));
        return true;
    }

    bool UltraCanvasPerformanceMatrix::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;
        switch (event.type) {
            case UCEventType::KeyDown:
                if (HandleKeyDown(event)) return true;
                break;

            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left || !layout.valid) break;
                if (GetWindow()) SetFocus(true);
                const Point2Di at(event.pointer.x, event.pointer.y);
                // The legend first: a click there shows or hides a group.
                const size_t entry = LegendEntryAt(at);
                const std::vector<std::string> groups = model.Groups();
                if (entry != SIZE_MAX && entry < groups.size()) {
                    SetGroupVisible(groups[entry], !IsGroupVisible(groups[entry]));
                    return true;
                }
                const int item = ItemAt(at);
                if (item >= 0) {
                    SetSelectedItem(item);
                    if (CanMoveItems()) {
                        // Maybe a drag: the click waits for the release.
                        pressedItem = item;
                        pressPoint = dragPoint = at;
                        dragging = false;
                        if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
                        return true;
                    }
                    if (onItemClick) onItemClick(model.items[static_cast<size_t>(item)]);
                    return true;
                }
                const PerformanceCellRef overflow = OverflowAt(at);
                if (overflow.IsValid() && onOverflowClick) {
                    onOverflowClick(overflow, GetCellItems(overflow));
                    return true;
                }
                const PerformanceCellRef clicked = CellAt(at);
                if (!clicked.IsValid()) break;
                SetSelectedItem(-1);
                if (enableSelection) SetSelectedCell(clicked == selectedCell ? PerformanceCellRef() : clicked);
                if (onCellClick) onCellClick(clicked);
                return true;
            }

            case UCEventType::MouseMove:
                if (pressedItem >= 0) {
                    const Point2Di at(event.pointer.x, event.pointer.y);
                    dragPoint = at;
                    // A few pixels of wobble in a click are not a drag.
                    if (!dragging && std::abs(at.x - pressPoint.x) + std::abs(at.y - pressPoint.y) > 4) {
                        dragging = true;
                        hoveredItem = -1;
                        UltraCanvasTooltipManager::HideTooltip();
                    }
                    if (dragging) {
                        dropTarget = CellAt(at);
                        RequestRedraw();
                    }
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (pressedItem >= 0 && event.button == UCMouseButton::Left) {
                    const int item = pressedItem;
                    const bool dropped = dragging;
                    const PerformanceCellRef target = dropTarget;
                    EndDrag();
                    if (dropped) {
                        if (target.IsValid() && MoveItem(static_cast<size_t>(item), target)) {
                            SetSelectedCell(target);
                        }
                    } else if (onItemClick && item < static_cast<int>(model.items.size())) {
                        onItemClick(model.items[static_cast<size_t>(item)]);
                    }
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseLeave:
                if (hoveredCell.IsValid() || hoveredItem >= 0) {
                    hoveredCell = PerformanceCellRef();
                    hoveredItem = -1;
                    if (onCellHover) onCellHover(hoveredCell);
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

    std::shared_ptr<UltraCanvasPerformanceMatrix> CreatePerformanceMatrix(const std::string& id, int x, int y,
                                                                          int width, int height) {
        return std::make_shared<UltraCanvasPerformanceMatrix>(id, x, y, width, height);
    }

} // namespace UltraCanvas

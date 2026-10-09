// Apps/DemoApp/UltraCanvasListViewDashboardExamples.cpp
// The domain dashboard on UltraCanvasListView: one row per domain, painted by
// a custom IItemDelegate - a link, a status, an action, a sparkline, a
// number, a plan and a row menu - with clickable cells, hover, sortable
// columns and a model the menu edits.
//
// It replaces the demo's old "Templates demo" (UltraCanvasTableDemo.cpp),
// which built eight elements and a line chart per row in a container: right
// for a handful of rows, wrong for a table, and it had stopped compiling. A
// list view paints only the rows on screen, so "Add 1,000 domains" costs
// nothing to scroll.
//
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

    namespace {

        struct DomainRecord {
            std::string domain;
            bool active = true;
            bool insightsEnabled = false;
            std::vector<float> history;   // unique visitors per day, oldest first
            int visitors = 0;             // the figure the row shows
            std::string plan = "Free";
        };

        enum DomainColumn {
            ColDomain, ColStatus, ColInsights, ColTrend, ColVisitors, ColPlan, ColMenu, ColCount
        };

        constexpr int kRowHeight = 44;
        constexpr int kCellPadding = 10;

        const Color kLinkColor(0, 102, 204);
        const Color kLinkHoverColor(0, 72, 160);
        const Color kActiveColor(34, 139, 34);
        const Color kInactiveColor(150, 150, 150);
        const Color kTextColor(40, 40, 40);
        const Color kMutedColor(110, 110, 110);
        const Color kProColor(156, 39, 176);
        const Color kSparkColor(52, 152, 219);
        const Color kSparkFill(52, 152, 219, 40);

        // "150.0k", "2.5M": integer arithmetic, so the figure reads the same
        // whatever the locale's decimal separator.
        std::string FormatVisitors(int visitors) {
            if (visitors >= 1000000) {
                const int tenths = (visitors + 50000) / 100000;
                return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "M";
            }
            if (visitors >= 1000) {
                const int tenths = (visitors + 50) / 100;
                return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "k";
            }
            return std::to_string(visitors);
        }

        // Twenty days of traffic ending today at `base` - the figure the row
        // shows - walked backwards, the same for the same domain on every run
        // (the seed is the name), so the page looks the same each time.
        std::vector<float> TrafficHistory(const std::string& domain, int base) {
            std::mt19937 gen(static_cast<std::mt19937::result_type>(std::hash<std::string>{}(domain)));
            std::uniform_real_distribution<float> step(-0.12f, 0.12f);
            std::vector<float> history(20);
            float value = static_cast<float>(base);
            for (int day = 19; day >= 0; --day) {
                history[static_cast<size_t>(day)] = value;
                value = std::max(base * 0.4f, value * (1.0f + step(gen)));
            }
            return history;
        }

        DomainRecord MakeDomain(const std::string& domain, int visitors, const std::string& plan = "Free") {
            DomainRecord r;
            r.domain = domain;
            r.visitors = visitors;
            r.history = TrafficHistory(domain, visitors);
            r.plan = plan;
            return r;
        }

        // ===== MODEL =====
        // Seven columns over a vector of records. The view reads text and
        // tooltips through GetData; the delegate reads the record itself
        // (the sparkline needs the history, which is not a string).
        class DomainListModel : public IListModel {
        public:
            std::vector<DomainRecord> rows;

            int GetRowCount() const override { return static_cast<int>(rows.size()); }
            int GetColumnCount() const override { return ColCount; }

            ListColumnDef GetColumnDef(int column) const override {
                switch (column) {
                    // 925 px in all: the 960 px list less its borders and
                    // the scrollbar that "Add 1,000 domains" brings in.
                    case ColDomain:   return {"Domain", 265, TextAlignment::Left, "Click a domain to open it in the browser"};
                    case ColStatus:   return {"Status", 95, TextAlignment::Left};
                    case ColInsights: return {"Security insights", 135, TextAlignment::Left};
                    case ColTrend:    return {"Unique visitors", 210, TextAlignment::Left, "The last 20 days; sorted by growth"};
                    case ColVisitors: return {"Visitors", 95, TextAlignment::Right};
                    case ColPlan:     return {"Plan", 75, TextAlignment::Left};
                    case ColMenu:     return {"", 50, TextAlignment::Center, "More actions"};
                    default:          return {"", 100};
                }
            }

            ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
                if (index.row < 0 || index.row >= GetRowCount()) return {};
                const DomainRecord& r = rows[static_cast<size_t>(index.row)];
                if (role == ListDataRole::DisplayRole) {
                    switch (index.column) {
                        case ColDomain:   return r.domain;
                        case ColStatus:   return std::string(r.active ? "\xE2\x9C\x93 Active" : "\xE2\x9C\x95 Inactive");
                        case ColInsights: return std::string(r.insightsEnabled ? "\xE2\x9C\x93 Enabled" : "Enable");
                        case ColVisitors: return FormatVisitors(r.visitors);
                        case ColPlan:     return r.plan;
                        case ColMenu:     return std::string("\xE2\x8B\xAE");   // ⋮
                        default:          return {};
                    }
                }
                if (role == ListDataRole::ToolTipRole) {
                    switch (index.column) {
                        case ColDomain:   return "Open https://" + r.domain;
                        case ColInsights: return r.insightsEnabled ? std::string("Security insights are on")
                                                                   : "Turn on security insights for " + r.domain;
                        case ColTrend: {
                            const auto [lo, hi] = std::minmax_element(r.history.begin(), r.history.end());
                            return "Last 20 days: low " + FormatVisitors(static_cast<int>(*lo)) +
                                   ", high " + FormatVisitors(static_cast<int>(*hi));
                        }
                        case ColMenu:     return std::string("More actions for ") + r.domain;
                        default:          return {};
                    }
                }
                return {};
            }

            bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

            int FindRow(const std::string& domain) const {
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].domain == domain) return static_cast<int>(i);
                return -1;
            }

            void RowChanged(int row) { NotifyRowChanged(row); }
            void Changed() { NotifyDataChanged(); }

            // The model orders its rows; the view only shows the indicator.
            bool SortBy(int column, bool ascending) {
                auto growth = [](const DomainRecord& r) {
                    return r.history.empty() || r.history.front() <= 0 ? 0.0f
                                                                       : r.history.back() / r.history.front();
                };
                std::function<bool(const DomainRecord&, const DomainRecord&)> less;
                switch (column) {
                    case ColDomain:   less = [](auto& a, auto& b) { return a.domain < b.domain; }; break;
                    case ColStatus:   less = [](auto& a, auto& b) { return a.active > b.active; }; break;
                    case ColInsights: less = [](auto& a, auto& b) { return a.insightsEnabled > b.insightsEnabled; }; break;
                    case ColTrend:    less = [growth](auto& a, auto& b) { return growth(a) < growth(b); }; break;
                    case ColVisitors: less = [](auto& a, auto& b) { return a.visitors < b.visitors; }; break;
                    case ColPlan:     less = [](auto& a, auto& b) { return a.plan < b.plan; }; break;
                    default:          return false;
                }
                std::stable_sort(rows.begin(), rows.end(), [&](const DomainRecord& a, const DomainRecord& b) {
                    return ascending ? less(a, b) : less(b, a);
                });
                NotifyDataChanged();
                return true;
            }
        };

        // ===== DELEGATE =====
        // Paints each cell from the record. The view has already painted the
        // row's background (selection, hover); this draws the content only.
        // Two cells hold something to click - the domain (a link) and
        // "Enable" (an action) - and the ⋮ cell opens the row's menu; their
        // text widths are measured here, while painting, so a click can tell
        // the text from the empty part of its cell.
        class DomainRowDelegate : public IItemDelegate {
        public:
            int hoverRow = -1;
            int hoverColumn = -1;
            bool hoverOnTarget = false;

            void RenderItem(IRenderContext* ctx, const IListModel* model, int row, int column,
                            const ListItemStyleOption& option) override {
                const auto* domains = dynamic_cast<const DomainListModel*>(model);
                if (!ctx || !domains || row < 0 || row >= domains->GetRowCount()) return;
                const DomainRecord& r = domains->rows[static_cast<size_t>(row)];
                const Rect2Dd cell(option.columnX + kCellPadding, option.rect.y,
                                   option.columnWidth - 2 * kCellPadding, option.rect.height);
                if (cell.width <= 0) return;

                if (column == ColTrend) {
                    DrawSparkline(ctx, r.history, Rect2Dd(cell.x, cell.y + 8, cell.width, cell.height - 16));
                    return;
                }

                const std::string text = GetStringValue(model->GetData({row, column}, ListDataRole::DisplayRole));
                if (text.empty()) return;

                const bool hovered = hoverRow == row && hoverColumn == column && hoverOnTarget;
                Color color = kTextColor;
                float size = 11.0f;
                FontWeight weight = FontWeight::Normal;
                switch (column) {
                    case ColDomain:   color = hovered ? kLinkHoverColor : kLinkColor; break;
                    case ColStatus:   color = r.active ? kActiveColor : kInactiveColor; size = 10.0f; break;
                    case ColInsights: color = r.insightsEnabled ? kMutedColor : (hovered ? kLinkHoverColor : kLinkColor); break;
                    case ColVisitors: weight = FontWeight::Bold; break;
                    case ColPlan:     color = r.plan == "Pro" ? kProColor : kMutedColor; size = 10.0f;
                                      weight = r.plan == "Pro" ? FontWeight::Bold : FontWeight::Normal; break;
                    case ColMenu:     color = hoverRow == row && hoverColumn == ColMenu ? kTextColor : kMutedColor;
                                      size = 16.0f; weight = FontWeight::Bold; break;
                    default: break;
                }

                ctx->SetFontSize(size);
                ctx->SetFontWeight(weight);
                ctx->SetTextWrap(TextWrap::WrapNone);
                ctx->SetTextAlignment(option.columnAlignment);
                ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
                ctx->SetTextPaint(color);
                ctx->DrawTextInRect(text, cell);

                if (column == ColDomain || (column == ColInsights && !r.insightsEnabled)) {
                    const int width = ctx->GetTextLineWidth(text);
                    textWidth[text] = width;
                    // A link shows it is one when the pointer is on it.
                    if (hovered) {
                        const double y = cell.y + cell.height / 2 + size * 0.75;
                        ctx->SetStrokePaint(color);
                        ctx->SetStrokeWidth(1.0);
                        ctx->DrawLine(Point2Dd(cell.x, y), Point2Dd(cell.x + std::min<double>(width, cell.width), y));
                    }
                }
                ctx->SetFontWeight(FontWeight::Normal);   // leave the context as found
            }

            int GetRowHeight(const IListModel*, int) const override { return kRowHeight; }

            // Whether a point in a cell (cell-local, as onCellClicked and
            // onCellHovered report it) is on what that cell lets you click.
            bool OnTarget(const DomainListModel& model, int row, int column, const Point2Di& inCell) const {
                if (row < 0 || row >= model.GetRowCount()) return false;
                const DomainRecord& r = model.rows[static_cast<size_t>(row)];
                if (column == ColMenu) return true;
                if (column != ColDomain && !(column == ColInsights && !r.insightsEnabled)) return false;
                const std::string text = column == ColDomain ? r.domain : std::string("Enable");
                const auto it = textWidth.find(text);
                const int width = it != textWidth.end() ? it->second : 0;
                return inCell.x >= kCellPadding - 2 && inCell.x <= kCellPadding + width + 2 &&
                       inCell.y >= kRowHeight / 2 - 10 && inCell.y <= kRowHeight / 2 + 10;
            }

        private:
            std::unordered_map<std::string, int> textWidth;

            static void DrawSparkline(IRenderContext* ctx, const std::vector<float>& values, const Rect2Dd& box) {
                if (values.size() < 2 || box.width <= 0 || box.height <= 0) return;
                const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
                const double span = std::max(1.0f, *hi - *lo);
                std::vector<Point2Dd> points;
                for (size_t i = 0; i < values.size(); ++i) {
                    const double x = box.x + box.width * static_cast<double>(i) / (values.size() - 1);
                    const double y = box.y + box.height - (values[i] - *lo) / span * box.height;
                    points.emplace_back(x, y);
                }
                // The area under the line, then the line.
                ctx->ClearPath();
                ctx->MoveTo(points.front().x, box.y + box.height);
                for (const auto& p : points) ctx->LineTo(p.x, p.y);
                ctx->LineTo(points.back().x, box.y + box.height);
                ctx->ClosePath();
                ctx->SetFillPaint(kSparkFill);
                ctx->Fill();
                ctx->ClearPath();
                ctx->SetStrokePaint(kSparkColor);
                ctx->SetStrokeWidth(1.6);
                ctx->DrawLinePath(points, false);
            }
        };

        // What the page's callbacks share. It holds the list view raw: the
        // view owns the callbacks that hold this, so a shared_ptr to it here
        // would be a cycle neither end escapes.
        struct DashboardState {
            std::shared_ptr<DomainListModel> model;
            std::shared_ptr<DomainRowDelegate> delegate;
            std::shared_ptr<UltraCanvasLabel> status;
            std::shared_ptr<UltraCanvasMenu> menu;   // the open row menu, kept alive while open
            UltraCanvasListView* list = nullptr;
            int sortColumn = -1;
            bool sortAscending = true;
            int added = 0;

            void Say(const std::string& text) const { if (status) status->SetText(text); }

            // The view selects by position, and rows move when the model is
            // re-sorted, grows or loses one: the selected row would become
            // whatever domain moved into its place. Take the selection by
            // name before such a change and put it back on that domain after.
            std::string SelectedDomain() const {
                if (!list || !list->GetSelection()) return {};
                const std::vector<int> selected = list->GetSelection()->GetSelectedRows();
                if (selected.empty() || selected.front() >= model->GetRowCount()) return {};
                return model->rows[static_cast<size_t>(selected.front())].domain;
            }

            void Reselect(const std::string& domain) {
                if (!list || !list->GetSelection()) return;
                const int row = domain.empty() ? -1 : model->FindRow(domain);
                if (row < 0) {
                    list->ResetSelection();
                } else {
                    list->GetSelection()->Select(row);
                    list->EnsureRowVisible(row);
                }
                list->RequestRedraw();
            }

            void OpenRowMenu(const std::string& domain, const Point2Di& at) {
                if (!list || !list->GetWindow()) return;
                const int row = model->FindRow(domain);
                if (row < 0) return;
                const DomainRecord& r = model->rows[static_cast<size_t>(row)];
                // Actions find the row again by name: sorting or removing rows
                // while the menu is open moves the one it was opened on.
                auto edit = [this, domain](const std::function<std::string(DomainRecord&)>& change) {
                    const int at = model->FindRow(domain);
                    if (at < 0) return;
                    Say(change(model->rows[static_cast<size_t>(at)]));
                    model->RowChanged(at);
                };
                menu = std::make_shared<UltraCanvasMenu>("DomainRowMenu", 0, 0, 220, 0);
                menu->SetMenuType(MenuType::PopupMenu);
                menu->AddItem(MenuItemData::Action("Open website", [this, domain]() {
                    OpenURL("https://" + domain);
                    Say("Opened https://" + domain);
                }));
                menu->AddItem(MenuItemData::Separator());
                menu->AddItem(MenuItemData::Action(r.active ? "Deactivate" : "Activate", [edit]() {
                    edit([](DomainRecord& d) {
                        d.active = !d.active;
                        return d.domain + (d.active ? " is active again" : " is deactivated");
                    });
                }));
                menu->AddItem(MenuItemData::Action(r.plan == "Pro" ? "Change to the Free plan" : "Upgrade to the Pro plan",
                                                   [edit]() {
                    edit([](DomainRecord& d) {
                        d.plan = d.plan == "Pro" ? "Free" : "Pro";
                        return d.domain + " is on the " + d.plan + " plan";
                    });
                }));
                menu->AddItem(MenuItemData::Separator());
                menu->AddItem(MenuItemData::Action("Remove", [this, domain]() {
                    const int at = model->FindRow(domain);
                    if (at < 0) return;
                    const std::string selected = SelectedDomain();
                    model->rows.erase(model->rows.begin() + at);
                    model->Changed();
                    Reselect(selected == domain ? std::string() : selected);
                    Say("Removed " + domain + " - " + std::to_string(model->GetRowCount()) + " domains");
                }));
                PopupElementSettings settings;
                menu->OpenMenu(at, *list->GetWindow(), settings);
            }
        };

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewDashboardExamples() {
        auto page = std::make_shared<UltraCanvasContainer>("ListViewDashboard", 0, 0, 1000, 720);
        page->SetBackgroundColor(Color(245, 245, 245));

        auto title = std::make_shared<UltraCanvasLabel>("DashboardTitle", 20, 10, 960, 32);
        title->SetText("Domain Management Dashboard - a list view with a custom delegate");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 40, 40));
        page->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("DashboardSubtitle", 20, 44, 960, 36);
        subtitle->SetText("Click a domain to open it, \"Enable\" to turn on security insights, \xE2\x8B\xAE (or right-click a row) "
                          "for its menu, and a column header to sort. Every row is painted by one IItemDelegate.");
        subtitle->SetFontSize(11);
        subtitle->SetWrap(TextWrap::WrapWordChar);
        subtitle->SetTextColor(Color(100, 100, 100));
        page->AddChild(subtitle);

        auto state = std::make_shared<DashboardState>();
        state->model = std::make_shared<DomainListModel>();
        state->model->rows = {
            MakeDomain("www.ultraos.eu", 150000),
            MakeDomain("www.tomtom.com", 500000),
            MakeDomain("www.futa.com", 75000),
            MakeDomain("www.godotengine.org", 180000),
            MakeDomain("www.duckduckgo.com", 2500000, "Pro"),
            MakeDomain("www.solar-aid.org", 45000),
            MakeDomain("www.democracynow.com", 320000),
            MakeDomain("www.firefox.org", 850000),
            MakeDomain("www.350.org", 125000),
            MakeDomain("www.doctorswithoutborders.org", 680000, "Pro"),
        };
        state->delegate = std::make_shared<DomainRowDelegate>();

        auto list = std::make_shared<UltraCanvasListView>("DomainList", 20, 86, 960, 520);
        ListViewStyle style = list->GetStyle();
        style.showHeader = true;
        style.showGridLines = true;
        style.headerHeight = 30;
        style.rowHeight = kRowHeight;
        // A light selection keeps the link, status and plan colours readable
        // on a selected row (the default selection is a strong blue).
        style.selectionBackgroundColor = Color(210, 230, 250);
        style.hoverBackgroundColor = Color(240, 246, 252);
        list->SetStyle(style);
        list->SetModel(state->model);
        list->SetDelegate(state->delegate);
        list->SetRowHeight(kRowHeight);
        state->list = list.get();
        page->AddChild(list);

        auto status = std::make_shared<UltraCanvasLabel>("DashboardStatus", 20, 616, 760, 30);
        status->SetText(std::to_string(state->model->GetRowCount()) + " domains");
        status->SetFontSize(11);
        status->SetBackgroundColor(Colors::White);
        status->SetBorders(1.0f);
        status->SetPadding(0, 8);
        state->status = status;
        page->AddChild(status);

        auto addMany = CreateButton("DashboardAddMany", 790, 616, 190, 30, "Add 1,000 domains");
        addMany->SetOnClick([state]() {
            const std::string selected = state->SelectedDomain();
            const int first = state->added;
            for (int i = 0; i < 1000; ++i) {
                const int n = first + i + 1;
                std::string name = std::to_string(n);
                name.insert(0, 4 - std::min<size_t>(4, name.size()), '0');
                state->model->rows.push_back(MakeDomain("site-" + name + ".example", 1000 + (n * 7919) % 900000));
            }
            state->added += 1000;
            if (state->sortColumn >= 0) state->model->SortBy(state->sortColumn, state->sortAscending);
            else state->model->Changed();
            state->Reselect(selected);
            state->Say(std::to_string(state->model->GetRowCount()) +
                       " domains - only the rows on screen are painted, so scrolling stays smooth");
        });
        page->AddChild(addMany);

        // ----- Cells -----
        list->onCellHovered = [state](int row, int column, const Point2Di& inCell) {
            auto& d = *state->delegate;
            const bool onTarget = row >= 0 && d.OnTarget(*state->model, row, column, inCell);
            if (d.hoverRow == row && d.hoverColumn == column && d.hoverOnTarget == onTarget) return;
            d.hoverRow = row;
            d.hoverColumn = column;
            d.hoverOnTarget = onTarget;
            if (state->list) {
                state->list->SetMouseCursor(onTarget ? UCMouseCursor::Hand : UCMouseCursor::Default);
                state->list->RequestRedraw();
            }
        };
        list->onCellClicked = [state](int row, int column, const Point2Di& inCell) {
            if (!state->delegate->OnTarget(*state->model, row, column, inCell)) return;
            DomainRecord& r = state->model->rows[static_cast<size_t>(row)];
            if (column == ColDomain) {
                OpenURL("https://" + r.domain);
                state->Say("Opened https://" + r.domain);
            } else if (column == ColInsights) {
                r.insightsEnabled = true;
                state->model->RowChanged(row);
                state->Say("Security insights are on for " + r.domain);
            } else if (column == ColMenu) {
                state->OpenRowMenu(r.domain, UltraCanvasApplication::GetInstance()->GetCurrentEvent().pointerWindow);
            }
        };
        list->onContextMenu = [state](int row, const UCEvent& event) {
            if (row < 0) return;
            state->OpenRowMenu(state->model->rows[static_cast<size_t>(row)].domain, event.pointerWindow);
        };

        // ----- Sorting: the model orders, the view shows the indicator -----
        list->onHeaderClicked = [state](int column) {
            if (column == ColMenu) return;
            state->sortAscending = state->sortColumn == column ? !state->sortAscending : column != ColVisitors;
            state->sortColumn = column;
            const std::string selected = state->SelectedDomain();
            if (state->model->SortBy(column, state->sortAscending) && state->list) {
                state->list->SetSortIndicator(column, state->sortAscending);
                state->Reselect(selected);
            }
        };

        return page;
    }

} // namespace UltraCanvas

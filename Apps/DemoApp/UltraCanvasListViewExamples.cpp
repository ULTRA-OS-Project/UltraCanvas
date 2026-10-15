// Apps/DemoApp/UltraCanvasListViewExamples.cpp
// ListView component demonstration examples: a simple list, a sortable
// multi-column list, a styled list, an icon list and - at the bottom of the
// page - the domain dashboard.
//
// The dashboard is one row per domain, painted by a custom IItemDelegate - a
// link, a status, an action, a sparkline, a number, a plan and a row menu -
// with clickable cells, hover, sortable columns and a model the menu edits.
// It replaced the demo's old "Templates demo" (UltraCanvasTableDemo.cpp),
// which built eight elements and a line chart per row in a container: right
// for a handful of rows, wrong for a table, and it had stopped compiling. A
// list view paints only the rows on screen, so "Add 1,000 domains" costs
// nothing to scroll.
//
// Version: 1.1.0 - the domain dashboard, until now a page of its own
//                 (UltraCanvasListViewDashboardExamples.cpp), is section 5
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextUtils.h"
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
                        // The tick and the cross before these, and the menu's three
                        // dots, are drawn by DomainRowDelegate: "✓", "✕" and "⋮"
                        // are not in the bundled UI font.
                        case ColStatus:   return std::string(r.active ? "Active" : "Inactive");
                        case ColInsights: return std::string(r.insightsEnabled ? "Enabled" : "Enable");
                        case ColVisitors: return FormatVisitors(r.visitors);
                        case ColPlan:     return r.plan;
                        case ColMenu:     return std::string("More actions");
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
                if (column == ColMenu) {
                    // Three dots, one above the other.
                    ctx->SetFillPaint(hoverRow == row && hoverColumn == ColMenu ? kTextColor : kMutedColor);
                    const Point2Dd centre(cell.x + cell.width / 2, cell.y + cell.height / 2);
                    for (int i = -1; i <= 1; ++i) ctx->FillCircle(Point2Dd(centre.x, centre.y + i * 5.0), 1.7);
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
                Rect2Dd textBox = cell;
                if (column == ColStatus || (column == ColInsights && r.insightsEnabled)) {
                    // A tick before the word, or a cross for an inactive domain.
                    DrawMark(ctx, column == ColInsights || r.active, color,
                             Point2Dd(cell.x + 5, cell.y + cell.height / 2));
                    textBox.x += 14;
                    textBox.width -= 14;
                }
                ctx->DrawTextInRect(text, textBox);

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

            static void DrawMark(IRenderContext* ctx, bool tick, const Color& color, const Point2Dd& c) {
                ctx->PushState();
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(1.6);
                ctx->SetLineCap(LineCap::Round);
                ctx->SetLineJoin(LineJoin::Round);
                ctx->ClearPath();
                if (tick) {
                    ctx->MoveTo(c.x - 4.0, c.y);
                    ctx->LineTo(c.x - 1.5, c.y + 2.8);
                    ctx->LineTo(c.x + 4.0, c.y - 3.2);
                } else {
                    ctx->MoveTo(c.x - 3.2, c.y - 3.2);
                    ctx->LineTo(c.x + 3.2, c.y + 3.2);
                    ctx->MoveTo(c.x + 3.2, c.y - 3.2);
                    ctx->LineTo(c.x - 3.2, c.y + 3.2);
                }
                ctx->Stroke();
                ctx->PopState();
            }

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

        // Section 5 of the page: the dashboard, its own status line and the
        // "Add 1,000 domains" button, 590 px from `top` down.
        void AddDomainDashboardSection(UltraCanvasContainer& page, int top) {
            auto section = std::make_shared<UltraCanvasLabel>("LVSection5", 20, top, 960, 25);
            section->SetText("5. Custom Delegate: Domain Dashboard");
            section->SetFontWeight(FontWeight::Bold);
            section->SetTextColor(Color(0, 100, 200, 255));
            page.AddChild(section);

            auto intro = std::make_shared<UltraCanvasLabel>("DashboardIntro", 20, top + 28, 960, 36);
            intro->SetText("Click a domain to open it, \"Enable\" to turn on security insights, the three dots (or right-click a row) "
                           "for its menu, and a column header to sort. Every row is painted by one IItemDelegate.");
            intro->SetFontSize(11);
            intro->SetWrap(TextWrap::WrapWordChar);
            intro->SetTextColor(Color(100, 100, 100));
            page.AddChild(intro);

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

            // Tall enough for the header and all ten rows, so the page scrolls
            // past the list until "Add 1,000 domains" gives it a scrollbar.
            auto list = std::make_shared<UltraCanvasListView>("DomainList", 20, top + 70, 960, 480);
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
            page.AddChild(list);

            auto status = std::make_shared<UltraCanvasLabel>("DashboardStatus", 20, top + 560, 760, 30);
            status->SetText(std::to_string(state->model->GetRowCount()) + " domains");
            status->SetFontSize(11);
            status->SetBackgroundColor(Colors::White);
            status->SetBorders(1.0f);
            status->SetPadding(0, 8);
            state->status = status;
            page.AddChild(status);

            auto addMany = CreateButton("DashboardAddMany", 790, top + 560, 190, 30, "Add 1,000 domains");
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
            page.AddChild(addMany);

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
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewExamples() {
        // Sections 1-4 end at y = 605 and the dashboard (section 5) runs from
        // 625 to 1215; the demo's display area scrolls down to it.
        auto container = std::make_shared<UltraCanvasContainer>("ListViewExamples", 0, 0, 1000, 1235);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ListViewTitle", 20, 10, 600, 35);
        title->SetText("ListView Component Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Subtitle
        // Two lines, and narrow enough to end before the status box at x = 600.
        auto subtitle = std::make_shared<UltraCanvasLabel>("ListViewSubtitle", 20, 45, 570, 40);
        subtitle->SetText("Simple, multi-column, styled and icon lists, and a dashboard below them —\n"
                          "hover a row or a column header for its tooltip");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // Status feedback label
        // Three lines of 11px text plus padding: the cell-click feedback below
        // is the tallest thing this label has to show.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("LVStatusLabel", 600, 10, 380, 74);
        statusLabel->SetText("Click any list item to see feedback here.\n"
                             "Hover a row (or a column header) to see its tooltip.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        container->AddChild(statusLabel);

        // ============================================================
        // Section 1: Simple List (Single Selection) — top-left
        // ============================================================
        auto section1 = std::make_shared<UltraCanvasLabel>("LVSection1", 20, 90, 460, 25);
        section1->SetText("1. Simple List (Single Selection)");
        section1->SetFontWeight(FontWeight::Bold);
        section1->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section1);

        auto simpleModel = std::make_shared<UltraCanvasSimpleListModel>();
        simpleModel->AddItem(ListItem("Apple", "", "A common red fruit"));
        simpleModel->AddItem(ListItem("Banana", "", "A yellow tropical fruit"));
        simpleModel->AddItem(ListItem("Cherry", "", "Small red stone fruit"));
        simpleModel->AddItem(ListItem("Date", "", "Sweet desert fruit"));
        simpleModel->AddItem(ListItem("Elderberry", "", "Small dark purple berry"));
        simpleModel->AddItem(ListItem("Fig", "", "Soft pear-shaped fruit"));
        simpleModel->AddItem(ListItem("Grape", "", "Small round berry"));
        simpleModel->AddItem(ListItem("Honeydew", "", "Green melon variety"));
        simpleModel->AddItem(ListItem("Kiwi", "", "Brown fuzzy fruit"));
        simpleModel->AddItem(ListItem("Lemon", "", "Yellow citrus fruit"));

        auto simpleList = std::make_shared<UltraCanvasListView>("SimpleListView", 20, 125, 460, 225);
        simpleList->SetModel(simpleModel);
        simpleList->SetRowHeight(22);

        simpleList->onItemClicked = [statusLabel, simpleModel](int row) {
            const auto& item = simpleModel->GetItem(row);
            statusLabel->SetText("Simple List: Clicked row " + std::to_string(row) +
                                 "\nItem: " + item.label +
                                 "\nTooltip: " + item.tooltip);
        };
        simpleList->onItemDoubleClicked = [statusLabel, simpleModel](int row) {
            statusLabel->SetText("Simple List: Double-clicked '" + simpleModel->GetItem(row).label + "'");
        };

        container->AddChild(simpleList);

        auto desc1 = std::make_shared<UltraCanvasLabel>("SimpleListDesc", 20, 355, 460, 20);
        desc1->SetText("Single-selection list with 10 fruit items — hover a row for its description");
        desc1->SetFontSize(10);
        desc1->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc1);

        // ============================================================
        // Section 2: Multi-Column List with Header — top-right
        // ============================================================
        auto section2 = std::make_shared<UltraCanvasLabel>("LVSection2", 500, 90, 320, 25);
        section2->SetText("2. Multi-Column List with Header");
        section2->SetFontWeight(FontWeight::Bold);
        section2->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section2);

        auto multiModel = std::make_shared<UltraCanvasMultiColumnListModel>();
        // The 4th ListColumnDef argument is the header tooltip, shown when the
        // pointer rests on that column's header cell.
        multiModel->AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left,
                                            "Name of the file on disk — click to sort"));
        multiModel->AddColumn(ListColumnDef("Type", 90, TextAlignment::Left,
                                            "File type, derived from the extension — click to sort"));
        multiModel->AddColumn(ListColumnDef("Size", 70, TextAlignment::Right,
                                            "Size on disk, rounded to one decimal — click to sort"));
        multiModel->AddColumn(ListColumnDef("Modified", 110, TextAlignment::Left,
                                            "Date of the last write, YYYY-MM-DD — click to sort"));

        // Rows carry both a row-wide tooltip and per-column tooltips: hovering
        // a cell shows that column's text, and any column left without one
        // falls back to the row tooltip.
        struct FileRow {
            std::vector<std::string> cells;
            std::string path;
            std::string typeTip;
            std::string bytes;
            std::string modifiedTip;
        };
        const std::vector<FileRow> fileRows = {
            {{"main.cpp", "C++ Source", "2.4 KB", "2025-03-15"}, "src/main.cpp",
             "C++ translation unit, compiled by the build", "2,458 bytes", "15 Mar 2025, 09:14"},
            {{"utils.h", "C++ Header", "1.1 KB", "2025-03-14"}, "src/utils.h",
             "C++ header, included by other units", "1,132 bytes", "14 Mar 2025, 17:02"},
            {{"README.md", "Markdown", "3.8 KB", "2025-03-10"}, "README.md",
             "Markdown document", "3,890 bytes", "10 Mar 2025, 11:47"},
            {{"Makefile", "Build Script", "0.9 KB", "2025-02-28"}, "Makefile",
             "Build script, run by make", "921 bytes", "28 Feb 2025, 08:30"},
            {{"config.json", "JSON", "0.5 KB", "2025-03-01"}, "etc/config.json",
             "JSON configuration data", "512 bytes", "1 Mar 2025, 22:05"},
            {{"test_main.cpp", "C++ Source", "4.2 KB", "2025-03-12"}, "tests/test_main.cpp",
             "C++ translation unit, compiled by the build", "4,301 bytes", "12 Mar 2025, 13:20"},
            {{"logo.png", "PNG Image", "45.6 KB", "2025-01-20"}, "media/logo.png",
             "PNG bitmap image", "46,694 bytes", "20 Jan 2025, 16:58"},
            {{"CHANGELOG.md", "Markdown", "12.3 KB", "2025-03-15"}, "Docs/CHANGELOG.md",
             "Markdown document", "12,595 bytes", "15 Mar 2025, 09:41"},
        };
        for (const auto& row : fileRows) {
            MultiColumnListItem item(row.cells);
            item.tooltip = row.path;                    // row-wide fallback
            item.SetCellTooltip(1, row.typeTip);
            item.SetCellTooltip(2, row.bytes);
            item.SetCellTooltip(3, row.modifiedTip);    // column 0 uses the path
            multiModel->AddItem(item);
        }

        // Sorting: the model keeps its rows in insertion order, and the view is
        // handed a UltraCanvasListSortFilterProxy in front of it, which presents
        // them re-ordered. Every row index the view reports is a *proxy* row —
        // map it with MapToSource() before looking the record up in the model.
        auto multiProxy = std::make_shared<UltraCanvasListSortFilterProxy>(multiModel);
        multiProxy->SetColumnSortKind(0, ListSortKind::Natural);   // "file2" before "file10"
        // "2.4 KB" is not a plain number (the unit is a letter), so the Size
        // column gets a comparator that reads the number in front of the unit.
        multiProxy->SetColumnComparator(2, [](const IListModel& source, int left, int right, int column) {
            auto kilobytes = [&](int row) {
                double kb = 0;
                TryParseFloat(GetStringValue(source.GetData({row, column}, ListDataRole::DisplayRole)), kb);
                return kb;
            };
            const double a = kilobytes(left), b = kilobytes(right);
            return a < b ? -1 : (b < a ? 1 : 0);
        });
        // Type and Modified use the default (Auto): case-insensitive text, and
        // ISO dates sort correctly as text.

        auto multiList = std::make_shared<UltraCanvasListView>("MultiColumnListView", 500, 125, 480, 225);
        multiList->SetModel(multiProxy);

        ListViewStyle multiStyle;
        multiStyle.headerFontSize = 10;
        multiStyle.showHeader = true;
        multiStyle.showGridLines = true;
        multiStyle.headerHeight = 26;
        multiStyle.rowHeight = 22;
        multiList->SetStyle(multiStyle);

        auto multiListDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        multiListDelegate->SetFontSize(10);
        multiList->SetDelegate(multiListDelegate);

        auto multiSelection = std::make_shared<UltraCanvasMultiSelection>();
        multiList->SetSelection(multiSelection);

        multiList->onCellClicked = [statusLabel, multiModel, multiProxy](int row, int column, const Point2Di&) {
            const int sourceRow = multiProxy->MapToSource(row);
            if (sourceRow < 0) return;
            const auto& item = multiModel->GetItem(sourceRow);
            statusLabel->SetText("Multi-Column: Clicked row " + std::to_string(row) +
                                 " (model row " + std::to_string(sourceRow) + ")" +
                                 ", column " + std::to_string(column) +
                                 " (" + multiModel->GetColumnDef(column).title + ")" +
                                 "\nFile: " + item.labels[0] +
                                 "\nCell tooltip: " + item.GetCellTooltip(column));
        };
        multiList->onSelectionChanged = [statusLabel, multiModel, multiProxy](const std::vector<int>& rows) {
            std::string names;
            for (size_t i = 0; i < rows.size(); i++) {
                const int sourceRow = multiProxy->MapToSource(rows[i]);
                if (sourceRow < 0) continue;
                if (!names.empty()) names += ", ";
                names += multiModel->GetItem(sourceRow).labels[0];
            }
            statusLabel->SetText("Multi-Column: " + std::to_string(rows.size()) +
                                 " items selected\nFiles: " + names);
        };

        // Sorting can be switched on and off. While it is on, clicking a header
        // sorts by that column ascending, clicking the sorted column again
        // turns it round, and the triangle in the header cell follows
        // SetSortIndicator. Switching it off restores the model's own order.
        auto sortCheckbox = std::make_shared<UltraCanvasCheckbox>("MultiColSortable", 830, 90, 150, 24,
                                                                  "Sortable columns");
        sortCheckbox->SetChecked(true);
        container->AddChild(sortCheckbox);

        std::weak_ptr<UltraCanvasListView> multiListWeak = multiList;
        std::weak_ptr<UltraCanvasCheckbox> sortCheckboxWeak = sortCheckbox;
        multiList->onHeaderClicked = [multiListWeak, sortCheckboxWeak, multiModel, multiProxy, statusLabel](int column) {
            auto view = multiListWeak.lock();
            auto sortable = sortCheckboxWeak.lock();
            if (!view || !sortable || !sortable->IsChecked()) return;
            const bool ascending = !(column == view->GetSortColumn() && view->GetSortAscending());
            view->ResetSelection();     // proxy rows move under a sort
            multiProxy->SortByColumn(column, ascending ? ListSortOrder::Ascending
                                                       : ListSortOrder::Descending);
            view->SetSortIndicator(column, ascending);
            statusLabel->SetText("Multi-Column: sorted by " + multiModel->GetColumnDef(column).title +
                                 (ascending ? " (ascending)" : " (descending)") +
                                 "\nClick the header again to reverse the order.");
        };
        sortCheckbox->onStateChanged = [multiListWeak, multiProxy, statusLabel](CheckedState, CheckedState newState) {
            auto view = multiListWeak.lock();
            if (!view) return;
            if (newState == CheckedState::Checked) {
                statusLabel->SetText("Multi-Column: sorting on\nClick a column header to sort by it.");
                return;
            }
            view->ResetSelection();
            multiProxy->ClearSort();
            view->ClearSortIndicator();
            statusLabel->SetText("Multi-Column: sorting off\nRows are back in model order.");
        };

        container->AddChild(multiList);

        auto desc2 = std::make_shared<UltraCanvasLabel>("MultiColDesc", 500, 355, 480, 20);
        desc2->SetText("Click a header to sort (again to reverse), multi-select (Ctrl+Click), per-cell tooltips");
        desc2->SetFontSize(10);
        desc2->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc2);

        // ============================================================
        // Section 3: Styled List (Alternating Rows + Multi-Select) — bottom-left
        // ============================================================
        auto section3 = std::make_shared<UltraCanvasLabel>("LVSection3", 20, 385, 460, 25);
        section3->SetText("3. Styled List (Alternating Rows + Multi-Select)");
        section3->SetFontWeight(FontWeight::Bold);
        section3->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section3);

        // ListItem's third argument is the tooltip (ToolTipRole), shown on hover
        auto styledModel = std::make_shared<UltraCanvasSimpleListModel>();
        styledModel->AddItem(ListItem("Crimson Red", "", "#DC143C — rgb(220, 20, 60)"));
        styledModel->AddItem(ListItem("Sunset Orange", "", "#FD5E53 — rgb(253, 94, 83)"));
        styledModel->AddItem(ListItem("Golden Yellow", "", "#FFDF00 — rgb(255, 223, 0)"));
        styledModel->AddItem(ListItem("Lime Green", "", "#32CD32 — rgb(50, 205, 50)"));
        styledModel->AddItem(ListItem("Forest Green", "", "#228B22 — rgb(34, 139, 34)"));
        styledModel->AddItem(ListItem("Sky Blue", "", "#87CEEB — rgb(135, 206, 235)"));
        styledModel->AddItem(ListItem("Royal Purple", "", "#7851A9 — rgb(120, 81, 169)"));
        styledModel->AddItem(ListItem("Hot Pink", "", "#FF69B4 — rgb(255, 105, 180)"));
        styledModel->AddItem(ListItem("Chocolate Brown", "", "#7B3F00 — rgb(123, 63, 0)"));
        styledModel->AddItem(ListItem("Silver Gray", "", "#C0C0C0 — rgb(192, 192, 192)"));
        styledModel->AddItem(ListItem("Midnight Black", "", "#000000 — rgb(0, 0, 0)"));
        styledModel->AddItem(ListItem("Pearl White", "", "#EAE0C8 — rgb(234, 224, 200)"));

        auto styledList = std::make_shared<UltraCanvasListView>("StyledListView", 20, 420, 460, 160);
        styledList->SetModel(styledModel);

        ListViewStyle styledStyle;
        styledStyle.backgroundColor = Color(252, 252, 255);
        styledStyle.alternateRowColors = true;
        styledStyle.alternateRowColor = Color(240, 240, 248);
        styledStyle.rowHeight = 24;
        styledStyle.selectionBackgroundColor = Color(100, 60, 180);
        styledStyle.hoverBackgroundColor = Color(220, 210, 240);
        styledList->SetStyle(styledStyle);

        auto styledDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        styledDelegate->SetFontSize(13.0f);
        styledDelegate->SetTextPadding(10);
        styledDelegate->SetSelectedTextColor(Colors::White);
        styledList->SetDelegate(styledDelegate);

        auto styledSelection = std::make_shared<UltraCanvasMultiSelection>();
        styledList->SetSelection(styledSelection);

        styledList->onSelectionChanged = [statusLabel, styledModel](const std::vector<int>& rows) {
            std::string names;
            for (size_t i = 0; i < rows.size(); i++) {
                if (i > 0) names += ", ";
                names += styledModel->GetItem(rows[i]).label;
            }
            statusLabel->SetText("Styled List: " + std::to_string(rows.size()) +
                                 " items selected\nSelected: " + names);
        };

        container->AddChild(styledList);

        auto desc3 = std::make_shared<UltraCanvasLabel>("StyledListDesc", 20, 585, 460, 20);
        desc3->SetText("Alternating rows, purple theme, multi-select (Ctrl/Shift+Click), hover for hex value");
        desc3->SetFontSize(10);
        desc3->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc3);

        // ============================================================
        // Section 4: Custom Delegate with Icons — bottom-right
        // ============================================================
        auto section4 = std::make_shared<UltraCanvasLabel>("LVSection4", 500, 385, 480, 25);
        section4->SetText("4. Custom Delegate with Icons");
        section4->SetFontWeight(FontWeight::Bold);
        section4->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section4);

        std::string iconsDir = NormalizePath(GetResourcesDir() + "media/icons/");

        auto iconModel = std::make_shared<UltraCanvasSimpleListModel>();
        iconModel->AddItem(ListItem("C++", iconsDir + "cpp.png", "Systems programming language"));
        iconModel->AddItem(ListItem("Python", iconsDir + "python.png", "General-purpose scripting language"));
        iconModel->AddItem(ListItem("Java", iconsDir + "java.png", "Enterprise application language"));
        iconModel->AddItem(ListItem("JavaScript", iconsDir + "javascript.png", "Web scripting language"));
        iconModel->AddItem(ListItem("TypeScript", iconsDir + "typescript.png", "Typed JavaScript superset"));
        iconModel->AddItem(ListItem("Rust", iconsDir + "rust.png", "Memory-safe systems language"));
        iconModel->AddItem(ListItem("C#", iconsDir + "csharp.png", "Microsoft .NET language"));
        iconModel->AddItem(ListItem("Go", iconsDir + "go.png", "Google systems language"));

        auto iconList = std::make_shared<UltraCanvasListView>("IconListView", 500, 420, 480, 160);
        iconList->SetModel(iconModel);
        iconList->SetRowHeight(28);

        auto iconDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        iconDelegate->SetFontSize(13.0f);
        iconDelegate->SetIconSize(20);
        iconDelegate->SetIconSpacing(8);
        iconDelegate->SetTextPadding(8);
        iconDelegate->SetRowHeight(28);
        iconList->SetDelegate(iconDelegate);

        iconList->onItemClicked = [statusLabel, iconModel](int row) {
            const auto& item = iconModel->GetItem(row);
            statusLabel->SetText("Icon List: Selected '" + item.label + "'" +
                                 "\nTooltip: " + item.tooltip);
        };
        iconList->onItemDoubleClicked = [statusLabel, iconModel](int row) {
            statusLabel->SetText("Icon List: Activated '" + iconModel->GetItem(row).label + "'" +
                                 "\n(Double-click action would open details)");
        };

        container->AddChild(iconList);

        auto desc4 = std::make_shared<UltraCanvasLabel>("IconListDesc", 500, 585, 480, 20);
        desc4->SetText("Single-column with 20px icons, 28px rows — hover a row for its description");
        desc4->SetFontSize(10);
        desc4->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc4);

        // ============================================================
        // Section 5: Custom Delegate - the domain dashboard, full width below
        // ============================================================
        AddDomainDashboardSection(*container, 625);

        return container;
    }

} // namespace UltraCanvas

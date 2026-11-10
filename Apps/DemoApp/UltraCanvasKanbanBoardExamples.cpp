// Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp
// Kanban board demo. One tab per design preset plus an interactive editor tab
// (drag & drop, add/edit/delete cards, column rename) and a tab whose board is
// built from a Mermaid `kanban` text definition.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): title and
//                  description of the kit, the tabs in a card as wide as the
//                  page, tab descriptions as tall as their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

    const GanttDate kToday(2026, 7, 30);

// =============================================================================
// SAMPLE BOARDS
// =============================================================================

    // Sprint board with WIP limits, priorities, due dates, assignees, tags and
    // a blocked card. Each tab gets its own instance because selection, order
    // and editor changes live on the data source.
    std::shared_ptr<KanbanDataSource> BuildSprintBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        ds->SetCurrentDate(kToday);

        int backlog = ds->AddColumn(Tr("Backlog"));
        int todo    = ds->AddColumn(Tr("To Do"), 4);
        int doing   = ds->AddColumn(Tr("In Progress"), 3);
        int testing = ds->AddColumn(Tr("Testing"), 2);
        int done    = ds->AddColumn(Tr("Done"));

        int c;
        c = ds->AddCard(backlog, Tr("Add editor role"));
        ds->SetCardDescription(c, Tr("Role-based permissions for shared boards"));
        ds->SetCardPriority(c, KanbanPriority::Low);
        c = ds->AddCard(backlog, Tr("Install analytics plugin"));
        ds->SetCardAssignee(c, "Dana Reyes");
        ds->AddCardTag(c, Tr("infra"));
        c = ds->AddCard(backlog, Tr("Update invoice design"));
        ds->SetCardDueDate(c, GanttDate(2026, 8, 21));

        c = ds->AddCard(todo, Tr("Shopping cart is missing size"));
        ds->SetCardDescription(c, Tr("Cart summary drops the size attribute of "
                                     "configurable products"));
        ds->SetCardPriority(c, KanbanPriority::High);
        ds->SetCardDueDate(c, GanttDate(2026, 8, 4));
        ds->SetCardAssignee(c, "Iris Chen");
        ds->AddCardTag(c, Tr("bug"));
        c = ds->AddCard(todo, Tr("Educational article about colors"));
        ds->SetCardAssignee(c, "Sam Ortiz");
        ds->AddCardTag(c, Tr("content"));

        c = ds->AddCard(doing, Tr("Fix broken links in Resources"));
        ds->SetCardDescription(c, Tr("Nightly crawl reports 14 dead links"));
        ds->SetCardPriority(c, KanbanPriority::Urgent);
        ds->SetCardDueDate(c, GanttDate(2026, 7, 28));   // Overdue
        ds->SetCardAssignee(c, "Priya Nair");
        ds->AddCardTag(c, Tr("bug"));
        c = ds->AddCard(doing, Tr("New landing page hero"));
        ds->SetCardAssignee(c, "Iris Chen");
        ds->SetCardPriority(c, KanbanPriority::Normal);
        ds->SetCardBlocked(c, true, Tr("Waiting for brand assets"));

        c = ds->AddCard(testing, Tr("High contrast mode"));
        ds->SetCardPriority(c, KanbanPriority::High);
        ds->SetCardAssignee(c, Tr("QA Team"));
        c = ds->AddCard(testing, Tr("External commenting system"));
        ds->SetCardDueDate(c, GanttDate(2026, 8, 7));
        ds->SetCardAssignee(c, "Leo Brandt");

        c = ds->AddCard(done, Tr("Update favicons & metadata"));
        ds->SetCardPriority(c, KanbanPriority::Low);
        c = ds->AddCard(done, Tr("Reduce backup size"));
        ds->SetCardAssignee(c, "Dana Reyes");
        c = ds->AddCard(done, Tr("Set up campaign"));
        ds->AddCardTag(c, Tr("marketing"));

        return ds;
    }

    // WordLayouts-style poster: colored panels, TASK NN cards with due dates
    // and priority dots (reference image 2).
    std::shared_ptr<KanbanDataSource> BuildPosterBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        ds->SetCurrentDate(kToday);

        int backlog = ds->AddColumn(Tr("Backlog"));
        int todo    = ds->AddColumn(Tr("To Do"));
        int doing   = ds->AddColumn(Tr("In Progress"));
        int testing = ds->AddColumn(Tr("Testing"));
        int done    = ds->AddColumn(Tr("Complete"));

        struct Item { int column; const char* number; KanbanPriority priority; };
        const Item items[] = {
                {backlog, "01", KanbanPriority::Urgent},
                {backlog, "02", KanbanPriority::High},
                {backlog, "04", KanbanPriority::Low},
                {todo,    "03", KanbanPriority::High},
                {todo,    "05", KanbanPriority::Urgent},
                {doing,   "12", KanbanPriority::High},
                {doing,   "08", KanbanPriority::Low},
                {doing,   "09", KanbanPriority::Urgent},
                {doing,   "07", KanbanPriority::High},
                {testing, "06", KanbanPriority::Urgent},
                {testing, "10", KanbanPriority::Low},
                {done,    "11", KanbanPriority::High},
        };
        for (const Item& item : items) {
            int c = ds->AddCard(item.column, TrF("TASK {0}", item.number));
            ds->SetCardDescription(c, "In id sapien nec dui tempus venenatis "
                                      "aliquam quis velit.");
            ds->SetCardPriority(c, item.priority);
            ds->SetCardDueDate(c, GanttDate(2026, 8, 15));
        }
        return ds;
    }

    // Minimal schematic board: color-block cards without text details
    // (reference image 3).
    std::shared_ptr<KanbanDataSource> BuildSchematicBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        int pending = ds->AddColumn(Tr("Pending"));
        int design  = ds->AddColumn(Tr("Design"));
        int develop = ds->AddColumn(Tr("Develop"));
        int test    = ds->AddColumn(Tr("Test"));
        int deploy  = ds->AddColumn(Tr("Deploy"));

        ds->AddCard(pending, Tr("Brief"));
        ds->AddCard(design, Tr("Wireframes"));
        ds->AddCard(design, Tr("Mockups"));
        ds->AddCard(develop, Tr("API"));
        ds->AddCard(develop, Tr("Frontend"));
        ds->AddCard(test, Tr("Smoke tests"));
        ds->AddCard(test, Tr("Load tests"));
        ds->AddCard(deploy, Tr("Staging"));
        ds->AddCard(deploy, Tr("Production"));
        return ds;
    }

    // Portfolio timeline: status columns with quarter captions, swimlane rows,
    // one-line pill cards colored by column (reference image 5).
    std::shared_ptr<KanbanDataSource> BuildTimelineBoard() {
        auto ds = std::make_shared<KanbanDataSource>();

        int completed = ds->AddColumn(Tr("Completed"));
        int progress  = ds->AddColumn(Tr("In Progress"));
        int soon      = ds->AddColumn(Tr("Soon"));
        int future    = ds->AddColumn(Tr("Future"));
        ds->SetColumnFooterCaption(completed, Tr("Q1"));
        ds->SetColumnFooterCaption(progress, Tr("Q2"));
        ds->SetColumnFooterCaption(soon, Tr("Q3"));
        ds->SetColumnFooterCaption(future, Tr("Q4"));

        int features = ds->AddLane(Tr("New features"));
        int sticky   = ds->AddLane(Tr("Stickiness"));
        int integ    = ds->AddLane(Tr("Integrations"));
        int infra    = ds->AddLane(Tr("Infrastructure"));

        auto add = [&](int column, int lane, const std::string& title) {
            ds->AddCard(column, title, lane);
        };
        add(completed, features, Tr("Feature A scope"));
        add(completed, features, Tr("Feature requirements"));
        add(progress, features, Tr("Undo function"));
        add(progress, features, Tr("Feature B scope"));
        add(progress, features, Tr("Search"));
        add(soon, features, Tr("Integrated prototype"));
        add(soon, features, Tr("MVP requirements"));
        add(future, features, Tr("Archiving"));
        add(future, features, Tr("Front-end prototyping"));

        add(completed, sticky, Tr("Status updates"));
        add(progress, sticky, Tr("Gamification"));
        add(progress, sticky, Tr("Desktop delighter"));
        add(soon, sticky, Tr("Mobile delighter"));
        add(future, sticky, Tr("Reward (progress bar)"));
        add(future, sticky, Tr("Onboarding flow"));

        add(completed, integ, "Slack");
        add(completed, integ, "Salesforce");
        add(progress, integ, "Zendesk");
        add(progress, integ, "Marketo");
        add(soon, integ, "Trello");
        add(soon, integ, "HubSpot");
        add(future, integ, "Jira");
        add(future, integ, "Appzi");

        add(completed, infra, Tr("Demo staging"));
        add(progress, infra, Tr("Metrics"));
        add(progress, infra, Tr("Design process"));
        add(soon, infra, Tr("Automated tests"));
        add(future, infra, Tr("Regression"));
        add(future, infra, Tr("Back-end analytics"));
        return ds;
    }

    // Board with three months of dated history: cards are created and moved
    // through the workflow day by day, so the cumulative flow chart can
    // derive its bands from KanbanDataSource::GetColumnCountsAt.
    std::shared_ptr<KanbanDataSource> BuildFlowHistoryBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        int todo   = ds->AddColumn(Tr("To Do"));
        int doing  = ds->AddColumn(Tr("Doing"));
        int review = ds->AddColumn(Tr("Review"));
        int done   = ds->AddColumn(Tr("Done"));
        ds->SetCommitmentColumn(todo);
        ds->SetDeliveryColumn(done);

        const GanttDate start(2026, 5, 1);
        std::vector<int> cards;
        size_t nextToDoing = 0, nextToReview = 0, nextToDone = 0;
        for (int day = 0; day < 90; ++day) {
            ds->SetCurrentDate(start.AddDays(day));
            // Arrivals: a small deterministic burst every few days.
            if (day % 3 != 2 && cards.size() < 60) {
                cards.push_back(ds->AddCard(
                        todo, TrF("Item {0}", cards.size() + 1)));
                if (day % 7 == 0 && cards.size() < 60) {
                    cards.push_back(ds->AddCard(
                            todo, TrF("Item {0}", cards.size() + 1)));
                }
            }
            // Flow: start work every 2nd day, review every 3rd, finish every
            // 3rd with a slow first month (ramp-up bottleneck).
            if (day % 2 == 0 && nextToDoing < cards.size()) {
                ds->MoveCard(cards[nextToDoing++], doing);
            }
            if (day % 3 == 0 && nextToReview < nextToDoing) {
                ds->MoveCard(cards[nextToReview++], review);
            }
            if (day % (day < 30 ? 4 : 2) == 1 && nextToDone < nextToReview) {
                ds->MoveCard(cards[nextToDone++], done);
            }
        }
        return ds;
    }

    // Mermaid `kanban` definition exercising column WIP metadata and every
    // supported card metadata key.
    const char* kTextDefinition = R"(kanban
  %% Sample board defined as text
  backlog[Backlog]
    [Refactor session cache]@{ priority: 'Low' }
    [Write Q3 report]@{ assigned: 'Dana Reyes', due: '2026-08-14' }
  todo[To Do]@{ wip: 3 }
    cart[Fix cart size bug]@{ ticket: 'UC-2038', assigned: 'Iris Chen', priority: 'Very High', due: '2026-08-04' }
    docs[Update API documentation]@{ assigned: 'Sam Ortiz', label: 'docs' }
  doing[In Progress]@{ wip: 2 }
    render[Renderer edge cases]@{ assigned: 'Priya Nair', priority: 'High', desc: 'Cover clipping and HiDPI paths' }
  done[Done]
    [Design grammar]@{ assigned: 'K. Sveidqvist' }
    [Create parsing tests]@{ label: 'tests' }
)";

// =============================================================================
// SMALL UI HELPERS
// =============================================================================

    void AddFlexChild(const std::shared_ptr<UltraCanvasContainer>& parent,
                      const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }

    std::shared_ptr<UltraCanvasLabel> MakeTabDescription(const std::string& id,
                                                         const std::string& text) {
        // No height of its own: as tall as its wrapped lines.
        auto lbl = std::make_shared<UltraCanvasLabel>(id, text);
        lbl->SetFontSize(11);
        lbl->SetWrap(TextWrap::WrapWord);
        lbl->SetTextColor(Color(70, 70, 80, 255));
        lbl->SetMargin(8, 12, 4, 12);
        return lbl;
    }

    std::shared_ptr<UltraCanvasContainer> MakeBoardTab(
            const std::string& id, const std::string& description,
            const std::shared_ptr<UltraCanvasKanbanBoardElement>& board,
            int innerW, int innerH) {
        auto tab = std::make_shared<UltraCanvasContainer>(id, 0, 0, innerW, innerH);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->layout.SetFlexColumn().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab, MakeTabDescription(id + "Desc", description), 0);
        AddFlexChild(tab, board, 1);
        return tab;
    }

} // namespace

// =============================================================================
// PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateKanbanBoardExamples() {
    const int INNER_W = 1000;
    const int INNER_H = 640;

    auto root = DemoKit::CreatePage("KanbanRoot",
            Tr("Kanban Board - Editor, Designs & Text Definitions"),
            Tr("Drag cards between columns, watch the WIP badges, and on the "
            "editable tabs double-click a card to edit it, use the dashed "
            "'+ Add card' buttons, and double-click a column header to "
            "rename it."));

    // The tabs in a card, as wide as the page and as tall as the boards
    // need; each tab arranges its own content.
    auto tabsCard = DemoKit::AddCard(root, "KanbanTabsCard");
    tabsCard->SetPadding(4);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("KanbanTabs", 0, 0,
                                                             INNER_W, INNER_H + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(30);
    tabs->SetTabCornerRadius(8.0f);
    tabs->SetTabMaxWidth(180);

    // ---- Tab 1: interactive editor (Professional design) --------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanEditor", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSprintBoard(),
                                               KanbanDesign::Professional);
        board->SetToday(kToday);
        board->SetEditable(true);
        tabs->AddTab(Tr("Interactive Editor"), MakeBoardTab(
                "KanbanTabEditor",
                Tr("Fully editable sprint board: drag & drop between columns "
                "(WIP badges update live), double-click cards for the editor "
                "overlay, '+ Add card' / '+' for new cards and columns, Del "
                "removes the selected card. The overdue card shows in red."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 2: sticky notes (reference image 1) ----------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanSticky", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSprintBoard(),
                                               KanbanDesign::StickyNotes);
        board->SetToday(kToday);
        tabs->AddTab(Tr("Sticky Notes"), MakeBoardTab(
                "KanbanTabSticky",
                Tr("Pastel sticky notes staggered inside neutral tracks with slim "
                "colored column headers - the light whiteboard look."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 3: poster (reference image 2) ----------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanPoster", 0, 0, INNER_W,
                                               INNER_H - 60, BuildPosterBoard(),
                                               KanbanDesign::Poster);
        board->SetChartTitle(Tr("Kanban Board Template"));
        board->SetToday(kToday);
        tabs->AddTab(Tr("Poster"), MakeBoardTab(
                "KanbanTabPoster",
                Tr("Solid colored column panels, pill headers with connector dots, "
                "dog-ear cards with due dates, a board title and the priority "
                "legend."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 4: schematic (reference image 3) -------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanSchematic", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSchematicBoard(),
                                               KanbanDesign::Schematic);
        tabs->AddTab(Tr("Schematic"), MakeBoardTab(
                "KanbanTabSchematic",
                Tr("Presentation-style board: gray rounded panel, dashed column "
                "separators and compact color-block cards."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 5: timeline with swimlanes (reference image 5) -----------------
    {
        auto board = CreateKanbanBoardWithData("KanbanTimeline", 0, 0, INNER_W,
                                               INNER_H - 60, BuildTimelineBoard(),
                                               KanbanDesign::Timeline);
        tabs->AddTab(Tr("Timeline / Swimlanes"), MakeBoardTab(
                "KanbanTabTimeline",
                Tr("Portfolio kanban: status columns with Q1-Q4 caption strips, "
                "swimlane rows for each work stream, and one-line pill cards "
                "colored by column."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 6: dark theme --------------------------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanDark", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSprintBoard(),
                                               KanbanDesign::Dark);
        board->SetToday(kToday);
        board->SetEditable(true);
        tabs->AddTab(Tr("Dark"), MakeBoardTab(
                "KanbanTabDark",
                Tr("Dark variant of the Professional design; editing works here "
                "too."),
                board, INNER_W, INNER_H));
    }

    // ---- Tab 7: board from a Mermaid text definition ------------------------
    {
        std::string error;
        auto board = CreateKanbanBoardFromText("KanbanText", 0, 0, INNER_W,
                                               INNER_H - 60, kTextDefinition,
                                               KanbanDesign::Professional,
                                               &error);
        if (board) {
            board->SetToday(kToday);
            board->SetEditable(true);
            tabs->AddTab(Tr("Text Definition"), MakeBoardTab(
                    "KanbanTabText",
                    Tr("This board was parsed from a Mermaid `kanban` text "
                    "definition: columns with @{ wip: N } limits and cards with "
                    "assigned / priority / due / ticket / label metadata. See "
                    "kTextDefinition in the example source."),
                    board, INNER_W, INNER_H));
        } else {
            auto lbl = MakeTabDescription("KanbanTextError",
                                          TrF("Text definition failed to parse: {0}", error));
            auto tab = std::make_shared<UltraCanvasContainer>("KanbanTabText", 0, 0,
                                                              INNER_W, INNER_H);
            tab->layout.SetFlexColumn();
            AddFlexChild(tab, lbl, 0);
            tabs->AddTab(Tr("Text Definition"), tab);
        }
    }

    // ---- Tab 8: cumulative flow diagrams ------------------------------------
    {
        auto tab = std::make_shared<UltraCanvasContainer>("KanbanTabFlow", 0, 0,
                                                          INNER_W, INNER_H);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->layout.SetFlexColumn().SetFlexGap(6)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        AddFlexChild(tab, MakeTabDescription(
                "KanbanTabFlowDesc",
                Tr("Cumulative flow diagrams. Top: manual series in the olive "
                "report style with the classic Lead Time / Cycle Time / WIP "
                "Limit annotations. Bottom: bands derived live from a Kanban "
                "board's 90-day move history via LoadFromKanban - hover for "
                "the per-stage counts.")), 0);

        // Olive report chart from manual series (reference image style).
        auto olive = CreateCumulativeFlowChartElement("KanbanFlowOlive", 0, 0,
                                                      INNER_W, (INNER_H - 80) / 2);
        olive->SetStyle(CumulativeFlowStyles::CreateOlive());
        olive->AddStage(Tr("To do"), {17, 15, 12, 12, 10, 5, 2, 1, 1});
        olive->AddStage(Tr("Doing"), {3, 3, 3, 3, 3, 5, 5, 5, 4});
        olive->AddStage(Tr("QA"),    {0, 2, 3, 2, 2, 3, 3, 3, 2});
        olive->AddStage(Tr("Done"),  {0, 0, 2, 3, 5, 7, 10, 11, 13});
        std::vector<std::string> months;
        for (int month = 1; month <= 9; ++month) months.push_back(TrF("Month {0}", month));
        olive->SetPeriodLabels(months);
        olive->AddLeadTimeAnnotation();
        olive->AddCycleTimeAnnotation();
        olive->AddWipAnnotation();
        AddFlexChild(tab, olive, 1);

        // Chart bound to a board's real move history.
        auto derived = CreateCumulativeFlowChartFromKanban(
                "KanbanFlowDerived", 0, 0, INNER_W, (INNER_H - 80) / 2,
                BuildFlowHistoryBoard(), GanttDate(2026, 5, 1),
                GanttDate(2026, 7, 29), /*stepDays=*/7,
                Tr("Derived from board history (weekly)"));
        AddFlexChild(tab, derived, 1);

        tabs->AddTab(Tr("Cumulative Flow"), tab);
    }

    DemoKit::AddStretched(tabsCard, tabs);
    return root;
}

} // namespace UltraCanvas

// Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp
// Kanban board demo. One tab per design preset plus an interactive editor tab
// (drag & drop, add/edit/delete cards, column rename) and a tab whose board is
// built from a Mermaid `kanban` text definition.
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDemo.h"

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

        int backlog = ds->AddColumn("Backlog");
        int todo    = ds->AddColumn("To Do", 4);
        int doing   = ds->AddColumn("In Progress", 3);
        int testing = ds->AddColumn("Testing", 2);
        int done    = ds->AddColumn("Done");

        int c;
        c = ds->AddCard(backlog, "Add editor role");
        ds->SetCardDescription(c, "Role-based permissions for shared boards");
        ds->SetCardPriority(c, KanbanPriority::Low);
        c = ds->AddCard(backlog, "Install analytics plugin");
        ds->SetCardAssignee(c, "Dana Reyes");
        ds->AddCardTag(c, "infra");
        c = ds->AddCard(backlog, "Update invoice design");
        ds->SetCardDueDate(c, GanttDate(2026, 8, 21));

        c = ds->AddCard(todo, "Shopping cart is missing size");
        ds->SetCardDescription(c, "Cart summary drops the size attribute of "
                                  "configurable products");
        ds->SetCardPriority(c, KanbanPriority::High);
        ds->SetCardDueDate(c, GanttDate(2026, 8, 4));
        ds->SetCardAssignee(c, "Iris Chen");
        ds->AddCardTag(c, "bug");
        c = ds->AddCard(todo, "Educational article about colors");
        ds->SetCardAssignee(c, "Sam Ortiz");
        ds->AddCardTag(c, "content");

        c = ds->AddCard(doing, "Fix broken links in Resources");
        ds->SetCardDescription(c, "Nightly crawl reports 14 dead links");
        ds->SetCardPriority(c, KanbanPriority::Urgent);
        ds->SetCardDueDate(c, GanttDate(2026, 7, 28));   // Overdue
        ds->SetCardAssignee(c, "Priya Nair");
        ds->AddCardTag(c, "bug");
        c = ds->AddCard(doing, "New landing page hero");
        ds->SetCardAssignee(c, "Iris Chen");
        ds->SetCardPriority(c, KanbanPriority::Normal);
        ds->SetCardBlocked(c, true, "Waiting for brand assets");

        c = ds->AddCard(testing, "High contrast mode");
        ds->SetCardPriority(c, KanbanPriority::High);
        ds->SetCardAssignee(c, "QA Team");
        c = ds->AddCard(testing, "External commenting system");
        ds->SetCardDueDate(c, GanttDate(2026, 8, 7));
        ds->SetCardAssignee(c, "Leo Brandt");

        c = ds->AddCard(done, "Update favicons & metadata");
        ds->SetCardPriority(c, KanbanPriority::Low);
        c = ds->AddCard(done, "Reduce backup size");
        ds->SetCardAssignee(c, "Dana Reyes");
        c = ds->AddCard(done, "Set up campaign");
        ds->AddCardTag(c, "marketing");

        return ds;
    }

    // WordLayouts-style poster: colored panels, TASK NN cards with due dates
    // and priority dots (reference image 2).
    std::shared_ptr<KanbanDataSource> BuildPosterBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        ds->SetCurrentDate(kToday);

        int backlog = ds->AddColumn("Backlog");
        int todo    = ds->AddColumn("To Do");
        int doing   = ds->AddColumn("In Progress");
        int testing = ds->AddColumn("Testing");
        int done    = ds->AddColumn("Complete");

        struct Item { int column; const char* title; KanbanPriority priority; };
        const Item items[] = {
                {backlog, "TASK 01", KanbanPriority::Urgent},
                {backlog, "TASK 02", KanbanPriority::High},
                {backlog, "TASK 04", KanbanPriority::Low},
                {todo,    "TASK 03", KanbanPriority::High},
                {todo,    "TASK 05", KanbanPriority::Urgent},
                {doing,   "TASK 12", KanbanPriority::High},
                {doing,   "TASK 08", KanbanPriority::Low},
                {doing,   "TASK 09", KanbanPriority::Urgent},
                {doing,   "TASK 07", KanbanPriority::High},
                {testing, "TASK 06", KanbanPriority::Urgent},
                {testing, "TASK 10", KanbanPriority::Low},
                {done,    "TASK 11", KanbanPriority::High},
        };
        for (const Item& item : items) {
            int c = ds->AddCard(item.column, item.title);
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
        int pending = ds->AddColumn("Pending");
        int design  = ds->AddColumn("Design");
        int develop = ds->AddColumn("Develop");
        int test    = ds->AddColumn("Test");
        int deploy  = ds->AddColumn("Deploy");

        ds->AddCard(pending, "Brief");
        ds->AddCard(design, "Wireframes");
        ds->AddCard(design, "Mockups");
        ds->AddCard(develop, "API");
        ds->AddCard(develop, "Frontend");
        ds->AddCard(test, "Smoke tests");
        ds->AddCard(test, "Load tests");
        ds->AddCard(deploy, "Staging");
        ds->AddCard(deploy, "Production");
        return ds;
    }

    // Portfolio timeline: status columns with quarter captions, swimlane rows,
    // one-line pill cards colored by column (reference image 5).
    std::shared_ptr<KanbanDataSource> BuildTimelineBoard() {
        auto ds = std::make_shared<KanbanDataSource>();

        int completed = ds->AddColumn("Completed");
        int progress  = ds->AddColumn("In Progress");
        int soon      = ds->AddColumn("Soon");
        int future    = ds->AddColumn("Future");
        ds->SetColumnFooterCaption(completed, "Q1");
        ds->SetColumnFooterCaption(progress, "Q2");
        ds->SetColumnFooterCaption(soon, "Q3");
        ds->SetColumnFooterCaption(future, "Q4");

        int features = ds->AddLane("New features");
        int sticky   = ds->AddLane("Stickiness");
        int integ    = ds->AddLane("Integrations");
        int infra    = ds->AddLane("Infrastructure");

        auto add = [&](int column, int lane, const char* title) {
            ds->AddCard(column, title, lane);
        };
        add(completed, features, "Feature A scope");
        add(completed, features, "Feature requirements");
        add(progress, features, "Undo function");
        add(progress, features, "Feature B scope");
        add(progress, features, "Search");
        add(soon, features, "Integrated prototype");
        add(soon, features, "MVP requirements");
        add(future, features, "Archiving");
        add(future, features, "Front-end prototyping");

        add(completed, sticky, "Status updates");
        add(progress, sticky, "Gamification");
        add(progress, sticky, "Desktop delighter");
        add(soon, sticky, "Mobile delighter");
        add(future, sticky, "Reward (progress bar)");
        add(future, sticky, "Onboarding flow");

        add(completed, integ, "Slack");
        add(completed, integ, "Salesforce");
        add(progress, integ, "Zendesk");
        add(progress, integ, "Marketo");
        add(soon, integ, "Trello");
        add(soon, integ, "HubSpot");
        add(future, integ, "Jira");
        add(future, integ, "Appzi");

        add(completed, infra, "Demo staging");
        add(progress, infra, "Metrics");
        add(progress, infra, "Design process");
        add(soon, infra, "Automated tests");
        add(future, infra, "Regression");
        add(future, infra, "Back-end analytics");
        return ds;
    }

    // Board with three months of dated history: cards are created and moved
    // through the workflow day by day, so the cumulative flow chart can
    // derive its bands from KanbanDataSource::GetColumnCountsAt.
    std::shared_ptr<KanbanDataSource> BuildFlowHistoryBoard() {
        auto ds = std::make_shared<KanbanDataSource>();
        int todo   = ds->AddColumn("To Do");
        int doing  = ds->AddColumn("Doing");
        int review = ds->AddColumn("Review");
        int done   = ds->AddColumn("Done");
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
                        todo, "Item " + std::to_string(cards.size() + 1)));
                if (day % 7 == 0 && cards.size() < 60) {
                    cards.push_back(ds->AddCard(
                            todo, "Item " + std::to_string(cards.size() + 1)));
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
        auto lbl = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 44);
        lbl->SetText(text);
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
    const int CONTAINER_W = 1180;
    const int CONTAINER_H = 800;
    const int PAD = 16;
    const int INNER_W = 1000;
    const int INNER_H = 640;

    auto root = std::make_shared<UltraCanvasContainer>("KanbanRoot", 0, 0,
                                                       CONTAINER_W, CONTAINER_H);
    root->SetBackgroundColor(Color(250, 250, 252, 255));
    root->SetPadding(PAD);
    root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto title = std::make_shared<UltraCanvasLabel>("KanbanTitle", 0, 0, 0, 28);
    title->SetText("Kanban Board - Editor, Designs & Text Definitions");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(40, 50, 120, 255));
    AddFlexChild(root, title, 0);

    auto desc = std::make_shared<UltraCanvasLabel>("KanbanDesc", 0, 0, 0, 20);
    desc->SetText("Drag cards between columns, watch the WIP badges, and on the "
                  "editable tabs double-click a card to edit it, use the dashed "
                  "'+ Add card' buttons, and double-click a column header to "
                  "rename it.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    desc->SetTextColor(Color(90, 90, 100, 255));
    AddFlexChild(root, desc, 0);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("KanbanTabs", 0, 0,
                                                             CONTAINER_W - 2 * PAD,
                                                             INNER_H + 40);
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
        tabs->AddTab("Interactive Editor", MakeBoardTab(
                "KanbanTabEditor",
                "Fully editable sprint board: drag & drop between columns "
                "(WIP badges update live), double-click cards for the editor "
                "overlay, '+ Add card' / '+' for new cards and columns, Del "
                "removes the selected card. The overdue card shows in red.",
                board, INNER_W, INNER_H));
    }

    // ---- Tab 2: sticky notes (reference image 1) ----------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanSticky", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSprintBoard(),
                                               KanbanDesign::StickyNotes);
        board->SetToday(kToday);
        tabs->AddTab("Sticky Notes", MakeBoardTab(
                "KanbanTabSticky",
                "Pastel sticky notes staggered inside neutral tracks with slim "
                "colored column headers - the light whiteboard look.",
                board, INNER_W, INNER_H));
    }

    // ---- Tab 3: poster (reference image 2) ----------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanPoster", 0, 0, INNER_W,
                                               INNER_H - 60, BuildPosterBoard(),
                                               KanbanDesign::Poster);
        board->SetChartTitle("Kanban Board Template");
        board->SetToday(kToday);
        tabs->AddTab("Poster", MakeBoardTab(
                "KanbanTabPoster",
                "Solid colored column panels, pill headers with connector dots, "
                "dog-ear cards with due dates, a board title and the priority "
                "legend.",
                board, INNER_W, INNER_H));
    }

    // ---- Tab 4: schematic (reference image 3) -------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanSchematic", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSchematicBoard(),
                                               KanbanDesign::Schematic);
        tabs->AddTab("Schematic", MakeBoardTab(
                "KanbanTabSchematic",
                "Presentation-style board: gray rounded panel, dashed column "
                "separators and compact color-block cards.",
                board, INNER_W, INNER_H));
    }

    // ---- Tab 5: timeline with swimlanes (reference image 5) -----------------
    {
        auto board = CreateKanbanBoardWithData("KanbanTimeline", 0, 0, INNER_W,
                                               INNER_H - 60, BuildTimelineBoard(),
                                               KanbanDesign::Timeline);
        tabs->AddTab("Timeline / Swimlanes", MakeBoardTab(
                "KanbanTabTimeline",
                "Portfolio kanban: status columns with Q1-Q4 caption strips, "
                "swimlane rows for each work stream, and one-line pill cards "
                "colored by column.",
                board, INNER_W, INNER_H));
    }

    // ---- Tab 6: dark theme --------------------------------------------------
    {
        auto board = CreateKanbanBoardWithData("KanbanDark", 0, 0, INNER_W,
                                               INNER_H - 60, BuildSprintBoard(),
                                               KanbanDesign::Dark);
        board->SetToday(kToday);
        board->SetEditable(true);
        tabs->AddTab("Dark", MakeBoardTab(
                "KanbanTabDark",
                "Dark variant of the Professional design; editing works here "
                "too.",
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
            tabs->AddTab("Text Definition", MakeBoardTab(
                    "KanbanTabText",
                    "This board was parsed from a Mermaid `kanban` text "
                    "definition: columns with @{ wip: N } limits and cards with "
                    "assigned / priority / due / ticket / label metadata. See "
                    "kTextDefinition in the example source.",
                    board, INNER_W, INNER_H));
        } else {
            auto lbl = MakeTabDescription("KanbanTextError",
                                          "Text definition failed to parse: " + error);
            auto tab = std::make_shared<UltraCanvasContainer>("KanbanTabText", 0, 0,
                                                              INNER_W, INNER_H);
            tab->layout.SetFlexColumn();
            AddFlexChild(tab, lbl, 0);
            tabs->AddTab("Text Definition", tab);
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
                "Cumulative flow diagrams. Top: manual series in the olive "
                "report style with the classic Lead Time / Cycle Time / WIP "
                "Limit annotations. Bottom: bands derived live from a Kanban "
                "board's 90-day move history via LoadFromKanban - hover for "
                "the per-stage counts."), 0);

        // Olive report chart from manual series (reference image style).
        auto olive = CreateCumulativeFlowChartElement("KanbanFlowOlive", 0, 0,
                                                      INNER_W, (INNER_H - 80) / 2);
        olive->SetStyle(CumulativeFlowStyles::CreateOlive());
        olive->AddStage("To do", {17, 15, 12, 12, 10, 5, 2, 1, 1});
        olive->AddStage("Doing", {3, 3, 3, 3, 3, 5, 5, 5, 4});
        olive->AddStage("QA",    {0, 2, 3, 2, 2, 3, 3, 3, 2});
        olive->AddStage("Done",  {0, 0, 2, 3, 5, 7, 10, 11, 13});
        olive->SetPeriodLabels({"Month 1", "Month 2", "Month 3", "Month 4",
                                "Month 5", "Month 6", "Month 7", "Month 8",
                                "Month 9"});
        olive->AddLeadTimeAnnotation();
        olive->AddCycleTimeAnnotation();
        olive->AddWipAnnotation();
        AddFlexChild(tab, olive, 1);

        // Chart bound to a board's real move history.
        auto derived = CreateCumulativeFlowChartFromKanban(
                "KanbanFlowDerived", 0, 0, INNER_W, (INNER_H - 80) / 2,
                BuildFlowHistoryBoard(), GanttDate(2026, 5, 1),
                GanttDate(2026, 7, 29), /*stepDays=*/7,
                "Derived from board history (weekly)");
        AddFlexChild(tab, derived, 1);

        tabs->AddTab("Cumulative Flow", tab);
    }

    AddFlexChild(root, tabs, 1);
    return root;
}

} // namespace UltraCanvas

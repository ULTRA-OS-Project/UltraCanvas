// Apps/UltraDesktop/ui/UltraDesktopCategoryBoard.cpp
// The Sticker board: files sorted into categories. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopCategoryBoard.h"
#include "UltraDesktopWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasPathUtf8.h"
#include "Plugins/Charts/UltraCanvasKanbanBoard.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace UltraDesktop {
namespace {

constexpr float kBarHeight = 34;
constexpr float kPanelMinWidth = 360;
constexpr float kPanelMinHeight = 220;
// The part of the bar the drag answers on: the title, left of the picker.
constexpr float kGripWidth = 150;
// The resize grip in the panel's bottom-right corner.
constexpr float kResizeGrip = 18;

struct DesignChoice {
    const char* key;
    const char* label;
    KanbanDesign design;
};

// The display styles the picker offers, in its order.
const DesignChoice kDesigns[] = {
    {"stickynotes", "Sticky notes", KanbanDesign::StickyNotes},
    {"poster", "Poster", KanbanDesign::Poster},
    {"schematic", "Schematic", KanbanDesign::Schematic},
    {"dark", "Dark", KanbanDesign::Dark},
};

int DesignIndex(const std::string& key) {
    for (size_t i = 0; i < std::size(kDesigns); ++i) {
        if (key == kDesigns[i].key) return static_cast<int>(i);
    }
    return 0;
}

GanttDate Today() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return GanttDate(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

// "/home/me/Docs/plan.pdf" -> "plan.pdf"; a folder keeps its own name even
// with a trailing separator.
std::string DisplayName(const std::string& path) {
    fs::path p = PathFromUtf8(path);
    if (!p.has_filename()) p = p.parent_path();
    std::string name = PathToUtf8(p.filename());
    return name.empty() ? path : name;
}

// The chip on a file's card: its extension ("pdf"), or "folder".
std::string KindTag(const std::string& path) {
    std::error_code ec;
    const fs::path p = PathFromUtf8(path);
    if (fs::is_directory(p, ec)) return "folder";
    std::string ext = PathToUtf8(p.extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext.empty() ? "file" : ext;
}

} // namespace

const std::vector<std::string>& UltraDesktopCategoryBoard::DefaultCategories() {
    static const std::vector<std::string> categories = {"Docs", "Work", "Home"};
    return categories;
}

UltraDesktopCategoryBoard::UltraDesktopCategoryBoard(const std::string& identifier,
                                                     UltraDesktopWindow* desktop)
    : UltraCanvasContainer(identifier, 0, 0, 0, 0), desktop_(desktop) {
    SetBackgroundColor(Colors::Transparent);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    SetContainerStyle(plain);

    panel_ = CreateContainer("CategoryBoard.Panel", 0, 0, 0, 0);
    panel_->SetBackgroundColor(Color(250, 250, 250, 245));
    panel_->SetBorders(1, Color(0, 0, 0, 60), 8);
    panel_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panel_->SetContainerStyle(plain);

    // The bar: the title to drag by, the display style, "Add category".
    auto bar = CreateContainer("CategoryBoard.Bar", 0, 0, 0, kBarHeight);
    bar->SetBackgroundColor(Color(0, 0, 0, 18));
    bar->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(8);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kBarHeight));
    bar->SetPadding(0, 8, 0, 8);
    bar->layoutItem.SetFlexShrink(0.0f);
    bar->SetContainerStyle(plain);

    grip_ = CreateLabel("CategoryBoard.Title", kGripWidth, 24, "Sticker board");
    grip_->SetFontSize(13);
    grip_->SetFontWeight(FontWeight::Bold);
    grip_->SetTextColor(Color(50, 50, 50, 255));
    grip_->SetTooltip("Drag to move. Drop files onto a category to add them.");
    grip_->layoutItem.SetFlexShrink(0.0f);
    bar->AddChild(grip_);

    auto spacer = CreateContainer("CategoryBoard.Spacer", 0, 0, 0, 1);
    spacer->SetBackgroundColor(Colors::Transparent);
    spacer->layoutItem.SetFlexGrow(1.0f);
    bar->AddChild(spacer);

    designPicker_ = CreateDropdown("CategoryBoard.Design", 0, 0, 140, 24);
    for (const DesignChoice& choice : kDesigns) designPicker_->AddItem(choice.label, choice.key);
    designPicker_->SetSelectedIndex(0, /*runNotifications=*/false);
    designPicker_->SetTooltip("Display style");
    designPicker_->layoutItem.SetFlexShrink(0.0f);
    designPicker_->onSelectionChanged = [this](int, const DropdownItem& item) {
        SetDesign(item.value);
        if (desktop_) {
            desktop_->Settings().categoryBoard.design = item.value;
            desktop_->SaveSettings();
        }
    };
    bar->AddChild(designPicker_);

    auto add = CreateButton("CategoryBoard.AddCategory", 0, 0, 140, 24, "+ Add category");
    add->SetTooltip("Add a category");
    add->layoutItem.SetFlexShrink(0.0f);
    add->onClick = [this]() { AddCategory(); };
    bar->AddChild(add);
    panel_->AddChild(bar);

    data_ = std::make_shared<KanbanDataSource>();
    board_ = std::make_shared<UltraCanvasKanbanBoardElement>("CategoryBoard.Board", 0, 0, 0, 0);
    board_->SetKanbanDataSource(data_);
    board_->SetEditable(true);
    board_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    board_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // Files dropped onto a category join it straight away.
    board_->onFilesDropped = [this](const std::vector<std::string>& files, int columnId, int) {
        if (columnId >= 0) AddFiles(files, columnId);
    };
    board_->onBoardChanged = [this]() { SaveBoard(); };
    // A file's card opens the file; a note's card keeps the editor the
    // board opened for it.
    board_->onCardDoubleClick = [this](int cardId) { OpenCardFile(cardId); };
    panel_->AddChild(board_);

    // The resize grip: a label over the board's corner, out of the flow. It
    // takes no events, so a press on it bubbles here (see OnEvent).
    resizeGrip_ = CreateLabel("CategoryBoard.Resize", kResizeGrip, kResizeGrip, "◢");
    resizeGrip_->SetFontSize(12);
    resizeGrip_->SetTextColor(Color(0, 0, 0, 110));
    resizeGrip_->SetAlignment(TextAlignment::Center);
    resizeGrip_->SetTooltip("Drag to resize");
    if (!resizeGrip_->layoutItem.position) resizeGrip_->layoutItem.position = CSSLayout::Position();
    resizeGrip_->layoutItem.position->right = CSSLayout::Dimension::Px(1);
    resizeGrip_->layoutItem.position->bottom = CSSLayout::Dimension::Px(1);
    resizeGrip_->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    panel_->AddChild(resizeGrip_);
    SetDesign(kDesigns[0].key);

    AddChild(panel_);
}

void UltraDesktopCategoryBoard::LoadFrom(const DesktopSettings& settings) {
    const CategoryBoardSettings& cb = settings.categoryBoard;
    data_->SetCurrentDate(Today());
    std::string error;
    if (cb.board.empty() || !data_->LoadFromJSONText(cb.board, &error)) {
        if (!error.empty()) debugOutput << "UltraDesktop: Sticker board not read: " << error << std::endl;
        data_->Clear();
        for (const std::string& name : DefaultCategories()) data_->AddColumn(name);
    }
    data_->SetCurrentDate(Today());
    designPicker_->SetSelectedIndex(DesignIndex(cb.design), /*runNotifications=*/false);
    SetDesign(cb.design);
    PlacePanel();
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopCategoryBoard::SetDesign(const std::string& design) {
    board_->ApplyDesign(kDesigns[DesignIndex(design)].design);
    // The bar above already names the board, and files carry no priorities.
    KanbanBoardStyle& style = board_->EditStyle();
    style.showTitle = false;
    style.showPriorityLegend = false;
    // A file's path is its description: the tooltip shows it, the card
    // keeps to the name.
    style.showCardDescription = false;
    board_->StyleChanged();
    board_->SetToday(Today());
    RequestRedraw();
}

void UltraDesktopCategoryBoard::AddCategory() {
    const int columnId = data_->AddColumn("New category");
    // Renaming scrolls the board to the new category when it is too wide to
    // show them all.
    board_->BeginColumnRename(columnId, /*startEmpty=*/true);
    SaveBoard();
}

void UltraDesktopCategoryBoard::AddFiles(const std::vector<std::string>& files, int columnId) {
    if (columnId < 0) {
        if (data_->GetColumns().empty()) return;
        columnId = data_->GetColumns().front().id;
    }
    if (!data_->FindColumn(columnId)) return;
    data_->SetCurrentDate(Today());
    int last = -1;
    for (const std::string& path : files) {
        if (path.empty()) continue;
        // One card per file: dropped again, it moves to the new category.
        int existing = -1;
        for (const KanbanColumn& column : data_->GetColumns()) {
            for (int id : data_->GetCardsInColumn(column.id)) {
                const KanbanCard* card = data_->FindCard(id);
                if (card && card->description == path) { existing = id; break; }
            }
            if (existing >= 0) break;
        }
        if (existing >= 0) {
            data_->MoveCard(existing, columnId);
            last = existing;
            continue;
        }
        const int cardId = data_->AddCard(columnId, DisplayName(path));
        if (cardId < 0) continue;
        data_->SetCardDescription(cardId, path);
        data_->AddCardTag(cardId, KindTag(path));
        last = cardId;
    }
    if (last >= 0) board_->ScrollToCard(last);
    SaveBoard();
    board_->RequestRedraw();
}

void UltraDesktopCategoryBoard::OpenCardFile(int cardId) {
    const KanbanCard* card = data_->FindCard(cardId);
    if (!card || card->description.empty()) return;
    std::error_code ec;
    if (!fs::exists(PathFromUtf8(card->description), ec)) return;   // a note, not a file
    board_->CloseCardEditor(/*save=*/false);
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({card->description}, error)) {
        debugOutput << "UltraDesktop: could not open " << card->description << ": " << error << std::endl;
    }
}

void UltraDesktopCategoryBoard::SaveBoard() {
    if (!desktop_) return;
    desktop_->Settings().categoryBoard.board = data_->ToJSON(/*pretty=*/false);
    desktop_->SaveSettings();
}

void UltraDesktopCategoryBoard::PlacePanel() {
    if (!desktop_) return;
    const CategoryBoardSettings& cb = desktop_->Settings().categoryBoard;
    PlaceChildAt(panel_, Rect2Df(cb.x, cb.y, std::max(kPanelMinWidth, cb.width),
                                 std::max(kPanelMinHeight, cb.height)));
}

bool UltraDesktopCategoryBoard::Contains(const Point2Df& point) {
    if (!IsVisible()) return false;
    return panel_ && panel_->GetBounds().Contains(point);
}

bool UltraDesktopCategoryBoard::OnEvent(const UCEvent& event) {
    // Moving the panel by its title, resizing it by the corner grip: the
    // press lands on a label (they take no events, so it bubbles here), the
    // moves follow the pointer, the release writes the result into the
    // settings.
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseDown:
            if (event.button == UCMouseButton::Left && panel_) {
                const Rect2Df panel = panel_->GetBounds();
                const Rect2Df corner(panel.x + panel.width - kResizeGrip - 2,
                                     panel.y + panel.height - kResizeGrip - 2,
                                     kResizeGrip + 2, kResizeGrip + 2);
                if (corner.Contains(event.pointer)) {
                    resizing_ = true;
                    dragOffset_ = Point2Df(panel.x + panel.width - event.pointer.x,
                                           panel.y + panel.height - event.pointer.y);
                    if (app) app->CaptureMouse(this);
                    return true;
                }
                const Rect2Df grip(panel.x, panel.y, kGripWidth + 8, kBarHeight);
                if (grip.Contains(event.pointer)) {
                    dragging_ = true;
                    dragOffset_ = Point2Df(event.pointer.x - panel.x, event.pointer.y - panel.y);
                    if (app) app->CaptureMouse(this);
                    return true;
                }
            }
            break;
        case UCEventType::MouseMove:
            if ((dragging_ || resizing_) && desktop_) {
                CategoryBoardSettings& cb = desktop_->Settings().categoryBoard;
                if (resizing_) {
                    // Never past the work area: the grip must stay reachable.
                    const Rect2Df area = GetBounds();
                    const float maxWidth = std::max(kPanelMinWidth, area.width - cb.x);
                    const float maxHeight = std::max(kPanelMinHeight, area.height - cb.y);
                    cb.width = std::clamp(event.pointer.x + dragOffset_.x - cb.x, kPanelMinWidth, maxWidth);
                    cb.height = std::clamp(event.pointer.y + dragOffset_.y - cb.y, kPanelMinHeight, maxHeight);
                } else {
                    cb.x = std::max(0.0f, event.pointer.x - dragOffset_.x);
                    cb.y = std::max(0.0f, event.pointer.y - dragOffset_.y);
                }
                PlacePanel();
                InvalidateLayout();
                RequestRedraw();
                return true;
            }
            break;
        case UCEventType::MouseUp:
            if (dragging_ || resizing_) {
                dragging_ = false;
                resizing_ = false;
                if (app) app->ReleaseMouse();
                if (desktop_) desktop_->SaveSettings();
                return true;
            }
            break;
        default:
            break;
    }
    return UltraCanvasContainer::OnEvent(event);
}

} // namespace UltraDesktop

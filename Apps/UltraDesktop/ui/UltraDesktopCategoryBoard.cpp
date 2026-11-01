// Apps/UltraDesktop/ui/UltraDesktopCategoryBoard.cpp
// The Sticker board: files sorted into categories. See the header.
// Version: 0.3.0 - thumbnails as UltraFiler makes them (RenderFileThumbnail):
//                  a PDF's first page, a page of a document's text
// Version: 0.2.0 - its own window's content; close button; thumbnails;
//                  smaller card text
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopCategoryBoard.h"
#include "UltraDesktopWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasUtils.h"
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

constexpr float kBarHeight = 38;
// A card's text: smaller than the board's own default, so a category holds
// more files at a glance.
constexpr float kCardTitleFont = 10.0f;
constexpr float kCardTextFont = 9.0f;
constexpr float kCardMetaFont = 8.5f;
// The thumbnail band on a card, and the pixels it is made at (twice the
// band, sharp on a HiDPI screen).
constexpr float kThumbnailHeight = 58.0f;
constexpr int kThumbnailPixels = 2 * 58;

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
    SetBackgroundColor(Color(250, 250, 250, 255));
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    SetContainerStyle(plain);
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // The bar: the title, the display style, "Add category", close.
    auto bar = CreateContainer("CategoryBoard.Bar", 0, 0, 0, kBarHeight);
    bar->SetBackgroundColor(Color(0, 0, 0, 18));
    bar->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center).SetFlexGap(8);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kBarHeight));
    bar->SetPadding(0, 8, 0, 12);
    bar->layoutItem.SetFlexShrink(0.0f);
    bar->SetContainerStyle(plain);

    title_ = CreateLabel("CategoryBoard.Title", 150, 24, "Sticker board");
    title_->SetFontSize(13);
    title_->SetFontWeight(FontWeight::Bold);
    title_->SetTextColor(Color(50, 50, 50, 255));
    title_->SetTooltip("Drop files onto a category to add them; double-click one to open it.");
    title_->layoutItem.SetFlexShrink(0.0f);
    bar->AddChild(title_);

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

    auto close = CreateButton("CategoryBoard.Close", 0, 0, 30, 26, "\u2715");
    close->SetTooltip("Hide the Sticker board (the pinboard icon shows it again)");
    close->layoutItem.SetFlexShrink(0.0f);
    close->onClick = [this]() {
        // After this click: hiding takes the window this button is in.
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([desktop = desktop_]() {
                if (desktop) desktop->ToggleCategoryBoard(false);
            });
        }
    };
    bar->AddChild(close);
    AddChild(bar);

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
    board_->cardImageProvider = [this](const KanbanCard& card) { return Thumbnail(card); };
    AddChild(board_);
    SetDesign(kDesigns[0].key);
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
    style.cardTitleFontSize = kCardTitleFont;
    style.cardTextFontSize = kCardTextFont;
    style.metaFontSize = kCardMetaFont;
    style.cardImageHeight = kThumbnailHeight;
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

std::shared_ptr<UCPixmap> UltraDesktopCategoryBoard::Thumbnail(const KanbanCard& card) {
    const std::string& path = card.description;   // a file's card keeps its path there
    if (path.empty()) return nullptr;
    auto ready = thumbnails_.find(path);
    if (ready != thumbnails_.end()) return ready->second;
    if (pending_.count(path) || missing_.count(path)) return nullptr;
    pending_.insert(path);
    const std::string icons = NormalizePath(GetResourcesDir() + "media/icons/");
    // Off the UI thread: a photo decodes, the system is asked for an icon.
    work_.Run<std::shared_ptr<UCPixmap>>(
        [path, icons]() -> std::shared_ptr<UCPixmap> {
            std::error_code ec;
            if (!fs::exists(PathFromUtf8(path), ec)) return nullptr;   // a note, or a file gone
            const bool folder = fs::is_directory(PathFromUtf8(path), ec);
            // What the file holds, as UltraFiler's tiles show it: the
            // picture, a PDF's first page, a book's cover, a page of a
            // document's first lines ...
            if (!folder) {
                if (auto preview = RenderFileThumbnail(path, kThumbnailPixels * 2, kThumbnailPixels, 1.0f)) {
                    return preview;
                }
            }
            // ... else the icon of its type.
            {
                NativeFileIconThreadScope scope;
                if (auto icon = LoadHostFileIconPixmap(path, folder, kThumbnailPixels)) return icon;
            }
            // No icon theme to ask (a bare system): the framework's own.
            const std::string kind = folder ? "folder" : KindTag(path);
            const std::string fallback = icons + (folder ? "folder-brown.svg" : kind == "pdf" ? "pdf.png" : "document.svg");
            if (auto image = UCImage::GetFresh(fallback)) {
                return image->GetPixmap(kThumbnailPixels, kThumbnailPixels, ImageFitMode::Contain, 1.0f);
            }
            return nullptr;
        },
        [this, path](std::shared_ptr<UCPixmap> pixmap) {
            pending_.erase(path);
            if (!pixmap) {
                missing_.insert(path);
                return;
            }
            thumbnails_[path] = pixmap;
            // The card grows to take the picture.
            board_->InvalidateLayout();
            board_->RequestRedraw();
        });
    return nullptr;
}

} // namespace UltraDesktop

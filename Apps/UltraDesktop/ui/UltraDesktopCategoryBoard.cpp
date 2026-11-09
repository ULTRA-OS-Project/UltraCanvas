// Apps/UltraDesktop/ui/UltraDesktopCategoryBoard.cpp
// The Sticker board: files sorted into categories. See the header.
// Version: 0.4.0 - the context menu, three card displays, the hover preview
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
#include "UltraCanvasClipboard.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTrash.h"
#include "UltraLocalisation/UltraLocalisation.h"
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
// The small square beside the name in the "row" display.
constexpr float kRowThumbnail = 34.0f;
// How long the pointer rests on a card before its preview opens.
constexpr unsigned kHoverPreviewDelayMs = 2000;

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

// The card displays, in the menu's order.
struct DisplayChoice {
    const char* key;
};
const DisplayChoice kDisplays[] = {{"text"}, {"thumbnail"}, {"row"}};

std::string DisplayLabel(const std::string& key) {
    if (key == "text") return Tr("Text only");
    if (key == "row") return Tr("Thumbnail and text in a row");
    return Tr("Thumbnail and text");
}

// The hover preview's sizes and their longest side in pixels.
struct PreviewChoice {
    const char* key;
    float size;
};
const PreviewChoice kPreviews[] = {{"small", 180.0f}, {"medium", 300.0f}, {"large", 460.0f}};

std::string PreviewLabel(const std::string& key) {
    if (key == "small") return Tr("Small");
    if (key == "large") return Tr("Large");
    return Tr("Medium");
}

std::string KnownDisplay(const std::string& key) {
    for (const DisplayChoice& choice : kDisplays) {
        if (key == choice.key) return key;
    }
    return "thumbnail";
}

std::string KnownPreview(const std::string& key) {
    for (const PreviewChoice& choice : kPreviews) {
        if (key == choice.key) return key;
    }
    return "medium";
}

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

std::shared_ptr<UCPixmap> MakeFilePicture(const std::string& path, int pixels, const std::string& icons) {
    std::error_code ec;
    if (!fs::exists(PathFromUtf8(path), ec)) return nullptr;   // a note, or a file gone
    const bool folder = fs::is_directory(PathFromUtf8(path), ec);
    if (!folder) {
        if (auto preview = RenderFileThumbnail(path, pixels * 2, pixels, 1.0f)) return preview;
    }
    {
        NativeFileIconThreadScope scope;
        if (auto icon = LoadHostFileIconPixmap(path, folder, pixels)) return icon;
    }
    // No icon theme to ask (a bare system): the framework's own.
    const std::string kind = folder ? "folder" : KindTag(path);
    const std::string fallback = icons + (folder ? "folder-brown.svg" : kind == "pdf" ? "pdf.png" : "document.svg");
    if (auto image = UCImage::GetFresh(fallback)) {
        return image->GetPixmap(pixels, pixels, ImageFitMode::Contain, 1.0f);
    }
    return nullptr;
}

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
    board_->onContextMenu = [this](int cardId, int columnId, const Point2Di& at) {
        ShowContextMenu(cardId, columnId, at);
    };
    board_->onCardHoverChanged = [this](int cardId) { OnCardHover(cardId); };
    AddChild(board_);
    SetDesign(kDesigns[0].key);
}

UltraDesktopCategoryBoard::~UltraDesktopCategoryBoard() {
    if (hoverTimer_ != InvalidTimerId) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(hoverTimer_);
    }
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
    display_ = KnownDisplay(cb.display);
    hoverPreview_ = KnownPreview(cb.hoverPreview);
    SetDesign(cb.design);
    InvalidateLayout();
    RequestRedraw();
}

void UltraDesktopCategoryBoard::SetDesign(const std::string& design) {
    design_ = kDesigns[DesignIndex(design)].key;
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
    board_->SetToday(Today());
    ApplyDisplayStyle();
}

void UltraDesktopCategoryBoard::ApplyDisplayStyle() {
    KanbanBoardStyle& style = board_->EditStyle();
    if (display_ == "text") {
        style.cardImageHeight = 0;   // no pictures: the name and its type
        style.centerCardTitle = false;
    } else if (display_ == "row") {
        style.cardImageHeight = kRowThumbnail;
        style.cardImagePlacement = KanbanCardImagePlacement::Left;
        style.centerCardTitle = false;
    } else {
        style.cardImageHeight = kThumbnailHeight;
        style.cardImagePlacement = KanbanCardImagePlacement::Top;
        style.centerCardTitle = true;
    }
    board_->StyleChanged();
    board_->InvalidateLayout();
    board_->RequestRedraw();
    RequestRedraw();
}

void UltraDesktopCategoryBoard::SetDisplay(const std::string& display) {
    display_ = KnownDisplay(display);
    ApplyDisplayStyle();
    if (desktop_) {
        desktop_->Settings().categoryBoard.display = display_;
        desktop_->SaveSettings();
    }
}

void UltraDesktopCategoryBoard::SetHoverPreview(const std::string& size) {
    hoverPreview_ = KnownPreview(size);
    board_->HideCardPreview();
    if (desktop_) {
        desktop_->Settings().categoryBoard.hoverPreview = hoverPreview_;
        desktop_->SaveSettings();
    }
}

void UltraDesktopCategoryBoard::ApplyDisplaySettings(const CategoryBoardSettings& settings) {
    const std::string display = KnownDisplay(settings.display);
    hoverPreview_ = KnownPreview(settings.hoverPreview);
    if (display != display_) {
        display_ = display;
        ApplyDisplayStyle();
    }
}

float UltraDesktopCategoryBoard::PreviewSize() const {
    for (const PreviewChoice& choice : kPreviews) {
        if (hoverPreview_ == choice.key) return choice.size;
    }
    return kPreviews[1].size;
}

void UltraDesktopCategoryBoard::AddCategory() {
    const int columnId = data_->AddColumn("New category");
    // Renaming scrolls the board to the new category when it is too wide to
    // show them all.
    board_->BeginColumnRename(columnId, /*startEmpty=*/true);
    SaveBoard();
}

void UltraDesktopCategoryBoard::AddFilesToCategory(const std::vector<std::string>& files,
                                                   const std::string& category) {
    int columnId = -1;
    for (const KanbanColumn& column : data_->GetColumns()) {
        if (column.title == category) {
            columnId = column.id;
            break;
        }
    }
    if (columnId < 0) columnId = data_->AddColumn(category);
    AddFiles(files, columnId);
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

std::string UltraDesktopCategoryBoard::CardFile(int cardId) const {
    const KanbanCard* card = data_->FindCard(cardId);
    if (!card || card->description.empty()) return "";
    std::error_code ec;
    if (!fs::exists(PathFromUtf8(card->description), ec)) return "";   // a note, not a file
    return card->description;
}

void UltraDesktopCategoryBoard::OpenCardFile(int cardId) {
    const std::string path = CardFile(cardId);
    if (path.empty()) return;
    board_->CloseCardEditor(/*save=*/false);
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({path}, error)) {
        debugOutput << "UltraDesktop: could not open " << path << ": " << error << std::endl;
    }
}

// ===== THE CONTEXT MENU =====

void UltraDesktopCategoryBoard::ShowContextMenu(int cardId, int columnId, const Point2Di& windowPos) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;
    board_->HideCardPreview();
    menu_ = std::make_shared<UltraCanvasMenu>("CategoryBoard.Menu", 0, 0, 240, 0);
    menu_->SetMenuType(MenuType::PopupMenu);

    const KanbanCard* card = cardId >= 0 ? data_->FindCard(cardId) : nullptr;
    if (card) {
        const std::string file = CardFile(cardId);
        menu_->AddItem(MenuItemData::Header(card->title.empty() ? Tr("Note") : card->title));
        if (!file.empty()) {
            menu_->AddItem(MenuItemData::Action(Tr("Open"), [this, cardId]() { OpenCardFile(cardId); }));
            menu_->AddItem(MenuItemData::Action(Tr("Copy"), [this, cardId]() { CopyCard(cardId); }));
            menu_->AddItem(MenuItemData::Action(TrashAvailable() ? Tr("Delete (move to the trash)") : Tr("Delete"),
                                                [this, cardId]() { DeleteCardFile(cardId); }));
            menu_->AddItem(MenuItemData::Action(Tr("Remove from the board"), [this, cardId]() { RemoveCard(cardId); }));
        } else {
            // A note, or a file that has gone: its text, and the card.
            menu_->AddItem(MenuItemData::Action(Tr("Open"), [this, cardId]() { board_->OpenCardEditor(cardId); }));
            menu_->AddItem(MenuItemData::Action(Tr("Copy"), [this, cardId]() { CopyCard(cardId); }));
            menu_->AddItem(MenuItemData::Action(Tr("Delete"), [this, cardId]() { RemoveCard(cardId); }));
        }
        // The same as dragging it onto another category.
        std::vector<MenuItemData> targets;
        for (const KanbanColumn& column : data_->GetColumns()) {
            if (column.id == card->columnId) continue;
            const int target = column.id;
            targets.push_back(MenuItemData::Action(column.title, [this, cardId, target]() { MoveCardTo(cardId, target); }));
        }
        if (!targets.empty()) menu_->AddItem(MenuItemData::Submenu(Tr("Move to"), targets));
        menu_->AddItem(MenuItemData::Separator());
    } else if (columnId >= 0) {
        menu_->AddItem(MenuItemData::Action(Tr("Rename category"), [this, columnId]() {
            board_->BeginColumnRename(columnId);
        }));
        menu_->AddItem(MenuItemData::Action(Tr("Add category"), [this]() { AddCategory(); }));
        menu_->AddItem(MenuItemData::Separator());
    }

    // Display: how cards show their files, and the hover preview's size.
    std::vector<MenuItemData> display;
    for (const DisplayChoice& choice : kDisplays) {
        const std::string key = choice.key;
        display.push_back(MenuItemData::Radio(DisplayLabel(key), 1, display_ == key, [this, key]() { SetDisplay(key); }));
    }
    display.push_back(MenuItemData::Separator());
    std::vector<MenuItemData> previews;
    for (const PreviewChoice& choice : kPreviews) {
        const std::string key = choice.key;
        previews.push_back(MenuItemData::Radio(PreviewLabel(key), 2, hoverPreview_ == key,
                                               [this, key]() { SetHoverPreview(key); }));
    }
    display.push_back(MenuItemData::Submenu(Tr("Mouse hover preview"), previews));
    menu_->AddItem(MenuItemData::Submenu(Tr("Display"), display));

    PopupElementSettings settings;
    menu_->OpenMenu(windowPos, *window, settings);
}

void UltraDesktopCategoryBoard::CopyCard(int cardId) {
    const std::string file = CardFile(cardId);
    if (!file.empty()) {
        // As the file manager copies it: pasted there, the file is copied.
        if (auto* clipboard = GetClipboard()) clipboard->SetFiles({file});
        return;
    }
    const KanbanCard* card = data_->FindCard(cardId);
    if (!card) return;
    std::string text = card->title;
    if (!card->description.empty()) text += (text.empty() ? "" : "\n") + card->description;
    SetClipboardText(text);
}

void UltraDesktopCategoryBoard::DeleteCardFile(int cardId) {
    const std::string file = CardFile(cardId);
    if (file.empty()) return;
    const bool trash = TrashAvailable();
    const std::string question = trash ? TrF("Move \"{0}\" to the {1}?", DisplayName(file), TrashDisplayName())
                                       : TrF("Delete \"{0}\" for good? It cannot be brought back.", DisplayName(file));
    UltraCanvasDialogManager::ShowConfirmation(question, Tr("Sticker board"), [this, cardId, file, trash](bool yes) {
        if (!yes) return;
        std::string error;
        bool gone = false;
        if (trash) {
            gone = MoveToTrash(file, error);
        } else {
            std::error_code ec;
            fs::remove_all(PathFromUtf8(file), ec);
            gone = !ec;
            if (ec) error = ec.message();
        }
        if (!gone) {
            UltraCanvasDialogManager::ShowWarning(TrF("\"{0}\" was not deleted: {1}", DisplayName(file), error),
                                                  Tr("Sticker board"), nullptr, GetWindow());
            return;
        }
        RemoveCard(cardId);
    }, GetWindow());
}

void UltraDesktopCategoryBoard::RemoveCard(int cardId) {
    board_->HideCardPreview();
    if (board_->GetSelectedCardId() == cardId) board_->SelectCard(-1);
    if (!data_->RemoveCard(cardId)) return;
    SaveBoard();
    board_->InvalidateLayout();
    board_->RequestRedraw();
}

void UltraDesktopCategoryBoard::MoveCardTo(int cardId, int columnId) {
    if (!data_->MoveCard(cardId, columnId)) return;
    board_->ScrollToCard(cardId);
    SaveBoard();
    board_->InvalidateLayout();
    board_->RequestRedraw();
}

// ===== THE HOVER PREVIEW =====

void UltraDesktopCategoryBoard::OnCardHover(int cardId) {
    hoverCardId_ = cardId;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (hoverTimer_ != InvalidTimerId) {
        app->StopTimer(hoverTimer_);
        hoverTimer_ = InvalidTimerId;
    }
    if (cardId < 0 || CardFile(cardId).empty()) return;
    hoverTimer_ = app->StartTimer(kHoverPreviewDelayMs, /*periodic=*/false, [this, cardId](TimerId) {
        hoverTimer_ = InvalidTimerId;
        if (hoverCardId_ == cardId) ShowHoverPreview(cardId);
    });
}

void UltraDesktopCategoryBoard::ShowHoverPreview(int cardId) {
    const std::string path = CardFile(cardId);
    if (path.empty()) return;
    const float size = PreviewSize();
    const int pixels = static_cast<int>(size);
    const std::string key = std::to_string(pixels) + "|" + path;
    auto ready = previews_.find(key);
    if (ready != previews_.end()) {
        board_->ShowCardPreview(cardId, ready->second, size);
        return;
    }
    if (previewPending_.count(key)) return;
    previewPending_.insert(key);
    const std::string icons = NormalizePath(GetResourcesDir() + "media/icons/");
    work_.Run<std::shared_ptr<UCPixmap>>(
        [path, pixels, icons]() { return MakeFilePicture(path, pixels, icons); },
        [this, key, cardId, size](std::shared_ptr<UCPixmap> pixmap) {
            previewPending_.erase(key);
            if (!pixmap) return;
            previews_[key] = pixmap;
            // Shown only while the pointer is still on that card.
            if (hoverCardId_ == cardId) board_->ShowCardPreview(cardId, pixmap, size);
        });
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
        [path, icons]() { return MakeFilePicture(path, kThumbnailPixels, icons); },
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

// Apps/UltraPress/ui/UltraPressMediaDialog.cpp
// See UltraPressMediaDialog.h.
// Version: 0.7.1 - a picture's pixels in the row; Remove unused
// Version: 0.6.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressMediaDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"

#include "UltraPressMedia.h"

#include <memory>
#include <vector>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const Color kSurface(255, 255, 255, 255);
const Color kInk(32, 32, 32, 255);
const Color kInkSoft(90, 90, 98, 255);
const Color kAccent(169, 7, 7, 255);
const Color kRow(246, 246, 249, 255);
const Color kRowSelected(255, 232, 232, 255);
const Color kTile(230, 230, 236, 255);
const char* kFont = "Ubuntu";

// Everything the rows and buttons need to reach, owned by their callbacks
// for as long as the dialog lives. The dialog itself is a raw pointer: it
// owns the widgets, so a shared_ptr here would be a cycle.
struct State {
    std::string siteDir;
    const Site* site = nullptr;
    bool pick = false;
    std::function<void(const std::string&)> onPicked;
    std::function<void()> onChanged;
    std::vector<MediaFile> files;
    int selected = -1;
    UltraCanvasModalDialog* dlg = nullptr;
    std::shared_ptr<UltraCanvasContainer> list;
    std::shared_ptr<UltraCanvasLabel> status;
    std::shared_ptr<UltraCanvasButton> removeBtn, pruneBtn, chooseBtn;
};

std::shared_ptr<UltraCanvasButton> PlainButton(const std::string& id, const std::string& text, float width, bool primary) {
    auto button = CreateButton(id, 0, 0, width, 32, text);
    button->SetFont(kFont, 14, primary ? FontWeight::Bold : FontWeight::Normal);
    button->SetColors(primary ? kAccent : kRow, primary ? Color(130, 5, 5, 255) : kTile);
    button->SetTextColors(primary ? kSurface : kInk);
    button->SetBorder(0, Colors::Transparent);
    button->SetCornerRadius(6);
    button->SetElementSize(CSSLayout::Dimension::Px(width), CSSLayout::Dimension::Px(32));
    button->layoutItem.SetFlexShrink(0);
    button->SetMouseCursor(UCMouseCursor::Hand);
    return button;
}

void Refill(const std::shared_ptr<State>& st) {
    st->files = ListMedia(st->siteDir, st->site);
    if (st->selected >= static_cast<int>(st->files.size())) st->selected = -1;
    st->list->ClearChildren();
    if (st->files.empty()) {
        auto empty = CreateLabel("MediaEmpty", "No files yet. Add file\xE2\x80\xA6 copies a picture or a download into the site's media folder.");
        empty->SetFont(kFont, 14);
        empty->SetTextColor(kInkSoft);
        empty->SetWrap(TextWrap::WrapWord);
        empty->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
        st->list->AddChild(empty);
    }
    for (size_t i = 0; i < st->files.size(); ++i) {
        const MediaFile& file = st->files[i];
        const bool active = static_cast<int>(i) == st->selected;
        auto row = CreateContainer("MediaRow" + std::to_string(i), 0, 0, 0, 64);
        row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(64));
        row->layoutItem.SetFlexShrink(0);
        row->SetBackgroundColor(active ? kRowSelected : kRow);
        row->SetBorderRadius(6);
        row->SetPadding(4, 6);
        if (file.kind == "image") {
            auto thumb = CreateImageFromFile("MediaThumb" + std::to_string(i), 0, 0, 76, 56, st->siteDir + "/" + file.path);
            thumb->SetFitMode(ImageFitMode::Contain);
            thumb->SetElementSize(CSSLayout::Dimension::Px(76), CSSLayout::Dimension::Px(56));
            thumb->layoutItem.SetFlexShrink(0);
            row->AddChild(thumb);
        } else {
            auto tile = CreateContainer("MediaTile" + std::to_string(i), 0, 0, 76, 56);
            tile->SetElementSize(CSSLayout::Dimension::Px(76), CSSLayout::Dimension::Px(56));
            tile->layoutItem.SetFlexShrink(0);
            tile->SetBackgroundColor(kTile);
            tile->SetBorderRadius(6);
            tile->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            const size_t dot = file.name.rfind('.');
            auto ext = CreateLabel("MediaExt" + std::to_string(i), dot == std::string::npos ? file.kind : file.name.substr(dot + 1));
            ext->SetFont(kFont, 12, FontWeight::Bold);
            ext->SetTextColor(kInkSoft);
            tile->AddChild(ext);
            row->AddChild(tile);
        }
        // The row's text is a button, so the row is picked by a click on
        // its text - the one element here that has a click.
        const std::string uses = file.usedBy == 0 ? "unused" : "used " + std::to_string(file.usedBy) + "\xC3\x97";
        const std::string pixels = file.width > 0 ? std::to_string(file.width) + " \xC3\x97 " + std::to_string(file.height) + ", " : "";
        auto button = CreateButton("MediaPick" + std::to_string(i), 0, 0, 0, 56,
                                   file.path + "\n" + file.kind + ", " + pixels + HumanSize(file.size) + ", " + uses);
        button->SetFont(kFont, 13, active ? FontWeight::Bold : FontWeight::Normal);
        button->SetColors(Colors::Transparent, Color(0, 0, 0, 12));
        button->SetTextColors(kInk);
        button->SetBorder(0, Colors::Transparent);
        button->SetTextAlign(TextAlignment::Left);
        button->SetPadding(0, 6);
        button->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(56));
        button->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        button->SetMouseCursor(UCMouseCursor::Hand);
        const int index = static_cast<int>(i);
        button->onClick = [st, index]() {
            st->selected = index;
            Refill(st);
        };
        row->AddChild(button);
        st->list->AddChild(row);
    }
    const bool any = st->selected >= 0;
    st->removeBtn->SetDisabled(!any || st->files[static_cast<size_t>(st->selected)].usedBy > 0);
    bool anyUnused = false;
    for (const MediaFile& file : st->files) anyUnused = anyUnused || file.usedBy == 0;
    st->pruneBtn->SetDisabled(!anyUnused);
    if (st->chooseBtn) st->chooseBtn->SetDisabled(!any);
    if (any) {
        const MediaFile& file = st->files[static_cast<size_t>(st->selected)];
        st->status->SetTextColor(kInkSoft);
        st->status->SetText(file.usedBy > 0 ? file.path + " is used " + std::to_string(file.usedBy) + " time(s); a used file cannot be removed"
                                           : file.path);
    } else {
        st->status->SetText(std::to_string(st->files.size()) + " file(s) in media/");
    }
}

} // namespace

void ShowMediaLibrary(UltraCanvasWindowBase* parent, const std::string& siteDirectory, const Site& site, bool pick,
                      std::function<void(const std::string& mediaPath)> onPicked, std::function<void()> onChanged,
                      const std::string& preselect) {
    DialogConfig config;
    config.title = pick ? "Choose a picture" : "Media library";
    config.width = 640;
    config.height = 560;
    config.dialogType = DialogType::Custom;
    config.buttons = DialogButtons::NoButtons;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);

    auto st = std::make_shared<State>();
    st->siteDir = siteDirectory;
    st->site = &site;
    st->pick = pick;
    st->onPicked = std::move(onPicked);
    st->onChanged = std::move(onChanged);
    st->dlg = dialog.get();

    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);
    dialog->SetBackgroundColor(kSurface);

    auto heading = CreateLabel("MediaHeading", "The site's media folder: pictures for the blocks, files for the downloads.");
    heading->SetFont(kFont, 13);
    heading->SetTextColor(kInkSoft);
    heading->SetWrap(TextWrap::WrapWord);
    heading->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    dialog->AddChild(heading);

    st->list = CreateScrollableContainer("MediaList", 0, 0, 0, 0, true, false);
    st->list->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    st->list->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(200));
    st->list->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    dialog->AddChild(st->list);

    st->status = CreateLabel("MediaStatus", "");
    st->status->SetFont(kFont, 13);
    st->status->SetTextColor(kInkSoft);
    st->status->SetWrap(TextWrap::WrapWord);
    st->status->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(36));
    st->status->layoutItem.SetFlexShrink(0);
    dialog->AddChild(st->status);

    auto buttons = CreateContainer("MediaButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Px(36));
    buttons->layoutItem.SetFlexShrink(0);
    auto addBtn = PlainButton("MediaAdd", "Add file\xE2\x80\xA6", 110, false);
    st->removeBtn = PlainButton("MediaRemove", "Remove", 90, false);
    st->pruneBtn = PlainButton("MediaPrune", "Remove unused", 150, false);
    buttons->AddChild(addBtn);
    buttons->AddChild(st->removeBtn);
    buttons->AddChild(st->pruneBtn);
    buttons->AddStretchSpacer(1);
    auto closeBtn = PlainButton("MediaClose", pick ? "Cancel" : "Close", 90, false);
    buttons->AddChild(closeBtn);
    if (pick) {
        st->chooseBtn = PlainButton("MediaChoose", "Choose", 110, true);
        buttons->AddChild(st->chooseBtn);
    }
    dialog->AddChild(buttons);

    addBtn->onClick = [st, parent]() {
        FileDialogOptions opts;
        opts.SetTitle("Add to the media folder")
            .AddFilter("Pictures", std::vector<std::string>{"png", "jpg", "jpeg", "gif", "webp", "svg", "avif"})
            .AddFilter("All files", std::vector<std::string>{"*"})
            .SetRegisterAsRecent(false)
            .SetParentWindow(parent);
        UltraCanvasFileLoader::OpenFileDialog(opts, [st](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK) return;
            std::string mediaPath, error;
            if (!ImportMedia(st->siteDir, path, mediaPath, error)) {
                st->status->SetTextColor(kAccent);
                st->status->SetText(error);
                return;
            }
            Refill(st);
            for (size_t i = 0; i < st->files.size(); ++i) {
                if (st->files[i].path == mediaPath) st->selected = static_cast<int>(i);
            }
            Refill(st);
            if (st->onChanged) st->onChanged();
        });
    };
    st->removeBtn->onClick = [st]() {
        if (st->selected < 0) return;
        std::string error;
        if (!RemoveMedia(st->siteDir, st->files[static_cast<size_t>(st->selected)].path, error, st->site)) {
            st->status->SetTextColor(kAccent);
            st->status->SetText(error);
            return;
        }
        st->selected = -1;
        Refill(st);
        if (st->onChanged) st->onChanged();
    };
    st->pruneBtn->onClick = [st]() {
        std::vector<std::string> removed, errors;
        PruneMedia(st->siteDir, *st->site, removed, errors);
        st->selected = -1;
        Refill(st);
        st->status->SetTextColor(errors.empty() ? kInkSoft : kAccent);
        st->status->SetText(std::to_string(removed.size()) + " unused file(s) removed" + (errors.empty() ? "" : "; " + errors.front()));
        if (!removed.empty() && st->onChanged) st->onChanged();
    };
    closeBtn->onClick = [st]() { st->dlg->CloseDialog(DialogResult::Cancel); };
    if (st->chooseBtn) {
        st->chooseBtn->onClick = [st]() {
            if (st->selected < 0) return;
            const std::string path = st->files[static_cast<size_t>(st->selected)].path;
            st->dlg->CloseDialog(DialogResult::OK);
            if (st->onPicked) st->onPicked(path);
        };
    }

    Refill(st);
    for (size_t i = 0; i < st->files.size(); ++i) {
        if (!preselect.empty() && st->files[i].path == preselect) st->selected = static_cast<int>(i);
    }
    if (st->selected >= 0) Refill(st);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraPress

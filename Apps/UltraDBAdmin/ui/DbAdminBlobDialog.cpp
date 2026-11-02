// Apps/UltraDBAdmin/ui/DbAdminBlobDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminBlobDialog.h"

#include "DbAdminBlob.h"
#include "DbAdminFormat.h"
#include "DbAdminTheme.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextArea.h"

#include <memory>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

// The part of the dialog that shows the bytes, filling what the label and
// the buttons leave.
void Fill(const std::shared_ptr<UltraCanvasUIElement>& element) {
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1)
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// The bytes as text: as they are when they read as text, else a hex dump.
std::shared_ptr<UltraCanvasTextArea> TextView(const std::vector<uint8_t>& bytes, bool text) {
    auto area = std::make_shared<UltraCanvasTextArea>("dbbText");
    area->ApplyPlainTextStyle();
    area->SetFont("monospace", 11.0f);
    area->SetShowLineNumbers(false);
    area->SetWordWrap(text);
    area->SetText(text ? std::string(bytes.begin(), bytes.end()) : HexDump(bytes), false);
    area->SetReadOnly(true);
    area->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
    return area;
}

// Runs `action` after the dialog has closed: the button that asked belongs
// to the dialog, so the request is copied out first.
std::function<void()> CloseThen(UltraCanvasModalDialog* dialog, std::function<void()> action) {
    return [dialog, action]() {
        const std::function<void()> then = action;
        dialog->CloseDialog(DialogResult::OK);
        if (then) then();
    };
}

} // namespace

void ShowBlobDialog(UltraCanvasWindowBase* parent, const std::string& title, const std::vector<uint8_t>& bytes,
                    std::function<void()> onSave, std::function<void()> onLoad) {
    DialogConfig config;
    config.title      = title;
    config.width      = 760;
    config.height     = 540;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetFollowTheme(true);
    UltraCanvasModalDialog* raw = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    const BlobKind kind = SniffBlob(bytes);
    std::string about = kind.name + ", " + HumanSize(static_cast<int64_t>(bytes.size()));
    std::shared_ptr<UltraCanvasUIElement> view;
    if (kind.image) {
        // A picture, fitted to the dialog; one the decoder refuses after all
        // (a damaged file with a good signature) falls back to its bytes.
        auto image = std::make_shared<UltraCanvasImageElement>("dbbImage");
        image->SetFitMode(ImageFitMode::Contain);
        if (image->LoadFromImage(UCImage::LoadFromMemory(bytes))) {
            image->SetBackgroundColor(Color(246, 247, 250));
            image->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
            view = image;
        } else {
            about += " - it does not decode as a picture: " + image->GetLastError();
        }
    }
    if (!view) {
        view = TextView(bytes, kind.text);
        if (!kind.text) about += " - shown as hex: offset, bytes, characters";
    }

    auto label = CreateLabel("dbbAbout", 0, 0, 0, 24, about);
    label->SetTextColor(StatusColor());
    label->layoutItem.SetFlexShrink(0);
    dialog->AddChild(label);
    Fill(view);
    dialog->AddChild(view);

    auto buttons = CreateContainer("dbbButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    auto save = MakeDialogButton("dbbSave", "Save to file", 120);
    save->SetIcon(IconPath("export"));
    save->SetIconSize(16, 16);
    save->SetOnClick(CloseThen(raw, std::move(onSave)));
    buttons->AddChild(save);
    if (onLoad) {
        auto load = MakeDialogButton("dbbLoad", "Load from file", 130);
        load->SetIcon(IconPath("import"));
        load->SetIconSize(16, 16);
        load->SetTooltip("Stage a file's bytes for this cell; Commit writes them");
        load->SetOnClick(CloseThen(raw, std::move(onLoad)));
        buttons->AddChild(load);
    }
    buttons->AddStretchSpacer(1);
    auto close = MakeDialogButton("dbbClose", "Close", 90, ButtonRole::Primary);
    close->SetOnClick([raw]() { raw->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(close);
    dialog->AddChild(buttons);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin

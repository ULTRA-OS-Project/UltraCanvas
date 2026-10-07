// Apps/UltraFiler/UltraFilerRamDiskDialog.cpp
// Implementation of the new RAM disc dialog. Construction follows
// UltraFilerFindTextDialog (flex-column content, custom Create / Cancel
// buttons so Create can refuse a bad name without closing).
// Version: 1.0.1 - the name field holds what the system can keep
// Author: UltraCanvas Framework

#include "UltraFilerRamDiskDialog.h"
#include "UltraFilerRamDisks.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

namespace {

constexpr float kContentWidth = 428.0f;
constexpr uint64_t kMiB = 1024ull * 1024;

// The sizes offered, smallest first. Powers of two because that is how memory
// is counted and sold; a size between two of them is not a real need.
const uint64_t kPresetSizes[] = {
    64 * kMiB,        128 * kMiB,       256 * kMiB,       512 * kMiB,
    1024 * kMiB,      2048 * kMiB,      4096 * kMiB,      8192 * kMiB,
    16384 * kMiB,     32768 * kMiB,     65536 * kMiB,
};

// What the chosen size means here, and what happens to the files. Shown under
// the size so nobody is surprised by either.
std::string PlatformNote(uint64_t largest) {
    const std::string freeText =
            largest > 0 ? UltraFilerRamDisks::FormatSize(largest) + " is free now. "
                        : std::string();
    std::string note;
    if (!UltraFilerRamDisks::TrueRamAvailable()) {
        note = "No RAM disc driver (ImDisk) is installed, so this disc will be "
               "a folder on the system drive, wiped when it is ejected - it "
               "is not held in memory. ";
    } else if (UltraFilerRamDisks::SizeIsEnforced()) {
        note = "The memory is reserved when the disc is created. " + freeText;
    } else {
        note = "All RAM discs share the system's memory area, and memory is "
               "used only as files are written - the size is checked against "
               "what is free, not reserved. " + freeText;
    }
    return note + "Everything on the disc is lost when it is ejected or the "
                  "computer restarts.";
}

}  // namespace

void UltraFilerRamDiskDialog::Initialize() {
    DialogConfig cfg;
    cfg.title = "New RAM disc";
    cfg.width = 460;
    cfg.height = 330;
    cfg.resizable = false;
    cfg.buttons = DialogButtons::NoButtons;
    // Custom type: skip the built-in icon/message/footer layout so its
    // grow-section can't eat the space our own controls need.
    cfg.dialogType = DialogType::Custom;
    CreateDialog(cfg);

    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layout.SetFlexGap(10);
    SetPadding(16);

    auto content = std::make_shared<UltraCanvasContainer>(
        "uf-ramdisk-content", 0, 0, kContentWidth, 236);
    content->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layout.SetFlexGap(6);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-name-label", 0, 0, kContentWidth, 20, "Name:"));
    nameInput_ = std::make_shared<UltraCanvasTextInput>(
        "uf-ramdisk-name", 0, 0, kContentWidth, 30.0f);
    // 23 on Windows, where the name has to fit the disc's volume label.
    nameInput_->SetMaxLength(static_cast<int>(UltraFilerRamDisks::MaxNameLength()));
    nameInput_->SetText(UltraFilerRamDisks::SuggestName());
    nameInput_->onEnterPressed = [this](const std::string&) {
        Accept();
        return true;
    };
    content->AddChild(nameInput_);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-size-label", 0, 0, kContentWidth, 20, "Size:"));
    sizeDropdown_ = std::make_shared<UltraCanvasDropdown>(
        "uf-ramdisk-size", 0, 0, kContentWidth, 30.0f);
    // Only sizes that fit: a choice that can only fail is not a choice. When
    // free memory cannot be read every size is offered and Create says so if
    // one does not fit.
    const uint64_t largest = UltraFilerRamDisks::LargestPossibleBytes();
    int preselect = 0;
    for (uint64_t size : kPresetSizes) {
        if (largest > 0 && size > largest && !sizes_.empty()) break;
        sizes_.push_back(size);
        sizeDropdown_->AddItem(UltraFilerRamDisks::FormatSize(size));
        // 512 MB when it fits: enough for a build tree or a batch of photos,
        // small enough not to squeeze anything else out of memory.
        if (size <= 512 * kMiB) preselect = static_cast<int>(sizes_.size()) - 1;
    }
    sizeDropdown_->SetSelectedIndex(preselect, false);
    content->AddChild(sizeDropdown_);

    auto note = std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-note", 0, 0, kContentWidth, 72, PlatformNote(largest));
    note->SetWrap(TextWrap::WrapWord);
    content->AddChild(note);

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-error", 0, 0, kContentWidth, 20, "");
    errorLabel_->SetTextColor(Color(200, 30, 30));
    content->AddChild(errorLabel_);

    AddChild(content);

    auto buttons = std::make_shared<UltraCanvasContainer>(
        "uf-ramdisk-buttons", 0, 0, kContentWidth, 34);
    buttons->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    buttons->layout.SetFlexGap(8);
    auto createBtn = std::make_shared<UltraCanvasButton>(
        "uf-ramdisk-create", 0, 0, 100, 30);
    createBtn->SetText("Create");
    createBtn->onClick = [this]() { Accept(); };
    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "uf-ramdisk-cancel", 0, 0, 100, 30);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    buttons->AddChild(createBtn);
    buttons->AddChild(cancelBtn);
    AddChild(buttons);
}

void UltraFilerRamDiskDialog::FocusInitialElement() {
    if (sizeDropdown_) {
        SetFocusedElement(sizeDropdown_.get());
        return;
    }
    UltraCanvasModalDialog::FocusInitialElement();
}

void UltraFilerRamDiskDialog::Accept() {
    const std::string name = nameInput_ ? nameInput_->GetText() : std::string();
    if (!UltraFilerRamDisks::IsValidName(name)) {
        if (errorLabel_) {
            const std::size_t maxLength = UltraFilerRamDisks::MaxNameLength();
            errorLabel_->SetText(name.empty()
                    ? std::string("Give the disc a name.")
                    : name.size() > maxLength
                    ? "At most " + std::to_string(maxLength) + " characters."
                    : std::string("Use letters, digits, '.', '_' and '-' only."));
        }
        return;
    }
    const int index = sizeDropdown_ ? sizeDropdown_->GetSelectedIndex() : -1;
    if (index < 0 || index >= static_cast<int>(sizes_.size())) return;
    const uint64_t sizeBytes = sizes_[static_cast<size_t>(index)];
    // Copied: closing may release the last reference to this dialog.
    auto callback = onCreate;
    CloseDialog(DialogResult::OK);
    if (callback) callback(name, sizeBytes);
}

std::shared_ptr<UltraFilerRamDiskDialog> ShowRamDiskDialog(
    std::function<void(const std::string& name, uint64_t sizeBytes)> onCreate,
    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraFilerRamDiskDialog>();
    dialog->Initialize();
    dialog->onCreate = std::move(onCreate);
    dialog->ShowModal(parent);
    return dialog;
}

}  // namespace UltraCanvas

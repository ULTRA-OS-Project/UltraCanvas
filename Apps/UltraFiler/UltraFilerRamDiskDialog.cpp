// Apps/UltraFiler/UltraFilerRamDiskDialog.cpp
// Implementation of the new RAM disc dialog. Construction follows
// UltraFilerFindTextDialog (flex-column content, custom Create / Cancel
// buttons so Create can refuse a bad name without closing).
// Version: 1.1.4 - the error colour is the theme's danger (UltraFilerLook.h)
// Version: 1.1.3 - the size notes and the name errors go through Tr
// Version: 1.1.2 - the note under the size is as tall as its text, the window as tall
//                  as its content, and the buttons sit at the bottom right
// Version: 1.1.1 - the interface strings go through Tr (UltraLocalisation)
// Version: 1.1.0 - the facts about the machine come from the caller
// Author: UltraCanvas Framework

#include "UltraFilerRamDiskDialog.h"
#include "UltraFilerLook.h"
#include "UltraFilerRamDisks.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cmath>

namespace UltraCanvas {

namespace {

constexpr float kContentWidth = 428.0f;
constexpr float kPadding = 16.0f;
constexpr float kSectionGap = 10.0f;    // between the controls and the buttons
constexpr float kRowGap = 6.0f;         // between the rows of the controls
constexpr float kLabelHeight = 20.0f;
constexpr float kFieldHeight = 30.0f;
constexpr float kButtonRowHeight = 34.0f;
// Room for the note when it cannot be measured (no render context yet):
// six lines, the longest the note runs to in English.
constexpr float kFallbackNoteHeight = 120.0f;
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
// Whole sentences, each translated on its own and joined with a space, so a
// translation never has to fit around a piece of another sentence.
std::string PlatformNote(uint64_t largest) {
    std::string note;
    if (!UltraFilerRamDisks::TrueRamAvailable()) {
        // Keeps its trailing space: it is the key the catalogues know it by.
        note = Tr("No RAM disc driver (ImDisk) is installed, so this disc will be "
               "a folder on the system drive, wiped when it is ejected - it "
               "is not held in memory. ");
    } else {
        note = UltraFilerRamDisks::SizeIsEnforced()
                ? Tr("The memory is reserved when the disc is created.")
                : Tr("All RAM discs share the system's memory area, and memory is "
                     "used only as files are written - the size is checked against "
                     "what is free, not reserved.");
        if (largest > 0)
            note += " " + TrF("{0} is free now.", UltraFilerRamDisks::FormatSize(largest));
        note += " ";
    }
    return note + Tr("Everything on the disc is lost when it is ejected or the "
                     "computer restarts.");
}

}  // namespace

void UltraFilerRamDiskDialog::Initialize(const RamDiskDialogFacts& facts) {
    DialogConfig cfg;
    cfg.title = Tr("New RAM disc");
    cfg.width = 460;
    cfg.height = 330;
    cfg.resizable = false;
    cfg.buttons = DialogButtons::NoButtons;
    // Custom type: skip the built-in icon/message/footer layout so its
    // grow-section can't eat the space our own controls need.
    cfg.dialogType = DialogType::Custom;
    CreateDialog(cfg);

    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    layout.SetFlexGap(kSectionGap);
    SetPadding(kPadding);

    // Its height is set below, once the note has been measured.
    auto content = std::make_shared<UltraCanvasContainer>(
        "uf-ramdisk-content", 0, 0, kContentWidth, 200);
    content->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layout.SetFlexGap(kRowGap);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-name-label", 0, 0, kContentWidth, kLabelHeight, Tr("Name:")));
    nameInput_ = std::make_shared<UltraCanvasTextInput>(
        "uf-ramdisk-name", 0, 0, kContentWidth, kFieldHeight);
    // 23 on Windows, where the name has to fit the disc's volume label.
    nameInput_->SetMaxLength(static_cast<int>(UltraFilerRamDisks::MaxNameLength()));
    nameInput_->SetText(facts.suggestedName);
    nameInput_->onEnterPressed = [this](const std::string&) {
        Accept();
        return true;
    };
    content->AddChild(nameInput_);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-size-label", 0, 0, kContentWidth, kLabelHeight, Tr("Size:")));
    sizeDropdown_ = std::make_shared<UltraCanvasDropdown>(
        "uf-ramdisk-size", 0, 0, kContentWidth, kFieldHeight);
    // Only sizes that fit: a choice that can only fail is not a choice. When
    // free memory cannot be read every size is offered and Create says so if
    // one does not fit.
    const uint64_t largest = facts.largestBytes;
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

    // The note runs from four lines to six, longer in other languages and
    // at a larger font: a fixed box cut its last line off. It is measured
    // at the width it wraps at once it is in the window, and everything
    // below it - the content, the window - follows from that height.
    auto note = std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-note", 0, 0, kContentWidth, kFallbackNoteHeight, PlatformNote(largest));
    note->SetWrap(TextWrap::WrapWord);
    content->AddChild(note);

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "uf-ramdisk-error", 0, 0, kContentWidth, kLabelHeight, "");
    errorLabel_->SetTextColor(Look().danger);
    content->AddChild(errorLabel_);

    AddChild(content);

    float noteHeight = kFallbackNoteHeight;
    {
        CSSLayout::LayoutContext lctx;
        const Size2Df measured = note->MeasureOwnContent(kContentWidth, lctx);
        if (measured.height > 0.0f) noteHeight = std::ceil(measured.height) + 4.0f;
    }
    note->SetElementSize(Size2Df(kContentWidth, noteHeight));
    // Name label, name, size label, size, note, error line - and a gap
    // between each pair.
    const float contentHeight = 3 * kLabelHeight + 2 * kFieldHeight + noteHeight + 5 * kRowGap;
    content->SetElementSize(Size2Df(kContentWidth, contentHeight));
    SetWindowSize(static_cast<int>(cfg.width),
                  static_cast<int>(std::ceil(2 * kPadding + contentHeight + kSectionGap +
                                             kButtonRowHeight)));

    // Create and Cancel at the bottom right, as in every other dialog.
    auto buttons = std::make_shared<UltraCanvasContainer>(
        "uf-ramdisk-buttons", 0, 0, kContentWidth, kButtonRowHeight);
    buttons->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center)
                   .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd);
    buttons->layout.SetFlexGap(8);
    auto createBtn = std::make_shared<UltraCanvasButton>(
        "uf-ramdisk-create", 0, 0, 100, 30);
    createBtn->SetText(Tr("Create"));
    createBtn->onClick = [this]() { Accept(); };
    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "uf-ramdisk-cancel", 0, 0, 100, 30);
    cancelBtn->SetText(Tr("Cancel"));
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
                    ? Tr("Give the disc a name.")
                    : name.size() > maxLength
                    ? TrN("At most {n} character.", "At most {n} characters.",
                          static_cast<long long>(maxLength))
                    : Tr("Use letters, digits, '.', '_' and '-' only."));
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

RamDiskDialogFacts GatherRamDiskDialogFacts() {
    RamDiskDialogFacts facts;
    facts.largestBytes = UltraFilerRamDisks::LargestPossibleBytes();
    facts.suggestedName = UltraFilerRamDisks::SuggestName();
    return facts;
}

std::shared_ptr<UltraFilerRamDiskDialog> ShowRamDiskDialog(
    const RamDiskDialogFacts& facts,
    std::function<void(const std::string& name, uint64_t sizeBytes)> onCreate,
    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraFilerRamDiskDialog>();
    dialog->Initialize(facts);
    dialog->onCreate = std::move(onCreate);
    dialog->ShowModal(parent);
    return dialog;
}

}  // namespace UltraCanvas

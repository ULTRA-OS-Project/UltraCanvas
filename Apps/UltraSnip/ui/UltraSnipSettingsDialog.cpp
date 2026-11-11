// Apps/UltraSnip/ui/UltraSnipSettingsDialog.cpp
// See the header. The window, the page skeleton and the controls come from
// the framework's UltraCanvasSettingsPageKit, which UltraFiler's settings
// window is built on too, so the two read as one family.
// Version: 0.4.0 - After a snip: the Print Screen key starts a snip
// Version: 0.3.0 - the texts go through Tr (UltraLocalisation); "Translate UltraSnip..." on the
//                  start page opens the localisation editor on UltraSnip's own set
// Version: 0.2.0 - on UltraCanvasSettingsPageKit
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipSettingsDialog.h"

#include "UltraSnipRecorder.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasLocalisationEditor.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <VideoFX/VideoFX.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraSnip {

namespace {

using namespace UltraCanvas::SettingsPageKit;


// Page ids double as tree node ids.
constexpr const char* kPagePictures = "pictures";
constexpr const char* kPagePictureType = "pictures/type";
constexpr const char* kPagePictureSaving = "pictures/saving";
constexpr const char* kPageVideos = "videos";
constexpr const char* kPageRecording = "videos/recording";
constexpr const char* kPageBehaviour = "behaviour";
constexpr const char* kPageAfterSnip = "behaviour/after";
constexpr const char* kPageStart = "start";

// The one open settings window: the kit's shell with this window's controls
struct DialogState : public UltraCanvasSettingsWindow {
    bool syncing = false;

    // Pictures > File type: one radio per recommended type, the "another
    // type" radio with its list, and the type's own settings button
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvasRadio>>> formatRadios;
    std::shared_ptr<UltraCanvasRadio> otherFormatRadio;
    UltraCanvasRadioGroup formatGroup;
    std::shared_ptr<UltraCanvasDropdown> otherFormats;
    std::vector<std::string> otherFormatExtensions;    // by the dropdown's index
    std::shared_ptr<UltraCanvasButton> formatSettingsButton;
    std::shared_ptr<UltraCanvasLabel> formatLine;

    // Pictures > Saving
    std::shared_ptr<UltraCanvasCheckbox> autoSaveBox;
    std::shared_ptr<UltraCanvasCheckbox> autoCopyBox;
    std::shared_ptr<UltraCanvasLabel> pictureFolderLine;

    // Videos > Recording
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvasRadio>>> videoRadios;
    UltraCanvasRadioGroup videoGroup;
    std::shared_ptr<UltraCanvasSlider> frameRateSlider;
    std::shared_ptr<UltraCanvasLabel> frameRateValue;
    std::shared_ptr<UltraCanvasLabel> videoFolderLine;

    // Behaviour > After a snip
    std::shared_ptr<UltraCanvasRadio> showWindowRadio;
    std::shared_ptr<UltraCanvasRadio> stayHiddenRadio;
    UltraCanvasRadioGroup afterGroup;
    std::shared_ptr<UltraCanvasCheckbox> printKeyBox;

    UltraSnipSettings* settings = nullptr;
    std::function<void()> onChanged;
};

std::shared_ptr<DialogState> g_dialog;

void ApplyAndSave(DialogState* d) {
    if (d->syncing) return;
    if (d->onChanged) d->onChanged();
    if (d->settings) d->settings->Save();
}

// ===== TEXT =====

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                            float fontSize = kTextFontSize, const Color& color = kTextColor) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text, int width = kTextWidth,
                                           float fontSize = kTextFontSize, const Color& color = kTextColor) {
    auto l = MakeLabel(id, text, fontSize, color);
    l->SetWrap(TextWrap::WrapWord);
    l->size.width = CSSLayout::Dimension::Px(width);
    return l;
}

// ===== PAGE SKELETON =====

struct PageParts {
    std::shared_ptr<UltraCanvasContainer> page;
    std::shared_ptr<UltraCanvasContainer> body;
    std::shared_ptr<UltraCanvasContainer> notes;
};

std::shared_ptr<UltraCanvasContainer> MakeNotesBlock(const std::string& id) {
    auto notes = std::make_shared<UltraCanvasContainer>(id);
    notes->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    notes->SetBackgroundColor(kNoteBackground);
    notes->SetBorderLeft(3, kNoteAccent);
    notes->SetPadding(10, 12, 10, 12);
    return notes;
}

PageParts MakePage(const std::string& id, const std::string& title, const std::string& caption) {
    PageParts parts;
    parts.page = std::make_shared<UltraCanvasContainer>(id);
    parts.page->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);

    auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
    titleLabel->SetFontWeight(FontWeight::Bold);
    parts.page->AddChild(titleLabel);

    auto captionLabel = MakeText(id + "-caption", caption);
    captionLabel->SetMargin(6, 0, 0, 0);
    parts.page->AddChild(captionLabel);

    parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
    parts.body->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.body->SetMargin(14, 0, 0, 0);
    parts.page->AddChild(parts.body);

    // The spacer pushes the notes to the foot of the page, away from the
    // controls, and gives way when the window is made small.
    auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    spacer->size.height = CSSLayout::Dimension::Px(24);
    parts.page->AddChild(spacer);

    parts.notes = MakeNotesBlock(id + "-notes");
    parts.page->AddChild(parts.notes);
    return parts;
}

void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
    parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize, kNoteTextColor));
}

void AddBodyCaption(PageParts& parts, const std::string& id, const std::string& text) {
    auto l = MakeText(id, text);
    l->SetMargin(10, 0, 0, 0);
    parts.body->AddChild(l);
}

// ===== CONTROLS =====

std::string IconPath(const std::string& fileName) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + fileName);
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& label, int width,
                                              std::function<void()> onClick, const std::string& iconFile = "") {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
    b->SetFontSize(kTextFontSize);
    b->SetCornerRadius(4.0f);
    b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
    b->SetTextColors(kTextColor);
    b->SetBorder(1.0f, Color(0, 0, 0, 60));
    if (!iconFile.empty()) {
        b->SetIcon(IconPath(iconFile));
        b->SetIconSize(15, 15);
        b->SetIconPosition(ButtonIconPosition::Left);
        b->SetIconSpacing(label.empty() ? 0 : 5);
        b->SetUseIconAsMask(true);
        b->SetIconMaskColor(Color(55, 55, 60, 255));
    }
    if (onClick) b->SetOnClick(std::move(onClick));
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

std::shared_ptr<UltraCanvasContainer> MakeButtonRow(const std::string& id) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(34);
    return row;
}

std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id, const std::string& text, bool checked,
                                             int width = kTextWidth) {
    auto radio = UltraCanvasRadio::Create(id, -1, -1, text, checked);
    RadioVisualStyle style = radio->GetVisualStyle();
    style.base.fontSize = kTextFontSize;
    style.base.textColor = kTextColor;
    style.base.textHoverColor = kTextColor;
    radio->SetVisualStyle(style);
    radio->size.width = CSSLayout::Dimension::Px(width);
    radio->size.height = CSSLayout::Dimension::Px(kControlHeight);
    radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return radio;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id, const std::string& text, int width,
                                                  bool checked, std::function<void(bool)> onChange) {
    auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, static_cast<float>(width),
                                                     static_cast<float>(kControlHeight), text);
    box->SetFontSize(kTextFontSize);
    CheckboxVisualStyle style = box->GetVisualStyle();
    style.base.textColor = kTextColor;
    style.base.textHoverColor = kTextColor;
    box->SetVisualStyle(style);
    box->SetChecked(checked);
    box->size.width = CSSLayout::Dimension::Px(width);
    box->size.height = CSSLayout::Dimension::Px(kControlHeight);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->onStateChanged = [onChange](CheckedState, CheckedState now) {
        if (onChange) onChange(now == CheckedState::Checked);
    };
    return box;
}

std::shared_ptr<UltraCanvasContainer> MakeSliderRow(const std::string& id, const std::string& caption,
                                                    const std::shared_ptr<UltraCanvasSlider>& slider,
                                                    const std::shared_ptr<UltraCanvasLabel>& value,
                                                    int labelWidth = 130) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(32);
    auto label = MakeLabel(id + "-label", caption);
    label->size.width = CSSLayout::Dimension::Px(labelWidth);
    label->size.height = CSSLayout::Dimension::Px(20);
    row->AddChild(label);
    row->AddChild(slider);
    row->AddChild(value);
    return row;
}

std::shared_ptr<UltraCanvasSlider> MakeIntSlider(const std::string& id, int minValue, int maxValue, int value,
                                                 std::function<void(int)> onChange) {
    auto slider = CreateSlider(id, 0, 0, 200, 24);
    slider->SetRange(static_cast<float>(minValue), static_cast<float>(maxValue));
    slider->SetStep(1.0f);
    slider->SetValue(static_cast<float>(value));
    slider->size.width = CSSLayout::Dimension::Px(200);
    slider->size.height = CSSLayout::Dimension::Px(24);
    slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    slider->onValueChanging = [onChange](float v) { if (onChange) onChange(static_cast<int>(v + 0.5f)); };
    slider->onValueChanged = [onChange](float v) { if (onChange) onChange(static_cast<int>(v + 0.5f)); };
    return slider;
}

std::shared_ptr<UltraCanvasLabel> MakeSliderValueLabel(const std::string& id, int width = 60) {
    auto label = MakeLabel(id, "");
    label->size.width = CSSLayout::Dimension::Px(width);
    label->size.height = CSSLayout::Dimension::Px(20);
    return label;
}

// A "Folder: <path>   [Choose] [Default]" block: the line and the buttons
std::shared_ptr<UltraCanvasLabel> AddFolderChooser(PageParts& parts, const std::string& id,
                                                   std::function<void(const std::string&)> onChosen) {
    auto line = MakeText(id + "-line", "");
    parts.body->AddChild(line);
    auto row = MakeButtonRow(id + "-row");
    row->AddChild(MakeButton(id + "-choose", Tr("Choose folder"), 130, [onChosen]() {
        FileDialogOptions opts;
        opts.SetTitle(Tr("Save in"));
        if (g_dialog && g_dialog->window) opts.SetParentWindow(g_dialog->window.get());
        UltraCanvasFileLoader::SelectFolderDialog(opts, [onChosen](DialogResult result, const std::string& folder) {
            if (result == DialogResult::OK && !folder.empty() && onChosen) onChosen(folder);
        });
    }, "folder-open.svg"));
    row->AddChild(MakeButton(id + "-default", Tr("The default folder"), 140, [onChosen]() {
        if (onChosen) onChosen("");
    }));
    parts.body->AddChild(row);
    return line;
}

// ===== THE FORMATS =====

struct PictureType {
    std::string extension;
    std::string description;
    bool writable = false;
};

// Every bitmap type FileLoader knows, with whether this build writes it
std::vector<PictureType> PictureTypes() {
    std::vector<PictureType> out;
    for (const MediaFormatInfo& f : UltraCanvasFileLoader::GetSupportedFormats(MediaFormatCategory::Bitmap)) {
        PictureType t;
        t.extension = f.extension;
        t.description = f.description;
        t.writable = f.canSave;
        out.push_back(t);
    }
    return out;
}

const PictureType* FindType(const std::vector<PictureType>& types, const std::string& extension) {
    for (const PictureType& t : types)
        if (t.extension == extension) return &t;
    return nullptr;
}

std::string UpperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// "JPEG XL (.jxl)" - the type's name and its extension
std::string TypeLabel(const std::vector<PictureType>& types, const std::string& extension) {
    const PictureType* t = FindType(types, extension);
    const std::string name = t && !t->description.empty() ? t->description : UpperAscii(extension);
    return name + " (." + extension + ")";
}

void UpdateFormatLine(DialogState* d) {
    if (!d->settings || !d->formatLine) return;
    const std::string& ext = d->settings->pictureFormat;
    const bool writable = UltraCanvasFileLoader::CanSaveImage(ext);
    std::string text = TrF("Pictures are written as .{0}.", ext);
    if (!writable) text += "  " + Tr("This build cannot write that type: the snip is saved as PNG instead.");
    d->formatLine->SetText(text);
    d->formatLine->SetTextColor(writable ? kTextColor : Color(185, 28, 28, 255));
    d->formatLine->RequestRedraw();
    if (d->formatSettingsButton) {
        const bool hasSettings = writable && UltraCanvasFileLoader::HasSaveSettings(ext);
        d->formatSettingsButton->SetText(TrF("Settings for .{0}", ext));
        d->formatSettingsButton->SetDisabled(!hasSettings);
        d->formatSettingsButton->SetTooltip(hasSettings ? Tr("The type's quality, compression and the like, as the Save dialog's Settings")
                                                        : Tr("This type has no settings of its own"));
        d->formatSettingsButton->RequestRedraw();
    }
}

// ===== PICTURES > FILE TYPE =====

std::shared_ptr<UltraCanvasContainer> BuildPictureTypePage(DialogState* d) {
    PageParts parts = MakePage("snip-set-page-type", Tr("File type"),
                               Tr("The type a picture is written as - by the automatic save, and when it goes to UltraPaint:"));
    const std::vector<PictureType> types = PictureTypes();
    const std::vector<std::string>& recommended = UltraSnipSettings::RecommendedPictureFormats();
    const std::string& chosen = d->settings->pictureFormat;
    const bool chosenIsRecommended = std::find(recommended.begin(), recommended.end(), chosen) != recommended.end();

    d->formatRadios.clear();
    for (const std::string& ext : recommended) {
        const PictureType* t = FindType(types, ext);
        const bool writable = t && t->writable;
        std::string label = TypeLabel(types, ext);
        if (!writable) label += "  -  " + Tr("not in this build");
        auto radio = MakeChoice("snip-set-type-" + ext, label, chosenIsRecommended && chosen == ext);
        if (!writable) radio->SetDisabled(true);
        d->formatGroup.AddRadioButton(radio);
        d->formatRadios.emplace_back(ext, radio);
        parts.body->AddChild(radio);
    }

    // Every other type this build writes, in one list under its own radio
    auto otherRow = std::make_shared<UltraCanvasContainer>("snip-set-type-other-row");
    otherRow->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    otherRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    otherRow->size.width = CSSLayout::Dimension::Px(kTextWidth);
    otherRow->size.height = CSSLayout::Dimension::Px(30);
    d->otherFormatRadio = MakeChoice("snip-set-type-other", Tr("Another type:"), !chosenIsRecommended, 130);
    d->formatGroup.AddRadioButton(d->otherFormatRadio);
    otherRow->AddChild(d->otherFormatRadio);
    d->otherFormats = CreateDropdown("snip-set-type-list", 0, 0, 280, 26);
    d->otherFormats->size.width = CSSLayout::Dimension::Px(280);
    d->otherFormats->size.height = CSSLayout::Dimension::Px(26);
    d->otherFormats->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    d->otherFormatExtensions.clear();
    int otherIndex = -1;
    for (const PictureType& t : types) {
        if (!t.writable) continue;
        if (std::find(recommended.begin(), recommended.end(), t.extension) != recommended.end()) continue;
        if (t.extension == chosen) otherIndex = static_cast<int>(d->otherFormatExtensions.size());
        d->otherFormatExtensions.push_back(t.extension);
        d->otherFormats->AddItem(TypeLabel(types, t.extension), t.extension);
    }
    if (d->otherFormats->GetItemCount() == 0) {
        d->otherFormats->AddItem(Tr("No other type in this build"), "");
        d->otherFormats->SetDisabled(true);
        d->otherFormatRadio->SetDisabled(true);
    } else {
        d->otherFormats->SetSelectedIndex(std::max(0, otherIndex), false);
    }
    d->otherFormats->onSelectionChanged = [d](int index, const DropdownItem&) {
        if (!d->settings || index < 0 || index >= static_cast<int>(d->otherFormatExtensions.size())) return;
        if (!d->otherFormatRadio->IsChecked()) {
            // Choosing from the list is choosing "another type"
            d->otherFormatRadio->SetChecked(true);
        }
        d->settings->pictureFormat = d->otherFormatExtensions[static_cast<size_t>(index)];
        UpdateFormatLine(d);
        ApplyAndSave(d);
    };
    otherRow->AddChild(d->otherFormats);
    parts.body->AddChild(otherRow);

    d->formatGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected || !d->settings) return;
        if (selected == d->otherFormatRadio) {
            const int index = d->otherFormats ? d->otherFormats->GetSelectedIndex() : -1;
            if (index >= 0 && index < static_cast<int>(d->otherFormatExtensions.size()))
                d->settings->pictureFormat = d->otherFormatExtensions[static_cast<size_t>(index)];
        } else {
            for (const auto& [ext, radio] : d->formatRadios)
                if (radio == selected) d->settings->pictureFormat = ext;
        }
        UpdateFormatLine(d);
        ApplyAndSave(d);
    };

    d->formatLine = MakeText("snip-set-type-line", "");
    d->formatLine->SetMargin(8, 0, 0, 0);
    parts.body->AddChild(d->formatLine);

    auto row = MakeButtonRow("snip-set-type-buttons");
    d->formatSettingsButton = MakeButton("snip-set-type-settings", Tr("Settings"), 200, [d]() {
        if (!d->settings || !d->window) return;
        // In this window's type size, not a stand-alone dialog's larger one
        SettingsPageKit::ShowSaveSettingsDialog(d->settings->pictureFormat, d->window.get());
    }, "settings.svg");
    row->AddChild(d->formatSettingsButton);
    parts.body->AddChild(row);
    UpdateFormatLine(d);

    d->resets[kPagePictureType] = PageReset{Tr("Restore default type"), 170, [d]() {
        if (!d->settings) return;
        d->settings->pictureFormat = "png";
        d->syncing = true;
        for (const auto& [ext, radio] : d->formatRadios)
            if (ext == "png") radio->SetChecked(true);
        d->syncing = false;
        UpdateFormatLine(d);
        ApplyAndSave(d);
    }};

    AddNote(parts, "snip-set-type-note1",
            Tr("PNG keeps every pixel and is read everywhere. JPEG XL, AVIF and HEIC are far smaller at the same "
               "quality but need a program that reads them; WebP sits between. JPEG is the smallest for a photo "
               "and the worst for text and lines."));
    AddNote(parts, "snip-set-type-note2",
            Tr("The list under \"Another type\" holds every other picture type this build writes. The type's "
               "Settings - quality, compression - are the ones the framework's Save dialog uses, shared with the "
               "other applications. The clipboard always gets a PNG, whatever the type."));
    return parts.page;
}

// ===== PICTURES > SAVING =====

void UpdatePictureFolderLine(DialogState* d) {
    if (!d->settings || !d->pictureFolderLine) return;
    const std::string& folder = d->settings->pictureFolder;
    d->pictureFolderLine->SetText(folder.empty() ? Tr("Folder: Pictures/Screenshots (the default)") : TrF("Folder: {0}", folder));
    d->pictureFolderLine->RequestRedraw();
}

std::shared_ptr<UltraCanvasContainer> BuildPictureSavingPage(DialogState* d) {
    PageParts parts = MakePage("snip-set-page-saving", TrC("settings page", "Saving"),
                               Tr("What happens to a picture the moment it is taken:"));
    d->autoSaveBox = MakeCheckbox("snip-set-autosave", Tr("Write every picture to the folder below"), kTextWidth,
                                  d->settings->autoSave, [d](bool on) {
        if (!d->settings) return;
        d->settings->autoSave = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->autoSaveBox);
    d->autoCopyBox = MakeCheckbox("snip-set-autocopy", Tr("Put every picture on the clipboard"), kTextWidth,
                                  d->settings->autoCopy, [d](bool on) {
        if (!d->settings) return;
        d->settings->autoCopy = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->autoCopyBox);

    AddBodyCaption(parts, "snip-set-pic-folder-caption", Tr("Where the pictures go:"));
    d->pictureFolderLine = AddFolderChooser(parts, "snip-set-pic-folder", [d](const std::string& folder) {
        if (!d->settings) return;
        d->settings->pictureFolder = folder;
        UpdatePictureFolderLine(d);
        ApplyAndSave(d);
    });
    UpdatePictureFolderLine(d);

    d->resets[kPagePictureSaving] = PageReset{Tr("Restore defaults"), 170, [d]() {
        if (!d->settings) return;
        const std::string format = d->settings->pictureFormat;   // the File type page's own
        d->settings->ResetPictures();
        d->settings->pictureFormat = format;
        d->syncing = true;
        if (d->autoSaveBox) d->autoSaveBox->SetChecked(d->settings->autoSave);
        if (d->autoCopyBox) d->autoCopyBox->SetChecked(d->settings->autoCopy);
        d->syncing = false;
        UpdatePictureFolderLine(d);
        ApplyAndSave(d);
    }};

    AddNote(parts, "snip-set-saving-note1",
            Tr("With both off a snip only shows in the window, where Save as and Copy do the rest. Save as "
               "always offers every type and any folder, whatever is set here."));
    AddNote(parts, "snip-set-saving-note2",
            Tr("The file is named Screenshot <date> <time> in the type chosen on the File type page. The "
               "default folder is the Pictures folder's Screenshots, made when it is first needed."));
    return parts.page;
}

// ===== VIDEOS > RECORDING =====

void UpdateFrameRateLabel(DialogState* d) {
    if (!d->settings || !d->frameRateValue) return;
    d->frameRateValue->SetText(TrF("{0} fps", d->settings->frameRate));
    d->frameRateValue->RequestRedraw();
}

void UpdateVideoFolderLine(DialogState* d) {
    if (!d->settings || !d->videoFolderLine) return;
    const std::string& folder = d->settings->videoFolder;
    d->videoFolderLine->SetText(folder.empty() ? Tr("Folder: Videos/Screen Recordings (the default)") : TrF("Folder: {0}", folder));
    d->videoFolderLine->RequestRedraw();
}

std::shared_ptr<UltraCanvasContainer> BuildRecordingPage(DialogState* d) {
    PageParts parts = MakePage("snip-set-page-recording", Tr("Recording"), Tr("How a video of the screen is written:"));
    const std::string engine = UltraSnipRecorder::UnavailableReason();

    AddBodyCaption(parts, "snip-set-video-type-caption", Tr("File type:"));
    struct VideoChoice { const char* ext; std::string label; VideoFX::VideoFXVideoCodec codec; };
    const VideoChoice choices[] = {
        {"mp4", Tr("MP4 with H.264 (.mp4)  -  plays everywhere"), VideoFX::VideoFXVideoCodec::H264},
        {"mkv", Tr("Matroska with H.264 (.mkv)"), VideoFX::VideoFXVideoCodec::H264},
        {"webm", Tr("WebM with VP9 (.webm)  -  the web's own"), VideoFX::VideoFXVideoCodec::VP9},
    };
    d->videoRadios.clear();
    for (const VideoChoice& c : choices) {
        const bool writable = engine.empty() && VideoFX::VideoFX_IsVideoEncoderAvailable(c.codec);
        std::string label = c.label;
        if (!writable) label += "  -  " + Tr("not in this build");
        auto radio = MakeChoice(std::string("snip-set-video-") + c.ext, label, d->settings->videoFormat == c.ext);
        if (!writable) radio->SetDisabled(true);
        d->videoGroup.AddRadioButton(radio);
        d->videoRadios.emplace_back(c.ext, radio);
        parts.body->AddChild(radio);
    }
    d->videoGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected || !d->settings) return;
        for (const auto& [ext, radio] : d->videoRadios) {
            if (radio != selected) continue;
            d->settings->videoFormat = ext;
            ApplyAndSave(d);
            return;
        }
    };

    AddBodyCaption(parts, "snip-set-rate-caption", Tr("Frames a second:"));
    d->frameRateValue = MakeSliderValueLabel("snip-set-rate-value");
    d->frameRateSlider = MakeIntSlider("snip-set-rate", 5, 60, d->settings->frameRate, [d](int rate) {
        if (!d->settings) return;
        d->settings->frameRate = rate;
        UpdateFrameRateLabel(d);
        ApplyAndSave(d);
    });
    parts.body->AddChild(MakeSliderRow("snip-set-rate-row", Tr("Frame rate:"), d->frameRateSlider, d->frameRateValue));
    UpdateFrameRateLabel(d);

    AddBodyCaption(parts, "snip-set-video-folder-caption", Tr("Where the recordings go:"));
    d->videoFolderLine = AddFolderChooser(parts, "snip-set-video-folder", [d](const std::string& folder) {
        if (!d->settings) return;
        d->settings->videoFolder = folder;
        UpdateVideoFolderLine(d);
        ApplyAndSave(d);
    });
    UpdateVideoFolderLine(d);

    d->resets[kPageRecording] = PageReset{Tr("Restore defaults"), 170, [d]() {
        if (!d->settings) return;
        d->settings->ResetVideos();
        d->syncing = true;
        for (const auto& [ext, radio] : d->videoRadios)
            if (ext == d->settings->videoFormat) radio->SetChecked(true);
        if (d->frameRateSlider) d->frameRateSlider->SetValue(static_cast<float>(d->settings->frameRate));
        d->syncing = false;
        UpdateFrameRateLabel(d);
        UpdateVideoFolderLine(d);
        ApplyAndSave(d);
    }};

    if (!engine.empty()) AddNote(parts, "snip-set-video-note0", TrF("No recording on this build: {0}", engine));
    AddNote(parts, "snip-set-video-note1",
            Tr("More frames a second make a smoother recording and a larger file; 20 is enough for a "
               "walk-through, 30 for motion. The recording is written as it goes, so one cut short by a "
               "crash still plays up to its last seconds."));
    AddNote(parts, "snip-set-video-note2",
            Tr("The file is named Screen recording <date> <time>. No sound is recorded yet, and the pointer is "
               "not in the picture."));
    return parts.page;
}

// ===== BEHAVIOUR > AFTER A SNIP =====

std::shared_ptr<UltraCanvasContainer> BuildAfterSnipPage(DialogState* d) {
    PageParts parts = MakePage("snip-set-page-after", Tr("After a snip"),
                               Tr("When a snip was started from the desktop's button, the shortcut or the command line:"));
    d->showWindowRadio = MakeChoice("snip-set-after-show", Tr("Show the UltraSnip window with the result"),
                                    d->settings->showWindowAfterSnip);
    d->stayHiddenRadio = MakeChoice("snip-set-after-hide", Tr("Stay out of the way: save and copy, show nothing"),
                                    !d->settings->showWindowAfterSnip);
    d->afterGroup.AddRadioButton(d->showWindowRadio);
    d->afterGroup.AddRadioButton(d->stayHiddenRadio);
    parts.body->AddChild(d->showWindowRadio);
    parts.body->AddChild(d->stayHiddenRadio);
    d->afterGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected || !d->settings) return;
        d->settings->showWindowAfterSnip = selected == d->showWindowRadio;
        ApplyAndSave(d);
    };
    AddBodyCaption(parts, "snip-set-printkey-caption", Tr("The Print Screen key:"));
    d->printKeyBox = MakeCheckbox("snip-set-printkey",
                                  Tr("Starts a snip while UltraSnip runs, instead of the system's screenshot tool"),
                                  kTextWidth, d->settings->takePrintKey, [d](bool on) {
        if (!d->settings) return;
        d->settings->takePrintKey = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->printKeyBox);
    d->resets[kPageAfterSnip] = PageReset{Tr("Restore defaults"), 170, [d]() {
        if (!d->settings) return;
        d->settings->ResetBehaviour();
        d->syncing = true;
        if (d->showWindowRadio) d->showWindowRadio->SetChecked(true);
        if (d->printKeyBox) d->printKeyBox->SetChecked(d->settings->takePrintKey);
        d->syncing = false;
        ApplyAndSave(d);
    }};
    AddNote(parts, "snip-set-after-note1",
            Tr("A snip taken from the window itself always comes back to the window. Out of the way, a "
               "picture is saved and copied as the Saving page says and nothing else appears; a "
               "recording still shows its bar, and its window stays hidden after Stop."));
    AddNote(parts, "snip-set-printkey-note",
            Tr("The Print Screen key takes the shape, kind and delay chosen on the bar, as Start does. It is "
               "UltraSnip's only while UltraSnip runs; on Windows it no longer opens the Snipping Tool then, "
               "and Alt+Print Screen and Win+Print Screen stay the system's."));
    return parts.page;
}

// ===== START PAGE =====

std::shared_ptr<UltraCanvasContainer> BuildStartPage(DialogState* d) {
    PageParts parts = MakePage("snip-set-page-start", Tr("Settings"), Tr("Open a section on the left and choose the page to set:"));
    AddNote(parts, "snip-set-start-note1",
            Tr("Pictures - the type a picture is written as (PNG, JPEG XL, AVIF, HEIC, WebP, JPEG or any other "
               "this build writes), whether it is saved and copied by itself, and where it goes."));
    AddNote(parts, "snip-set-start-note2", Tr("Videos - the recording's container, its frame rate and its folder."));
    AddNote(parts, "snip-set-start-note3", Tr("Behaviour - whether the window appears after a snip taken from the desktop."));
    AddNote(parts, "snip-set-start-note4", Tr("Every change applies straight away and is saved; there is nothing to confirm."));

    // The localisation editor on UltraSnip's own set, in the language it
    // runs in: the "Translate this application..." entry of the
    // localisation proposal (5), here because UltraSnip has no menu bar.
    auto translateRow = MakeButtonRow("snip-set-start-translate-row");
    translateRow->AddChild(MakeButton("snip-set-start-translate", Tr("Translate UltraSnip..."), 200, [d]() {
        if (!d->window) return;
        ShowLocalisationEditorDialog(d->window.get(), UltraLocalisation::CurrentApplication(),
                                     UltraLocalisation::CurrentLanguage());
    }));
    parts.body->AddChild(translateRow);
    AddNote(parts, "snip-set-start-note-translate",
            Tr("Every text UltraSnip shows, in one grid with the sets it comes from: its own set, the Image "
               "and Video base sets and the standard set. A change is saved into the settings folder and "
               "shows the next time UltraSnip starts."));
    return parts.page;
}

// ===== THE WINDOW =====

void BuildDialog(DialogState* d, UltraCanvasWindowBase* parent) {
    if (!d->Create(parent, Tr("UltraSnip - Settings"))) return;
    d->AddSection(kPagePictures, Tr("Pictures"));
    d->AddTreeNode(kPagePictures, kPagePictureType, Tr("File type"));
    // "Saving" as the page's name, not the standard set's "Saving..." in progress
    d->AddTreeNode(kPagePictures, kPagePictureSaving, TrC("settings page", "Saving"));
    d->AddSection(kPageVideos, Tr("Videos"));
    d->AddTreeNode(kPageVideos, kPageRecording, Tr("Recording"));
    d->AddSection(kPageBehaviour, Tr("Behaviour"));
    d->AddTreeNode(kPageBehaviour, kPageAfterSnip, Tr("After a snip"));

    d->AddPage(kPagePictureType, BuildPictureTypePage(d));
    d->AddPage(kPagePictureSaving, BuildPictureSavingPage(d));
    d->AddPage(kPageRecording, BuildRecordingPage(d));
    d->AddPage(kPageAfterSnip, BuildAfterSnipPage(d));
    d->AddPage(kPageStart, BuildStartPage(d));
    d->Finish(kPageStart);
}

} // namespace

void UltraSnipSettingsDialog::Show(UltraCanvasWindowBase* parent, UltraSnipSettings* settings,
                                   std::function<void()> onChanged) {
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        g_dialog->settings = settings;
        g_dialog->onChanged = std::move(onChanged);
        g_dialog->window->Show();
        g_dialog->window->RaiseAndFocus();
        return;
    }
    auto d = std::make_shared<DialogState>();
    d->settings = settings;
    d->onChanged = std::move(onChanged);
    g_dialog = d;
    BuildDialog(d.get(), parent);
}

void UltraSnipSettingsDialog::Shutdown() {
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        g_dialog->window->onWindowClosed = nullptr;
        g_dialog->window->Close();
    }
    g_dialog.reset();
}

} // namespace UltraSnip

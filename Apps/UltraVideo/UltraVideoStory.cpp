// Apps/UltraVideo/UltraVideoStory.cpp
// The story wizard (UltraVideoStory.h).
// Version: 0.3.0 - the wizard follows the theme (its dialog, tabs, album, labels); no palette parameter
// Version: 0.2.0 - the photos, clips and songs offered are FileLoader's types the engine reads
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#include "UltraVideoStory.h"
#include "UltraVideoFormats.h"

#include "UltraCanvasAlbum.h"
#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTabbedContainer.h"

#include <algorithm>
#include <memory>
#include <sstream>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

namespace {

constexpr int kDialogWidth = 620;
constexpr int kContentWidth = 570;
constexpr int kControl = static_cast<int>(Tokens::ControlH);

struct TransitionPick { const char* label; VideoFXTransitionType type; };
const TransitionPick kTransitions[] = {
    {"Cross dissolve", VideoFXTransitionType::Crossfade}, {"Cut", VideoFXTransitionType::Cut},
    {"Dissolve", VideoFXTransitionType::Dissolve}, {"Fade through black", VideoFXTransitionType::FadeThroughBlack},
    {"Wipe", VideoFXTransitionType::WipeLeft}, {"Slide", VideoFXTransitionType::SlideLeft},
    {"Smooth", VideoFXTransitionType::SmoothLeft}, {"Circle open", VideoFXTransitionType::CircleOpen},
    {"Pixelize", VideoFXTransitionType::Pixelize}, {"Blur", VideoFXTransitionType::Blur},
};
const double kSeconds[] = {2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
const char* kSecondsLabels[] = {"2 seconds", "3 seconds", "4 seconds", "5 seconds", "6 seconds", "8 seconds"};

struct Wizard {
    StoryChoice choice;
    std::shared_ptr<UltraCanvasAlbum> album;
    std::shared_ptr<UltraCanvasLabel> count, songs;
    std::shared_ptr<UltraCanvasLabel> lengthValue;
    std::shared_ptr<UltraCanvasSwitch> fit, beat;
    UltraCanvasWindow* parent = nullptr;

    void RefreshFiles() {
        std::vector<AlbumItem> items;
        for (const std::string& f : choice.files) {
            AlbumItem item;
            item.mediaPath = f;
            item.title = PathToUtf8(PathFromUtf8(f).filename());
            item.mediaType = Document::KindOfPath(f) == MediaKind::Image ? AlbumMediaType::Photo : AlbumMediaType::Video;
            item.subtitle = item.mediaType == AlbumMediaType::Photo ? "photo" : "clip";
            items.push_back(item);
        }
        album->SetItems(items);
        size_t photos = 0;
        for (const std::string& f : choice.files) if (Document::KindOfPath(f) == MediaKind::Image) ++photos;
        const size_t clips = choice.files.size() - photos;
        std::ostringstream text;
        if (choice.files.empty()) text << "Nothing chosen yet.";
        else text << photos << (photos == 1 ? " photo" : " photos") << ", " << clips << (clips == 1 ? " clip" : " clips");
        count->SetText(text.str());
    }
    void RefreshSongs() {
        if (choice.songs.empty()) {
            songs->SetText("No music: the video keeps the clips' own sound.");
        } else {
            std::string list;
            for (const std::string& s : choice.songs) {
                if (!list.empty()) list += ", ";
                list += PathToUtf8(PathFromUtf8(s).filename());
            }
            songs->SetText(list);
        }
        fit->SetDisabled(choice.songs.empty());
        beat->SetDisabled(choice.songs.empty());
    }
};

std::shared_ptr<UltraCanvasContainer> Row(const std::shared_ptr<UltraCanvasContainer>& page, const std::string& id,
                                          const std::string& caption, std::shared_ptr<UltraCanvasUIElement> field) {
    auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, kContentWidth, kControl + 4);
    row->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0);
    auto label = CreateLabel(id + ".caption", 0, 0, 180, kControl, caption);
    label->layoutItem.SetFlexShrink(0);
    FollowTheme(label, LabelVariant::Muted);
    row->AddChild(label);
    field->layoutItem.SetFlexShrink(0);
    row->AddChild(field);
    page->AddChild(row);
    return row;
}

std::shared_ptr<UltraCanvasContainer> Page(const std::string& id, const std::string& intro) {
    auto page = std::make_shared<UltraCanvasContainer>(id, 0, 0, kContentWidth, 380);
    page->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(12);
    auto text = CreateLabel(id + ".intro", 0, 0, kContentWidth - 24, 40, intro);
    text->layoutItem.SetFlexShrink(0);
    FollowTheme(text, LabelVariant::Body);
    page->AddChild(text);
    return page;
}

} // namespace

void ShowStoryWizard(UltraCanvasWindow* parent, std::vector<std::string> files,
                     std::function<void(const StoryChoice&)> onMake) {
    auto w = std::make_shared<Wizard>();
    w->parent = parent;
    for (const std::string& f : files) {
        const MediaKind kind = Document::KindOfPath(f);
        if (kind == MediaKind::Audio) w->choice.songs.push_back(f);
        else w->choice.files.push_back(f);
    }

    DialogConfig config;
    config.title = "Make a story";
    config.message = "Photos and clips, a song, a style: the engine pans over the pictures, keeps faces in shot, "
                     "changes them on the beat and dips the music under speech. The result opens on the timeline.";
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::NoButtons;
    config.width = kDialogWidth;
    config.height = 560;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->SetIconVisible(false);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("uv.story.tabs", 0, 0, kContentWidth, 400);
    tabs->SetCloseMode(TabCloseMode::NoClose);
    tabs->SetTabStyle(TabStyle::Flat);
    tabs->SetFollowTheme(true);

    // ---- 1. Pictures and clips ----
    auto pictures = Page("uv.story.pictures", "Step 1 - the pictures and clips, in the order they play. Drag to reorder; hover one for Remove.");
    auto addRow = std::make_shared<UltraCanvasContainer>("uv.story.addRow", 0, 0, kContentWidth, kControl + 4);
    addRow->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    addRow->layoutItem.SetFlexShrink(0);
    auto add = MakeTextButton("uv.story.add", "Add photos and clips\xE2\x80\xA6", 190);
    Wizard* wiz = w.get();
    add->SetOnClick([wiz]() {
        FileDialogOptions opts;
        opts.SetTitle("Photos and clips for the story").SetParentWindow(wiz->parent);
        AddImportTypes(opts, "Photos and video", true, true, false);
        UltraCanvasFileLoader::OpenMultipleFilesDialog(opts, [wiz](DialogResult result, const std::vector<std::string>& chosen) {
            if (result != DialogResult::OK) return;
            for (const std::string& f : chosen) {
                if (Document::KindOfPath(f) == MediaKind::Audio) continue;
                if (std::find(wiz->choice.files.begin(), wiz->choice.files.end(), f) == wiz->choice.files.end())
                    wiz->choice.files.push_back(f);
            }
            wiz->RefreshFiles();
        });
    });
    addRow->AddChild(add);
    w->count = CreateLabel("uv.story.count", 0, 0, 340, kControl, "");
    w->count->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    FollowTheme(w->count, LabelVariant::Muted);
    addRow->AddChild(w->count);
    pictures->AddChild(addRow);
    w->album = std::make_shared<UltraCanvasAlbum>("uv.story.album", 0, 0, kContentWidth - 24, 190);
    w->album->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    w->album->SetMode(AlbumMode::UserEdit);
    AlbumConfig cfg;
    cfg.layout = AlbumLayout::UniformGrid;
    cfg.sizingMode = AlbumSizingMode::ItemsPerRow;
    cfg.itemsPerRow = 5;
    cfg.gap = 6;
    cfg.outerPadding = 0;
    cfg.actionDisplay = AlbumActionDisplay::OnHover;
    cfg.showBorder = false;
    w->album->SetConfig(cfg);
    w->album->SetFollowTheme(true);      // the panel, raised tiles, captions from the theme
    AlbumAction remove;
    remove.id = "remove";
    remove.label = "Remove";
    remove.iconPath = IconPath("trash");
    remove.onTrigger = [wiz](size_t index) {
        if (index < wiz->choice.files.size()) wiz->choice.files.erase(wiz->choice.files.begin() + static_cast<std::ptrdiff_t>(index));
        wiz->RefreshFiles();
    };
    w->album->AddAction(remove);
    w->album->onItemsReordered = [wiz](size_t from, size_t to) {
        if (from >= wiz->choice.files.size() || to >= wiz->choice.files.size() || from == to) return;
        const std::string moved = wiz->choice.files[from];
        wiz->choice.files.erase(wiz->choice.files.begin() + static_cast<std::ptrdiff_t>(from));
        wiz->choice.files.insert(wiz->choice.files.begin() + static_cast<std::ptrdiff_t>(to), moved);
        wiz->RefreshFiles();
    };
    pictures->AddChild(w->album);
    auto seconds = CreateDropdown("uv.story.seconds", 0, 0, 160, kControl);
    for (const char* l : kSecondsLabels) seconds->AddItem(l);
    seconds->SetSelectedIndex(2, false);
    seconds->onSelectionChanged = [wiz](int i, const DropdownItem&) { wiz->choice.secondsPerPhoto = kSeconds[std::clamp(i, 0, 5)]; };
    Row(pictures, "uv.story.row.seconds", "Each photo stays for", seconds);
    tabs->AddTab("1  Pictures", pictures);

    // ---- 2. Music ----
    auto music = Page("uv.story.music", "Step 2 - a song under the whole video, or none.");
    w->songs = CreateLabel("uv.story.songs", 0, 0, kContentWidth - 24, 36, "");
    w->songs->layoutItem.SetFlexShrink(0);
    FollowTheme(w->songs, LabelVariant::Body);
    music->AddChild(w->songs);
    auto songRow = std::make_shared<UltraCanvasContainer>("uv.story.songRow", 0, 0, kContentWidth, kControl + 4);
    songRow->layout.SetFlexRow().SetFlexGap(12);
    songRow->layoutItem.SetFlexShrink(0);
    auto choose = MakeTextButton("uv.story.choose", "Choose a song\xE2\x80\xA6", 150);
    choose->SetOnClick([wiz]() {
        FileDialogOptions opts;
        opts.SetTitle("A song for the story").SetParentWindow(wiz->parent);
        AddImportTypes(opts, "Music", false, false, true);
        UltraCanvasFileLoader::OpenMultipleFilesDialog(opts, [wiz](DialogResult result, const std::vector<std::string>& chosen) {
            if (result != DialogResult::OK || chosen.empty()) return;
            wiz->choice.songs = chosen;
            wiz->RefreshSongs();
        });
    });
    songRow->AddChild(choose);
    auto none = MakeTextButton("uv.story.none", "No music", 110);
    none->SetOnClick([wiz]() { wiz->choice.songs.clear(); wiz->RefreshSongs(); });
    songRow->AddChild(none);
    music->AddChild(songRow);
    w->fit = UltraCanvasSwitch::Create("uv.story.fit", 0, 0, "Fit the video to the song's length", false);
    w->fit->onStateChanged = [wiz](CheckedState, CheckedState now) { wiz->choice.fitToMusic = now == CheckedState::Checked; };
    w->fit->layoutItem.SetFlexShrink(0);
    music->AddChild(w->fit);
    w->beat = UltraCanvasSwitch::Create("uv.story.beat", 0, 0, "Change pictures on the beat", false);
    w->beat->onStateChanged = [wiz](CheckedState, CheckedState now) { wiz->choice.beatSync = now == CheckedState::Checked; };
    w->beat->layoutItem.SetFlexShrink(0);
    music->AddChild(w->beat);
    auto ducking = CreateDropdown("uv.story.ducking", 0, 0, 200, kControl);
    for (const char* l : {"Speech in a quiet room", "Talking outdoors", "A loud event"}) ducking->AddItem(l);
    ducking->SetSelectedIndex(0, false);
    ducking->onSelectionChanged = [wiz](int i, const DropdownItem&) {
        wiz->choice.ducking = i == 1 ? VideoFXDuckingPreset::Outdoor : i == 2 ? VideoFXDuckingPreset::LoudEvent : VideoFXDuckingPreset::Speech;
    };
    Row(music, "uv.story.row.ducking", "The clips' own sound is", ducking);
    tabs->AddTab("2  Music", music);

    // ---- 3. Style ----
    auto style = Page("uv.story.style", "Step 3 - how the pictures move and change.");
    auto transition = CreateDropdown("uv.story.transition", 0, 0, 200, kControl);
    for (const TransitionPick& t : kTransitions) transition->AddItem(t.label);
    transition->SetSelectedIndex(0, false);
    transition->onSelectionChanged = [wiz](int i, const DropdownItem&) { wiz->choice.transition = kTransitions[std::clamp(i, 0, 9)].type; };
    Row(style, "uv.story.row.transition", "Between pictures", transition);
    auto length = CreateSlider("uv.story.length", 0, 0, 160, kControl);
    length->SetRange(0.2f, 3.0f);
    length->SetStep(0.1f);
    length->SetValue(1.0f);
    length->SetValueDisplay(SliderValueDisplay::NoDisplay);
    auto lengthRow = Row(style, "uv.story.row.length", "Lasting", length);
    w->lengthValue = CreateLabel("uv.story.lengthValue", 0, 0, 60, kControl, "1.0 s");
    FollowTheme(w->lengthValue, LabelVariant::Muted);
    lengthRow->AddChild(w->lengthValue);
    length->onValueChanged = [wiz](float v) {
        wiz->choice.transitionSeconds = v;
        char text[32];
        std::snprintf(text, sizeof text, "%.1f s", v);
        wiz->lengthValue->SetText(text);
    };
    auto faces = UltraCanvasSwitch::Create("uv.story.faces", 0, 0, "Keep faces in shot while the camera moves over photos", true);
    faces->onStateChanged = [wiz](CheckedState, CheckedState now) { wiz->choice.keepFaces = now == CheckedState::Checked; };
    faces->layoutItem.SetFlexShrink(0);
    style->AddChild(faces);
    auto portrait = CreateDropdown("uv.story.portrait", 0, 0, 240, kControl);
    for (const char* l : {"Whole, on a blurred background", "Filling the frame", "Whole, with bars"}) portrait->AddItem(l);
    portrait->SetSelectedIndex(0, false);
    portrait->onSelectionChanged = [wiz](int i, const DropdownItem&) {
        wiz->choice.portraitFit = i == 1 ? VideoFXImageFit::Cover : i == 2 ? VideoFXImageFit::Contain : VideoFXImageFit::Auto;
    };
    Row(style, "uv.story.row.portrait", "Photos and clips of another shape", portrait);
    auto captions = UltraCanvasSwitch::Create("uv.story.captions", 0, 0, "Captions from the file names", false);
    captions->onStateChanged = [wiz](CheckedState, CheckedState now) { wiz->choice.captionsFromNames = now == CheckedState::Checked; };
    captions->layoutItem.SetFlexShrink(0);
    style->AddChild(captions);
    auto fades = UltraCanvasSwitch::Create("uv.story.fades", 0, 0, "Fade in at the start and out at the end", true);
    fades->onStateChanged = [wiz](CheckedState, CheckedState now) { wiz->choice.fadeInOut = now == CheckedState::Checked; };
    fades->layoutItem.SetFlexShrink(0);
    style->AddChild(fades);
    tabs->AddTab("3  Style", style);

    w->RefreshFiles();
    w->RefreshSongs();
    dialog->AddDialogElement(tabs);
    dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton("Make video", DialogResult::OK, DialogButtonRole::Default);
    dialog->SetFollowTheme(true);        // the dialog's surface, message and buttons with the window's look
    UltraCanvasDialogManager::ShowDialog(dialog, [w, onMake, parent](DialogResult result) {
        if (result != DialogResult::OK) return;
        if (w->choice.files.empty()) {
            UltraCanvasAlert::Error("Choose at least one photo or clip first.", "Make a story", nullptr, parent);
            return;
        }
        if (onMake) onMake(w->choice);
    });
}

} // namespace UltraVideo

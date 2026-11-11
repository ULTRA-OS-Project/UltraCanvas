// Apps/UltraSnip/ui/UltraSnipWindow.cpp
// See the header.
// Version: 0.5.1 - the Start button as wide as its measured icon and text
// Version: 0.5.0 - opens at the top centre of the screen over the screen already dimmed
//                  (Arm); the delay a segmented choice like the shape and the kind;
//                  each segment its own tooltip
// Version: 0.4.1 - the delay list measured in its font, not estimated (Thai ran wide)
// Version: 0.4.0 - the texts go through Tr (UltraLocalisation)
// Version: 0.3.0 - one bar at the top, icons for the choices and actions, the settings
//                  window's type size; the bar alone until there is a snip; New is Start
// Version: 0.2.0 - the settings window; the picture type, folders and after-snip behaviour
// Version: 0.1.1 - the result shown from its pixels (UCImage::FromRgba)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipWindow.h"

#include "UltraSnipSettingsDialog.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

#ifndef ULTRASNIP_VERSION
#error "ULTRASNIP_VERSION must come from Docs/UltraSnip/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace UltraSnip {

namespace {
const Color kChrome(246, 246, 248, 255);
const Color kLine(214, 214, 220, 255);
const Color kCanvas(232, 232, 236, 255);
const Color kStatusText(70, 70, 80, 255);
const Color kErrorText(185, 28, 28, 255);
constexpr int kBarHeight = 44;              // the bar: 28-high controls with 8 above and below
constexpr int kControlHeight = 28;
constexpr int kChoiceWidth = 34;            // one icon segment
constexpr int kIconButtonWidth = 30;
constexpr int kMinWidth = 700;              // every control of the bar, and room for the news
constexpr int kMinOpenHeight = 360;         // opened out: room for the snip under the bar
constexpr int kDelays[] = {0, 3, 5, 10};
constexpr unsigned kSettleMs = 350;         // the window has left the screen by then
constexpr unsigned kRecordingTickMs = 250;
constexpr int kBarTopMargin = 8;            // the armed bar's distance from the top of the screen

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string ParentFolder(const std::string& path) {
    const std::filesystem::path p = PathFromUtf8(path);
    return p.has_parent_path() ? PathToUtf8(p.parent_path()) : std::string();
}

std::string FileName(const std::string& path) { return PathToUtf8(PathFromUtf8(path).filename()); }

int IndexOf(const int* values, size_t count, int value) {
    for (size_t i = 0; i < count; ++i)
        if (values[i] == value) return static_cast<int>(i);
    return 0;
}
} // namespace

UltraSnipWindow::UltraSnipWindow(UltraCanvasApplication& app) : app_(app) {}

UltraSnipWindow::~UltraSnipWindow() {
    UltraSnipSettingsDialog::Shutdown();
    if (delayTimer_) app_.StopTimer(delayTimer_);
    if (recordingTimer_) app_.StopTimer(recordingTimer_);
    recordingBar_.reset();
    if (recorder_) recorder_->Stop();
    recorder_.reset();
    overlay_.reset();
    if (window_) {
        window_->onWindowClosing = nullptr;
        window_->Close();
    }
}

// ===== BUILDING =====

bool UltraSnipWindow::Create() {
    settings_.Load();

    // Until there is a snip the window is its bar alone; the picture or the
    // recording opens it out below the bar (SetCompact).
    WindowConfig config;
    config.title = std::string("UltraSnip ") + ULTRASNIP_VERSION;
    config.width = 820;
    config.height = kBarHeight;
    config.minWidth = kMinWidth;
    config.minHeight = kBarHeight;
    config.backgroundColor = kCanvas;
    window_ = CreateWindow(config);
    if (!window_) return false;

    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(kCanvas);
    window->AddChild(BuildToolbar());
    window->AddChild(BuildContent());
    ApplySettingsToControls();
    ShowNothing(Tr("Click Start, or press Ctrl+N, to take a snip."));

    window->InstallEventFilter("ultrasnip-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown});
    window->onWindowClosing = [this]() {
        Disarm();
        if (recorder_ && recorder_->IsRecording()) StopRecording();
        settings_.Save();
        app_.RequestExit();
        return true;
    };
    return true;
}

namespace {
// The bar's segmented choices: icons only, in the settings window's type
// size, each segment naming itself in its own tooltip.
std::shared_ptr<UltraCanvasSegmentedControl> MakeIconChoice(const std::string& id,
                                                            const std::vector<std::pair<std::string, std::string>>& segments) {
    auto control = CreateSegmentedControl(id, 0, 0, static_cast<long>(segments.size()) * kChoiceWidth, kControlHeight);
    SegmentedControlStyle look = control->GetStyle();
    look.fontSize = SettingsPageKit::kTextFontSize;
    look.iconSize = 16;
    look.iconAsMask = true;
    look.paddingHorizontal = 6;
    look.paddingVertical = 4;
    look.cornerRadius = 4.0f;
    control->SetStyle(look);
    for (const auto& segment : segments) control->AddSegment("", SettingsPageKit::IconPath(segment.first));
    for (size_t i = 0; i < segments.size(); ++i) control->SetSegmentTooltip(static_cast<int>(i), segments[i].second);
    control->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return control;
}

std::shared_ptr<UltraCanvasContainer> MakeBarRule(const std::string& id) {
    auto rule = std::make_shared<UltraCanvasContainer>(id, 0, 0, 1, 22);
    rule->SetBackgroundColor(kLine);
    rule->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return rule;
}
} // namespace

// One bar for everything: Start, the shape, the kind and the delay; then
// what can be done with the snip (shown once there is one), its line of
// news, and Settings at the far end.
std::shared_ptr<UltraCanvasContainer> UltraSnipWindow::BuildToolbar() {
    using SettingsPageKit::MakeButton;
    auto bar = std::make_shared<UltraCanvasContainer>("snip.bar");
    bar->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->size.height = CSSLayout::Dimension::Px(kBarHeight);
    bar->SetPadding(0, 10, 0, 10);
    bar->SetBackgroundColor(kChrome);

    // As wide as its icon and its word in the language shown, measured in its
    // own font when the bar is laid out (the button grows to its text, and a
    // width of 1 leaves it nothing else): "Start" and "開始" are no longer
    // padded out to the same 72 px, and "Démarrer" is not cut
    startButton_ = MakeButton("snip.start", Tr("Start"), 1, [this]() { StartCapture(RequestFromControls()); },
                              "scissors.svg");
    startButton_->SetGrowToFitText(true);
    startButton_->SetTooltip(Tr("Take a snip with the shape, kind and delay chosen here (Ctrl+N)"));
    bar->AddChild(startButton_);

    modes_ = MakeIconChoice("snip.mode", {{"snip-rectangle.svg", Tr("Rectangle: an area you drag out")},
                                          {"snip-window.svg", Tr("Window: the window you click")},
                                          {"snip-screen.svg", Tr("Full screen: the whole screen")}});
    modes_->onSegmentSelected = [this](int index) {
        settings_.mode = index == 1 ? SnipMode::Window : index == 2 ? SnipMode::FullScreen : SnipMode::Rectangle;
        if (armed_ && overlay_) overlay_->SetMode(settings_.mode);
    };
    bar->AddChild(modes_);

    kinds_ = MakeIconChoice("snip.kind", {{"image.svg", Tr("Picture: a screenshot")},
                                          {"snip-video.svg", Tr("Video: a recording of the area")}});
    kinds_->onSegmentSelected = [this](int index) { settings_.kind = index == 1 ? SnipKind::Video : SnipKind::Picture; };
    bar->AddChild(kinds_);

    // The delay as a choice like the shape and the kind: 0, 3, 5 or 10
    // seconds, every segment as wide as the widest caption in the language
    // shown - measured in the control's font by the window, which has its
    // render context before the bar is built (an estimate of ~0.62 em a
    // character without one).
    std::vector<std::string> delayItems;
    for (int seconds : kDelays) delayItems.push_back(TrF("{0} s", seconds));
    delays_ = CreateSegmentedControl("snip.delay", 0, 0, 0, kControlHeight);
    SegmentedControlStyle delayLook = delays_->GetStyle();
    delayLook.fontSize = SettingsPageKit::kTextFontSize;
    delayLook.paddingHorizontal = 6;
    delayLook.paddingVertical = 4;
    delayLook.cornerRadius = 4.0f;
    delays_->SetStyle(delayLook);
    IRenderContext* measure = window_ ? window_->GetRenderContext() : nullptr;
    if (measure) {
        measure->PushState();
        measure->SetFontFace(delayLook.fontFamily, FontWeight::Normal, FontSlant::Normal);
        measure->SetFontSize(delayLook.fontSize);
    }
    int widest = 0;
    for (const std::string& item : delayItems) {
        const int width = measure ? measure->GetTextLineWidth(item)
                                  : static_cast<int>(utf8_length(item) * delayLook.fontSize * 0.62f);
        widest = std::max(widest, width);
        delays_->AddSegment(item);
    }
    if (measure) measure->PopState();
    const int segmentWidth = std::clamp(widest + static_cast<int>(delayLook.paddingHorizontal) * 2 + 4, kChoiceWidth, 90);
    const int delayWidth = segmentWidth * static_cast<int>(delayItems.size());
    delays_->SetSize(delayWidth, kControlHeight);
    delays_->size.width = CSSLayout::Dimension::Px(static_cast<float>(delayWidth));
    delays_->SetSegmentTooltip(0, Tr("No delay: the screen is taken at once"));
    for (int i = 1; i < static_cast<int>(std::size(kDelays)); ++i)
        delays_->SetSegmentTooltip(i, TrF("Wait {0} seconds before the screen is taken, so a menu can be opened first",
                                          kDelays[i]));
    delays_->onSegmentSelected = [this](int index) {
        settings_.delaySeconds = kDelays[std::clamp(index, 0, 3)];
    };
    delays_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->AddChild(delays_);

    // What can be done with the snip: there only while there is one
    actions_ = std::make_shared<UltraCanvasContainer>("snip.actions");
    actions_->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    actions_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    actions_->AddChild(MakeBarRule("snip.actions-rule"));

    saveButton_ = MakeButton("snip.save", "", kIconButtonWidth, [this]() { SaveAs(); }, "save.svg");
    saveButton_->SetTooltip(Tr("Save as: the snip under another name or type (Ctrl+S)"));
    actions_->AddChild(saveButton_);

    copyButton_ = MakeButton("snip.copy", "", kIconButtonWidth, [this]() {
        if (CopyPictureToClipboard()) SetStatus(Tr("Copied to the clipboard."));
    }, "copy.svg");
    copyButton_->SetTooltip(Tr("Copy: the picture to the clipboard (Ctrl+C)"));
    actions_->AddChild(copyButton_);

    folderButton_ = MakeButton("snip.folder", "", kIconButtonWidth, [this]() { OpenFolder(); }, "folder-open.svg");
    folderButton_->SetTooltip(Tr("Open folder: the saved file's folder in the file manager"));
    actions_->AddChild(folderButton_);

    paintButton_ = MakeButton("snip.paint", "", kIconButtonWidth, [this]() { EditInPaint(); }, "edit.svg");
    paintButton_->SetTooltip(Tr("Edit in UltraPaint: open the saved picture there"));
    actions_->AddChild(paintButton_);
    bar->AddChild(actions_);

    // What just happened: the size, saved, copied - short enough for two
    // lines in any language; the saved file's whole path is the tooltip
    status_ = CreateLabel("snip.status", "");
    status_->SetTextColor(kStatusText);
    status_->SetFontSize(SettingsPageKit::kNoteFontSize);
    status_->SetWrap(TextWrap::WrapWord);
    status_->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    status_->size.height = CSSLayout::Dimension::Px(kBarHeight);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    bar->AddChild(status_);

    settingsButton_ = MakeButton("snip.settings", "", kIconButtonWidth, [this]() { OpenSettings(); }, "settings.svg");
    settingsButton_->SetTooltip(Tr("Settings: the picture type and folder, the recording's type and frame rate, "
                                   "what happens after a snip"));
    bar->AddChild(settingsButton_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraSnipWindow::BuildContent() {
    content_ = std::make_shared<UltraCanvasContainer>("snip.content");
    content_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    content_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    content_->SetBackgroundColor(kCanvas);
    content_->SetBorderTop(1, kLine);
    content_->SetPadding(12);

    picture_ = CreateImageElement("snip.picture", 0, 0, 0, 0);
    picture_->SetFitMode(ImageFitMode::ScaleDown);
    picture_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    picture_->SetVisible(false);
    content_->AddChild(picture_);

    viewer_ = CreateMediaViewer("snip.viewer", 0, 0, 0, 0);
    viewer_->SetTopBarsVisible(false);          // one recording, not a folder to browse
    viewer_->SetGrabFocusOnAttach(false);
    viewer_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    viewer_->SetVisible(false);
    content_->AddChild(viewer_);
    content_->SetVisible(false);
    return content_;
}

// The bar alone, or the bar with the snip under it. Opened out, the window
// takes the height it last had opened out.
void UltraSnipWindow::SetCompact(bool compact) {
    if (!window_ || compact == compact_) return;
    compact_ = compact;
    const int width = static_cast<int>(window_->GetWidth());
    if (compact) {
        const int height = static_cast<int>(window_->GetHeight());
        if (height > kBarHeight + 100) openHeight_ = height;
        if (content_) content_->SetVisible(false);
        window_->SetWindowSize(width, kBarHeight);
    } else {
        if (content_) content_->SetVisible(true);
        window_->SetWindowSize(width, std::max(openHeight_, kMinOpenHeight));
    }
}

void UltraSnipWindow::ApplySettingsToControls() {
    if (modes_) modes_->SetSelectedIndex(settings_.mode == SnipMode::Window ? 1 : settings_.mode == SnipMode::FullScreen ? 2 : 0);
    if (kinds_) kinds_->SetSelectedIndex(settings_.kind == SnipKind::Video ? 1 : 0);
    if (delays_) delays_->SetSelectedIndex(IndexOf(kDelays, 4, settings_.delaySeconds));
}

void UltraSnipWindow::OpenSettings() {
    if (!window_) return;
    Disarm();   // the settings window would open under the dimmed screen
    UltraSnipSettingsDialog::Show(window_.get(), &settings_, [this]() { ApplySettingsToControls(); });
}

SnipRequest UltraSnipWindow::RequestFromControls() const {
    SnipRequest request;
    request.mode = settings_.mode;
    request.kind = settings_.kind;
    request.delaySeconds = settings_.delaySeconds;
    request.startAtOnce = true;
    return request;
}

void UltraSnipWindow::Show() {
    if (!window_) return;
    window_->Show();
    window_->RaiseAndFocus();
}

void UltraSnipWindow::Present(const SnipRequest& request) {
    if (request.startAtOnce) {
        StartCapture(request);
        return;
    }
    Arm();
}

// ===== THE BAR OVER THE DIMMED SCREEN =====

void UltraSnipWindow::Arm() {
    if (!window_) return;
    // A snip on show, one in flight, or the screen already dimmed: the
    // window forward, as it is
    if (armed_ || capturing_ || (recorder_ && recorder_->IsRecording()) || result_ != ResultKind::Nothing ||
        !compact_) {
        Show();
        return;
    }
    // The screen is taken without the bar on it
    capturing_ = true;
    if (window_->IsWindowVisible()) {
        window_->Hide();
        if (delayTimer_) app_.StopTimer(delayTimer_);
        delayTimer_ = app_.StartTimer(kSettleMs, false, [this](TimerId) {
            delayTimer_ = 0;
            ArmNow();
        });
        return;
    }
    ArmNow();
}

void UltraSnipWindow::ArmNow() {
    capturing_ = false;
    std::string error;
    if (!UltraCanvasDesktopShell::CaptureScreenImage(shot_, &error)) {
        shot_ = DesktopScreenImage();
        PlaceBarTopCentre();
        Show();
        SetStatus(TrF("The screen could not be captured: {0}", error), true);
        return;
    }
    screenBounds_ = Rect2Di(shot_.x, shot_.y, shot_.width, shot_.height);
    overlay_ = std::make_unique<UltraSnipOverlay>();
    overlay_->onSelected = [this](const Rect2Di& area) {
        app_.PostToUIThread([this, area]() {
            overlay_.reset();
            armed_ = false;
            pending_ = RequestFromControls();
            if (pending_.kind == SnipKind::Video) {
                const std::string reason = UltraSnipRecorder::UnavailableReason();
                if (!reason.empty()) {
                    shot_ = DesktopScreenImage();
                    Show();
                    UltraCanvasAlert::Error(TrF("A video cannot be recorded: {0}", reason), "UltraSnip", nullptr,
                                            window_.get());
                    return;
                }
                window_->Hide();   // not in the recording
            }
            settings_.Save();
            capturing_ = true;
            windowWasShown_ = true;
            OnAreaChosen(area);
        });
    };
    overlay_->onCancelled = [this]() {
        app_.PostToUIThread([this]() {
            overlay_.reset();
            armed_ = false;
            shot_ = DesktopScreenImage();
            Show();
        });
    };
    // A click on the dimmed screen raises it over the bar: the bar comes back
    overlay_->onReleased = [this]() {
        if (window_ && armed_) window_->RaiseAndFocus();
    };
    if (!overlay_->Open(shot_, settings_.mode, ListPickableWindows(), &error)) {
        overlay_.reset();
        shot_ = DesktopScreenImage();
        PlaceBarTopCentre();
        Show();
        SetStatus(error, true);
        return;
    }
    armed_ = true;
    SetStatus(Tr("Drag an area on the screen now, or choose a delay and click Start."));
    overlay_->SetHintTop(static_cast<float>(kBarTopMargin + kBarHeight + 12));
    PlaceBarTopCentre();
    Show();                // after the overlay, so the bar is raised above it
    PlaceBarTopCentre();   // again once mapped: a window manager may place a new window itself
}

void UltraSnipWindow::Disarm() {
    if (!armed_) return;
    armed_ = false;
    SetStatus(Tr("Click Start, or press Ctrl+N, to take a snip."));
    if (overlay_) overlay_->Close();
    overlay_.reset();
    shot_ = DesktopScreenImage();
}

// Centred across the primary screen (the first one without a primary), at
// its top: in the pixels of ListScreens, which SetWindowPosition takes
void UltraSnipWindow::PlaceBarTopCentre() {
    if (!window_) return;
    const std::vector<DesktopScreenInfo> screens = UltraCanvasDesktopShell::ListScreens();
    if (screens.empty()) return;
    DesktopScreenInfo screen = screens.front();
    for (const DesktopScreenInfo& s : screens)
        if (s.primary) screen = s;
    int w = 0, h = 0;
    window_->GetNativeWindowSize(w, h);
    const float scale = window_->GetDeviceScale() > 0.0f ? window_->GetDeviceScale() : 1.0f;
    const int x = screen.x + std::max(0, (screen.width - w) / 2);
    const int y = screen.y + static_cast<int>(std::lround(kBarTopMargin * scale));
    window_->SetWindowPosition(x, y);
}

std::vector<SnipWindowBounds> UltraSnipWindow::ListPickableWindows() const {
    std::vector<SnipWindowBounds> windows;
    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
        if (w.minimized || w.skipTaskbar) continue;
        SnipWindowBounds b;
        if (!UltraCanvasDesktopShell::GetWindowBounds(w.id, b.bounds.x, b.bounds.y, b.bounds.width, b.bounds.height))
            continue;
        b.id = w.id;
        b.title = w.title;
        windows.push_back(b);
    }
    return windows;
}

// ===== THE CAPTURE =====

void UltraSnipWindow::StartCapture(const SnipRequest& request) {
    if (!window_) return;
    Disarm();   // taken afresh, after the delay
    if (capturing_ || (recorder_ && recorder_->IsRecording())) return;
    if (request.kind == SnipKind::Video) {
        const std::string reason = UltraSnipRecorder::UnavailableReason();
        if (!reason.empty()) {
            Show();
            UltraCanvasAlert::Error(TrF("A video cannot be recorded: {0}", reason), "UltraSnip", nullptr, window_.get());
            return;
        }
    }
    pending_ = request;
    settings_.mode = request.mode;
    settings_.kind = request.kind;
    settings_.delaySeconds = request.delaySeconds;
    ApplySettingsToControls();
    settings_.Save();
    capturing_ = true;
    windowWasShown_ = window_->IsWindowVisible();
    if (windowWasShown_) window_->Hide();
    const unsigned wait = kSettleMs + static_cast<unsigned>(std::max(0, request.delaySeconds)) * 1000u;
    if (delayTimer_) app_.StopTimer(delayTimer_);
    delayTimer_ = app_.StartTimer(wait, false, [this](TimerId) {
        delayTimer_ = 0;
        TakeScreen();
    });
}

void UltraSnipWindow::TakeScreen() {
    std::string error;
    if (!UltraCanvasDesktopShell::CaptureScreenImage(shot_, &error)) {
        Fail(TrF("The screen could not be captured: {0}", error));
        return;
    }
    screenBounds_ = Rect2Di(shot_.x, shot_.y, shot_.width, shot_.height);
    if (pending_.mode == SnipMode::FullScreen) {
        OnAreaChosen(screenBounds_);
        return;
    }
    std::vector<SnipWindowBounds> windows;
    if (pending_.mode == SnipMode::Window) windows = ListPickableWindows();
    overlay_ = std::make_unique<UltraSnipOverlay>();
    overlay_->onSelected = [this](const Rect2Di& area) {
        app_.PostToUIThread([this, area]() {
            overlay_.reset();
            OnAreaChosen(area);
        });
    };
    overlay_->onCancelled = [this]() {
        app_.PostToUIThread([this]() {
            overlay_.reset();
            OnCaptureCancelled();
        });
    };
    if (!overlay_->Open(shot_, pending_.mode, windows, &error)) {
        overlay_.reset();
        Fail(error);
    }
}

void UltraSnipWindow::OnCaptureCancelled() {
    shot_ = DesktopScreenImage();
    capturing_ = false;
    ShowWindowAgain();
    SetStatus(Tr("Cancelled."));
}

void UltraSnipWindow::OnAreaChosen(const Rect2Di& area) {
    if (pending_.kind == SnipKind::Video) {
        BeginRecording(area);
        return;
    }
    FinishPicture(area);
}

void UltraSnipWindow::FinishPicture(const Rect2Di& area) {
    ClipboardDib::RgbaImage image;
    Rect2Di taken;
    const bool cropped = CropToRgba(shot_, area, image, &taken);
    shot_ = DesktopScreenImage();
    capturing_ = false;
    if (!cropped) {
        Fail(Tr("The chosen area lies outside the screen picture."));
        return;
    }
    std::vector<uint8_t> png;
    if (!ClipboardDib::EncodePng(image, png)) {
        Fail(Tr("The picture could not be encoded as PNG."));
        return;
    }
    result_ = ResultKind::Picture;
    resultImage_ = std::move(image);
    resultPng_ = std::move(png);
    resultPath_.clear();
    ShowPicture();

    const std::string dot = "  \xC2\xB7  ";
    std::string status = TrF("{0} \xC3\x97 {1} px", resultImage_.width, resultImage_.height);
    bool error = false;
    if (settings_.autoSave) {
        const std::string path = settings_.NewPicturePath();
        std::string reason;
        if (SavePictureTo(path, reason)) {
            status += dot + Tr("saved");
            if (!reason.empty()) status += " (" + reason + ")";
        } else {
            status += dot + TrF("not saved: {0}", reason);
            error = true;
        }
    }
    if (settings_.autoCopy) {
        if (CopyPictureToClipboard()) status += dot + Tr("copied");
        else {
            status += dot + Tr("not copied");
            error = true;
        }
    }
    SetStatus(status, error);
    if (settings_.showWindowAfterSnip || windowWasShown_ || error) ShowWindowAgain();
}

void UltraSnipWindow::BeginRecording(const Rect2Di& area) {
    shot_ = DesktopScreenImage();
    // The overlay is still leaving the screen: give it a moment, or its
    // dimmed picture is the first frame
    if (delayTimer_) app_.StopTimer(delayTimer_);
    delayTimer_ = app_.StartTimer(kSettleMs, false, [this, area](TimerId) {
        delayTimer_ = 0;
        recorder_ = std::make_unique<UltraSnipRecorder>();
        recorder_->onFailed = [this](const std::string& reason) {
            app_.PostToUIThread([this, reason]() { OnRecordingFailed(reason); });
        };
        std::string error;
        const std::string path = settings_.NewRecordingPath();
        if (!recorder_->Start(area, path, settings_.frameRate, &error)) {
            recorder_.reset();
            capturing_ = false;
            Fail(error);
            return;
        }
        recordingBar_ = std::make_unique<UltraSnipRecordingBar>();
        recordingBar_->onPause = [this]() { if (recorder_) recorder_->Pause(); TickRecording(); };
        recordingBar_->onResume = [this]() { if (recorder_) recorder_->Resume(); TickRecording(); };
        recordingBar_->onStop = [this]() { app_.PostToUIThread([this]() { StopRecording(); }); };
        if (!recordingBar_->Open(area, screenBounds_)) {
            // Without the bar there is no way to stop: do not record blind
            recorder_->Stop();
            recorder_.reset();
            recordingBar_.reset();
            capturing_ = false;
            Fail(Tr("The recording bar could not be opened."));
            return;
        }
        recordingTimer_ = app_.StartTimer(kRecordingTickMs, true, [this](TimerId) { TickRecording(); });
        TickRecording();
    });
}

void UltraSnipWindow::TickRecording() {
    if (!recorder_ || !recordingBar_) return;
    recordingBar_->SetElapsed(recorder_->ElapsedSeconds(), recorder_->IsPaused());
}

void UltraSnipWindow::OnRecordingFailed(const std::string& reason) {
    if (!recorder_) return;
    StopRecording();
    SetStatus(reason, true);
    UltraCanvasAlert::Error(reason, "UltraSnip", nullptr, window_.get());
}

void UltraSnipWindow::StopRecording() {
    if (recordingTimer_) {
        app_.StopTimer(recordingTimer_);
        recordingTimer_ = 0;
    }
    recordingBar_.reset();
    if (!recorder_) {
        capturing_ = false;
        return;
    }
    std::string error;
    const bool complete = recorder_->Stop(&error);
    const std::string path = recorder_->Path();
    const double seconds = recorder_->ElapsedSeconds();
    const int64_t frames = recorder_->FrameCount();
    recorder_.reset();
    capturing_ = false;
    if (frames == 0) {
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(path), ec);
        ShowNothing(Tr("Nothing was recorded."));
        SetStatus(complete ? Tr("The recording was empty.") : error, !complete);
        ShowWindowAgain();
        return;
    }
    result_ = ResultKind::Video;
    resultImage_ = ClipboardDib::RgbaImage();
    resultPng_.clear();
    resultPath_ = path;
    ShowVideo(path);
    SetStatus(TrF("{0} recorded", FormatElapsed(seconds)) + "  \xC2\xB7  " + Tr("saved") +
                  (complete ? "" : "  \xC2\xB7  " + error),
              !complete);
    if (settings_.showWindowAfterSnip || windowWasShown_ || !complete) ShowWindowAgain();
}

void UltraSnipWindow::ShowWindowAgain() {
    if (!window_) return;
    window_->Show();
    window_->RaiseAndFocus();
}

void UltraSnipWindow::Fail(const std::string& message) {
    shot_ = DesktopScreenImage();
    capturing_ = false;
    ShowWindowAgain();
    SetStatus(message, true);
    UltraCanvasAlert::Error(message, "UltraSnip", nullptr, window_.get());
}

// ===== THE RESULT =====

void UltraSnipWindow::ShowNothing(const std::string& message) {
    if (viewer_) {
        viewer_->CloseFile();
        viewer_->SetVisible(false);
    }
    if (picture_) picture_->SetVisible(false);
    result_ = ResultKind::Nothing;
    resultImage_ = ClipboardDib::RgbaImage();
    resultPng_.clear();
    resultPath_.clear();
    SetCompact(true);
    SetStatus(message);
    UpdateActions();
}

void UltraSnipWindow::ShowPicture() {
    if (viewer_) {
        viewer_->CloseFile();
        viewer_->SetVisible(false);
    }
    if (picture_) {
        // Straight from the pixels; the PNG is for the clipboard and the file
        picture_->LoadFromImage(UCImage::FromRgba(resultImage_.pixels.data(), resultImage_.width, resultImage_.height));
        picture_->SetVisible(true);
    }
    SetCompact(false);
    UpdateActions();
}

void UltraSnipWindow::ShowVideo(const std::string& path) {
    if (picture_) picture_->SetVisible(false);
    if (viewer_) {
        viewer_->SetVisible(true);
        viewer_->OpenFile(path);
    }
    SetCompact(false);
    UpdateActions();
}

void UltraSnipWindow::UpdateActions() {
    const bool any = result_ != ResultKind::Nothing;
    const bool picture = result_ == ResultKind::Picture;
    if (actions_) actions_->SetVisible(any);
    if (saveButton_) saveButton_->SetDisabled(!any);
    if (copyButton_) copyButton_->SetDisabled(!picture);
    if (folderButton_) folderButton_->SetDisabled(resultPath_.empty());
    if (paintButton_) paintButton_->SetDisabled(!picture || resultPath_.empty());
}

void UltraSnipWindow::SetStatus(const std::string& text, bool error) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(error ? kErrorText : kStatusText);
    // The whole path in the tooltip; the line names the file only
    status_->SetTooltip(resultPath_.empty() ? std::string() : resultPath_);
}

bool UltraSnipWindow::CopyPictureToClipboard() {
    if (result_ != ResultKind::Picture || resultPng_.empty()) return false;
    UltraCanvasClipboard* clipboard = GetClipboard();
    if (!clipboard) {
        SetStatus(Tr("The clipboard is not available."), true);
        return false;
    }
    if (!clipboard->SetImage(resultPng_, "image/png")) {
        SetStatus(Tr("The picture could not be put on the clipboard."), true);
        return false;
    }
    return true;
}

// A PNG is written as the bytes already encoded; any other type goes through
// the framework's image writer with the type's own settings (the Save
// dialog's Settings, shared with the other applications). A type this build
// cannot write falls back to PNG beside it, and says so.
bool UltraSnipWindow::SavePictureTo(const std::string& path, std::string& error) {
    if (result_ != ResultKind::Picture || resultPng_.empty()) {
        error = Tr("There is no picture to save.");
        return false;
    }
    std::string target = path;
    const std::string type = LowerExtension(path);
    if (type == "png") {
        if (!WriteFileBytes(target, resultPng_, error)) return false;
    } else {
        UCImageSave::ImageExportOptions options;
        if (!UltraCanvasFileLoader::CanSaveImage(type) || !UltraCanvasFileLoader::GetImageExportOptions(target, options)) {
            target = SnipFilePath(path, "", "png");
            if (!WriteFileBytes(target, resultPng_, error)) return false;
            error = TrF("this build cannot write .{0}, saved as PNG", type);
        } else {
            const std::shared_ptr<UCImage> picture =
                UCImage::FromRgba(resultImage_.pixels.data(), resultImage_.width, resultImage_.height);
            if (!picture) {
                error = Tr("The picture could not be prepared.");
                return false;
            }
            std::error_code ec;
            const std::filesystem::path folder = PathFromUtf8(target).parent_path();
            if (!folder.empty()) std::filesystem::create_directories(folder, ec);
            error = picture->Save(target, options);
            if (!error.empty()) return false;
        }
    }
    resultPath_ = target;
    UpdateActions();
    return true;
}

void UltraSnipWindow::SaveAs() {
    if (result_ == ResultKind::Nothing || !window_) return;
    FileDialogOptions opts;
    opts.SetParentWindow(window_.get()).SetInitialDirectory(settings_.saveFolder);
    if (result_ == ResultKind::Video) {
        const std::string type = LowerExtension(resultPath_);
        opts.SetTitle(Tr("Save the recording as"))
            .SetDefaultFileName(FileName(resultPath_))
            .SetDefaultExtension(type)
            .AddFilter(type == "webm" ? Tr("WebM video") : type == "mkv" ? Tr("Matroska video") : Tr("MP4 video"), type);
        const std::string source = resultPath_;
        UltraCanvasFileLoader::SaveFileDialog(opts, [this, source](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            std::string error;
            if (!CopyFileTo(source, path, error)) {
                UltraCanvasAlert::Error(TrF("Could not save {0}", path) + "\n" + error, Tr("Save as"), nullptr, window_.get());
                return;
            }
            settings_.saveFolder = ParentFolder(path);
            settings_.Save();
            SetStatus(TrF("Saved as {0}", FileName(path)));
        });
        return;
    }
    const std::string defaultType = UltraCanvasFileLoader::CanSaveImage(settings_.pictureFormat) ? settings_.pictureFormat : "png";
    opts.SetTitle(Tr("Save the picture as"))
        .SetDefaultFileName(resultPath_.empty() ? FileName(settings_.NewPicturePath()) : FileName(resultPath_))
        .SetDefaultExtension(defaultType)
        .SetImageSize(resultImage_.width, resultImage_.height);
    UltraCanvasFileLoader::AddImageSaveTypes(opts);
    UltraCanvasFileLoader::SaveFileDialog(opts, [this](DialogResult result, const std::string& path,
                                                      const FileSaveImageSize& size) {
        if (result != DialogResult::OK || path.empty()) return;
        std::string error;
        const std::string type = LowerExtension(path);
        if (size.IsOriginal() && type == "png") {
            WriteFileBytes(path, resultPng_, error);
        } else {
            // Another type or size: the framework's image writer, with the
            // type's own settings (the Save dialog's Settings edits them)
            UCImageSave::ImageExportOptions options;
            const std::shared_ptr<UCImage> picture = UCImage::LoadFromMemory(resultPng_);
            if (!picture) error = Tr("The picture could not be read.");
            else if (!UltraCanvasFileLoader::GetImageExportOptions(path, size, options))
                error = TrF("This build cannot write .{0} pictures.", type);
            else error = picture->Save(path, options);
        }
        if (!error.empty()) {
            UltraCanvasAlert::Error(TrF("Could not save {0}", path) + "\n" + error, Tr("Save as"), nullptr, window_.get());
            return;
        }
        resultPath_ = path;
        settings_.saveFolder = ParentFolder(path);
        settings_.Save();
        UpdateActions();
        SetStatus(TrF("Saved as {0}", FileName(path)));
    });
}

void UltraSnipWindow::OpenFolder() {
    if (resultPath_.empty()) return;
    const std::string folder = ParentFolder(resultPath_);
    if (folder.empty()) return;
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({folder}, error))
        UltraCanvasAlert::Error(TrF("The folder could not be opened: {0}", error), "UltraSnip", nullptr, window_.get());
}

void UltraSnipWindow::EditInPaint() {
    if (result_ != ResultKind::Picture) return;
    if (resultPath_.empty()) {
        std::string error;
        if (!SavePictureTo(settings_.NewPicturePath(), error)) {
            UltraCanvasAlert::Error(TrF("The picture could not be saved for UltraPaint: {0}", error), "UltraSnip", nullptr,
                                    window_.get());
            return;
        }
    }
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchProgram("UltraPaint", {resultPath_}, &error))
        UltraCanvasAlert::Error(TrF("UltraPaint could not be started: {0}", error), "UltraSnip", nullptr, window_.get());
}

bool UltraSnipWindow::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;
    if (armed_ && event.virtualKey == UCKeys::Escape) {
        Disarm();
        return true;
    }
    if (event.ctrl && event.virtualKey == UCKeys::N) {
        StartCapture(RequestFromControls());
        return true;
    }
    if (event.ctrl && event.virtualKey == UCKeys::S) {
        SaveAs();
        return true;
    }
    if (event.ctrl && event.virtualKey == UCKeys::C && result_ == ResultKind::Picture) {
        if (CopyPictureToClipboard()) SetStatus(Tr("Copied to the clipboard."));
        return true;
    }
    return false;
}

} // namespace UltraSnip

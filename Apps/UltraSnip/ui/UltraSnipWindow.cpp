// Apps/UltraSnip/ui/UltraSnipWindow.cpp
// See the header.
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
#include "UltraCanvasDropdown.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <filesystem>

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
const Color kWelcomeText(110, 110, 120, 255);
constexpr int kDelays[] = {0, 3, 5, 10};
constexpr unsigned kSettleMs = 350;         // the window has left the screen by then
constexpr unsigned kRecordingTickMs = 250;

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

    WindowConfig config;
    config.title = std::string("UltraSnip ") + ULTRASNIP_VERSION;
    config.width = 820;
    config.height = 600;
    config.minWidth = 640;
    config.minHeight = 420;
    config.backgroundColor = kCanvas;
    window_ = CreateWindow(config);
    if (!window_) return false;

    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(kCanvas);
    window->AddChild(BuildToolbar());
    window->AddChild(BuildContent());
    window->AddChild(BuildActions());
    ApplySettingsToControls();
    ShowNothing("Click New, or press Ctrl+N, to take a snip.");

    window->InstallEventFilter("ultrasnip-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown});
    window->onWindowClosing = [this]() {
        if (recorder_ && recorder_->IsRecording()) StopRecording();
        settings_.Save();
        app_.RequestExit();
        return true;
    };
    return true;
}

std::shared_ptr<UltraCanvasContainer> UltraSnipWindow::BuildToolbar() {
    auto bar = std::make_shared<UltraCanvasContainer>("snip.bar");
    bar->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bar->SetPadding(10, 16, 10, 16);
    bar->SetBackgroundColor(kChrome);
    bar->SetBorderBottom(1, kLine);

    newButton_ = CreateButton("snip.new", 0, 0, 96, 34, "New");
    newButton_->SetTooltip("Take a snip with the shape, kind and delay chosen here (Ctrl+N)");
    newButton_->onClick = [this]() { StartCapture(RequestFromControls()); };
    bar->AddChild(newButton_);

    modes_ = CreateSegmentedControl("snip.mode", 0, 0, 300, 34);
    modes_->AddSegment("Rectangle");
    modes_->AddSegment("Window");
    modes_->AddSegment("Full screen");
    modes_->SetTooltip("The shape: an area you drag, a window you click, or the whole screen");
    modes_->onSegmentSelected = [this](int index) {
        settings_.mode = index == 1 ? SnipMode::Window : index == 2 ? SnipMode::FullScreen : SnipMode::Rectangle;
    };
    bar->AddChild(modes_);

    kinds_ = CreateSegmentedControl("snip.kind", 0, 0, 180, 34);
    kinds_->AddSegment("Picture");
    kinds_->AddSegment("Video");
    kinds_->SetTooltip("A picture, or a video recording of the area");
    kinds_->onSegmentSelected = [this](int index) { settings_.kind = index == 1 ? SnipKind::Video : SnipKind::Picture; };
    bar->AddChild(kinds_);

    delays_ = CreateDropdown("snip.delay", 0, 0, 120, 34);
    delays_->AddItem("No delay");
    delays_->AddItem("3 s delay");
    delays_->AddItem("5 s delay");
    delays_->AddItem("10 s delay");
    delays_->SetTooltip("Wait before the screen is taken, so a menu can be opened first");
    delays_->onSelectionChanged = [this](int index, const DropdownItem&) {
        settings_.delaySeconds = kDelays[std::clamp(index, 0, 3)];
    };
    bar->AddChild(delays_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraSnipWindow::BuildContent() {
    content_ = std::make_shared<UltraCanvasContainer>("snip.content");
    content_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch)
        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    content_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    content_->SetBackgroundColor(kCanvas);
    content_->SetPadding(12);

    welcome_ = CreateLabel("snip.welcome", "");
    welcome_->SetTextColor(kWelcomeText);
    welcome_->SetFontSize(15);
    welcome_->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    welcome_->SetWrap(TextWrap::WrapWord);
    welcome_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    content_->AddChild(welcome_);

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
    return content_;
}

std::shared_ptr<UltraCanvasContainer> UltraSnipWindow::BuildActions() {
    auto foot = std::make_shared<UltraCanvasContainer>("snip.foot");
    foot->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    foot->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    foot->SetPadding(10, 16, 10, 16);
    foot->SetBackgroundColor(kChrome);
    foot->SetBorderTop(1, kLine);

    auto actions = std::make_shared<UltraCanvasContainer>("snip.actions");
    actions->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    actions->layoutItem.SetFlexShrink(0);

    saveButton_ = CreateButton("snip.save", 0, 0, 96, 32, "Save as");
    saveButton_->SetTooltip("Save the snip under another name or type (Ctrl+S)");
    saveButton_->onClick = [this]() { SaveAs(); };
    actions->AddChild(saveButton_);

    copyButton_ = CreateButton("snip.copy", 0, 0, 80, 32, "Copy");
    copyButton_->SetTooltip("Put the picture on the clipboard (Ctrl+C)");
    copyButton_->onClick = [this]() {
        if (CopyPictureToClipboard()) SetStatus("Copied to the clipboard.");
    };
    actions->AddChild(copyButton_);

    folderButton_ = CreateButton("snip.folder", 0, 0, 110, 32, "Open folder");
    folderButton_->SetTooltip("Show the saved file's folder in the file manager");
    folderButton_->onClick = [this]() { OpenFolder(); };
    actions->AddChild(folderButton_);

    paintButton_ = CreateButton("snip.paint", 0, 0, 140, 32, "Edit in UltraPaint");
    paintButton_->SetTooltip("Open the saved picture in UltraPaint");
    paintButton_->onClick = [this]() { EditInPaint(); };
    actions->AddChild(paintButton_);

    settingsButton_ = CreateButton("snip.settings", 0, 0, 100, 32, "Settings");
    settingsButton_->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.svg"));
    settingsButton_->SetIconSize(16, 16);
    settingsButton_->SetIconPosition(ButtonIconPosition::Left);
    settingsButton_->SetUseIconAsMask(true);
    settingsButton_->SetIconMaskColor(kStatusText);
    settingsButton_->SetTooltip("The picture type and folder, the recording's type and frame rate, what happens after a snip");
    settingsButton_->onClick = [this]() { OpenSettings(); };
    actions->AddChild(settingsButton_);

    status_ = CreateLabel("snip.status", "");
    status_->SetTextColor(kStatusText);
    status_->SetFontSize(12);
    status_->SetWrap(TextWrap::WrapWord);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    actions->AddChild(status_);
    foot->AddChild(actions);
    return foot;
}

void UltraSnipWindow::ApplySettingsToControls() {
    if (modes_) modes_->SetSelectedIndex(settings_.mode == SnipMode::Window ? 1 : settings_.mode == SnipMode::FullScreen ? 2 : 0);
    if (kinds_) kinds_->SetSelectedIndex(settings_.kind == SnipKind::Video ? 1 : 0);
    if (delays_) delays_->SetSelectedIndex(IndexOf(kDelays, 4, settings_.delaySeconds), false);
}

void UltraSnipWindow::OpenSettings() {
    if (!window_) return;
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
    Show();
}

// ===== THE CAPTURE =====

void UltraSnipWindow::StartCapture(const SnipRequest& request) {
    if (!window_) return;
    if (capturing_ || (recorder_ && recorder_->IsRecording())) return;
    if (request.kind == SnipKind::Video) {
        const std::string reason = UltraSnipRecorder::UnavailableReason();
        if (!reason.empty()) {
            Show();
            UltraCanvasAlert::Error("A video cannot be recorded: " + reason, "UltraSnip", nullptr, window_.get());
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
        Fail("The screen could not be captured: " + error);
        return;
    }
    screenBounds_ = Rect2Di(shot_.x, shot_.y, shot_.width, shot_.height);
    if (pending_.mode == SnipMode::FullScreen) {
        OnAreaChosen(screenBounds_);
        return;
    }
    std::vector<SnipWindowBounds> windows;
    if (pending_.mode == SnipMode::Window) {
        for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
            if (w.minimized || w.skipTaskbar) continue;
            SnipWindowBounds b;
            if (!UltraCanvasDesktopShell::GetWindowBounds(w.id, b.bounds.x, b.bounds.y, b.bounds.width, b.bounds.height))
                continue;
            b.id = w.id;
            b.title = w.title;
            windows.push_back(b);
        }
    }
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
    SetStatus("Cancelled.");
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
        Fail("The chosen area lies outside the screen picture.");
        return;
    }
    std::vector<uint8_t> png;
    if (!ClipboardDib::EncodePng(image, png)) {
        Fail("The picture could not be encoded as PNG.");
        return;
    }
    result_ = ResultKind::Picture;
    resultImage_ = std::move(image);
    resultPng_ = std::move(png);
    resultPath_.clear();
    ShowPicture();

    std::string status = std::to_string(resultImage_.width) + " \xC3\x97 " + std::to_string(resultImage_.height) + " px";
    bool error = false;
    if (settings_.autoSave) {
        const std::string path = settings_.NewPicturePath();
        std::string reason;
        if (SavePictureTo(path, reason)) {
            status += "  \xC2\xB7  saved as " + FileName(resultPath_);
            if (!reason.empty()) status += " (" + reason + ")";
        } else {
            status += "  \xC2\xB7  not saved: " + reason;
            error = true;
        }
    }
    if (settings_.autoCopy) {
        if (CopyPictureToClipboard()) status += "  \xC2\xB7  copied";
        else {
            status += "  \xC2\xB7  not copied";
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
            Fail("The recording bar could not be opened.");
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
        ShowNothing("Nothing was recorded.");
        SetStatus(complete ? "The recording was empty." : error, !complete);
        ShowWindowAgain();
        return;
    }
    result_ = ResultKind::Video;
    resultImage_ = ClipboardDib::RgbaImage();
    resultPng_.clear();
    resultPath_ = path;
    ShowVideo(path);
    SetStatus(FormatElapsed(seconds) + " recorded  \xC2\xB7  saved as " + FileName(path) +
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
    result_ = result_ == ResultKind::Nothing ? ResultKind::Nothing : result_;
    if (viewer_) {
        viewer_->CloseFile();
        viewer_->SetVisible(false);
    }
    if (picture_) picture_->SetVisible(false);
    if (welcome_) {
        welcome_->SetText(message);
        welcome_->SetVisible(true);
    }
    result_ = ResultKind::Nothing;
    resultImage_ = ClipboardDib::RgbaImage();
    resultPng_.clear();
    resultPath_.clear();
    UpdateActions();
}

void UltraSnipWindow::ShowPicture() {
    if (viewer_) {
        viewer_->CloseFile();
        viewer_->SetVisible(false);
    }
    if (welcome_) welcome_->SetVisible(false);
    if (picture_) {
        // Straight from the pixels; the PNG is for the clipboard and the file
        picture_->LoadFromImage(UCImage::FromRgba(resultImage_.pixels.data(), resultImage_.width, resultImage_.height));
        picture_->SetVisible(true);
    }
    UpdateActions();
}

void UltraSnipWindow::ShowVideo(const std::string& path) {
    if (welcome_) welcome_->SetVisible(false);
    if (picture_) picture_->SetVisible(false);
    if (viewer_) {
        viewer_->SetVisible(true);
        viewer_->OpenFile(path);
    }
    UpdateActions();
}

void UltraSnipWindow::UpdateActions() {
    const bool any = result_ != ResultKind::Nothing;
    const bool picture = result_ == ResultKind::Picture;
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
        SetStatus("The clipboard is not available.", true);
        return false;
    }
    if (!clipboard->SetImage(resultPng_, "image/png")) {
        SetStatus("The picture could not be put on the clipboard.", true);
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
        error = "There is no picture to save.";
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
            error = "this build cannot write ." + type + ", saved as PNG";
        } else {
            const std::shared_ptr<UCImage> picture =
                UCImage::FromRgba(resultImage_.pixels.data(), resultImage_.width, resultImage_.height);
            if (!picture) {
                error = "The picture could not be prepared.";
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
        opts.SetTitle("Save the recording as")
            .SetDefaultFileName(FileName(resultPath_))
            .SetDefaultExtension(type)
            .AddFilter(type == "webm" ? "WebM video" : type == "mkv" ? "Matroska video" : "MP4 video", type);
        const std::string source = resultPath_;
        UltraCanvasFileLoader::SaveFileDialog(opts, [this, source](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            std::string error;
            if (!CopyFileTo(source, path, error)) {
                UltraCanvasAlert::Error("Could not save " + path + "\n" + error, "Save as", nullptr, window_.get());
                return;
            }
            settings_.saveFolder = ParentFolder(path);
            settings_.Save();
            SetStatus("Saved as " + FileName(path));
        });
        return;
    }
    const std::string defaultType = UltraCanvasFileLoader::CanSaveImage(settings_.pictureFormat) ? settings_.pictureFormat : "png";
    opts.SetTitle("Save the picture as")
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
            if (!picture) error = "The picture could not be read.";
            else if (!UltraCanvasFileLoader::GetImageExportOptions(path, size, options))
                error = "This build cannot write ." + type + " pictures.";
            else error = picture->Save(path, options);
        }
        if (!error.empty()) {
            UltraCanvasAlert::Error("Could not save " + path + "\n" + error, "Save as", nullptr, window_.get());
            return;
        }
        resultPath_ = path;
        settings_.saveFolder = ParentFolder(path);
        settings_.Save();
        UpdateActions();
        SetStatus("Saved as " + FileName(path));
    });
}

void UltraSnipWindow::OpenFolder() {
    if (resultPath_.empty()) return;
    const std::string folder = ParentFolder(resultPath_);
    if (folder.empty()) return;
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({folder}, error))
        UltraCanvasAlert::Error("The folder could not be opened: " + error, "UltraSnip", nullptr, window_.get());
}

void UltraSnipWindow::EditInPaint() {
    if (result_ != ResultKind::Picture) return;
    if (resultPath_.empty()) {
        std::string error;
        if (!SavePictureTo(settings_.NewPicturePath(), error)) {
            UltraCanvasAlert::Error("The picture could not be saved for UltraPaint: " + error, "UltraSnip", nullptr,
                                    window_.get());
            return;
        }
    }
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchProgram("UltraPaint", {resultPath_}, &error))
        UltraCanvasAlert::Error("UltraPaint could not be started: " + error, "UltraSnip", nullptr, window_.get());
}

bool UltraSnipWindow::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;
    if (event.ctrl && event.virtualKey == UCKeys::N) {
        StartCapture(RequestFromControls());
        return true;
    }
    if (event.ctrl && event.virtualKey == UCKeys::S) {
        SaveAs();
        return true;
    }
    if (event.ctrl && event.virtualKey == UCKeys::C && result_ == ResultKind::Picture) {
        if (CopyPictureToClipboard()) SetStatus("Copied to the clipboard.");
        return true;
    }
    return false;
}

} // namespace UltraSnip

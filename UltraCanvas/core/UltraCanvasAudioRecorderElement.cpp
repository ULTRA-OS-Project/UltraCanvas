// core/UltraCanvasAudioRecorderElement.cpp
// Composite UI control wrapping UltraCanvasAudioRecorder, built from child widgets
// (icon buttons, level meter, label, device dropdown, save/discard buttons)
// arranged by a flex row layout.
// Version: 0.3.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework

#include "UltraCanvasAudioRecorderElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasAudioDevices.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {
    constexpr int kPadding = 8;
    constexpr int kGap = 8;
    constexpr int kTimeWidth = 56;
    constexpr int kSaveBtnWidth = 70;
    constexpr int kDeviceWidth = 120;
    constexpr int kGainWidth = 80;
    constexpr int kCtrlHeight = 20;
    constexpr int kIconSize = 18;      // transport icon size inside the buttons
}

UltraCanvasAudioRecorderElement::UltraCanvasAudioRecorderElement(
        const std::string& identifier, float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    recorder = std::make_shared<UltraCanvasAudioRecorder>();

    // Fixed-height bar: never show scrollbars.
    ContainerStyle cs;
    cs.autoShowScrollbars = false;
    cs.forceShowVerticalScrollbar = false;
    cs.forceShowHorizontalScrollbar = false;
    SetContainerStyle(cs);

    // Horizontal flex row with all children vertically centred.
    SetPadding(static_cast<float>(kPadding));
    layout.SetFlexRow().SetFlexGap(static_cast<float>(kGap))
          .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    BuildChildren();
    HookRecorderCallbacks();
    ApplyStyleToChildren();
    UpdateRecordIcon();
    UpdatePauseButton();
}

UltraCanvasAudioRecorderElement::~UltraCanvasAudioRecorderElement() {
    StopElapsedTimer();
}

void UltraCanvasAudioRecorderElement::BuildChildren() {
    const int btn = style.buttonSize;
    auto fixed = [](const std::shared_ptr<UltraCanvasUIElement>& e) {
        e->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                     .SetAlignSelf(CSSLayout::AlignSelf::Center);
    };

    // ----- Record / Stop -----
    recordButton = MakeAudioIconButton(GetIdentifier() + ".Rec", btn, kIconSize,
                                       "record.svg", style.recordButtonColor);
    recordButton->onClick = [this]() { ToggleRecording(); };
    fixed(recordButton);
    AddChild(recordButton);

    // ----- Pause / Resume -----
    pauseButton = MakeAudioIconButton(GetIdentifier() + ".Pause", btn, kIconSize,
                                      "pause.svg", style.disabledColor);
    pauseButton->onClick = [this]() {
        auto st = recorder->GetState();
        if (st == AudioRecordingState::Recording) PauseRecording();
        else if (st == AudioRecordingState::Paused) ResumeRecording();
    };
    fixed(pauseButton);
    AddChild(pauseButton);

    // ----- Elapsed time label -----
    timeLabel = std::make_shared<UltraCanvasLabel>(GetIdentifier() + ".Time",
                                                   0, 0, kTimeWidth, kCtrlHeight, "0:00");
    timeLabel->SetFontSize(12);
    timeLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    fixed(timeLabel);
    AddChild(timeLabel);

    // ----- Level meter / waveform (absorbs the slack) -----
    levelMeter = std::make_shared<UltraCanvasLevelMeter>(GetIdentifier() + ".Meter",
                                                         0, static_cast<float>(style.meterHeight));
    levelMeter->SetColors(style.levelMeterBgColor, style.levelMeterLowColor,
                          style.levelMeterMidColor, style.levelMeterHighColor);
    levelMeter->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetFlexBasis(CSSLayout::Dimension::Px(0))
                          .SetAlignSelf(CSSLayout::AlignSelf::Center);
    AddChild(levelMeter);

    // ----- Gain slider -----
    gainSlider = std::make_shared<UltraCanvasSlider>(GetIdentifier() + ".Gain",
                                                     0, 0, kGainWidth, kCtrlHeight);
    gainSlider->SetSliderStyle(SliderStyle::Horizontal);
    gainSlider->SetRange(0.0f, 2.0f);
    gainSlider->SetStep(0.0f);
    gainSlider->SetValue(recorder->GetInputGain());
    gainSlider->SetTrackHeight(6.0f);
    {
        auto cb = [this](float v) { recorder->SetInputGain(v); };
        gainSlider->onValueChanging = cb;
        gainSlider->onValueChanged  = cb;
    }
    fixed(gainSlider);
    AddChild(gainSlider);

    // ----- Input device dropdown -----
    deviceDropdown = std::make_shared<UltraCanvasDropdown>(GetIdentifier() + ".Device",
                                                           0, 0, kDeviceWidth, kCtrlHeight + 4);
    deviceDropdown->onSelectionChanged = [this](int, const DropdownItem& it) {
        recorder->SetInputDevice(it.value);
    };
    fixed(deviceDropdown);
    AddChild(deviceDropdown);
    PopulateDevices();

    // ----- Save / Discard -----
    saveButton = std::make_shared<UltraCanvasButton>(GetIdentifier() + ".Save",
                                                     0.0f, 0.0f, static_cast<float>(kSaveBtnWidth),
                                                     static_cast<float>(btn - 4), "Save");
    saveButton->SetTextColors(Color(255, 255, 255), Color(255, 255, 255));
    saveButton->SetColors(style.levelMeterLowColor, style.levelMeterLowColor);
    saveButton->SetFontSize(11);
    saveButton->SetAcceptsFocus(false);
    saveButton->onClick = [this]() {
        if (recorder->GetState() == AudioRecordingState::Stopped) ShowSaveDialog();
    };
    fixed(saveButton);
    AddChild(saveButton);

    discardButton = std::make_shared<UltraCanvasButton>(GetIdentifier() + ".Discard",
                                                        0.0f, 0.0f, static_cast<float>(kSaveBtnWidth),
                                                        static_cast<float>(btn - 4), "Discard");
    discardButton->SetTextColors(Color(255, 255, 255), Color(255, 255, 255));
    discardButton->SetColors(style.levelMeterHighColor, style.levelMeterHighColor);
    discardButton->SetFontSize(11);
    discardButton->SetAcceptsFocus(false);
    discardButton->onClick = [this]() {
        if (recorder->GetState() == AudioRecordingState::Stopped) DiscardRecording();
    };
    fixed(discardButton);
    AddChild(discardButton);
}

void UltraCanvasAudioRecorderElement::PopulateDevices() {
    if (!deviceDropdown) return;
    deviceDropdown->ClearItems();
    deviceDropdown->AddItem("Default mic", "");   // value "" -> system default
    auto devices = UltraCanvasAudioDevices::ListInputDevices();
    for (const auto& dev : devices) {
        std::string label = dev.name + (dev.isDefault ? "  (default)" : "");
        deviceDropdown->AddItem(label, dev.id);
    }
    deviceDropdown->SetSelectedIndex(0, false);   // don't fire SetInputDevice yet
}

void UltraCanvasAudioRecorderElement::ApplyStyleToChildren() {
    // Visibility from style toggles (hidden children drop out of flex flow).
    if (pauseButton)    pauseButton->SetVisible(!style.compact);
    if (timeLabel)      timeLabel->SetVisible(style.showElapsedTime);
    if (gainSlider)     gainSlider->SetVisible(style.showGainSlider && !style.compact);
    if (deviceDropdown) deviceDropdown->SetVisible(style.showDeviceSelect && !style.compact);
    const bool showSD = style.showSaveDiscard && !style.compact;
    if (saveButton)     saveButton->SetVisible(showSD);
    if (discardButton)  discardButton->SetVisible(showSD);

    // Colors from style. The icon buttons tint via their (masked) text colour.
    if (recordButton)   recordButton->SetTextColors(style.recordButtonColor, style.recordButtonColor);
    if (timeLabel)      timeLabel->SetTextColor(style.textColor);
    if (saveButton)     saveButton->SetColors(style.levelMeterLowColor, style.levelMeterLowColor);
    if (discardButton)  discardButton->SetColors(style.levelMeterHighColor, style.levelMeterHighColor);
    if (levelMeter) {
        levelMeter->SetColors(style.levelMeterBgColor, style.levelMeterLowColor,
                              style.levelMeterMidColor, style.levelMeterHighColor);
        levelMeter->SetWaveformMode(style.showWaveform);
        // A waveform strip wants the full row height; the thin VU meter stays centred.
        if (style.showWaveform) {
            levelMeter->size.height = CSSLayout::Dimension::Auto();
            levelMeter->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        } else {
            levelMeter->size.height = CSSLayout::Dimension::Px(static_cast<float>(style.meterHeight));
            levelMeter->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        }
    }

    SyncElapsedAndButtons();
    InvalidateLayout();
}

void UltraCanvasAudioRecorderElement::UpdateRecordIcon() {
    if (!recordButton) return;
    bool recording = recorder->GetState() == AudioRecordingState::Recording;
    // record.svg when idle/armed; circle-stop.svg while recording (click to stop).
    recordButton->SetIcon(AudioIconPath(recording ? "circle-stop.svg" : "record.svg"));
    Color c = (recording && pulsePhase) ? style.recordingPulseColor : style.recordButtonColor;
    recordButton->SetTextColors(c, c);   // tint follows text colour (icon-as-mask)
}

void UltraCanvasAudioRecorderElement::UpdatePauseButton() {
    if (!pauseButton) return;
    auto st = recorder->GetState();
    if (st == AudioRecordingState::Paused) {
        pauseButton->SetIcon(AudioIconPath("play.svg"));   // shows "resume"
        pauseButton->SetTextColors(style.iconColor, style.iconColor);
    } else {
        pauseButton->SetIcon(AudioIconPath("pause.svg"));
        Color c = (st == AudioRecordingState::Recording) ? style.iconColor : style.disabledColor;
        pauseButton->SetTextColors(c, c);
    }
}

void UltraCanvasAudioRecorderElement::SyncElapsedAndButtons() {
    if (timeLabel) timeLabel->SetText(FormatTime(recorder->GetElapsed()));
    bool canSave = recorder->GetState() == AudioRecordingState::Stopped &&
                   recorder->GetSampleCount() > 0;
    if (saveButton)    saveButton->SetDisabled(!canSave);
    if (discardButton) discardButton->SetDisabled(!canSave);
}

void UltraCanvasAudioRecorderElement::StartElapsedTimer() {
    if (elapsedTimerId != InvalidTimerId) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    // GetElapsed() advances continuously; the level callback cadence is backend
    // dependent, so drive the clock + record-pulse from a timer while recording.
    elapsedTimerId = app->StartTimer(200, true, [this](TimerId) {
        if (recorder->GetState() == AudioRecordingState::Recording) {
            pulsePhase = !pulsePhase;
            UpdateRecordIcon();
        }
        SyncElapsedAndButtons();
        RequestRedraw();
    });
}

void UltraCanvasAudioRecorderElement::StopElapsedTimer() {
    if (elapsedTimerId == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(elapsedTimerId);
    elapsedTimerId = InvalidTimerId;
}

void UltraCanvasAudioRecorderElement::HookRecorderCallbacks() {
    recorder->onRecordingStateChanged = [this](AudioRecordingState st) {
        UpdateRecordIcon();
        UpdatePauseButton();
        SyncElapsedAndButtons();
        if (st == AudioRecordingState::Recording) StartElapsedTimer();
        else StopElapsedTimer();
        RequestRedraw();
    };
    recorder->onLevelChanged = [this](float peak, float rms) {
        if (levelMeter) {
            levelMeter->SetLevel(peak, rms);
            levelMeter->PushWaveformSample(peak);
        }
    };
    recorder->onClipping = [this]() { RequestRedraw(); };
    recorder->onMaxDurationReached = [this]() { StopRecording(); };
    recorder->onPermissionChanged = [this](MicrophonePermission p) {
        if (onPermissionChanged) onPermissionChanged(p);
        RequestRedraw();
    };
}

void UltraCanvasAudioRecorderElement::StartRecording() {
    if (recorder->Start()) {
        if (levelMeter) levelMeter->Reset();
        if (onRecordStarted) onRecordStarted();
        RequestRedraw();
    }
}

void UltraCanvasAudioRecorderElement::PauseRecording()  { recorder->Pause();  RequestRedraw(); }
void UltraCanvasAudioRecorderElement::ResumeRecording() { recorder->Resume(); RequestRedraw(); }

void UltraCanvasAudioRecorderElement::StopRecording() {
    recorder->Stop();
    if (onRecordStopped) onRecordStopped();
    RefreshEmbeddedPlayer();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::ToggleRecording() {
    auto state = recorder->GetState();
    if (state == AudioRecordingState::Recording) StopRecording();
    else StartRecording();
}

std::shared_ptr<UCAudio> UltraCanvasAudioRecorderElement::TakeBuffer() {
    auto a = recorder->TakeBuffer();
    if (onBufferReady) onBufferReady(a);
    return a;
}

bool UltraCanvasAudioRecorderElement::SaveToFile(const std::string& filePath, AudioFormat format) {
    bool ok = recorder->SaveToFile(filePath, format);
    if (ok && onSaved) onSaved(filePath);
    return ok;
}

void UltraCanvasAudioRecorderElement::DiscardRecording() {
    recorder->Discard();
    if (levelMeter) levelMeter->Reset();
    SyncElapsedAndButtons();
    if (onDiscarded) onDiscarded();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::ShowSaveDialog() {
    if (recorder->GetState() != AudioRecordingState::Stopped ||
        recorder->GetSampleCount() == 0) {
        if (onSaveCancelled) onSaveCancelled();
        return;
    }

    FileDialogOptions opts;
    opts.SetTitle("Save Recording")
        .SetDefaultFileName("recording.wav")
        .AddFilter("Wave audio (*.wav)", "wav")
        .AddFilter("All files (*.*)", "*")
        .SetParentWindow(GetWindow());

    auto self = this;
    UltraCanvasFileLoader::SaveFileDialog(opts,
        [self](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) {
                if (self->SaveToFile(path, AudioFormat::WAV)) {
                    // onSaved is already fired by SaveToFile; nothing more needed.
                }
            } else {
                if (self->onSaveCancelled) self->onSaveCancelled();
            }
        });
}

void UltraCanvasAudioRecorderElement::SetCaptureConfig(const AudioCaptureConfig& cfg) {
    recorder->Open(cfg);
}

const AudioCaptureConfig& UltraCanvasAudioRecorderElement::GetCaptureConfig() const {
    return recorder->GetConfig();
}

void UltraCanvasAudioRecorderElement::SetInputDevice(const std::string& deviceId) {
    recorder->SetInputDevice(deviceId);
}

void UltraCanvasAudioRecorderElement::SetStyle(const AudioRecorderStyle& s) {
    style = s;
    ApplyStyleToChildren();
    RefreshEmbeddedPlayer();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::RefreshEmbeddedPlayer() {
    if (!style.showEmbeddedPlayer) { embeddedPlayer.reset(); return; }
    // Higher-level glue: when integrating with a container, app code should
    // mount embeddedPlayer below this element and feed it `recorder->TakeBuffer()`.
}

std::string UltraCanvasAudioRecorderElement::FormatTime(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return std::string(buf);
}

void UltraCanvasAudioRecorderElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx) return;
    Rect2Df b = GetLocalBounds();

    // Background panel (rounded, with border) drawn before the children.
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height),
                             style.backgroundColor, 1.0f,
                             style.borderColor, style.cornerRadius);

    UltraCanvasContainer::Render(ctx, dirtyRect);
}

} // namespace UltraCanvas

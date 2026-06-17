// core/UltraCanvasAudioRecorderElement.cpp
// Composite UI control: record button, VU meter, elapsed timer, optional
// device select / gain slider / waveform strip / save+discard / embedded player.
// Version: 0.2.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioRecorderElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasMenu.h"
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
    constexpr int kWaveformHistory = 200;

    inline bool Hit(const Rect2Di& r, const Point2Di& p) {
        return r.width > 0 && r.height > 0 &&
               p.x >= r.x && p.x < r.x + r.width &&
               p.y >= r.y && p.y < r.y + r.height;
    }
}

UltraCanvasAudioRecorderElement::UltraCanvasAudioRecorderElement(
        const std::string& identifier, float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    recorder = std::make_shared<UltraCanvasAudioRecorder>();
    waveformHistory.assign(kWaveformHistory, 0.0f);
    HookRecorderCallbacks();
    Relayout();
}

UltraCanvasAudioRecorderElement::~UltraCanvasAudioRecorderElement() = default;

void UltraCanvasAudioRecorderElement::StartRecording() {
    if (recorder->Start()) {
        clipping = false;
        std::fill(waveformHistory.begin(), waveformHistory.end(), 0.0f);
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
    Relayout();
    RefreshEmbeddedPlayer();
    RequestRedraw();
}

void UltraCanvasAudioRecorderElement::HookRecorderCallbacks() {
    auto self = this;
    recorder->onRecordingStateChanged = [self](AudioRecordingState) { self->RequestRedraw(); };
    recorder->onLevelChanged = [self](float peak, float rms) {
        self->lastPeakLevel = peak;
        self->lastRmsLevel = rms;
        // Push into ring buffer for waveform strip
        if (!self->waveformHistory.empty()) {
            std::rotate(self->waveformHistory.begin(),
                        self->waveformHistory.begin() + 1,
                        self->waveformHistory.end());
            self->waveformHistory.back() = peak;
        }
        // Toggle blink ~ each level update
        self->pulsePhase = !self->pulsePhase;
        self->RequestRedraw();
    };
    recorder->onClipping = [self]() { self->clipping = true; self->RequestRedraw(); };
    recorder->onMaxDurationReached = [self]() { self->StopRecording(); };
    recorder->onPermissionChanged = [self](MicrophonePermission p) {
        if (self->onPermissionChanged) self->onPermissionChanged(p);
        self->RequestRedraw();
    };
}

void UltraCanvasAudioRecorderElement::Relayout() {
    Rect2Di b = GetLocalBounds();
    int btn = style.buttonSize;
    int cy = b.y + b.height / 2;
    int x = b.x + kPadding;

    // Record button
    recordButtonRect = Rect2Di(x, cy - btn / 2, btn, btn);
    x += btn + kGap;

    // Pause button (not compact)
    if (!style.compact) {
        pauseButtonRect = Rect2Di(x, cy - btn / 2, btn, btn);
        x += btn + kGap;
    } else {
        pauseButtonRect = Rect2Di();
    }

    // Time label (next to buttons)
    if (style.showElapsedTime) {
        timeLabelRect = Rect2Di(x, cy - 8, kTimeWidth, 16);
        x += kTimeWidth + kGap;
    } else {
        timeLabelRect = Rect2Di();
    }

    int rightX = b.x + b.width - kPadding;

    // Save / Discard (right edge)
    if (style.showSaveDiscard && !style.compact) {
        discardButtonRect = Rect2Di(rightX - kSaveBtnWidth, cy - btn / 2,
                                    kSaveBtnWidth, btn);
        rightX = discardButtonRect.x - kGap;
        saveButtonRect = Rect2Di(rightX - kSaveBtnWidth, cy - btn / 2,
                                 kSaveBtnWidth, btn);
        rightX = saveButtonRect.x - kGap;
    } else {
        saveButtonRect = Rect2Di();
        discardButtonRect = Rect2Di();
    }

    // Device select
    if (style.showDeviceSelect && !style.compact) {
        int devW = 120;
        deviceSelectRect = Rect2Di(rightX - devW, cy - 12, devW, 24);
        rightX = deviceSelectRect.x - kGap;
    } else {
        deviceSelectRect = Rect2Di();
    }

    // Gain slider
    if (style.showGainSlider && !style.compact) {
        int gW = 80;
        gainSliderRect = Rect2Di(rightX - gW, cy - 4, gW, 8);
        rightX = gainSliderRect.x - kGap;
    } else {
        gainSliderRect = Rect2Di();
    }

    // Waveform vs. level meter occupies the middle
    int midW = rightX - x;
    if (midW < 20) midW = 20;
    if (style.showWaveform) {
        waveformRect = Rect2Di(x, b.y + kPadding, midW,
                               b.height - 2 * kPadding);
        levelMeterRect = Rect2Di();
    } else {
        waveformRect = Rect2Di();
        levelMeterRect = Rect2Di(x, cy - style.meterHeight / 2,
                                 midW, style.meterHeight);
    }
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

void UltraCanvasAudioRecorderElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    Rect2Di b = GetLocalBounds();
    Relayout();

    // Background panel
    ctx->DrawFilledRectangle(Rect2Df(b.x, b.y, b.width, b.height),
                             style.backgroundColor, 1.0f,
                             style.borderColor, style.cornerRadius);

    DrawRecordButton(ctx);
    if (style.showElapsedTime) DrawTimeLabel(ctx);
    if (style.showWaveform) DrawWaveformStrip(ctx);
    else                    DrawLevelMeter(ctx);
    if (style.showDeviceSelect && !style.compact) DrawDeviceSelect(ctx);
    if (style.showSaveDiscard && !style.compact)  DrawSaveDiscardButtons(ctx);
}

void UltraCanvasAudioRecorderElement::DrawRecordButton(IRenderContext* ctx) {
    auto state = recorder->GetState();
    bool isRecording = state == AudioRecordingState::Recording;

    Color fill = isRecording
        ? (pulsePhase ? style.recordingPulseColor : style.recordButtonColor)
        : style.recordButtonColor;

    int cx = recordButtonRect.x + recordButtonRect.width / 2;
    int cy = recordButtonRect.y + recordButtonRect.height / 2;
    int r = recordButtonRect.width / 2 - 2;

    ctx->SetFillPaint(fill);
    ctx->FillCircle(Point2Df(cx, cy), r);

    if (isRecording) {
        // White square "stop" glyph on top of the recording button
        int sr = r / 2;
        ctx->SetFillPaint(Color(255, 255, 255));
        ctx->FillRectangle(Rect2Df(cx - sr, cy - sr, sr * 2, sr * 2));
    } else {
        // White ring + smaller red dot to convey "armed"
        ctx->SetStrokePaint(Color(255, 255, 255));
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawCircle(Point2Df(cx, cy), r - 2);
    }

    // Pause button
    if (pauseButtonRect.width > 0) {
        int px = pauseButtonRect.x + pauseButtonRect.width / 2;
        int py = pauseButtonRect.y + pauseButtonRect.height / 2;
        int pr = pauseButtonRect.width / 3;
        Color pcolor = (state == AudioRecordingState::Paused)
            ? style.iconColor : style.disabledColor;
        ctx->SetFillPaint(pcolor);
        if (state == AudioRecordingState::Paused) {
            // Resume (▶)
            std::vector<Point2Dd> tri = {
                Point2Dd(px - pr * 0.6f, py - pr),
                Point2Dd(px - pr * 0.6f, py + pr),
                Point2Dd(px + pr,        py)
            };
            ctx->FillLinePath(tri);
        } else {
            // Pause (▮▮)
            int bw = pr / 2;
            int gap = bw / 2;
            ctx->FillRectangle(Rect2Df(px - gap - bw, py - pr, bw, pr * 2));
            ctx->FillRectangle(Rect2Df(px + gap,      py - pr, bw, pr * 2));
        }
    }
}

void UltraCanvasAudioRecorderElement::DrawTimeLabel(IRenderContext* ctx) {
    std::string t = FormatTime(recorder->GetElapsed());
    ctx->SetFontSize(12);
    ctx->SetTextPaint(style.textColor);
    ctx->DrawText(t, Point2Df(timeLabelRect.x, timeLabelRect.y + 12));
}

void UltraCanvasAudioRecorderElement::DrawLevelMeter(IRenderContext* ctx) {
    // Background
    ctx->DrawFilledRectangle(
        Rect2Df(levelMeterRect.x, levelMeterRect.y,
                levelMeterRect.width, levelMeterRect.height),
        style.levelMeterBgColor, 0.0f, Color(0, 0, 0, 0),
        levelMeterRect.height / 2);

    // Three-zone meter: green (0..0.6), yellow (0.6..0.85), red (0.85..1.0).
    float peak = std::clamp(lastPeakLevel, 0.0f, 1.0f);
    int totalW = levelMeterRect.width;
    int fillW = static_cast<int>(totalW * peak);

    auto zoneRect = [&](float a, float b) {
        int x0 = levelMeterRect.x + static_cast<int>(totalW * a);
        int x1 = levelMeterRect.x + static_cast<int>(totalW * b);
        int xf = levelMeterRect.x + fillW;
        if (xf < x0) return Rect2Df(0, 0, 0, 0);
        int x_end = std::min(xf, x1);
        return Rect2Df(x0, levelMeterRect.y,
                       x_end - x0, levelMeterRect.height);
    };

    Rect2Df low  = zoneRect(0.0f, 0.6f);
    Rect2Df mid  = zoneRect(0.6f, 0.85f);
    Rect2Df high = zoneRect(0.85f, 1.0f);

    if (low.width  > 0) { ctx->SetFillPaint(style.levelMeterLowColor);  ctx->FillRectangle(low); }
    if (mid.width  > 0) { ctx->SetFillPaint(style.levelMeterMidColor);  ctx->FillRectangle(mid); }
    if (high.width > 0) { ctx->SetFillPaint(style.levelMeterHighColor); ctx->FillRectangle(high); }

    // RMS tick (thin vertical line)
    float rms = std::clamp(lastRmsLevel, 0.0f, 1.0f);
    int rmsX = levelMeterRect.x + static_cast<int>(totalW * rms);
    ctx->SetStrokePaint(Color(40, 40, 40));
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawLine(Point2Df(rmsX, levelMeterRect.y),
                  Point2Df(rmsX, levelMeterRect.y + levelMeterRect.height));
}

void UltraCanvasAudioRecorderElement::DrawWaveformStrip(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(
        Rect2Df(waveformRect.x, waveformRect.y,
                waveformRect.width, waveformRect.height),
        style.levelMeterBgColor, 0.0f, Color(0, 0, 0, 0), 4);

    if (waveformHistory.empty()) return;

    int n = waveformHistory.size();
    int midY = waveformRect.y + waveformRect.height / 2;
    int halfH = waveformRect.height / 2 - 2;
    float barW = static_cast<float>(waveformRect.width) / n;
    ctx->SetFillPaint(style.levelMeterLowColor);
    for (int i = 0; i < n; ++i) {
        float v = std::clamp(waveformHistory[i], 0.0f, 1.0f);
        int h = static_cast<int>(halfH * v);
        if (h <= 0) continue;
        float x = waveformRect.x + i * barW;
        ctx->FillRectangle(Rect2Df(x, midY - h, std::max(1.0f, barW - 1), h * 2));
    }
}

void UltraCanvasAudioRecorderElement::DrawDeviceSelect(IRenderContext* ctx) {
    ctx->DrawFilledRectangle(
        Rect2Df(deviceSelectRect.x, deviceSelectRect.y,
                deviceSelectRect.width, deviceSelectRect.height),
        Color(255, 255, 255), 1.0f, style.borderColor, 3);
    ctx->SetFontSize(10);
    ctx->SetTextPaint(style.textColor);
    // Truncate label if it overflows the chip
    std::string label = currentDeviceLabel;
    size_t maxChars = std::max<size_t>(4, (deviceSelectRect.width - 24) / 6);
    if (label.size() > maxChars) label = label.substr(0, maxChars - 1) + "…";
    ctx->DrawText(label + " ▾",
                  Point2Df(deviceSelectRect.x + 6, deviceSelectRect.y + 16));
}

void UltraCanvasAudioRecorderElement::OpenDevicePicker() {
    auto* window = GetWindow();
    if (!window) return;

    auto devices = UltraCanvasAudioDevices::ListInputDevices();
    if (devices.empty()) {
        // No real backend / no devices — still let user keep the default.
        AudioDeviceInfo placeholder;
        placeholder.name = "(no devices found)";
        devices.push_back(placeholder);
    }

    auto menu = std::make_shared<UltraCanvasMenu>(
        GetIdentifier() + ".DevicePicker", 0, 0, 220, 0);

    auto self = this;
    // Default-device entry always first
    menu->AddItem(MenuItemData("System default",
        [self]() {
            self->currentDeviceLabel = "Default mic";
            self->recorder->SetInputDevice("");
            self->RequestRedraw();
        }));

    for (const auto& dev : devices) {
        std::string label = dev.name + (dev.isDefault ? "  (default)" : "");
        std::string id = dev.id;
        std::string display = dev.name;
        menu->AddItem(MenuItemData(label,
            [self, id, display]() {
                self->currentDeviceLabel = display;
                self->recorder->SetInputDevice(id);
                self->RequestRedraw();
            }));
    }

    Point2Di anchor(GetXInWindow() + deviceSelectRect.x,
                    GetYInWindow() + deviceSelectRect.y + deviceSelectRect.height);
    PopupElementSettings settings;
    menu->OpenMenu(anchor, *window, settings);

    devicePopupMenu = menu;   // keep alive until the menu closes itself
}

void UltraCanvasAudioRecorderElement::DrawSaveDiscardButtons(IRenderContext* ctx) {
    auto state = recorder->GetState();
    bool enabled = state == AudioRecordingState::Stopped &&
                   recorder->GetSampleCount() > 0;

    auto drawBtn = [&](const Rect2Di& r, const std::string& label, const Color& bg) {
        if (r.width <= 0) return;
        ctx->DrawFilledRectangle(Rect2Df(r.x, r.y, r.width, r.height),
                                 enabled ? bg : style.disabledColor,
                                 1.0f, style.borderColor, 3);
        // Center the label inside the button the same way UltraCanvasButton does,
        // instead of anchoring it at a fixed offset. PushState/PopState keeps the
        // alignment change from leaking into other elements' rendering.
        ctx->PushState();
        ctx->SetFontSize(11);
        ctx->SetTextPaint(Color(255, 255, 255));
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(label, Rect2Df(r.x, r.y, r.width, r.height));
        ctx->PopState();
    };

    drawBtn(saveButtonRect,    "Save",    style.levelMeterLowColor);
    drawBtn(discardButtonRect, "Discard", style.levelMeterHighColor);
}

bool UltraCanvasAudioRecorderElement::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    Point2Di p = event.pointer;

    switch (event.type) {
        case UCEventType::MouseDown: {
            if (event.button != UCMouseButton::Left) return false;

            if (Hit(recordButtonRect, p)) {
                ToggleRecording();
                return true;
            }
            if (Hit(pauseButtonRect, p)) {
                auto state = recorder->GetState();
                if (state == AudioRecordingState::Recording) PauseRecording();
                else if (state == AudioRecordingState::Paused) ResumeRecording();
                return true;
            }
            if (Hit(saveButtonRect, p) &&
                recorder->GetState() == AudioRecordingState::Stopped) {
                ShowSaveDialog();
                return true;
            }
            if (Hit(discardButtonRect, p) &&
                recorder->GetState() == AudioRecordingState::Stopped) {
                DiscardRecording();
                return true;
            }
            if (Hit(deviceSelectRect, p)) {
                OpenDevicePicker();
                return true;
            }
            if (Hit(gainSliderRect, p)) {
                float pct = static_cast<float>(p.x - gainSliderRect.x) /
                            std::max(1.0f, gainSliderRect.width);
                recorder->SetInputGain(std::clamp(pct, 0.0f, 1.0f) * 2.0f);
                RequestRedraw();
                return true;
            }
            return false;
        }

        case UCEventType::MouseLeave:
            RequestRedraw();
            return false;

        default:
            return false;
    }
}

} // namespace UltraCanvas

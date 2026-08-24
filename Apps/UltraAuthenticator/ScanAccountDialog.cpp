// Apps/UltraAuthenticator/ScanAccountDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ScanAccountDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"
#include "Plugins/QRCode/UltraCanvasQRCode.h"
#include "UltraCrypt/UltraCryptCore.h"

namespace UltraCanvas {
namespace Authenticator {

namespace {

bool LooksLikeOtpAuth(const std::string& text) {
    // Case-insensitive on the scheme only. Everything else is the parser's
    // business — this just decides whether a decoded QR is worth handing over.
    static const std::string kScheme = "otpauth://";
    if (text.size() < kScheme.size()) return false;
    for (size_t i = 0; i < kScheme.size(); ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kScheme[i]) return false;
    }
    return true;
}

} // namespace

ScanAccountDialog::ScanAccountDialog(UltraCanvasApplication& app) : app_(app) {}

ScanAccountDialog::~ScanAccountDialog() {
    StopScanning();
}

void ScanAccountDialog::CreateScanAccountDialog() {
    DialogConfig cfg;
    cfg.title      = "Scan account QR code";
    cfg.width      = kDialogWidth;
    cfg.height     = kDialogHeight;
    cfg.message    = "";
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = false;
    cfg.dialogType = DialogType::Custom;   // see AddAccountDialog for why
    autoSizeHeight = false;
    CreateDialog(cfg);

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    long y = margin;

    hintLabel_ = std::make_shared<UltraCanvasLabel>(
        "scan-hint", margin, y, fieldWidth, 36,
        "Point the camera at the QR code the service showed you. "
        "Nothing is recorded — the picture is never saved.");
    hintLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    hintLabel_->SetTextColor(Theme::kTextSecondary);
    hintLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(hintLabel_);
    y += 46;

    // The catalogue's recorder element draws the live preview; this dialog only
    // reads frames off its recorder. Nothing here paints video by hand.
    preview_ = std::make_shared<UltraCanvasVideoRecorderElement>(
        "scan-preview", margin, y, fieldWidth, 340);
    VideoCaptureConfig capture;
    capture.captureAudio = false;      // a QR code has no sound; do not open a mic
    capture.width        = 1280;
    capture.height       = 720;
    preview_->SetConfig(capture);

    // No record button and no elapsed-time counter. This dialog promises that
    // nothing is saved, and a record control sitting under that promise would
    // be a contradiction the user could act on. The camera picker stays: with
    // more than one camera, choosing the right one is the difference between
    // scanning and not.
    VideoRecorderStyle previewStyle = preview_->GetStyle();
    previewStyle.showRecordButton = false;
    previewStyle.showElapsedTime  = false;
    previewStyle.showCameraSelect = true;
    preview_->SetStyle(previewStyle);
    preview_->onPermissionChanged = [this](CameraPermission permission) {
        if (permission == CameraPermission::Denied) {
            SetStatus("Camera access was refused. You can still add the "
                      "account by typing its setup key.", true);
            StopScanning();
        }
    };
    preview_->onError = [this](const std::string& message) {
        SetStatus(message, true);
    };
    AddChild(preview_);
    y += 350;

    statusLabel_ = std::make_shared<UltraCanvasLabel>(
        "scan-status", margin, y, fieldWidth, 36, "Looking for a QR code…");
    statusLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    statusLabel_->SetTextColor(Theme::kTextMuted);
    statusLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(statusLabel_);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "scan-cancel", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() {
        StopScanning();
        CloseDialog(DialogResult::Cancel);
    };
    AddChild(cancelBtn);

    if (!QRCodeUtils::IsDecoderAvailable()) {
        // Built without libzbar. Say so plainly instead of showing a preview
        // that could never decode anything.
        SetStatus("This build has no QR decoder, so scanning is unavailable. "
                  "Add the account by typing its setup key instead.", true);
        return;
    }

    // live=true: the moving preview is what the user aims with.
    if (!preview_->OpenCamera(true)) {
        SetStatus("No camera is available. You can still add the account by "
                  "typing its setup key.", true);
        return;
    }

    scanTimer_ = app_.StartTimer(kScanIntervalMs, true, [this](TimerId) {
        PollFrame();
    });
    timerRunning_ = true;
}

void ScanAccountDialog::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextMuted);
}

void ScanAccountDialog::StopScanning() {
    if (timerRunning_) {
        app_.StopTimer(scanTimer_);
        timerRunning_ = false;
    }
    // Release the camera as soon as scanning ends. Holding it open after the
    // dialog is done would leave the indicator light on with nothing watching.
    if (preview_) preview_->CloseCamera();
}

void ScanAccountDialog::PollFrame() {
    if (accepted_ || !preview_) return;

    auto recorder = preview_->GetRecorder();
    if (!recorder) return;

    UCVideoFramePtr frame = recorder->GetPreviewFrame();
    if (!frame || !frame->IsValid()) return;

    std::string error;
    auto results = QRCodeUtils::ScanQRCodeImage(*frame, &error);
    if (results.empty()) return;   // no code in view; keep looking, say nothing

    // Take the first decoded symbol that looks like an account. A poster in
    // shot alongside the enrolment code should not derail the scan.
    for (const QRScanResult& result : results) {
        if (!result.valid || !LooksLikeOtpAuth(result.data)) continue;

        // Stop the camera before handing the URI on: from here the string is
        // a live secret, and there is no reason to keep capturing while a
        // dialog decides what to do with it.
        accepted_ = true;
        StopScanning();

        std::string uri = result.data;
        std::string handlerError;
        if (onScanned) handlerError = onScanned(uri);

        // The URI carries the Base32 seed, so wipe this copy rather than
        // letting it sit in the dialog's memory until destruction.
        if (!uri.empty()) UltraCrypt_SecureZero(&uri[0], uri.size());

        if (!handlerError.empty()) {
            // Rejected — let the user try another code rather than closing.
            accepted_ = false;
            SetStatus(handlerError, true);
            if (preview_->OpenCamera(true)) {
                scanTimer_ = app_.StartTimer(kScanIntervalMs, true,
                                             [this](TimerId) { PollFrame(); });
                timerRunning_ = true;
            }
            return;
        }
        CloseDialog(DialogResult::OK);
        return;
    }

    // Something decoded, but it was not an account code. Say it once.
    if (!reportedNonAccountCode_) {
        reportedNonAccountCode_ = true;
        SetStatus("That QR code is not an authenticator account. Look for the "
                  "one the service shows during two-factor setup.");
    }
}

} // namespace Authenticator
} // namespace UltraCanvas

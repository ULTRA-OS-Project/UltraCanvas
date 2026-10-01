// Apps/UltraAuthenticator/ScanAccountDialog.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ScanAccountDialog.h"
#include "Theme.h"

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasFileLoader.h"
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

const char* const kNotAccountMessage =
    "That QR code is not an authenticator account. Look for the one the "
    "service shows during two-factor setup.";

// What to say when a one-off decode found nothing. The scanner leaves the
// error empty for an image with no code in it and fills it only when the
// scan could not run (an unreadable file, no decoder), and the two deserve
// different sentences: the friendly hint the caller passes in, or the reason.
std::string NoCodeStatus(const std::string& friendly, const std::string& reasonPrefix,
                         const std::string& error) {
    return error.empty() ? friendly : reasonPrefix + error;
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
        "Point the camera at the QR code the service showed you, or read it "
        "from an image file or from this screen. Nothing is recorded — no "
        "picture is ever saved.");
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
            SetStatus("Camera access was refused. You can still read the code "
                      "from an image or from the screen, or type its setup key.",
                      true);
            StopScanning();
            cameraOpened_ = false;
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

    // The bottom row: the two other sources on the left, Cancel on the right.
    const long buttonY = kDialogHeight - 54;
    imageBtn_ = std::make_shared<UltraCanvasButton>(
        "scan-from-image", margin, buttonY, 130, 32);
    imageBtn_->SetText("From image…");
    imageBtn_->onClick = [this]() { ScanImageFile(); };
    AddChild(imageBtn_);

    screenBtn_ = std::make_shared<UltraCanvasButton>(
        "scan-from-screen", margin + 140, buttonY, 130, 32);
    screenBtn_->SetText("From screen");
    screenBtn_->onClick = [this]() { ScanScreen(); };
    AddChild(screenBtn_);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "scan-cancel", kDialogWidth - margin - 100, buttonY, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() {
        StopScanning();
        CloseDialog(DialogResult::Cancel);
    };
    AddChild(cancelBtn);

    if (!QRCodeUtils::IsDecoderAvailable()) {
        // Built without libzbar. Say so plainly instead of showing a preview
        // that could never decode anything. The image and screen buttons need
        // the same decoder, so they go too.
        imageBtn_->SetDisabled(true);
        screenBtn_->SetDisabled(true);
        SetStatus("This build has no QR decoder, so scanning is unavailable. "
                  "Add the account by typing its setup key instead.", true);
        return;
    }

    // live=true: the moving preview is what the user aims with.
    if (!preview_->OpenCamera(true)) {
        SetStatus("No camera is available. Read the code from an image or from "
                  "the screen instead, or type its setup key.", true);
        return;
    }
    cameraOpened_ = true;
    StartScanning();
}

void ScanAccountDialog::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextMuted);
}

void ScanAccountDialog::StartScanning() {
    if (timerRunning_) return;
    scanTimer_ = app_.StartTimer(kScanIntervalMs, true, [this](TimerId) {
        PollFrame();
    });
    timerRunning_ = true;
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
    // No code in view: keep looking, say nothing. A decoded non-account code
    // is reported once rather than on every frame, which would make the
    // status line flicker unreadably.
    OfferResults(results, std::string());
}

void ScanAccountDialog::ScanImageFile() {
    if (accepted_) return;

    FileDialogOptions opts;
    opts.SetTitle("Open the QR code image");
    opts.AddFilter("Images", std::vector<std::string>{
        "png", "jpg", "jpeg", "webp", "gif", "bmp", "tif", "tiff", "qoi", "avif"});
    opts.AddFilter("All files", std::vector<std::string>{ "*" });
    opts.SetParentWindow(this);
    // The picked file is a seed in the clear. It must not turn up in the
    // desktop's recent-files list for anyone to open later.
    opts.registerAsRecent = false;

    UltraCanvasFileLoader::OpenFileDialog(
        opts, [this](DialogResult result, const std::string& path) {
            if (accepted_) return;
            if (result != DialogResult::OK || path.empty()) return;

            // Read by the scanner only: the file is never copied, converted
            // or thumbnailed by this app. What is on disk was there already.
            std::string error;
            auto results = QRCodeUtils::ScanQRCodeFile(path, &error);
            OfferResults(results, NoCodeStatus("No QR code was found in that image.",
                                               "The image could not be read: ", error));
        });
}

void ScanAccountDialog::ScanScreen() {
    if (accepted_) return;

    // In memory only. CaptureScreen() would write a PNG, and a PNG of an
    // enrolment QR is the seed sitting in the Pictures folder.
    DesktopScreenImage shot;
    std::string error;
    if (!UltraCanvasDesktopShell::CaptureScreenImage(shot, &error)) {
        SetStatus("The screen could not be captured: " + error, true);
        return;
    }

    auto results = QRCodeUtils::ScanQRCodeImage(
        shot.pixels.data(), shot.width, shot.height, shot.stride,
        QRCodeUtils::QRPixelFormat::BGRA32, &error);

    // The capture may contain the QR, so it is a copy of the seed: wipe it
    // now rather than when the vector happens to be freed.
    if (!shot.pixels.empty()) {
        UltraCrypt_SecureZero(shot.pixels.data(), shot.pixels.size());
    }

    OfferResults(results,
                 NoCodeStatus("No QR code is visible on the screen. Put the page "
                              "showing the code beside this window, where nothing "
                              "covers it, and try again.",
                              "The screen could not be decoded: ", error));
}

bool ScanAccountDialog::OfferResults(const std::vector<QRScanResult>& results,
                                     const std::string& noCodeMessage) {
    if (results.empty()) {
        if (!noCodeMessage.empty()) SetStatus(noCodeMessage, true);
        return false;
    }

    // Take the first decoded symbol that looks like an account. A poster in
    // shot alongside the enrolment code should not derail the scan.
    for (const QRScanResult& result : results) {
        if (!result.valid || !LooksLikeOtpAuth(result.data)) continue;
        return AcceptUri(result.data);
    }

    // Something decoded, but it was not an account code. The camera says it
    // once; a deliberate one-off scan says it every time, since each is a
    // separate attempt the user is waiting on.
    if (!noCodeMessage.empty()) {
        SetStatus(kNotAccountMessage, true);
    } else if (!reportedNonAccountCode_) {
        reportedNonAccountCode_ = true;
        SetStatus(kNotAccountMessage);
    }
    return false;
}

bool ScanAccountDialog::AcceptUri(std::string uri) {
    // Stop the camera before handing the URI on: from here the string is a
    // live secret, and there is no reason to keep capturing while a dialog
    // decides what to do with it.
    accepted_ = true;
    StopScanning();

    std::string handlerError;
    if (onScanned) handlerError = onScanned(uri);

    // The URI carries the Base32 seed, so wipe this copy rather than letting
    // it sit in the dialog's memory until destruction.
    if (!uri.empty()) UltraCrypt_SecureZero(&uri[0], uri.size());

    if (!handlerError.empty()) {
        // Rejected — let the user try another code rather than closing.
        accepted_ = false;
        SetStatus(handlerError, true);
        if (cameraOpened_ && preview_ && preview_->OpenCamera(true)) {
            StartScanning();
        }
        return false;
    }
    CloseDialog(DialogResult::OK);
    return true;
}

} // namespace Authenticator
} // namespace UltraCanvas

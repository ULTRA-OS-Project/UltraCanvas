// Apps/UltraAuthenticator/ScanAccountDialog.h
// Enrol an account by pointing the camera at the service's QR code.
//
// This is how every authenticator is meant to be used, and until now the app
// had only manual Base32 entry — accurate, but a 32-character transcription
// nobody enjoys and everybody mistypes.
//
// Two properties matter more than the convenience:
//
//  - **Nothing is written to disk.** The camera is opened for preview only and
//    recording is never started, so no frame ever reaches a file. That is not
//    incidental tidiness: a preview frame containing an enrolment QR *is* the
//    seed, and a frame written to a temp file or picked up by a thumbnailer
//    would leak the second factor permanently
//    (UltraAuthenticator-Investigation.md §2.2c, §3.5).
//  - **Frames are pulled on the UI thread**, by this dialog's own timer
//    calling GetPreviewFrame(), rather than by hooking the recorder's
//    onPreviewFrame callback. The callback's thread is not documented, and a
//    decode that ran on a capture thread would be touching the vault from
//    somewhere the rest of the app does not expect. Polling also throttles the
//    scan naturally: a QR does not need decoding sixty times a second.
//
// The scanned string is not trusted. It is handed straight to the same
// AccountStore::AddFromUri that manual entry uses, so a hostile QR code faces
// exactly the parser that Tests/UltraOtpTests.cpp exercises — there is no
// second, laxer path into the vault.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef SCANACCOUNTDIALOG_H
#define SCANACCOUNTDIALOG_H

#include "UltraCanvasApplication.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasVideoRecorderElement.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class ScanAccountDialog : public UltraCanvasModalDialog {
public:
    explicit ScanAccountDialog(UltraCanvasApplication& app);
    ~ScanAccountDialog() override;

    void CreateScanAccountDialog();

    // Called with the decoded otpauth:// URI. Returns an error to display, or
    // an empty string on success (which closes the dialog). Same contract as
    // AddAccountDialog: a rejected code leaves the camera running so the user
    // can try another, rather than dumping them back to the account list.
    std::function<std::string(const std::string& uri)> onScanned;

private:
    void PollFrame();
    void StopScanning();
    void SetStatus(const std::string& text, bool isError = false);

    UltraCanvasApplication& app_;

    std::shared_ptr<UltraCanvasVideoRecorderElement> preview_;
    std::shared_ptr<UltraCanvasLabel>                statusLabel_;
    std::shared_ptr<UltraCanvasLabel>                hintLabel_;

    TimerId scanTimer_   = 0;
    bool    timerRunning_ = false;
    bool    accepted_     = false;

    // A QR that is not an account URI is common — a poster, a wifi code — so
    // it is reported once rather than on every frame, which would make the
    // status line flicker unreadably.
    bool    reportedNonAccountCode_ = false;

    static constexpr long kDialogWidth  = 620;
    static constexpr long kDialogHeight = 560;
    // 5 Hz: fast enough that aiming feels responsive, slow enough that a
    // 1280x720 frame conversion is not run sixty times a second.
    static constexpr int  kScanIntervalMs = 200;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // SCANACCOUNTDIALOG_H

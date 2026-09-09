// include/UltraCanvasAlert.h
// Modal alert dialogs for UltraCanvas.
//
// An Alert is a floating, always-on-top, modal message box that the user
// cannot miss: it blocks interaction with the rest of the application until
// dismissed. It is a thin, purpose-built façade over the UltraCanvas dialog
// system (UltraCanvasModalDialog / UltraCanvasDialogManager) that gives every
// alert a clear severity — Info, Success, Warning, Error or Question — a
// matching coloured icon, and a one-call API.
//
// Alerts are non-blocking: they show immediately and deliver the user's choice
// through a callback, so the application main loop keeps running underneath.
//
//   UltraCanvasAlert::Error("Could not save the file.");
//   UltraCanvasAlert::Successful("Export complete.");
//   UltraCanvasAlert::Confirm("Delete this item?", "Confirm",
//                             [](bool yes){ if (yes) deleteItem(); });
//
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasModalDialog.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== ALERT SEVERITY =====
    enum class AlertSeverity {
        Info,
        Successful,   // "Success" is an X11 macro (#define Success 0), so we spell it out
        Warning,
        Error,
        Question
    };

// ===== ALERT OPTIONS (rich form) =====
    struct AlertOptions {
        std::string message;
        std::string title;        // empty => derived from the severity
        std::string details;      // optional secondary line
        AlertSeverity severity = AlertSeverity::Info;
        // The coloured severity badge on the left of the message. Set to false
        // for an alert whose body carries its own graphic (a progress ring, a
        // chart, a preview): without the badge the message column spans the
        // whole window, so that graphic is horizontally centred in the dialog
        // instead of being pushed to the right of the icon.
        bool showIcon = true;
        DialogButtons buttons = DialogButtons::OK;
        DialogButton  defaultButton = DialogButton::OK;
        UltraCanvasWindowBase* parent = nullptr;
        std::function<void(DialogResult)> onResult;
    };

// ===== ALERT FAÇADE =====
// All methods are non-blocking. The result (which button was pressed) is
// delivered via the callback. `parent` centres the alert over that window and
// is the window whose input is blocked; pass nullptr to centre on screen.
    class UltraCanvasAlert {
    public:
        // ----- rich form -----
        static void Show(const AlertOptions& opts) {
            DialogConfig config = UltraCanvasDialogManager::GetDefaultConfig();
            config.message     = opts.message;
            config.details     = opts.details;
            config.dialogType  = ToDialogType(opts.severity);
            config.showIcon    = opts.showIcon;
            config.buttons     = opts.buttons;
            config.defaultButton = opts.defaultButton;
            // Leave the config title at its default ("Dialog") when none is given
            // so the dialog fills in the severity name (Information/Success/...).
            if (!opts.title.empty()) config.title = opts.title;

            auto dialog = UltraCanvasDialogManager::CreateDialog(config);
            UltraCanvasDialogManager::ShowDialog(dialog, opts.onResult, opts.parent);
        }

        // ----- severity one-liners (OK button) -----
        static void Info(const std::string& message, const std::string& title = "",
                         std::function<void(DialogResult)> onResult = nullptr,
                         UltraCanvasWindowBase* parent = nullptr) {
            ShowSimple(AlertSeverity::Info, message, title, onResult, parent);
        }

        static void Successful(const std::string& message, const std::string& title = "",
                               std::function<void(DialogResult)> onResult = nullptr,
                               UltraCanvasWindowBase* parent = nullptr) {
            ShowSimple(AlertSeverity::Successful, message, title, onResult, parent);
        }

        static void Warning(const std::string& message, const std::string& title = "",
                            std::function<void(DialogResult)> onResult = nullptr,
                            UltraCanvasWindowBase* parent = nullptr) {
            ShowSimple(AlertSeverity::Warning, message, title, onResult, parent);
        }

        static void Error(const std::string& message, const std::string& title = "",
                          std::function<void(DialogResult)> onResult = nullptr,
                          UltraCanvasWindowBase* parent = nullptr) {
            ShowSimple(AlertSeverity::Error, message, title, onResult, parent);
        }

        // ----- icon-less message (OK button) -----
        // Same as Info()/Warning()/... but without the severity badge, so the
        // message column — and anything the caller adds to it — is centred in
        // the full width of the dialog.
        static void Plain(const std::string& message, const std::string& title = "",
                          std::function<void(DialogResult)> onResult = nullptr,
                          UltraCanvasWindowBase* parent = nullptr,
                          AlertSeverity severity = AlertSeverity::Info) {
            ShowSimple(severity, message, title, onResult, parent,
                       /*showIcon=*/false);
        }

        // ----- yes/no confirmation -> bool -----
        static void Confirm(const std::string& message, const std::string& title,
                            std::function<void(bool confirmed)> onConfirmed,
                            UltraCanvasWindowBase* parent = nullptr) {
            AlertOptions opts;
            opts.severity = AlertSeverity::Question;
            opts.message = message;
            opts.title = title;
            opts.buttons = DialogButtons::YesNo;
            opts.defaultButton = DialogButton::Yes;
            opts.parent = parent;
            opts.onResult = [onConfirmed](DialogResult r) {
                if (onConfirmed) onConfirmed(r == DialogResult::Yes);
            };
            Show(opts);
        }

        // ----- severity <-> dialog type mapping -----
        static DialogType ToDialogType(AlertSeverity s) {
            switch (s) {
                case AlertSeverity::Info:       return DialogType::Information;
                case AlertSeverity::Successful: return DialogType::Successful;
                case AlertSeverity::Warning:    return DialogType::Warning;
                case AlertSeverity::Error:      return DialogType::Error;
                case AlertSeverity::Question:   return DialogType::Question;
            }
            return DialogType::Information;
        }

    private:
        static void ShowSimple(AlertSeverity severity, const std::string& message,
                               const std::string& title,
                               std::function<void(DialogResult)> onResult,
                               UltraCanvasWindowBase* parent,
                               bool showIcon = true) {
            AlertOptions opts;
            opts.severity = severity;
            opts.showIcon = showIcon;
            opts.message = message;
            opts.title = title;
            opts.buttons = DialogButtons::OK;
            opts.defaultButton = DialogButton::OK;
            opts.parent = parent;
            opts.onResult = std::move(onResult);
            Show(opts);
        }
    };

} // namespace UltraCanvas

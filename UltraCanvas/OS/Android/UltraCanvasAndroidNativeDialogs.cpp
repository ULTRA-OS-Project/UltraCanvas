// OS/Android/UltraCanvasAndroidNativeDialogs.cpp
// Android implementation of the UltraCanvasNativeDialogs statics (link-time
// selected).
//
// Message dialogs are real: they go through UltraCanvasAndroidDialogBridge to
// an AlertDialog in UltraCanvasActivity. An app running a plain NativeActivity
// has no such bridge, so those calls fall back to the "cancelled" stub below -
// as do the file and input dialogs, which still need the Storage Access
// Framework adapter (investigation §3.5).
// Version: 1.1.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasAndroidDialogBridge.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {
        DialogResult StubDialog(const char* what, const std::string& title) {
            debugOutput << "UltraCanvas Android: native dialog '" << what
                        << "' (" << title << ") not implemented - returning Cancel"
                        << std::endl;
            return DialogResult::Cancel;
        }

        // Which Android buttons a DialogButtons set maps onto, and what each
        // one means coming back. Android shows at most three (positive,
        // negative, neutral); neutral is the leftmost on screen, which is why
        // Cancel lands there for the three-button sets.
        struct ButtonSpec {
            const char* label = nullptr;          // null: button omitted
            DialogResult result = DialogResult::Cancel;
        };
        struct ButtonLayout {
            ButtonSpec positive, negative, neutral;
            DialogResult onDismiss = DialogResult::Cancel;   // back button
        };

        ButtonLayout LayoutFor(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OKCancel:
                    return { {"OK", DialogResult::OK},
                             {"Cancel", DialogResult::Cancel}, {},
                             DialogResult::Cancel };
                case DialogButtons::YesNo:
                    return { {"Yes", DialogResult::Yes},
                             {"No", DialogResult::No}, {},
                             DialogResult::No };
                case DialogButtons::YesNoCancel:
                    return { {"Yes", DialogResult::Yes},
                             {"No", DialogResult::No},
                             {"Cancel", DialogResult::Cancel},
                             DialogResult::Cancel };
                case DialogButtons::RetryCancel:
                    return { {"Retry", DialogResult::Retry},
                             {"Cancel", DialogResult::Cancel}, {},
                             DialogResult::Cancel };
                case DialogButtons::AbortRetryIgnore:
                    return { {"Retry", DialogResult::Retry},
                             {"Abort", DialogResult::Abort},
                             {"Ignore", DialogResult::Ignore},
                             DialogResult::Abort };
                case DialogButtons::NoButtons:
                case DialogButtons::OK:
                default:
                    // Nothing to choose: dismissing IS acknowledging.
                    return { {"OK", DialogResult::OK}, {}, {},
                             DialogResult::OK };
            }
        }

        AndroidDialogs::JavaIcon IconFor(DialogType type) {
            switch (type) {
                case DialogType::Warning:
                case DialogType::Error:
                    return AndroidDialogs::JavaIcon::Alert;
                case DialogType::Information:
                case DialogType::Successful:
                    return AndroidDialogs::JavaIcon::Info;
                default:
                    return AndroidDialogs::JavaIcon::None;
            }
        }

        // Returns NoResult when there is no Java bridge, so each caller can
        // apply its own fallback.
        DialogResult ShowViaJava(const std::string& message,
                                 const std::string& title,
                                 DialogType type, DialogButtons buttons) {
            const ButtonLayout layout = LayoutFor(buttons);
            auto outcome = AndroidDialogs::ShowMessage(
                    title, message, IconFor(type),
                    layout.positive.label, layout.negative.label,
                    layout.neutral.label);
            if (!outcome.bridged) return DialogResult::NoResult;

            switch (outcome.result) {
                case AndroidDialogs::JavaResult::Positive: return layout.positive.result;
                case AndroidDialogs::JavaResult::Negative: return layout.negative.result;
                case AndroidDialogs::JavaResult::Neutral:  return layout.neutral.result;
                case AndroidDialogs::JavaResult::Cancel:
                default:                                   return layout.onDismiss;
            }
        }

        DialogResult ShowOrStub(const char* what, const std::string& message,
                                const std::string& title,
                                DialogType type, DialogButtons buttons) {
            const DialogResult result = ShowViaJava(message, title, type, buttons);
            if (result != DialogResult::NoResult) return result;
            return StubDialog(what, title);
        }
    }

    // ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase*) {
        return ShowOrStub("ShowInfo", message, title,
                          DialogType::Information, DialogButtons::OK);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string& message, const std::string& title, UltraCanvasWindowBase*) {
        return ShowOrStub("ShowWarning", message, title,
                          DialogType::Warning, DialogButtons::OK);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string& message, const std::string& title, UltraCanvasWindowBase*) {
        return ShowOrStub("ShowError", message, title,
                          DialogType::Error, DialogButtons::OK);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string& message, const std::string& title, DialogButtons buttons,
            UltraCanvasWindowBase*) {
        return ShowOrStub("ShowQuestion", message, title,
                          DialogType::Question, buttons);
    }

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string& message, const std::string& title, DialogType type,
            DialogButtons buttons, UltraCanvasWindowBase*) {
        return ShowOrStub("ShowMessage", message, title, type, buttons);
    }

    // ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string& message, const std::string& title, UltraCanvasWindowBase*) {
        const DialogResult result = ShowViaJava(message, title,
                                                DialogType::Question,
                                                DialogButtons::OKCancel);
        if (result == DialogResult::NoResult) {
            StubDialog("Confirm", title);
            return false;
        }
        return result == DialogResult::OK;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase*) {
        const DialogResult result = ShowViaJava(message, title,
                                                DialogType::Question,
                                                DialogButtons::YesNo);
        if (result == DialogResult::NoResult) {
            StubDialog("ConfirmYesNo", title);
            return false;
        }
        return result == DialogResult::Yes;
    }

    // ===== FILE DIALOGS =====

    std::string UltraCanvasNativeDialogs::OpenFile(
            const std::string& title, const std::vector<FileFilter>&,
            const std::string&, UltraCanvasWindowBase*) {
        StubDialog("OpenFile", title);
        return {};
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const FileDialogOptions& options) {
        StubDialog("OpenFile", options.title);
        return {};
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string& title, const std::vector<FileFilter>&,
            const std::string&, UltraCanvasWindowBase*) {
        StubDialog("OpenMultipleFiles", title);
        return {};
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const FileDialogOptions& options) {
        StubDialog("OpenMultipleFiles", options.title);
        return {};
    }

    std::string UltraCanvasNativeDialogs::SaveFile(
            const std::string& title, const std::vector<FileFilter>&,
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        StubDialog("SaveFile", title);
        return {};
    }

    std::string UltraCanvasNativeDialogs::SaveFile(const FileDialogOptions& options) {
        StubDialog("SaveFile", options.title);
        return {};
    }

    std::string UltraCanvasNativeDialogs::SelectFolder(
            const std::string& title, const std::string&, UltraCanvasWindowBase*) {
        StubDialog("SelectFolder", title);
        return {};
    }

    // ===== INPUT DIALOGS =====

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const std::string&, const std::string& title, const std::string&,
            UltraCanvasWindowBase*) {
        StubDialog("InputText", title);
        return {};
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const NativeInputDialogOptions& options) {
        StubDialog("InputText", options.title);
        return {};
    }

    NativeInputResult UltraCanvasNativeDialogs::InputPassword(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        StubDialog("InputPassword", title);
        return {};
    }

    // ===== CONVENIENCE FUNCTIONS =====

    std::string UltraCanvasNativeDialogs::GetInput(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputText(prompt, title, defaultValue, parent);
        return r.IsOK() ? r.value : std::string();
    }

    std::string UltraCanvasNativeDialogs::GetPassword(
            const std::string& prompt, const std::string& title,
            UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputPassword(prompt, title, parent);
        return r.IsOK() ? r.value : std::string();
    }

    // ===== PRINTING =====

    bool UltraCanvasNativeDialogs::ShowPrintDialog(
            const std::string& documentName, const std::string&,
            UltraCanvasWindowBase*) {
        StubDialog("ShowPrintDialog", documentName);
        return false;
    }

} // namespace UltraCanvas

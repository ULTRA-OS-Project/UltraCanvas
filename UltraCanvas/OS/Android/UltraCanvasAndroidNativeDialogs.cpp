// OS/Android/UltraCanvasAndroidNativeDialogs.cpp
// Android stubs for the UltraCanvasNativeDialogs statics (link-time selected).
// Real Android dialogs are callback-based JNI (AlertDialog / Storage Access
// Framework) and need an async bridge - a later phase (investigation §3.5).
// Until then every dialog resolves as "cancelled" so callers take their
// normal cancel path instead of blocking or crashing.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {
        DialogResult StubDialog(const char* what, const std::string& title) {
            debugOutput << "UltraCanvas Android: native dialog '" << what
                        << "' (" << title << ") not implemented - returning Cancel"
                        << std::endl;
            return DialogResult::Cancel;
        }
    }

    // ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        return StubDialog("ShowInfo", title);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        return StubDialog("ShowWarning", title);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        return StubDialog("ShowError", title);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string&, const std::string& title, DialogButtons,
            UltraCanvasWindowBase*) {
        return StubDialog("ShowQuestion", title);
    }

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string&, const std::string& title, DialogType, DialogButtons,
            UltraCanvasWindowBase*) {
        return StubDialog("ShowMessage", title);
    }

    // ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        StubDialog("Confirm", title);
        return false;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string&, const std::string& title, UltraCanvasWindowBase*) {
        StubDialog("ConfirmYesNo", title);
        return false;
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

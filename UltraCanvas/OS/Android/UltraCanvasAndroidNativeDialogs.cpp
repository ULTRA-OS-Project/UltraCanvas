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

#include <algorithm>
#include <cctype>
#include <optional>

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

        // SAF filters by MIME type; the framework's filters are extensions.
        // Only the common families are worth mapping - anything unmapped
        // simply widens the picker rather than hiding files the user wants.
        const char* MimeForExtension(std::string ext) {
            if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c)));

            if (ext == "png")  return "image/png";
            if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
            if (ext == "gif")  return "image/gif";
            if (ext == "webp") return "image/webp";
            if (ext == "bmp")  return "image/bmp";
            if (ext == "svg")  return "image/svg+xml";
            if (ext == "tif" || ext == "tiff") return "image/tiff";
            if (ext == "pdf")  return "application/pdf";
            if (ext == "json") return "application/json";
            if (ext == "xml")  return "text/xml";
            if (ext == "html" || ext == "htm") return "text/html";
            if (ext == "csv")  return "text/csv";
            if (ext == "txt" || ext == "log" || ext == "md") return "text/plain";
            if (ext == "zip")  return "application/zip";
            if (ext == "mp3")  return "audio/mpeg";
            if (ext == "wav")  return "audio/wav";
            if (ext == "ogg")  return "audio/ogg";
            if (ext == "mp4")  return "video/mp4";
            if (ext == "webm") return "video/webm";
            return nullptr;
        }

        // "image/png,image/jpeg", or empty to offer every file. Empty is also
        // the answer when any extension is unmapped or a wildcard is present:
        // a picker that hides a file the user asked for is worse than one
        // that shows too much.
        std::string MimeCsvFor(const std::vector<FileFilter>& filters) {
            std::vector<std::string> types;
            for (const auto& filter : filters) {
                for (const auto& ext : filter.extensions) {
                    if (ext == "*" || ext == "*.*" || ext.empty()) return {};
                    const char* mime = MimeForExtension(ext);
                    if (!mime) return {};
                    if (std::find(types.begin(), types.end(), mime) == types.end()) {
                        types.emplace_back(mime);
                    }
                }
            }
            std::string csv;
            for (const auto& type : types) {
                if (!csv.empty()) csv += ',';
                csv += type;
            }
            return csv;
        }

        std::vector<std::string> SplitLines(const std::string& text) {
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (start <= text.size()) {
                const std::size_t end = text.find('\n', start);
                if (end == std::string::npos) {
                    if (start < text.size()) lines.push_back(text.substr(start));
                    break;
                }
                if (end > start) lines.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return lines;
        }

        // Returns the picked paths, or nullopt when there is no Java bridge.
        std::optional<std::vector<std::string>> PickDocuments(
                const std::vector<FileFilter>& filters, bool allowMultiple) {
            auto outcome = AndroidDialogs::ShowOpenDocument(MimeCsvFor(filters),
                                                            allowMultiple);
            if (!outcome.bridged) return std::nullopt;
            if (outcome.result != AndroidDialogs::JavaResult::Positive) {
                return std::vector<std::string>{};   // user cancelled
            }
            return SplitLines(outcome.value);
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
            const std::string& title, const std::vector<FileFilter>& filters,
            const std::string&, UltraCanvasWindowBase*) {
        auto picked = PickDocuments(filters, false);
        if (!picked) {
            StubDialog("OpenFile", title);
            return {};
        }
        return picked->empty() ? std::string() : picked->front();
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const FileDialogOptions& options) {
        return OpenFile(options.title, options.filters,
                        options.initialDirectory, options.parentWindow);
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string& title, const std::vector<FileFilter>& filters,
            const std::string&, UltraCanvasWindowBase*) {
        auto picked = PickDocuments(filters, true);
        if (!picked) {
            StubDialog("OpenMultipleFiles", title);
            return {};
        }
        return *picked;
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const FileDialogOptions& options) {
        return OpenMultipleFiles(options.title, options.filters,
                                 options.initialDirectory, options.parentWindow);
    }

    // Saving cannot be bridged the way opening is, and the reason is the API
    // shape rather than anything Android-specific. SaveFile() hands back a
    // path and returns; the caller writes to it afterwards, and nothing tells
    // us when it is done. Opening survives that because a copy of the document
    // is a complete answer at return time. For saving, the copy would have to
    // be pushed back to the content:// URI at a commit point this API has no
    // way to express (NotifyRecentFile fires before the caller writes a byte),
    // so a bridged SaveFile would silently drop the user's data.
    //
    // Closing this needs a cross-platform API decision - a save variant that
    // takes the bytes, or an explicit commit call - so it is deliberately
    // still a stub rather than a plausible-looking one that loses work.
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

    // Likewise: ACTION_OPEN_DOCUMENT_TREE yields a tree URI that callers would
    // enumerate and write through, which no single filesystem path can stand
    // in for.
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

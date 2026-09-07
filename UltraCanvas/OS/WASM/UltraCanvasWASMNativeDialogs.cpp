// OS/WASM/UltraCanvasWASMNativeDialogs.cpp
// WebAssembly implementation of the UltraCanvasNativeDialogs statics
// (link-time selected, like every other OS/<Platform>/ directory).
//
// What the browser can and cannot do synchronously decides the mapping:
//
//   Message / question dialogs  -> window.alert() / window.confirm(). Both
//                                  block the calling (main) thread, so the
//                                  synchronous DialogResult contract holds.
//                                  confirm() has two buttons, so three-button
//                                  sets lose their third choice (see
//                                  ButtonsForConfirm below).
//   Text input                  -> window.prompt(). Passwords are NOT routed
//                                  there: prompt() echoes what is typed, so a
//                                  password request reports Cancel instead.
//   SaveContent()               -> a Blob download. The bytes are handed to
//                                  the browser's download manager under the
//                                  suggested file name; the user picks the
//                                  destination in the browser's own UI, and
//                                  the page never learns the path.
//   OpenFile / SaveFile /       -> Cancel. A browser file picker is
//   SelectFolder                   asynchronous and cannot return a path
//                                  before the function does. Applications
//                                  that want to import a user's file call
//                                  WASMBrowser::PickFilesAsync()
//                                  (UltraCanvasWASMSupport.h), which copies
//                                  the picked files into the virtual
//                                  filesystem and calls back with their paths.
//   ShowPrintDialog()           -> the text is opened in a new window as
//                                  preformatted text and window.print() runs
//                                  on it.
//
// All of it needs the DOM, i.e. the browser main thread, which is where the
// framework's UI code runs under this backend.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasDebug.h"

#include <emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace UltraCanvas {

    namespace {

        // Browser dialogs carry no title bar, so the title becomes the first
        // line of the message. Empty titles are simply left out.
        std::string Compose(const std::string& title, const std::string& message) {
            if (title.empty()) return message;
            return title + "\n\n" + message;
        }

        void BrowserAlert(const std::string& text) {
            EM_ASM({ window.alert(UTF8ToString($0)); }, text.c_str());
        }

        bool BrowserConfirm(const std::string& text) {
            return EM_ASM_INT({ return window.confirm(UTF8ToString($0)) ? 1 : 0; },
                              text.c_str()) != 0;
        }

        // Returns true and fills `value` when the user pressed OK; false when
        // the prompt was dismissed.
        bool BrowserPrompt(const std::string& text, const std::string& defaultValue,
                           std::string& value) {
            char* result = static_cast<char*>(EM_ASM_PTR({
                var r = window.prompt(UTF8ToString($0), UTF8ToString($1));
                if (r === null) return 0;
                return stringToNewUTF8(r);
            }, text.c_str(), defaultValue.c_str()));
            if (!result) return false;
            value = result;
            free(result);
            return true;
        }

        // How a DialogButtons set is expressed with confirm()'s OK / Cancel
        // pair, and what each answer means. Three-button sets cannot be
        // represented: the third choice folds into the dismissive one, which
        // is the answer a user gets by closing the dialog on the desktop too.
        struct ConfirmMapping {
            bool useConfirm = false;            // false: alert(), always `accept`
            DialogResult accept = DialogResult::OK;
            DialogResult dismiss = DialogResult::Cancel;
            const char* hint = nullptr;         // appended so the choice is legible
        };

        ConfirmMapping ButtonsForConfirm(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OKCancel:
                    return { true, DialogResult::OK, DialogResult::Cancel, nullptr };
                case DialogButtons::YesNo:
                    return { true, DialogResult::Yes, DialogResult::No,
                             "[OK = Yes, Cancel = No]" };
                case DialogButtons::YesNoCancel:
                    return { true, DialogResult::Yes, DialogResult::No,
                             "[OK = Yes, Cancel = No]" };
                case DialogButtons::RetryCancel:
                    return { true, DialogResult::Retry, DialogResult::Cancel,
                             "[OK = Retry, Cancel = Cancel]" };
                case DialogButtons::AbortRetryIgnore:
                    return { true, DialogResult::Retry, DialogResult::Abort,
                             "[OK = Retry, Cancel = Abort]" };
                case DialogButtons::NoButtons:
                case DialogButtons::OK:
                default:
                    return { false, DialogResult::OK, DialogResult::OK, nullptr };
            }
        }

        std::string SuggestedDownloadName(const FileDialogOptions& options) {
            if (!options.defaultFileName.empty()) return options.defaultFileName;
            // Take the first extension of the first filter so the download at
            // least carries the right type.
            for (const auto& filter : options.filters) {
                for (const auto& ext : filter.extensions) {
                    if (ext.empty() || ext == "*" || ext == "*.*") continue;
                    std::string e = ext;
                    if (e.rfind("*.", 0) == 0) e.erase(0, 2);
                    if (!e.empty() && e.front() == '.') e.erase(0, 1);
                    if (!e.empty()) return "download." + e;
                }
            }
            return "download";
        }

        std::string NotAvailable(const char* what) {
            debugOutput << "UltraCanvas WASM: native dialog '" << what
                        << "' cannot block for a browser file picker - returning "
                           "Cancel (use WASMBrowser::PickFilesAsync for imports)"
                        << std::endl;
            return {};
        }

    } // namespace

// ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string& message, const std::string& title,
            DialogType /*type*/, DialogButtons buttons, UltraCanvasWindowBase* /*parent*/) {
        const ConfirmMapping mapping = ButtonsForConfirm(buttons);
        std::string text = Compose(title, message);
        if (mapping.hint) {
            text += "\n\n";
            text += mapping.hint;
        }
        if (!mapping.useConfirm) {
            BrowserAlert(text);
            return mapping.accept;
        }
        return BrowserConfirm(text) ? mapping.accept : mapping.dismiss;
    }

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string& message, const std::string& title,
            DialogButtons buttons, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question, buttons, parent);
    }

// ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question,
                           DialogButtons::OKCancel, parent) == DialogResult::OK;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question,
                           DialogButtons::YesNo, parent) == DialogResult::Yes;
    }

// ===== FILE DIALOGS =====

    std::string UltraCanvasNativeDialogs::OpenFile(
            const std::string&, const std::vector<FileFilter>&,
            const std::string&, UltraCanvasWindowBase*) {
        return NotAvailable("OpenFile");
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const FileDialogOptions&) {
        return NotAvailable("OpenFile");
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string&, const std::vector<FileFilter>&,
            const std::string&, UltraCanvasWindowBase*) {
        NotAvailable("OpenMultipleFiles");
        return {};
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const FileDialogOptions&) {
        NotAvailable("OpenMultipleFiles");
        return {};
    }

    std::string UltraCanvasNativeDialogs::SaveFile(
            const std::string&, const std::vector<FileFilter>&,
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        return NotAvailable("SaveFile");
    }

    std::string UltraCanvasNativeDialogs::SaveFile(const FileDialogOptions&) {
        return NotAvailable("SaveFile");
    }

    // The desktop definition (ask for a path, write the bytes) is compiled out
    // for this platform in core/UltraCanvasFileLoader.cpp: a page cannot write
    // to the user's disk, but it can hand the bytes to the download manager,
    // which is the browser's own "save as".
    bool UltraCanvasNativeDialogs::SaveContent(const void* data, std::size_t size,
                                               const FileDialogOptions& options) {
        if (!data && size > 0) return false;
        const std::string name = SuggestedDownloadName(options);
        const int started = EM_ASM_INT({
            try {
                // Copy out of the (possibly shared) wasm heap: Blob rejects
                // views on a SharedArrayBuffer under -pthread.
                var bytes = new Uint8Array($2);
                if ($2 > 0) bytes.set(new Uint8Array(HEAPU8.buffer, $1, $2));
                var blob = new Blob([bytes], { type: 'application/octet-stream' });
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = UTF8ToString($0);
                a.style.display = 'none';
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
                return 1;
            } catch (e) {
                console.error('UltraCanvas WASM: download failed:', e);
                return 0;
            }
        }, name.c_str(), reinterpret_cast<uintptr_t>(data), static_cast<int>(size));
        return started != 0;
    }

    std::string UltraCanvasNativeDialogs::SelectFolder(
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        return NotAvailable("SelectFolder");
    }

// ===== INPUT DIALOGS =====

    NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
        NativeInputResult r;
        if (options.password) {
            // prompt() shows the typed characters; refusing is safer than
            // echoing a password on screen.
            debugOutput << "UltraCanvas WASM: password input has no masked browser "
                           "dialog - returning Cancel" << std::endl;
            return r;
        }
        std::string value;
        if (BrowserPrompt(Compose(options.title, options.prompt), options.defaultValue, value)) {
            r.result = DialogResult::OK;
            r.value = value;
        }
        return r;
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, UltraCanvasWindowBase* parent) {
        NativeInputDialogOptions options;
        options.title = title;
        options.prompt = prompt;
        options.defaultValue = defaultValue;
        options.parentWindow = parent;
        return InputText(options);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputPassword(
            const std::string& prompt, const std::string& title, UltraCanvasWindowBase* parent) {
        NativeInputDialogOptions options;
        options.title = title;
        options.prompt = prompt;
        options.password = true;
        options.parentWindow = parent;
        return InputText(options);
    }

// ===== CONVENIENCE FUNCTIONS =====

    std::string UltraCanvasNativeDialogs::GetInput(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputText(prompt, title, defaultValue, parent);
        return r.IsOK() ? r.value : std::string();
    }

    std::string UltraCanvasNativeDialogs::GetPassword(
            const std::string& prompt, const std::string& title, UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputPassword(prompt, title, parent);
        return r.IsOK() ? r.value : std::string();
    }

// ===== PRINTING =====

    bool UltraCanvasNativeDialogs::ShowPrintDialog(
            const std::string& documentName, const std::string& textContent,
            UltraCanvasWindowBase* /*parent*/) {
        const int opened = EM_ASM_INT({
            try {
                var w = window.open('', '_blank');
                if (!w) return 0;                  // popup blocked
                var doc = w.document;
                doc.open();
                doc.write('<!DOCTYPE html><html><head><meta charset="utf-8"><title></title>' +
                          '<style>body{margin:2em;font:12pt monospace;white-space:pre-wrap;}</style>' +
                          '</head><body></body></html>');
                doc.close();
                doc.title = UTF8ToString($0);
                doc.body.textContent = UTF8ToString($1);   // textContent: no HTML injection
                w.focus();
                w.print();
                return 1;
            } catch (e) {
                console.error('UltraCanvas WASM: print failed:', e);
                return 0;
            }
        }, documentName.c_str(), textContent.c_str());
        return opened != 0;
    }

} // namespace UltraCanvas

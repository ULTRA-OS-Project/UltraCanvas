// OS/MSWindows/UltraCanvasWindowsNativeDialogs.cpp
// Win32 implementation of native OS dialogs
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasWindowsApplication.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <iostream>

namespace UltraCanvas {

    namespace {

// ===== HELPER FUNCTIONS =====

        HWND ToHWND(NativeWindowHandle handle) {
            return static_cast<HWND>(handle);
        }

// Convert DialogType to MessageBox icon flags
        UINT ToMessageBoxIcon(DialogType type) {
            switch (type) {
                case DialogType::Information: return MB_ICONINFORMATION;
                case DialogType::Warning:     return MB_ICONWARNING;
                case DialogType::Error:       return MB_ICONERROR;
                case DialogType::Question:    return MB_ICONQUESTION;
                case DialogType::Custom:
                default:                      return 0;
            }
        }

// Convert DialogButtons to MessageBox button flags
        UINT ToMessageBoxButtons(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OK:               return MB_OK;
                case DialogButtons::OKCancel:         return MB_OKCANCEL;
                case DialogButtons::YesNo:            return MB_YESNO;
                case DialogButtons::YesNoCancel:      return MB_YESNOCANCEL;
                case DialogButtons::RetryCancel:      return MB_RETRYCANCEL;
                case DialogButtons::AbortRetryIgnore: return MB_ABORTRETRYIGNORE;
                default:                              return MB_OK;
            }
        }

// Convert MessageBox result to DialogResult
        DialogResult FromMessageBoxResult(int result) {
            switch (result) {
                case IDOK:     return DialogResult::OK;
                case IDCANCEL: return DialogResult::Cancel;
                case IDYES:    return DialogResult::Yes;
                case IDNO:     return DialogResult::No;
                case IDRETRY:  return DialogResult::Retry;
                case IDIGNORE: return DialogResult::Ignore;
                case IDABORT:  return DialogResult::Abort;
                default:       return DialogResult::NoResult;
            }
        }

// Build COMDLG_FILTERSPEC array from FileFilter vector
        struct FilterSpecData {
            std::vector<std::wstring> descriptions;
            std::vector<std::wstring> patterns;
            std::vector<COMDLG_FILTERSPEC> specs;

            void Build(const std::vector<FileFilter>& filters) {
                descriptions.reserve(filters.size());
                patterns.reserve(filters.size());
                specs.reserve(filters.size());

                for (const auto& filter : filters) {
                    descriptions.push_back(
                        UltraCanvasWindowsApplication::Utf8ToUtf16(filter.description));

                    // Build pattern string: "*.txt;*.log;*.md"
                    std::wstring pattern;
                    for (size_t i = 0; i < filter.extensions.size(); i++) {
                        if (i > 0) pattern += L";";
                        if (filter.extensions[i] == "*") {
                            pattern += L"*.*";
                        } else {
                            pattern += L"*." + UltraCanvasWindowsApplication::Utf8ToUtf16(
                                filter.extensions[i]);
                        }
                    }
                    patterns.push_back(pattern);
                }

                for (size_t i = 0; i < descriptions.size(); i++) {
                    COMDLG_FILTERSPEC spec;
                    spec.pszName = descriptions[i].c_str();
                    spec.pszSpec = patterns[i].c_str();
                    specs.push_back(spec);
                }
            }
        };

// Set initial directory on a file dialog via IShellItem
        void SetInitialDirectory(IFileDialog* dialog, const std::string& dir) {
            if (dir.empty()) return;

            std::wstring wdir = UltraCanvasWindowsApplication::Utf8ToUtf16(dir);
            IShellItem* psi = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(wdir.c_str(), nullptr,
                    IID_PPV_ARGS(&psi)))) {
                dialog->SetFolder(psi);
                psi->Release();
            }
        }

// Extract file path from IShellItem
        std::string GetPathFromShellItem(IShellItem* item) {
            PWSTR pszPath = nullptr;
            std::string result;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                result = UltraCanvasWindowsApplication::Utf16ToUtf8(pszPath);
                CoTaskMemFree(pszPath);
            }
            return result;
        }

    } // anonymous namespace

// ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string& message, const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string& message, const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string& message, const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string& message, const std::string& title,
            DialogButtons buttons, NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Question, buttons, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string& message, const std::string& title,
            DialogType type, DialogButtons buttons,
            NativeWindowHandle parent) {

        std::wstring wmessage = UltraCanvasWindowsApplication::Utf8ToUtf16(message);
        std::wstring wtitle = UltraCanvasWindowsApplication::Utf8ToUtf16(title);

        UINT flags = ToMessageBoxIcon(type) | ToMessageBoxButtons(buttons);
        int result = MessageBoxW(ToHWND(parent), wmessage.c_str(), wtitle.c_str(), flags);

        return FromMessageBoxResult(result);
    }

// ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string& message, const std::string& title,
            NativeWindowHandle parent) {
        DialogResult result = ShowMessage(message, title,
            DialogType::Question, DialogButtons::OKCancel, parent);
        return result == DialogResult::OK;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string& message, const std::string& title,
            NativeWindowHandle parent) {
        DialogResult result = ShowMessage(message, title,
            DialogType::Question, DialogButtons::YesNo, parent);
        return result == DialogResult::Yes;
    }

// ===== FILE DIALOGS =====

    std::string UltraCanvasNativeDialogs::OpenFile(
            const std::string& title, const std::vector<FileFilter>& filters,
            const std::string& initialDir, NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.parentWindow = parent;
        return OpenFile(options);
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const NativeFileDialogOptions& options) {
        IFileOpenDialog* pDialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&pDialog));
        if (FAILED(hr)) {
            std::cerr << "UltraCanvas NativeDialogs: Failed to create FileOpenDialog" << std::endl;
            return "";
        }

        // Set title
        if (!options.title.empty()) {
            std::wstring wtitle = UltraCanvasWindowsApplication::Utf8ToUtf16(options.title);
            pDialog->SetTitle(wtitle.c_str());
        }

        // Set filters
        FilterSpecData filterData;
        if (!options.filters.empty()) {
            filterData.Build(options.filters);
            pDialog->SetFileTypes(
                static_cast<UINT>(filterData.specs.size()), filterData.specs.data());
        }

        // Set initial directory
        SetInitialDirectory(pDialog, options.initialDirectory);

        // Show hidden files
        if (options.showHiddenFiles) {
            DWORD dwFlags = 0;
            pDialog->GetOptions(&dwFlags);
            pDialog->SetOptions(dwFlags | FOS_FORCESHOWHIDDEN);
        }

        // Show dialog
        hr = pDialog->Show(ToHWND(options.parentWindow));
        std::string result;

        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDialog->GetResult(&pItem))) {
                result = GetPathFromShellItem(pItem);
                pItem->Release();
            }
        }

        pDialog->Release();
        return result;
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string& title, const std::vector<FileFilter>& filters,
            const std::string& initialDir, NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.parentWindow = parent;
        options.allowMultiSelect = true;
        return OpenMultipleFiles(options);
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const NativeFileDialogOptions& options) {
        std::vector<std::string> results;

        IFileOpenDialog* pDialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&pDialog));
        if (FAILED(hr)) {
            std::cerr << "UltraCanvas NativeDialogs: Failed to create FileOpenDialog" << std::endl;
            return results;
        }

        // Enable multi-select
        DWORD dwFlags = 0;
        pDialog->GetOptions(&dwFlags);
        dwFlags |= FOS_ALLOWMULTISELECT;
        if (options.showHiddenFiles) {
            dwFlags |= FOS_FORCESHOWHIDDEN;
        }
        pDialog->SetOptions(dwFlags);

        // Set title
        if (!options.title.empty()) {
            std::wstring wtitle = UltraCanvasWindowsApplication::Utf8ToUtf16(options.title);
            pDialog->SetTitle(wtitle.c_str());
        }

        // Set filters
        FilterSpecData filterData;
        if (!options.filters.empty()) {
            filterData.Build(options.filters);
            pDialog->SetFileTypes(
                static_cast<UINT>(filterData.specs.size()), filterData.specs.data());
        }

        // Set initial directory
        SetInitialDirectory(pDialog, options.initialDirectory);

        // Show dialog
        hr = pDialog->Show(ToHWND(options.parentWindow));

        if (SUCCEEDED(hr)) {
            IShellItemArray* pItems = nullptr;
            if (SUCCEEDED(pDialog->GetResults(&pItems))) {
                DWORD count = 0;
                pItems->GetCount(&count);

                for (DWORD i = 0; i < count; i++) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
                        std::string path = GetPathFromShellItem(pItem);
                        if (!path.empty()) {
                            results.push_back(path);
                        }
                        pItem->Release();
                    }
                }
                pItems->Release();
            }
        }

        pDialog->Release();
        return results;
    }

    std::string UltraCanvasNativeDialogs::SaveFile(
            const std::string& title, const std::vector<FileFilter>& filters,
            const std::string& initialDir, const std::string& defaultFileName,
            NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.defaultFileName = defaultFileName;
        options.parentWindow = parent;
        return SaveFile(options);
    }

    std::string UltraCanvasNativeDialogs::SaveFile(const NativeFileDialogOptions& options) {
        IFileSaveDialog* pDialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&pDialog));
        if (FAILED(hr)) {
            std::cerr << "UltraCanvas NativeDialogs: Failed to create FileSaveDialog" << std::endl;
            return "";
        }

        // Set title
        if (!options.title.empty()) {
            std::wstring wtitle = UltraCanvasWindowsApplication::Utf8ToUtf16(options.title);
            pDialog->SetTitle(wtitle.c_str());
        }

        // Set filters
        FilterSpecData filterData;
        if (!options.filters.empty()) {
            filterData.Build(options.filters);
            pDialog->SetFileTypes(
                static_cast<UINT>(filterData.specs.size()), filterData.specs.data());
        }

        // Set default filename
        if (!options.defaultFileName.empty()) {
            std::wstring wname = UltraCanvasWindowsApplication::Utf8ToUtf16(options.defaultFileName);
            pDialog->SetFileName(wname.c_str());
        }

        // Set initial directory
        SetInitialDirectory(pDialog, options.initialDirectory);

        // Show hidden files
        if (options.showHiddenFiles) {
            DWORD dwFlags = 0;
            pDialog->GetOptions(&dwFlags);
            pDialog->SetOptions(dwFlags | FOS_FORCESHOWHIDDEN);
        }

        // Show dialog
        hr = pDialog->Show(ToHWND(options.parentWindow));
        std::string result;

        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDialog->GetResult(&pItem))) {
                result = GetPathFromShellItem(pItem);
                pItem->Release();
            }
        }

        pDialog->Release();
        return result;
    }

    std::string UltraCanvasNativeDialogs::SelectFolder(
            const std::string& title, const std::string& initialDir,
            NativeWindowHandle parent) {

        IFileOpenDialog* pDialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&pDialog));
        if (FAILED(hr)) {
            std::cerr << "UltraCanvas NativeDialogs: Failed to create FileOpenDialog" << std::endl;
            return "";
        }

        // Set folder-picking mode
        DWORD dwFlags = 0;
        pDialog->GetOptions(&dwFlags);
        pDialog->SetOptions(dwFlags | FOS_PICKFOLDERS);

        // Set title
        if (!title.empty()) {
            std::wstring wtitle = UltraCanvasWindowsApplication::Utf8ToUtf16(title);
            pDialog->SetTitle(wtitle.c_str());
        }

        // Set initial directory
        SetInitialDirectory(pDialog, initialDir);

        // Show dialog
        hr = pDialog->Show(ToHWND(parent));
        std::string result;

        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pDialog->GetResult(&pItem))) {
                result = GetPathFromShellItem(pItem);
                pItem->Release();
            }
        }

        pDialog->Release();
        return result;
    }

// ===== INPUT DIALOGS =====

    namespace {

// Dialog control IDs for input dialog
        const int IDC_INPUT_LABEL = 100;
        const int IDC_INPUT_EDIT  = 101;

// In-memory dialog template for input dialogs
// Creates a simple dialog with a label and an edit control
        struct InputDialogData {
            std::wstring title;
            std::wstring prompt;
            std::wstring defaultValue;
            std::wstring resultValue;
            bool password;
            bool accepted;
        };

        INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
            switch (msg) {
                case WM_INITDIALOG: {
                    auto* data = reinterpret_cast<InputDialogData*>(lParam);
                    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

                    // Set title
                    SetWindowTextW(hDlg, data->title.c_str());

                    // Set prompt label
                    SetDlgItemTextW(hDlg, IDC_INPUT_LABEL, data->prompt.c_str());

                    // Set default value
                    SetDlgItemTextW(hDlg, IDC_INPUT_EDIT, data->defaultValue.c_str());

                    // Set password mode
                    if (data->password) {
                        SendDlgItemMessageW(hDlg, IDC_INPUT_EDIT, EM_SETPASSWORDCHAR,
                            static_cast<WPARAM>(L'*'), 0);
                    }

                    // Select all text and focus
                    SendDlgItemMessageW(hDlg, IDC_INPUT_EDIT, EM_SETSEL, 0, -1);
                    SetFocus(GetDlgItem(hDlg, IDC_INPUT_EDIT));

                    // Center dialog on parent or screen
                    RECT rcDlg, rcOwner;
                    HWND hwndOwner = GetParent(hDlg);
                    if (!hwndOwner) hwndOwner = GetDesktopWindow();
                    GetWindowRect(hwndOwner, &rcOwner);
                    GetWindowRect(hDlg, &rcDlg);
                    int x = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
                    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
                    SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);

                    return FALSE; // We set focus manually
                }

                case WM_COMMAND: {
                    auto* data = reinterpret_cast<InputDialogData*>(
                        GetWindowLongPtrW(hDlg, GWLP_USERDATA));

                    switch (LOWORD(wParam)) {
                        case IDOK: {
                            // Get text from edit control
                            int len = GetWindowTextLengthW(GetDlgItem(hDlg, IDC_INPUT_EDIT));
                            std::wstring wtext(len + 1, 0);
                            GetDlgItemTextW(hDlg, IDC_INPUT_EDIT, &wtext[0], len + 1);
                            wtext.resize(len);

                            if (data) {
                                data->resultValue = wtext;
                                data->accepted = true;
                            }
                            EndDialog(hDlg, IDOK);
                            return TRUE;
                        }

                        case IDCANCEL:
                            if (data) {
                                data->accepted = false;
                            }
                            EndDialog(hDlg, IDCANCEL);
                            return TRUE;
                    }
                    break;
                }

                case WM_CLOSE:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }

            return FALSE;
        }

// Build an in-memory DLGTEMPLATE for the input dialog
// Layout: prompt label at top, edit control below, OK/Cancel buttons at bottom
        std::vector<uint8_t> BuildInputDialogTemplate() {
            // Dialog dimensions (in dialog units)
            const int DLG_WIDTH = 280;
            const int DLG_HEIGHT = 90;
            const int MARGIN = 7;
            const int LABEL_HEIGHT = 14;
            const int EDIT_HEIGHT = 14;
            const int BUTTON_WIDTH = 50;
            const int BUTTON_HEIGHT = 14;

            // Estimate buffer size and reserve
            std::vector<uint8_t> buffer;
            buffer.reserve(1024);

            auto Align4 = [&buffer]() {
                while (buffer.size() % 4 != 0) buffer.push_back(0);
            };

            auto PushWord = [&buffer](WORD w) {
                buffer.push_back(static_cast<uint8_t>(w & 0xFF));
                buffer.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
            };

            auto PushDWord = [&buffer](DWORD d) {
                buffer.push_back(static_cast<uint8_t>(d & 0xFF));
                buffer.push_back(static_cast<uint8_t>((d >> 8) & 0xFF));
                buffer.push_back(static_cast<uint8_t>((d >> 16) & 0xFF));
                buffer.push_back(static_cast<uint8_t>((d >> 24) & 0xFF));
            };

            auto PushString = [&buffer](const wchar_t* str) {
                do {
                    buffer.push_back(static_cast<uint8_t>(*str & 0xFF));
                    buffer.push_back(static_cast<uint8_t>((*str >> 8) & 0xFF));
                } while (*str++);
            };

            // === DLGTEMPLATE ===
            DWORD style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
            PushDWord(style);        // style
            PushDWord(0);            // dwExtendedStyle
            PushWord(4);             // cdit (4 controls: label, edit, OK, Cancel)
            PushWord(0);             // x
            PushWord(0);             // y
            PushWord(DLG_WIDTH);     // cx
            PushWord(DLG_HEIGHT);    // cy
            PushString(L"");         // menu
            PushString(L"");         // class
            PushString(L"Input");    // title (overridden in WM_INITDIALOG)
            PushWord(8);             // font size
            PushString(L"MS Shell Dlg 2");  // font name

            // === Label (STATIC) ===
            Align4();
            PushDWord(WS_CHILD | WS_VISIBLE | SS_LEFT);  // style
            PushDWord(0);                                  // dwExtendedStyle
            PushWord(MARGIN);                              // x
            PushWord(MARGIN);                              // y
            PushWord(DLG_WIDTH - 2 * MARGIN);              // cx
            PushWord(LABEL_HEIGHT);                        // cy
            PushWord(IDC_INPUT_LABEL);                     // id
            PushWord(0xFFFF); PushWord(0x0082);            // class: STATIC
            PushString(L"");                               // text (set in WM_INITDIALOG)
            PushWord(0);                                   // extra

            // === Edit Control ===
            Align4();
            DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
            PushDWord(editStyle);                          // style
            PushDWord(0);                                  // dwExtendedStyle
            PushWord(MARGIN);                              // x
            PushWord(MARGIN + LABEL_HEIGHT + 4);           // y
            PushWord(DLG_WIDTH - 2 * MARGIN);              // cx
            PushWord(EDIT_HEIGHT);                         // cy
            PushWord(IDC_INPUT_EDIT);                      // id
            PushWord(0xFFFF); PushWord(0x0081);            // class: EDIT
            PushString(L"");                               // text
            PushWord(0);                                   // extra

            // === OK Button ===
            int btnY = DLG_HEIGHT - MARGIN - BUTTON_HEIGHT;
            int okX = DLG_WIDTH - 2 * BUTTON_WIDTH - MARGIN - 4;
            int cancelX = DLG_WIDTH - BUTTON_WIDTH - MARGIN;

            Align4();
            PushDWord(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON);  // style
            PushDWord(0);                                  // dwExtendedStyle
            PushWord(okX);                                 // x
            PushWord(btnY);                                // y
            PushWord(BUTTON_WIDTH);                        // cx
            PushWord(BUTTON_HEIGHT);                       // cy
            PushWord(IDOK);                                // id
            PushWord(0xFFFF); PushWord(0x0080);            // class: BUTTON
            PushString(L"OK");                             // text
            PushWord(0);                                   // extra

            // === Cancel Button ===
            Align4();
            PushDWord(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON);  // style
            PushDWord(0);                                  // dwExtendedStyle
            PushWord(cancelX);                             // x
            PushWord(btnY);                                // y
            PushWord(BUTTON_WIDTH);                        // cx
            PushWord(BUTTON_HEIGHT);                       // cy
            PushWord(IDCANCEL);                            // id
            PushWord(0xFFFF); PushWord(0x0080);            // class: BUTTON
            PushString(L"Cancel");                         // text
            PushWord(0);                                   // extra

            return buffer;
        }

        NativeInputResult ShowInputDialogImpl(
                const std::string& prompt, const std::string& title,
                const std::string& defaultValue, bool password,
                NativeWindowHandle parent) {

            InputDialogData data;
            data.title = UltraCanvasWindowsApplication::Utf8ToUtf16(title);
            data.prompt = UltraCanvasWindowsApplication::Utf8ToUtf16(prompt);
            data.defaultValue = UltraCanvasWindowsApplication::Utf8ToUtf16(defaultValue);
            data.password = password;
            data.accepted = false;

            auto templateData = BuildInputDialogTemplate();

            INT_PTR result = DialogBoxIndirectParamW(
                GetModuleHandle(nullptr),
                reinterpret_cast<LPCDLGTEMPLATEW>(templateData.data()),
                ToHWND(parent),
                InputDialogProc,
                reinterpret_cast<LPARAM>(&data));

            NativeInputResult inputResult;
            if (result == IDOK && data.accepted) {
                inputResult.result = DialogResult::OK;
                inputResult.value = UltraCanvasWindowsApplication::Utf16ToUtf8(data.resultValue);
            } else {
                inputResult.result = DialogResult::Cancel;
            }
            return inputResult;
        }

    } // anonymous namespace

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, NativeWindowHandle parent) {
        return ShowInputDialogImpl(prompt, title, defaultValue, false, parent);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const NativeInputDialogOptions& options) {
        return ShowInputDialogImpl(options.prompt, options.title,
            options.defaultValue, options.password, options.parentWindow);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputPassword(
            const std::string& prompt, const std::string& title,
            NativeWindowHandle parent) {
        return ShowInputDialogImpl(prompt, title, "", true, parent);
    }

// ===== CONVENIENCE FUNCTIONS =====

    std::string UltraCanvasNativeDialogs::GetInput(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, NativeWindowHandle parent) {
        auto result = InputText(prompt, title, defaultValue, parent);
        return result.IsOK() ? result.value : "";
    }

    std::string UltraCanvasNativeDialogs::GetPassword(
            const std::string& prompt, const std::string& title,
            NativeWindowHandle parent) {
        auto result = InputPassword(prompt, title, parent);
        return result.IsOK() ? result.value : "";
    }

} // namespace UltraCanvas

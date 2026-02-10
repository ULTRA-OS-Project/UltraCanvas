// OS/MSWindows/UltraCanvasNativeDialogsWindows.cpp
// Windows implementation of native OS dialogs using Win32 API
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 2.0.0
// Last Modified: 2026-01-25
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"

#if defined(_WIN32) || defined(_WIN64)

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
#include <string>
#include <vector>
#include <algorithm>
#include <codecvt>
#include <locale>

namespace UltraCanvas {

namespace {

// ===== STRING CONVERSION HELPERS =====

std::wstring ToWideString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

std::string ToNarrowString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

// Convert DialogType to MessageBox icon
UINT ToMessageBoxIcon(DialogType type) {
    switch (type) {
        case DialogType::Information: return MB_ICONINFORMATION;
        case DialogType::Warning:     return MB_ICONWARNING;
        case DialogType::Error:       return MB_ICONERROR;
        case DialogType::Question:    return MB_ICONQUESTION;
        case DialogType::Custom:
        default:                      return MB_ICONINFORMATION;
    }
}

// Convert DialogButtons to MessageBox buttons
UINT ToMessageBoxButtons(DialogButtons buttons) {
    switch (buttons) {
        case DialogButtons::OK:              return MB_OK;
        case DialogButtons::OKCancel:        return MB_OKCANCEL;
        case DialogButtons::YesNo:           return MB_YESNO;
        case DialogButtons::YesNoCancel:     return MB_YESNOCANCEL;
        case DialogButtons::RetryCancel:     return MB_RETRYCANCEL;
        case DialogButtons::AbortRetryIgnore: return MB_ABORTRETRYIGNORE;
        default:                             return MB_OK;
    }
}

// Convert MessageBox result to DialogResult
DialogResult FromMessageBoxResult(int result) {
    switch (result) {
        case IDOK:     return DialogResult::OK;
        case IDCANCEL: return DialogResult::Cancel;
        case IDYES:    return DialogResult::Yes;
        case IDNO:     return DialogResult::No;
        case IDABORT:  return DialogResult::Abort;
        case IDRETRY:  return DialogResult::Retry;
        case IDIGNORE: return DialogResult::Ignore;
        case IDCLOSE:  return DialogResult::Close;
        default:       return DialogResult::Cancel;
    }
}

// Build filter string for file dialogs using FileFilter
// Format: "Description\0*.ext1;*.ext2\0Description2\0*.ext3\0\0"
std::wstring BuildFilterString(const std::vector<FileFilter>& filters) {
    std::wstring filterStr;

    for (const auto& filter : filters) {
        filterStr += ToWideString(filter.description);
        filterStr += L'\0';

        // Build extension pattern
        std::wstring extPattern;
        for (size_t i = 0; i < filter.extensions.size(); ++i) {
            if (i > 0) extPattern += L";";
            if (filter.extensions[i] == "*") {
                extPattern += L"*.*";
            } else {
                extPattern += L"*." + ToWideString(filter.extensions[i]);
            }
        }
        filterStr += extPattern;
        filterStr += L'\0';
    }

    // Add "All Files" if no filters specified
    if (filters.empty()) {
        filterStr += L"All Files\0*.*\0";
    }

    // Double null terminator
    filterStr += L'\0';

    return filterStr;
}

// COM initialization helper
class ComInitializer {
public:
    ComInitializer() : initialized(false) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        initialized = SUCCEEDED(hr) || hr == S_FALSE; // S_FALSE = already initialized
    }

    ~ComInitializer() {
        if (initialized) {
            CoUninitialize();
        }
    }

    bool IsInitialized() const { return initialized; }

private:
    bool initialized;
};

} // anonymous namespace

// ===== MESSAGE DIALOGS =====

DialogResult UltraCanvasNativeDialogs::ShowInfo(
    const std::string& message,
    const std::string& title,
    NativeWindowHandle parent) {
    return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowWarning(
    const std::string& message,
    const std::string& title,
    NativeWindowHandle parent) {
    return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowError(
    const std::string& message,
    const std::string& title,
    NativeWindowHandle parent) {
    return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowQuestion(
    const std::string& message,
    const std::string& title,
    DialogButtons buttons,
    NativeWindowHandle parent) {
    return ShowMessage(message, title, DialogType::Question, buttons, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowMessage(
    const std::string& message,
    const std::string& title,
    DialogType type,
    DialogButtons buttons,
    NativeWindowHandle parent) {

    std::wstring wMessage = ToWideString(message);
    std::wstring wTitle = ToWideString(title);

    UINT uType = ToMessageBoxButtons(buttons) | ToMessageBoxIcon(type) | MB_SETFOREGROUND;

    // If parent is provided, add MB_APPLMODAL to keep dialog on top of parent
    if (parent != nullptr) {
        uType |= MB_APPLMODAL;
    } else {
        uType |= MB_TASKMODAL;  // System modal if no parent
    }

    HWND hwndParent = static_cast<HWND>(parent);
    int result = MessageBoxW(hwndParent, wMessage.c_str(), wTitle.c_str(), uType);

    return FromMessageBoxResult(result);
}

// ===== CONFIRMATION DIALOGS =====

bool UltraCanvasNativeDialogs::Confirm(
    const std::string& message,
    const std::string& title,
    NativeWindowHandle parent) {
    DialogResult result = ShowMessage(message, title,
        DialogType::Question, DialogButtons::OKCancel, parent);
    return result == DialogResult::OK;
}

bool UltraCanvasNativeDialogs::ConfirmYesNo(
    const std::string& message,
    const std::string& title,
    NativeWindowHandle parent) {
    DialogResult result = ShowMessage(message, title,
        DialogType::Question, DialogButtons::YesNo, parent);
    return result == DialogResult::Yes;
}

// ===== FILE DIALOGS =====

std::string UltraCanvasNativeDialogs::OpenFile(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    NativeWindowHandle parent) {

    NativeFileDialogOptions options;
    options.title = title;
    options.filters = filters;
    options.initialDirectory = initialDir;
    options.parentWindow = parent;
    return OpenFile(options);
}

std::string UltraCanvasNativeDialogs::OpenFile(const NativeFileDialogOptions& options) {
    std::wstring filterStr = BuildFilterString(options.filters);
    std::wstring initialDir = ToWideString(options.initialDirectory);
    std::wstring titleStr = ToWideString(options.title.empty() ? "Open File" : options.title);

    wchar_t filename[MAX_PATH] = {0};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(options.parentWindow);  // Parent window handle
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = titleStr.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!initialDir.empty()) {
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (options.showHiddenFiles) {
        ofn.Flags |= OFN_FORCESHOWHIDDEN;
    }

    if (GetOpenFileNameW(&ofn)) {
        return ToNarrowString(filename);
    }

    return "";
}

std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    NativeWindowHandle parent) {

    NativeFileDialogOptions options;
    options.title = title;
    options.filters = filters;
    options.initialDirectory = initialDir;
    options.allowMultiSelect = true;
    options.parentWindow = parent;
    return OpenMultipleFiles(options);
}

std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const NativeFileDialogOptions& options) {
    std::wstring filterStr = BuildFilterString(options.filters);
    std::wstring initialDir = ToWideString(options.initialDirectory);
    std::wstring titleStr = ToWideString(options.title.empty() ? "Open Files" : options.title);

    // Use larger buffer for multiple files
    const int bufferSize = 32768;
    std::vector<wchar_t> buffer(bufferSize, 0);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(options.parentWindow);  // Parent window handle
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = bufferSize;
    ofn.lpstrTitle = titleStr.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_EXPLORER | OFN_ALLOWMULTISELECT;

    if (!initialDir.empty()) {
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (options.showHiddenFiles) {
        ofn.Flags |= OFN_FORCESHOWHIDDEN;
    }

    std::vector<std::string> results;

    if (GetOpenFileNameW(&ofn)) {
        // Parse the result - could be single file or directory + multiple files
        const wchar_t* ptr = buffer.data();
        std::wstring firstPart = ptr;
        ptr += firstPart.length() + 1;

        if (*ptr == L'\0') {
            // Single file selected
            results.push_back(ToNarrowString(firstPart));
        } else {
            // Multiple files: firstPart is directory, subsequent are filenames
            std::wstring directory = firstPart;
            while (*ptr != L'\0') {
                std::wstring filename = ptr;
                std::wstring fullPath = directory + L"\\" + filename;
                results.push_back(ToNarrowString(fullPath));
                ptr += filename.length() + 1;
            }
        }
    }

    return results;
}

std::string UltraCanvasNativeDialogs::SaveFile(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    const std::string& defaultFileName,
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
    std::wstring filterStr = BuildFilterString(options.filters);
    std::wstring initialDir = ToWideString(options.initialDirectory);
    std::wstring titleStr = ToWideString(options.title.empty() ? "Save File" : options.title);

    wchar_t filename[MAX_PATH] = {0};

    // Set default filename if provided
    if (!options.defaultFileName.empty()) {
        std::wstring defaultName = ToWideString(options.defaultFileName);
        wcscpy_s(filename, MAX_PATH, defaultName.c_str());
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(options.parentWindow);  // Parent window handle
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = titleStr.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!initialDir.empty()) {
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (options.showHiddenFiles) {
        ofn.Flags |= OFN_FORCESHOWHIDDEN;
    }

    if (GetSaveFileNameW(&ofn)) {
        return ToNarrowString(filename);
    }

    return "";
}

std::string UltraCanvasNativeDialogs::SelectFolder(
    const std::string& title,
    const std::string& initialDir,
    NativeWindowHandle parent) {

    ComInitializer com;
    if (!com.IsInitialized()) {
        return "";
    }

    IFileDialog* pFileDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileDialog, reinterpret_cast<void**>(&pFileDialog));

    if (FAILED(hr) || !pFileDialog) {
        return "";
    }

    // Set options for folder picker
    DWORD options;
    pFileDialog->GetOptions(&options);
    pFileDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    // Set title
    if (!title.empty()) {
        std::wstring wTitle = ToWideString(title);
        pFileDialog->SetTitle(wTitle.c_str());
    }

    // Set initial directory
    if (!initialDir.empty()) {
        std::wstring wInitialDir = ToWideString(initialDir);
        IShellItem* pFolder = nullptr;
        hr = SHCreateItemFromParsingName(wInitialDir.c_str(), nullptr, IID_IShellItem,
                                          reinterpret_cast<void**>(&pFolder));
        if (SUCCEEDED(hr) && pFolder) {
            pFileDialog->SetFolder(pFolder);
            pFolder->Release();
        }
    }

    std::string result;

    // Show dialog with parent window handle
    HWND hwndParent = static_cast<HWND>(parent);
    hr = pFileDialog->Show(hwndParent);
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pFileDialog->GetResult(&pItem);
        if (SUCCEEDED(hr) && pItem) {
            PWSTR pszFilePath = nullptr;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
            if (SUCCEEDED(hr) && pszFilePath) {
                result = ToNarrowString(pszFilePath);
                CoTaskMemFree(pszFilePath);
            }
            pItem->Release();
        }
    }

    pFileDialog->Release();

    return result;
}

// ===== INPUT DIALOGS =====

namespace {

// Dialog template for input dialog
struct InputDialogData {
    std::wstring title;
    std::wstring prompt;
    std::wstring defaultValue;
    std::wstring result;
    bool password;
    bool okPressed;
};

// Input dialog procedure
INT_PTR CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InputDialogData* data = reinterpret_cast<InputDialogData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            data = reinterpret_cast<InputDialogData*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);

            // Set title
            SetWindowTextW(hwnd, data->title.c_str());

            // Set prompt
            SetDlgItemTextW(hwnd, 101, data->prompt.c_str());

            // Set default value and password mode
            HWND hEdit = GetDlgItem(hwnd, 102);
            if (data->password) {
                SendMessageW(hEdit, EM_SETPASSWORDCHAR, L'*', 0);
            }
            SetWindowTextW(hEdit, data->defaultValue.c_str());

            // Center dialog
            RECT rcDlg, rcOwner;
            GetWindowRect(hwnd, &rcDlg);
            GetWindowRect(GetDesktopWindow(), &rcOwner);
            int x = (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
            int y = (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
            SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);

            SetFocus(hEdit);
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t buffer[4096];
                    GetDlgItemTextW(hwnd, 102, buffer, 4096);
                    data->result = buffer;
                    data->okPressed = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    data->okPressed = false;
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            data->okPressed = false;
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }

    return FALSE;
}

// Create dialog template in memory
std::vector<BYTE> CreateInputDialogTemplate() {
    std::vector<BYTE> buffer;

    // Helper to add data to buffer
    auto AddWord = [&buffer](WORD w) {
        buffer.push_back(static_cast<BYTE>(w & 0xFF));
        buffer.push_back(static_cast<BYTE>((w >> 8) & 0xFF));
    };

    auto AddDWord = [&buffer](DWORD dw) {
        buffer.push_back(static_cast<BYTE>(dw & 0xFF));
        buffer.push_back(static_cast<BYTE>((dw >> 8) & 0xFF));
        buffer.push_back(static_cast<BYTE>((dw >> 16) & 0xFF));
        buffer.push_back(static_cast<BYTE>((dw >> 24) & 0xFF));
    };

    auto AddString = [&buffer](const wchar_t* str) {
        while (*str) {
            buffer.push_back(static_cast<BYTE>(*str & 0xFF));
            buffer.push_back(static_cast<BYTE>((*str >> 8) & 0xFF));
            str++;
        }
        buffer.push_back(0);
        buffer.push_back(0);
    };

    auto Align = [&buffer]() {
        while (buffer.size() % 4 != 0) {
            buffer.push_back(0);
        }
    };

    // DLGTEMPLATE
    DWORD style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    AddDWord(style);      // style
    AddDWord(0);          // extended style
    AddWord(4);           // number of items
    AddWord(0);           // x
    AddWord(0);           // y
    AddWord(200);         // width
    AddWord(70);          // height
    AddWord(0);           // menu
    AddWord(0);           // class
    AddString(L"Input"); // title
    AddWord(9);           // font size
    AddString(L"Segoe UI"); // font name

    // Static text (prompt) - ID 101
    Align();
    AddDWord(WS_CHILD | WS_VISIBLE | SS_LEFT);  // style
    AddDWord(0);          // extended style
    AddWord(10);          // x
    AddWord(10);          // y
    AddWord(180);         // width
    AddWord(14);          // height
    AddWord(101);         // ID
    AddWord(0xFFFF);      // class (predefined)
    AddWord(0x0082);      // STATIC
    AddString(L"");       // text
    AddWord(0);           // creation data

    // Edit control - ID 102
    Align();
    AddDWord(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);  // style
    AddDWord(0);          // extended style
    AddWord(10);          // x
    AddWord(26);          // y
    AddWord(180);         // width
    AddWord(14);          // height
    AddWord(102);         // ID
    AddWord(0xFFFF);      // class (predefined)
    AddWord(0x0081);      // EDIT
    AddString(L"");       // text
    AddWord(0);           // creation data

    // OK button - IDOK
    Align();
    AddDWord(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON);  // style
    AddDWord(0);          // extended style
    AddWord(50);          // x
    AddWord(48);          // y
    AddWord(45);          // width
    AddWord(14);          // height
    AddWord(IDOK);        // ID
    AddWord(0xFFFF);      // class (predefined)
    AddWord(0x0080);      // BUTTON
    AddString(L"OK");     // text
    AddWord(0);           // creation data

    // Cancel button - IDCANCEL
    Align();
    AddDWord(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON);  // style
    AddDWord(0);          // extended style
    AddWord(105);         // x
    AddWord(48);          // y
    AddWord(45);          // width
    AddWord(14);          // height
    AddWord(IDCANCEL);    // ID
    AddWord(0xFFFF);      // class (predefined)
    AddWord(0x0080);      // BUTTON
    AddString(L"Cancel"); // text
    AddWord(0);           // creation data

    return buffer;
}

} // anonymous namespace

NativeInputResult UltraCanvasNativeDialogs::InputText(
    const std::string& prompt,
    const std::string& title,
    const std::string& defaultValue) {

    NativeInputDialogOptions options;
    options.prompt = prompt;
    options.title = title;
    options.defaultValue = defaultValue;
    return InputText(options);
}

NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
    InputDialogData data;
    data.title = ToWideString(options.title);
    data.prompt = ToWideString(options.prompt);
    data.defaultValue = ToWideString(options.defaultValue);
    data.password = options.password;
    data.okPressed = false;

    std::vector<BYTE> dialogTemplate = CreateInputDialogTemplate();

    DialogBoxIndirectParamW(
        GetModuleHandle(nullptr),
        reinterpret_cast<LPCDLGTEMPLATEW>(dialogTemplate.data()),
        nullptr,
        InputDialogProc,
        reinterpret_cast<LPARAM>(&data)
    );

    NativeInputResult result;
    result.result = data.okPressed ? DialogResult::OK : DialogResult::Cancel;
    result.value = ToNarrowString(data.result);

    return result;
}

NativeInputResult UltraCanvasNativeDialogs::InputPassword(
    const std::string& prompt,
    const std::string& title) {

    NativeInputDialogOptions options;
    options.prompt = prompt;
    options.title = title;
    options.password = true;
    return InputText(options);
}

// ===== CONVENIENCE FUNCTIONS =====

std::string UltraCanvasNativeDialogs::GetInput(
    const std::string& prompt,
    const std::string& title,
    const std::string& defaultValue) {

    NativeInputResult result = InputText(prompt, title, defaultValue);
    return result.IsOK() ? result.value : "";
}

} // namespace UltraCanvas

#endif // _WIN32 || _WIN64
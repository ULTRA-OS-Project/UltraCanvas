
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "UltraCanvasTextEditor.h"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace UltraCanvas {
// Helper: format file size into human-readable string
std::string UltraCanvasTextEditor::FormatFileSize(uintmax_t bytes) {
    if (bytes == 0) return "0 B";

    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream oss;
    if (unitIndex == 0) {
        oss << bytes << " B";
    } else {
        oss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
    }

// Append exact byte count for sizes > 1 KB
    if (unitIndex > 0) {
        oss << "  (" << bytes << " bytes)";
    }

    return oss.str();
}

// Helper: format time_point to readable string
static std::string FormatFileTime(const std::filesystem::file_time_type &ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm *tm = std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d  %H:%M:%S");
    return oss.str();
}

void UltraCanvasTextEditor::OnInfoFileStatistics() {
    if (fileStatsDialog) return;

    auto doc = GetActiveDocument();
    if (!doc) return;

    // ===== GATHER DATA =====
    std::string fileName = doc->fileName;
    std::string filePath = doc->filePath;
    std::string fileLocation = "";
    std::string fileSizeStr = "—";
    std::string charset = doc->encoding.empty() ? "UTF-8" : doc->encoding;
    std::string createdDateStr = "—";
    std::string modifiedDateStr = "—";
    std::string statusStr = doc->isModified ? "Unsaved modifications" : "Saved";

    // File system info (only available for saved files)
    if (!filePath.empty() && std::filesystem::exists(filePath)) {
        std::filesystem::path p(filePath);

        // File location (directory)
        fileLocation = p.parent_path().string();

        // File size
        std::error_code ec;
        uintmax_t fileSize = std::filesystem::file_size(filePath, ec);
        if (!ec) {
            fileSizeStr = FormatFileSize(fileSize);
        }

        // Last modified time from filesystem
        auto lastWriteTime = std::filesystem::last_write_time(filePath, ec);
        if (!ec) {
            modifiedDateStr = FormatFileTime(lastWriteTime);
        }

        // Created date: use stat() for birth time on supported platforms
        // std::filesystem doesn't provide creation time directly.
        // On Linux, stat.st_ctime is metadata change time (not creation).
        // We try statx() if available, otherwise show last modified as fallback.
#if defined(__linux__)
        struct statx stx;
        if (statx(AT_FDCWD, filePath.c_str(), 0, STATX_BTIME, &stx) == 0 &&
            (stx.stx_mask & STATX_BTIME)) {
            std::time_t btime = stx.stx_btime.tv_sec;
            std::tm *tm = std::localtime(&btime);
            std::ostringstream oss;
            oss << std::put_time(tm, "%Y-%m-%d  %H:%M:%S");
            createdDateStr = oss.str();
        } else {
            createdDateStr = "Not available";
        }
#elif defined(_WIN32)
        // On Windows, creation time is available via GetFileAttributesEx
            // but std::filesystem doesn't expose it. Use Win32 API:
            WIN32_FILE_ATTRIBUTE_DATA fad;
            std::wstring wpath(filePath.begin(), filePath.end());
            if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &fad)) {
                FILETIME ft = fad.ftCreationTime;
                SYSTEMTIME st;
                FileTimeToSystemTime(&ft, &st);
                std::ostringstream oss;
                oss << st.wYear << "-"
                    << std::setw(2) << std::setfill('0') << st.wMonth << "-"
                    << std::setw(2) << std::setfill('0') << st.wDay << "  "
                    << std::setw(2) << std::setfill('0') << st.wHour << ":"
                    << std::setw(2) << std::setfill('0') << st.wMinute << ":"
                    << std::setw(2) << std::setfill('0') << st.wSecond;
                createdDateStr = oss.str();
            } else {
                createdDateStr = "Not available";
            }
#elif defined(__APPLE__)
            struct stat st;
            if (stat(filePath.c_str(), &st) == 0) {
                std::time_t btime = st.st_birthtime;
                std::tm* tm = std::localtime(&btime);
                std::ostringstream oss;
                oss << std::put_time(tm, "%Y-%m-%d  %H:%M:%S");
                createdDateStr = oss.str();
            } else {
                createdDateStr = "Not available";
            }
#else
            createdDateStr = "Not available";
#endif
    } else {
        fileLocation = "(not saved)";
        createdDateStr = "—";
        modifiedDateStr = "—";
    }

    // Word count and letter count from text content
    std::string text = doc->textArea ? doc->textArea->GetText() : "";
    int letterCount = 0;
    int wordCount = 0;
    bool inWord = false;

    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Count UTF-8 characters (not raw bytes)
        int charBytes = 1;
        if (c >= 0xF0) charBytes = 4;
        else if (c >= 0xE0) charBytes = 3;
        else if (c >= 0xC0) charBytes = 2;

        // Skip newline/carriage return for letter count
        if (c != '\n' && c != '\r') {
            letterCount++;
        }

        // Word counting: split on whitespace
        bool isWhitespace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!isWhitespace && !inWord) {
            wordCount++;
            inWord = true;
        } else if (isWhitespace) {
            inWord = false;
        }

        i += charBytes;
    }

    std::string wordCountStr = std::to_string(wordCount);
    std::string letterCountStr = std::to_string(letterCount);

    // ===== BUILD DIALOG =====
    DialogConfig dlgConfig;
    dlgConfig.title = "File Statistics";
    dlgConfig.dialogType = DialogType::Custom;
    dlgConfig.buttons = DialogButtons::NoButtons;
    dlgConfig.width = 480;
    dlgConfig.height = 380;

    fileStatsDialog = UltraCanvasDialogManager::CreateDialog(dlgConfig);

    auto mainLayout = CreateVBoxLayout(fileStatsDialog.get());
    mainLayout->SetSpacing(6);
    fileStatsDialog->SetPadding(20);

    // Title
    auto titleLabel = std::make_shared<UltraCanvasLabel>("StatsTitle", 500, 22, "File Statistics");
    titleLabel->SetFontSize(16);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetAlignment(TextAlignment::Left);
    titleLabel->SetMargin(0, 0, 12, 0);
    mainLayout->AddUIElement(titleLabel)->SetWidthMode(SizeMode::Fill);

    // Grid container for label:value pairs
    auto gridContainer = std::make_shared<UltraCanvasContainer>(
            "StatsGrid", 501, 0, 0, 440, 260);

    auto grid = CreateGridLayout(gridContainer.get(), 9, 2);
    grid->SetSpacing(6);
    grid->SetColumnDefinition(0, GridRowColumnDefinition::Fixed(120));
    grid->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));

    // Helper lambda to add a row
    int row = 0;
    int labelId = 510;

    auto addRow = [&](const std::string &labelText, const std::string &valueText) {
        auto label = std::make_shared<UltraCanvasLabel>(
                "StatLabel" + std::to_string(row), labelId++, 0, 0, 120, 20);
        label->SetText(labelText);
        label->SetFontSize(11);
        label->SetFontWeight(FontWeight::Bold);
        label->SetTextColor(Color(60, 60, 60));
        label->SetAlignment(TextAlignment::Right);
        label->SetMargin(0, 8, 0, 0);

        auto value = std::make_shared<UltraCanvasLabel>(
                "StatValue" + std::to_string(row), labelId++, 0, 0, 300, 20);
        value->SetText(valueText);
        value->SetFontSize(11);
        value->SetTextColor(Color(30, 30, 30));
        value->SetAlignment(TextAlignment::Left);
        value->SetWrap(TextWrap::WrapWord);
        value->SetAutoResize(true);

        grid->AddUIElement(label, row, 0);
        grid->AddUIElement(value, row, 1);
        row++;
    };

    addRow("File Name:", fileName);
    addRow("Location:", fileLocation);
    addRow("File Size:", fileSizeStr);
    addRow("Charset:", charset);
    addRow("Words:", wordCountStr);
    addRow("Characters:", letterCountStr);
    addRow("Created:", createdDateStr);
    addRow("Modified:", modifiedDateStr);
    addRow("Status:", statusStr);

    // Color the status value based on saved/unsaved
    // The status value is the last label added (labelId - 1)
    // Access it via grid children - the 18th child (index 17) is the status value
    if (doc->isModified) {
        // Find the status value label and color it orange
        auto statusValue = std::dynamic_pointer_cast<UltraCanvasLabel>(
                gridContainer->GetChildren()[gridContainer->GetChildCount() - 1]);
        if (statusValue) {
            statusValue->SetTextColor(Color(200, 120, 0));
            statusValue->SetFontWeight(FontWeight::Bold);
        }
    }

    mainLayout->AddUIElement(gridContainer)->SetWidthMode(SizeMode::Fill);

    // Push OK button to the bottom
    mainLayout->AddStretch(1);

    // OK button
    auto okButton = std::make_shared<UltraCanvasButton>("StatsOK", 550, 0, 0, 80, 28);
    okButton->SetText("OK");
    okButton->onClick = [this]() {
        fileStatsDialog->CloseDialog(DialogResult::OK);
    };
    mainLayout->AddUIElement(okButton)->SetCrossAlignment(LayoutAlignment::Center);

    fileStatsDialog->onResult = [this](DialogResult) {
        fileStatsDialog.reset();
    };

    UltraCanvasDialogManager::ShowDialog(fileStatsDialog, nullptr);
}
}
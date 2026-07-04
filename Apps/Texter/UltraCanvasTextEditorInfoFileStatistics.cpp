// Apps/Texter/UltraCanvasTextEditorInfoFileStatistics.cpp
// File statistics dialog for the text editor.
// Version: 1.2.0
// Last Modified: 2026-06-07
// Author: UltraCanvas Framework
// V1.2.0: Migrated from the removed legacy layout-manager API (CreateVBoxLayout /
//   CreateGridLayout / GridRowColumnDefinition) to the CSS layout API — the dialog
//   is a flex column and the table is a CSS grid.

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include <vector>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
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

// Helper: count UTF-8 characters and whitespace-delimited words in text.
static void CountWordsAndChars(const std::string &text, int &wordCount, int &charCount) {
    wordCount = 0;
    charCount = 0;
    bool inWord = false;

    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        int charBytes = 1;
        if (c >= 0xF0) charBytes = 4;
        else if (c >= 0xE0) charBytes = 3;
        else if (c >= 0xC0) charBytes = 2;

        if (c != '\n' && c != '\r') {
            charCount++;
        }

        bool isWhitespace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!isWhitespace && !inWord) {
            wordCount++;
            inWord = true;
        } else if (isWhitespace) {
            inWord = false;
        }

        i += charBytes;
    }
}

// Helper: get creation time string for a file (platform-specific).
static std::string GetFileCreatedTimeString(const std::string &filePath) {
#if defined(__linux__)
    struct statx stx;
    if (statx(AT_FDCWD, filePath.c_str(), 0, STATX_BTIME, &stx) == 0 &&
        (stx.stx_mask & STATX_BTIME)) {
        std::time_t btime = stx.stx_btime.tv_sec;
        std::tm *tm = std::localtime(&btime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d  %H:%M:%S");
        return oss.str();
    }
    return "Not available";
#elif defined(_WIN32)
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
        return oss.str();
    }
    return "Not available";
#elif defined(__APPLE__)
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        std::time_t btime = st.st_birthtime;
        std::tm* tm = std::localtime(&btime);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d  %H:%M:%S");
        return oss.str();
    }
    return "Not available";
#else
    return "Not available";
#endif
}

void UltraCanvasTextEditor::OnInfoFileStatistics() {
    if (fileStatsDialog) return;

    // ===== GATHER DATA FOR ALL OPEN DOCUMENTS =====
    struct FileStatRow {
        std::string fileName;
        std::string filePath;
        std::string location;
        std::string sizeStr;
        uintmax_t sizeBytes = 0;
        std::string charset;
        int wordCount = 0;
        int charCount = 0;
        std::string createdStr;
        std::string modifiedStr;
        std::string statusStr;
        bool isModified = false;
        bool isActive = false;
    };

    std::vector<FileStatRow> stats;
    uintmax_t totalBytes = 0;
    long long totalWords = 0;
    long long totalChars = 0;

    const DocumentTab *activeDoc = GetActiveDocument();

    for (const auto &doc : documents) {
        FileStatRow row;
        row.fileName = doc->fileName.empty() ? "(Untitled)" : doc->fileName;
        row.filePath = doc->filePath;
        row.isActive = (doc.get() == activeDoc);
        row.isModified = doc->isModified;
        row.charset = doc->encoding.empty() ? "UTF-8" : doc->encoding;
        row.statusStr = doc->isModified ? "Unsaved" : "Saved";
        row.sizeStr = "—";
        row.createdStr = "—";
        row.modifiedStr = "—";
        row.location = "(not saved)";

        if (!doc->filePath.empty() && std::filesystem::exists(doc->filePath)) {
            std::filesystem::path p(doc->filePath);
            row.location = p.parent_path().string();

            std::error_code ec;
            uintmax_t fileSize = std::filesystem::file_size(doc->filePath, ec);
            if (!ec) {
                row.sizeBytes = fileSize;
                row.sizeStr = FormatFileSize(fileSize);
                totalBytes += fileSize;
            }

            auto lastWriteTime = std::filesystem::last_write_time(doc->filePath, ec);
            if (!ec) {
                row.modifiedStr = FormatFileTime(lastWriteTime);
            }

            row.createdStr = GetFileCreatedTimeString(doc->filePath);
        }

        std::string text = doc->textArea ? doc->textArea->GetText() : "";
        CountWordsAndChars(text, row.wordCount, row.charCount);
        totalWords += row.wordCount;
        totalChars += row.charCount;

        stats.push_back(std::move(row));
    }

    // ===== LAYOUT METRICS =====
    const int dialogPadding = 16;
    const int titleHeight   = 28;
    const int headerHeight  = 26;
    const int rowHeight     = 24;
    const int summaryHeight = 22;
    const int buttonHeight  = 28;
    const int spacing       = 8;

    const int rowCount = static_cast<int>(stats.size());
    const int gridRowCount = std::max(1, rowCount) + 1;  // +1 for header

    // Column widths: marker, name, size, charset, words, chars, modified, status
    const int colMarker   = 26;
    const int colName     = 240;
    const int colSize     = 90;
    const int colCharset  = 70;
    const int colWords    = 65;
    const int colChars    = 70;
    const int colModified = 150;
    const int colStatus   = 90;
    const int gridWidth   = colMarker + colName + colSize + colCharset +
                            colWords + colChars + colModified + colStatus;

    const int dialogWidth = gridWidth + dialogPadding * 2;

    // Clamp dialog height to a reasonable max (window is non-scrollable for now).
    const int maxVisibleRows = 16;
    const int visibleRows = std::min(std::max(rowCount, 1), maxVisibleRows);
    const int gridHeight = headerHeight + 2 + visibleRows * (rowHeight + 1);

    int dialogHeight = dialogPadding * 2
                       + titleHeight + spacing
                       + gridHeight + spacing
                       + summaryHeight + spacing
                       + buttonHeight + spacing;

    // ===== BUILD DIALOG =====
    DialogConfig dlgConfig;
    dlgConfig.title = "File Statistics";
    dlgConfig.dialogType = DialogType::Custom;
    dlgConfig.buttons = DialogButtons::NoButtons;
    dlgConfig.width = dialogWidth;
    dlgConfig.height = dialogHeight;

    fileStatsDialog = UltraCanvasDialogManager::CreateDialog(dlgConfig);

    fileStatsDialog->layout.SetFlexColumn().SetFlexGap(spacing)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    fileStatsDialog->SetPadding(dialogPadding);

    // ===== TITLE =====
    std::string titleText = "File Statistics  —  "
                            + std::to_string(rowCount)
                            + (rowCount == 1 ? " file open" : " files open");
    auto titleLabel = std::make_shared<UltraCanvasLabel>(
            "StatsTitle", 0, 0, gridWidth, titleHeight);
    titleLabel->SetText(titleText);
    titleLabel->SetFontSize(15);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetAlignment(TextAlignment::Left);
    fileStatsDialog->AddChild(titleLabel);
    titleLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    titleLabel->size.height = CSSLayout::Dimension::Px(titleHeight);

    // ===== TABLE =====
    auto gridContainer = std::make_shared<UltraCanvasContainer>(
            "StatsGridContainer", 0, 0, gridWidth, gridHeight);
    gridContainer->SetBackgroundColor(Color(220, 220, 224));
    gridContainer->SetBorders(1, Color(180, 180, 188));

    using CSSLayout::GridTrackSize;
    using CSSLayout::GridTrackSizeKind;
    using CSSLayout::Dimension;

    // 8 columns: marker, name (expands), size, charset, words, chars, modified, status.
    std::vector<GridTrackSize> cols = {
            {GridTrackSizeKind::Fixed, Dimension::Px(colMarker)},
            {GridTrackSizeKind::Fr,    Dimension::Fr(1)},          // File Name expands
            {GridTrackSizeKind::Fixed, Dimension::Px(colSize)},
            {GridTrackSizeKind::Fixed, Dimension::Px(colCharset)},
            {GridTrackSizeKind::Fixed, Dimension::Px(colWords)},
            {GridTrackSizeKind::Fixed, Dimension::Px(colChars)},
            {GridTrackSizeKind::Fixed, Dimension::Px(colModified)},
            {GridTrackSizeKind::Fixed, Dimension::Px(colStatus)},
    };
    // Row 0 = header; rows 1..N = one per document.
    std::vector<GridTrackSize> rows;
    rows.push_back({GridTrackSizeKind::Fixed, Dimension::Px(headerHeight)});
    for (int r = 1; r < gridRowCount; ++r) {
        rows.push_back({GridTrackSizeKind::Fixed, Dimension::Px(rowHeight)});
    }

    gridContainer->layout.SetGrid()
                        .SetGridColumns(cols)
                        .SetGridRows(rows)
                        .SetGridGap(1);  // 1px gaps act as grid lines via container bg color

    // ----- Header row -----
    auto addHeaderCell = [&](int col, const std::string &title, TextAlignment align) {
        auto label = std::make_shared<UltraCanvasLabel>(
                "StatHdr" + std::to_string(col), 0, 0, 80, headerHeight);
        label->SetText(title);
        label->SetFontSize(11);
        label->SetFontWeight(FontWeight::Bold);
        label->SetTextColor(Color(40, 40, 50));
        label->SetBackgroundColor(Color(235, 235, 240));
        label->SetAlignment(align, VerticalAlignment::Middle);
        label->SetPadding(0, 8, 0, 8);
        gridContainer->AddChild(label);
        label->layoutItem.SetGridRowColSimplified(0, col);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    addHeaderCell(0, "",         TextAlignment::Center);
    addHeaderCell(1, "File Name", TextAlignment::Left);
    addHeaderCell(2, "Size",     TextAlignment::Right);
    addHeaderCell(3, "Charset",  TextAlignment::Center);
    addHeaderCell(4, "Words",    TextAlignment::Right);
    addHeaderCell(5, "Chars",    TextAlignment::Right);
    addHeaderCell(6, "Modified", TextAlignment::Center);
    addHeaderCell(7, "Status",   TextAlignment::Center);

    // ----- Data rows -----
    if (rowCount == 0) {
        auto empty = std::make_shared<UltraCanvasLabel>(
                "StatsEmpty", 0, 0, gridWidth, rowHeight);
        empty->SetText("No files open");
        empty->SetFontSize(11);
        empty->SetTextColor(Color(120, 120, 120));
        empty->SetBackgroundColor(Colors::White);
        empty->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        gridContainer->AddChild(empty);
        empty->layoutItem.SetGridRowColSimplified(1, 0, 1, 8);  // span all 8 columns
        empty->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    } else {
        for (int i = 0; i < rowCount; i++) {
            const FileStatRow &s = stats[i];
            const int rowIdx = i + 1;
            const bool altRow = (i % 2 == 1);

            Color rowBgColor = s.isActive
                               ? Color(220, 235, 255)
                               : (altRow ? Color(248, 248, 250) : Colors::White);

            auto addCell = [&](int col, const std::string &text, TextAlignment align,
                               Color textColor = Color(30, 30, 30), bool bold = false,
                               const std::string &tooltip = "") {
                auto label = std::make_shared<UltraCanvasLabel>(
                        "StatCell" + std::to_string(rowIdx) + "_" + std::to_string(col),
                        0, 0, 80, rowHeight);
                label->SetText(text);
                label->SetFontSize(11);
                label->SetFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
                label->SetTextColor(textColor);
                label->SetBackgroundColor(rowBgColor);
                label->SetAlignment(align, VerticalAlignment::Middle);
                label->SetPadding(0, 6, 0, 6);
                if (!tooltip.empty()) label->SetTooltip(tooltip);
                gridContainer->AddChild(label);
                label->layoutItem.SetGridRowColSimplified(rowIdx, col);
                label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            };

            addCell(0, s.isActive ? "▶" : "",
                    TextAlignment::Center, Color(0, 100, 200), true);

            std::string nameTooltip;
            if (s.filePath.empty()) {
                nameTooltip = "(not saved)";
            } else {
                nameTooltip = s.filePath;
                if (!s.createdStr.empty() && s.createdStr != "—") {
                    nameTooltip += "\nCreated: " + s.createdStr;
                }
            }
            addCell(1, s.fileName, TextAlignment::Left,
                    Color(30, 30, 30), s.isActive, nameTooltip);

            addCell(2, s.sizeStr,                  TextAlignment::Right,  Color(30, 30, 30), false,
                    s.sizeBytes > 0 ? (std::to_string(s.sizeBytes) + " bytes") : "");
            addCell(3, s.charset,                  TextAlignment::Center);
            addCell(4, std::to_string(s.wordCount), TextAlignment::Right);
            addCell(5, std::to_string(s.charCount), TextAlignment::Right);
            addCell(6, s.modifiedStr,              TextAlignment::Center);

            Color statusColor = s.isModified ? Color(200, 120, 0) : Color(40, 130, 60);
            addCell(7, s.statusStr, TextAlignment::Center, statusColor, true);
        }
    }

    fileStatsDialog->AddChild(gridContainer);
    gridContainer->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    gridContainer->size.height = CSSLayout::Dimension::Px(gridHeight);

    // ===== SUMMARY FOOTER =====
    std::ostringstream summary;
    summary << "Total: " << rowCount
            << (rowCount == 1 ? " file" : " files")
            << "   •   " << FormatFileSize(totalBytes)
            << "   •   " << totalWords << " words"
            << "   •   " << totalChars << " chars";

    auto summaryLabel = std::make_shared<UltraCanvasLabel>(
            "StatsSummary", 0, 0, gridWidth, summaryHeight);
    summaryLabel->SetText(summary.str());
    summaryLabel->SetFontSize(11);
    summaryLabel->SetFontWeight(FontWeight::Bold);
    summaryLabel->SetTextColor(Color(60, 60, 60));
    summaryLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    fileStatsDialog->AddChild(summaryLabel);
    summaryLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    summaryLabel->size.height = CSSLayout::Dimension::Px(summaryHeight);

    // ===== OK BUTTON =====
    auto okButton = std::make_shared<UltraCanvasButton>("StatsOK", 0, 0, 80, buttonHeight);
    okButton->SetText("OK");
    okButton->onClick = [this]() {
        fileStatsDialog->CloseDialog(DialogResult::OK);
    };
    fileStatsDialog->AddChild(okButton);
    okButton->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    okButton->size.width  = CSSLayout::Dimension::Px(80);
    okButton->size.height = CSSLayout::Dimension::Px(buttonHeight);

    fileStatsDialog->onResult = [this](DialogResult) {
        fileStatsDialog.reset();
    };

    UltraCanvasDialogManager::ShowDialog(fileStatsDialog, nullptr);
}
}
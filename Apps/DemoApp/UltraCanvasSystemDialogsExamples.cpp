// Apps/DemoApp/UltraCanvasSystemDialogsExamples.cpp
// ULTRA OS system dialogs demo page (ULTRA OS modules -> System dialogs).
// Two tabs:
//   * Examples - every system dialog behind a button: Open file, Open multiple
//                files, Save file, Select folder, Print settings, Print a test
//                page, and the information / question / warning / error /
//                text / password dialogs. A "Dialog style" switch shows each
//                one either as the ULTRA OS dialog (the framework's own) or as
//                the host platform's, and every answer is written to the log.
//   * Details  - Docs/UltraCanvas/UltraCanvasSystemDialogs.md.
// Version: 1.0.0
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasFileLoader.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // Which implementation the page's buttons show.
    enum class DialogStyle { UltraOS = 0, Native = 1 };

    // Runs `show` with the dialog manager's native-dialogs switch set for the
    // chosen style, and puts the application's own setting back afterwards,
    // so the rest of the demo keeps whatever it had. A native dialog blocks
    // and has answered by the time `show` returns; the ULTRA OS dialog has
    // read the setting by then and answers later through its callback.
    void WithDialogStyle(DialogStyle style, const std::function<void()>& show) {
        const bool previous = UltraCanvasDialogManager::GetUseNativeDialogs();
        UltraCanvasDialogManager::SetUseNativeDialogs(style == DialogStyle::Native);
        show();
        UltraCanvasDialogManager::SetUseNativeDialogs(previous);
    }

    std::string DialogResultText(DialogResult result) {
        return UltraCanvasDialogManager::DialogResultToString(result);
    }

    std::string DescribePrintChoice(const NativePrintResult& choice) {
        if (!choice) return "Print dialog cancelled.";
        const IOPrintOptions& o = choice.options;
        std::string text = "Print settings chosen:";
        text += "\n    printer:     " + (choice.printerName.empty() ? std::string("(none)") : choice.printerName);
        if (choice.printToFile) {
            text += "\n    to file:     " + choice.outputFilePath;
        }
        text += "\n    copies:      " + std::to_string(o.copies) + (o.collate ? " (collated)" : "");
        text += "\n    paper:       " + std::string(IOPaperSizeToString(o.page.paperSize));
        text += "\n    orientation: " + std::string(
                    o.page.orientation == IOPrintOrientation::Landscape ||
                    o.page.orientation == IOPrintOrientation::ReverseLandscape
                    ? "landscape" : "portrait");
        text += "\n    duplex:      " + std::string(IODuplexModeToString(o.duplex));
        text += "\n    colour:      " + std::string(IOPrinterColorModeToString(o.colorMode));
        text += "\n    quality:     " + std::string(IOPrintQualityToString(o.quality));
        if (choice.pageRange.empty()) {
            text += "\n    pages:       all";
        } else {
            text += "\n    pages:      ";
            for (int page : choice.pageRange) text += " " + std::to_string(page);
        }
        text += "\n    (settings only - nothing was sent to the printer)";
        return text;
    }

    const char* kTestPageText =
        "ULTRA OS - system dialogs test page\n"
        "===================================\n\n"
        "This page was printed from the UltraCanvas demo application\n"
        "(ULTRA OS modules -> System dialogs -> Print test page).\n\n"
        "The settings chosen in the print dialog - printer, copies,\n"
        "paper, orientation, duplex and page range - were carried to\n"
        "the printer by IODeviceManager.\n";

    std::shared_ptr<UltraCanvasLabel> SectionLabel(const std::string& id, const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 22);
        label->SetText(text);
        label->SetFontSize(13);
        label->SetFontWeight(FontWeight::Bold);
        label->SetTextColor(Color(60, 60, 60, 255));
        label->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return label;
    }

    std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id) {
        auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, 34);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return row;
    }

    std::shared_ptr<UltraCanvasButton> AddButton(const std::shared_ptr<UltraCanvasContainer>& row,
                                                 const std::string& id, const std::string& text,
                                                 float width) {
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 30, text);
        button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(button);
        return button;
    }

    // ---- Examples tab: every system dialog behind a button. ----
    std::shared_ptr<UltraCanvasUIElement> BuildExamplesTab() {
        auto root = std::make_shared<UltraCanvasContainer>("SysDlgDemo", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);
        root->SetBackgroundColor(Colors::White);

        auto title = std::make_shared<UltraCanvasLabel>("SysDlgTitle", 0, 0, 0, 26);
        title->SetText("ULTRA OS system dialogs");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        auto intro = std::make_shared<UltraCanvasLabel>("SysDlgIntro", 0, 0, 0, 36);
        intro->SetText("The dialogs an application asks the system for. \"ULTRA OS\" shows the "
                       "framework's own dialogs, the same on every platform; \"Native\" shows the "
                       "host platform's. The print dialog is always the platform's.");
        intro->SetFontSize(12);
        intro->SetTextColor(Color(90, 90, 90, 255));
        intro->SetWrap(TextWrap::WrapWord);
        intro->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(intro);

        // ----- Dialog style + options -----
        auto styleRow = ButtonRow("SysDlgStyleRow");
        auto styleLabel = std::make_shared<UltraCanvasLabel>("SysDlgStyleLabel", 0, 0, 90, 30);
        styleLabel->SetText("Dialog style:");
        styleLabel->SetFontSize(12);
        styleLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        styleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        styleRow->AddChild(styleLabel);

        auto styleSelector = CreateSegmentedControl("SysDlgStyle", 0, 0, 220, 30);
        styleSelector->AddSegment("ULTRA OS");
        styleSelector->AddSegment("Native");
        styleSelector->SetSelectedIndex(static_cast<int>(DialogStyle::UltraOS));
        styleSelector->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        styleRow->AddChild(styleSelector);

        auto hiddenCheck = UltraCanvasCheckbox::CreateCheckbox("SysDlgHidden", 0, 0, 160, 30,
                                                               "Show hidden files", false);
        hiddenCheck->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        styleRow->AddChild(hiddenCheck);

        auto togglesCheck = UltraCanvasCheckbox::CreateCheckbox("SysDlgToggles", 0, 0, 200, 30,
                                                                "Filters as toggle buttons", false);
        togglesCheck->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        styleRow->AddChild(togglesCheck);
        root->AddChild(styleRow);

        // ----- Files -----
        root->AddChild(SectionLabel("SysDlgFilesLabel", "Files"));
        auto fileRow = ButtonRow("SysDlgFileRow");
        auto openBtn     = AddButton(fileRow, "SysDlgOpen",     "Open file...",        120);
        auto openManyBtn = AddButton(fileRow, "SysDlgOpenMany", "Open multiple...",    140);
        auto saveBtn     = AddButton(fileRow, "SysDlgSave",     "Save file...",        120);
        auto folderBtn   = AddButton(fileRow, "SysDlgFolder",   "Select folder...",    140);
        root->AddChild(fileRow);

        // ----- Printing -----
        root->AddChild(SectionLabel("SysDlgPrintLabel", "Printing"));
        auto printRow = ButtonRow("SysDlgPrintRow");
        auto printSettingsBtn = AddButton(printRow, "SysDlgPrintSettings", "Print settings...",   150);
        auto printTestBtn     = AddButton(printRow, "SysDlgPrintTest",     "Print test page...",  150);
        root->AddChild(printRow);

        // ----- Messages and input -----
        root->AddChild(SectionLabel("SysDlgMsgLabel", "Messages and input"));
        auto msgRow = ButtonRow("SysDlgMsgRow");
        auto infoBtn     = AddButton(msgRow, "SysDlgInfo",     "Information", 110);
        auto questionBtn = AddButton(msgRow, "SysDlgQuestion", "Question",    100);
        auto warningBtn  = AddButton(msgRow, "SysDlgWarning",  "Warning",     100);
        auto errorBtn    = AddButton(msgRow, "SysDlgError",    "Error",       90);
        auto inputBtn    = AddButton(msgRow, "SysDlgInput",    "Text input",  110);
        auto passwordBtn = AddButton(msgRow, "SysDlgPassword", "Password",    100);
        root->AddChild(msgRow);

        // ----- Result log -----
        auto logHeader = ButtonRow("SysDlgLogHeader");
        auto logLabel = SectionLabel("SysDlgLogLabel", "What the dialogs answered");
        logLabel->size.width = CSSLayout::Dimension::Px(220);
        logHeader->AddChild(logLabel);
        auto clearBtn = AddButton(logHeader, "SysDlgClear", "Clear", 70);
        root->AddChild(logHeader);

        auto log = std::make_shared<UltraCanvasTextArea>("SysDlgLog");
        log->SetReadOnly(true);
        log->SetWordWrap(true);
        log->SetText("");
        log->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(log);

        // Appends one entry, prefixed with the style the dialog was shown in,
        // and keeps the newest entry in view.
        auto write = [log, styleSelector](const std::string& text) {
            const bool native = styleSelector->GetSelectedIndex() == static_cast<int>(DialogStyle::Native);
            log->AppendText(std::string(native ? "[Native]   " : "[ULTRA OS] ") + text + "\n");
            log->MoveCursorToEnd();
            log->RequestRedraw();
        };
        auto style = [styleSelector]() {
            return styleSelector->GetSelectedIndex() == static_cast<int>(DialogStyle::Native)
                   ? DialogStyle::Native : DialogStyle::UltraOS;
        };
        // The options the file buttons share; a fresh copy per click.
        auto fileOptions = [styleSelector, hiddenCheck, togglesCheck](const std::string& dialogTitle) {
            FileDialogOptions opts;
            opts.SetTitle(dialogTitle)
                // Parenting makes the dialog modal to the demo window.
                .SetParentWindow(styleSelector->GetWindow())
                .SetShowHidden(hiddenCheck->IsChecked())
                .SetFilterToggles(togglesCheck->IsChecked())
                // A demo should not fill the desktop's recent-files list.
                .SetRegisterAsRecent(false);
            return opts;
        };

        clearBtn->onClick = [log]() {
            log->SetText("");
            log->RequestRedraw();
        };

        // ----- File dialogs -----
        openBtn->onClick = [write, style, fileOptions]() {
            auto opts = fileOptions("Open a file");
            opts.AddFilter("Images", std::vector<std::string>{"png", "jpg", "jpeg", "gif", "webp", "svg"})
                .AddFilter("Documents", std::vector<std::string>{"txt", "md", "pdf", "odt", "docx"})
                .AddFilter("Audio and video", std::vector<std::string>{"mp3", "wav", "flac", "ogg", "mp4", "mkv", "webm"})
                .AddFilter("All files", "*");
            WithDialogStyle(style(), [&]() {
                UltraCanvasFileLoader::OpenFileDialog(opts,
                    [write](DialogResult result, const std::string& path) {
                        if (result == DialogResult::OK && !path.empty()) write("Open file: " + path);
                        else write("Open file: " + DialogResultText(result));
                    });
            });
        };

        openManyBtn->onClick = [write, style, fileOptions]() {
            auto opts = fileOptions("Open files");
            opts.AddFilter("All files", "*");
            WithDialogStyle(style(), [&]() {
                UltraCanvasFileLoader::OpenMultipleFilesDialog(opts,
                    [write](DialogResult result, const std::vector<std::string>& paths) {
                        if (result != DialogResult::OK || paths.empty()) {
                            write("Open multiple: " + DialogResultText(result));
                            return;
                        }
                        std::string text = "Open multiple: " + std::to_string(paths.size()) + " file(s)";
                        for (const auto& path : paths) text += "\n    " + path;
                        write(text);
                    });
            });
        };

        saveBtn->onClick = [write, style, fileOptions]() {
            auto opts = fileOptions("Save a file");
            opts.SetDefaultFileName("Untitled.txt")
                .AddFilter("Text document", "txt")
                .AddFilter("Markdown", "md")
                .AddFilter("All files", "*");
            WithDialogStyle(style(), [&]() {
                // Only the chosen name is reported: the demo writes nothing.
                UltraCanvasFileLoader::SaveFileDialog(opts,
                    [write](DialogResult result, const std::string& path) {
                        if (result == DialogResult::OK && !path.empty())
                            write("Save file: " + path + "   (nothing was written)");
                        else write("Save file: " + DialogResultText(result));
                    });
            });
        };

        folderBtn->onClick = [write, style, fileOptions]() {
            auto opts = fileOptions("Select a folder");
            WithDialogStyle(style(), [&]() {
                UltraCanvasFileLoader::SelectFolderDialog(opts,
                    [write](DialogResult result, const std::string& folder) {
                        if (result == DialogResult::OK && !folder.empty()) write("Select folder: " + folder);
                        else write("Select folder: " + DialogResultText(result));
                    });
            });
        };

        // ----- Print dialogs (always the platform's) -----
        printSettingsBtn->onClick = [write, styleSelector]() {
            NativePrintResult choice = UltraCanvasNativeDialogs::RequestPrintSettings(
                    "UltraCanvas demo - test page", styleSelector->GetWindow());
            write(DescribePrintChoice(choice));
        };

        printTestBtn->onClick = [write, styleSelector]() {
            IODeviceResult result = PrintTextWithDialog("UltraCanvas demo - test page",
                                                        kTestPageText, styleSelector->GetWindow());
            if (result.success) {
                write("Print test page: sent (job " + std::to_string(result.backendCode) + ")");
            } else if (result.code == IODeviceResultCode::Cancelled) {
                write("Print test page: Cancel");
            } else {
                write("Print test page: failed - " +
                      (result.message.empty() ? std::string("no reason given") : result.message));
            }
        };

        // ----- Message and input dialogs -----
        infoBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowInformation(
                    "The document was saved.", "Information",
                    [write](DialogResult r) { write("Information: " + DialogResultText(r)); },
                    styleSelector->GetWindow());
            });
        };

        questionBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowQuestion(
                    "Discard the changes to \"Untitled.txt\"?", "Question",
                    [write](DialogResult r) { write("Question: " + DialogResultText(r)); },
                    styleSelector->GetWindow());
            });
        };

        warningBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowWarning(
                    "The disk is nearly full.", "Warning",
                    [write](DialogResult r) { write("Warning: " + DialogResultText(r)); },
                    styleSelector->GetWindow());
            });
        };

        errorBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowError(
                    "The file could not be written.", "Error",
                    [write](DialogResult r) { write("Error: " + DialogResultText(r)); },
                    styleSelector->GetWindow());
            });
        };

        inputBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowInputDialog(
                    "Name of the new folder:", "New folder", "Untitled folder", InputType::Text,
                    [write](DialogResult r, const std::string& value) {
                        if (r == DialogResult::OK) write("Text input: \"" + value + "\"");
                        else write("Text input: " + DialogResultText(r));
                    },
                    styleSelector->GetWindow());
            });
        };

        passwordBtn->onClick = [write, style, styleSelector]() {
            WithDialogStyle(style(), [&]() {
                UltraCanvasDialogManager::ShowInputDialog(
                    "Password for \"demo@ultra-os\":", "Password", "", InputType::Password,
                    [write](DialogResult r, const std::string& value) {
                        // Never echo a password, not even a demo one.
                        if (r == DialogResult::OK)
                            write("Password: " + std::to_string(value.size()) + " character(s) entered");
                        else write("Password: " + DialogResultText(r));
                    },
                    styleSelector->GetWindow());
            });
        };

        return root;
    }

    // ---- Details tab: the documentation page. ----
    std::shared_ptr<UltraCanvasUIElement> BuildDetailsTab() {
        const std::string base = NormalizePath(GetResourcesDir() + "Docs/UltraCanvas/");

        auto text = std::make_shared<UltraCanvasTextArea>("SysDlgDetails");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(LoadFile(base + "UltraCanvasSystemDialogs.md"));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

} // anonymous namespace

// ===== ULTRA OS SYSTEM DIALOGS DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSystemDialogsExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("SystemDialogsExamples", 0, 0, 1000, 720);
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("SystemDialogsTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(30);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Examples", BuildExamplesTab());
        tabs->AddTab("Details",  BuildDetailsTab());
        tabs->SetActiveTab(0);

        root->AddChild(tabs);
        return root;
    }

} // namespace UltraCanvas

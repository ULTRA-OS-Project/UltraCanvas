// Apps/DemoApp/UltraCanvasAlertExamples.cpp
// Demonstration of UltraCanvasAlert - modal, floating, can't-be-missed message
// boxes built on the UltraCanvas dialog system. Each button below raises an
// alert of a given severity; the result is echoed into the status label.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the buttons and the
//                  result in one card, the usage notes in another beside it; every
//                  text wraps and grows
// Version: 1.0.1 - points at the Notification Toast page for what must not interrupt
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAlertExamples() {
        auto page = DemoKit::CreatePage("AlertExamples", "Alert Dialog Examples",
                "An Alert is a modal, always-on-top message box the user cannot miss - it blocks "
                "the app until dismissed. Each severity has its own colour and icon.");

        auto row = DemoKit::AddRow(page, "AlertRow");
        auto buttonsCard = DemoKit::AddCard(row, "AlertButtonsCard", "Raise an alert",
                "Each button opens an alert; its result is shown below.");
        auto buttons = DemoKit::CreateColumn("AlertButtons", 12);
        DemoKit::Add(buttonsCard, buttons);

        // Status readout, updated from the alert callbacks.
        auto status = DemoKit::CreateText("AlertStatus", "Result: (press a button)");
        status->SetBackgroundColor(Color(240, 240, 240));
        status->SetPadding(4);
        DemoKit::Add(buttonsCard, status);

        auto describe = [](DialogResult r) -> std::string {
            return UltraCanvasDialogManager::DialogResultToString(r);
        };

        auto addButton = [&](const std::string& id, const std::string& text) {
            auto btn = CreateButton(id, 0, 0, 200, 34, text);
            DemoKit::Add(buttons, btn);
            return btn;
        };

        // Weak pointer to the page so callbacks can reach the live window
        // without extending its lifetime.
        std::weak_ptr<UltraCanvasContainer> weakContainer = page;
        auto parentWin = [weakContainer]() -> UltraCanvasWindowBase* {
            auto c = weakContainer.lock();
            return c ? c->GetWindow() : nullptr;
        };

        // ===== INFO =====
        addButton("AlertInfoBtn", "Info Alert")->onClick = [status, describe, parentWin]() {
            UltraCanvasAlert::Info("Your document has been opened in read-only mode.",
                                   "", [status, describe](DialogResult r) {
                status->SetText("Result: Info -> " + describe(r));
            }, parentWin());
        };

        // ===== SUCCESS =====
        addButton("AlertSuccessBtn", "Success Alert")->onClick = [status, describe, parentWin]() {
            UltraCanvasAlert::Successful("The export finished successfully.",
                                         "", [status, describe](DialogResult r) {
                status->SetText("Result: Success -> " + describe(r));
            }, parentWin());
        };

        // ===== WARNING =====
        addButton("AlertWarningBtn", "Warning Alert")->onClick = [status, describe, parentWin]() {
            UltraCanvasAlert::Warning("You are about to leave with unsaved changes.",
                                      "", [status, describe](DialogResult r) {
                status->SetText("Result: Warning -> " + describe(r));
            }, parentWin());
        };

        // ===== ERROR =====
        addButton("AlertErrorBtn", "Error Alert")->onClick = [status, describe, parentWin]() {
            UltraCanvasAlert::Error("Could not save the file: disk is full.",
                                    "", [status, describe](DialogResult r) {
                status->SetText("Result: Error -> " + describe(r));
            }, parentWin());
        };

        // ===== CONFIRM (Yes/No -> bool) =====
        addButton("AlertConfirmBtn", "Confirm (Yes/No)")->onClick = [status, parentWin]() {
            UltraCanvasAlert::Confirm("Delete the selected item permanently?", "Confirm Delete",
                                      [status](bool yes) {
                status->SetText(std::string("Result: Confirm -> ") + (yes ? "Yes (deleted)" : "No (kept)"));
            }, parentWin());
        };

        // ===== RICH: Markdown details + OK/Cancel =====
        addButton("AlertRichBtn", "Rich Alert (Markdown)")->onClick = [status, describe, parentWin]() {
            AlertOptions opts;
            opts.severity = AlertSeverity::Warning;
            opts.title = "Update available";
            // The message is rendered as Markdown, so inline **bold**, *italic*
            // and `code` all format; the dialog height fits the text.
            opts.message = "A new version of **UltraCanvas** is ready to install.";
            opts.details = "Version `4.2.0` — the application will *restart* to finish updating.";
            opts.buttons = DialogButtons::OKCancel;
            opts.defaultButton = DialogButton::OK;
            opts.parent = parentWin();
            opts.onResult = [status, describe](DialogResult r) {
                status->SetText("Result: Rich -> " + describe(r));
            };
            UltraCanvasAlert::Show(opts);
        };

        // ===== LONG MARKDOWN: auto-size + scrollbar =====
        addButton("AlertLongBtn", "Long Markdown Alert")->onClick = [status, describe, parentWin]() {
            AlertOptions opts;
            opts.severity = AlertSeverity::Info;
            opts.title = "Release notes";
            // Long, structured Markdown: the dialog grows to fit up to the
            // monitor cap, then the message area shows its own scrollbar.
            opts.message =
                    "# UltraCanvas 4.2.0\n\n"
                    "This release focuses on **dialogs** and *text rendering*.\n\n"
                    "## Highlights\n\n"
                    "- Alerts now render their message as Markdown\n"
                    "- Dialog height **auto-fits** the text\n"
                    "- A vertical scrollbar appears when the text is too tall\n"
                    "- Inline `code`, **bold** and *italic* are supported\n\n"
                    "## Details\n\n"
                    "The message area is a read-only text area, so long content\n"
                    "stays fully readable by scrolling instead of being clipped.\n\n"
                    "1. Open an alert\n"
                    "2. Watch it size itself to the text\n"
                    "3. Scroll if the content exceeds the screen\n\n"
                    "> Tip: pass Markdown straight into the message string.\n\n"
                    "Thanks for using UltraCanvas!";
            opts.buttons = DialogButtons::OK;
            opts.defaultButton = DialogButton::OK;
            opts.parent = parentWin();
            opts.onResult = [status, describe](DialogResult r) {
                status->SetText("Result: Long -> " + describe(r));
            };
            UltraCanvasAlert::Show(opts);
        };

        // ===== NO ICON: content centred in the full width =====
        addButton("AlertNoIconBtn", "Alert Without Icon")->onClick = [status, describe, parentWin]() {
            AlertOptions opts;
            opts.title = "Extracting";
            opts.message = "Unpacking \"UCDemo-Windows.zip\"";
            opts.details = "Without the severity badge the message column spans the "
                           "whole dialog, so anything centred in it is centred in the "
                           "window - which is how the progress dialog centres its ring.";
            // Same as UltraCanvasAlert::Plain(...): no icon column at all.
            opts.showIcon = false;
            opts.parent = parentWin();
            opts.onResult = [status, describe](DialogResult r) {
                status->SetText("Result: No icon -> " + describe(r));
            };
            UltraCanvasAlert::Show(opts);
        };

        // ===== INSTRUCTIONS =====
        auto usageCard = DemoKit::AddCard(row, "AlertInstructions",
                "UltraCanvasAlert (include/UltraCanvasAlert.h)");
        DemoKit::AddText(usageCard, "AlertInstructionsText",
                "One-liners (non-blocking, result via callback):\n"
                "  UltraCanvasAlert::Info(msg);\n"
                "  UltraCanvasAlert::Successful(msg);\n"
                "  UltraCanvasAlert::Warning(msg);\n"
                "  UltraCanvasAlert::Error(msg);\n"
                "  UltraCanvasAlert::Confirm(msg, title, [](bool yes){ ... });\n"
                "  UltraCanvasAlert::Plain(msg, title);   // no severity icon\n\n"
                "Rich form:\n"
                "  AlertOptions o; o.severity = AlertSeverity::Warning;\n"
                "  o.message = ...; o.details = ...; o.buttons = DialogButtons::OKCancel;\n"
                "  UltraCanvasAlert::Show(o);\n\n"
                "The message supports Markdown (**bold**, *italic*, `code`, "
                "headers, lists). The dialog height auto-fits the text and shows "
                "a vertical scrollbar once the content grows past the screen.\n\n"
                "Every alert is modal and always-on-top; the parent window's input "
                "is blocked until the user answers. Severity picks the icon + colour "
                "(Info/Question blue, Success green, Warning amber, Error red).\n\n"
                "showIcon = false (or Plain()) drops the icon column entirely, so "
                "the content spans the full width - use it when the alert body "
                "carries its own graphic that has to sit in the middle.\n\n"
                "For a message that must not interrupt: a toast - see the "
                "Notification Toast page (UltraCanvasNotificationToast).")->SetFontSize(11);

        return page;
    }

} // namespace UltraCanvas

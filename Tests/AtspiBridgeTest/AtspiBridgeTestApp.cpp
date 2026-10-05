// Tests/AtspiBridgeTest/AtspiBridgeTestApp.cpp
// The application side of AtspiBridgeTest: a window holding a rich text
// editor and a password field, published to AT-SPI by the Linux bridge. When
// an assistive technology moves the caret to offset 8 it types "XY" there, and
// shortly after deletes it again, so the client can watch both events arrive.
// Quits after 20 s if nobody stops it.
// Version: 1.1.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextInput.h"

#include <iostream>

using namespace UltraCanvas;

int main() {
    UltraCanvasApplication app;
    if (!app.Initialize("AtspiBridgeTestApp")) return 1;
    WindowConfig config;
    config.title = "Accessibility Test";
    config.width = 600;
    config.height = 450;
    auto window = CreateWindow(config);
    window->Show();

    auto edit = std::make_shared<UltraCanvasRichTextEdit>("docEditor", 10, 10, 580, 380);
    window->AddChild(edit);
    edit->SetMarkdown("# Report\n\nHello **bold** world. Second sentence here.\n");
    edit->GetDocument()->metadata.title = "Quarterly Report";
    // Below the editor: the bridge must call it password text and keep its
    // content to itself.
    auto password = CreatePasswordInput("masterPassword", 10, 400, 300, 30);
    password->SetText("hunter2");
    window->AddChild(password);
    edit->SetFocus(true);

    bool typed = false;
    UltraCanvasRichTextEdit* editor = edit.get();
    edit->onSelectionChanged = [&app, &typed, editor]() {
        IAccessibleText* text = editor->GetAccessibleTextInterface();
        if (typed || !text || text->GetCaretOffset() != 8) return;
        typed = true;
        app.StartTimer(200, false, [&app, editor](TimerId) {
            editor->InsertText("XY");
            std::cerr << "APP: typed XY" << std::endl;
            app.StartTimer(300, false, [editor](TimerId) {
                IAccessibleText* text = editor->GetAccessibleTextInterface();
                text->SetSelection(8, 10);
                editor->DeleteSelection();
                std::cerr << "APP: deleted XY" << std::endl;
            });
        });
    };
    app.StartTimer(20000, false, [&app](TimerId) { app.Exit(); });
    app.Run();
    return 0;
}

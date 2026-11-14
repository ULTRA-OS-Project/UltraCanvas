// Tests/TextAreaSyntaxStateTest.cpp
// Syntax colours in a real UltraCanvasTextArea, read back from the window:
//   - the middle line of a /* block comment */ is drawn as a comment, and
//     goes back to code the moment an edit above removes the "/*" - the
//     cached layout of a line the edit did not touch is rebuilt because what
//     the line above leaves open changed;
//   - a word the language lists as a type is drawn with the type style (it
//     used to fall through to the default colour);
//   - in Markdown mode, a block comment inside a ``` code fence colours all of
//     its lines, ends at the fence, and never leaks into the raw line the
//     cursor is editing (read with the Markdown rules, not the fence's);
//   - a fence in a language the tokenizer does not know is shown plain.
//
// Every token class is painted one pure colour so a pixel says which class
// covers it. Runs headless under Xvfb; skips when there is no display.
// Version: 1.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

const Color kComment(255, 0, 255);   // magenta: no Markdown decoration uses it
const Color kCode(0, 0, 255);
const Color kType(0, 200, 0);

struct Counts { int comment = 0; int code = 0; int type = 0; };

bool Near(const Color& px, const Color& c) {
    auto close = [](uint8_t a, uint8_t b) { return std::abs(int(a) - int(b)) <= 60; };
    return close(px.r, c.r) && close(px.g, c.g) && close(px.b, c.b);
}

Counts CountPixels(const std::shared_ptr<UltraCanvasWindow>& window,
                   const std::shared_ptr<UltraCanvasTextArea>& area) {
    area->RequestRedraw();
    window->UpdateAndRender();
    Counts n;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 580; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) continue;
            if (Near(px, kComment)) n.comment++;
            else if (Near(px, kCode)) n.code++;
            else if (Near(px, kType)) n.type++;
        }
    }
    std::cerr << "   comment " << n.comment << ", code " << n.code << ", type " << n.type << std::endl;
    return n;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   TextArea Syntax State Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextAreaSyntaxStateTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "TextAreaSyntaxStateTest";
    cfg.width = 600;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Code", 0, 0, 580, 300);
    window->AddChild(area);
    area->SetShowLineNumbers(false);
    area->SetHighlightCurrentLine(false);
    area->SetFontSize(28);
    area->SetHighlightSyntax(true);
    area->SetProgrammingLanguage("C++");

    TextAreaStyle& style = area->GetStyle();
    auto& ts = style.tokenStyles;
    for (TokenStyle* code : {&ts.keywordStyle, &ts.functionStyle, &ts.numberStyle, &ts.stringStyle,
                             &ts.characterStyle, &ts.operatorStyle, &ts.punctuationStyle,
                             &ts.preprocessorStyle, &ts.constantStyle, &ts.identifierStyle,
                             &ts.builtinStyle, &ts.defaultStyle}) {
        code->color = kCode;
        code->bold = true;
    }
    ts.commentStyle.color = kComment;
    ts.commentStyle.bold = true;
    ts.typeStyle.color = kType;
    ts.typeStyle.bold = true;
    style.fontColor = kCode;

    // ===== BLOCK COMMENT OVER THREE LINES =====
    std::cerr << "\n--- A block comment over three lines ---" << std::endl;
    area->SetText("/*\nwhile return while\n*/");
    Counts inside = CountPixels(window, area);
    TEST("The comment is drawn in the comment colour", inside.comment > 200);
    TEST("No line of it is drawn as code - not even the middle one", inside.code < 20);

    // Removing "/*" edits line 0 only; lines 1 and 2 must follow.
    std::cerr << "\n--- After deleting the \"/*\" above it ---" << std::endl;
    TEST("The edit is accepted", area->ReplaceTextRange(0, 2, ""));
    Counts after = CountPixels(window, area);
    TEST("The former middle line is code again", after.code > 200);
    TEST("Nothing is left in the comment colour", after.comment < 20);

    std::cerr << "\n--- After putting it back ---" << std::endl;
    TEST("The edit is accepted", area->ReplaceTextRange(0, 0, "/*"));
    Counts again = CountPixels(window, area);
    TEST("The following lines are a comment again", again.comment > 200 && again.code < 20);

    // ===== TYPE STYLE =====
    std::cerr << "\n--- A type name ---" << std::endl;
    area->SetText("size_t uint8_t");
    Counts types = CountPixels(window, area);
    TEST("A type is drawn with the type style", types.type > 100);

    // ===== MARKDOWN FENCED CODE =====
    // A ``` block is highlighted line by line with its language's rules; its
    // lines carry state among themselves like a plain code file does.
    std::cerr << "\n--- A block comment inside a Markdown code fence ---" << std::endl;
    style.fontColor = Color(0, 0, 0);     // prose and the fence's language tag
    style.fixedFontStyle.fontSize = 28;   // code lines use the fixed-width font
    style.fixedFontStyle.fontWeight = FontWeight::Bold;
    area->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    area->SetDisplayOnly(true);           // every line rendered, none raw for editing
    area->SetText("```cpp\n/*\nwhile return while\n*/\n```");
    Counts fenced = CountPixels(window, area);
    TEST("The fenced comment is drawn in the comment colour", fenced.comment > 200);
    TEST("Its middle line is not drawn as code", fenced.code < 20);

    area->SetText("```cpp\nwhile return while\n```");
    Counts fencedCode = CountPixels(window, area);
    TEST("Code in a fence is still drawn as code", fencedCode.code > 200 && fencedCode.comment < 20);

    area->SetText("```cpp\n/* never closed\n```\n\n```cpp\nwhile return while\n```");
    Counts nextBlock = CountPixels(window, area);
    TEST("A comment left open in one fence does not run into the next", nextBlock.code > 200);

    // An unknown language shows plain - not in the previous fence's language.
    area->SetText("```cpp\nwhile return while\n```\n\n```foo\nwhile return while\n```");
    Counts unknownLang = CountPixels(window, area);
    TEST("A fence in an unknown language is not highlighted as the one before it",
         std::abs(unknownLang.code - fencedCode.code) < fencedCode.code / 10);

    // Editing: the line under the cursor is shown raw and read with the
    // Markdown rules. It must not take a C++ comment state from the code line
    // above it - in the Markdown rules that state would mean an HTML comment.
    std::cerr << "\n--- The raw cursor line after an open fenced comment ---" << std::endl;
    area->SetDisplayOnly(false);
    area->SetReadOnly(false);
    area->SetText("```cpp\n/* open\n```");
    area->SetCursorPosition({0, 0});
    Counts cursorOnOpen = CountPixels(window, area);
    area->SetCursorPosition({2, 0});
    Counts cursorOnClose = CountPixels(window, area);
    TEST("The raw closing fence is not drawn as a comment",
         std::abs(cursorOnClose.comment - cursorOnOpen.comment) < 10);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}

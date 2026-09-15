// Tests/RichTextEditorTest.cpp
// Standalone tests for UCRichDocumentEditor — the editing core behind
// UltraCanvasRichTextEdit. Exercises positions, text editing, run splitting
// and coalescing, character and paragraph formatting, structure, clipboard
// ranges and undo/redo.
//
// Builds without the UI stack (the editing core is UI-free by design), so it
// needs no display and runs anywhere: see Tests/CMakeLists.txt.
#include "UltraCanvasRichDocumentEditor.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    auto&& a_ = (actual); auto&& e_ = (expected); \
    if (!(a_ == e_)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #actual "\n" \
                  << "  expected: [" << e_ << "]\n  actual:   [" << a_ << "]\n"; \
        ++failures; \
    } \
} while (0)

// ===== HELPERS =====

static std::shared_ptr<UCRichDocument> MakeDocument(const std::vector<std::string>& paragraphs) {
    auto doc = std::make_shared<UCRichDocument>();
    for (const auto& text : paragraphs) {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        RichTextRun run;
        run.text = text;
        block.runs.push_back(run);
        doc->blocks.push_back(block);
    }
    return doc;
}

// Every block's text joined by '|', so one comparison covers content and
// block structure at once.
static std::string Shape(const UCRichDocumentEditor& editor) {
    std::string out;
    for (int i = 0; i < editor.GetBlockCount(); i++) {
        if (i) out += "|";
        out += editor.BlockText(i);
    }
    return out;
}

// Run formatting of one block as a compact string: "text{bi}" per run.
static std::string Runs(const UCRichDocumentEditor& editor, int blockIndex) {
    std::string out;
    for (const auto& run : editor.GetBlock(blockIndex).runs) {
        out += "[";
        if (run.lineBreakBefore) out += "\\n";
        out += run.text;
        out += "{";
        if (run.bold) out += "b";
        if (run.italic) out += "i";
        if (run.underline) out += "u";
        if (run.strikethrough) out += "s";
        if (run.code) out += "c";
        if (!run.linkTarget.empty()) out += "L";
        out += "}]";
    }
    return out;
}

// ===== TESTS =====

static void TestPositionsAndNavigation() {
    UCRichDocumentEditor ed(MakeDocument({"Hello world", "Second"}));

    CHECK_EQ(ed.GetBlockCount(), 2);
    CHECK_EQ(ed.BlockText(0), std::string("Hello world"));
    CHECK_EQ(ed.BlockTextLength(0), 11);

    // Clamping keeps a position inside the document.
    RichDocPosition clamped = ed.ClampPosition({5, 99});
    CHECK_EQ(clamped.blockIndex, 1);
    CHECK_EQ(clamped.byteOffset, 6);

    // Character motion crosses block boundaries in both directions.
    RichDocPosition atEnd = ed.BlockEnd({0, 0});
    CHECK_EQ(atEnd.byteOffset, 11);
    RichDocPosition next = ed.NextCharacter(atEnd);
    CHECK_EQ(next.blockIndex, 1);
    CHECK_EQ(next.byteOffset, 0);
    RichDocPosition back = ed.PreviousCharacter(next);
    CHECK(back == atEnd);

    // Word motion.
    RichDocPosition word = ed.NextWord({0, 0});
    CHECK_EQ(word.byteOffset, 6);                 // start of "world"
    CHECK_EQ(ed.PreviousWord({0, 8}).byteOffset, 6);
    RichDocRange wordRange = ed.WordAt({0, 8});
    CHECK_EQ(wordRange.start.byteOffset, 6);
    CHECK_EQ(wordRange.end.byteOffset, 11);

    // Document ends.
    CHECK(ed.DocumentStart() == RichDocPosition(0, 0));
    CHECK(ed.DocumentEnd() == RichDocPosition(1, 6));

    // UTF-8: "é" is two bytes and must be crossed atomically.
    UCRichDocumentEditor utf(MakeDocument({"aéb"}));
    CHECK_EQ(utf.BlockTextLength(0), 4);
    CHECK_EQ(utf.NextCharacter({0, 1}).byteOffset, 3);
    CHECK_EQ(utf.PreviousCharacter({0, 3}).byteOffset, 1);
    CHECK_EQ(utf.ClampPosition({0, 2}).byteOffset, 1);   // snapped off the continuation byte
}

static void TestTypingAndDeleting() {
    UCRichDocumentEditor ed(MakeDocument({"Hello world"}));

    ed.SetCaret({0, 5});
    ed.InsertText(",");
    CHECK_EQ(Shape(ed), std::string("Hello, world"));
    CHECK_EQ(ed.GetCaret().byteOffset, 6);

    ed.SetCaret({0, 0});
    ed.DeleteForward();
    CHECK_EQ(Shape(ed), std::string("ello, world"));

    ed.SetCaret(ed.DocumentEnd());
    ed.DeleteBackward();
    CHECK_EQ(Shape(ed), std::string("ello, worl"));

    // Selection replace.
    ed.SetSelection({0, 0}, {0, 4});
    ed.InsertText("Y");
    CHECK_EQ(Shape(ed), std::string("Y, worl"));
    CHECK(!ed.HasSelection());

    // Backspace at the start of a block joins it to the previous one.
    UCRichDocumentEditor join(MakeDocument({"one", "two"}));
    join.SetCaret({1, 0});
    join.DeleteBackward();
    CHECK_EQ(Shape(join), std::string("onetwo"));
    CHECK_EQ(join.GetCaret().blockIndex, 0);
    CHECK_EQ(join.GetCaret().byteOffset, 3);

    // Delete at the end of a block pulls the next one up.
    UCRichDocumentEditor pull(MakeDocument({"one", "two"}));
    pull.SetCaret({0, 3});
    pull.DeleteForward();
    CHECK_EQ(Shape(pull), std::string("onetwo"));

    // A selection spanning blocks leaves one joined block.
    UCRichDocumentEditor span(MakeDocument({"alpha", "beta", "gamma"}));
    span.SetSelection({0, 2}, {2, 2});
    span.DeleteSelection();
    CHECK_EQ(Shape(span), std::string("almma"));
    CHECK(span.GetCaret() == RichDocPosition(0, 2));
}

static void TestBlockSplitting() {
    UCRichDocumentEditor ed(MakeDocument({"Hello world"}));
    ed.SetCaret({0, 5});
    ed.SplitBlock();
    CHECK_EQ(Shape(ed), std::string("Hello| world"));
    CHECK(ed.GetCaret() == RichDocPosition(1, 0));

    // Enter at the end of a heading starts body text, not another heading.
    UCRichDocumentEditor heading(MakeDocument({"Title"}));
    heading.SetCaret({0, 0});
    heading.SetHeadingLevel(2);
    CHECK(heading.GetBlock(0).type == RichBlockType::Heading);
    heading.SetCaret({0, 5});
    heading.SplitBlock();
    CHECK(heading.GetBlock(1).type == RichBlockType::Paragraph);

    // Enter in a list continues the list; on an empty item it leaves the list.
    UCRichDocumentEditor list(MakeDocument({"item"}));
    list.SetCaret({0, 0});
    list.SetListStyle(false);
    list.SetCaret({0, 4});
    list.SplitBlock();
    CHECK(list.GetBlock(1).type == RichBlockType::ListItem);
    list.SplitBlock();
    CHECK(list.GetBlock(1).type == RichBlockType::Paragraph);
    CHECK_EQ(list.GetBlockCount(), 2);

    // Soft break stays inside the paragraph and shows up as a '\n'.
    UCRichDocumentEditor soft(MakeDocument({"ab"}));
    soft.SetCaret({0, 1});
    soft.InsertLineBreak();
    CHECK_EQ(soft.GetBlockCount(), 1);
    CHECK_EQ(soft.BlockText(0), std::string("a\nb"));
    CHECK_EQ(soft.GetCaret().byteOffset, 2);

    // ... and deleting it restores the single line.
    soft.DeleteBackward();
    CHECK_EQ(soft.BlockText(0), std::string("ab"));

    // Multi-line inserted text becomes paragraphs.
    UCRichDocumentEditor paste(MakeDocument({""}));
    paste.InsertText("one\ntwo\nthree");
    CHECK_EQ(Shape(paste), std::string("one|two|three"));
}

static void TestCharacterFormatting() {
    UCRichDocumentEditor ed(MakeDocument({"Hello world"}));

    // Bold a middle range: the run splits in three, and only the middle is bold.
    ed.SetSelection({0, 0}, {0, 5});
    ed.ToggleBold();
    CHECK_EQ(Runs(ed, 0), std::string("[Hello{b}][ world{}]"));
    CHECK(RichCharFormatState::IsOn(ed.GetFormatState().bold));

    // Toggling back merges the runs again — no leftover fragmentation.
    ed.ToggleBold();
    CHECK_EQ(Runs(ed, 0), std::string("[Hello world{}]"));

    // Italic over a partial word, then bold over a range crossing that
    // boundary: attributes compose rather than replace.
    ed.SetSelection({0, 6}, {0, 11});
    ed.ToggleItalic();
    ed.SetSelection({0, 3}, {0, 8});
    ed.ToggleBold();
    CHECK_EQ(Runs(ed, 0), std::string("[Hel{}][lo {b}][wo{bi}][rld{i}]"));

    // Mixed state is reported as mixed, not as on or off.
    ed.SetSelection({0, 0}, {0, 11});
    RichCharFormatState state = ed.GetFormatState();
    CHECK(state.bold == RichCharFormatState::Tri::Mixed);
    CHECK(state.italic == RichCharFormatState::Tri::Mixed);

    // Clearing formatting collapses everything back to one plain run.
    ed.ClearFormatting();
    CHECK_EQ(Runs(ed, 0), std::string("[Hello world{}]"));

    // Font, size, colour and link.
    ed.SetSelection({0, 0}, {0, 5});
    ed.SetFontFamily("Georgia");
    ed.SetFontSize(14.0f);
    ed.SetTextColor("#FF0000");
    ed.SetLink("https://example.com");
    const RichTextRun& first = ed.GetBlock(0).runs[0];
    CHECK_EQ(first.fontFamily, std::string("Georgia"));
    CHECK(first.fontSizePt == 14.0f);
    CHECK_EQ(first.color, std::string("#FF0000"));
    CHECK_EQ(first.linkTarget, std::string("https://example.com"));

    // Sub- and superscript are mutually exclusive.
    UCRichDocumentEditor script(MakeDocument({"x2"}));
    script.SetSelection({0, 1}, {0, 2});
    script.ToggleSuperscript();
    CHECK(script.GetBlock(0).runs[1].superscript);
    script.ToggleSubscript();
    CHECK(script.GetBlock(0).runs[1].subscript);
    CHECK(!script.GetBlock(0).runs[1].superscript);

    // A format armed at a collapsed caret applies to what is typed next.
    UCRichDocumentEditor pending(MakeDocument({"ab"}));
    pending.SetCaret({0, 2});
    pending.ToggleBold();
    CHECK(pending.HasPendingFormat());
    CHECK(RichCharFormatState::IsOn(pending.GetFormatState().bold));
    pending.InsertText("C");
    CHECK_EQ(Runs(pending, 0), std::string("[ab{}][C{b}]"));
    CHECK(!pending.HasPendingFormat());
}

static void TestBlockFormatting() {
    UCRichDocumentEditor ed(MakeDocument({"One", "Two", "Three"}));

    // A block command applies to every block the selection touches.
    ed.SetSelection({0, 1}, {1, 1});
    ed.SetHeadingLevel(3);
    CHECK(ed.GetBlock(0).type == RichBlockType::Heading);
    CHECK_EQ(ed.GetBlock(0).headingLevel, 3);
    CHECK(ed.GetBlock(1).type == RichBlockType::Heading);
    CHECK(ed.GetBlock(2).type == RichBlockType::Paragraph);

    // A selection ending exactly at a block start does not include that block.
    ed.SetSelection({0, 0}, {1, 0});
    ed.SetAlignment(RichTextAlign::Center);
    CHECK(ed.GetBlock(0).align == RichTextAlign::Center);
    CHECK(ed.GetBlock(1).align == RichTextAlign::Default);

    // Lists: toggle on, indent, outdent, and toggle back off.
    ed.SetCaret({2, 0});
    ed.ToggleList(true);
    CHECK(ed.GetBlock(2).type == RichBlockType::ListItem);
    CHECK(ed.GetBlock(2).orderedList);
    ed.IndentList();
    CHECK_EQ(ed.GetBlock(2).listLevel, 1);
    ed.OutdentList();
    CHECK_EQ(ed.GetBlock(2).listLevel, 0);
    ed.ToggleList(true);
    CHECK(ed.GetBlock(2).type == RichBlockType::Paragraph);

    // Outdenting a top-level item leaves the list entirely.
    ed.ToggleList(false);
    CHECK(ed.GetBlock(2).type == RichBlockType::ListItem);
    ed.OutdentList();
    CHECK(ed.GetBlock(2).type == RichBlockType::Paragraph);

    // Quotes and code blocks toggle as a pair of states.
    ed.ToggleBlockQuote();
    CHECK(ed.GetBlock(2).type == RichBlockType::BlockQuote);
    ed.ToggleBlockQuote();
    CHECK(ed.GetBlock(2).type == RichBlockType::Paragraph);
    ed.ToggleCodeBlock("cpp");
    CHECK(ed.GetBlock(2).type == RichBlockType::CodeBlock);
    CHECK_EQ(ed.GetBlock(2).codeLanguage, std::string("cpp"));

    // Enter inside a code block adds a line instead of ending the block.
    ed.SetCaret({2, 5});
    int before = ed.GetBlockCount();
    ed.SplitBlock();
    CHECK_EQ(ed.GetBlockCount(), before);
    CHECK_EQ(ed.BlockText(2), std::string("Three\n"));
}

static void TestStructureBlocks() {
    UCRichDocumentEditor ed(MakeDocument({"Above", "Below"}));

    ed.SetCaret({0, 5});
    ed.InsertHorizontalRule();
    CHECK(ed.GetBlock(1).type == RichBlockType::HorizontalRule);
    // The caret lands in a text block, never on the rule.
    CHECK(ed.IsTextBlock(ed.GetCaret().blockIndex));

    // Backspace at the start of the block after a rule removes the rule.
    int ruleIndex = 1;
    ed.SetCaret({ruleIndex + 1, 0});
    ed.DeleteBackward();
    CHECK(ed.GetBlock(1).type != RichBlockType::HorizontalRule);

    // Images enter the media store once and are referenced by index.
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    UCRichDocumentEditor img(MakeDocument({""}));
    int blockIndex = img.InsertImage("pic.png", "image/png", png, "A picture");
    CHECK(blockIndex >= 0);
    CHECK(img.GetBlock(blockIndex).type == RichBlockType::Image);
    CHECK_EQ(img.GetBlock(blockIndex).mediaIndex, 0);
    CHECK_EQ(img.GetDocument()->media.size(), size_t(1));
    CHECK_EQ(img.GetBlock(blockIndex).imageAltText, std::string("A picture"));
    // An image block holds no text, so it never traps the caret.
    CHECK(!img.IsTextBlock(blockIndex));
    CHECK_EQ(img.BlockTextLength(blockIndex), 0);

    // Typing with the caret on a non-text block starts a paragraph instead.
    img.SetCaret({blockIndex, 0});
    img.InsertText("caption");
    CHECK(img.IsTextBlock(img.GetCaret().blockIndex));
    CHECK_EQ(img.BlockText(img.GetCaret().blockIndex), std::string("caption"));
}

static void TestClipboardRanges() {
    UCRichDocumentEditor ed(MakeDocument({"Hello world", "Second line"}));
    ed.SetSelection({0, 0}, {0, 5});
    ed.ToggleBold();

    // A range inside one block copies just that slice, keeping formatting.
    std::vector<RichDocBlock> copied = ed.ExtractRange(RichDocRange({0, 2}, {0, 8}));
    CHECK_EQ(copied.size(), size_t(1));
    CHECK_EQ(UCRichDocument::ConcatenateRunText(copied[0].runs), std::string("llo wo"));
    CHECK(copied[0].runs[0].bold);
    CHECK(!copied[0].runs.back().bold);

    // A multi-block range trims the first and last blocks.
    std::vector<RichDocBlock> multi = ed.ExtractRange(RichDocRange({0, 6}, {1, 6}));
    CHECK_EQ(multi.size(), size_t(2));
    CHECK_EQ(UCRichDocument::ConcatenateRunText(multi[0].runs), std::string("world"));
    CHECK_EQ(UCRichDocument::ConcatenateRunText(multi[1].runs), std::string("Second"));
    CHECK_EQ(ed.RangeToPlainText(RichDocRange({0, 6}, {1, 6})), std::string("world\nSecond"));

    // Pasting one paragraph flows into the current one, keeping its formatting.
    UCRichDocumentEditor target(MakeDocument({"AB"}));
    target.SetCaret({0, 1});
    target.InsertBlocks(copied);
    CHECK_EQ(Shape(target), std::string("Allo woB"));
    CHECK(target.GetBlock(0).runs[1].bold);
    CHECK_EQ(target.GetCaret().byteOffset, 7);

    // Pasting several blocks splits the target and rejoins the tail.
    UCRichDocumentEditor target2(MakeDocument({"AB"}));
    target2.SetCaret({0, 1});
    target2.InsertBlocks(multi);
    CHECK_EQ(Shape(target2), std::string("Aworld|SecondB"));
}

static void TestUndoRedo() {
    UCRichDocumentEditor ed(MakeDocument({"Hello"}));
    CHECK(!ed.CanUndo());
    CHECK(!ed.IsModified());

    // Consecutive keystrokes coalesce into one undo step.
    ed.SetCaret({0, 5});
    ed.InsertText(" ");
    ed.InsertText("w");
    ed.InsertText("o");
    CHECK_EQ(Shape(ed), std::string("Hello wo"));
    CHECK(ed.IsModified());
    CHECK(ed.Undo());
    CHECK_EQ(Shape(ed), std::string("Hello"));
    CHECK(!ed.CanUndo());
    CHECK(ed.Redo());
    CHECK_EQ(Shape(ed), std::string("Hello wo"));

    // Moving the caret ends the typing run, so the next keystrokes undo apart.
    ed.SetCaret({0, 0});
    ed.InsertText("X");
    CHECK_EQ(Shape(ed), std::string("XHello wo"));
    CHECK(ed.Undo());
    CHECK_EQ(Shape(ed), std::string("Hello wo"));

    // Structural edits undo as one step each, caret included.
    UCRichDocumentEditor split(MakeDocument({"one two"}));
    split.SetCaret({0, 3});
    split.SplitBlock();
    CHECK_EQ(Shape(split), std::string("one| two"));
    CHECK(split.Undo());
    CHECK_EQ(Shape(split), std::string("one two"));
    CHECK(split.GetCaret() == RichDocPosition(0, 3));

    // Formatting undoes to the exact run structure it replaced.
    UCRichDocumentEditor fmt(MakeDocument({"Hello world"}));
    fmt.SetSelection({0, 0}, {0, 5});
    fmt.ToggleBold();
    CHECK_EQ(Runs(fmt, 0), std::string("[Hello{b}][ world{}]"));
    CHECK(fmt.Undo());
    CHECK_EQ(Runs(fmt, 0), std::string("[Hello world{}]"));
    CHECK(fmt.Redo());
    CHECK_EQ(Runs(fmt, 0), std::string("[Hello{b}][ world{}]"));

    // A multi-block deletion is one step, and undo restores every block.
    UCRichDocumentEditor del(MakeDocument({"alpha", "beta", "gamma"}));
    del.SetSelection({0, 2}, {2, 2});
    del.DeleteSelection();
    CHECK_EQ(del.GetBlockCount(), 1);
    CHECK(del.Undo());
    CHECK_EQ(Shape(del), std::string("alpha|beta|gamma"));
    CHECK_EQ(del.GetBlockCount(), 3);

    // A new edit after undo drops the redo branch.
    del.SetCaret({0, 0});
    del.InsertText("Z");
    CHECK(!del.CanRedo());
}

static void TestDocumentIntegration() {
    // The editor's output stays a valid UCRichDocument: what it produces must
    // survive the serializers the format layer is built on.
    UCRichDocumentEditor ed(MakeDocument({"Title", "Body text"}));
    ed.SetCaret({0, 0});
    ed.SetHeadingLevel(1);
    ed.SetSelection({1, 0}, {1, 4});
    ed.ToggleBold();

    std::string markdown = ed.GetMarkdown();
    CHECK(markdown.find("# Title") != std::string::npos);
    CHECK(markdown.find("**Body**") != std::string::npos);
    CHECK_EQ(ed.GetPlainText().find("Title"), size_t(0));

    // Markdown round-trip through the model keeps the structure the editor built.
    UCRichDocument reparsed = UCRichDocument::FromMarkdown(markdown);
    CHECK(!reparsed.blocks.empty());
    CHECK(reparsed.blocks[0].type == RichBlockType::Heading);
    CHECK_EQ(reparsed.blocks[0].headingLevel, 1);

    // An editor over an empty document still has somewhere to type.
    UCRichDocumentEditor empty(std::make_shared<UCRichDocument>());
    CHECK_EQ(empty.GetBlockCount(), 1);
    empty.InsertText("first");
    CHECK_EQ(Shape(empty), std::string("first"));

    // Change notifications fire for edits and for undo.
    int changes = 0;
    empty.onChanged = [&changes]() { changes++; };
    empty.InsertText("!");
    empty.Undo();
    CHECK(changes >= 2);
}

int main() {
    TestPositionsAndNavigation();
    TestTypingAndDeleting();
    TestBlockSplitting();
    TestCharacterFormatting();
    TestBlockFormatting();
    TestStructureBlocks();
    TestClipboardRanges();
    TestUndoRedo();
    TestDocumentIntegration();

    if (failures == 0) {
        std::cout << "ALL TESTS PASSED (" << checks << " checks)\n";
        return 0;
    }
    std::cout << failures << " FAILURES of " << checks << " checks\n";
    return 1;
}

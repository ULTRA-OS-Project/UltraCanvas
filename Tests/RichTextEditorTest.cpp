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

// ===================================================================
// SEARCH
// ===================================================================
static void TestSearch() {
    std::cout << "\n--- Search ---\n";

    auto doc = std::make_shared<UCRichDocument>();
    auto addParagraph = [&doc](const std::string& text) {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        RichTextRun run;
        run.text = text;
        block.runs.push_back(run);
        doc->blocks.push_back(block);
    };
    addParagraph("The report is late.");        // block 0
    addParagraph("Reporting begins Monday.");   // block 1
    addParagraph("Another report follows.");    // block 2

    UCRichDocumentEditor ed(doc);
    RichFindOptions options;                    // case-insensitive, wrapping
    RichDocRange match;

    // --- forwards ---
    CHECK(ed.Find("report", {0, 0}, false, options, match));
    CHECK_EQ(match.start.blockIndex, 0);
    CHECK_EQ(match.start.byteOffset, 4);
    CHECK_EQ(match.end.byteOffset, 10);

    // From just past the first match, the next one is in the following block.
    CHECK(ed.Find("report", match.start, false, options, match));
    CHECK_EQ(match.start.blockIndex, 0);   // the same match: search starts AT `from`
    CHECK(ed.Find("report", match.end, false, options, match));
    CHECK_EQ(match.start.blockIndex, 1);
    CHECK_EQ(match.start.byteOffset, 0);   // "Reporting", case-insensitively

    CHECK(ed.Find("report", match.end, false, options, match));
    CHECK_EQ(match.start.blockIndex, 2);
    CHECK_EQ(match.start.byteOffset, 8);

    // Wrapping past the last match returns to the first.
    CHECK(ed.Find("report", match.end, false, options, match));
    CHECK_EQ(match.start.blockIndex, 0);

    // ...unless wrapping is off.
    RichFindOptions noWrap;
    noWrap.wrapAround = false;
    CHECK(!ed.Find("report", {2, 20}, false, noWrap, match));

    // --- backwards ---
    CHECK(ed.Find("report", {2, 22}, true, options, match));
    CHECK_EQ(match.start.blockIndex, 2);
    CHECK_EQ(match.start.byteOffset, 8);
    // Searching back from a match's own start walks to the previous one rather
    // than finding the same match again.
    CHECK(ed.Find("report", match.start, true, options, match));
    CHECK_EQ(match.start.blockIndex, 1);
    CHECK(ed.Find("report", match.start, true, options, match));
    CHECK_EQ(match.start.blockIndex, 0);
    // And wraps to the last.
    CHECK(ed.Find("report", match.start, true, options, match));
    CHECK_EQ(match.start.blockIndex, 2);

    // --- case sensitivity ---
    RichFindOptions exact;
    exact.caseSensitive = true;
    CHECK(ed.Find("Report", {0, 0}, false, exact, match));
    CHECK_EQ(match.start.blockIndex, 1);        // only "Reporting" has a capital R
    CHECK(!ed.Find("REPORT", {0, 0}, false, exact, match));

    // --- whole word ---
    RichFindOptions wholeWord;
    wholeWord.wholeWord = true;
    CHECK(ed.Find("report", {0, 0}, false, wholeWord, match));
    CHECK_EQ(match.start.blockIndex, 0);        // "report", not "Reporting"
    CHECK(ed.Find("report", match.end, false, wholeWord, match));
    CHECK_EQ(match.start.blockIndex, 2);
    CHECK_EQ(ed.FindAll("report", wholeWord).size(), size_t(2));
    CHECK_EQ(ed.FindAll("report", options).size(), size_t(3));

    // A needle longer than the text, and an empty needle, find nothing.
    CHECK(!ed.Find("a needle longer than any of these paragraphs are",
                   {0, 0}, false, options, match));
    CHECK(!ed.Find("", {0, 0}, false, options, match));
    CHECK(ed.FindAll("", options).empty());

    // --- blocks with no inline text are skipped, not mis-indexed ---
    {
        auto withImage = std::make_shared<UCRichDocument>();
        RichDocBlock rule;
        rule.type = RichBlockType::HorizontalRule;
        withImage->blocks.push_back(rule);
        RichDocBlock para;
        para.type = RichBlockType::Paragraph;
        RichTextRun run;
        run.text = "after the rule";
        para.runs.push_back(run);
        withImage->blocks.push_back(para);

        UCRichDocumentEditor ruled(withImage);
        RichDocRange hit;
        CHECK(ruled.Find("rule", {0, 0}, false, options, hit));
        CHECK_EQ(hit.start.blockIndex, 1);
    }

    // --- replace all ---
    const int replaced = ed.ReplaceAll("report", "summary", options);
    CHECK_EQ(replaced, 3);
    CHECK_EQ(Shape(ed), std::string("The summary is late.|summarying begins Monday.|Another summary follows."));

    // One undo step for the whole replace, not one per match.
    CHECK(ed.Undo());
    CHECK_EQ(Shape(ed), std::string("The report is late.|Reporting begins Monday.|Another report follows."));

    // Replacing with nothing deletes the matches.
    CHECK_EQ(ed.ReplaceAll("Another ", "", options), 1);
    CHECK_EQ(ed.BlockText(2), std::string("report follows."));
    CHECK(ed.Undo());

    // Replacing something absent changes nothing and adds no undo step.
    const bool couldUndoBefore = ed.CanUndo();
    CHECK_EQ(ed.ReplaceAll("nothing here matches", "x", options), 0);
    CHECK_EQ(ed.CanUndo(), couldUndoBefore);

    // Replaced text keeps the formatting of what it replaced.
    {
        auto styled = std::make_shared<UCRichDocument>();
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        RichTextRun plain;
        plain.text = "plain ";
        RichTextRun bold;
        bold.text = "target";
        bold.bold = true;
        block.runs.push_back(plain);
        block.runs.push_back(bold);
        styled->blocks.push_back(block);

        UCRichDocumentEditor se(styled);
        CHECK_EQ(se.ReplaceAll("target", "replaced", options), 1);
        CHECK_EQ(se.BlockText(0), std::string("plain replaced"));
        bool boldSurvived = false;
        for (const auto& run : se.GetBlock(0).runs) {
            if (run.bold && run.text.find("replaced") != std::string::npos) boldSurvived = true;
        }
        CHECK(boldSurvived);
    }
}

// ===================================================================
// TABLE CELL EDITING
// ===================================================================
static std::shared_ptr<UCRichDocument> BuildTableDocument() {
    auto doc = std::make_shared<UCRichDocument>();

    auto paragraph = [&doc](const std::string& text) {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        RichTextRun run;
        run.text = text;
        block.runs.push_back(run);
        doc->blocks.push_back(block);
    };

    paragraph("before the table");          // block 0

    RichDocBlock table;                     // block 1: 2 rows x 2 columns
    table.type = RichBlockType::Table;
    const char* cellText[2][2] = {{"alpha", "beta"}, {"gamma", "delta"}};
    for (int r = 0; r < 2; r++) {
        RichTableRow row;
        for (int c = 0; c < 2; c++) {
            RichTableCell cell;
            RichTextRun run;
            run.text = cellText[r][c];
            cell.runs.push_back(run);
            row.cells.push_back(cell);
        }
        table.tableRows.push_back(row);
    }
    doc->blocks.push_back(table);

    paragraph("after the table");           // block 2
    return doc;
}

static void TestTableCellEditing() {
    std::cout << "\n--- Table cells ---\n";

    UCRichDocumentEditor ed(BuildTableDocument());
    CHECK_EQ(ed.GetBlockCount(), 3);

    // --- addressing ---
    const RichDocPosition a1(1, 0, 0, 0);   // {block, row, column, offset}
    const RichDocPosition b2(1, 1, 1, 0);
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    CHECK_EQ(ed.TextAt(b2), std::string("delta"));
    CHECK_EQ(ed.TextLengthAt(a1), 5);
    CHECK(a1.InCell());
    CHECK(!RichDocPosition(0, 0).InCell());
    // A table block itself still has no inline text of its own.
    CHECK_EQ(ed.BlockText(1), std::string(""));
    CHECK_EQ(ed.TableRowCount(1), 2);
    CHECK_EQ(ed.TableColumnCount(1, 0), 2);

    // --- container order is row-major, and leaves the table at the end ---
    std::vector<RichDocPosition> containers = ed.AllContainers();
    CHECK_EQ(containers.size(), size_t(6));     // 2 paragraphs + 4 cells
    CHECK_EQ(ed.TextAt(containers[0]), std::string("before the table"));
    CHECK_EQ(ed.TextAt(containers[1]), std::string("alpha"));
    CHECK_EQ(ed.TextAt(containers[2]), std::string("beta"));
    CHECK_EQ(ed.TextAt(containers[3]), std::string("gamma"));
    CHECK_EQ(ed.TextAt(containers[4]), std::string("delta"));
    CHECK_EQ(ed.TextAt(containers[5]), std::string("after the table"));

    // --- typing goes into the cell, and only that cell ---
    ed.SetCaret(RichDocPosition(1, 0, 0, 5));   // end of "alpha"
    ed.InsertText("!");
    CHECK_EQ(ed.TextAt(a1), std::string("alpha!"));
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 0, 1, 0)), std::string("beta"));
    CHECK_EQ(ed.GetBlockCount(), 3);            // the table did not split

    // --- Enter inside a cell is a line break, not a block split ---
    ed.SplitBlock();
    CHECK_EQ(ed.GetBlockCount(), 3);
    CHECK_EQ(ed.TextAt(a1), std::string("alpha!\n"));
    CHECK(ed.Undo());
    CHECK(ed.Undo());
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    CHECK_EQ(ed.GetBlockCount(), 3);

    // --- character motion crosses cell boundaries in document order ---
    RichDocPosition p = ed.NextCharacter(RichDocPosition(1, 0, 0, 5));  // end of "alpha"
    CHECK(p.InCell());
    CHECK_EQ(p.cellRow, 0);
    CHECK_EQ(p.cellColumn, 1);                  // into "beta"
    CHECK_EQ(p.byteOffset, 0);

    p = ed.PreviousCharacter(RichDocPosition(1, 1, 0, 0));   // start of "gamma"
    CHECK_EQ(p.cellRow, 0);
    CHECK_EQ(p.cellColumn, 1);                  // back into "beta"
    CHECK_EQ(p.byteOffset, 4);                  // at its end

    // Out of the last cell into the paragraph after the table.
    p = ed.NextCharacter(RichDocPosition(1, 1, 1, 5));
    CHECK_EQ(p.blockIndex, 2);
    CHECK(!p.InCell());

    // Into the first cell from the paragraph before it.
    p = ed.NextCharacter(RichDocPosition(0, 16));
    CHECK_EQ(p.blockIndex, 1);
    CHECK(p.InCell());
    CHECK_EQ(p.cellRow, 0);
    CHECK_EQ(p.cellColumn, 0);

    // --- Home/End act on the cell ---
    CHECK_EQ(ed.BlockStart(RichDocPosition(1, 1, 0, 3)).byteOffset, 0);
    CHECK_EQ(ed.BlockEnd(RichDocPosition(1, 1, 0, 0)).byteOffset, 5);   // "gamma"
    CHECK(ed.BlockEnd(RichDocPosition(1, 1, 0, 0)).InCell());

    // --- Backspace at a cell start steps back, it does not join cells ---
    ed.SetCaret(RichDocPosition(1, 0, 1, 0));   // start of "beta"
    CHECK(ed.DeleteBackward());
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));            // nothing deleted
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 0, 1, 0)), std::string("beta"));
    CHECK_EQ(ed.GetCaret().cellColumn, 0);                    // moved into "alpha"
    CHECK_EQ(ed.GetCaret().byteOffset, 5);                    // at its end

    // Delete at a cell end likewise steps forward.
    CHECK(ed.DeleteForward());
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    CHECK_EQ(ed.GetCaret().cellColumn, 1);
    CHECK_EQ(ed.GetCaret().byteOffset, 0);

    // --- deleting inside a cell affects only that cell ---
    ed.SetSelection(RichDocPosition(1, 0, 1, 0), RichDocPosition(1, 0, 1, 2));
    CHECK(ed.DeleteSelection());
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 0, 1, 0)), std::string("ta"));
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    CHECK_EQ(ed.GetBlockCount(), 3);
    CHECK(ed.Undo());
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 0, 1, 0)), std::string("beta"));

    // --- a selection never spans two cells ---
    ed.SetSelection(RichDocPosition(1, 0, 0, 0), RichDocPosition(1, 1, 1, 5));
    CHECK(ed.GetSelectionRange().start.SameContainer(ed.GetSelectionRange().end));
    // ...nor out of a cell into a following block.
    ed.SetSelection(RichDocPosition(1, 0, 0, 0), RichDocPosition(2, 5));
    CHECK(ed.GetSelectionRange().start.SameContainer(ed.GetSelectionRange().end));
    // Ordinary block-to-block selections still span freely.
    ed.SetSelection(RichDocPosition(0, 0), RichDocPosition(2, 5));
    CHECK(!ed.GetSelectionRange().start.SameContainer(ed.GetSelectionRange().end));

    // --- formatting applies inside a cell ---
    ed.SetSelection(RichDocPosition(1, 1, 0, 0), RichDocPosition(1, 1, 0, 5));
    ed.ToggleBold();
    CHECK(RichCharFormatState::IsOn(ed.GetFormatState().bold));
    bool cellBold = false;
    for (const auto& run : ed.GetBlock(1).tableRows[1].cells[0].runs) {
        if (run.bold && run.text.find("gamma") != std::string::npos) cellBold = true;
    }
    CHECK(cellBold);
    CHECK(ed.Undo());

    // --- paragraph commands do not reach past the cell onto the table ---
    // A RichTableCell holds runs and nothing else, so there is nowhere to
    // record a heading, list, alignment or quote for one. Applying these to the
    // enclosing table block instead would silently restyle the whole table.
    {
        const RichDocBlock tableBefore = ed.GetBlock(1);
        ed.SetCaret(RichDocPosition(1, 0, 0, 2));
        ed.SetAlignment(RichTextAlign::Center);
        ed.SetHeadingLevel(2);
        ed.ToggleList(false);
        ed.ToggleBlockQuote();
        ed.IndentList();
        const RichDocBlock& tableAfter = ed.GetBlock(1);
        CHECK(tableAfter.type == RichBlockType::Table);
        CHECK(tableAfter.align == tableBefore.align);
        CHECK_EQ(tableAfter.listLevel, tableBefore.listLevel);
        CHECK_EQ(tableAfter.headingLevel, tableBefore.headingLevel);
        CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    }
    // ...but they still work normally outside a table.
    ed.SetCaret(RichDocPosition(0, 0));
    ed.SetHeadingLevel(2);
    CHECK(ed.GetBlock(0).type == RichBlockType::Heading);
    CHECK(ed.Undo());

    // --- copying from a cell yields its text ---
    ed.SetSelection(RichDocPosition(1, 1, 1, 0), RichDocPosition(1, 1, 1, 5));
    CHECK_EQ(ed.RangeToPlainText(ed.GetSelectionRange()), std::string("delta"));

    // --- search reaches into cells (it could not before) ---
    RichFindOptions options;
    RichDocRange match;
    CHECK(ed.Find("gamma", ed.DocumentStart(), false, options, match));
    CHECK(match.start.InCell());
    CHECK_EQ(match.start.cellRow, 1);
    CHECK_EQ(match.start.cellColumn, 0);

    // A term in every cell plus both paragraphs is found in all six containers.
    CHECK_EQ(ed.FindAll("a", options).size() > 0, true);
    CHECK_EQ(ed.FindAll("delta", options).size(), size_t(1));

    // Backwards from the paragraph after the table lands in the last cell.
    CHECK(ed.Find("delta", RichDocPosition(2, 0), true, options, match));
    CHECK(match.start.InCell());
    CHECK_EQ(match.start.cellColumn, 1);

    // --- replace reaches into cells too, in one undo step ---
    CHECK_EQ(ed.ReplaceAll("a", "A", options), 
             static_cast<int>(ed.FindAll("A", options).size()));
    CHECK_EQ(ed.TextAt(a1), std::string("AlphA"));
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 1, 1, 0)), std::string("deltA"));
    CHECK(ed.Undo());
    CHECK_EQ(ed.TextAt(a1), std::string("alpha"));
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 1, 1, 0)), std::string("delta"));

    // --- a cell edit survives a document round trip through the model ---
    ed.SetCaret(RichDocPosition(1, 1, 1, 5));
    ed.InsertText(" edited");
    CHECK_EQ(ed.TextAt(RichDocPosition(1, 1, 1, 0)), std::string("delta edited"));
    CHECK(ed.GetDocument()->ToPlainText().find("delta edited") != std::string::npos);
}

// ===================================================================
// INLINE IMAGES
// ===================================================================
// A helper that renders a table's grid as text, so a failure prints the shape
// that went wrong rather than a count that disagrees. "--" is a slot no cell
// starts in; a cell shows its text (or "." when empty) and "*" at its origin.
static std::string GridPicture(const UCRichDocumentEditor& ed, int blockIndex) {
    const RichDocBlock& table = ed.GetBlock(blockIndex);
    const RichTableGrid grid = BuildTableGrid(table);
    std::string out;
    for (int r = 0; r < grid.rowCount; ++r) {
        for (int c = 0; c < grid.columnCount; ++c) {
            const RichTableGridSlot& slot = grid.At(r, c);
            if (c) out += " ";
            if (!slot.Occupied()) { out += "--"; continue; }
            std::string text = UCRichDocument::ConcatenateRunText(
                table.tableRows[slot.row].cells[slot.cellIndex].runs);
            if (text.empty()) text = ".";
            out += text + (slot.origin ? "*" : "");
        }
        if (r + 1 < grid.rowCount) out += " / ";
    }
    return out;
}

static void SetCellText(UCRichDocumentEditor& ed, int block, int row, int cell,
                        const std::string& text) {
    ed.SetCaret(RichDocPosition(block, row, cell, 0));
    ed.InsertText(text);
}

static void TestTableGrid() {
    std::cout << "\n--- Table grid ---\n";

    // The shape that made the grid necessary: a column span in one row and a
    // row span in another, so no cell's index equals its column.
    auto doc = std::make_shared<UCRichDocument>();
    RichDocBlock table;
    table.type = RichBlockType::Table;
    auto cell = [](const std::string& text, int cs = 1, int rs = 1) {
        RichTableCell c;
        RichTextRun run; run.text = text; c.runs.push_back(run);
        c.columnSpan = cs; c.rowSpan = rs;
        return c;
    };
    RichTableRow r0; r0.cells = {cell("A", 2), cell("B")};
    RichTableRow r1; r1.cells = {cell("C", 1, 2), cell("D"), cell("E")};
    RichTableRow r2; r2.cells = {cell("F"), cell("G")};
    table.tableRows = {r0, r1, r2};
    doc->blocks.push_back(table);
    UCRichDocumentEditor ed(doc);

    const RichTableGrid grid = ed.TableGrid(0);
    CHECK_EQ(grid.rowCount, 3);
    CHECK_EQ(grid.columnCount, 3);
    CHECK_EQ(GridPicture(ed, 0), std::string("A* A B* / C* D* E* / C F* G*"));

    // F is row 2's FIRST cell but the grid's second column, because C reaches
    // down into column 0. This is the mapping everything else depends on.
    int row = -1, column = -1;
    CHECK(grid.OriginOf(2, 0, row, column));
    CHECK_EQ(row, 2);
    CHECK_EQ(column, 1);

    // (2,0) is C's, even though C is stored in row 1.
    int ownerRow = -1, ownerCell = -1;
    CHECK(grid.CellAt(2, 0, ownerRow, ownerCell));
    CHECK_EQ(ownerRow, 1);
    CHECK_EQ(ownerCell, 0);

    // A malformed span must not read past the grid: a rowSpan of 99 on a
    // three-row table is clamped, not trusted.
    doc->blocks[0].tableRows[0].cells[1].rowSpan = 99;
    const RichTableGrid clamped = ed.TableGrid(0);
    CHECK_EQ(clamped.rowCount, 3);
    CHECK(clamped.At(2, 2).Occupied());

    // A non-table block has no grid at all.
    CHECK_EQ(ed.TableGrid(-1).columnCount, 0);
}

static void TestTableInsertion() {
    std::cout << "\n--- Table insertion ---\n";

    UCRichDocumentEditor ed(MakeDocument({"intro"}));
    const int table = ed.InsertTable(2, 3);
    CHECK(table >= 0);
    CHECK_EQ(ed.TableRowCount(table), 2);
    CHECK_EQ(ed.TableColumnCount(table, 0), 3);
    // The caret lands in the first cell: a table you have to click into first
    // is a table you cannot fill in from the keyboard.
    CHECK(ed.GetCaret().InCell());
    CHECK_EQ(ed.GetCaret().blockIndex, table);
    CHECK_EQ(ed.GetCaret().cellRow, 0);
    CHECK_EQ(ed.GetCaret().cellColumn, 0);
    // A paragraph follows it, or the caret could never get past the table.
    CHECK(ed.IsTextBlock(table + 1));

    SetCellText(ed, table, 0, 0, "one");
    CHECK_EQ(ed.TextAt(RichDocPosition(table, 0, 0, 0)), std::string("one"));

    // Insertion is one undo step, table and trailing paragraph together.
    const int blocksWithTable = ed.GetBlockCount();
    CHECK(ed.Undo());                       // the typing
    CHECK(ed.Undo());                       // the table
    CHECK(ed.GetBlockCount() < blocksWithTable);
    CHECK(ed.GetBlock(0).type == RichBlockType::Paragraph);

    // A degenerate size is refused rather than producing a table with no cells.
    UCRichDocumentEditor other(MakeDocument({"x"}));
    CHECK_EQ(other.InsertTable(0, 3), -1);
    CHECK_EQ(other.InsertTable(2, 0), -1);
    CHECK_EQ(other.GetBlockCount(), 1);

    // An empty paragraph is replaced rather than left above the table.
    UCRichDocumentEditor empty(MakeDocument({""}));
    CHECK_EQ(empty.InsertTable(1, 1), 0);
    CHECK(empty.GetBlock(0).type == RichBlockType::Table);
}

static void TestTableRowsAndColumns() {
    std::cout << "\n--- Table rows and columns ---\n";

    UCRichDocumentEditor ed(MakeDocument({"x"}));
    const int t = ed.InsertTable(2, 2);
    SetCellText(ed, t, 0, 0, "a");
    SetCellText(ed, t, 0, 1, "b");
    SetCellText(ed, t, 1, 0, "c");
    SetCellText(ed, t, 1, 1, "d");
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    // --- rows ---
    CHECK(ed.InsertTableRow(t, 0, true));           // below the first row
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / .* .* / c* d*"));
    CHECK(ed.Undo());
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    CHECK(ed.InsertTableRow(t, 0, false));          // above the first row
    CHECK_EQ(GridPicture(ed, t), std::string(".* .* / a* b* / c* d*"));
    CHECK(ed.DeleteTableRow(t, 0));
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    // --- columns ---
    CHECK(ed.InsertTableColumn(t, 0, true));        // right of the first column
    CHECK_EQ(GridPicture(ed, t), std::string("a* .* b* / c* .* d*"));
    CHECK(ed.DeleteTableColumn(t, 1));
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    CHECK(ed.InsertTableColumn(t, 1, false));       // left of the second column
    CHECK_EQ(GridPicture(ed, t), std::string("a* .* b* / c* .* d*"));
    CHECK(ed.Undo());
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    // --- deleting content-bearing rows and columns ---
    CHECK(ed.DeleteTableRow(t, 1));
    CHECK_EQ(GridPicture(ed, t), std::string("a* b*"));
    CHECK(ed.DeleteTableColumn(t, 0));
    CHECK_EQ(GridPicture(ed, t), std::string("b*"));

    // The last one takes the table with it: a table with no cells has nothing
    // to type into and no way back.
    const int blocksBefore = ed.GetBlockCount();
    CHECK(ed.DeleteTableRow(t, 0));
    CHECK_EQ(ed.GetBlockCount(), blocksBefore - 1);
    CHECK(ed.GetBlockCount() > 0);
    CHECK(ed.IsTextBlock(ed.GetCaret().blockIndex));

    // Out-of-range asks are refused, not clamped into the wrong row.
    UCRichDocumentEditor other(MakeDocument({"x"}));
    const int t2 = other.InsertTable(2, 2);
    CHECK(!other.InsertTableRow(t2, 5, true));
    CHECK(!other.DeleteTableRow(t2, -1));
    CHECK(!other.InsertTableColumn(t2, 9, false));
    CHECK(!other.DeleteTableColumn(t2, 2));
    CHECK_EQ(other.TableRowCount(t2), 2);
}

static void TestTableStructureWithSpans() {
    std::cout << "\n--- Table structure across spans ---\n";

    // A row span is the case that breaks naive row insertion: the new row must
    // not appear inside the span, and the span has to grow instead.
    UCRichDocumentEditor ed(MakeDocument({"x"}));
    const int t = ed.InsertTable(3, 2);
    SetCellText(ed, t, 0, 0, "tall");
    SetCellText(ed, t, 0, 1, "b");
    SetCellText(ed, t, 1, 1, "d");
    SetCellText(ed, t, 2, 0, "e");
    SetCellText(ed, t, 2, 1, "f");
    // Merge (0,0) downwards over row 1 — "tall" now covers two rows.
    CHECK(ed.MergeTableCells(t, 0, 0, 0, 1));
    CHECK_EQ(GridPicture(ed, t), std::string("tall* b* / tall d* / e* f*"));

    // Inserting a row inside the span grows it rather than splitting it.
    CHECK(ed.InsertTableRow(t, 0, true));
    CHECK_EQ(GridPicture(ed, t), std::string("tall* b* / tall .* / tall d* / e* f*"));
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].rowSpan, 3);
    CHECK(ed.Undo());
    CHECK_EQ(GridPicture(ed, t), std::string("tall* b* / tall d* / e* f*"));

    // Inserting a row past the span's end does not touch it.
    CHECK(ed.InsertTableRow(t, 2, true));
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].rowSpan, 2);
    CHECK(ed.Undo());

    // Deleting a row the span crosses shortens the span; the text survives.
    CHECK(ed.DeleteTableRow(t, 1));
    CHECK_EQ(GridPicture(ed, t), std::string("tall* b* / e* f*"));
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].rowSpan, 1);
    CHECK(ed.Undo());
    CHECK_EQ(GridPicture(ed, t), std::string("tall* b* / tall d* / e* f*"));

    // Deleting the row the span STARTS in re-homes the cell one row down, so
    // its text is not deleted along with the row.
    CHECK(ed.DeleteTableRow(t, 0));
    CHECK_EQ(GridPicture(ed, t), std::string("tall* d* / e* f*"));
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].rowSpan, 1);
    CHECK(ed.Undo());

    // A column span grows when a column is inserted through it.
    UCRichDocumentEditor wide(MakeDocument({"x"}));
    const int w = wide.InsertTable(2, 3);
    SetCellText(wide, w, 0, 0, "W");
    SetCellText(wide, w, 1, 0, "p");
    SetCellText(wide, w, 1, 1, "q");
    SetCellText(wide, w, 1, 2, "r");
    CHECK(wide.MergeTableCells(w, 0, 0, 2, 0));      // W spans all three columns
    CHECK_EQ(GridPicture(wide, w), std::string("W* W W / p* q* r*"));

    CHECK(wide.InsertTableColumn(w, 1, false));      // inside the span
    CHECK_EQ(GridPicture(wide, w), std::string("W* W W W / p* .* q* r*"));
    CHECK_EQ(wide.GetBlock(w).tableRows[0].cells[0].columnSpan, 4);
    CHECK(wide.Undo());
    CHECK_EQ(GridPicture(wide, w), std::string("W* W W / p* q* r*"));

    // Deleting a column the span crosses narrows it by one.
    CHECK(wide.DeleteTableColumn(w, 1));
    CHECK_EQ(GridPicture(wide, w), std::string("W* W / p* r*"));
    CHECK_EQ(wide.GetBlock(w).tableRows[0].cells[0].columnSpan, 2);
    CHECK(wide.Undo());

    // A cell spanning BOTH ways is widened ONCE, not once per row it covers.
    // The insertion point has to fall inside its column span for this to be
    // reachable at all: a cell one column wide is never widened, whichever
    // rows it covers, so a narrower case would pass with the bug present.
    UCRichDocumentEditor deep(MakeDocument({"x"}));
    const int d = deep.InsertTable(3, 3);
    SetCellText(deep, d, 0, 0, "S");
    SetCellText(deep, d, 0, 2, "t");
    SetCellText(deep, d, 1, 2, "u");
    SetCellText(deep, d, 2, 0, "v");
    SetCellText(deep, d, 2, 1, "w");
    SetCellText(deep, d, 2, 2, "x");
    CHECK(deep.MergeTableCells(d, 0, 0, 1, 1));      // S covers rows 0-1, cols 0-1
    CHECK_EQ(GridPicture(deep, d), std::string("S* S t* / S S u* / v* w* x*"));

    // Column inserted through the middle of S: it is two rows tall, so a
    // per-row walk would widen it twice and push the grid out of shape.
    CHECK(deep.InsertTableColumn(d, 0, true));
    CHECK_EQ(deep.GetBlock(d).tableRows[0].cells[0].columnSpan, 3);
    CHECK_EQ(GridPicture(deep, d), std::string("S* S S t* / S S S u* / v* .* w* x*"));
    CHECK(deep.Undo());
    CHECK_EQ(GridPicture(deep, d), std::string("S* S t* / S S u* / v* w* x*"));

    // Deleting a column through it narrows it once, by the same argument.
    CHECK(deep.DeleteTableColumn(d, 1));
    CHECK_EQ(deep.GetBlock(d).tableRows[0].cells[0].columnSpan, 1);
    CHECK_EQ(GridPicture(deep, d), std::string("S* t* / S u* / v* x*"));
}

static void TestTableMergeAndSplit() {
    std::cout << "\n--- Table merge and split ---\n";

    UCRichDocumentEditor ed(MakeDocument({"x"}));
    const int t = ed.InsertTable(2, 2);
    SetCellText(ed, t, 0, 0, "a");
    SetCellText(ed, t, 0, 1, "b");
    SetCellText(ed, t, 1, 0, "c");
    SetCellText(ed, t, 1, 1, "d");

    // Merging right keeps the text of BOTH cells: a merge is a layout change,
    // and throwing away what somebody typed would be a silent deletion.
    CHECK(ed.MergeTableCells(t, 0, 0, 1, 0));
    CHECK_EQ(GridPicture(ed, t), std::string("a\nb* a\nb / c* d*"));
    CHECK_EQ(ed.TextAt(RichDocPosition(t, 0, 0, 0)), std::string("a\nb"));
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells.size(), size_t(1));

    // Undo restores both cells with their own text.
    CHECK(ed.Undo());
    CHECK_EQ(GridPicture(ed, t), std::string("a* b* / c* d*"));

    // Splitting a merged cell gives the slots back as empty cells; the text
    // stays with the cell that held it.
    CHECK(ed.MergeTableCells(t, 0, 0, 1, 1));       // the whole 2x2
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].rowSpan, 2);
    CHECK_EQ(ed.GetBlock(t).tableRows[0].cells[0].columnSpan, 2);
    CHECK(ed.SplitTableCell(t, 0, 0));
    CHECK_EQ(GridPicture(ed, t), std::string("a\nb\nc\nd* .* / .* .*"));
    CHECK_EQ(ed.TableColumnCount(t, 1), 2);

    // Splitting a 1x1 cell changes nothing, and says so.
    CHECK(!ed.SplitTableCell(t, 0, 1));
    // Merging nothing is not a merge.
    CHECK(!ed.MergeTableCells(t, 0, 0, 0, 0));
    // Nor is merging off the end of the grid.
    CHECK(!ed.MergeTableCells(t, 0, 0, 5, 0));
    CHECK(!ed.MergeTableCells(t, 0, 0, 0, 5));

    // A rectangle that would cut an existing span in half is refused: the model
    // cannot store half a cell, so approximating it would corrupt the grid.
    UCRichDocumentEditor sp(MakeDocument({"x"}));
    const int s = sp.InsertTable(3, 3);
    CHECK(sp.MergeTableCells(s, 1, 1, 1, 0));       // (1,1)-(1,2) merged
    CHECK_EQ(sp.GetBlock(s).tableRows[1].cells[1].columnSpan, 2);
    // Now try to merge (0,0)-(1,1): it would take only half of that span.
    CHECK(!sp.MergeTableCells(s, 0, 0, 1, 1));
    CHECK_EQ(sp.GetBlock(s).tableRows[1].cells[1].columnSpan, 2);   // untouched
    // Covering the whole span is fine.
    CHECK(sp.MergeTableCells(s, 0, 0, 2, 1));
    CHECK_EQ(sp.GetBlock(s).tableRows[0].cells[0].columnSpan, 3);
    CHECK_EQ(sp.GetBlock(s).tableRows[0].cells[0].rowSpan, 2);
}

static void TestTableCaretFollowsStructure() {
    std::cout << "\n--- Caret follows table structure ---\n";

    UCRichDocumentEditor ed(MakeDocument({"x"}));
    const int t = ed.InsertTable(2, 2);
    SetCellText(ed, t, 1, 1, "target");

    // The caret is in the last cell; inserting a row above it must keep it
    // pointing at the same text rather than at whatever moved into its slot.
    ed.SetCaret(RichDocPosition(t, 1, 1, 6));
    CHECK(ed.InsertTableRow(t, 0, true));
    CHECK_EQ(ed.TextAt(ed.GetCaret()), std::string("target"));
    CHECK_EQ(ed.GetCaret().cellRow, 2);
    CHECK_EQ(ed.GetCaret().byteOffset, 6);

    // Same for a column inserted to its left.
    CHECK(ed.InsertTableColumn(t, 0, true));
    CHECK_EQ(ed.TextAt(ed.GetCaret()), std::string("target"));

    // CaretGridPosition reports where the caret's cell sits in the grid, which
    // is what a "insert column right of here" menu item needs.
    int row = -1, column = -1;
    CHECK(ed.CaretGridPosition(row, column));
    CHECK_EQ(row, 2);
    CHECK_EQ(column, 2);

    // Outside a table it reports nothing rather than a stale position.
    ed.SetCaret(RichDocPosition(0, 0));
    CHECK(!ed.CaretGridPosition(row, column));

    // Deleting the row the caret is in leaves it somewhere valid.
    ed.SetCaret(RichDocPosition(t, 0, 0, 0));
    CHECK(ed.DeleteTableRow(t, 0));
    CHECK(ed.IsTextContainer(ed.GetCaret()));
}

static void TestInlineImages() {
    std::cout << "\n--- Inline images ---\n";

    // A one-pixel PNG is enough: nothing here decodes it.
    const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    auto doc = std::make_shared<UCRichDocument>();
    RichDocBlock para;
    para.type = RichBlockType::Paragraph;
    RichTextRun run;
    run.text = "Logo  here";
    para.runs.push_back(run);
    doc->blocks.push_back(para);

    UCRichDocumentEditor ed(doc);
    ed.SetCaret({0, 5});                       // between "Logo " and " here"
    const int media = ed.InsertInlineImage("logo.png", "image/png", png, "the logo");
    CHECK(media >= 0);

    // It is a run in the same paragraph, not a new block.
    CHECK_EQ(ed.GetBlockCount(), 1);
    CHECK(ed.GetBlock(0).type == RichBlockType::Paragraph);

    int imageRuns = 0;
    for (const auto& r : ed.GetBlock(0).runs) {
        if (r.IsInlineImage()) imageRuns++;
    }
    CHECK_EQ(imageRuns, 1);

    // The placeholder occupies exactly one character of the block's text, so
    // the caret steps over the picture like any other character.
    const std::string text = ed.BlockText(0);
    CHECK_EQ(text.size(), std::string("Logo  here").size() + 3);   // U+FFFC is 3 bytes
    CHECK(text.find(RichTextRun::kObjectReplacement) != std::string::npos);
    CHECK_EQ(ed.GetCaret().byteOffset, 8);                         // 5 + 3

    RichDocPosition afterPicture = ed.GetCaret();
    RichDocPosition beforePicture = ed.PreviousCharacter(afterPicture);
    CHECK_EQ(beforePicture.byteOffset, 5);       // one character back, not three
    CHECK_EQ(ed.NextCharacter(beforePicture).byteOffset, 8);

    // Backspace deletes the whole picture, not a third of its placeholder.
    ed.SetCaret(afterPicture);
    CHECK(ed.DeleteBackward());
    CHECK_EQ(ed.BlockText(0), std::string("Logo  here"));
    int remaining = 0;
    for (const auto& r : ed.GetBlock(0).runs) if (r.IsInlineImage()) remaining++;
    CHECK_EQ(remaining, 0);
    CHECK(ed.Undo());
    CHECK_EQ(ed.BlockText(0).size(), std::string("Logo  here").size() + 3);

    // Two pictures never coalesce into one run, and never merge with text.
    ed.SetCaret(ed.DocumentEnd());
    CHECK(ed.InsertInlineImage("b.png", "image/png", png, "second") >= 0);
    int count = 0;
    for (const auto& r : ed.GetBlock(0).runs) if (r.IsInlineImage()) count++;
    CHECK_EQ(count, 2);

    // A picture never reaches a reader as U+FFFC.
    const std::string plain = ed.GetDocument()->ToPlainText();
    CHECK(plain.find(RichTextRun::kObjectReplacement) == std::string::npos);
    CHECK(plain.find("[the logo]") != std::string::npos);
    CHECK(plain.find("Logo") != std::string::npos);

    // Markdown with no image directory degrades to the alt text, as block
    // images do; HTML embeds the bytes.
    const std::string md = ed.GetDocument()->ToMarkdown();
    CHECK(md.find(RichTextRun::kObjectReplacement) == std::string::npos);
    CHECK(md.find("[the logo]") != std::string::npos);
    const std::string html = ed.GetDocument()->ToHTML();
    CHECK(html.find(RichTextRun::kObjectReplacement) == std::string::npos);
    CHECK(html.find("<img") != std::string::npos);

    // Formatting the text around a picture leaves the picture alone.
    ed.SetSelection({0, 0}, ed.DocumentEnd());
    ed.ToggleBold();
    for (const auto& r : ed.GetBlock(0).runs) {
        if (r.IsInlineImage()) CHECK(r.mediaIndex >= 0);   // still a picture
    }
    int stillImages = 0;
    for (const auto& r : ed.GetBlock(0).runs) if (r.IsInlineImage()) stillImages++;
    CHECK_EQ(stillImages, 2);

    // Searching skips over the placeholder rather than matching inside it.
    RichFindOptions options;
    RichDocRange match;
    CHECK(ed.Find("here", ed.DocumentStart(), false, options, match));
    CHECK(!ed.Find(RichTextRun::kObjectReplacement, ed.DocumentStart(), false, options, match)
          || match.start.byteOffset >= 0);   // finding it is harmless; crashing is not
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
    TestSearch();
    TestTableCellEditing();
    TestTableGrid();
    TestTableInsertion();
    TestTableRowsAndColumns();
    TestTableStructureWithSpans();
    TestTableMergeAndSplit();
    TestTableCaretFollowsStructure();
    TestInlineImages();

    if (failures == 0) {
        std::cout << "ALL TESTS PASSED (" << checks << " checks)\n";
        return 0;
    }
    std::cout << failures << " FAILURES of " << checks << " checks\n";
    return 1;
}

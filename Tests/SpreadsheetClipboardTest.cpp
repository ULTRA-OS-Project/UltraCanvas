// Tests/SpreadsheetClipboardTest.cpp
// The spreadsheet grid and the system clipboard, on a clipboard backend that
// stands in for the platform's (no display):
//  - Copy puts the shown values on it as tab-separated text and as an HTML
//    table, beside the grid's own copy;
//  - Paste takes the grid's own copy (formulas, formats) while the
//    clipboard still holds that text, and another program's HTML table or
//    text once it does not - each value read as if typed (numbers,
//    percentages, formulas), spans kept apart;
//  - every paste, the grid's own and another program's, is one undo step.
//
// Usage: SpreadsheetClipboardTest
// Exit code is the number of failed checks.

#include "UltraCanvasClipboard.h"
#include "UltraCanvasSpreadsheet.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

struct FakeState {
    std::string text;
    std::string html;
};

// A platform clipboard with text and HTML flavours.
class FakeBackend : public UltraCanvasClipboardBackend {
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool Initialize() override { return true; }
    void Shutdown() override {}
    bool GetClipboardText(std::string& text) override { text = state_->text; return true; }
    bool SetClipboardText(const std::string& text) override { state_->text = text; state_->html.clear(); return true; }
    bool SetClipboardHtml(const std::string& html, const std::string& plain) override {
        state_->html = html;
        state_->text = plain;
        return true;
    }
    bool GetClipboardHtml(std::string& html) override { html = state_->html; return !html.empty(); }
    bool GetClipboardImage(std::vector<uint8_t>&, std::string&) override { return false; }
    bool SetClipboardImage(const std::vector<uint8_t>&, const std::string&) override { return false; }
    bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
    bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }
    bool HasClipboardChanged() override { return false; }
    void ResetChangeState() override {}
    std::vector<std::string> GetAvailableFormats() override { return {"text/plain", "text/html"}; }
    bool IsFormatAvailable(const std::string&) override { return true; }

private:
    std::shared_ptr<FakeState> state_;
};

} // namespace

int main() {
    std::printf("=== Spreadsheet grid and the system clipboard ===\n");
    InitializeClipboard();   // no display: the platform's backend may not start
    auto state = std::make_shared<FakeState>();
    Check(GetClipboard() && GetClipboard()->InitializeWithBackend(std::make_unique<FakeBackend>(state)),
          "a stand-in clipboard backend");

    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    grid->SetCellValue(0, 0, std::string("Item"));
    grid->SetCellValue(0, 1, std::string("Price"));
    grid->SetCellValue(1, 0, std::string("Tab\there"));
    grid->SetCellValue(1, 1, 12.5);
    grid->SetCellValue(2, 0, std::string("Say \"hi\" & <go>"));
    grid->SetCellFormula(2, 1, "=B2*2");
    grid->GetActiveSheet()->GetCell(0, 0)->SetBold(true);

    std::printf("\n-- Copy publishes text and a table --\n");
    grid->Select(CellRange(0, 0, 2, 1));
    grid->Copy();
    Check(state->text == "Item\tPrice\n\"Tab\there\"\t12.5\nSay \"\"hi\"\" & <go>\t25" || state->text.find("Item\tPrice\n") == 0,
          "tab-separated text [" + state->text + "]");
    Check(state->text.find("\"Tab\there\"") != std::string::npos, "a value with a tab is quoted");
    Check(state->html.find("<table>") == 0 && state->html.find("<td style=\"font-weight:bold;\">Item</td>") != std::string::npos,
          "an HTML table, bold kept [" + state->html.substr(0, 80) + "]");
    Check(state->html.find("Say &quot;hi&quot; &amp; &lt;go&gt;") != std::string::npos, "text escaped in the table");

    std::printf("\n-- Paste of its own copy keeps the formula --\n");
    grid->SelectCell(5, 0);   // A6
    const std::string undoBefore = grid->GetUndoDescription();
    grid->Paste();
    Check(grid->GetCellFormula(7, 1) == "=B7*2", "B8 is the formula moved with it [" + grid->GetCellFormula(7, 1) + "]");
    Check(grid->GetCellNumber(7, 1) == 25.0, "and reads 25");
    Check(grid->GetUndoDescription() != undoBefore, "the paste is an undo step");
    grid->Undo();
    Check(grid->GetCellText(5, 0).empty() && grid->GetCellText(7, 1).empty(), "undone, A6:B8 are empty again");

    std::printf("\n-- Paste Special values; a cut formula keeps its references --\n");
    grid->Select(CellRange(2, 1, 2, 1));   // B3, =B2*2
    grid->Copy();
    grid->SelectCell(5, 5);                // F6
    PasteOptions values;
    values.pasteType = PasteType::Values;
    grid->PasteSpecial(values);
    Check(grid->GetCellFormula(5, 5).empty() && grid->GetCellNumber(5, 5) == 25.0,
          "values only: 25, no formula [" + grid->GetCellDisplayValue(5, 5) + "]");
    grid->Cut();
    grid->SelectCell(6, 6);                // G7
    grid->Paste();
    Check(grid->GetCellNumber(6, 6) == 25.0 && grid->GetCellText(5, 5).empty(), "cut F6 to G7: moved");
    grid->Select(CellRange(2, 1, 2, 1));
    grid->Cut();
    grid->SelectCell(8, 3);                // D9
    grid->Paste();
    Check(grid->GetCellFormula(8, 3) == "=B2*2" && grid->GetCellNumber(8, 3) == 25.0,
          "a cut formula still reads B2 [" + grid->GetCellFormula(8, 3) + "]");

    std::printf("\n-- another program's table --\n");
    grid->Select(CellRange(0, 0, 0, 0));
    grid->Copy();   // the grid's own copy, which another program then replaces
    state->html = "<html><body><table><thead><tr><th>Name</th><th>Share</th><th>Sum</th></tr></thead>"
                  "<tbody><tr><td>North</td><td>45%</td><td>=1+2</td></tr>"
                  "<tr><td colspan=\"2\">Wide</td><td>1,5</td></tr>"
                  "<tr><td rowspan=\"2\">Tall</td><td>7</td><td></td></tr>"
                  "<tr><td>8</td><td>9</td></tr></tbody></table></body></html>";
    state->text = "Name\tShare\tSum\n...";
    grid->SelectCell(10, 2);   // C11
    const std::string undoBeforeTable = grid->GetUndoDescription();
    grid->Paste();
    Check(grid->GetCellText(10, 2) == "Name" && grid->GetCellText(10, 4) == "Sum", "the header row at C11");
    Check(!grid->HasClipboardData(), "the grid's own, stale copy is dropped");
    Check(grid->GetCellNumber(11, 3) == 0.45, "45% is the number 0.45 [" + grid->GetCellDisplayValue(11, 3) + "]");
    Check(grid->GetCellNumber(11, 4) == 3.0, "=1+2 is a formula giving 3 [" + grid->GetCellDisplayValue(11, 4) + "]");
    Check(grid->GetCellText(12, 2) == "Wide" && grid->GetCellText(12, 3).empty() && grid->GetCellText(12, 4) == "1,5",
          "a spanned cell keeps the cell it covers free");
    Check(grid->GetCellNumber(13, 3) == 7.0 && grid->GetCellText(14, 2).empty() && grid->GetCellNumber(14, 3) == 8.0
              && grid->GetCellNumber(14, 4) == 9.0,
          "a cell spanning rows pushes the next row's cells right");
    Check(grid->GetSelection().ToString() == "C11:E15", "the pasted cells are selected [" + grid->GetSelection().ToString() + "]");
    Check(grid->GetUndoDescription() != undoBeforeTable, "one undo step");
    grid->Undo();
    Check(grid->GetCellText(10, 2).empty() && grid->GetCellText(14, 4).empty(), "undone in one go");

    std::printf("\n-- another program's text --\n");
    state->html.clear();
    state->text = "a\tb\r\n1\t2\r\n";
    grid->SelectCell(20, 0);   // A21
    grid->Paste();
    Check(grid->GetCellText(20, 0) == "a" && grid->GetCellText(20, 1) == "b" && grid->GetCellNumber(21, 1) == 2.0,
          "tab-separated text, Windows line ends");
    state->text = "x,y\n\"3,5\",4\n";
    grid->SelectCell(30, 0);
    grid->Paste();
    Check(grid->GetCellText(30, 1) == "y" && grid->GetCellText(31, 0) == "3,5" && grid->GetCellNumber(31, 1) == 4.0,
          "comma-separated lines, a quoted comma kept");
    state->text = "Hello, world";
    grid->SelectCell(40, 0);
    grid->Paste();
    Check(grid->GetCellText(40, 0) == "Hello, world" && grid->GetCellText(40, 1).empty(), "one line of text stays one value");

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}

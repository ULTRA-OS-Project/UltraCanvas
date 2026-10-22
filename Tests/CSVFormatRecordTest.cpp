// Tests/CSVFormatRecordTest.cpp
// CSVFormatRecord (UltraCanvasCSVImport.h), the one quoting rule of the CSV
// writers - the spreadsheet's export and UltraDBAdmin's streamed one: what
// is quoted and how, the line endings, "quote all fields", and a record read
// back by CSVParse as the fields that went in. Header-only, no UI.
// Version: 0.1.0
// Author: UltraCanvas Framework
#include "UltraCanvasCSVImport.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what);
    }
}

} // namespace

int main() {
    std::printf("CSVFormatRecord\n");
    CSVExportOptions options;
    Check(CSVFormatRecord({"a", "b", ""}, options) == "a,b,\n", "plain fields stand as they are");
    Check(CSVFormatRecord({"x,y", "say \"hi\"", "two\nlines", "cr\rhere"}, options) ==
              "\"x,y\",\"say \"\"hi\"\"\",\"two\nlines\",\"cr\rhere\"\n",
          "a separator, a quote or a line break is quoted, a quote doubled");
    options.quoteAllFields = true;
    Check(CSVFormatRecord({"a", ""}, options) == "\"a\",\"\"\n", "quote all fields: every one, the empty one too");
    options = CSVExportOptions();
    options.fieldSeparator = '\t';
    options.lineEnding = CSVExportOptions::LineEnding::CRLF;
    Check(CSVFormatRecord({"a,b", "c\td"}, options) == "a,b\t\"c\td\"\r\n", "TSV: the tab is the separator, CRLF ends it");
    options = CSVExportOptions();
    options.textDelimiter = 0;
    Check(CSVFormatRecord({"x,y"}, options) == "x,y\n", "no quote character: nothing is quoted");
    Check(CSVFormatRecord({}, CSVExportOptions()) == "\n", "no fields: an empty record");

    // What is written reads back as what went in.
    const std::vector<std::string> fields = {"Äpfel, \"rot\"", "", "two\nlines", "plain"};
    const CSVGrid grid = CSVParse(CSVFormatRecord(fields, CSVExportOptions()), CSVImportOptions());
    bool same = grid.size() == 1 && grid[0].size() == fields.size();
    for (size_t i = 0; same && i < fields.size(); ++i) same = grid[0][i].text == fields[i];
    Check(same, "CSVParse reads the record back field for field");

    std::printf("%s\n", g_failures == 0 ? "all passed" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}

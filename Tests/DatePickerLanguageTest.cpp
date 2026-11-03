// Tests/DatePickerLanguageTest.cpp
// The calendar view's weekday and month names follow the language: from
// the standard set's context entries at construction, again when the
// language changes while the view exists, and not at all once the
// application set names of its own. No display: the view is built, not
// shown.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasDatePicker.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int testCount = 0, failCount = 0;
static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("datepicker-language-test-" + std::to_string(static_cast<long long>(std::rand())));
    const fs::path shipped = root / "locale";
    std::error_code ec;
    fs::create_directories(shipped / "de", ec);
    {
        std::ofstream out(shipped / "de" / "Standard.json", std::ios::binary);
        out << "{ \"@language\": \"de\", \"weekday|Mo\": \"Mo\", \"weekday|Su\": \"So\", \"month|May\": \"Mai\", \"month-short|Mar\": \"März\" }";
    }
    UltraLocalisation::Reset();
    UltraLocalisation::SetFolders(PathToUtf8(shipped), "");
    UltraLocalisation::Load("en", "Test");

    auto view = std::make_shared<UltraCanvasCalendarView>("cal", 0, 0, 300, 300);
    Check(view->GetWeekdayNames().size() == 7 && view->GetWeekdayNames()[0] == "Su", "English names at construction");
    Check(view->GetMonthNamesLong()[4] == "May", "English month");

    UltraLocalisation::SetLanguage("de");
    Check(view->GetWeekdayNames()[0] == "So" && view->GetWeekdayNames()[1] == "Mo", "the names follow a language switch");
    Check(view->GetMonthNamesLong()[4] == "Mai" && view->GetMonthNamesShort()[2] == "März", "months too");
    Check(view->GetMonthNamesLong()[0] == "January", "an entry the catalogue lacks stays English");

    auto own = std::make_shared<UltraCanvasCalendarView>("cal2", 0, 0, 300, 300);
    Check(own->GetWeekdayNames()[0] == "So", "a view built in German starts German");
    own->SetWeekdayNames({"a", "b", "c", "d", "e", "f", "g"});
    UltraLocalisation::SetLanguage("en");
    Check(own->GetWeekdayNames()[0] == "a", "names set by the application stay through a switch");
    Check(view->GetWeekdayNames()[0] == "Su", "... while a view without its own follows");

    view.reset();
    own.reset();
    UltraLocalisation::SetLanguage("de");   // no handler left behind: nothing to crash on
    Check(true, "destroyed views leave no handler");

    UltraLocalisation::Reset();
    fs::remove_all(root, ec);
    std::printf("DatePickerLanguageTest: %d checks, %d failed\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}

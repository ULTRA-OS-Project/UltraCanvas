// Tests/UltraOfficeTemplateWindowTest.cpp
// UltraOffice's "new from template", in its real window: every shipped
// template (media/templates/office/) is offered, and opening one - the way the
// gallery, the File menu and the Open dialog do (NewFromTemplate), or as a
// file named on the command line or picked from the recent list (OpenPaths) -
// gives a new, untitled document of its kind with the template's content,
// unchanged and without unsaved changes, so Save asks for a name. The
// template file itself is left as it was.
//
// Opens a window: skips without a display, fails instead when
// ULTRACANVAS_TEST_DISPLAY_REQUIRED is set (build.yml's framework window
// tests). The settings folder is a temporary one (XDG_CONFIG_HOME), so the
// user's recent files and templates are neither read nor written.
//
// Usage: UltraOfficeTemplateWindowTest
// Exit code is the number of failed checks.

#include "DisplayTestSupport.h"
#include "UltraOfficeTemplates.h"
#include "UltraOfficeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using UltraOffice::OfficeKind;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

std::string Bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const char* UntitledPrefix(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return "Spreadsheet ";
        case OfficeKind::Presentation: return "Presentation ";
        default: return "Document ";
    }
}

} // namespace

int main() {
    if (!std::getenv("DISPLAY") || !*std::getenv("DISPLAY")) {
        if (DisplayTest::DisplayRequired()) {
            std::printf("FAIL: no DISPLAY, and ULTRACANVAS_TEST_DISPLAY_REQUIRED is set\n");
            return 1;
        }
        std::printf("SKIP: no DISPLAY\n");
        return 0;
    }
    // A settings folder of the test's own: no recent files, no templates of
    // the user's, nothing written into the real one.
    std::error_code ec;
    const fs::path settings = fs::temp_directory_path() / "uc-ultraoffice-template-test";
    fs::remove_all(settings, ec);
    fs::create_directories(settings, ec);
    setenv("XDG_CONFIG_HOME", PathToUtf8(settings).c_str(), 1);   // path-string-ok: test's own ASCII temp folder

    UltraCanvasApplication app;
    if (!app.Initialize("UltraOfficeTemplateWindowTest")) {
        std::printf("%s: the application could not be initialised\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    auto window = std::make_shared<UltraOffice::UltraOfficeWindow>();
    WindowConfig config;
    config.title = "UltraOffice";
    config.width = 1280;
    config.height = 820;
    if (!window->Create(config)) {
        std::printf("%s: the window could not be created\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    window->Initialize({});

    std::printf("=== The templates offered ===\n");
    const std::vector<UltraOffice::OfficeTemplate> offered = UltraOffice::ListTemplates(window->OwnTemplatesFolder());
    int documents = 0, sheets = 0, decks = 0;
    for (const auto& t : offered) {
        documents += t.kind == OfficeKind::Document;
        sheets += t.kind == OfficeKind::Spreadsheet;
        decks += t.kind == OfficeKind::Presentation;
        Check(!t.own && !t.name.empty(), "shipped, with a name: " + t.name);
    }
    Check(offered.size() == 8 && documents == 3 && sheets == 3 && decks == 2,
          "the shipped set: three documents, three spreadsheets, two presentations (" +
              std::to_string(offered.size()) + " found in " + PathToUtf8(UltraOffice::ShippedTemplatesFolder()) + ")");
    Check(window->GetDocuments().empty(), "nothing open before");

    std::printf("\n=== Each opens as a new, untitled document ===\n");
    int expected = 0;
    for (const auto& t : offered) {
        const fs::path file = PathFromUtf8(t.path);
        const std::string before = Bytes(file);
        const auto writtenBefore = fs::last_write_time(file, ec);
        // Through the gallery and the menu ...
        window->NewFromTemplate(t.path);
        ++expected;
        auto open = window->GetDocuments();
        Check(static_cast<int>(open.size()) == expected, t.name + ": a tab opened");
        if (static_cast<int>(open.size()) == expected) {
            const auto& doc = open.back();
            Check(doc.path.empty(), t.name + ": untitled - Save asks for a name");
            Check(doc.name.rfind(UntitledPrefix(t.kind), 0) == 0, t.name + ": named as a new one (" + doc.name + ")");
            Check(doc.kind == t.kind, t.name + ": of its kind");
            Check(!doc.modified, t.name + ": no unsaved changes");
        }
        // ... and as a file opened by name, which is no different.
        window->OpenPaths({t.path});
        ++expected;
        open = window->GetDocuments();
        Check(static_cast<int>(open.size()) == expected && open.back().path.empty(),
              t.name + ": opened by its name, untitled too");
        Check(Bytes(file) == before && fs::last_write_time(file, ec) == writtenBefore, t.name + ": the template is unchanged");
    }

    window->Close();
    fs::remove_all(settings, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}

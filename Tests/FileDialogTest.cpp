// Tests/FileDialogTest.cpp
// The framework file dialog: what it shows, and the name a Save hands back.
//
// Two glyphs were off centre. The Up button was created without a label, so
// it carried UltraCanvasButton's default "Button" and was laid out as an icon
// beside text: the arrow started at the 8px left padding, 6px right of centre
// in the 28px button, and lost its right arm to the button's clip. The view
// buttons are an UltraCanvasSegmentedControl, which put an icon-only
// segment's glyph at the left padding instead of the segment's centre, 3px
// right of centre in a 30px segment.
//
// The hover icon menu (Copy / Cut / Rename / Delete on the hovered file) is a
// file manager's toolbar; a picker turns it off unless the caller asks for it
// (FileDialogConfig::hoverIconMenu).
//
// A Save handed back the name exactly as typed. Applications pick the format
// from the extension, so "photo" with JPEG chosen came back without one and
// each application added its own default - UltraPaint ".png" - after the
// dialog had asked about replacing "photo", not the file actually written.
// The name now takes the chosen type's extension (ApplySaveExtension) before
// the "Replace it?" question.
//
// FileDialogConfig::validateNames and addToRecent were declared and never
// read: a name the file system cannot hold reached the caller's write, and a
// dialog made without UltraCanvasFileLoader added nothing to the recent
// files. The dialog now refuses such a name (InvalidFileNameReason) and adds
// what it accepts to the recent files itself.
//
// The naming rule is checked on its own and always runs; the dialog itself
// is read back from the composited pixels, so that part runs under Xvfb and
// skips - rather than fails - without a display.
// Version: 1.4.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextInput.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(__linux__)
#include <glib.h>   // drives GTK's recent-files store, which saves on the main loop
#endif

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

// The glyphs are drawn black; the button faces, the selected segment's blue
// and the borders are all lighter than this.
bool IsInk(const Color& px) {
    return px.r < 100 && px.g < 100 && px.b < 100;
}

struct InkBox {
    bool found = false;
    int left = 0, top = 0, right = 0, bottom = 0;   // inclusive
    float CenterX() const { return (left + right) / 2.0f; }
    float CenterY() const { return (top + bottom) / 2.0f; }
};

// The dark pixels inside `area` (window coordinates).
InkBox FindInk(UltraCanvasWindow* window, int x0, int y0, int x1, int y1) {
    InkBox box;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px) || !IsInk(px)) continue;
            if (!box.found) {
                box = {true, x, y, x, y};
            } else {
                box.left = std::min(box.left, x);
                box.right = std::max(box.right, x);
                box.top = std::min(box.top, y);
                box.bottom = std::max(box.bottom, y);
            }
        }
    }
    return box;
}

void Report(const char* what, float glyph, float cell) {
    std::cerr << "      " << what << ": glyph centre " << glyph
              << ", cell centre " << cell << std::endl;
}

// The UltraCanvas settings folder (UltraCanvasFileDialogSettings.h), moved
// into `root` so the test never touches a real installation's FileDialog.conf.
void RedirectSettingsFolder(const std::filesystem::path& root) {
    const std::string path = PathToUtf8(root);
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", path.c_str());
#elif defined(__APPLE__)
    setenv("HOME", path.c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", path.c_str(), 1);
    // GTK's recent files (recently-used.xbel), where an accepted file goes.
    setenv("XDG_DATA_HOME", path.c_str(), 1);
#endif
}

// Whether GTK's recent files hold `path` - after running the main loop
// until the store has been written (it is added and saved asynchronously).
// Linux only; elsewhere the answer is not looked for.
bool InRecentFiles(const std::filesystem::path& dataHome, const std::string& path) {
#if defined(__linux__)
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri) return false;
    const std::string wanted = uri;
    g_free(uri);
    for (int pass = 0; pass < 300; ++pass) {   // up to about 3 s
        while (g_main_context_iteration(nullptr, FALSE)) {}
        std::ifstream in(dataHome / "recently-used.xbel");
        const std::string xbel((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (xbel.find(wanted) != std::string::npos) return true;
        g_usleep(10000);
    }
#else
    (void)dataHome; (void)path;
#endif
    return false;
}

// The window as a binary PPM, for a human to look at.
void WritePpm(UltraCanvasWindow* window, int width, int height, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            out.put(static_cast<char>(px.r)).put(static_cast<char>(px.g)).put(static_cast<char>(px.b));
        }
    }
}

// The types UltraPaint offers when saving, cut down.
std::vector<FileFilter> ImageTypes() {
    return {FileFilter("PNG", "png"),
            FileFilter("JPEG", std::vector<std::string>{"jpg", "jpeg"}),
            FileFilter("Archive", "tar.gz"),
            FileFilter("All files", "*")};
}

void CheckName(const std::string& typed, int type, const std::string& expected) {
    const auto types = ImageTypes();
    const std::string got = ApplySaveExtension(typed, types[type], types);
    TEST("\"" + typed + "\" as " + types[type].description + " -> \"" + expected + "\"",
         got == expected);
    if (got != expected) std::cerr << "      got \"" << got << "\"" << std::endl;
}

// Shows a Save dialog, types `name` and presses Enter. Answers the path the
// dialog accepted, or "" when it accepted none (it asked a question instead).
struct SaveRun {
    std::shared_ptr<UltraCanvasFileDialog> dialog;
    UltraCanvasTextInput* nameField = nullptr;
    std::shared_ptr<std::string> accepted = std::make_shared<std::string>();
};

SaveRun ShowSave(const std::string& folder, const std::string& defaultName, int selectedType = 0,
                 const std::string& defaultExtension = "",
                 const std::function<void(FileDialogConfig&)>& adjust = nullptr) {
    FileDialogConfig config;
    config.title = "Save test";
    config.dialogType = FileDialogType::Save;
    config.initialDirectory = folder;
    config.defaultFileName = defaultName;
    config.defaultExtension = defaultExtension;
    config.filters = ImageTypes();
    config.selectedFilterIndex = selectedType;
    config.addToRecent = false;   // the recent-files case turns it on itself
    if (adjust) adjust(config);
    SaveRun run;
    run.dialog = UltraCanvasDialogManager::CreateFileDialog(config);
    auto accepted = run.accepted;
    run.dialog->onFileSelected = [accepted](const std::string& path) { *accepted = path; };
    UltraCanvasDialogManager::ShowDialog(run.dialog, nullptr, nullptr);
    run.nameField = dynamic_cast<UltraCanvasTextInput*>(run.dialog->FindChildById("FileDialogName"));
    return run;
}

void PressEnter(SaveRun& run, const std::string& name) {
    if (!run.nameField || !run.nameField->onEnterPressed) return;
    run.nameField->SetText(name);
    run.nameField->onEnterPressed(name);
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   File Dialog Suite"                     << std::endl;
    std::cerr << "========================================" << std::endl;

    std::cerr << "\n--- Save names ---" << std::endl;
    CheckName("photo", 0, "photo.png");
    CheckName("photo.", 0, "photo.png");
    CheckName("photo.png", 0, "photo.png");
    CheckName("photo.PNG", 0, "photo.PNG");
    CheckName("photo.jpg", 0, "photo.png");                 // another offered type's
    CheckName("photo.jpeg", 1, "photo.jpeg");               // any of the type's own
    CheckName("photo.png", 1, "photo.jpg");                 // the type's first
    CheckName("Report v1.2", 0, "Report v1.2.png");         // not an offered type's
    CheckName("backup.tar.gz", 0, "backup.png");            // the longest that matches
    CheckName("backup", 2, "backup.tar.gz");
    CheckName("anything.xyz", 3, "anything.xyz");           // all files: as typed
    CheckName("noext", 3, "noext");
    CheckName(".png", 0, ".png.png");                       // a name, not an extension
    CheckName("/home/me/my.pictures/photo", 0, "/home/me/my.pictures/photo.png");
    CheckName("C:\\Users\\me\\photo.jpg", 0, "C:\\Users\\me\\photo.png");
    {
        const auto types = ImageTypes();
        TEST("A JPEG name finds the JPEG type", FindFilterForName("holiday.JPEG", types) == 1);
        TEST("A name of no offered type finds none", FindFilterForName("notes.txt", types) == -1);
        TEST("The all-files type is never the one found", FindFilterForName("notes", types) == -1);
    }

    std::cerr << "\n--- Default extension ---" << std::endl;
    TEST("A bare name takes the default", ApplyDefaultExtension("photo", "png") == "photo.png");
    TEST("... dotted or not", ApplyDefaultExtension("photo", ".png") == "photo.png");
    TEST("... without its trailing dot", ApplyDefaultExtension("photo.", "png") == "photo.png");
    TEST("A name with an extension keeps it", ApplyDefaultExtension("notes.txt", "png") == "notes.txt");
    TEST("A dot-file is a bare name", ApplyDefaultExtension(".profile", "png") == ".profile.png");
    TEST("Only the last component counts",
         ApplyDefaultExtension("/home/me/my.pictures/photo", "png") == "/home/me/my.pictures/photo.png");
    TEST("No default changes nothing", ApplyDefaultExtension("photo", "") == "photo");

    std::cerr << "\n--- File names ---" << std::endl;
    {
        auto refused = [](const std::string& name, FileNameRules rules) {
            return !InvalidFileNameReason(name, rules).empty();
        };
        const auto win = FileNameRules::Windows;
        const auto posix = FileNameRules::Posix;
        TEST("An ordinary name is a file name everywhere",
             !refused("Report v1.2.png", win) && !refused("Report v1.2.png", posix));
        TEST("So is one in Thai with an emoji", !refused("รายงาน 📷.png", win) && !refused("รายงาน 📷.png", posix));
        TEST("Empty, \".\" and \"..\" are not", refused("", posix) && refused(".", posix) && refused("..", win));
        TEST("A control character is refused everywhere", refused("a\tb", posix) && refused("a\tb", win));
        TEST("\"a:b\" is a name on POSIX, not on Windows", !refused("a:b", posix) && refused("a:b", win));
        TEST("... as are < > \" | ? * on Windows",
             refused("a<b", win) && refused("a>b", win) && refused("a\"b", win) &&
             refused("a|b", win) && refused("a?b", win) && refused("a*b", win));
        TEST("Windows refuses a trailing dot or space", refused("photo.", win) && refused("photo ", win));
        TEST("... and the device names, with any extension",
             refused("CON", win) && refused("con.txt", win) && refused("Lpt1.png", win) &&
             refused("NUL .txt", win) && !refused("CONSOLE.txt", win) && !refused("COM0", win));
        TEST("... which POSIX allows", !refused("CON", posix) && !refused("photo.", posix));
        TEST("The reason names the character", InvalidFileNameReason("a:b", win) == "contains \":\"");
        const std::string long255(255, 'a'), long256(256, 'a');
        TEST("255 bytes fit on POSIX, 256 do not", !refused(long255, posix) && refused(long256, posix));
        std::string thai;   // 100 Thai letters: 300 bytes, 100 UTF-16 units
        for (int i = 0; i < 100; ++i) thai += "ก";
        TEST("Windows counts characters, POSIX bytes", !refused(thai, win) && refused(thai, posix));
    }

    std::cerr << "\n--- Icon buttons ---" << std::endl;
    {
        // The Up button is made without a label; it used to default to
        // "Button", which laid its arrow out beside text, off centre.
        UltraCanvasButton plain("plain", 0, 0, 28, 28);
        TEST("A button made without a label has none", plain.GetText().empty());
        TEST("... nor one from CreateButton", CreateButton("made", 0, 0, 28, 28)->GetText().empty());
    }

    if (!std::getenv("DISPLAY")) {
        std::cerr << "SKIP: the dialog itself (no DISPLAY)" << std::endl;
        std::cerr << "\n   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
        return failCount == 0 ? 0 : 1;
    }

    // The dialog remembers its view and size in the settings folder: keep
    // the test's copy away from the user's, and start from the defaults.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec) / "FileDialogTest";
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config", ec);
    fs::create_directories(scratch / "listing" / "A folder", ec);
    std::ofstream(scratch / "listing" / "picture.png") << "not really";
    RedirectSettingsFolder(scratch / "config");

    UltraCanvasApplication app;
    if (!app.Initialize("FileDialogTest")) SKIP_ALL("application would not initialise");
    UltraCanvasDialogManager::SetUseNativeDialogs(false);

    FileDialogConfig config;
    config.title = "File dialog layout";
    config.dialogType = FileDialogType::Open;
    config.initialDirectory = PathToUtf8(scratch / "listing");
    auto dialog = UltraCanvasDialogManager::CreateFileDialog(config);
    if (!dialog) SKIP_ALL("the file dialog could not be created");
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
    for (int frame = 0; frame < 4; ++frame) dialog->UpdateAndRender();

    std::cerr << "\n--- Up button ---" << std::endl;
    {
        auto* up = dynamic_cast<UltraCanvasButton*>(dialog->FindChildById("FileDialogUp"));
        TEST("The dialog has its Up button", up != nullptr);
        if (up) {
            const Rect2Df r = up->GetBoundsInWindow();
            // Inside the border and the focus ring.
            const int x0 = static_cast<int>(std::lround(r.x)) + 2;
            const int y0 = static_cast<int>(std::lround(r.y)) + 2;
            const int x1 = static_cast<int>(std::lround(r.x + r.width)) - 2;
            const int y1 = static_cast<int>(std::lround(r.y + r.height)) - 2;
            const InkBox ink = FindInk(dialog.get(), x0, y0, x1, y1);
            TEST("The arrow is drawn", ink.found);
            if (ink.found) {
                const float cx = r.x + r.width / 2.0f - 0.5f;
                const float cy = r.y + r.height / 2.0f - 0.5f;
                Report("horizontal", ink.CenterX(), cx);
                Report("vertical", ink.CenterY(), cy);
                TEST("The arrow is centred across the button", std::fabs(ink.CenterX() - cx) <= 1.0f);
                TEST("The arrow is centred down the button", std::fabs(ink.CenterY() - cy) <= 1.0f);
                // The arrow is 16px wide (24px icon, ink from 4 to 20): none
                // of it is cut off by the button's edge.
                TEST("The arrow is drawn whole", ink.right - ink.left + 1 >= 15);
            }
        }
    }

    std::cerr << "\n--- View buttons ---" << std::endl;
    {
        auto* views = dynamic_cast<UltraCanvasSegmentedControl*>(dialog->FindChildById("FileDialogView"));
        TEST("The dialog has its view buttons", views != nullptr);
        if (views) {
            const Rect2Df r = views->GetBoundsInWindow();
            const int y0 = static_cast<int>(std::lround(r.y)) + 2;
            const int y1 = static_cast<int>(std::lround(r.y + r.height)) - 2;
            const int count = views->GetSegmentCount();
            TEST("There are six views", count == 6);
            TEST("The views share the width equally",
                 views->GetWidthMode() == SegmentWidthMode::Equal);
            // Equal segments: the width shared out in whole pixels after the
            // spacing, starting inside the control's left border.
            const SegmentedControlStyle st = views->GetStyle();
            const int spacing = st.segmentSpacing;
            const int segmentWidth = count > 0
                    ? (static_cast<int>(r.width) - spacing * (count - 1)) / count : 0;
            for (int i = 0; i < count; ++i) {
                const int first = static_cast<int>(st.borderWidth) + i * (segmentWidth + spacing);
                const int last = first + segmentWidth - 1;
                const int left = static_cast<int>(std::lround(r.x)) + first;
                const int right = static_cast<int>(std::lround(r.x)) + last;
                // One pixel in from each side: the separators are not ink.
                const InkBox ink = FindInk(dialog.get(), left + 1, y0, right, y1);
                const std::string name = "View " + std::to_string(i + 1);
                TEST(name + ": its glyph is drawn", ink.found);
                if (!ink.found) continue;
                const float cx = (left + right) / 2.0f;
                const float cy = r.y + r.height / 2.0f - 0.5f;
                Report("horizontal", ink.CenterX(), cx);
                TEST(name + ": its glyph is centred across the segment",
                     std::fabs(ink.CenterX() - cx) <= 1.0f);
                TEST(name + ": its glyph is centred down the segment",
                     std::fabs(ink.CenterY() - cy) <= 1.0f);
            }
        }
    }

    std::cerr << "\n--- Listing ---" << std::endl;
    {
        auto* listing = dynamic_cast<UltraCanvasFilerWidget*>(dialog->FindChildById("FileDialogListing"));
        TEST("The dialog has its listing", listing != nullptr);
        if (listing) {
            TEST("The listing has no hover icon menu", !listing->IsHoverIconMenuEnabled());
        }
    }
    {
        FileDialogConfig withMenu = config;
        withMenu.hoverIconMenu = true;
        auto other = UltraCanvasDialogManager::CreateFileDialog(withMenu);
        auto* listing = other ? dynamic_cast<UltraCanvasFilerWidget*>(other->FindChildById("FileDialogListing"))
                              : nullptr;
        TEST("A caller that asks for the hover icon menu gets it",
             listing && listing->IsHoverIconMenuEnabled());
    }

    std::cerr << "\n--- Save dialog ---" << std::endl;
    {
        const std::string folder = PathToUtf8(scratch / "listing");
        auto expect = [&](const char* leaf) { return PathToUtf8(scratch / "listing" / leaf); };

        SaveRun run = ShowSave(folder, "untitled.png");
        PressEnter(run, "photo");
        TEST("A name typed without an extension gets the chosen type's",
             *run.accepted == expect("photo.png"));

        run = ShowSave(folder, "untitled", 1);
        PressEnter(run, "photo");
        TEST("... the type chosen, not the first in the list", *run.accepted == expect("photo.jpg"));

        run = ShowSave(folder, "photo.jpg", 1);
        PressEnter(run, "photo.png");
        TEST("An extension of another type gives way to the chosen one's",
             *run.accepted == expect("photo.jpg"));

        // All files names no extension: a bare name stays bare, unless the
        // caller gave a default extension (FileDialogConfig::defaultExtension).
        run = ShowSave(folder, "untitled", 3);
        PressEnter(run, "photo");
        TEST("All files leaves a bare name bare", *run.accepted == expect("photo"));
        run = ShowSave(folder, "untitled", 3, "png");
        PressEnter(run, "photo");
        TEST("... unless the dialog has a default extension", *run.accepted == expect("photo.png"));
        run = ShowSave(folder, "untitled", 3, "png");
        PressEnter(run, "notes.txt");
        TEST("... which a name with an extension does not get", *run.accepted == expect("notes.txt"));
        run = ShowSave(folder, "untitled", 1, "png");
        PressEnter(run, "photo");
        TEST("... nor one the chosen type gave one", *run.accepted == expect("photo.jpg"));

        // A name the file system cannot hold is refused, with the dialog left
        // open on it; validateNames off lets it through to the caller.
        const std::string tooLong(300, 'x');
        run = ShowSave(folder, "untitled");
        PressEnter(run, tooLong);
        TEST("A name too long for a file is refused", run.accepted->empty() && run.dialog->IsWindowVisible());
        for (const auto& open : UltraCanvasDialogManager::GetActiveDialogs()) {
            if (open && open != dialog) open->CloseDialog(DialogResult::Cancel);
        }
        run = ShowSave(folder, "untitled", 0, "",
                       [](FileDialogConfig& c) { c.validateNames = false; });
        PressEnter(run, tooLong);
        TEST("... unless validateNames is off", *run.accepted == expect((tooLong + ".png").c_str()));

#if defined(__linux__)
        // What a Save accepts goes on the recent files when addToRecent is
        // on (as UltraCanvasFileLoader sets it), and not when it is off.
        run = ShowSave(folder, "untitled", 0, "",
                       [](FileDialogConfig& c) { c.addToRecent = true; });
        PressEnter(run, "remembered");
        TEST("addToRecent puts the saved file on the recent files",
             InRecentFiles(scratch / "config", expect("remembered.png")));
        run = ShowSave(folder, "untitled");
        PressEnter(run, "forgotten");
        TEST("... and leaves it off when it is off",
             !InRecentFiles(scratch / "config", expect("forgotten.png")));
#endif

        // Through UltraCanvasFileLoader: FileDialogOptions::defaultExtension
        // reaches the framework dialog.
        {
            UltraCanvasDialogManager::SetUseNativeDialogs(false);
            auto saved = std::make_shared<std::string>();
            FileDialogOptions opts;
            opts.SetTitle("Loader save").SetInitialDirectory(folder)
                .AddFilter("All files", std::string("*"))
                .SetDefaultExtension("png").SetRegisterAsRecent(false);
            UltraCanvasFileLoader::SaveFileDialog(opts, [saved](DialogResult r, const std::string& path) {
                if (r == DialogResult::OK) *saved = path;
            });
            const auto open = UltraCanvasDialogManager::GetActiveDialogs();
            auto* shown = open.empty() ? nullptr : dynamic_cast<UltraCanvasFileDialog*>(open.back().get());
            auto* field = shown ? dynamic_cast<UltraCanvasTextInput*>(shown->FindChildById("FileDialogName"))
                                : nullptr;
            TEST("UltraCanvasFileLoader shows the framework Save dialog", field != nullptr);
            if (field && field->onEnterPressed) {
                field->SetText("loader");
                field->onEnterPressed("loader");
            }
            TEST("... which gives a bare name FileDialogOptions' default extension",
                 *saved == expect("loader.png"));
        }

        run = ShowSave(folder, "holiday.jpg");
        TEST("The dialog opens on the type of the name it suggests",
             run.dialog->GetSelectedFilterIndex() == 1);
        PressEnter(run, "holiday.jpg");
        TEST("... so that name is saved as it is", *run.accepted == expect("holiday.jpg"));

        run = ShowSave(folder, "untitled.png");
        auto* types = dynamic_cast<UltraCanvasDropdown*>(run.dialog->FindChildById("FileDialogType"));
        TEST("The Save dialog has its type dropdown", types != nullptr);
        if (types && run.nameField) {
            run.nameField->SetText("photo.png");
            types->SetSelectedIndex(1);
            TEST("Switching the type swaps the name's extension",
                 run.nameField->GetText() == "photo.jpg");
            types->SetSelectedIndex(3);
            TEST("... and All files leaves the name alone", run.nameField->GetText() == "photo.jpg");
        }
        run.dialog->CloseDialog(DialogResult::Cancel);

        // picture.png is in the folder: "picture" means that file, so the
        // dialog asks before replacing it rather than accepting the name.
        run = ShowSave(folder, "untitled.png");
        PressEnter(run, "picture");
        TEST("A name that becomes an existing file is asked about first", run.accepted->empty());
        TEST("... and the name field shows the name that would be written",
             run.nameField && run.nameField->GetText() == "picture.png");
        // The question and the Save dialog under it; the Open dialog stays
        // up for the screenshot below.
        for (const auto& open : UltraCanvasDialogManager::GetActiveDialogs()) {
            if (open && open != dialog) open->CloseDialog(DialogResult::Cancel);
        }
    }

    if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        WritePpm(dialog.get(), static_cast<int>(dialog->GetWidth()), static_cast<int>(dialog->GetHeight()),
                 std::string(shotDir) + "/file-dialog.ppm");
    }

    dialog->CloseDialog(DialogResult::Cancel);
    fs::remove_all(scratch, ec);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}

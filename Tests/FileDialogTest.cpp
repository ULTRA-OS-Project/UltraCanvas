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
// A Save dialog puts the icon of the type being saved in front of the name.
// Dragged onto a folder of the listing or of the folder tree, it opens that
// folder - the one the file is saved in. A Settings button after the name
// edits the type's save settings (UltraCanvasFileLoader's SAVE SETTINGS):
// greyed out for a type that has none, and stored on OK where every
// application's writer reads them - UCRasterDocument::SaveToFile among them.
//
// The picture's size, when the caller gives it, makes a bitmap type's Settings
// start with a Size row: width x height, Keep proportions. The size is for
// that save only and comes back with it (GetSaveImageSize, and the
// SaveFileDialog that reports a FileSaveImageSize), never stored.
//
// The naming rule and the settings store are checked on their own and always
// run; the dialog itself is read back from the composited pixels, so that
// part runs under Xvfb and skips - rather than fails - without a display.
// Version: 1.6.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileDragIcon.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"

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

// The name typed into the field: SetText, then what an edit fires.
void TypeName(SaveRun& run, const std::string& name) {
    if (!run.nameField) return;
    run.nameField->SetText(name);
    if (run.nameField->onTextChanged) run.nameField->onTextChanged(name);
}

// A pointer or key event delivered to `window` through the application's own
// dispatch, as the backend delivers it.
void SendMouse(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
               UCEventType type, const Point2Di& p) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    e.pointerWindow = p;
    e.pointer = p;
    app.DispatchEvent(e);
}

void SendKey(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
             UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.targetWindow = window;
    e.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(e);
}

Point2Di CentreOf(const UltraCanvasUIElement& element) {
    const Rect2Df r = element.GetBoundsInWindow();
    return Point2Di(static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + r.height / 2));
}

// Picks the Save icon up and carries it over `area` in steps until `found`
// says the pointer is where it should be; the button stays down. True when
// it got there.
bool DragIconUntil(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
                   const UltraCanvasFileDragIcon& icon, const Rect2Df& area,
                   const std::function<bool()>& found, Point2Di& at) {
    const Point2Di from = CentreOf(icon);
    SendMouse(app, window, UCEventType::MouseDown, from);
    SendMouse(app, window, UCEventType::MouseMove, Point2Di(from.x + 12, from.y - 12));   // past the slop
    for (int y = static_cast<int>(area.y) + 6; y < static_cast<int>(area.y + area.height) - 6; y += 5) {
        for (int x = static_cast<int>(area.x) + 8; x < static_cast<int>(area.x + area.width) - 8; x += 12) {
            at = Point2Di(x, y);
            SendMouse(app, window, UCEventType::MouseMove, at);
            if (found()) return true;
        }
    }
    return false;
}

// The folder the dialog shows, as its path field has it. (Not the dialog's
// GetCurrentDirectory: windows.h renames that, and new callers are not added.)
std::string ShownFolder(const SaveRun& run) {
    auto* field = run.dialog ? dynamic_cast<UltraCanvasTextInput*>(run.dialog->FindChildById("FileDialogPath"))
                             : nullptr;
    return field ? field->GetText() : std::string();
}

std::string Canonical(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path c = std::filesystem::canonical(p, ec);
    return PathToUtf8(ec ? p : c);
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

    std::cerr << "\n--- Save settings ---" << std::endl;
    {
        // Kept away from the user's SaveSettings.conf.
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path store = fs::temp_directory_path(ec) / "FileDialogTest-settings";
        fs::remove_all(store, ec);
        fs::create_directories(store, ec);
        RedirectSettingsFolder(store);

        TEST("JPEG has save settings, by any of its extensions",
             UltraCanvasFileLoader::HasSaveSettings("jpg") &&
             UltraCanvasFileLoader::HasSaveSettings(".JPEG") &&
             UltraCanvasFileLoader::HasSaveSettings("/home/me/my.pictures/photo.jpe"));
        TEST("BMP is written without options, so it has none",
             !UltraCanvasFileLoader::HasSaveSettings("picture.bmp"));
        TEST("Neither has a type the framework does not write",
             !UltraCanvasFileLoader::HasSaveSettings("notes.txt") &&
             !UltraCanvasFileLoader::HasSaveSettings("noextension"));

        FileSaveSettings jpeg = UltraCanvasFileLoader::GetSaveSettings("photo.jpeg");
        TEST("The settings are kept under the format's first extension", jpeg.format == "jpg");
        TEST("Unset, they are the defaults",
             jpeg.GetInt("quality") == 85 && jpeg.GetBool("optimize") && !jpeg.GetBool("progressive"));
        TEST("... and nothing is stored", !UltraCanvasFileLoader::HasStoredSaveSettings("jpg"));

        FileSaveSettings change;
        change.format = "Joint Photographic";   // a name, not an extension
        TEST("Settings naming no format are refused", !UltraCanvasFileLoader::SetSaveSettings(change));
        change.format = "jpeg";
        change.Set("quality", "150");      // out of range
        change.Set("progressive", "yes");
        change.Set("bogus", "1");          // no such setting
        TEST("Settings are stored by any extension of the format",
             UltraCanvasFileLoader::SetSaveSettings(change));
        jpeg = UltraCanvasFileLoader::GetSaveSettings("jpg");
        TEST("A number is clamped into its range", jpeg.GetInt("quality") == 100);
        TEST("An on/off is read as one", jpeg.GetBool("progressive"));
        TEST("A key the format does not have is dropped", jpeg.values.count("bogus") == 0);
        TEST("... and what was not set keeps its default", jpeg.GetBool("optimize"));
        TEST("The format now has stored settings", UltraCanvasFileLoader::HasStoredSaveSettings("jpe"));
        {
            std::ifstream in(UltraCanvasSettingsFolder() / "SaveSettings.conf");
            const std::string conf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            TEST("They are in SaveSettings.conf, one line each",
                 conf.find("jpg.quality=100\n") != std::string::npos &&
                 conf.find("jpg.progressive=true\n") != std::string::npos);
        }

        UCImageSave::ImageExportOptions options;
        TEST("The image writer's options come from them",
             UltraCanvasFileLoader::GetImageExportOptions("/tmp/out.JPG", options) &&
             options.format == UCImageSaveFormat::JPEG && options.jpeg.quality == 100 &&
             options.jpeg.progressive && options.jpeg.optimizeHuffman);
        TEST("A format without settings still has the writer's format",
             UltraCanvasFileLoader::GetImageExportOptions("x.bmp", options) &&
             options.format == UCImageSaveFormat::BMP);
        TEST("A type the image writer does not write has no options",
             !UltraCanvasFileLoader::GetImageExportOptions("notes.txt", options));
        {
            FileSaveFormat bmp;
            TEST("BMP is a bitmap format with no settings",
                 UltraCanvasFileLoader::FindSaveFormat("x.bmp", bmp) && bmp.bitmap && bmp.settings.empty());
            FileSaveImageSize half;
            half.width = 300;
            half.height = 200;
            TEST("A chosen size is the writer's target size",
                 UltraCanvasFileLoader::GetImageExportOptions("x.png", half, options) &&
                 options.targetWidth == 300 && options.targetHeight == 200 && !options.maintainAspectRatio);
            TEST("... and the picture's own size is none",
                 UltraCanvasFileLoader::GetImageExportOptions("x.png", FileSaveImageSize(), options) &&
                 options.targetWidth == 0 && options.targetHeight == 0);
        }

        // A plugin's own format.
        FileSaveFormat custom;
        custom.name = "Test format";
        custom.extensions = {"TST", ".ts2"};
        custom.settings = {
                FileSaveSettingSpec::Number("level", "Level", 1, 5, 9),     // default clamped to 5
                FileSaveSettingSpec::Choice("mode", "Mode", {{"a", "A"}, {"b", "B"}}, "c"),
                FileSaveSettingSpec::Toggle("bad.key", "Bad", true)};       // a dot cannot be stored
        UltraCanvasFileLoader::RegisterSaveFormat(custom);
        FileSaveFormat found;
        TEST("A registered format is found by any of its extensions",
             UltraCanvasFileLoader::FindSaveFormat("file.TS2", found) && found.Key() == "tst");
        TEST("... with a key that cannot be stored left out",
             found.settings.size() == 2 && !found.FindSetting("bad.key"));
        const FileSaveSettings fresh = UltraCanvasFileLoader::GetSaveSettings("tst");
        TEST("... and defaults that fit their settings",
             fresh.GetInt("level") == 5 && fresh.Get("mode") == "a");

        TEST("Resetting forgets what was stored", UltraCanvasFileLoader::ResetSaveSettings("jpg") &&
             !UltraCanvasFileLoader::HasStoredSaveSettings("jpg") &&
             UltraCanvasFileLoader::GetSaveSettings("jpg").GetInt("quality") == 85);

#ifdef HAS_LIBVIPS
        // The raster writer uses the stored settings when the caller passes
        // none: the same picture at quality 5 and at 100.
        {
            UCRasterDocument doc(64, 64, RasterPixel(30, 60, 90, 255));
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    doc.GetLayer(0)->SetPixel(x, y, RasterPixel(static_cast<uint8_t>(x * 4),
                                                               static_cast<uint8_t>(y * 4),
                                                               static_cast<uint8_t>((x * y) & 255), 255));
            auto sizeAt = [&](int quality) -> uintmax_t {
                FileSaveSettings q;
                q.format = "jpg";
                q.SetInt("quality", quality);
                UltraCanvasFileLoader::SetSaveSettings(q);
                const fs::path file = store / ("q" + std::to_string(quality) + ".jpg");
                std::string error;
                if (!doc.SaveToFile(PathToUtf8(file), error)) return 0;
                std::error_code sec;
                return fs::file_size(file, sec);
            };
            const uintmax_t low = sizeAt(5);
            const uintmax_t high = sizeAt(100);
            TEST("SaveToFile writes with the stored settings", low > 0 && high > low);
            UltraCanvasFileLoader::ResetSaveSettings("jpg");

            // At a chosen size: 64 x 64 written as 32 x 16.
            FileSaveImageSize smaller;
            smaller.width = 32;
            smaller.height = 16;
            UCImageSave::ImageExportOptions sized;
            const std::string file = PathToUtf8(store / "smaller.png");
            std::string error;
            UCRasterDocument reopened;
            TEST("A picture is written at the size chosen for it",
                 UltraCanvasFileLoader::GetImageExportOptions(file, smaller, sized) &&
                 doc.SaveToFile(file, error, &sized) && reopened.LoadFromFile(file, error) &&
                 reopened.GetWidth() == 32 && reopened.GetHeight() == 16);
        }
#endif
        fs::remove_all(store, ec);
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

        // ----- the icon in front of the name, the Settings button after it -----
        {
            run = ShowSave(folder, "untitled.png");
            for (int frame = 0; frame < 3; ++frame) run.dialog->UpdateAndRender();
            auto* icon = dynamic_cast<UltraCanvasFileDragIcon*>(run.dialog->FindChildById("FileDialogFileIcon"));
            auto* settings = dynamic_cast<UltraCanvasButton*>(run.dialog->FindChildById("FileDialogSaveSettings"));
            auto* listing = dynamic_cast<UltraCanvasFilerWidget*>(run.dialog->FindChildById("FileDialogListing"));
            auto* tree = dynamic_cast<UltraCanvasTreeView*>(run.dialog->FindChildById("FileDialogFolders"));
            TEST("A Save dialog has the file icon and the Settings button", icon && settings);
            if (icon && settings && run.nameField) {
                const Rect2Df iconBox = icon->GetBoundsInWindow();
                const Rect2Df nameBox = run.nameField->GetBoundsInWindow();
                const Rect2Df buttonBox = settings->GetBoundsInWindow();
                TEST("... the icon in front of the name, the button after it",
                     iconBox.x + iconBox.width <= nameBox.x && nameBox.x + nameBox.width <= buttonBox.x);
                TEST("... on the name's line", std::fabs(iconBox.y - nameBox.y) <= 1.0f &&
                                               std::fabs(buttonBox.y - nameBox.y) <= 1.0f);
                TEST("The icon is the type being saved", icon->GetFileName() == "untitled.png");
                TEST("PNG has settings, so the button can be used", !settings->IsDisabled());
                TypeName(run, "photo");
                TEST("The icon follows the name, with the type's extension",
                     icon->GetFileName() == "photo.png");
                if (auto* types = dynamic_cast<UltraCanvasDropdown*>(run.dialog->FindChildById("FileDialogType"))) {
                    types->SetSelectedIndex(3);                 // All files
                    TypeName(run, "notes.txt");
                    TEST("A type without settings greys the button out", settings->IsDisabled());
                    TypeName(run, "photo.jpg");
                    TEST("... and a name's own type is the one that counts",
                         !settings->IsDisabled() && icon->GetFileName() == "photo.jpg");
                    TypeName(run, "noext");
                    TEST("... a name of no type has none", settings->IsDisabled());
                    types->SetSelectedIndex(0);
                }
                if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
                    TypeName(run, "holiday photo");
                    for (int frame = 0; frame < 3; ++frame) run.dialog->UpdateAndRender();
                    WritePpm(run.dialog.get(), static_cast<int>(run.dialog->GetWidth()),
                             static_cast<int>(run.dialog->GetHeight()),
                             std::string(shotDir) + "/save-dialog.ppm");
                }
            }

            // Dropped on "A folder" in the listing: that is where the file goes.
            if (icon && listing) {
                const std::string target = Canonical(scratch / "listing" / "A folder");
                Point2Di at;
                const bool over = DragIconUntil(app, run.dialog, *icon, listing->GetBoundsInWindow(),
                        [&]() { return Canonical(PathFromUtf8(listing->GetIncomingDropFolder())) == target; },
                        at);
                TEST("Dragged over the listing, the icon frames the folder under it", over);
                TEST("... while the icon is being dragged", icon->IsDragging());
                if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
                    for (int frame = 0; frame < 2; ++frame) run.dialog->UpdateAndRender();
                    WritePpm(run.dialog.get(), static_cast<int>(run.dialog->GetWidth()),
                             static_cast<int>(run.dialog->GetHeight()),
                             std::string(shotDir) + "/save-dialog-drag.ppm");
                }
                SendMouse(app, run.dialog, UCEventType::MouseUp, at);
                TEST("Dropped on a folder of the listing, the dialog opens it",
                     Canonical(PathFromUtf8(ShownFolder(run))) == target);
                TEST("... and the drag is over", !icon->IsDragging() && listing->GetIncomingDropFolder().empty());
                PressEnter(run, "inside");
                TEST("... so the file is saved there",
                     Canonical(PathFromUtf8(*run.accepted).parent_path()) == target);
            }

            // Escape ends the drag, not the dialog.
            run = ShowSave(folder, "untitled.png");
            for (int frame = 0; frame < 3; ++frame) run.dialog->UpdateAndRender();
            icon = dynamic_cast<UltraCanvasFileDragIcon*>(run.dialog->FindChildById("FileDialogFileIcon"));
            listing = dynamic_cast<UltraCanvasFilerWidget*>(run.dialog->FindChildById("FileDialogListing"));
            tree = dynamic_cast<UltraCanvasTreeView*>(run.dialog->FindChildById("FileDialogFolders"));
            if (icon && listing) {
                const std::string start = ShownFolder(run);
                Point2Di at;
                DragIconUntil(app, run.dialog, *icon, listing->GetBoundsInWindow(),
                              [&]() { return !listing->GetIncomingDropFolder().empty(); }, at);
                SendKey(app, run.dialog, UCKeys::Escape);
                TEST("Escape ends the drag", !icon->IsDragging());
                TEST("... and leaves the dialog open", run.dialog->IsWindowVisible() && run.accepted->empty());
                SendMouse(app, run.dialog, UCEventType::MouseUp, at);
                TEST("... and the release after it drops nothing", ShownFolder(run) == start);
            }
            // Dropped on a row of the folder tree: that folder.
            if (icon && tree) {
                const std::string start = ShownFolder(run);
                Point2Di at;
                const bool over = DragIconUntil(app, run.dialog, *icon, tree->GetBoundsInWindow(),
                        [&]() { return tree->GetDropTargetNode() != nullptr; }, at);
                TEST("Dragged over the folder tree, a folder row takes the drop", over);
                SendMouse(app, run.dialog, UCEventType::MouseUp, at);
                std::error_code dec;
                const std::string now = ShownFolder(run);
                TEST("Dropped on it, the dialog opens that folder",
                     over && now != start && std::filesystem::is_directory(PathFromUtf8(now), dec));
                TEST("... and the row is no longer highlighted", tree->GetDropTargetNode() == nullptr);
            }
            run.dialog->CloseDialog(DialogResult::Cancel);

            // The Settings button: the type's settings, stored on OK.
            run = ShowSave(folder, "untitled.jpg", 1);
            for (int frame = 0; frame < 3; ++frame) run.dialog->UpdateAndRender();
            settings = dynamic_cast<UltraCanvasButton*>(run.dialog->FindChildById("FileDialogSaveSettings"));
            const size_t before = UltraCanvasDialogManager::GetActiveDialogs().size();
            if (settings && settings->onClick) settings->onClick();
            const auto open = UltraCanvasDialogManager::GetActiveDialogs();
            auto top = open.size() > before ? open.back() : nullptr;
            TEST("The Settings button opens the type's settings",
                 top && top->GetDialogTitle() == "JPEG settings");
            if (top) {
                auto* quality = dynamic_cast<UltraCanvasSlider*>(top->FindChildById("SaveSetting_quality"));
                auto* progressive = dynamic_cast<UltraCanvasCheckbox*>(top->FindChildById("SaveSetting_progressive"));
                TEST("... one control per setting", quality && progressive);
                if (!quality || !progressive)
                    std::cerr << "      quality " << top->FindChildById("SaveSetting_quality")
                              << ", progressive " << top->FindChildById("SaveSetting_progressive") << std::endl;
                if (quality && progressive) {
                    TEST("... showing the current values", std::lround(quality->GetValue()) == 85 &&
                                                          !progressive->IsChecked());
                    quality->SetValue(42);
                    progressive->SetChecked(true);
                    if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
                        for (int frame = 0; frame < 3; ++frame) top->UpdateAndRender();
                        WritePpm(top.get(), static_cast<int>(top->GetWidth()),
                                 static_cast<int>(top->GetHeight()),
                                 std::string(shotDir) + "/save-settings.ppm");
                    }
                    top->ActivateButton(DialogButton::OK);
                    const FileSaveSettings stored = UltraCanvasFileLoader::GetSaveSettings("jpg");
                    TEST("OK stores them for every application",
                         stored.GetInt("quality") == 42 && stored.GetBool("progressive"));
                }
            }
            UltraCanvasFileLoader::ResetSaveSettings("jpg");
            run.dialog->CloseDialog(DialogResult::Cancel);

            // The picture's size given: a Size row first, for this save only.
            auto openSettings = [&](SaveRun& r) -> std::shared_ptr<UltraCanvasModalDialog> {
                auto* button = dynamic_cast<UltraCanvasButton*>(r.dialog->FindChildById("FileDialogSaveSettings"));
                const size_t count = UltraCanvasDialogManager::GetActiveDialogs().size();
                if (!button || button->IsDisabled() || !button->onClick) return nullptr;
                button->onClick();
                const auto now = UltraCanvasDialogManager::GetActiveDialogs();
                return now.size() > count ? now.back() : nullptr;
            };
            run = ShowSave(folder, "picture.png", 0, "", [](FileDialogConfig& c) {
                c.imageWidth = 640;
                c.imageHeight = 480;
            });
            TEST("Nothing chosen, the size is the picture's own", run.dialog->GetSaveImageSize().IsOriginal());
            top = openSettings(run);
            auto* sizeW = top ? dynamic_cast<UltraCanvasSpinner*>(top->FindChildById("SaveSizeWidth")) : nullptr;
            auto* sizeH = top ? dynamic_cast<UltraCanvasSpinner*>(top->FindChildById("SaveSizeHeight")) : nullptr;
            auto* keep = top ? dynamic_cast<UltraCanvasCheckbox*>(top->FindChildById("SaveSizeKeepProportions"))
                             : nullptr;
            TEST("With the picture's size, a bitmap's settings have a Size row", sizeW && sizeH && keep);
            if (sizeW && sizeH && keep) {
                TEST("... opened on the picture's size, proportions kept",
                     sizeW->GetIntValue() == 640 && sizeH->GetIntValue() == 480 && keep->IsChecked());
                sizeW->SetValue(320);
                TEST("... and the height follows the width", sizeH->GetIntValue() == 240);
                sizeH->SetValue(120);
                TEST("... and the width the height", sizeW->GetIntValue() == 160);
                if (auto* defaults = dynamic_cast<UltraCanvasButton*>(top->FindChildById("SaveSettingsDefaults"))) {
                    if (defaults->onClick) defaults->onClick();
                }
                TEST("Defaults puts the picture's size back", sizeW->GetIntValue() == 640 &&
                                                              sizeH->GetIntValue() == 480);
                keep->SetChecked(false);
                sizeW->SetValue(100);
                sizeH->SetValue(50);
                TEST("Without Keep proportions the sides are free",
                     sizeW->GetIntValue() == 100 && sizeH->GetIntValue() == 50);
                if (const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
                    for (int frame = 0; frame < 3; ++frame) top->UpdateAndRender();
                    WritePpm(top.get(), static_cast<int>(top->GetWidth()), static_cast<int>(top->GetHeight()),
                             std::string(shotDir) + "/save-settings-size.ppm");
                }
                top->ActivateButton(DialogButton::OK);
                const FileSaveImageSize picked = run.dialog->GetSaveImageSize();
                TEST("OK makes it this save's size", picked.width == 100 && picked.height == 50 &&
                                                     !picked.keepProportions);
                TEST("... without storing it as a setting", !UltraCanvasFileLoader::HasStoredSaveSettings("png") ||
                     UltraCanvasFileLoader::GetSaveSettings("png").values.count("width") == 0);
                TypeName(run, "notes.txt");
                if (auto* types = dynamic_cast<UltraCanvasDropdown*>(run.dialog->FindChildById("FileDialogType")))
                    types->SetSelectedIndex(3);   // All files: a text file is no picture
                TEST("A type that is not a bitmap is saved at no size",
                     run.dialog->GetSaveImageSize().IsOriginal());
                TypeName(run, "picture.bmp");
                TEST("BMP has no settings but its size: the button can be used",
                     !dynamic_cast<UltraCanvasButton*>(run.dialog->FindChildById("FileDialogSaveSettings"))->IsDisabled());
                TEST("... and the size chosen holds for it", run.dialog->GetSaveImageSize().width == 100);
            }
            run.dialog->CloseDialog(DialogResult::Cancel);

            // Without the picture's size: no Size row, and BMP has nothing.
            run = ShowSave(folder, "picture.png");
            top = openSettings(run);
            TEST("Without the picture's size there is no Size row",
                 top && top->FindChildById("SaveSizeWidth") == nullptr);
            if (top) top->CloseDialog(DialogResult::Cancel);
            if (auto* types = dynamic_cast<UltraCanvasDropdown*>(run.dialog->FindChildById("FileDialogType")))
                types->SetSelectedIndex(3);
            TypeName(run, "picture.bmp");
            TEST("... and a bitmap without settings has none to show",
                 dynamic_cast<UltraCanvasButton*>(run.dialog->FindChildById("FileDialogSaveSettings"))->IsDisabled());
            run.dialog->CloseDialog(DialogResult::Cancel);

            // Through UltraCanvasFileLoader: the size comes back with the save.
            {
                auto saved = std::make_shared<std::string>();
                auto savedSize = std::make_shared<FileSaveImageSize>();
                FileDialogOptions opts;
                opts.SetTitle("Loader save").SetInitialDirectory(folder)
                    .AddFilter("PNG", std::string("png"))
                    .SetDefaultFileName("scaled.png").SetImageSize(800, 600).SetRegisterAsRecent(false);
                UltraCanvasFileLoader::SaveFileDialog(opts, [saved, savedSize](DialogResult r, const std::string& path,
                                                                               const FileSaveImageSize& size) {
                    if (r != DialogResult::OK) return;
                    *saved = path;
                    *savedSize = size;
                });
                const auto open = UltraCanvasDialogManager::GetActiveDialogs();
                auto shown = open.empty() ? nullptr : std::dynamic_pointer_cast<UltraCanvasFileDialog>(open.back());
                SaveRun loaderRun;
                loaderRun.dialog = shown;
                loaderRun.nameField = shown ? dynamic_cast<UltraCanvasTextInput*>(shown->FindChildById("FileDialogName"))
                                            : nullptr;
                auto settingsDialog = shown ? openSettings(loaderRun) : nullptr;
                auto* w = settingsDialog ? dynamic_cast<UltraCanvasSpinner*>(settingsDialog->FindChildById("SaveSizeWidth"))
                                         : nullptr;
                TEST("SetImageSize reaches the framework dialog's settings", w != nullptr);
                if (w) {
                    w->SetValue(400);
                    settingsDialog->ActivateButton(DialogButton::OK);
                }
                PressEnter(loaderRun, "scaled");
                TEST("... and the size chosen comes back with the path",
                     *saved == expect("scaled.png") && savedSize->width == 400 && savedSize->height == 300);
            }

            // A caller whose writer does not read the settings turns it off.
            run = ShowSave(folder, "untitled.png", 0, "",
                           [](FileDialogConfig& c) { c.saveSettingsButton = false; });
            TEST("saveSettingsButton off leaves the button out",
                 run.dialog->FindChildById("FileDialogSaveSettings") == nullptr &&
                 run.dialog->FindChildById("FileDialogFileIcon") != nullptr);
            run.dialog->CloseDialog(DialogResult::Cancel);
            TEST("An Open dialog has neither", dialog->FindChildById("FileDialogFileIcon") == nullptr &&
                                               dialog->FindChildById("FileDialogSaveSettings") == nullptr);
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

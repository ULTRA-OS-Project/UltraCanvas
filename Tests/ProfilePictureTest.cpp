// Tests/ProfilePictureTest.cpp
// The profile picture (UltraCanvasProfilePicture.h), without a display:
//
//  - Initials: the first letters of the first and last word, upper-cased,
//    UTF-8 letters whole, punctuation skipped.
//  - The initials' colour: the same key always the same palette colour; the
//    ink is whichever of white and the dark ink reads on it.
//  - The frame: zoom 1 is the shorter side; the square never leaves the
//    picture; a frame out of range comes back inside it.
//  - CropProfilePicture: the square framed, at the size asked for - a
//    two-colour picture cropped to each half comes out all one colour, as
//    JPEG (over white) and as PNG (transparency kept); a file that is no
//    picture is refused with a reason.
//  - CropProfilePictureWithin: a noisy picture brought under a size as JPEG;
//    one that fits kept as asked; one that never fits refused.
//  - The cropper: a drag moves the picture with the pointer (the frame the
//    other way), the wheel zooms within 1 .. the maximum, a key without the
//    focus does nothing; every change is reported.
//  - The picture and the editor row: SetPicturePath shows "Change picture"
//    and Remove, RemovePicture reports "" and goes back to "Choose a picture".
//  - The element registry: "profile-picture" and "profile-picture-editor".
//
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasProfilePicture.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

bool Near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

// A w x h picture, left half `left`, right half `right` (B, G, R, A, straight).
std::string WriteTwoColourPng(const fs::path& dir, const std::string& name, int w, int h, const uint8_t left[4],
                              const uint8_t right[4]) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 4; ++k) px[(static_cast<size_t>(y) * w + x) * 4 + k] = (x < w / 2 ? left : right)[k];
    auto image = UCImage::FromBgra(px.data(), w, h, w * 4, false);
    const std::string path = PathToUtf8(dir / name);
    UCImageSave::ImageExportOptions options;
    options.format = UCImageSaveFormat::PNG;
    const std::string failure = image ? image->Save(path, options) : std::string("no image");
    if (!failure.empty()) std::printf("  (writing %s: %s)\n", name.c_str(), failure.c_str());
    return path;
}

// The pixel at (x, y) of the picture at `path`, as straight B, G, R, A.
struct Pixel {
    int b = 0, g = 0, r = 0, a = 0;
};
Pixel PixelOf(const std::string& path, int x, int y, int* width = nullptr) {
    UCImage::RemoveFromCache(path);
    UCImagePtr image = UCImage::Get(path);
    Pixel p;
    if (!image) return p;
    if (width) *width = image->GetWidth();
    auto pm = image->GetPixmap(image->GetWidth(), image->GetHeight(), ImageFitMode::Fill, 1.0f);
    if (!pm || !pm->IsValid()) return p;
    pm->Flush();
    const uint32_t v = pm->GetPixelData()[static_cast<size_t>(y) * pm->GetRawWidth() + x];
    p.a = (v >> 24) & 0xFF;
    auto un = [&](int c) { return p.a ? c * 255 / p.a : 0; };
    p.b = un(v & 0xFF);
    p.g = un((v >> 8) & 0xFF);
    p.r = un((v >> 16) & 0xFF);
    return p;
}

UCEvent Mouse(UCEventType type, int x, int y) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.pointer = Point2Di(x, y);
    return e;
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ProfilePictureTest");
    const fs::path dir = fs::temp_directory_path() / "uc-profile-picture-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    std::printf("Initials\n");
    Check(ProfilePictureInitials("Erin Mayer") == "EM", "\"Erin Mayer\" -> EM");
    Check(ProfilePictureInitials("erin") == "E", "\"erin\" -> E");
    Check(ProfilePictureInitials("  Ana Lucía Pérez ") == "AP", "first and last word: \"Ana Lucía Pérez\" -> AP");
    Check(ProfilePictureInitials("Zoë") == "Z", "\"Zoë\" -> Z");
    Check(ProfilePictureInitials("\xC3\x89lodie") == "\xC3\x89", "a UTF-8 first letter is taken whole");
    Check(ProfilePictureInitials("(Bob)") == "B", "punctuation before a word is skipped");
    Check(ProfilePictureInitials("").empty() && ProfilePictureInitials(" - ").empty(), "no letters, no initials");

    std::printf("Colours\n");
    const ProfilePictureStyle light = ProfilePictureStyle::Light();
    const Color c1 = light.ColorForKey("user-42");
    Check(c1 == light.ColorForKey("user-42"), "the same key, the same colour");
    bool inPalette = false;
    for (const Color& c : light.initialsPalette) inPalette = inPalette || c == c1;
    Check(inPalette && light.initialsPalette.size() == 8, "the colour is one of the eight");
    int distinct = 0;
    std::vector<Color> seen;
    for (int i = 0; i < 40; ++i) {
        const Color c = light.ColorForKey("person-" + std::to_string(i));
        bool known = false;
        for (const Color& s : seen) known = known || s == c;
        if (!known) {
            seen.push_back(c);
            ++distinct;
        }
    }
    Check(distinct >= 6, "forty keys spread over the palette (" + std::to_string(distinct) + " colours)");
    Check(light.InkOn(Color(43, 132, 79, 255)) == light.initialsInkLight, "white initials on the deep green");
    const ProfilePictureStyle dark = ProfilePictureStyle::Dark();
    Check(dark.InkOn(Color(214, 180, 48, 255)) == dark.initialsInkDark, "dark ink on the light yellow");

    std::printf("The frame\n");
    {
        ProfilePictureFrame f;
        Rect2Dd s = f.SquareIn(400, 200);
        Check(Near(s.width, 200) && Near(s.x, 100) && Near(s.y, 0), "zoom 1: the middle 200 of 400x200");
        f.zoom = 2;
        s = f.SquareIn(400, 200);
        Check(Near(s.width, 100) && Near(s.x, 150) && Near(s.y, 50), "zoom 2: a square half as wide, centred");
        f.centerX = 0.0;
        f.centerY = 1.0;
        s = f.SquareIn(400, 200);
        Check(Near(s.x, 0) && Near(s.y, 100), "a centre at the corner keeps the square inside");
        const ProfilePictureFrame c = ProfilePictureFrame{-3, 7, 0.2}.Clamped(400, 200);
        Check(Near(c.zoom, 1) && Near(c.centerX, 0.25) && Near(c.centerY, 0.5), "out of range comes back inside");
    }

    std::printf("Cropping\n");
    const uint8_t red[4] = {0, 0, 255, 255}, blue[4] = {255, 0, 0, 255};
    const std::string source = WriteTwoColourPng(dir, "two-colours.png", 400, 200, red, blue);
    {
        const std::string out = PathToUtf8(dir / "left.jpg");
        std::string error;
        const bool ok = CropProfilePicture(source, ProfilePictureFrame{0.25, 0.5, 1.0}, 128, out, &error);
        Check(ok, "the left half written as JPEG" + (error.empty() ? std::string() : " (" + error + ")"));
        int width = 0;
        const Pixel corner = PixelOf(out, 2, 2, &width);
        const Pixel edge = PixelOf(out, 125, 64);
        Check(width == 128, "at the size asked for (128)");
        Check(corner.r > 230 && corner.b < 25 && edge.r > 230 && edge.b < 25, "all red, to its right edge");
    }
    {
        const std::string out = PathToUtf8(dir / "right.png");
        const bool ok = CropProfilePicture(source, ProfilePictureFrame{0.75, 0.5, 2.0}, 64, out);
        const Pixel p = PixelOf(out, 32, 32);
        Check(ok && p.b > 250 && p.r < 5, "zoom 2 in the right half: all blue, as PNG");
    }
    {
        const uint8_t clear[4] = {0, 0, 0, 0};
        const std::string logo = WriteTwoColourPng(dir, "logo.png", 100, 100, clear, blue);
        Check(ProfilePictureHasTransparency(logo), "a picture with clear pixels says so");
        Check(!ProfilePictureHasTransparency(source), "an opaque one does not");
        const std::string png = PathToUtf8(dir / "logo-out.png");
        Check(CropProfilePicture(logo, ProfilePictureFrame{}, 100, png), "a transparent picture written as PNG");
        const Pixel left = PixelOf(png, 5, 50), right = PixelOf(png, 95, 50);
        Check(left.a == 0 && right.a == 255 && right.b > 250, "its transparency kept");
        const std::string jpg = PathToUtf8(dir / "logo-out.jpg");
        Check(CropProfilePicture(logo, ProfilePictureFrame{}, 100, jpg), "... and as JPEG");
        const Pixel flat = PixelOf(jpg, 5, 50);
        Check(flat.r > 240 && flat.g > 240 && flat.b > 240, "a JPEG puts the clear part over white");
    }
    {
        const std::string notPicture = PathToUtf8(dir / "notes.txt");
        if (FILE* f = OpenFileUtf8(notPicture, "wb")) {
            std::fputs("not a picture", f);
            std::fclose(f);
        }
        std::string error;
        Check(!CropProfilePicture(notPicture, ProfilePictureFrame{}, 64, PathToUtf8(dir / "x.jpg"), &error) &&
                  !error.empty(),
              "a file that is no picture is refused, with a reason");
    }

    std::printf("Kept under a size\n");
    {
        // Noise: a picture that compresses badly.
        const int side = 300;
        std::vector<uint8_t> px(static_cast<size_t>(side) * side * 4);
        uint32_t seed = 12345;
        for (size_t i = 0; i < px.size(); ++i) {
            seed = seed * 1103515245u + 12345u;
            px[i] = (i % 4 == 3) ? 255 : static_cast<uint8_t>(seed >> 16);
        }
        auto noise = UCImage::FromBgra(px.data(), side, side, side * 4, true);
        const std::string noisy = PathToUtf8(dir / "noise.png");
        UCImageSave::ImageExportOptions png;
        png.format = UCImageSaveFormat::PNG;
        Check(noise && noise->Save(noisy, png).empty(), "a noisy picture to start from");
        const std::string wanted = PathToUtf8(dir / "small.png");
        std::string written, error;
        const bool ok = CropProfilePictureWithin(noisy, ProfilePictureFrame{}, 256, wanted, 12000, &written, &error);
        std::error_code sizeError;
        const auto bytes = fs::file_size(PathFromUtf8(written), sizeError);
        Check(ok && !sizeError && bytes <= 12000, "written within 12000 bytes (" + std::to_string(bytes) + ")");
        Check(written == PathToUtf8(dir / "small.jpg") && !fs::exists(PathFromUtf8(wanted)),
              "as a JPEG, the larger PNG gone");
        std::string first;
        Check(CropProfilePictureWithin(source, ProfilePictureFrame{}, 64, PathToUtf8(dir / "fits.png"), 1 << 20,
                                       &first) &&
                  first == PathToUtf8(dir / "fits.png"),
              "one that fits is kept as asked");
        Check(!CropProfilePictureWithin(noisy, ProfilePictureFrame{}, 256, PathToUtf8(dir / "never.jpg"), 50,
                                        &written, &error) &&
                  !error.empty() && !fs::exists(dir / "never.jpg"),
              "one that never fits is refused, nothing left behind");
    }

    std::printf("The cropper\n");
    {
        auto cropper = std::make_shared<UltraCanvasProfilePictureCropper>("Cropper", 0, 0, 240, 240);
        int reports = 0;
        cropper->onFrameChanged = [&reports](const ProfilePictureFrame&) { ++reports; };
        Check(cropper->SetSourcePath(source), "reads the picture");
        Check(cropper->GetSourceWidth() == 400 && cropper->GetSourceHeight() == 200, "its size");
        const Rect2Df circle = cropper->CircleRect();
        Check(Near(circle.width, 200) && Near(circle.x, 20), "the circle: the element less the inset");
        // 200 picture pixels fill 200 element pixels: a 50-pixel drag left moves the frame 50 pixels right.
        cropper->OnEvent(Mouse(UCEventType::MouseDown, 120, 120));
        cropper->OnEvent(Mouse(UCEventType::MouseMove, 70, 120));
        cropper->OnEvent(Mouse(UCEventType::MouseUp, 70, 120));
        Check(Near(cropper->GetFrame().centerX, 0.625, 1e-6) && reports >= 1,
              "a drag to the left shows more of the right, reported");
        cropper->OnEvent(Mouse(UCEventType::MouseDown, 120, 120));
        cropper->OnEvent(Mouse(UCEventType::MouseMove, 900, 120));
        cropper->OnEvent(Mouse(UCEventType::MouseUp, 900, 120));
        Check(Near(cropper->GetFrame().centerX, 0.25, 1e-6), "dragged past the edge, it stops there");
        UCEvent wheel;
        wheel.type = UCEventType::MouseWheel;
        wheel.wheelDelta = 1;
        for (int i = 0; i < 40; ++i) cropper->OnEvent(wheel);
        Check(Near(cropper->GetZoom(), cropper->GetMaxZoom()), "the wheel zooms in up to the maximum");
        wheel.wheelDelta = -1;
        for (int i = 0; i < 40; ++i) cropper->OnEvent(wheel);
        Check(Near(cropper->GetZoom(), 1.0), "and out down to 1");
        // The arrow keys need the focus, which needs a window: not here.
        UCEvent key;
        key.type = UCEventType::KeyDown;
        key.virtualKey = UCKeys::Left;
        const ProfilePictureFrame before = cropper->GetFrame();
        Check(!cropper->OnEvent(key) && cropper->GetFrame() == before, "a key without the focus does nothing");
        Check(!cropper->SetSourcePath(PathToUtf8(dir / "notes.txt")), "a file that is no picture is refused");
    }

    std::printf("The picture and the editor row\n");
    {
        auto editor = CreateProfilePictureEditor("Editor", 400);
        std::vector<std::string> reported;
        editor->onPictureChanged = [&reported](const std::string& path) { reported.push_back(path); };
        auto* choose = dynamic_cast<UltraCanvasButton*>(editor->FindChildById("Editor-choose"));
        auto* remove = dynamic_cast<UltraCanvasButton*>(editor->FindChildById("Editor-remove"));
        Check(choose && remove, "the row's two buttons");
        if (choose && remove) {
            Check(choose->GetText() == "Choose a picture" && !remove->IsVisible(), "no picture: Choose, no Remove");
            editor->SetPicturePath(source);
            Check(choose->GetText() == "Change picture" && remove->IsVisible(), "a picture: Change and Remove");
            editor->GetPicture()->RemovePicture();
            Check(reported.size() == 1 && reported[0].empty(), "Remove reports \"\"");
            Check(choose->GetText() == "Choose a picture" && !remove->IsVisible(), "and the row is back");
        }
        auto picture = CreateProfilePicture("Picture", 48);
        picture->SetName("Erin Mayer");
        Check(!picture->HasPicture() && !picture->AcceptsFocus(), "not editable: no focus");
        picture->SetEditable(true);
        Check(picture->AcceptsFocus(), "editable: takes the focus");
        picture->SetOutputSize(5000);
        Check(picture->GetOutputSize() == 1024, "the output size is kept within 64 .. 1024");
    }

    std::printf("The element registry\n");
    UltraCanvas_RegisterSelectedWidgets();
    Check(UltraCanvasElementRegistry::Create("profile-picture", "p", 0, 0, 64, 64) != nullptr,
          "profile-picture by name");
    Check(UltraCanvasElementRegistry::Create("profile-picture-editor", "e", 0, 0, 400, 96) != nullptr,
          "profile-picture-editor by name");

    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Tests/ImageAndRadioBehaviourTest.cpp
// The image element, the radio group and the slider's handle shape do what
// their declarations say.
//
// - A failed LoadFromFile shows the error placeholder. SetError replaced the
//   image, and Render looked for the message on the replacement, so the
//   element drew nothing.
// - SetTintColor tints the picture (it was stored and never drawn).
// - LoadFromImage fires onImageLoaded / onImageLoadFailed as LoadFromFile
//   does, and CreateImageFromMemory ignores its format hint as documented.
// - UltraCanvasRadioGroup's onChecked handler held a shared_ptr to its own
//   radio (which was then never freed) and a raw pointer to the group (a
//   crash when the group went first). It now holds the radio raw, and the
//   group takes the handler back when it goes or the radio leaves it.
// - SliderHandleShape is an enum class, so Circle, Square, Triangle and
//   Diamond no longer sit in namespace UltraCanvas.
// - A radio added to a group already checked becomes the selection (the
//   group ignored it, so its dot showed with no selection behind it).
// - Checkboxes, radios and switches take the keyboard focus: Tab reaches
//   them (Space and Enter are checked in a dialog by DialogEscapeTest).
//   IsFocused() without an application no longer crashes.
//
// Runs headless: the image element renders into an offscreen context whose
// pixels are read back; focus moves in a window with nothing native behind
// it; the rest is plain state.
// Version: 1.1.0 - radios added checked, and the toggles' keyboard focus
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlider.h"
#ifdef ULTRACANVAS_ENABLE_GL
#include "UltraCanvasGLSurface.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

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

namespace {

struct Rgba { int r, g, b, a; };

// An offscreen context and the pixmap its pixels are read back through.
struct Canvas {
    UCPixmap pixmap;
    std::unique_ptr<IRenderContext> ctx;
    Canvas(int width, int height) {
        pixmap.Init(width, height);
        ctx = CreateRenderContext(Size2Di(width, height), nullptr);
        ctx->Clear(Color(0, 0, 0, 0));
    }
    // Un-premultiplied colour of one pixel.
    Rgba Sample(int x, int y) {
        ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
        pixmap.MarkDirty();
        pixmap.Flush();
        const uint32_t p = pixmap.GetPixel(x, y);
        int a = (p >> 24) & 0xFF, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        if (a != 0 && a != 255) {
            r = std::min(255, (r * 255 + a / 2) / a);
            g = std::min(255, (g * 255 + a / 2) / a);
            b = std::min(255, (b * 255 + a / 2) / a);
        }
        return { r, g, b, a };
    }
};

bool Near(const Rgba& p, int r, int g, int b, int a, int tol = 6) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol &&
           std::abs(p.b - b) <= tol && std::abs(p.a - a) <= tol;
}

// A 40x20 PNG, solid blue (#2060C0).
const std::vector<uint8_t>& BluePng40x20() {
    static const std::vector<uint8_t> png = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x14,0x08,0x02,0x00,0x00,0x00,0x70,0x24,0xE8,
        0xEC,0x00,0x00,0x00,0x24,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x50,0x48,0x38,0x30,
        0x20,0x88,0x61,0xD4,0xE2,0x51,0x8B,0x47,0x2D,0x1E,0xB5,0x78,0xD4,0xE2,0x51,0x8B,
        0x47,0x2D,0x1E,0xB5,0x78,0xE4,0x58,0x0C,0x00,0xBA,0x4F,0xE8,0x2E,0x68,0xAC,0xCD,
        0xD0,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82 };
    return png;
}

// Renders an 80x20 element showing the blue picture centred (x 20..60) into
// an 80x20 canvas.
void RenderBlue(Canvas& canvas, const std::function<void(UltraCanvasImageElement&)>& setup) {
    auto element = CreateImageElement("blue", 0, 0, 80, 20);
    element->LoadFromImage(UCImageRaster::LoadFromMemory(BluePng40x20()));
    element->SetFitMode(ImageFitMode::Contain);
    setup(*element);
    element->Render(canvas.ctx.get(), Rect2Df(0, 0, 80, 20));
}

void TestErrorPlaceholder() {
    auto element = CreateImageElement("broken", 0, 0, 60, 40);
    std::string reported;
    element->onImageLoadFailed = [&reported](const std::string& why) { reported = why; };
    const bool loaded = element->LoadFromFile("/nonexistent-ultracanvas-dir/missing.png");
    TEST("a missing file does not load", !loaded);
    TEST("and says why", !element->GetLastError().empty() && reported == element->GetLastError());

    Canvas canvas(60, 40);
    element->Render(canvas.ctx.get(), Rect2Df(0, 0, 60, 40));
    // The placeholder fills the element with the error colour (200, 200, 200).
    TEST("a failed load draws the error placeholder", Near(canvas.Sample(4, 4), 200, 200, 200, 255));
}

void TestTint() {
    Canvas plain(80, 20);
    RenderBlue(plain, [](UltraCanvasImageElement&) {});
    TEST("untinted: the picture is drawn as it is", Near(plain.Sample(40, 10), 32, 96, 192, 255));

    Canvas red(80, 20);
    RenderBlue(red, [](UltraCanvasImageElement& e) { e.SetTintColor(Color(255, 0, 0)); });
    TEST("a red tint keeps only the picture's red", Near(red.Sample(40, 10), 32, 0, 0, 255));
    TEST("the tint stays inside the picture", red.Sample(5, 10).a == 0 && red.Sample(75, 10).a == 0);

    Canvas half(80, 20);
    RenderBlue(half, [](UltraCanvasImageElement& e) { e.SetTintColor(Color(0, 0, 0, 128)); });
    TEST("the tint's alpha sets its strength", Near(half.Sample(40, 10), 16, 48, 96, 255));

    Canvas faded(80, 20);
    RenderBlue(faded, [](UltraCanvasImageElement& e) {
        e.SetTintColor(Color(255, 0, 0));
        e.SetOpacity(0.5f);
    });
    TEST("a tinted picture keeps its opacity (applied once)", Near(faded.Sample(40, 10), 32, 0, 0, 128));
}

void TestLoadFromImage() {
    auto element = CreateImageElement("memory", 0, 0, 40, 20);
    int loaded = 0;
    std::string failed;
    element->onImageLoaded = [&loaded]() { ++loaded; };
    element->onImageLoadFailed = [&failed](const std::string& why) { failed = why; };

    element->LoadFromFile("/nonexistent-ultracanvas-dir/missing.png");
    failed.clear();
    const bool ok = element->LoadFromImage(UCImageRaster::LoadFromMemory(BluePng40x20()));
    TEST("LoadFromImage of a decoded picture succeeds", ok);
    TEST("and fires onImageLoaded, as LoadFromFile does", loaded == 1);
    TEST("and clears the last load's error", element->GetLastError().empty());

    const std::vector<uint8_t> garbage = { 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e' };
    const bool bad = element->LoadFromImage(UCImageRaster::LoadFromMemory(garbage));
    TEST("LoadFromImage of bytes that did not decode fails", !bad);
    TEST("and fires onImageLoadFailed with the reason",
         !failed.empty() && failed == element->GetLastError() && loaded == 1);

    failed.clear();
    const bool cleared = element->LoadFromImage(std::make_shared<UCImage>());
    TEST("an empty image clears the element, firing neither callback",
         !cleared && failed.empty() && loaded == 1 && element->GetLastError().empty() &&
         element->GetImageSize().x == 0);

    // The loader detects the format from the bytes: a wrong hint is ignored.
    auto hinted = CreateImageFromMemory("hinted", 0, 0, 40, 20, BluePng40x20(), UCImageLoadFormat::JPEG);
    TEST("CreateImageFromMemory ignores its format hint",
         hinted->GetImageSize().x == 40 && hinted->GetImageSize().y == 20);
    auto broken = CreateImageFromMemory("broken-bytes", 0, 0, 40, 20, garbage);
    TEST("CreateImageFromMemory of bad bytes reports why", !broken->GetLastError().empty());
}

void TestRadioGroup() {
    // The handler must not own its radio.
    auto a = std::make_shared<UltraCanvasRadio>("a", "A");
    auto b = std::make_shared<UltraCanvasRadio>("b", "B");
    std::weak_ptr<UltraCanvasRadio> weakA = a;
    std::shared_ptr<UltraCanvasRadio> changedTo;
    {
        UltraCanvasRadioGroup group;
        group.AddRadioButton(a);
        group.AddRadioButton(b);
        group.onSelectionChanged = [&changedTo](std::shared_ptr<UltraCanvasRadio> r) { changedTo = r; };
        TEST("only the test and the group own a radio (the handler holds it raw)", a.use_count() == 2);

        b->SetChecked(true);   // what a click does
        TEST("checking a radio selects it in the group", group.GetSelectedButton() == b && changedTo == b);
        a->SetChecked(true);
        TEST("checking another moves the selection", group.GetSelectedButton() == a && !b->IsChecked());
    }
    TEST("the group gone first: it took its handlers back", !a->onChecked && !b->onChecked);
    if (!a->onChecked) {   // the old handler would call into the destroyed group
        b->SetChecked(true);
        TEST("a radio checked after its group is gone just checks itself", b->IsChecked());
    }
    changedTo.reset();
    a.reset();
    TEST("a radio is freed once its last owner drops it", weakA.expired());

    // RemoveRadioButton takes the handler back; one the application set stays.
    auto c = std::make_shared<UltraCanvasRadio>("c", "C");
    auto d = std::make_shared<UltraCanvasRadio>("d", "D");
    int ownCalls = 0;
    {
        UltraCanvasRadioGroup group;
        group.AddRadioButton(c);
        group.AddRadioButton(d);
        group.RemoveRadioButton(c);
        TEST("a removed radio loses the group's handler", !c->onChecked);
        d->onChecked = [&ownCalls]() { ++ownCalls; };
    }
    d->SetChecked(true);
    TEST("an onChecked the application set survives the group", ownCalls == 1);

    // A moved group takes its radios' clicks with it.
    auto e = std::make_shared<UltraCanvasRadio>("e", "E");
    auto f = std::make_shared<UltraCanvasRadio>("f", "F");
    auto source = std::make_unique<UltraCanvasRadioGroup>();
    source->AddRadioButton(e);
    source->AddRadioButton(f);
    UltraCanvasRadioGroup moved(std::move(*source));
    f->SetChecked(true);
    TEST("a moved group receives its radios' clicks", moved.GetSelectedButton() == f);
    source.reset();
    TEST("and keeps them when the moved-from group goes", static_cast<bool>(e->onChecked));
}

void TestRadioAddedChecked() {
    auto low = std::make_shared<UltraCanvasRadio>("low", "Low");
    auto high = std::make_shared<UltraCanvasRadio>("high", "High");
    high->SetChecked(true);   // created as the initial choice
    UltraCanvasRadioGroup group;
    int reports = 0;
    group.onSelectionChanged = [&reports](std::shared_ptr<UltraCanvasRadio>) { ++reports; };
    group.AddRadioButton(low);
    group.AddRadioButton(high);
    TEST("a radio added already checked is the group's selection", group.GetSelectedButton() == high);
    TEST("and building the group reports no change", reports == 0);

    auto other = std::make_shared<UltraCanvasRadio>("other", "Other");
    other->SetChecked(true);
    group.AddRadioButton(other);
    TEST("a second checked radio added wins, and the first is cleared",
         group.GetSelectedButton() == other && other->IsChecked() && !high->IsChecked());
    low->SetChecked(true);   // what a click does
    TEST("the adopted selection moves on a click like any other",
         group.GetSelectedButton() == low && !other->IsChecked() && reports == 1);
}

// A window with nothing native behind it; the focus moves in it as in a real
// one.
class FocusWindow : public UltraCanvasWindowBase {
public:
    FocusWindow() { SetBounds(Rect2Df(0, 0, 400, 300)); }
    void Show() override {}
    void Hide() override {}
    void RaiseAndFocus() override {}
    void SetWindowTitle(const std::string&) override {}
    void SetWindowIcon(const std::string&) override {}
    void SetWindowPosition(int, int) override {}
    void SetWindowSize(int, int) override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void SetFullscreen(bool) override {}
    void SetResizable(bool) override {}
    void GetScreenSize(int& width, int& height) const override { width = 400; height = 300; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}

protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

UCEvent Key(UCKeys key) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    return event;
}

void TestTogglesTakeFocus() {
    auto win = std::make_shared<FocusWindow>();
    auto box = std::make_shared<UltraCanvasCheckbox>("box", 10, 10, 140, 24, "Remember me");
    auto radio = std::make_shared<UltraCanvasRadio>("radio", "Option");
    auto toggle = UltraCanvasSwitch::Create("switch", 10, 70, "Dark mode", false);
    auto skipped = std::make_shared<UltraCanvasCheckbox>("skipped", 10, 100, 140, 24, "Not in Tab order");
    skipped->SetAcceptsFocus(false);
    win->AddChild(box);
    win->AddChild(radio);
    win->AddChild(toggle);
    win->AddChild(skipped);

    TEST("a checkbox, a radio and a switch accept the focus",
         box->CanReceiveFocus() && radio->CanReceiveFocus() && toggle->CanReceiveFocus());
    win->FocusNextElement();
    TEST("Tab reaches the checkbox", win->GetFocusedElement() == box.get());
    win->FocusNextElement();
    TEST("then the radio", win->GetFocusedElement() == radio.get());
    win->FocusNextElement();
    TEST("then the switch", win->GetFocusedElement() == toggle.get());
    win->FocusNextElement();
    TEST("SetAcceptsFocus(false) keeps a toggle out of the Tab order",
         win->GetFocusedElement() != skipped.get() && !skipped->SetFocus(true));
    TEST("SetFocus(true) gives a checkbox its window's focus",
         box->SetFocus(true) && win->GetFocusedElement() == box.get());
    // With no application no window has the focus, so no element is focused
    // (this used to call through a null application pointer). Space and
    // Enter on a focused toggle are checked in a real dialog by
    // DialogEscapeTest.
    TEST("IsFocused() is false, not a crash, without an application", !box->IsFocused());
    TEST("so Space does nothing to it", !box->OnEvent(Key(UCKeys::Space)) && !box->IsChecked());
}

void TestSliderHandleShape() {
    TEST("SliderHandleShape is a scoped enum (no implicit int, no names in UltraCanvas)",
         (std::is_enum_v<SliderHandleShape> && !std::is_convertible_v<SliderHandleShape, int>));
    SliderVisualStyle style;
    TEST("the default handle is a circle", style.handleShape == SliderHandleShape::Circle);
}

void TestGLSurfacePlacement() {
#ifdef ULTRACANVAS_ENABLE_GL
    // The config constructor's (0, 0) default leaves the surface in the flow,
    // as the other constructors' (-1, -1) does: only a positive origin pins it.
    UltraCanvasGLSurface configured{ GLSurfaceConfig{} };
    UltraCanvasGLSurface sized("sized", 300, 300);
    TEST("a GL surface built from a config stays in the layout flow",
         configured.layoutItem.positionType == sized.layoutItem.positionType &&
         configured.layoutItem.positionType != CSSLayout::PositionType::AbsoluteUI);
#endif
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ImageAndRadioBehaviourTest");

    TestErrorPlaceholder();
    TestTint();
    TestLoadFromImage();
    TestRadioGroup();
    TestRadioAddedChecked();
    TestTogglesTakeFocus();
    TestSliderHandleShape();
    TestGLSurfacePlacement();

    std::cerr << "\nImageAndRadioBehaviourTest: " << testCount << " checks, " << failCount << " failures"
              << std::endl;
    return failCount == 0 ? 0 : 1;
}

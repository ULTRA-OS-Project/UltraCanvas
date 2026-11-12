// OS/WASM/runtime/examples/hello-app.cpp
// An UltraCanvas application written against the ordinary C++ API, built as
// a WebAssembly side module: it carries only its own code and runs on the
// UltraCanvas a browser already has, from the UltraCanvas extension or the
// site's copy of the runtime (Docs/UltraCanvas/UltraCanvasWasmRuntime.md).
//
// Labels, a button with a click counter, a checkbox and an element of the
// app's own - a subclass of UltraCanvasUIElement that draws itself and takes
// clicks - so the whole C++ surface is crossed: factories, shared_ptr,
// std::function callbacks, virtual overrides, and an exception thrown in the
// runtime and caught here. Every event is also printed,
// which is how runtime-test.mjs follows it. The positions are fixed, so the
// test knows where to click.
//
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace UltraCanvas;

namespace {

// An element the app defines itself: a disc that changes colour on a click.
class ColourDot : public UltraCanvasUIElement {
public:
    explicit ColourDot(const std::string& id) : UltraCanvasUIElement(id, 20, 230, 80, 80) {}

    std::function<void(int)> onPicked;

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        const Rect2Df box = GetLocalBounds();
        static const Color kColours[] = {Color(220, 60, 50), Color(40, 150, 90), Color(50, 100, 210)};
        ctx->DrawFilledCircle(Point2Dd(box.x + box.width / 2, box.y + box.height / 2), box.width / 2 - 2,
                              kColours[colour_ % 3], Color(30, 30, 30), 2.0f);
    }

    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::MouseDown) {
            ++colour_;
            RequestRedraw();
            if (onPicked) onPicked(colour_ % 3);
            return true;
        }
        return UltraCanvasUIElement::OnEvent(event);
    }

private:
    int colour_ = 0;
};

} // namespace

int main(int, char* argv[]) {
    // Static, as every UltraCanvas application in a browser: Run() never returns.
    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    WindowConfig config;
    config.title = "Hello from an UltraCanvas app";
    config.width = 640;
    config.height = 420;
    auto window = CreateWindow(config);
    if (!window) return 1;

    auto title = std::make_shared<UltraCanvasLabel>("Title", 20, 24, 600, 32);
    title->SetText("An UltraCanvas C++ app with only its own code in it");
    title->SetFontSize(18);
    window->AddChild(title);

    auto counter = std::make_shared<UltraCanvasLabel>("Counter", 240, 98, 360, 24);
    counter->SetText("Clicked 0 times");
    window->AddChild(counter);

    auto button = CreateButton("Count", 20, 90, 200, 40, "Count");
    auto clicks = std::make_shared<int>(0);
    UltraCanvasLabel* counterView = counter.get();
    button->SetOnClick([counterView, clicks]() {
        ++*clicks;
        counterView->SetText("Clicked " + std::to_string(*clicks) + (*clicks == 1 ? " time" : " times"));
        std::printf("[hello-app] clicked %d\n", *clicks);
    });
    window->AddChild(button);

    auto state = std::make_shared<UltraCanvasLabel>("State", 240, 152, 360, 24);
    state->SetText("The box is not ticked");
    window->AddChild(state);
    auto box = UltraCanvasCheckbox::CreateCheckbox("Tick", 20, 150, 200, 28, "Tick me", false);
    UltraCanvasLabel* stateView = state.get();
    box->onStateChanged = [stateView](CheckedState, CheckedState now) {
        const bool ticked = now == CheckedState::Checked;
        stateView->SetText(ticked ? "The box is ticked" : "The box is not ticked");
        std::printf("[hello-app] ticked %d\n", ticked ? 1 : 0);
    };
    window->AddChild(box);

    auto dot = std::make_shared<ColourDot>("Dot");
    auto picked = std::make_shared<UltraCanvasLabel>("Picked", 120, 258, 480, 24);
    picked->SetText("Click the dot: it is drawn by the app's own element");
    UltraCanvasLabel* pickedView = picked.get();
    dot->onPicked = [pickedView](int colour) {
        static const char* const kNames[] = {"red", "green", "blue"};
        pickedView->SetText(std::string("The dot is ") + kNames[colour]);
        std::printf("[hello-app] dot %s\n", kNames[colour]);
    };
    window->AddChild(dot);
    window->AddChild(picked);

    // An exception thrown inside the runtime (its C++ library) and caught
    // here, in the app: the two modules share one exception mechanism.
    try {
        (void)std::stoi("not a number");
    } catch (const std::invalid_argument&) {
        std::printf("[hello-app] caught an exception thrown in the runtime\n");
    }
    std::printf("[hello-app] started\n");
    window->Show();
    app.Run();   // never returns in the browser
    return 0;
}

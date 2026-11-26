# Getting started

This page is the short path to a running window. The long, step-by-step
version - toolchains per platform, packaging, working with an AI assistant -
is [`Docs/GettingStarted.md`](../GettingStarted.md).

## 1. Build the tree

UltraCanvas is built with CMake (≥ 3.16) and a C++20 compiler. On
Ubuntu / Debian the dependencies are listed under *Building and testing* in
[`AGENTS.md`](../../AGENTS.md); on macOS run `scripts/macos-deps.sh`; on
Windows use `build-win.cmd` (vcpkg is configured by `vcpkg.json`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

Then run the demo, which shows every element live:

```bash
./build/UltraCanvasDemo
```

## 2. A first application

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraLocalisation/UltraLocalisation.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

using namespace UltraCanvas;

#ifdef __linux__
static void OnSignal(int) { UltraCanvasApplicationBase::RequestExitFromSignal(); }
#endif

int main(int argc, char* argv[]) {
#ifdef __linux__
    XInitThreads();
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    if (!app.Initialize("HelloUltraCanvas")) return 1;

    WindowConfig config;
    config.title  = "Hello UltraCanvas 1.0.0";
    config.width  = 480;
    config.height = 200;
    auto window = CreateWindow(config);

    // A column: the label, the field, the button.
    window->SetPadding(16);
    window->layout.SetFlexColumn().SetFlexGap(10);

    auto greeting = std::make_shared<UltraCanvasLabel>("Greeting", Tr("What is your name?"));
    auto name     = std::make_shared<UltraCanvasTextInput>("Name");
    name->SetPlaceholder(Tr("Your name"));
    auto hello    = std::make_shared<UltraCanvasButton>("SayHello", Tr("Say hello"));

    // Callbacks capture elements raw - never a shared_ptr (see Events and callbacks).
    UltraCanvasLabel*     greetingRaw = greeting.get();
    UltraCanvasTextInput* nameRaw     = name.get();
    hello->onClick = [greetingRaw, nameRaw]() {
        greetingRaw->SetText(TrF("Hello, {0}!", nameRaw->GetText()));
    };
    name->onEnterPressed = [hello = hello.get()](const std::string&) {
        hello->onClick();
        return true;
    };

    window->AddChild(greeting);
    window->AddChild(name);
    window->AddChild(hello);
    window->Show();

    app.Run();
    return 0;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
```

What happens here:

1. `UltraCanvasApplication` is the platform's application. `Initialize`
   connects to the display and loads settings, `Run` is the event loop and
   returns when the last window closes or `RequestExit()` is called.
2. `CreateWindow(WindowConfig)` makes a top-level window. A window is a
   container, so it has a `layout` and takes children with `AddChild`.
3. The elements are created with no coordinates - `(identifier, text)` - so
   the layout engine sizes them by their content and stacks them in the
   window's flex column. See [Layout](Layout.md).
4. `onClick` and `onEnterPressed` are [callbacks](Events-and-Callbacks.md);
   every element's callbacks are listed on its page (for example
   [Button](Elements/UltraCanvasButton.md#callbacks),
   [TextInput](Elements/UltraCanvasTextInput.md#callbacks)).
5. Every visible text goes through `Tr` / `TrF` so it can be translated.

## 3. Add it to the build

For an application inside this repository, add a directory under `Apps/`
with a `CMakeLists.txt` that links the framework:

```cmake
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [-] HelloUltraCanvas - SKIPPED (framework not built)")
    return()
endif()

add_executable(HelloUltraCanvas main.cpp)
target_include_directories(HelloUltraCanvas PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(HelloUltraCanvas PRIVATE ${ULTRACANVAS_LIBRARY})
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(HelloUltraCanvas PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(HelloUltraCanvas PRIVATE cxx_std_20)
```

and register it in the root `CMakeLists.txt` and
`cmake/UltraCanvasVersion.cmake` as described in Step 4 of
[`Docs/GettingStarted.md`](../GettingStarted.md) (the version then comes from
the application's changelog). An application outside the tree uses the SDK -
see [`Docs/UltraCanvasSDK.md`](../UltraCanvasSDK.md).

## 4. Where to go next

- Pick elements from the [element groups](Home.md#element-groups) or the
  [element index](Element-Index.md).
- Learn the [core concepts](Core-Concepts.md): identifiers, ownership, the
  element tree.
- Lay out a real window: [Layout](Layout.md).
- Draw your own element: [Rendering](Rendering.md).
- Open and save files: [Files, text and localisation](Files-Text-And-Localisation.md).

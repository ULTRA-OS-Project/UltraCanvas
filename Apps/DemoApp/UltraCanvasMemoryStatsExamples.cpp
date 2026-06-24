// Apps/DemoApp/UltraCanvasMemoryStatsExamples.cpp
// Demonstrates the scoped memory-usage statistics: an embedded MemoryStats
// panel widget plus controls that allocate/free memory under demo "tabs" so
// the per-scope / per-category numbers update live. Also opens the full
// task-manager dialog.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasMemoryStats.h"
#include "UltraCanvasMemoryStatsPanel.h"
#include "../dialogs/UltraCanvasMemoryStatsDialog.h"
#include <vector>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMemoryStatsExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("MemoryStatsExamples", 0, 0, 1000, 800);

        // ===== TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("MemStatsTitle", 20, 10, 760, 35);
        title->SetText("Memory Usage Statistics");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("MemStatsSubtitle", 20, 45, 800, 40);
        subtitle->SetText("Allocate memory under demo 'tabs' (scopes) and watch the per-scope / "
                          "per-category usage update. This is the same data a browser task "
                          "manager would show.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        subtitle->SetWrap(TextWrap::WrapWord);
        container->AddChild(subtitle);

        // ===== EMBEDDED MEMORYSTATS WIDGET =====
        // Lives on the right; the controls on the left feed it.
        auto panel = CreateMemoryStatsPanel("MemStatsDemoPanel", 470, 95, 500, 560);
        panel->SetShowTypeBreakdown(true);
        panel->SetBorders(1.0f, Color(200, 200, 200, 255));
        container->AddChild(panel);

        // ===== DEMO SCOPES + RETAINED ALLOCATIONS =====
        // Static so they persist across navigation and across button clicks.
        static MemoryScopeId tabA = MemoryStats::CreateScope("Demo Tab A");
        static MemoryScopeId tabB = MemoryStats::CreateScope("Demo Tab B");
        static std::vector<TrackedBuffer> imagesA, imagesB;
        static std::vector<std::shared_ptr<UltraCanvasUIElement>> widgetsA;

        // ===== CONTROLS =====
        int y = 95;
        auto note = std::make_shared<UltraCanvasLabel>("MemStatsNote", 20, y, 430, 24);
        note->SetText("Use the buttons; the panel on the right refreshes live.");
        note->SetFontSize(11);
        note->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(note);
        y += 36;

        // Allocate a 4 MB "image" in Tab A.
        auto allocImgA = CreateButton("MemAllocImgA", 20, y, 280, 32, "Allocate 4 MB image -> Tab A");
        allocImgA->onClick = [panel]() {
            MemoryScopeGuard g(tabA);
            imagesA.emplace_back(MemoryCategory::ImagePixels, 4 * 1024 * 1024, "DemoBitmap");
            panel->RefreshNow();
        };
        container->AddChild(allocImgA);
        y += 40;

        // Allocate ~100 UI widgets in Tab A.
        auto allocUiA = CreateButton("MemAllocUiA", 20, y, 280, 32, "Allocate 100 widgets -> Tab A");
        allocUiA->onClick = [panel]() {
            MemoryScopeGuard g(tabA);
            for (int i = 0; i < 100; ++i)
                widgetsA.push_back(UltraCanvasUIElementFactory::Create<UltraCanvasLabel>("demoWidget"));
            panel->RefreshNow();
        };
        container->AddChild(allocUiA);
        y += 40;

        // Allocate a 2 MB "image" in Tab B.
        auto allocImgB = CreateButton("MemAllocImgB", 20, y, 280, 32, "Allocate 2 MB image -> Tab B");
        allocImgB->onClick = [panel]() {
            MemoryScopeGuard g(tabB);
            imagesB.emplace_back(MemoryCategory::ImagePixels, 2 * 1024 * 1024, "DemoBitmap");
            panel->RefreshNow();
        };
        container->AddChild(allocImgB);
        y += 40;

        // Free everything in Tab A.
        auto freeA = CreateButton("MemFreeA", 20, y, 280, 32, "Free all in Tab A");
        freeA->onClick = [panel]() {
            imagesA.clear();
            widgetsA.clear();
            panel->RefreshNow();
        };
        container->AddChild(freeA);
        y += 40;

        // Free everything in Tab B.
        auto freeB = CreateButton("MemFreeB", 20, y, 280, 32, "Free all in Tab B");
        freeB->onClick = [panel]() {
            imagesB.clear();
            panel->RefreshNow();
        };
        container->AddChild(freeB);
        y += 52;

        // Open the standalone task-manager dialog.
        auto openDialog = CreateButton("MemOpenDialog", 20, y, 280, 34, "Open Task Manager dialog...");
        openDialog->SetStyle(ButtonStyles::PrimaryStyle());
        openDialog->onClick = [this]() {
            MemoryStatsDialogConfig cfg;
            cfg.title = "Task Manager - Memory";
            cfg.showTypeBreakdown = true;
            UltraCanvasMemoryStatsDialog::Show(mainWindow.get(), cfg);
        };
        container->AddChild(openDialog);

        return container;
    }

} // namespace UltraCanvas

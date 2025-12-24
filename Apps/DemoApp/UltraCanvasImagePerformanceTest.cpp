// Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
// Image performance testing demonstration - load, decompress, and render benchmarks
// Version: 1.0.0
// Last Modified: 2025-12-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasImage.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <thread>

namespace UltraCanvas {

// ===== IMAGE PERFORMANCE TEST SCREEN =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateImagePerformanceTest() {
        // Main container
        auto mainContainer = std::make_shared<UltraCanvasContainer>("ImagePerfTestMain", 9000, 0, 0, 1000, 810);
        mainContainer->SetBackgroundColor(Color(255, 255, 255, 255));

        int currentY = 10;

        // ===== TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("PerfTestTitle", 9001, 20, currentY, 600, 35);
        title->SetText("Image Performance Test");
        title->SetFontSize(20);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 80, 120, 255));
        mainContainer->AddChild(title);
        currentY += 45;

        // ===== DESCRIPTION =====
        auto description = std::make_shared<UltraCanvasLabel>("PerfTestDesc", 9002, 20, currentY, 940, 50);
        description->SetText(
                "Measure image decompression and rendering performance. Select an image and test mode, "
                "then click 'Start Test' to run a 10-second benchmark counting how many times the image can be processed.");
        description->SetFontSize(12);
        description->SetWordWrap(true);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);
        currentY += 60;

        // ===== CONTROLS SECTION =====
        auto controlsContainer = std::make_shared<UltraCanvasContainer>("PerfControls", 9010, 20, currentY, 940, 50);
        controlsContainer->SetBackgroundColor(Color(245, 248, 252, 255));
        controlsContainer->SetBorders(1.0f, Color(200, 210, 220, 255));
        controlsContainer->SetPadding(0,0,0,5);
        auto controlsLayout = CreateHBoxLayout(controlsContainer.get());
        controlsLayout->SetSpacing(5);
        controlsLayout->SetDefaultCrossAxisAlignment(LayoutAlignment::Center);

        // Image Selection Dropdown
        auto imageLabel = std::make_shared<UltraCanvasLabel>("ImageLabel", 9011, 10, 15, 50, 20);
        imageLabel->SetText("Image:");
        imageLabel->SetFontSize(12);
        imageLabel->SetAutoResize(true);
        controlsLayout->AddUIElement(imageLabel);

        auto imageDropdown = std::make_shared<UltraCanvasDropdown>("ImageDropdown", 9012, 65, 10, 240, 30);
        imageDropdown->AddItem("PNG sample (161Kb)", "media/images/alphachannel.png");
        imageDropdown->AddItem("JPEG sample (36Kb)", "media/images/alphachannel.jpg");
        imageDropdown->AddItem("WEBP sample (41Kb)", "media/images/alphachannel.webp");
        imageDropdown->AddItem("BMP sample (523Kb)", "media/images/alphachannel.bmp");
        imageDropdown->AddItem("Icon, small PNG (633b)", "media/images/test_small.png");
        imageDropdown->AddItem("Icon, small JPG (1197b)", "media/images/test_small.jpg");
        imageDropdown->AddItem("Icon, small WEBP (410b)", "media/images/test_small.webp");
        imageDropdown->AddItem("Icon, small BMP (2690b)", "media/images/test_small.bmp");
        imageDropdown->SetSelectedIndex(0);
        controlsLayout->AddUIElement(imageDropdown);
        controlsLayout->AddSpacing(5);

        // Test Mode Dropdown
        auto modeLabel = std::make_shared<UltraCanvasLabel>("ModeLabel", 9013, 280, 15, 80, 20);
        modeLabel->SetText("Test Mode:");
        modeLabel->SetFontSize(12);
        modeLabel->SetAutoResize(true);
        controlsLayout->AddUIElement(modeLabel);

        auto modeDropdown = std::make_shared<UltraCanvasDropdown>("ModeDropdown", 9014, 365, 10, 250, 30);
        modeDropdown->AddItem("Load, Decompress, Draw", "full");
        modeDropdown->AddItem("Decompress, Draw", "decompress_draw");
        modeDropdown->AddItem("Draw cached pixmap only", "draw_only");
        modeDropdown->SetSelectedIndex(0);
        controlsLayout->AddUIElement(modeDropdown);
        controlsLayout->AddSpacing(5);

        // Start Test Button
        auto startButton = std::make_shared<UltraCanvasButton>("StartTestBtn", 9015, 590, 10, 120, 30);
        startButton->SetText("Start Test");
        controlsLayout->AddUIElement(startButton);

        // Stop Test Button (initially hidden/disabled)
//        auto stopButton = std::make_shared<UltraCanvasButton>("StopTestBtn", 9016, 720, 10, 100, 30);
//        stopButton->SetText("Stop");
//        stopButton->SetEnabled(false);
//        controlsContainer->AddChild(stopButton);
//
        mainContainer->AddChild(controlsContainer);
        currentY += 60;

        // ===== IMAGE DISPLAY AREA =====
        auto imageContainer = std::make_shared<UltraCanvasContainer>("ImageDisplayArea", 9020, 20, currentY, 500, 400);
        imageContainer->SetBackgroundColor(Color(240, 240, 240, 255));
        imageContainer->SetBorders(2.0f, Color(180, 190, 200, 255));

        auto imageElement = std::make_shared<UltraCanvasImageElement>("PerfTestImage", 9021, 10, 10, 480, 380);
        imageElement->SetFitMode(ImageFitMode::ScaleDown);
        imageElement->LoadFromFile("media/images/alphachannel.png");
        imageContainer->AddChild(imageElement);

        mainContainer->AddChild(imageContainer);

        // ===== RESULTS PANEL =====
        auto resultsContainer = std::make_shared<UltraCanvasContainer>("ResultsPanel", 9030, 540, currentY, 420, 400);
        resultsContainer->SetBackgroundColor(Color(250, 252, 255, 255));
        resultsContainer->SetBorders(2.0f, Color(180, 190, 200, 255));

        // Results Title
        auto resultsTitle = std::make_shared<UltraCanvasLabel>("ResultsTitle", 9031, 15, 10, 200, 25);
        resultsTitle->SetText("Test Results");
        resultsTitle->SetFontSize(16);
        resultsTitle->SetFontWeight(FontWeight::Bold);
        resultsTitle->SetTextColor(Color(40, 80, 120, 255));
        resultsContainer->AddChild(resultsTitle);

        // Status Label
        auto statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 9032, 15, 45, 390, 25);
        statusLabel->SetText("Status: Ready");
        statusLabel->SetFontSize(13);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        resultsContainer->AddChild(statusLabel);

        // Progress Label
        auto progressLabel = std::make_shared<UltraCanvasLabel>("ProgressLabel", 9033, 15, 75, 390, 25);
        progressLabel->SetText("Progress: 0 / 10 seconds");
        progressLabel->SetFontSize(13);
        progressLabel->SetTextColor(Color(60, 60, 60, 255));
        resultsContainer->AddChild(progressLabel);

        // Iteration Count Label
        auto iterationLabel = std::make_shared<UltraCanvasLabel>("IterationLabel", 9034, 15, 110, 390, 30);
        iterationLabel->SetText("Iterations: 0");
        iterationLabel->SetFontSize(18);
        iterationLabel->SetFontWeight(FontWeight::Bold);
        iterationLabel->SetTextColor(Color(0, 120, 60, 255));
        resultsContainer->AddChild(iterationLabel);

        // Detailed Results
        auto detailsLabel = std::make_shared<UltraCanvasLabel>("DetailsLabel", 9035, 10, 140, 395, 240);
        detailsLabel->SetText(
                "Detailed Results:\n"
                "─────────────────────────\n"
                "Test Duration:     --\n"
                "Total Iterations:  --\n"
                "Avg Time/Iteration: --\n"
                "Iterations/Second: --\n"
                "─────────────────────────\n"
                "\nClick 'Start Test' to begin.");
        detailsLabel->SetFontSize(12);
        detailsLabel->SetTextColor(Color(60, 60, 60, 255));
        detailsLabel->SetBackgroundColor(Color(245, 248, 252, 255));
        detailsLabel->SetPadding(5.0f);
        //detailsLabel->SetAutoResize(true);
        resultsContainer->AddChild(detailsLabel);

        mainContainer->AddChild(resultsContainer);
        currentY += 410;

        // ===== TEST MODE DESCRIPTION =====
        auto modeDescLabel = std::make_shared<UltraCanvasLabel>("ModeDescLabel", 9040, 20, currentY, 940, 140);
        modeDescLabel->SetText(
                "Test Modes:\n"
                "• Load, Decompress, Draw:  Loads image from disk, decompresses, and renders (tests I/O + CPU + GPU)\n"
                "• Decompress, Draw:           Uses cached file data, decompresses and renders (tests CPU + GPU, no disk I/O)\n"
                "• Draw cached pixmap only: Uses pre-decompressed, prepared Cairo image surfaces, only renders image (tests GPU/rendering)");
        modeDescLabel->SetFontSize(11);
        modeDescLabel->SetWordWrap(true);
        modeDescLabel->SetTextColor(Color(80, 80, 80, 255));
        modeDescLabel->SetBackgroundColor(Color(252, 252, 252, 255));
        modeDescLabel->SetBorders(1.0f, Color(220, 220, 220, 255));
        modeDescLabel->SetPadding(10.0f);
        modeDescLabel->SetAutoResize(true);
        mainContainer->AddChild(modeDescLabel);

        // ===== TEST STATE VARIABLES (captured by lambdas) =====
        auto testRunning = false;
        auto iterationCount = 0;

        // ===== IMAGE SELECTION CALLBACK =====
        imageDropdown->onSelectionChanged = [imageElement, imageDropdown](int index, const DropdownItem& item) {
            if (!item.value.empty()) {
                imageElement->LoadFromFile(item.value);
                imageElement->RequestRedraw();
                std::cout << "Image Performance Test: Selected image - " << item.value << std::endl;
            }
        };

        // ===== START TEST CALLBACK =====
        startButton->SetOnClick([=]() mutable {
            if (testRunning) {
                return; // Already running
            }

            // Get selected image and mode
            const DropdownItem* selectedImage = imageDropdown->GetSelectedItem();
            const DropdownItem* selectedMode = modeDropdown->GetSelectedItem();

            if (!selectedImage || !selectedMode) {
                statusLabel->SetText("Status: Error - No selection");
                return;
            }

            std::string imagePath = selectedImage->value;
            std::string testMode = selectedMode->value;

            // Reset state
            testRunning = true;
            iterationCount = 0;
            startButton->SetDisabled(true);
//            stopButton->SetDisabled(false);
            imageDropdown->SetDisabled(true);
            modeDropdown->SetDisabled(true);

            statusLabel->SetText("Status: Running...");
            statusLabel->SetTextColor(Color(0, 120, 200, 255));
            progressLabel->SetText("Progress: 0 / 10 seconds");
            iterationLabel->SetText("Iterations: 0");
            detailsLabel->SetText("Test in progress...\n\nPlease wait...");

            // Pre-load image for "Draw Only" mode
            std::shared_ptr<UCImage> img = UCImage::Load(imagePath, false);

            // Start timing
            auto testStartTime = std::chrono::steady_clock::now();
            const int testDurationSeconds = 10;
            auto ctx = mainContainer->GetRenderContext();


            // Run test loop
            long int prevElapsedSeconds = 0;
            while (testRunning) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - testStartTime).count();

                if (elapsedSeconds >= testDurationSeconds) {
                    break;
                }

                // Perform test based on mode
                std::shared_ptr<UCPixmap> pixmap;
                if (testMode == "full") {
                    // Full pipeline: Load from file (bypasses cache), decompress, prepare for render
                    //UCImage::ClearCache();  // Force reload from disk
                    img = UCImage::Load(imagePath, false);
                    pixmap = img->CreatePixmap(img->GetWidth(), img->GetHeight(), ImageFitMode::NoScale);
                } else if (testMode == "decompress_draw") {
                    // Decompress and draw: Use cached file, decompress fresh
                    pixmap = img->CreatePixmap(img->GetWidth(), img->GetHeight(), ImageFitMode::NoScale);
                } else if (testMode == "draw_only") {
                    // Draw only: Use pre-loaded and decompressed image
                    pixmap = img->GetPixmap(img->GetWidth(), img->GetHeight(), ImageFitMode::NoScale);
                }

                ctx->DrawPixmap(*pixmap.get(), imageElement->GetXInWindow(), imageElement->GetYInWindow());
                iterationCount++;

                // Update UI every 1 second
                if (prevElapsedSeconds < elapsedSeconds) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - testStartTime).count();

                    std::ostringstream progressText;
                    progressText << "Progress: " << elapsedSeconds << " / " << testDurationSeconds << " seconds";
                    progressLabel->SetText(progressText.str());

                    std::ostringstream iterText;
                    iterText << "Iterations: " << iterationCount;
                    iterationLabel->SetText(iterText.str());

                    // Force UI update
                    mainContainer->GetWindow()->Render(ctx);
//                    ctx->SetFillPaint(Colors::White);
//                    ctx->FillRectangle(imageElement->GetXInWindow(), imageElement->GetYInWindow(), imageElement->GetWidth(), imageElement->GetHeight());
                    mainContainer->GetWindow()->Flush();
                    prevElapsedSeconds = elapsedSeconds;
                }
            }

            // Test complete - calculate results
            auto testEndTime = std::chrono::steady_clock::now();
            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    testEndTime - testStartTime);

            int totalIterations = iterationCount;
            double durationSeconds = totalDuration.count() / 1000.0;
            double avgTimePerIteration = (totalIterations > 0) ? (totalDuration.count() / (double)totalIterations) : 0;
            double iterationsPerSecond = (durationSeconds > 0) ? (totalIterations / durationSeconds) : 0;

            // Update final results
            std::ostringstream finalResults;
            finalResults << std::fixed << std::setprecision(2);
            finalResults << "Detailed Results:\n";
            finalResults << "─────────────────────────\n";
            finalResults << "Test Duration:     " << durationSeconds << " s\n";
            finalResults << "Total Iterations:  " << totalIterations << "\n";
            finalResults << "Avg Time/Iteration: " << avgTimePerIteration << " ms\n";
            finalResults << "Iterations/Second: " << iterationsPerSecond << "\n";
            finalResults << "─────────────────────────\n";
            finalResults << "\nImage: " << selectedImage->text << " (" << img->GetWidth() << "x" << img->GetHeight() << ")\n";
            finalResults << "Mode:  " << selectedMode->text;

            detailsLabel->SetText(finalResults.str());

            std::ostringstream finalProgress;
            finalProgress << "Progress: " << testDurationSeconds << " / " << testDurationSeconds << " seconds (Complete)";
            progressLabel->SetText(finalProgress.str());

            std::ostringstream finalIter;
            finalIter << "Iterations: " << totalIterations;
            iterationLabel->SetText(finalIter.str());

            statusLabel->SetText("Status: Complete");
            statusLabel->SetTextColor(Color(0, 150, 0, 255));

            // Reset controls
            testRunning = false;
            startButton->SetDisabled(false);
//            stopButton->SetDEnabled(false);
            imageDropdown->SetDisabled(false);
            modeDropdown->SetDisabled(false);

            std::cout << "Image Performance Test Complete: " << totalIterations
                      << " iterations in " << durationSeconds << "s" << std::endl;
        });

        // ===== STOP TEST CALLBACK =====
//        stopButton->SetOnClick([=]() mutable {
//            if (testRunning->load()) {
//                testRunning->store(false);
//                statusLabel->SetText("Status: Stopped by user");
//                statusLabel->SetTextColor(Color(200, 100, 0, 255));
//            }
//        });

        return mainContainer;
    }

} // namespace UltraCanvas
// Apps/DemoApp/UltraCanvasDomainTableDemo.cpp
// Domain management table demonstration with embedded sparkline charts and interactive features
// Version: 1.0.0
// Last Modified: 2025-11-14
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
//#include "UltraCanvasModalDialog.h"
#include "UltraCanvasImageElement.h"
#include <memory>
#include <vector>
#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===== DOMAIN DATA STRUCTURE =====
    struct DomainEntry {
        std::string domain;
        std::string status;
        std::string securityInsights;
        std::vector<double> visitorData;  // Traffic history for sparkline
        std::string visitorCount;
        std::string plan;
        bool isUSAID;  // Special flag for USAID domain

        DomainEntry(const std::string& d, const std::string& s, const std::string& si,
                    const std::vector<double>& vd, const std::string& vc, const std::string& p, bool usaid = false)
                : domain(d), status(s), securityInsights(si), visitorData(vd), visitorCount(vc), plan(p), isUSAID(usaid) {}
    };

// ===== CUSTOM DOMAIN ROW COMPONENT =====
    class DomainRowComponent : public UltraCanvasContainer {
    private:
        std::shared_ptr<UltraCanvasLabel> domainLabel;
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasButton> securityButton;
        std::shared_ptr<UltraCanvasLineChartElement> sparklineChart;
        std::shared_ptr<UltraCanvasLabel> visitorLabel;
        std::shared_ptr<UltraCanvasLabel> planLabel;
        std::shared_ptr<UltraCanvasButton> moreButton;

        std::string domainUrl;
        bool isUSAIDDomain;

    public:
        std::function<void(const std::string&)> onDomainClick;
        std::function<void()> onUSAIDInfoClick;

        DomainRowComponent(const std::string& identifier, long id, const DomainEntry& entry, int rowHeight)
                : UltraCanvasContainer(identifier, id, 0, 0, 958, rowHeight),
                  domainUrl(entry.domain),
                  isUSAIDDomain(entry.isUSAID) {

            SetBackgroundColor(Color(255, 255, 255, 255));

            int currentX = 10;
            int labelHeight = rowHeight - 10;
            int yOffset = 5;

            // Column 1: Domain (clickable link style)
            domainLabel = std::make_shared<UltraCanvasLabel>(identifier + "_domain", id + 1,
                                                             currentX, yOffset, 200, labelHeight);
            domainLabel->SetText(entry.domain);
            domainLabel->SetFontSize(11);
            domainLabel->SetTextColor(Color(0, 102, 204, 255));  // Blue link color
            domainLabel->SetAlignment(TextAlignment::Left);
            AddChild(domainLabel);
            currentX += 210;

            // Column 2: Status (with icon)
            statusLabel = std::make_shared<UltraCanvasLabel>(identifier + "_status", id + 2,
                                                             currentX, yOffset, 100, labelHeight);
            statusLabel->SetText("✓ " + entry.status);
            statusLabel->SetFontSize(10);
            statusLabel->SetTextColor(Color(34, 139, 34, 255));  // Green
            statusLabel->SetAlignment(TextAlignment::Center);
            AddChild(statusLabel);
            currentX += 110;

            // Column 3: Security Insights (button)
            securityButton = std::make_shared<UltraCanvasButton>(identifier + "_security", id + 3,
                                                                 currentX, yOffset + 2, 80, labelHeight - 4);
            securityButton->SetText(entry.securityInsights);
            securityButton->SetFontSize(9);
            securityButton->SetTextColors(Color(0, 102, 204, 255), Color(0, 102, 204, 255));
            securityButton->SetColors(Color(240, 248, 255, 255), Color(220, 235, 255, 255));
            securityButton->SetCornerRadius(3);
            securityButton->SetBorders(1.0f);
            AddChild(securityButton);
            currentX += 90;

            // Column 4: Traffic Sparkline Chart
            sparklineChart = std::make_shared<UltraCanvasLineChartElement>(
                    identifier + "_sparkline", id + 4, currentX, yOffset, 180, labelHeight);

            // Create chart data from visitor data
            auto chartData = std::make_shared<ChartDataVector>();
            std::vector<ChartDataPoint> dataPoints;
            for (size_t i = 0; i < entry.visitorData.size(); ++i) {
                dataPoints.emplace_back(static_cast<double>(i), entry.visitorData[i], 0, "", entry.visitorData[i]);
            }
            chartData->LoadFromArray(dataPoints);

            sparklineChart->SetDataSource(chartData);
            sparklineChart->SetLineColor(Color(52, 152, 219, 255));  // Nice blue
            sparklineChart->SetLineWidth(2.0f);
            sparklineChart->SetShowDataPoints(false);
            sparklineChart->SetShowGrid(false);
            sparklineChart->SetShowAxes(false);
//            sparklineChart->SetShowLegend(false);
            sparklineChart->SetShowValueLabels(false);
            sparklineChart->SetSmoothingEnabled(true);
            sparklineChart->SetEnableTooltips(false);
            sparklineChart->SetEnableZoom(false);
            sparklineChart->SetEnablePan(false);
            sparklineChart->SetBackgroundColor(Color(250, 250, 250, 255));
            AddChild(sparklineChart);
            currentX += 190;

            // Column 5: Visitor Count
            visitorLabel = std::make_shared<UltraCanvasLabel>(identifier + "_visitors", id + 5,
                                                              currentX, yOffset, 100, labelHeight);
            visitorLabel->SetText(entry.visitorCount);
            visitorLabel->SetFontSize(11);
            visitorLabel->SetFontWeight(FontWeight::Bold);
            visitorLabel->SetTextColor(Color(50, 50, 50, 255));
            visitorLabel->SetAlignment(TextAlignment::Right);
            AddChild(visitorLabel);
            currentX += 110;

            // Column 6: Plan
            planLabel = std::make_shared<UltraCanvasLabel>(identifier + "_plan", id + 6,
                                                           currentX, yOffset, 60, labelHeight);
            planLabel->SetText(entry.plan);
            planLabel->SetFontSize(10);
            planLabel->SetTextColor(Color(100, 100, 100, 255));
            planLabel->SetAlignment(TextAlignment::Center);
            AddChild(planLabel);
            currentX += 70;

            // Column 7: More Options (three dots button)
            moreButton = std::make_shared<UltraCanvasButton>(identifier + "_more", id + 7,
                                                             currentX, yOffset + 2, 30, labelHeight - 4);
            moreButton->SetText("⋮");
            moreButton->SetFontSize(14);
            moreButton->SetColors(Color(245, 245, 245, 255), Color(230, 230, 230, 255));
            moreButton->SetCornerRadius(3);
            AddChild(moreButton);
        }

        void SetupEventHandlers() {
            // Domain label click - open URL or show USAID info
            domainLabel->SetEventCallback([this](const UCEvent& event) {
                if (event.type == UCEventType::MouseUp) {
                    if (isUSAIDDomain && onUSAIDInfoClick) {
                        onUSAIDInfoClick();
                    } else if (onDomainClick) {
                        onDomainClick("https://" + domainUrl);
                    }
                    return true;
                } else if (event.type == UCEventType::MouseEnter) {
                    domainLabel->SetTextColor(Color(255, 69, 0, 255));  // Orange red on hover
                    return true;
                } else if (event.type == UCEventType::MouseLeave) {
                    domainLabel->SetTextColor(Color(0, 102, 204, 255));  // Back to blue
                    return true;
                }
                return false;
            });

            // Security button click
            securityButton->onClick = [this]() {
                std::cout << "Security insights for: " << domainUrl << std::endl;
            };

            // More button click
            moreButton->onClick = [this]() {
                std::cout << "More options for: " << domainUrl << std::endl;
            };
        }
    };

// ===== GENERATE SAMPLE TRAFFIC DATA =====
    std::vector<double> GenerateTrafficData(int points, double baseValue, double variance) {
        std::vector<double> data;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(-variance, variance);

        double currentValue = baseValue;
        for (int i = 0; i < points; ++i) {
            currentValue += dist(gen);
            currentValue = std::max(1000.0, currentValue);  // Ensure positive values
            data.push_back(currentValue);
        }

        return data;
    }

// ===== USAID INFORMATION DIALOG =====
//    class USAIDInfoDialog : public UltraCanvasModalDialog {
//    private:
//        std::shared_ptr<UltraCanvasImageElement> usaidImage;
//        std::shared_ptr<UltraCanvasLabel> infoText;
//        std::shared_ptr<UltraCanvasButton> moreInfoButton;
//        std::shared_ptr<UltraCanvasButton> closeButton;
//
//    public:
//        std::function<void(const std::string&)> onMoreInfoClick;
//
//        USAIDInfoDialog() : UltraCanvasModalDialog("USAIDDialog", 9000, 100, 100, 550, 450) {
//            DialogConfig config;
//            config.title = "USAID - United States Agency for International Development";
//            config.message = "";  // We'll use custom content
//            config.type = DialogType::Information;
//            config.buttons = DialogButtons::None;  // Custom buttons
//            config.width = 550;
//            config.height = 450;
//            config.modal = true;
//            config.allowEscapeKey = true;
//            SetConfig(config);
//
//            CreateCustomContent();
//        }
//
//        void CreateCustomContent() {
//            // USAID Logo Image (top of dialog)
//            usaidImage = std::make_shared<UltraCanvasImageElement>("USAIDImage", 9001, 150, 50, 250, 150);
//            usaidImage->SetScaleMode(ImageScaleMode::Uniform);
//            usaidImage->LoadFromFile("/mnt/user-data/uploads/usaid.png");
//            AddChild(usaidImage);
//
//            // Information Text
//            infoText = std::make_shared<UltraCanvasLabel>("USAIDInfo", 9002, 30, 220, 490, 140);
//            infoText->SetText(
//                    "USAID was closed by Elon Musk and Donald Trump under the \"America First\" agenda.\n\n"
//                    "The United States Agency for International Development (USAID) was a former agency of the "
//                    "United States federal government. Until its closure in 2025, USAID was the world's largest "
//                    "agency for foreign aid."
//            );
//            infoText->SetFontSize(11);
//            infoText->SetTextColor(Color(50, 50, 50, 255));
//            infoText->SetAlignment(TextAlignment::Left);
//            infoText->SetWordWrap(true);
//            infoText->SetBackgroundColor(Color(248, 248, 248, 255));
//            infoText->SetBorders(1.0f);
//            infoText->SetPadding(12.0f);
//            AddChild(infoText);
//
//            // More Info Button
//            moreInfoButton = std::make_shared<UltraCanvasButton>("MoreInfoBtn", 9003, 150, 380, 120, 35);
//            moreInfoButton->SetText("More infos");
//            moreInfoButton->SetFontSize(12);
//            moreInfoButton->SetBackgroundColor(Color(0, 102, 204, 255));
//            moreInfoButton->SetTextColor(Color(255, 255, 255, 255));
//            moreInfoButton->SetHoverBackgroundColor(Color(0, 122, 224, 255));
//            moreInfoButton->SetPressedBackgroundColor(Color(0, 82, 184, 255));
//            moreInfoButton->SetCornerRadius(4);
//            moreInfoButton->onClick = [this]() {
//                if (onMoreInfoClick) {
//                    onMoreInfoClick("https://en.wikipedia.org/wiki/United_States_Agency_for_International_Development");
//                }
//                Close(DialogResult::OK);
//            };
//            AddChild(moreInfoButton);
//
//            // Close Button
//            closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 9004, 280, 380, 120, 35);
//            closeButton->SetText("Close");
//            closeButton->SetFontSize(12);
//            closeButton->SetBackgroundColor(Color(150, 150, 150, 255));
//            closeButton->SetTextColor(Color(255, 255, 255, 255));
//            closeButton->SetHoverBackgroundColor(Color(170, 170, 170, 255));
//            closeButton->SetPressedBackgroundColor(Color(130, 130, 130, 255));
//            closeButton->SetCornerRadius(4);
//            closeButton->onClick = [this]() {
//                Close(DialogResult::Cancel);
//            };
//            AddChild(closeButton);
//        }
//    };

// ===== FORMAT VISITOR COUNT =====
    std::string FormatVisitorCount(double visitors) {
        if (visitors >= 1000000) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << (visitors / 1000000.0) << "M";
            return oss.str();
        } else if (visitors >= 1000) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << (visitors / 1000.0) << "k";
            return oss.str();
        } else {
            return std::to_string(static_cast<int>(visitors));
        }
    }

// ===== CREATE DOMAIN TABLE DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDomainTableDemo() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("DomainTableDemo", 8000, 0, 0, 1000, 800);
        mainContainer->SetBackgroundColor(Color(245, 245, 245, 255));

        // Title
        auto titleLabel = std::make_shared<UltraCanvasLabel>("DomainTableTitle", 8001, 20, 10, 960, 40);
        titleLabel->SetText("Domain Management Dashboard - Interactive Table with Sparkline Charts");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetTextColor(Color(40, 40, 40, 255));
        titleLabel->SetAlignment(TextAlignment::Center);
//        titleLabel->SetBackgroundColor(Color(255, 255, 255, 255));
//        titleLabel->SetBorders(1.0f);
        mainContainer->AddChild(titleLabel);

        // Description
        auto descLabel = std::make_shared<UltraCanvasLabel>("DomainTableDesc", 8002, 20, 60, 960, 35);
        descLabel->SetText("Click on domain names to visit websites. The USAID.com domain shows special information about its closure.");
        descLabel->SetFontSize(11);
//        descLabel->SetTextColor(Color(80, 80, 80, 255));
        descLabel->SetAlignment(TextAlignment::Center);
//        descLabel->SetBackgroundColor(Color(255, 255, 255, 255));
        mainContainer->AddChild(descLabel);

        // Column Headers Container
        auto headerContainer = std::make_shared<UltraCanvasContainer>("HeaderContainer", 8003, 20, 105, 960, 35);
        headerContainer->SetBackgroundColor(Color(230, 230, 230, 255));
        headerContainer->SetBorders(1.0f);

        int headerX = 10;
        auto createHeader = [&](const std::string& text, int width, long id) {
            auto header = std::make_shared<UltraCanvasLabel>("Header_" + text, id, headerX, 5, width, 25);
            header->SetText(text);
            header->SetFontSize(10);
            header->SetFontWeight(FontWeight::Bold);
            header->SetTextColor(Color(40, 40, 40, 255));
            header->SetAlignment(width > 100 ? TextAlignment::Left : TextAlignment::Center);
            headerContainer->AddChild(header);
            headerX += width + 10;
        };

        createHeader("Domain", 200, 8010);
        createHeader("Status ⓘ", 100, 8011);
        createHeader("Security insights ⓘ", 180, 8012);
        createHeader("Unique visitors ⓘ", 180, 8013);
        createHeader("", 1, 8014);  // Visitor count column
        createHeader("Plan", 60, 8015);
        createHeader("", 30, 8016);   // More button column

        mainContainer->AddChild(headerContainer);

        // Create domain data
        std::vector<DomainEntry> domains = {
                DomainEntry("www.ultraos.eu", "Active", "Enable",
                            GenerateTrafficData(20, 150000, 10000), FormatVisitorCount(150000), "Free"),
                DomainEntry("www.tomtom.com", "Active", "Enable",
                            GenerateTrafficData(20, 500000, 25000), FormatVisitorCount(500000), "Free"),
                DomainEntry("www.futa.com", "Active", "Enable",
                            GenerateTrafficData(20, 75000, 8000), FormatVisitorCount(75000), "Free"),
                DomainEntry("www.godotengine.org", "Active", "Enable",
                            GenerateTrafficData(20, 180000, 15000), FormatVisitorCount(180000), "Free"),
                DomainEntry("www.duckduckgo.com", "Active", "Enable",
                            GenerateTrafficData(20, 2500000, 200000), FormatVisitorCount(2500000), "Free"),
                DomainEntry("www.solar-aid.org", "Active", "Enable",
                            GenerateTrafficData(20, 45000, 5000), FormatVisitorCount(45000), "Free"),
                DomainEntry("www.democracynow.com", "Active", "Enable",
                            GenerateTrafficData(20, 320000, 25000), FormatVisitorCount(320000), "Free"),
                DomainEntry("www.usaid.com", "Active", "Enable",
                            GenerateTrafficData(20, 850000, 50000), FormatVisitorCount(850000), "Free", true),
                DomainEntry("www.350.org", "Active", "Enable",
                            GenerateTrafficData(20, 125000, 12000), FormatVisitorCount(125000), "Free"),
                DomainEntry("www.doctorswithoutborders.org", "Active", "Enable",
                            GenerateTrafficData(20, 680000, 40000), FormatVisitorCount(680000), "Free")
        };

        // Rows Container (scrollable area)
        auto rowsContainer = std::make_shared<UltraCanvasContainer>("RowsContainer", 8004, 20, 140, 960, 540);
        rowsContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        rowsContainer->SetBorderLeft(1.0f);
        rowsContainer->SetBorderRight(1.0f);
        rowsContainer->SetBorderBottom(1.0f);

        // Create rows
        int rowY = 5;
        int rowHeight = 50;
        long rowIdBase = 8100;

        for (size_t i = 0; i < domains.size(); ++i) {
            auto domainRow = std::make_shared<DomainRowComponent>(
                    "DomainRow_" + std::to_string(i),
                    rowIdBase + (i * 10),
                    domains[i],
                    rowHeight
            );

            domainRow->SetPosition(0, rowY);

            // Set up event handlers for this row
            domainRow->onDomainClick = [](const std::string& url) {
                std::cout << "Opening URL: " << url << std::endl;
                // In a real implementation, this would open the URL in a browser
                // For now, just log it
            };

            domainRow->onUSAIDInfoClick = [mainContainer]() {
                // Create and show USAID information dialog
//                auto usaidDialog = std::make_shared<USAIDInfoDialog>();
//
//                usaidDialog->onMoreInfoClick = [](const std::string& url) {
//                    std::cout << "Opening Wikipedia: " << url << std::endl;
//                    // In a real implementation, this would open the URL
//                };
//
//                // Show the dialog
//                usaidDialog->Show();
            };

            domainRow->SetupEventHandlers();

            rowsContainer->AddChild(domainRow);
            rowY += rowHeight + 2;  // 2px spacing between rows
        }

        mainContainer->AddChild(rowsContainer);

        // Footer with info
        auto footerLabel = std::make_shared<UltraCanvasLabel>("FooterInfo", 8005, 20, 690, 960, 30);
        footerLabel->SetText("✨ Demonstration of UltraCanvas table templates with embedded charts, clickable links, and interactive dialogs");
        footerLabel->SetFontSize(10);
        footerLabel->SetTextColor(Color(100, 100, 100, 255));
        footerLabel->SetAlignment(TextAlignment::Center);
        mainContainer->AddChild(footerLabel);

        return mainContainer;
    }

} // namespace UltraCanvas
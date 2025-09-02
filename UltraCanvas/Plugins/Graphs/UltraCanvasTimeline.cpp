// UltraCanvasTimeline.cpp
// Interactive timeline component with Gantt chart visualization and time capture
// Version: 1.0.0
// Last Modified: 2025-08-28
// Author: UltraCanvas Framework

#include "UltraCanvasTimeline.h"
#include "UltraCanvasDrawingSurface.h"
#include "UltraCanvasShapePrimitives.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>

namespace UltraCanvas {

    // Global timeline instances for the TimelineManager
    std::map<std::string, std::unique_ptr<UltraCanvasTimeline>> GlobalTimelines;

    // Constructor
    UltraCanvasTimeline::UltraCanvasTimeline(int x, int y, int w, int h)
        : PositionX(x), PositionY(y), Width(w), Height(h),
          CurrentPalette(TimelinePalette::Pastel), DisplayType(TimelineType::Standard),
          DisplayStyle(TimelineStyle::Accumulative), AutoScale(true), ZoomLevel(1.0f),
          BarHeight(30), BarSpacing(10), ShowGrid(true), ShowLabels(true), 
          ShowTimeMarkers(true), DrawingSurface(nullptr) {
        
        ViewStartTime = std::chrono::steady_clock::now();
        ViewEndTime = ViewStartTime + std::chrono::hours(24); // Default 24-hour view
    }

    // Destructor
    UltraCanvasTimeline::~UltraCanvasTimeline() {
        Datasets.clear();
        ActiveCaptures.clear();
    }

    // Core timeline management functions
    bool UltraCanvasTimeline::StartTimeline(const std::string& datasetName, TimelineType type) {
        DisplayType = type;
        
        // Create new dataset if it doesn't exist
        if (Datasets.find(datasetName) == Datasets.end()) {
            auto dataset = std::make_shared<TimelineDataset>(datasetName);
            dataset->DatasetColor = GetPaletteColor(Datasets.size());
            Datasets[datasetName] = dataset;
        }
        
        // Reset view if this is the first dataset
        if (Datasets.size() == 1) {
            ViewStartTime = std::chrono::steady_clock::now();
            ViewEndTime = ViewStartTime + std::chrono::hours(8); // 8-hour default view
        }
        
        return true;
    }

    bool UltraCanvasTimeline::StartTimeCapture(const std::string& datasetName, const std::string& eventName) {
        // Ensure dataset exists
        if (Datasets.find(datasetName) == Datasets.end()) {
            StartTimeline(datasetName);
        }
        
        // Create unique capture key
        std::string captureKey = datasetName + "::" + eventName;
        
        // Check if already capturing
        if (ActiveCaptures.find(captureKey) != ActiveCaptures.end()) {
            return false; // Already capturing this event
        }
        
        // Start capture
        auto now = std::chrono::steady_clock::now();
        ActiveCaptures[captureKey] = now;
        
        // Create the event in the dataset
        auto event = std::make_shared<TimelineEvent>(eventName);
        event->StartTime = now;
        event->IsActive = true;
        event->EventColor = GetPaletteColor(Datasets[datasetName]->Events.size());
        
        Datasets[datasetName]->Events.push_back(event);
        
        // Trigger update callback
        if (OnTimelineUpdateCallback) {
            OnTimelineUpdateCallback();
        }
        
        return true;
    }

    bool UltraCanvasTimeline::EndTimeCapture(const std::string& datasetName, const std::string& eventName) {
        std::string captureKey = datasetName + "::" + eventName;
        
        // Check if we're capturing this event
        auto captureIt = ActiveCaptures.find(captureKey);
        if (captureIt == ActiveCaptures.end()) {
            return false; // Not currently capturing this event
        }
        
        // Find the event in the dataset
        auto datasetIt = Datasets.find(datasetName);
        if (datasetIt == Datasets.end()) {
            return false;
        }
        
        auto& events = datasetIt->second->Events;
        auto eventIt = std::find_if(events.begin(), events.end(),
            [&eventName](const std::shared_ptr<TimelineEvent>& evt) {
                return evt->EventName == eventName && evt->IsActive;
            });
        
        if (eventIt != events.end()) {
            // End the capture
            auto now = std::chrono::steady_clock::now();
            (*eventIt)->EndTime = now;
            (*eventIt)->IsActive = false;
            
            // Remove from active captures
            ActiveCaptures.erase(captureIt);
            
            // Update view range if auto-scaling
            if (AutoScale) {
                CalculateTimeRange();
            }
            
            // Trigger update callback
            if (OnTimelineUpdateCallback) {
                OnTimelineUpdateCallback();
            }
            
            return true;
        }
        
        return false;
    }

    // Dataset management
    bool UltraCanvasTimeline::AddDataset(const std::string& datasetName) {
        if (Datasets.find(datasetName) != Datasets.end()) {
            return false; // Dataset already exists
        }
        
        auto dataset = std::make_shared<TimelineDataset>(datasetName);
        dataset->DatasetColor = GetPaletteColor(Datasets.size());
        Datasets[datasetName] = dataset;
        
        return true;
    }

    bool UltraCanvasTimeline::RemoveDataset(const std::string& datasetName) {
        auto it = Datasets.find(datasetName);
        if (it == Datasets.end()) {
            return false;
        }
        
        // Remove any active captures from this dataset
        auto captureIt = ActiveCaptures.begin();
        while (captureIt != ActiveCaptures.end()) {
            if (captureIt->first.substr(0, datasetName.length()) == datasetName) {
                captureIt = ActiveCaptures.erase(captureIt);
            } else {
                ++captureIt;
            }
        }
        
        Datasets.erase(it);
        return true;
    }

    bool UltraCanvasTimeline::SetDatasetVisible(const std::string& datasetName, bool visible) {
        auto it = Datasets.find(datasetName);
        if (it == Datasets.end()) {
            return false;
        }
        
        it->second->Visible = visible;
        return true;
    }

    std::vector<std::string> UltraCanvasTimeline::GetDatasetNames() const {
        std::vector<std::string> names;
        for (const auto& pair : Datasets) {
            names.push_back(pair.first);
        }
        return names;
    }

    // Event management
    bool UltraCanvasTimeline::AddEvent(const std::string& datasetName, const std::string& eventName,
                                     const std::chrono::steady_clock::time_point& startTime,
                                     const std::chrono::steady_clock::time_point& endTime) {
        auto datasetIt = Datasets.find(datasetName);
        if (datasetIt == Datasets.end()) {
            return false;
        }
        
        auto event = std::make_shared<TimelineEvent>(eventName);
        event->StartTime = startTime;
        event->EndTime = endTime;
        event->EventColor = GetPaletteColor(datasetIt->second->Events.size());
        event->IsActive = false;
        
        datasetIt->second->Events.push_back(event);
        
        if (AutoScale) {
            CalculateTimeRange();
        }
        
        return true;
    }

    bool UltraCanvasTimeline::RemoveEvent(const std::string& datasetName, const std::string& eventName) {
        auto datasetIt = Datasets.find(datasetName);
        if (datasetIt == Datasets.end()) {
            return false;
        }
        
        auto& events = datasetIt->second->Events;
        auto eventIt = std::remove_if(events.begin(), events.end(),
            [&eventName](const std::shared_ptr<TimelineEvent>& evt) {
                return evt->EventName == eventName;
            });
        
        if (eventIt != events.end()) {
            events.erase(eventIt, events.end());
            return true;
        }
        
        return false;
    }

    bool UltraCanvasTimeline::SetEventDescription(const std::string& datasetName, const std::string& eventName,
                                                const std::string& description) {
        auto datasetIt = Datasets.find(datasetName);
        if (datasetIt == Datasets.end()) {
            return false;
        }
        
        auto& events = datasetIt->second->Events;
        auto eventIt = std::find_if(events.begin(), events.end(),
            [&eventName](const std::shared_ptr<TimelineEvent>& evt) {
                return evt->EventName == eventName;
            });
        
        if (eventIt != events.end()) {
            (*eventIt)->Description = description;
            return true;
        }
        
        return false;
    }

    // Visual configuration
    void UltraCanvasTimeline::SetPalette(TimelinePalette palette) {
        CurrentPalette = palette;
        
        // Update existing dataset colors
        int colorIndex = 0;
        for (auto& datasetPair : Datasets) {
            datasetPair.second->DatasetColor = GetPaletteColor(colorIndex++);
        }
    }

    void UltraCanvasTimeline::SetCustomPalette(const std::vector<TimelineColor>& colors) {
        CurrentPalette = TimelinePalette::Custom;
        CustomColors = colors;
        
        // Update existing colors
        int colorIndex = 0;
        for (auto& datasetPair : Datasets) {
            datasetPair.second->DatasetColor = GetPaletteColor(colorIndex++);
        }
    }

    void UltraCanvasTimeline::SetTimelineType(TimelineType type) {
        DisplayType = type;
        
        // Adjust display parameters based on type
        switch (type) {
            case TimelineType::Compact:
                BarHeight = 20;
                BarSpacing = 5;
                ShowLabels = false;
                break;
            case TimelineType::Detailed:
                BarHeight = 40;
                BarSpacing = 15;
                ShowLabels = true;
                break;
            case TimelineType::Hierarchical:
                BarHeight = 25;
                BarSpacing = 8;
                ShowLabels = true;
                break;
            default: // Standard
                BarHeight = 30;
                BarSpacing = 10;
                ShowLabels = true;
                break;
        }
    }

    void UltraCanvasTimeline::SetPosition(int x, int y) {
        PositionX = x;
        PositionY = y;
    }

    void UltraCanvasTimeline::SetSize(int width, int height) {
        Width = width;
        Height = height;
    }

    // Style and layout configuration
    void UltraCanvasTimeline::SetTimelineStyle(TimelineStyle style) {
        DisplayStyle = style;
        
        // Adjust display parameters based on style
        switch (style) {
            case TimelineStyle::Accumulative:
                // Traditional Gantt style - events in separate rows
                break;
            case TimelineStyle::Comparative:
                // All events start from same baseline for time comparison
                BarSpacing = std::max(BarSpacing, 15); // More space for comparison labels
                break;
            case TimelineStyle::Overlapping:
                // Events can overlap, useful for concurrent operations
                BarSpacing = std::max(BarSpacing, 8);
                break;
        }
    }

    TimelineStyle UltraCanvasTimeline::GetTimelineStyle() const {
        return DisplayStyle;
    }

    // Display options
    void UltraCanvasTimeline::SetAutoScale(bool autoScale) {
        AutoScale = autoScale;
        if (autoScale) {
            CalculateTimeRange();
        }
    }

    void UltraCanvasTimeline::SetZoomLevel(float zoom) {
        ZoomLevel = std::max(0.1f, std::min(zoom, 10.0f));
    }

    void UltraCanvasTimeline::SetTimeRange(const std::chrono::steady_clock::time_point& startTime,
                                         const std::chrono::steady_clock::time_point& endTime) {
        ViewStartTime = startTime;
        ViewEndTime = endTime;
        AutoScale = false;
    }

    void UltraCanvasTimeline::SetBarHeight(int height) {
        BarHeight = std::max(10, std::min(height, 100));
    }

    void UltraCanvasTimeline::SetBarSpacing(int spacing) {
        BarSpacing = std::max(2, std::min(spacing, 50));
    }

    void UltraCanvasTimeline::SetShowGrid(bool show) {
        ShowGrid = show;
    }

    void UltraCanvasTimeline::SetShowLabels(bool show) {
        ShowLabels = show;
    }

    void UltraCanvasTimeline::SetShowTimeMarkers(bool show) {
        ShowTimeMarkers = show;
    }

    // Helper methods implementation
    TimelineColor UltraCanvasTimeline::GetPaletteColor(int index) const {
        const std::vector<TimelineColor>* palette = nullptr;
        
        switch (CurrentPalette) {
            case TimelinePalette::Pastel:
                palette = &TimelinePalettes::PastelColors;
                break;
            case TimelinePalette::Ocean:
                palette = &TimelinePalettes::OceanColors;
                break;
            case TimelinePalette::Sunset:
                palette = &TimelinePalettes::SunsetColors;
                break;
            case TimelinePalette::Forest:
                palette = &TimelinePalettes::ForestColors;
                break;
            case TimelinePalette::Lavender:
                palette = &TimelinePalettes::LavenderColors;
                break;
            case TimelinePalette::Corporate:
                palette = &TimelinePalettes::CorporateColors;
                break;
            case TimelinePalette::Custom:
                palette = &CustomColors;
                break;
        }
        
        if (palette && !palette->empty()) {
            return (*palette)[index % palette->size()];
        }
        
        // Fallback color
        return TimelineColor(0.7f, 0.7f, 0.7f, 0.9f);
    }

    void UltraCanvasTimeline::CalculateTimeRange() {
        if (Datasets.empty()) {
            return;
        }
        
        auto earliest = std::chrono::steady_clock::now();
        auto latest = earliest;
        bool hasEvents = false;
        
        for (const auto& datasetPair : Datasets) {
            for (const auto& event : datasetPair.second->Events) {
                if (!hasEvents) {
                    earliest = event->StartTime;
                    latest = event->IsActive ? std::chrono::steady_clock::now() : event->EndTime;
                    hasEvents = true;
                } else {
                    earliest = std::min(earliest, event->StartTime);
                    auto eventEnd = event->IsActive ? std::chrono::steady_clock::now() : event->EndTime;
                    latest = std::max(latest, eventEnd);
                }
            }
        }
        
        if (hasEvents) {
            // Add 10% padding to the view range
            auto duration = latest - earliest;
            auto padding = duration / 10;
            ViewStartTime = earliest - padding;
            ViewEndTime = latest + padding;
        }
    }

    int UltraCanvasTimeline::CalculateBarPosition(const std::chrono::steady_clock::time_point& time) const {
        auto totalDuration = ViewEndTime - ViewStartTime;
        auto timeOffset = time - ViewStartTime;
        
        if (totalDuration.count() == 0) {
            return PositionX;
        }
        
        double ratio = static_cast<double>(timeOffset.count()) / static_cast<double>(totalDuration.count());
        return PositionX + static_cast<int>(ratio * Width * ZoomLevel);
    }

    int UltraCanvasTimeline::CalculateYPosition(int datasetIndex, int eventIndex) const {
        int baseY = PositionY + 60; // Start below time labels
        
        switch (DisplayStyle) {
            case TimelineStyle::Accumulative:
                // Traditional Gantt: each dataset gets its own row
                return baseY + datasetIndex * (BarHeight + BarSpacing);
                
            case TimelineStyle::Comparative:
                // Comparative: all events on same row, different datasets offset slightly
                return baseY + datasetIndex * (BarHeight / 3);
                
            case TimelineStyle::Overlapping:
                // Overlapping: events can share same Y space but with slight offset
                return baseY + (eventIndex % 3) * (BarHeight / 4);
        }
        
        return baseY + datasetIndex * (BarHeight + BarSpacing);
    }

    void UltraCanvasTimeline::DrawTimelineGrid() {
        if (!DrawingSurface || !ShowGrid) {
            return;
        }
        
        // Draw vertical time markers
        auto duration = ViewEndTime - ViewStartTime;
        auto intervalCount = 10; // Number of grid lines
        auto interval = duration / intervalCount;
        
        for (int i = 0; i <= intervalCount; ++i) {
            auto timePoint = ViewStartTime + interval * i;
            int xPos = CalculateBarPosition(timePoint);
            
            if (xPos >= PositionX && xPos <= PositionX + Width) {
                // Draw vertical grid line
                DrawingSurface->DrawLine(xPos, PositionY, xPos, PositionY + Height,
                                       TimelineColor(0.8f, 0.8f, 0.8f, 0.5f));
            }
        }
        
        // Draw horizontal lines between datasets
        int yOffset = PositionY + 50; // Space for time labels
        for (size_t i = 0; i <= Datasets.size(); ++i) {
            DrawingSurface->DrawLine(PositionX, yOffset, PositionX + Width, yOffset,
                                   TimelineColor(0.8f, 0.8f, 0.8f, 0.3f));
            yOffset += BarHeight + BarSpacing;
        }
    }

    void UltraCanvasTimeline::DrawTimeLabels() {
        if (!DrawingSurface || !ShowTimeMarkers) {
            return;
        }
        
        auto duration = ViewEndTime - ViewStartTime;
        auto intervalCount = 5; // Fewer labels to avoid crowding
        auto interval = duration / intervalCount;
        
        for (int i = 0; i <= intervalCount; ++i) {
            auto timePoint = ViewStartTime + interval * i;
            int xPos = CalculateBarPosition(timePoint);
            
            if (xPos >= PositionX && xPos <= PositionX + Width) {
                // Convert time point to readable string
                auto timeT = std::chrono::duration_cast<std::chrono::seconds>(
                    timePoint.time_since_epoch()).count();
                std::stringstream ss;
                ss << std::put_time(std::localtime(&timeT), "%H:%M");
                
                // Draw time label
                DrawingSurface->DrawText(ss.str(), xPos - 20, PositionY + 20, 12,
                                       TimelineColor(0.3f, 0.3f, 0.3f, 1.0f));
            }
        }
    }

    void UltraCanvasTimeline::DrawEventBar(const TimelineEvent& event, int yPosition, int datasetIndex) {
        if (!DrawingSurface) {
            return;
        }
        
        auto endTime = event.IsActive ? std::chrono::steady_clock::now() : event.EndTime;
        
        int startX, endX;
        
        if (DisplayStyle == TimelineStyle::Comparative) {
            // For comparative style, all events start from the same baseline (ViewStartTime)
            // This allows direct visual comparison of event durations
            auto eventDuration = endTime - event.StartTime;
            startX = CalculateBarPosition(ViewStartTime);
            endX = CalculateBarPosition(ViewStartTime + eventDuration);
        } else {
            // Normal positioning for Accumulative and Overlapping styles
            startX = CalculateBarPosition(event.StartTime);
            endX = CalculateBarPosition(endTime);
        }
        
        // Ensure minimum width for visibility
        if (endX - startX < 5) {
            endX = startX + 5;
        }
        
        // Adjust bar height for comparative style
        int adjustedBarHeight = BarHeight;
        if (DisplayStyle == TimelineStyle::Comparative) {
            adjustedBarHeight = BarHeight - 5; // Slightly smaller bars for better comparison
        }
        
        // Draw the event bar with rounded corners
        DrawingSurface->DrawRoundedRectangle(startX, yPosition, endX - startX, adjustedBarHeight,
                                           event.EventColor, 5);
        
        // Add subtle gradient effect for better visual appeal
        auto gradientColor = event.EventColor;
        gradientColor.Alpha *= 0.7f;
        DrawingSurface->DrawRoundedRectangle(startX, yPosition, endX - startX, adjustedBarHeight / 2,
                                           gradientColor, 5);
        
        // Draw event name and duration for comparative style
        if (ShowLabels) {
            if (DisplayStyle == TimelineStyle::Comparative) {
                // Show duration in comparative mode
                auto duration = endTime - event.StartTime;
                auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                
                std::string label = event.EventName + " (" + std::to_string(durationMs) + "ms)";
                if ((endX - startX) > 100) {
                    DrawingSurface->DrawText(label, startX + 5, yPosition + adjustedBarHeight / 2 + 3, 9,
                                           TimelineColor(0.2f, 0.2f, 0.2f, 1.0f));
                }
                
                // Also show duration at the end of the bar
                DrawingSurface->DrawText(std::to_string(durationMs) + "ms", endX + 5, yPosition + adjustedBarHeight / 2 + 3, 8,
                                       TimelineColor(0.4f, 0.4f, 0.4f, 1.0f));
            } else {
                // Normal label display for other styles
                if ((endX - startX) > 50) {
                    DrawingSurface->DrawText(event.EventName, startX + 5, yPosition + adjustedBarHeight / 2 + 3, 10,
                                           TimelineColor(0.2f, 0.2f, 0.2f, 1.0f));
                }
            }
        }
        
        // Draw progress indicator for active events
        if (event.IsActive) {
            DrawingSurface->DrawRoundedRectangle(endX - 3, yPosition - 2, 6, adjustedBarHeight + 4,
                                               TimelineColor(1.0f, 0.3f, 0.3f, 0.8f), 3);
        }
        
        // Add performance indicator for comparative style
        if (DisplayStyle == TimelineStyle::Comparative && !event.IsActive) {
            // Color-code performance: green for fast, yellow for medium, red for slow
            auto duration = endTime - event.StartTime;
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            
            TimelineColor perfColor;
            if (durationMs < 100) {
                perfColor = TimelineColor(0.2f, 0.8f, 0.2f, 0.8f); // Green - Fast
            } else if (durationMs < 500) {
                perfColor = TimelineColor(0.9f, 0.9f, 0.2f, 0.8f); // Yellow - Medium
            } else {
                perfColor = TimelineColor(0.9f, 0.2f, 0.2f, 0.8f); // Red - Slow
            }
            
            // Small performance indicator circle
            DrawingSurface->DrawCircle(startX - 8, yPosition + adjustedBarHeight / 2, 4, perfColor);
        }
    }

    // Rendering
    void UltraCanvasTimeline::Render() {
        if (!DrawingSurface) {
            return;
        }
        
        // Clear the timeline area
        DrawingSurface->FillRectangle(PositionX, PositionY, Width, Height,
                                    TimelineColor(0.98f, 0.98f, 0.98f, 1.0f));
        
        // Draw grid if enabled
        if (ShowGrid) {
            DrawTimelineGrid();
        }
        
        // Draw time labels
        if (ShowTimeMarkers) {
            DrawTimeLabels();
        }
        
        // Draw dataset labels and events based on style
        if (DisplayStyle == TimelineStyle::Comparative) {
            // Comparative style: draw all events on the same timeline for comparison
            int yOffset = PositionY + 80; // More space for labels in comparative mode
            
            // Draw a baseline reference line
            DrawingSurface->DrawLine(PositionX, yOffset - 10, PositionX + Width, yOffset - 10,
                                   TimelineColor(0.6f, 0.6f, 0.6f, 0.5f));
            
            int eventIndex = 0;
            for (const auto& datasetPair : Datasets) {
                if (!datasetPair.second->Visible) {
                    continue;
                }
                
                // Draw dataset name on the left
                if (ShowLabels) {
                    DrawingSurface->DrawText(datasetPair.first, PositionX - 120, 
                                           yOffset + eventIndex * 25 + BarHeight / 2 + 3,
                                           11, datasetPair.second->DatasetColor);
                }
                
                // Draw all events from this dataset
                for (const auto& event : datasetPair.second->Events) {
                    int eventY = yOffset + eventIndex * 25;
                    DrawEventBar(*event, eventY, eventIndex);
                    eventIndex++;
                }
            }
        } else {
            // Accumulative and Overlapping styles: traditional layout
            int datasetIndex = 0;
            for (const auto& datasetPair : Datasets) {
                if (!datasetPair.second->Visible) {
                    continue;
                }
                
                int yPosition = CalculateYPosition(datasetIndex, 0);
                
                // Draw dataset name
                if (ShowLabels) {
                    DrawingSurface->DrawText(datasetPair.first, PositionX - 80, yPosition + BarHeight / 2 + 3,
                                           12, datasetPair.second->DatasetColor);
                }
                
                // Draw events in this dataset
                int eventIndex = 0;
                for (const auto& event : datasetPair.second->Events) {
                    int eventY = (DisplayStyle == TimelineStyle::Overlapping) ? 
                                CalculateYPosition(datasetIndex, eventIndex) : yPosition;
                    DrawEventBar(*event, eventY, datasetIndex);
                    eventIndex++;
                }
                
                datasetIndex++;
            }
        }
        
        // Draw style indicator
        if (ShowLabels) {
            std::string styleText;
            switch (DisplayStyle) {
                case TimelineStyle::Accumulative: styleText = "Accumulative View"; break;
                case TimelineStyle::Comparative: styleText = "Comparative View - Duration Comparison"; break;
                case TimelineStyle::Overlapping: styleText = "Overlapping View"; break;
            }
            DrawingSurface->DrawText(styleText, PositionX + Width - 200, PositionY + 15, 10,
                                   TimelineColor(0.5f, 0.5f, 0.5f, 0.8f));
        }
    }

    void UltraCanvasTimeline::Update() {
        if (AutoScale) {
            CalculateTimeRange();
        }
        
        // Update any active event end times for display
        for (auto& datasetPair : Datasets) {
            for (auto& event : datasetPair.second->Events) {
                if (event->IsActive) {
                    // Active events show current time as end time
                }
            }
        }
    }

    // Utility functions
    std::string UltraCanvasTimeline::GetCurrentTimeString() const {
        auto now = std::chrono::steady_clock::now();
        auto timeT = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        std::stringstream ss;
        ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    double UltraCanvasTimeline::GetEventDuration(const std::string& datasetName, const std::string& eventName) const {
        auto datasetIt = Datasets.find(datasetName);
        if (datasetIt == Datasets.end()) {
            return -1.0;
        }
        
        auto& events = datasetIt->second->Events;
        auto eventIt = std::find_if(events.begin(), events.end(),
            [&eventName](const std::shared_ptr<TimelineEvent>& evt) {
                return evt->EventName == eventName;
            });
        
        if (eventIt != events.end()) {
            auto endTime = (*eventIt)->IsActive ? std::chrono::steady_clock::now() : (*eventIt)->EndTime;
            auto duration = endTime - (*eventIt)->StartTime;
            return std::chrono::duration<double>(duration).count();
        }
        
        return -1.0;
    }

    std::vector<std::string> UltraCanvasTimeline::GetActiveEvents() const {
        std::vector<std::string> activeEvents;
        
        for (const auto& datasetPair : Datasets) {
            for (const auto& event : datasetPair.second->Events) {
                if (event->IsActive) {
                    activeEvents.push_back(datasetPair.first + "::" + event->EventName);
                }
            }
        }
        
        return activeEvents;
    }

    // Event callbacks
    void UltraCanvasTimeline::SetOnEventClick(std::function<void(const std::string&, const std::string&)> callback) {
        OnEventClickCallback = callback;
    }

    void UltraCanvasTimeline::SetOnTimelineUpdate(std::function<void()> callback) {
        OnTimelineUpdateCallback = callback;
    }

    void UltraCanvasTimeline::AttachDrawingSurface(UltraCanvasDrawingSurface* surface) {
        DrawingSurface = surface;
    }

    // Global timeline manager implementation
    namespace TimelineManager {
        UltraCanvasTimeline* CreateTimeline(const std::string& timelineName,
                                          int x, int y, int w, int h) {
            auto timeline = std::make_unique<UltraCanvasTimeline>(x, y, w, h);
            UltraCanvasTimeline* ptr = timeline.get();
            GlobalTimelines[timelineName] = std::move(timeline);
            return ptr;
        }
        
        bool DestroyTimeline(const std::string& timelineName) {
            auto it = GlobalTimelines.find(timelineName);
            if (it != GlobalTimelines.end()) {
                GlobalTimelines.erase(it);
                return true;
            }
            return false;
        }
        
        UltraCanvasTimeline* GetTimeline(const std::string& timelineName) {
            auto it = GlobalTimelines.find(timelineName);
            if (it != GlobalTimelines.end()) {
                return it->second.get();
            }
            return nullptr;
        }
        
        std::vector<std::string> GetAllTimelineNames() {
            std::vector<std::string> names;
            for (const auto& pair : GlobalTimelines) {
                names.push_back(pair.first);
            }
            return names;
        }
    }

} // namespace UltraCanvas

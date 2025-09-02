// UltraCanvasTimeline.h
// Interactive timeline component with Gantt chart visualization and time capture
// Version: 1.0.0
// Last Modified: 2025-08-28
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>

namespace UltraCanvas {

    // Color palette definitions for different timeline themes
    enum class TimelinePalette {
        Pastel,      // Soft pastel colors
        Ocean,       // Blue-green gradient theme
        Sunset,      // Orange-pink gradient theme
        Forest,      // Green gradient theme
        Lavender,    // Purple gradient theme
        Corporate,   // Professional blue-gray theme
        Custom       // User-defined palette
    };

    // Timeline visualization types
    enum class TimelineType {
        Standard,    // Traditional horizontal Gantt bars
        Compact,     // Condensed view with smaller bars
        Detailed,    // Extended view with labels and descriptions
        Hierarchical // Tree-like structure with parent-child relationships
    };

    // Timeline display style for data presentation
    enum class TimelineStyle {
        Accumulative,    // Events stack sequentially (traditional Gantt)
        Comparative,     // Events align to same baseline for comparison
        Overlapping      // Events can overlap in time for concurrent visualization
    };

    // Structure to hold color information
    struct TimelineColor {
        float Red;
        float Green;
        float Blue;
        float Alpha;
        
        TimelineColor(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
            : Red(r), Green(g), Blue(b), Alpha(a) {}
    };

    // Structure for individual timeline events
    struct TimelineEvent {
        std::string EventName;
        std::chrono::steady_clock::time_point StartTime;
        std::chrono::steady_clock::time_point EndTime;
        TimelineColor EventColor;
        std::string Description;
        bool IsActive;  // Currently running event
        
        TimelineEvent(const std::string& name)
            : EventName(name), IsActive(false) {}
    };

    // Structure for timeline datasets
    struct TimelineDataset {
        std::string DatasetName;
        std::vector<std::shared_ptr<TimelineEvent>> Events;
        TimelineColor DatasetColor;
        bool Visible;
        
        TimelineDataset(const std::string& name)
            : DatasetName(name), Visible(true) {}
    };

    class UltraCanvasTimeline {
    private:
        std::map<std::string, std::shared_ptr<TimelineDataset>> Datasets;
        std::map<std::string, std::chrono::steady_clock::time_point> ActiveCaptures;
        
        TimelinePalette CurrentPalette;
        std::vector<TimelineColor> CustomColors;
        TimelineType DisplayType;
        TimelineStyle DisplayStyle;
        
        // Display properties
        int Width;
        int Height;
        int PositionX;
        int PositionY;
        
        // Timeline view settings
        std::chrono::steady_clock::time_point ViewStartTime;
        std::chrono::steady_clock::time_point ViewEndTime;
        bool AutoScale;
        float ZoomLevel;
        
        // Visual styling
        int BarHeight;
        int BarSpacing;
        bool ShowGrid;
        bool ShowLabels;
        bool ShowTimeMarkers;
        
        // Helper methods
        TimelineColor GetPaletteColor(int index) const;
        void CalculateTimeRange();
        void DrawTimelineGrid();
        void DrawEventBar(const TimelineEvent& event, int yPosition, int datasetIndex = 0);
        void DrawTimeLabels();
        int CalculateBarPosition(const std::chrono::steady_clock::time_point& time) const;
        int CalculateYPosition(int datasetIndex, int eventIndex) const;
        
    public:
        // Constructor
        UltraCanvasTimeline(int x = 0, int y = 0, int w = 800, int h = 400);
        
        // Destructor
        ~UltraCanvasTimeline();
        
        // Timeline management functions
        bool StartTimeline(const std::string& datasetName, TimelineType type = TimelineType::Standard);
        bool StartTimeCapture(const std::string& datasetName, const std::string& eventName);
        bool EndTimeCapture(const std::string& datasetName, const std::string& eventName);
        
        // Style and layout configuration
        void SetTimelineStyle(TimelineStyle style);
        TimelineStyle GetTimelineStyle() const;
        
        // Dataset management
        bool AddDataset(const std::string& datasetName);
        bool RemoveDataset(const std::string& datasetName);
        bool SetDatasetVisible(const std::string& datasetName, bool visible);
        std::vector<std::string> GetDatasetNames() const;
        
        // Event management
        bool AddEvent(const std::string& datasetName, const std::string& eventName,
                     const std::chrono::steady_clock::time_point& startTime,
                     const std::chrono::steady_clock::time_point& endTime);
        bool RemoveEvent(const std::string& datasetName, const std::string& eventName);
        bool SetEventDescription(const std::string& datasetName, const std::string& eventName,
                               const std::string& description);
        
        // Visual configuration
        void SetPalette(TimelinePalette palette);
        void SetCustomPalette(const std::vector<TimelineColor>& colors);
        void SetTimelineType(TimelineType type);
        void SetPosition(int x, int y);
        void SetSize(int width, int height);
        
        // Display options
        void SetAutoScale(bool autoScale);
        void SetZoomLevel(float zoom);
        void SetTimeRange(const std::chrono::steady_clock::time_point& startTime,
                         const std::chrono::steady_clock::time_point& endTime);
        void SetBarHeight(int height);
        void SetBarSpacing(int spacing);
        void SetShowGrid(bool show);
        void SetShowLabels(bool show);
        void SetShowTimeMarkers(bool show);
        
        // Rendering
        void Render();
        void Update();
        
        // Data export/import
        bool ExportToJson(const std::string& filename) const;
        bool ImportFromJson(const std::string& filename);
        
        // Utility functions
        std::string GetCurrentTimeString() const;
        double GetEventDuration(const std::string& datasetName, const std::string& eventName) const;
        std::vector<std::string> GetActiveEvents() const;
        
        // Event callbacks
        void SetOnEventClick(std::function<void(const std::string&, const std::string&)> callback);
        void SetOnTimelineUpdate(std::function<void()> callback);
        
    private:
        // Callback function pointers
        std::function<void(const std::string&, const std::string&)> OnEventClickCallback;
        std::function<void()> OnTimelineUpdateCallback;
    };

    // Global timeline management functions for convenience
    namespace TimelineManager {
        UltraCanvasTimeline* CreateTimeline(const std::string& timelineName,
                                          int x = 0, int y = 0, int w = 800, int h = 400);
        bool DestroyTimeline(const std::string& timelineName);
        UltraCanvasTimeline* GetTimeline(const std::string& timelineName);
        std::vector<std::string> GetAllTimelineNames();
    }

    // Predefined color schemes
    namespace TimelinePalettes {
        extern const std::vector<TimelineColor> PastelColors;
        extern const std::vector<TimelineColor> OceanColors;
        extern const std::vector<TimelineColor> SunsetColors;
        extern const std::vector<TimelineColor> ForestColors;
        extern const std::vector<TimelineColor> LavenderColors;
        extern const std::vector<TimelineColor> CorporateColors;
    }

} // namespace UltraCanvas

// include/UltraCanvasDatePicker.h
// Interactive date picker component with calendar dropdown and date validation
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasButton.h"
#include <string>
#include <functional>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== DATE STRUCTURE =====
struct Date {
    int year = 2024;
    int month = 1;  // 1-12
    int day = 1;    // 1-31
    
    Date() = default;
    Date(int y, int m, int d) : year(y), month(m), day(d) {}
    
    // Parse from ISO format (YYYY-MM-DD)
    static Date FromISO(const std::string& iso) {
        Date date;
        if (iso.length() >= 10) {
            try {
                date.year = std::stoi(iso.substr(0, 4));
                date.month = std::stoi(iso.substr(5, 2));
                date.day = std::stoi(iso.substr(8, 2));
            } catch (...) {
                // Return default date on parse error
            }
        }
        return date;
    }
    
    // Convert to ISO format (YYYY-MM-DD)
    std::string ToISO() const {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << "-"
            << std::setw(2) << month << "-"
            << std::setw(2) << day;
        return oss.str();
    }
    
    // Convert to display format (e.g., "January 15, 2024")
    std::string ToDisplayString() const {
        const char* monthNames[] = {
            "", "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };
        
        std::ostringstream oss;
        if (month >= 1 && month <= 12) {
            oss << monthNames[month] << " " << day << ", " << year;
        } else {
            oss << ToISO(); // Fallback to ISO format
        }
        return oss.str();
    }
    
    bool IsValid() const {
        if (month < 1 || month > 12) return false;
        if (day < 1) return false;
        
        int daysInMonth = GetDaysInMonth(year, month);
        return day <= daysInMonth;
    }
    
    bool operator==(const Date& other) const {
        return year == other.year && month == other.month && day == other.day;
    }
    
    bool operator!=(const Date& other) const {
        return !(*this == other);
    }
    
    static int GetDaysInMonth(int year, int month) {
        const int daysPerMonth[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        
        if (month == 2 && IsLeapYear(year)) {
            return 29;
        }
        
        if (month >= 1 && month <= 12) {
            return daysPerMonth[month];
        }
        
        return 30; // Default
    }
    
    static bool IsLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }
    
    static Date Today() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        return Date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
};

// ===== DATE FORMAT =====
enum class DateFormat {
    ISO,        // YYYY-MM-DD
    US,         // MM/DD/YYYY
    European,   // DD/MM/YYYY
    Display     // January 15, 2024
};

// ===== CALENDAR POPUP =====
class UltraCanvasCalendarPopup : public UltraCanvasElement {
private:
    StandardProperties properties;
    Date selectedDate;
    Date displayMonth; // Month/year being displayed
    int cellWidth = 30;
    int cellHeight = 25;
    int headerHeight = 30;
    
    Color backgroundColor = Colors::White;
    Color headerColor = Color(50, 120, 200);
    Color selectedColor = Color(100, 150, 255);
    Color hoverColor = Color(230, 240, 255);
    Color todayColor = Color(255, 220, 220);
    Color textColor = Colors::Black;
    Color headerTextColor = Colors::White;
    
    int hoveredDay = -1;
    Date today = Date::Today();
    
public:
    std::function<void(const Date&)> onDateSelected;
    std::function<void()> onClosed;
    
    UltraCanvasCalendarPopup(const std::string& identifier, long id, long x, long y)
        : UltraCanvasElement(identifier, id, x, y, 7 * 30 + 20, 6 * 25 + 30 + 20),
          properties(identifier, id, x, y, 7 * 30 + 20, 6 * 25 + 30 + 20) {
        
        selectedDate = Date::Today();
        displayMonth = selectedDate;
        
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
        properties.z_index = 10000; // Always on top
    }

    void SetSelectedDate(const Date& date) {
        selectedDate = date;
        displayMonth = date;
    }
    
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Draw background and border
        UltraCanvas::DrawFilledRect(GetBounds(), backgroundColor, Colors::Gray, 1.0f);
        
        // Draw header
        DrawHeader();
        
        // Draw calendar grid
        DrawCalendarGrid();
    }
    
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
        return false;
    }
    
private:
    void DrawHeader() {
        Rect2D headerRect(GetX() + 1, GetY() + 1, GetWidth() - 2, headerHeight);
        UltraCanvas::DrawFilledRect(headerRect, headerColor);
        
        // Month/Year text
        std::string monthYear = GetMonthName(displayMonth.month) + " " + std::to_string(displayMonth.year);
        ctx->SetTextColor(headerTextColor);
        ctx->SetFont("Arial", 12.0f);
        
        Point2D textSize = GetRenderContext()->ctx->MeasureText(monthYear);
        Point2D textPos(
            headerRect.x + (headerRect.width - textSize.x) / 2,
            headerRect.y + (headerRect.height + textSize.y) / 2
        );
        ctx->DrawText(monthYear, textPos);
        
        // Navigation arrows
        DrawNavigationArrows(headerRect);
        
        // Day headers
        DrawDayHeaders();
    }
    
    void DrawNavigationArrows(const Rect2D& headerRect) {
        int arrowSize = 16;
        int margin = 5;
        
        // Previous month arrow
        Point2D prevCenter(headerRect.x + margin + arrowSize/2, headerRect.y + headerRect.height/2);
        DrawArrow(prevCenter, arrowSize, true); // pointing left
        
        // Next month arrow  
        Point2D nextCenter(headerRect.x + headerRect.width - margin - arrowSize/2, headerRect.y + headerRect.height/2);
        DrawArrow(nextCenter, arrowSize, false); // pointing right
    }
    
    void DrawArrow(const Point2D& center, int size, bool pointingLeft) {
        ctx->SetStrokeColor(headerTextColor);
        ctx->SetStrokeWidth(2.0f);
        
        int offset = size / 4;
        if (pointingLeft) {
            ctx->DrawLine(Point2D(center.x + offset, center.y - offset), Point2D(center.x - offset, center.y));
            ctx->DrawLine(Point2D(center.x - offset, center.y), Point2D(center.x + offset, center.y + offset));
        } else {
            ctx->DrawLine(Point2D(center.x - offset, center.y - offset), Point2D(center.x + offset, center.y));
            ctx->DrawLine(Point2D(center.x + offset, center.y), Point2D(center.x - offset, center.y + offset));
        }
    }
    
    void DrawDayHeaders() {
        const char* dayNames[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
        
        ctx->SetTextColor(Colors::Gray);
        ctx->SetFont("Arial", 10.0f);
        
        for (int i = 0; i < 7; i++) {
            int x = GetX() + 10 + i * cellWidth;
            int y = GetY() + headerHeight + 5;
            
            ctx->DrawText(dayNames[i], Point2D(x, y + 12));
        }
    }
    
    void DrawCalendarGrid() {
        int startY = GetY() + headerHeight + 20;
        
        // Get first day of month and days in month
        Date firstOfMonth(displayMonth.year, displayMonth.month, 1);
        int firstDayOfWeek = GetDayOfWeek(firstOfMonth);
        int daysInMonth = Date::GetDaysInMonth(displayMonth.year, displayMonth.month);
        
        ctx->SetFont("Arial", 11.0f);
        
        // Draw calendar days
        int dayNumber = 1;
        for (int week = 0; week < 6; week++) {
            for (int dayOfWeek = 0; dayOfWeek < 7; dayOfWeek++) {
                int x = GetX() + 10 + dayOfWeek * cellWidth;
                int y = startY + week * cellHeight;
                
                if ((week == 0 && dayOfWeek < firstDayOfWeek) || dayNumber > daysInMonth) {
                    continue; // Empty cell
                }
                
                Rect2D cellRect(x, y, cellWidth - 2, cellHeight - 2);
                Date cellDate(displayMonth.year, displayMonth.month, dayNumber);
                
                // Determine cell color
                Color cellColor = backgroundColor;
                if (cellDate == selectedDate) {
                    cellColor = selectedColor;
                } else if (cellDate == today) {
                    cellColor = todayColor;
                } else if (hoveredDay == dayNumber) {
                    cellColor = hoverColor;
                }
                
                // Draw cell background
                if (cellColor != backgroundColor) {
                    UltraCanvas::DrawFilledRect(cellRect, cellColor);
                }
                
                // Draw day number
                ctx->SetTextColor(cellDate == selectedDate ? Colors::White : textColor);
                std::string dayStr = std::to_string(dayNumber);
                Point2D textSize = GetRenderContext()->ctx->MeasureText(dayStr);
                Point2D textPos(
                    x + (cellWidth - textSize.x) / 2,
                    y + (cellHeight + textSize.y) / 2
                );
                ctx->DrawText(dayStr, textPos);
                
                dayNumber++;
            }
        }
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) {
            // Clicked outside - close popup
            if (onClosed) onClosed();
            return;
        }
        
        // Check navigation arrows
        if (event.y <= GetY() + headerHeight) {
            int arrowSize = 16;
            int margin = 5;
            
            // Previous month
            if (event.x <= GetX() + margin + arrowSize) {
                NavigateMonth(-1);
                return;
            }
            
            // Next month
            if (event.x >= GetX() + GetWidth() - margin - arrowSize) {
                NavigateMonth(1);
                return;
            }
        }
        
        // Check calendar grid
        int clickedDay = GetDayFromPosition(event.x, event.y);
        if (clickedDay > 0) {
            Date newDate(displayMonth.year, displayMonth.month, clickedDay);
            if (newDate.IsValid()) {
                selectedDate = newDate;
                if (onDateSelected) onDateSelected(selectedDate);
                if (onClosed) onClosed();
            }
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        hoveredDay = GetDayFromPosition(event.x, event.y);
    }
    
    void HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Escape:
                if (onClosed) onClosed();
                break;
                
            case UCKeys::Left:
                NavigateMonth(-1);
                break;
                
            case UCKeys::Right:
                NavigateMonth(1);
                break;
        }
    }
    
    void NavigateMonth(int direction) {
        displayMonth.month += direction;
        if (displayMonth.month < 1) {
            displayMonth.month = 12;
            displayMonth.year--;
        } else if (displayMonth.month > 12) {
            displayMonth.month = 1;
            displayMonth.year++;
        }
    }
    
    int GetDayFromPosition(int x, int y) {
        int startY = GetY() + headerHeight + 20;
        
        if (y < startY) return -1;
        
        int dayOfWeek = (x - GetX() - 10) / cellWidth;
        int week = (y - startY) / cellHeight;
        
        if (dayOfWeek < 0 || dayOfWeek >= 7 || week < 0 || week >= 6) {
            return -1;
        }
        
        Date firstOfMonth(displayMonth.year, displayMonth.month, 1);
        int firstDayOfWeek = GetDayOfWeek(firstOfMonth);
        int daysInMonth = Date::GetDaysInMonth(displayMonth.year, displayMonth.month);
        
        int dayNumber = week * 7 + dayOfWeek - firstDayOfWeek + 1;
        
        if (dayNumber < 1 || dayNumber > daysInMonth) {
            return -1;
        }
        
        return dayNumber;
    }
    
    int GetDayOfWeek(const Date& date) {
        // Zeller's congruence algorithm
        int q = date.day;
        int m = date.month;
        int k = date.year % 100;
        int j = date.year / 100;
        
        if (m < 3) {
            m += 12;
            k--;
            if (k < 0) {
                k = 99;
                j--;
            }
        }
        
        int h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
        return (h + 5) % 7; // Convert to Sunday = 0
    }
    
    std::string GetMonthName(int month) {
        const char* monthNames[] = {
            "", "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };
        
        if (month >= 1 && month <= 12) {
            return monthNames[month];
        }
        return "Unknown";
    }
};

// ===== MAIN DATE PICKER COMPONENT =====
class UltraCanvasDatePicker : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Date state
    Date selectedDate;
    DateFormat displayFormat = DateFormat::Display;
    
    // UI components
    std::shared_ptr<UltraCanvasCalendarPopup> calendar;
    bool calendarVisible = false;
    
    // Appearance
    Color backgroundColor = Colors::White;
    Color borderColor = Colors::Gray;
    Color textColor = Colors::Black;
    Color buttonColor = Color(240, 240, 240);
    Color focusColor = Color(100, 150, 255);
    
    int padding = 8;
    int buttonWidth = 20;
    
    // Validation
    Date minDate;
    Date maxDate;
    bool hasMinDate = false;
    bool hasMaxDate = false;
    
public:
    // ===== EVENTS =====
    std::function<void(const Date&)> onDateChanged;
    std::function<void(const Date&)> onDateSelected;
    std::function<void()> onCalendarOpened;
    std::function<void()> onCalendarClosed;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasDatePicker(const std::string& identifier = "DatePicker", long id = 0,
                         long x = 0, long y = 0, long w = 200, long h = 30)
        : UltraCanvasElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h) {
        
        selectedDate = Date::Today();
        
        properties.MousePtr = MousePointer::Text;
        properties.MouseCtrl = MouseControls::Input;
    }

    // ===== DATE OPERATIONS =====
    void SetDate(const Date& date) {
        if (IsValidDate(date) && selectedDate != date) {
            Date oldDate = selectedDate;
            selectedDate = date;
            
            if (onDateChanged) onDateChanged(selectedDate);
        }
    }
    
    void SetDateFromISO(const std::string& iso) {
        SetDate(Date::FromISO(iso));
    }
    
    const Date& GetDate() const {
        return selectedDate;
    }
    
    std::string GetDateISO() const {
        return selectedDate.ToISO();
    }
    
    std::string GetDateString() const {
        return FormatDate(selectedDate, displayFormat);
    }
    
    // ===== FORMAT CONFIGURATION =====
    void SetDisplayFormat(DateFormat format) {
        displayFormat = format;
    }
    
    DateFormat GetDisplayFormat() const {
        return displayFormat;
    }
    
    // ===== VALIDATION =====
    void SetMinDate(const Date& date) {
        minDate = date;
        hasMinDate = true;
        
        // Adjust current date if needed
        if (selectedDate.year < minDate.year ||
            (selectedDate.year == minDate.year && selectedDate.month < minDate.month) ||
            (selectedDate.year == minDate.year && selectedDate.month == minDate.month && selectedDate.day < minDate.day)) {
            SetDate(minDate);
        }
    }
    
    void SetMaxDate(const Date& date) {
        maxDate = date;
        hasMaxDate = true;
        
        // Adjust current date if needed
        if (selectedDate.year > maxDate.year ||
            (selectedDate.year == maxDate.year && selectedDate.month > maxDate.month) ||
            (selectedDate.year == maxDate.year && selectedDate.month == maxDate.month && selectedDate.day > maxDate.day)) {
            SetDate(maxDate);
        }
    }
    
    void ClearDateRange() {
        hasMinDate = false;
        hasMaxDate = false;
    }
    
    // ===== APPEARANCE =====
    void SetColors(const Color& background, const Color& border, const Color& text) {
        backgroundColor = background;
        borderColor = border;
        textColor = text;
    }
    
    void SetPadding(int paddingValue) {
        padding = paddingValue;
    }
    
    // ===== CALENDAR OPERATIONS =====
    void ShowCalendar() {
        if (calendarVisible) return;
        
        // Create calendar popup
        calendar = std::make_shared<UltraCanvasCalendarPopup>(
            GetIdentifier() + "_calendar", 
            GetIdentifierID() + 1000,
            GetX(), 
            GetY() + GetHeight() + 2
        );
        
        calendar->SetSelectedDate(selectedDate);
        
        calendar->onDateSelected = [this](const Date& date) {
            SetDate(date);
            if (onDateSelected) onDateSelected(date);
        };
        
        calendar->onClosed = [this]() {
            HideCalendar();
        };
        
        calendar->SetVisible(true);
        calendarVisible = true;
        
        if (onCalendarOpened) onCalendarOpened();
    }
    
    void HideCalendar() {
        if (!calendarVisible) return;
        
        if (calendar) {
            calendar->SetVisible(false);
            calendar.reset();
        }
        
        calendarVisible = false;
        
        if (onCalendarClosed) onCalendarClosed();
    }
    
    bool IsCalendarVisible() const {
        return calendarVisible;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Draw background and border
        Color currentBorderColor = IsFocused() ? focusColor : borderColor;
        UltraCanvas::DrawFilledRect(GetBounds(), backgroundColor, currentBorderColor, 1.0f);
        
        // Draw date text
        DrawDateText();
        
        // Draw dropdown button
        DrawDropdownButton();
        
        // Render calendar popup if visible
        if (calendarVisible && calendar) {
            calendar->Render();
        }
    }
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return false;
        
        // Forward events to calendar if visible
        if (calendarVisible && calendar) {
            calendar->OnEvent(event);
            return;
        }
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
                
            case UCEventType::FocusGained:
                // Component gained focus
                break;
                
            case UCEventType::FocusLost:
                HideCalendar();
                break;
        }
        return false;
    }
    
private:
    void DrawDateText() {
        std::string dateText = GetDateString();
        
        ctx->SetTextColor(textColor);
        ctx->SetFont("Arial", 11.0f);
        
        // Calculate text position
        Point2D textSize = GetRenderContext()->ctx->MeasureText(dateText);
        Point2D textPos(
            GetX() + padding,
            GetY() + (GetHeight() + textSize.y) / 2
        );
        
        // Clip text to available space
        int availableWidth = GetWidth() - padding * 2 - buttonWidth;
        Rect2D textRect(GetX() + padding, GetY(), availableWidth, GetHeight());
        ctx->SetClipRect(textRect);
        
        ctx->DrawText(dateText, textPos);
        
        // Reset clipping
        ctx->SetClipRect(GetBounds());
    }
    
    void DrawDropdownButton() {
        int buttonX = GetX() + GetWidth() - buttonWidth;
        Rect2D buttonRect(buttonX, GetY() + 1, buttonWidth - 1, GetHeight() - 2);
        
        Color currentButtonColor = IsHovered() ? Color(230, 230, 230) : buttonColor;
        UltraCanvas::DrawFilledRect(buttonRect, currentButtonColor, borderColor, 1.0f);
        
        // Draw dropdown arrow
        ctx->SetStrokeColor(Colors::Black);
        ctx->SetStrokeWidth(1.0f);
        
        Point2D center(buttonX + buttonWidth / 2, GetY() + GetHeight() / 2);
        int arrowSize = 4;
        
        ctx->DrawLine(Point2D(center.x - arrowSize, center.y - 2), Point2D(center.x, center.y + 2));
        ctx->DrawLine(Point2D(center.x, center.y + 2), Point2D(center.x + arrowSize, center.y - 2));
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        // Check if clicking on dropdown button
        int buttonX = GetX() + GetWidth() - buttonWidth;
        if (event.x >= buttonX) {
            ToggleCalendar();
        } else {
            // Clicking on text area - also show calendar
            ShowCalendar();
        }
    }
    
    void HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Space:
            case UCKeys::Return:
                ToggleCalendar();
                break;
                
            case UCKeys::Escape:
                HideCalendar();
                break;
                
            case UCKeys::Left:
                if (!calendarVisible) {
                    NavigateDate(-1);
                }
                break;
                
            case UCKeys::Right:
                if (!calendarVisible) {
                    NavigateDate(1);
                }
                break;
                
            case UCKeys::Up:
                if (!calendarVisible) {
                    NavigateDate(-7); // Previous week
                }
                break;
                
            case UCKeys::Down:
                if (!calendarVisible) {
                    NavigateDate(7); // Next week
                }
                break;
        }
    }
    
    void ToggleCalendar() {
        if (calendarVisible) {
            HideCalendar();
        } else {
            ShowCalendar();
        }
    }
    
    void NavigateDate(int days) {
        // Simple date navigation (doesn't handle month/year boundaries perfectly)
        Date newDate = selectedDate;
        newDate.day += days;
        
        // Adjust for month boundaries
        while (newDate.day < 1) {
            newDate.month--;
            if (newDate.month < 1) {
                newDate.month = 12;
                newDate.year--;
            }
            newDate.day += Date::GetDaysInMonth(newDate.year, newDate.month);
        }
        
        while (newDate.day > Date::GetDaysInMonth(newDate.year, newDate.month)) {
            newDate.day -= Date::GetDaysInMonth(newDate.year, newDate.month);
            newDate.month++;
            if (newDate.month > 12) {
                newDate.month = 1;
                newDate.year++;
            }
        }
        
        if (IsValidDate(newDate)) {
            SetDate(newDate);
        }
    }
    
    bool IsValidDate(const Date& date) const {
        if (!date.IsValid()) return false;
        
        if (hasMinDate) {
            if (date.year < minDate.year ||
                (date.year == minDate.year && date.month < minDate.month) ||
                (date.year == minDate.year && date.month == minDate.month && date.day < minDate.day)) {
                return false;
            }
        }
        
        if (hasMaxDate) {
            if (date.year > maxDate.year ||
                (date.year == maxDate.year && date.month > maxDate.month) ||
                (date.year == maxDate.year && date.month == maxDate.month && date.day > maxDate.day)) {
                return false;
            }
        }
        
        return true;
    }
    
    std::string FormatDate(const Date& date, DateFormat format) const {
        switch (format) {
            case DateFormat::ISO:
                return date.ToISO();
            case DateFormat::US:
                return std::to_string(date.month) + "/" + std::to_string(date.day) + "/" + std::to_string(date.year);
            case DateFormat::European:
                return std::to_string(date.day) + "/" + std::to_string(date.month) + "/" + std::to_string(date.year);
            case DateFormat::Display:
                return date.ToDisplayString();
            default:
                return date.ToISO();
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasDatePicker> CreateDatePicker(
    const std::string& identifier, long id, long x, long y, long w = 200, long h = 30) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasDatePicker>(id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasDatePicker> CreateDatePicker(
    const std::string& identifier, long id, const Rect2D& bounds) {
    return CreateDatePicker(identifier, id, static_cast<long>(bounds.x), static_cast<long>(bounds.y),
                           static_cast<long>(bounds.width), static_cast<long>(bounds.height));
}

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasDatePicker> CreateDatePickerWithRange(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const Date& minDate, const Date& maxDate) {
    auto picker = CreateDatePicker(identifier, id, x, y, w, h);
    picker->SetMinDate(minDate);
    picker->SetMaxDate(maxDate);
    return picker;
}

inline std::shared_ptr<UltraCanvasDatePicker> CreateDatePickerWithInitialDate(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& initialDateISO) {
    auto picker = CreateDatePicker(identifier, id, x, y, w, h);
    picker->SetDateFromISO(initialDateISO);
    return picker;
}

// ===== LEGACY C-STYLE INTERFACE =====
extern "C" {
    static UltraCanvasDatePicker* g_currentDatePicker = nullptr;
    
    void CreateDatePicker(int x, int y) {
        g_currentDatePicker = new UltraCanvasDatePicker("legacy_datepicker", 9999, x, y, 200, 30);
    }
    
    void SetDate(const char* dateISO) {
        if (g_currentDatePicker && dateISO) {
            g_currentDatePicker->SetDateFromISO(dateISO);
        }
    }
    
    const char* GetDate() {
        static std::string dateString;
        if (g_currentDatePicker) {
            dateString = g_currentDatePicker->GetDateISO();
            return dateString.c_str();
        }
        return "2024-01-01";
    }
    
    void ShowDatePickerCalendar() {
        if (g_currentDatePicker) {
            g_currentDatePicker->ShowCalendar();
        }
    }
    
    void HideDatePickerCalendar() {
        if (g_currentDatePicker) {
            g_currentDatePicker->HideCalendar();
        }
    }
}

} // namespace UltraCanvas
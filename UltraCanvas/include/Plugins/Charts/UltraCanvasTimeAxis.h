// include/Plugins/Charts/UltraCanvasTimeAxis.h
// Header-only date axis: date<->pixel projection, scale resolution and two-tier
// tick generation. Shared by UltraCanvasTimelineChart and available to any other
// element that needs a calendar axis.
//
// Positions are "day serials": days since 1970-01-01, matching GanttDate::serial.
// The type is double so sub-day scales (hours, minutes) work - the fractional
// part is the fraction of the day.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasCalendarDate.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SCALE
// =============================================================================

    enum class TimelineScale {
        Auto,      // Chosen from the pixels-per-day of the current view
        Minutes,
        Hours,
        Days,
        Weeks,
        Months,
        Quarters,
        Years,
        Decades
    };

    struct TimeAxisTick {
        double serial = 0.0;   // Day serial of the tick
        double x = 0.0;        // Pixel position
        double width = 0.0;    // Pixel width of the slot this tick opens
        std::string label;
        bool isWeekend = false;
    };

// =============================================================================
// LABEL HELPERS
// =============================================================================

    namespace TimeAxisText {
        inline const char* MonthShort(int month) {
            static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            return (month >= 1 && month <= 12) ? names[month - 1] : "";
        }
        inline const char* WeekdayShort(int weekdaySunday0) {
            static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            return (weekdaySunday0 >= 0 && weekdaySunday0 <= 6) ? names[weekdaySunday0] : "";
        }
        inline std::string TwoDigits(int value) {
            std::string s = std::to_string(value);
            return s.size() < 2 ? "0" + s : s;
        }
    }

// =============================================================================
// TIME AXIS
// =============================================================================

    class TimeAxis {
    public:
        // ===== RANGE =====
        void SetRange(double startSerial, double endSerial) {
            rangeStart = startSerial;
            rangeEnd = std::max(endSerial, startSerial + kMinSpanDays);
        }
        void SetPixelRange(double xStart, double xEnd) {
            pixelStart = xStart;
            pixelEnd = std::max(xEnd, xStart + 1.0);
        }
        double GetRangeStart() const { return rangeStart; }
        double GetRangeEnd() const { return rangeEnd; }
        double GetSpanDays() const { return rangeEnd - rangeStart; }
        double GetPixelsPerDay() const { return (pixelEnd - pixelStart) / GetSpanDays(); }

        // Grows the range by a fraction of its span on both sides
        void Pad(double fraction) {
            const double pad = GetSpanDays() * fraction;
            rangeStart -= pad;
            rangeEnd += pad;
        }

        // ===== PROJECTION =====
        double ToPixel(double serial) const {
            return pixelStart + (serial - rangeStart) * GetPixelsPerDay();
        }
        double ToSerial(double pixel) const {
            return rangeStart + (pixel - pixelStart) / GetPixelsPerDay();
        }

        // Zooms by a factor about a fixed pixel position (wheel zoom).
        // factor < 1 zooms in.
        void ZoomAbout(double pixel, double factor) {
            const double anchor = ToSerial(pixel);
            const double newSpan = std::clamp(GetSpanDays() * factor, kMinSpanDays, kMaxSpanDays);
            const double t = (anchor - rangeStart) / GetSpanDays();
            rangeStart = anchor - newSpan * t;
            rangeEnd = rangeStart + newSpan;
        }
        void PanPixels(double deltaPixels) {
            const double deltaDays = deltaPixels / GetPixelsPerDay();
            rangeStart -= deltaDays;
            rangeEnd -= deltaDays;
        }

        // ===== SCALE =====
        TimelineScale Resolve(TimelineScale requested) const {
            if (requested != TimelineScale::Auto) return requested;
            const double ppd = GetPixelsPerDay();
            if (ppd >= 1500.0) return TimelineScale::Minutes;
            if (ppd >= 200.0)  return TimelineScale::Hours;
            if (ppd >= 14.0)   return TimelineScale::Days;
            if (ppd >= 3.5)    return TimelineScale::Weeks;
            if (ppd >= 0.9)    return TimelineScale::Months;
            if (ppd >= 0.3)    return TimelineScale::Quarters;
            if (ppd >= 0.03)   return TimelineScale::Years;
            return TimelineScale::Decades;
        }

        // The coarser tier drawn above the minor one
        static TimelineScale MajorFor(TimelineScale minor) {
            switch (minor) {
                case TimelineScale::Minutes:  return TimelineScale::Hours;
                case TimelineScale::Hours:    return TimelineScale::Days;
                case TimelineScale::Days:     return TimelineScale::Months;
                case TimelineScale::Weeks:    return TimelineScale::Months;
                case TimelineScale::Months:   return TimelineScale::Years;
                case TimelineScale::Quarters: return TimelineScale::Years;
                case TimelineScale::Years:    return TimelineScale::Decades;
                case TimelineScale::Decades:
                default:                      return TimelineScale::Decades;
            }
        }

        // ===== TICKS =====
        // Ticks covering the visible range, each carrying the pixel width of the
        // slot it opens. `major` switches to the coarser label form.
        std::vector<TimeAxisTick> Ticks(TimelineScale scale, bool major) const {
            std::vector<TimeAxisTick> ticks;
            if (scale == TimelineScale::Auto) scale = Resolve(scale);

            double cursor = FloorTo(rangeStart, scale);
            int guard = 0;
            while (cursor < rangeEnd && guard++ < kMaxTicks) {
                const double next = StepFrom(cursor, scale);
                TimeAxisTick tick;
                tick.serial = cursor;
                tick.x = ToPixel(cursor);
                tick.width = ToPixel(next) - tick.x;
                tick.label = Label(cursor, scale, major);
                tick.isWeekend = (scale == TimelineScale::Days) &&
                                 IsWeekendSerial(cursor);
                ticks.push_back(tick);
                cursor = next;
            }
            return ticks;
        }

        // ===== CONVERSION HELPERS =====
        static double Serial(int year, int month, int day) {
            return static_cast<double>(CalendarDaysFromCivil(year, month, day));
        }
        static double Serial(int year, int month, int day, int hour, int minute) {
            return Serial(year, month, day) +
                   (hour * 60.0 + minute) / 1440.0;
        }
        static bool IsWeekendSerial(double serial) {
            const int wd = CalendarWeekdaySunday0(static_cast<long>(std::floor(serial)));
            return wd == 0 || wd == 6;
        }

        // Formats a serial as a short human date, for tooltips and captions
        static std::string FormatDate(double serial) {
            int y, m, d;
            CalendarCivilFromDays(static_cast<long>(std::floor(serial)), y, m, d);
            return std::to_string(d) + " " + TimeAxisText::MonthShort(m) + " " +
                   std::to_string(y);
        }
        static std::string FormatShortDate(double serial) {
            int y, m, d;
            CalendarCivilFromDays(static_cast<long>(std::floor(serial)), y, m, d);
            return std::to_string(d) + "/" + std::to_string(m) + "/" +
                   TimeAxisText::TwoDigits(y % 100);
        }

    private:
        static constexpr double kMinSpanDays = 1.0 / 1440.0;   // one minute
        static constexpr double kMaxSpanDays = 400000.0;       // ~1100 years
        static constexpr int kMaxTicks = 2000;

        double rangeStart = 0.0;
        double rangeEnd = 30.0;
        double pixelStart = 0.0;
        double pixelEnd = 100.0;

        // Start of the period containing `serial`
        static double FloorTo(double serial, TimelineScale scale) {
            const long day = static_cast<long>(std::floor(serial));
            const double frac = serial - static_cast<double>(day);
            int y, m, d;
            CalendarCivilFromDays(day, y, m, d);

            switch (scale) {
                case TimelineScale::Minutes:
                    return static_cast<double>(day) + std::floor(frac * 1440.0) / 1440.0;
                case TimelineScale::Hours:
                    return static_cast<double>(day) + std::floor(frac * 24.0) / 24.0;
                case TimelineScale::Days:
                    return static_cast<double>(day);
                case TimelineScale::Weeks: {
                    // Weeks start on Monday
                    const int wd = CalendarWeekdaySunday0(day);
                    const int backToMonday = (wd + 6) % 7;
                    return static_cast<double>(day - backToMonday);
                }
                case TimelineScale::Months:
                    return static_cast<double>(CalendarDaysFromCivil(y, m, 1));
                case TimelineScale::Quarters: {
                    const int qm = ((m - 1) / 3) * 3 + 1;
                    return static_cast<double>(CalendarDaysFromCivil(y, qm, 1));
                }
                case TimelineScale::Years:
                    return static_cast<double>(CalendarDaysFromCivil(y, 1, 1));
                case TimelineScale::Decades:
                default: {
                    const int decade = (y >= 0) ? (y / 10) * 10 : ((y - 9) / 10) * 10;
                    return static_cast<double>(CalendarDaysFromCivil(decade, 1, 1));
                }
            }
        }

        // Start of the period after the one beginning at `serial`
        static double StepFrom(double serial, TimelineScale scale) {
            const long day = static_cast<long>(std::floor(serial));
            int y, m, d;
            CalendarCivilFromDays(day, y, m, d);

            switch (scale) {
                case TimelineScale::Minutes: return serial + 1.0 / 1440.0;
                case TimelineScale::Hours:   return serial + 1.0 / 24.0;
                case TimelineScale::Days:    return serial + 1.0;
                case TimelineScale::Weeks:   return serial + 7.0;
                case TimelineScale::Months: {
                    const int nm = (m == 12) ? 1 : m + 1;
                    const int ny = (m == 12) ? y + 1 : y;
                    return static_cast<double>(CalendarDaysFromCivil(ny, nm, 1));
                }
                case TimelineScale::Quarters: {
                    const int qm = ((m - 1) / 3) * 3 + 1;
                    const int nm = (qm + 3 > 12) ? qm + 3 - 12 : qm + 3;
                    const int ny = (qm + 3 > 12) ? y + 1 : y;
                    return static_cast<double>(CalendarDaysFromCivil(ny, nm, 1));
                }
                case TimelineScale::Years:
                    return static_cast<double>(CalendarDaysFromCivil(y + 1, 1, 1));
                case TimelineScale::Decades:
                default: {
                    const int decade = (y >= 0) ? (y / 10) * 10 : ((y - 9) / 10) * 10;
                    return static_cast<double>(CalendarDaysFromCivil(decade + 10, 1, 1));
                }
            }
        }

        static std::string Label(double serial, TimelineScale scale, bool major) {
            const long day = static_cast<long>(std::floor(serial));
            const double frac = serial - static_cast<double>(day);
            int y, m, d;
            CalendarCivilFromDays(day, y, m, d);

            switch (scale) {
                case TimelineScale::Minutes: {
                    const int minutes = static_cast<int>(std::lround(frac * 1440.0));
                    return TimeAxisText::TwoDigits(minutes / 60) + ":" +
                           TimeAxisText::TwoDigits(minutes % 60);
                }
                case TimelineScale::Hours: {
                    const int hour = static_cast<int>(std::lround(frac * 24.0));
                    return TimeAxisText::TwoDigits(hour) + ":00";
                }
                case TimelineScale::Days:
                    return major ? (std::to_string(d) + " " + TimeAxisText::MonthShort(m))
                                 : std::to_string(d);
                case TimelineScale::Weeks:
                    return major ? (TimeAxisText::MonthShort(m) + std::string(" ") + std::to_string(y))
                                 : (std::to_string(d) + " " + TimeAxisText::MonthShort(m));
                case TimelineScale::Months:
                    return major ? (TimeAxisText::MonthShort(m) + std::string(" ") + std::to_string(y))
                                 : TimeAxisText::MonthShort(m);
                case TimelineScale::Quarters:
                    return "Q" + std::to_string((m - 1) / 3 + 1) +
                           (major ? (" " + std::to_string(y)) : "");
                case TimelineScale::Years:
                    return std::to_string(y);
                case TimelineScale::Decades:
                default:
                    return std::to_string((y / 10) * 10) + "s";
            }
        }
    };

} // namespace UltraCanvas

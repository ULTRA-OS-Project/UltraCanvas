// include/Plugins/Charts/UltraCanvasCalendarDate.h
// Minimal proleptic-Gregorian date helpers (header-only, no dependencies) used
// by the calendar heatmap. Based on Howard Hinnant's public-domain civil/serial
// algorithms. "Serial" is the day count since 1970-01-01 (can be negative).
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

namespace UltraCanvas {

// Days since 1970-01-01 for the civil date (y, m, d). m in [1,12], d in [1,31].
    inline long CalendarDaysFromCivil(int y, int m, int d) {
        y -= (m <= 2);
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);             // [0, 399]
        const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // [0, 146096]
        return era * 146097 + static_cast<long>(doe) - 719468;
    }

// Inverse of CalendarDaysFromCivil: serial -> civil (y, m, d).
    inline void CalendarCivilFromDays(long z, int& y, int& m, int& d) {
        z += 719468;
        const long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);          // [0, 146096]
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
        const int yy = static_cast<int>(yoe) + static_cast<int>(era) * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          // [0, 365]
        const unsigned mp = (5 * doy + 2) / 153;                               // [0, 11]
        d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);                    // [1, 31]
        m = static_cast<int>(mp + (mp < 10 ? 3 : -9));                         // [1, 12]
        y = yy + (m <= 2);
    }

// Day of week with Sunday = 0 ... Saturday = 6.
    inline int CalendarWeekdaySunday0(long serial) {
        return static_cast<int>(((serial % 7) + 4 + 7) % 7);
    }

} // namespace UltraCanvas

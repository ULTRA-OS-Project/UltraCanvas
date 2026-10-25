// Apps/UltraDBAdmin/engine/DbAdminMonitor.h
// The Server tab's monitor (Docs/Research/UltraDBAdminDesignProposal.md,
// "Status / monitor"): the server's status (UltraDb_ServerStatus) read every
// few seconds, turned into the charts the tab draws - the sessions connected
// and running as they are; statements, transactions, rows read and written
// and the traffic as rates per second; the buffer cache's hit ratio over
// each interval. A chart is there only when the server keeps its measure.
//
// A counter that goes back - the server restarted - starts its rate again
// from the new value rather than drawing a negative one.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace UltraDBAdmin {

struct MonitorPoint {
    double seconds = 0;   // when it was read, from the monitor's first reading
    double value = 0;
    std::time_t at = 0;   // the wall clock's time of it, for the axis
};

struct MonitorChart {
    std::string key;                    // "sessions", "queries", ... (fixed)
    std::string title;                  // "Sessions", "Rows read"
    std::string unit;                   // "", "/s", "KB/s", "%"
    std::vector<MonitorPoint> points;   // oldest first, at most kMonitorPoints
};

// How often the tab reads, and how much of it a chart keeps: four minutes.
constexpr int kMonitorIntervalMs = 2000;
constexpr size_t kMonitorPoints = 120;

class ServerMonitor {
public:
    // Forget every reading: the next one starts the charts again.
    void Clear();

    // One reading - UltraDb_ServerStatus's rows - taken at `seconds` on a
    // steady clock, `at` on the wall clock. A reading no later than the last
    // is ignored.
    void Add(double seconds, const UltraDbResultSet& status, std::time_t at = 0);

    // The charts, in the order the tab shows them; empty before the first
    // reading.
    const std::vector<MonitorChart>& Charts() const { return charts_; }
    size_t Readings() const { return readings_; }

private:
    std::vector<MonitorChart> charts_;
    std::map<std::string, int64_t> last_;   // the previous reading's values
    double start_ = 0;
    double lastSeconds_ = 0;
    size_t readings_ = 0;
};

// A chart's value as its title shows it: "12", "3.5", "1,234 KB/s".
std::string MonitorValueText(double value, const std::string& unit);

// A point's time as the axis shows it, local: "17:59:21".
std::string MonitorClockText(std::time_t at);

} // namespace UltraDBAdmin

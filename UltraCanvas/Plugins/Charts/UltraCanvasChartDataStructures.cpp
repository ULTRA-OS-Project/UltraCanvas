// Plugins/Charts/UltraCanvasChartDataStructures.cpp
// Essential data structures for chart rendering
// Version: 1.0.3 - a CSV row whose x or y is not a number is skipped, wherever it is
//                  (it was plotted at the origin)
// Version: 1.0.2 - a CSV's first line is a header only when its x and y columns
//                  are not numbers ("5,120,0,May" is data)
// Version: 1.0.1
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include <sstream>
#include "UltraCanvasTextUtils.h"   // TryParseFloat - dot-decimal, non-throwing
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

    namespace {
        // A CSV line is a data row when its first two columns (x and y) read
        // as numbers. Anything else is skipped wherever it stands: a header
        // ("x,y", "Month,Sales"), a blank line, or a row that cannot be read,
        // which used to be plotted at the origin. Looking for the letters x or
        // y anywhere dropped a first row labelled "May" or "July".
        bool IsCSVDataLine(const std::string& line) {
            std::stringstream ss(line);
            std::string cell;
            double value = 0.0;
            for (int column = 0; column < 2; ++column) {
                if (!std::getline(ss, cell, ',') || !TryParseFloat(cell, value)) return false;
            }
            return true;
        }
    }

    // ChartDataVector
    void ChartDataVector::LoadFromCSV(const std::string &filePath) {
        std::ifstream file(UltraCanvas::PathFromUtf8(filePath));
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open CSV file: " + filePath);
        }

        data.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (IsCSVDataLine(line)) data.push_back(ParseCSVLine(line));
        }
    }

    ChartDataPoint ChartDataVector::ParseCSVLine(const std::string &line) {
        std::stringstream ss(line);
        std::string cell;
        std::vector <std::string> values;

        while (std::getline(ss, cell, ',')) {
            // Trim whitespace
            cell.erase(0, cell.find_first_not_of(" \t"));
            cell.erase(cell.find_last_not_of(" \t") + 1);
            values.push_back(cell);
        }

        if (values.size() >= 2) {
            double x = 0.0, y = 0.0, z = 0.0;
            if (!TryParseFloat(values[0], x) || !TryParseFloat(values[1], y)) {
                return ChartDataPoint(0, 0, 0);
            }
            if (values.size() > 2) TryParseFloat(values[2], z);
            std::string label = values.size() > 3 ? values[3] : "";

            return ChartDataPoint(x, y, z, label);
        }

        return ChartDataPoint(0, 0, 0);
    }



    // ChartDataStream
    size_t ChartDataStream::GetPointCount() const {
        if (!pointCountCalculated) {
            CalculatePointCount();
            pointCountCalculated = true;
        }
        return totalPoints;
    }

    ChartDataPoint ChartDataStream::GetPoint(size_t index) {
        // Check if point is in current cache
        if (index >= cacheStartIndex && index < cacheStartIndex + cache.size()) {
            return cache[index - cacheStartIndex];
        }

        // Load appropriate chunk
        LoadChunk(index);

        if (index >= cacheStartIndex && index < cacheStartIndex + cache.size()) {
            return cache[index - cacheStartIndex];
        }

        // Fallback - return empty point
        return ChartDataPoint(0, 0, 0);
    }

    void ChartDataStream::LoadFromCSV(const std::string &path) {
        filePath = path;
        pointCountCalculated = false;
        cache.clear();
        cacheStartIndex = 0;
    }

    void ChartDataStream::LoadFromArray(const std::vector<ChartDataPoint> &data) {
        // Not supported for streaming - use ChartDataVector instead
        throw std::runtime_error("ChartDataStream doesn't support LoadFromArray - use ChartDataVector");
    }

    void ChartDataStream::CalculatePointCount() const {
        std::ifstream file(UltraCanvas::PathFromUtf8(filePath));
        if (!file.is_open()) {
            totalPoints = 0;
            return;
        }

        std::string line;
        totalPoints = 0;
        while (std::getline(file, line)) {
            if (IsCSVDataLine(line)) totalPoints++;
        }
    }

    void ChartDataStream::LoadChunk(size_t targetIndex) const {
        std::ifstream file(UltraCanvas::PathFromUtf8(filePath));
        if (!file.is_open()) return;

        // Calculate chunk start
        cacheStartIndex = (targetIndex / CHUNK_SIZE) * CHUNK_SIZE;
        cache.clear();
        cache.reserve(CHUNK_SIZE);

        // Points are numbered over the data rows only, as CalculatePointCount
        // counts them, so a header, a blank line or an unreadable row shifts
        // neither the count nor the chunks.
        std::string line;
        size_t dataIndex = 0;
        while (cache.size() < CHUNK_SIZE && std::getline(file, line)) {
            if (!IsCSVDataLine(line)) continue;
            if (dataIndex >= cacheStartIndex) cache.push_back(ParseCSVLine(line));
            ++dataIndex;
        }
    }

    ChartDataPoint ChartDataStream::ParseCSVLine(const std::string &line) const {
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> values;

        while (std::getline(ss, cell, ',')) {
            // Trim whitespace
            cell.erase(0, cell.find_first_not_of(" \t"));
            cell.erase(cell.find_last_not_of(" \t") + 1);
            values.push_back(cell);
        }

        if (values.size() >= 2) {
            double x = 0.0, y = 0.0, z = 0.0;
            if (!TryParseFloat(values[0], x) || !TryParseFloat(values[1], y)) {
                return ChartDataPoint(0, 0, 0);
            }
            if (values.size() > 2) TryParseFloat(values[2], z);
            std::string label = values.size() > 3 ? values[3] : "";

            return ChartDataPoint(x, y, z, label);
        }

        return ChartDataPoint(0, 0, 0);
    }


    // ChartDataBounds
    void ChartDataBounds::Expand(double x, double y, double z) {
        if (!hasData) {
            minX = maxX = x;
            minY = maxY = y;
            minZ = maxZ = z;
            hasData = true;
        } else {
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
        }
    }

    void ChartDataBounds::AddMargin(double marginPercent) {
        if (!hasData) return;

        double xMargin = GetXRange() * marginPercent;
        double yMargin = GetYRange() * marginPercent;

        minX -= xMargin;
        maxX += xMargin;
        minY -= yMargin;
        maxY += yMargin;
    }
}
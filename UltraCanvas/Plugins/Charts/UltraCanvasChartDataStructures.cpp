// Plugins/Charts/UltraCanvasChartDataStructures.cpp
// Essential data structures for chart rendering
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework

#include <sstream>
#include "Plugins/Charts/UltraCanvasChartDataStructures.h"

namespace UltraCanvas {

    // ChartDataVector
    void ChartDataVector::LoadFromCSV(const std::string &filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open CSV file: " + filePath);
        }

        data.clear();
        std::string line;
        bool skipHeader = false;

        // Check for header
        if (std::getline(file, line)) {
            if (line.find("x") != std::string::npos || line.find("y") != std::string::npos) {
                skipHeader = true;
            } else {
                // Process first line as data
                data.push_back(ParseCSVLine(line));
            }
        }

        while (std::getline(file, line)) {
            if (!line.empty()) {
                data.push_back(ParseCSVLine(line));
            }
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
            double x = std::stod(values[0]);
            double y = std::stod(values[1]);
            double z = values.size() > 2 ? std::stod(values[2]) : 0.0;
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
        std::ifstream file(filePath);
        if (!file.is_open()) {
            totalPoints = 0;
            return;
        }

        std::string line;
        totalPoints = 0;

        // Skip header if present
        if (std::getline(file, line)) {
            // Check if first line looks like a header
            if (line.find("x") != std::string::npos || line.find("y") != std::string::npos) {
                // Skip header
            } else {
                totalPoints = 1;  // First line is data
            }
        }

        while (std::getline(file, line)) {
            if (!line.empty()) totalPoints++;
        }
    }

    void ChartDataStream::LoadChunk(size_t targetIndex) const {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        // Calculate chunk start
        cacheStartIndex = (targetIndex / CHUNK_SIZE) * CHUNK_SIZE;
        cache.clear();
        cache.reserve(CHUNK_SIZE);

        std::string line;
        size_t currentIndex = 0;

        // Skip header if present
        if (std::getline(file, line)) {
            if (line.find("x") == std::string::npos && line.find("y") == std::string::npos) {
                // First line is data, process it
                if (currentIndex >= cacheStartIndex && cache.size() < CHUNK_SIZE) {
                    cache.push_back(ParseCSVLine(line));
                }
                currentIndex++;
            }
        }

        // Skip to target chunk
        while (currentIndex < cacheStartIndex && std::getline(file, line)) {
            currentIndex++;
        }

        // Load chunk data
        while (cache.size() < CHUNK_SIZE && std::getline(file, line)) {
            if (!line.empty()) {
                cache.push_back(ParseCSVLine(line));
            }
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
            double x = std::stod(values[0]);
            double y = std::stod(values[1]);
            double z = values.size() > 2 ? std::stod(values[2]) : 0.0;
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
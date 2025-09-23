// UltraCanvasDivergingDataSource.h
// Data source implementation for diverging/multi-valued bar charts
// Version: 1.0.0
// Last Modified: 2025-09-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartDataStructures.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <memory>

namespace UltraCanvas {

// ===== DIVERGING DATA POINT =====
    struct DivergingChartPoint : public ChartDataPoint {
        std::map<std::string, float> categoryValues;  // Values for each category
        std::string rowLabel;                         // Y-axis label

        DivergingChartPoint(const std::string& label, double xPos, double totalValue)
                : ChartDataPoint(xPos, totalValue, 0, label, totalValue), rowLabel(label) {}

        void AddCategoryValue(const std::string& category, float value) {
            categoryValues[category] = value;
        }
    };

// ===== DIVERGING DATA SOURCE =====
    class DivergingDataSource : public IChartDataSource {
    private:
        std::vector<DivergingChartPoint> divergingData;
        std::vector<std::string> categories;
        std::map<std::string, bool> categoryPositions; // true = positive side, false = negative

    public:
        // IChartDataSource interface implementation
        size_t GetPointCount() const override {
            return divergingData.size();
        }

        ChartDataPoint GetPoint(size_t index) const override {
            if (index < divergingData.size()) {
                return divergingData[index];
            }
            return ChartDataPoint(0, 0);
        }

        bool SupportsStreaming() const override {
            return false;
        }

        void LoadFromCSV(const std::string& filePath) override {
            // CSV format: rowLabel,Category1,Category2,Category3,...
            // Could implement CSV loading for diverging data
        }

        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            // Convert standard points to diverging points
            divergingData.clear();
            for (const auto& point : data) {
                DivergingChartPoint divPoint(point.label, point.x, point.y);
                divergingData.push_back(divPoint);
            }
        }

        // ===== DIVERGING-SPECIFIC METHODS =====

        void AddCategory(const std::string& category, bool isPositive) {
            categories.push_back(category);
            categoryPositions[category] = isPositive;
        }

        void AddDataRow(const std::string& rowLabel, const std::map<std::string, float>& values) {
            double xPos = divergingData.size();
            double totalValue = 0;

            // Calculate total value
            for (const auto& [category, value] : values) {
                totalValue += std::abs(value);
            }

            DivergingChartPoint point(rowLabel, xPos, totalValue);

            // Add category values
            for (const auto& [category, value] : values) {
                point.AddCategoryValue(category, value);
            }

            divergingData.push_back(point);
        }

        void AddDataMatrix(const std::vector<std::string>& rowLabels,
                           const std::vector<std::vector<float>>& matrix) {
            divergingData.clear();

            for (size_t row = 0; row < rowLabels.size() && row < matrix.size(); ++row) {
                std::map<std::string, float> values;

                for (size_t col = 0; col < categories.size() && col < matrix[row].size(); ++col) {
                    values[categories[col]] = matrix[row][col];
                }

                AddDataRow(rowLabels[row], values);
            }
        }

        void Clear() {
            divergingData.clear();
        }

        // Get specific diverging point
        const DivergingChartPoint& GetDivergingPoint(size_t index) const {
            static DivergingChartPoint empty("", 0, 0);
            if (index < divergingData.size()) {
                return divergingData[index];
            }
            return empty;
        }

        // Get all categories
        const std::vector<std::string>& GetCategories() const {
            return categories;
        }

        // Check if category is on positive side
        bool IsCategoryPositive(const std::string& category) const {
            auto it = categoryPositions.find(category);
            if (it != categoryPositions.end()) {
                return it->second;
            }
            return false;
        }

        // Calculate bounds
        void GetDataBounds(float& maxNegative, float& maxPositive) const {
            maxNegative = 0;
            maxPositive = 0;

            for (const auto& point : divergingData) {
                float negTotal = 0;
                float posTotal = 0;

                for (const auto& [category, value] : point.categoryValues) {
                    if (IsCategoryPositive(category)) {
                        posTotal += std::abs(value);
                    } else {
                        negTotal += std::abs(value);
                    }
                }

                maxNegative = std::max(maxNegative, negTotal);
                maxPositive = std::max(maxPositive, posTotal);
            }
        }
    };

// ===== FACTORY FUNCTION =====
    inline std::shared_ptr<DivergingDataSource> CreateDivergingDataSource() {
        return std::make_shared<DivergingDataSource>();
    }

} // namespace UltraCanvas
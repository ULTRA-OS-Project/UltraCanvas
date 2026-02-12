// include/UltraCanvasVirtualInterfaces.h
// Unified virtual rendering and data access interfaces for huge datasets
// Version: 1.0.0
// Last Modified: 2026-01-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
class UltraCanvasTemplate;

// =============================================================================
// PART 1: DATA ACCESS INTERFACES (for huge datasets)
// =============================================================================

/**
 * @brief Generic data item that can be used across all components
 * 
 * This is a flexible container that can represent:
 * - List items
 * - Table cells/rows
 * - Tree nodes
 * - Chart data points
 * - Any other data element
 */
struct VirtualDataItem {
    // Core data
    std::string primaryText;                           // Main display text
    std::string secondaryText;                         // Subtitle/additional info
    std::string iconIdentifier;                        // Icon path or identifier
    
    // Visual properties
    Color textColor = Colors::Black;
    Color backgroundColor = Colors::Transparent;
    bool enabled = true;
    
    // Metadata
    void* userData = nullptr;                          // Custom user data pointer
    int dataIndex = -1;                                // Index in original dataset
    
    // Flexible property bag for template binding or custom data
    std::unordered_map<std::string, std::string> properties;
    
    VirtualDataItem() = default;
    
    VirtualDataItem(const std::string& text, const std::string& icon = "")
        : primaryText(text), iconIdentifier(icon) {}
};

// =============================================================================
// DATA PROVIDER INTERFACES
// =============================================================================

/**
 * @brief Base interface for all data providers
 * 
 * This allows components to request data on-demand rather than
 * storing everything in memory.
 */
class IVirtualDataProvider {
public:
    virtual ~IVirtualDataProvider() = default;
    
    /**
     * @brief Get total number of items in dataset
     */
    virtual size_t GetItemCount() const = 0;
    
    /**
     * @brief Get a single item by index (on-demand)
     * @param index Item index (0-based)
     * @return Virtual data item
     */
    virtual VirtualDataItem GetItem(size_t index) const = 0;
    
    /**
     * @brief Get multiple items at once (batch loading for optimization)
     * @param startIndex Starting index
     * @param count Number of items to retrieve
     * @return Vector of items
     */
    virtual std::vector<VirtualDataItem> GetItemBatch(size_t startIndex, size_t count) const {
        std::vector<VirtualDataItem> batch;
        batch.reserve(count);
        for (size_t i = 0; i < count && (startIndex + i) < GetItemCount(); ++i) {
            batch.push_back(GetItem(startIndex + i));
        }
        return batch;
    }
    
    /**
     * @brief Check if data source supports streaming/async loading
     */
    virtual bool SupportsStreaming() const { return false; }
    
    /**
     * @brief Invalidate cached data (call when underlying data changes)
     */
    virtual void InvalidateCache() {}
};

/**
 * @brief Simple vector-based data provider (stores all data in memory)
 * 
 * Use this for small to medium datasets (< 10,000 items)
 */
class VectorDataProvider : public IVirtualDataProvider {
private:
    std::vector<VirtualDataItem> items;
    
public:
    VectorDataProvider() = default;
    
    explicit VectorDataProvider(const std::vector<VirtualDataItem>& data)
        : items(data) {}
    
    explicit VectorDataProvider(std::vector<VirtualDataItem>&& data)
        : items(std::move(data)) {}
    
    size_t GetItemCount() const override {
        return items.size();
    }
    
    VirtualDataItem GetItem(size_t index) const override {
        if (index < items.size()) {
            return items[index];
        }
        return VirtualDataItem();
    }
    
    std::vector<VirtualDataItem> GetItemBatch(size_t startIndex, size_t count) const override {
        if (startIndex >= items.size()) {
            return {};
        }
        
        size_t endIndex = std::min(startIndex + count, items.size());
        return std::vector<VirtualDataItem>(
            items.begin() + startIndex,
            items.begin() + endIndex
        );
    }
    
    // Modification methods
    void SetItems(const std::vector<VirtualDataItem>& data) {
        items = data;
        InvalidateCache();
    }
    
    void AddItem(const VirtualDataItem& item) {
        items.push_back(item);
        InvalidateCache();
    }
    
    void InsertItem(size_t index, const VirtualDataItem& item) {
        if (index <= items.size()) {
            items.insert(items.begin() + index, item);
            InvalidateCache();
        }
    }
    
    void RemoveItem(size_t index) {
        if (index < items.size()) {
            items.erase(items.begin() + index);
            InvalidateCache();
        }
    }
    
    void Clear() {
        items.clear();
        InvalidateCache();
    }
};

/**
 * @brief Callback-based data provider (on-demand loading)
 * 
 * Use this for huge datasets (100,000+ items) where data is loaded from:
 * - Database
 * - File system
 * - Network
 * - Generated algorithmically
 */
class CallbackDataProvider : public IVirtualDataProvider {
private:
    size_t itemCount;
    std::function<VirtualDataItem(size_t index)> getItemCallback;
    std::function<std::vector<VirtualDataItem>(size_t start, size_t count)> getBatchCallback;
    
public:
    CallbackDataProvider(
        size_t totalItems,
        std::function<VirtualDataItem(size_t)> itemGetter,
        std::function<std::vector<VirtualDataItem>(size_t, size_t)> batchGetter = nullptr
    ) : itemCount(totalItems), 
        getItemCallback(itemGetter),
        getBatchCallback(batchGetter) {}
    
    size_t GetItemCount() const override {
        return itemCount;
    }
    
    VirtualDataItem GetItem(size_t index) const override {
        if (getItemCallback && index < itemCount) {
            return getItemCallback(index);
        }
        return VirtualDataItem();
    }
    
    std::vector<VirtualDataItem> GetItemBatch(size_t startIndex, size_t count) const override {
        if (getBatchCallback) {
            return getBatchCallback(startIndex, count);
        }
        // Fall back to individual fetches
        return IVirtualDataProvider::GetItemBatch(startIndex, count);
    }
    
    bool SupportsStreaming() const override {
        return getBatchCallback != nullptr;
    }
    
    void SetItemCount(size_t count) {
        itemCount = count;
        InvalidateCache();
    }
};

// =============================================================================
// PART 2: VIRTUAL RENDERING INTERFACE
// =============================================================================

/**
 * @brief Viewport information for virtual rendering
 */
struct VirtualViewport {
    int x = 0;                  // Viewport X position in content space
    int y = 0;                  // Viewport Y position in content space
    int width = 0;              // Viewport width
    int height = 0;             // Viewport height
    int scrollOffsetX = 0;      // Horizontal scroll offset
    int scrollOffsetY = 0;      // Vertical scroll offset
    
    bool Contains(int px, int py) const {
        return px >= x && px < (x + width) &&
               py >= y && py < (y + height);
    }
    
    Rect2Di ToRect() const {
        return Rect2Di(x, y, width, height);
    }
};

/**
 * @brief Information about visible range in virtual scrolling
 */
struct VirtualRange {
    size_t firstVisible = 0;     // First visible item index
    size_t lastVisible = 0;      // Last visible item index
    size_t totalItems = 0;       // Total number of items
    size_t bufferBefore = 0;     // Number of items to buffer before viewport
    size_t bufferAfter = 0;      // Number of items to buffer after viewport
    
    size_t GetVisibleCount() const {
        return (lastVisible >= firstVisible) ? (lastVisible - firstVisible + 1) : 0;
    }
    
    size_t GetBufferedStart() const {
        return (firstVisible >= bufferBefore) ? (firstVisible - bufferBefore) : 0;
    }
    
    size_t GetBufferedEnd() const {
        return std::min(lastVisible + bufferAfter, totalItems - 1);
    }
    
    bool IsVisible(size_t index) const {
        return index >= firstVisible && index <= lastVisible;
    }
    
    bool IsInBuffer(size_t index) const {
        return index >= GetBufferedStart() && index <= GetBufferedEnd();
    }
};

/**
 * @brief Base interface for components that support virtual rendering
 * 
 * Components implementing this interface can efficiently render
 * huge datasets by only rendering visible items.
 */
class IVirtualRenderer {
public:
    virtual ~IVirtualRenderer() = default;
    
    /**
     * @brief Get current viewport information
     */
    virtual VirtualViewport GetViewport() const = 0;
    
    /**
     * @brief Calculate which items are visible in current viewport
     * @return Range of visible items
     */
    virtual VirtualRange CalculateVisibleRange() const = 0;
    
    /**
     * @brief Calculate the position (Y coordinate) of an item
     * @param index Item index
     * @return Y position in content space
     */
    virtual int CalculateItemPosition(size_t index) const = 0;
    
    /**
     * @brief Calculate the height of an item
     * @param index Item index (some items may have variable height)
     * @return Item height in pixels
     */
    virtual int CalculateItemHeight(size_t index) const = 0;
    
    /**
     * @brief Calculate total content height for all items
     * @return Total height in pixels
     */
    virtual int CalculateTotalContentHeight() const = 0;
    
    /**
     * @brief Get buffer size (number of items to render beyond viewport)
     */
    virtual size_t GetBufferSize() const = 0;
    
    /**
     * @brief Set buffer size
     * @param bufferItems Number of items to render before/after viewport
     */
    virtual void SetBufferSize(size_t bufferItems) = 0;
    
    /**
     * @brief Invalidate render cache (forces re-render on next frame)
     */
    virtual void InvalidateRenderCache() = 0;
};

/**
 * @brief Configuration for virtual rendering
 */
struct VirtualRenderConfig {
    size_t bufferItemsBefore = 10;      // Items to render before viewport
    size_t bufferItemsAfter = 10;       // Items to render after viewport
    bool enableCaching = true;          // Enable template/render caching
    size_t maxCacheSize = 200;          // Maximum cached items
    bool reuseTemplates = true;         // Reuse template instances
    int fixedItemHeight = 0;            // Fixed item height (0 = variable)
    
    static VirtualRenderConfig Default() {
        return VirtualRenderConfig();
    }
    
    static VirtualRenderConfig HighPerformance() {
        VirtualRenderConfig config;
        config.bufferItemsBefore = 20;
        config.bufferItemsAfter = 20;
        config.maxCacheSize = 500;
        return config;
    }
    
    static VirtualRenderConfig LowMemory() {
        VirtualRenderConfig config;
        config.bufferItemsBefore = 5;
        config.bufferItemsAfter = 5;
        config.maxCacheSize = 50;
        return config;
    }
};

// =============================================================================
// PART 3: TEMPLATE-BASED RENDERING INTERFACE
// =============================================================================

/**
 * @brief Interface for components that use templates for rendering items
 */
class ITemplateRenderer {
public:
    virtual ~ITemplateRenderer() = default;
    
    /**
     * @brief Set the template used for rendering items
     * @param templateInstance Template to use
     */
    virtual void SetItemTemplate(std::shared_ptr<UltraCanvasTemplate> templateInstance) = 0;
    
    /**
     * @brief Get current item template
     */
    virtual std::shared_ptr<UltraCanvasTemplate> GetItemTemplate() const = 0;
    
    /**
     * @brief Bind data to template for a specific item
     * @param templateInstance Template to bind data to
     * @param item Data item to bind
     * @param index Item index
     */
    virtual void BindDataToTemplate(
        std::shared_ptr<UltraCanvasTemplate> templateInstance,
        const VirtualDataItem& item,
        size_t index
    ) = 0;
    
    /**
     * @brief Clear template cache
     */
    virtual void ClearTemplateCache() = 0;
};

// =============================================================================
// PART 4: COMBINED INTERFACE FOR FULL VIRTUAL COMPONENTS
// =============================================================================

/**
 * @brief Complete interface combining data access, virtual rendering, and templates
 * 
 * Components implementing this interface get:
 * - On-demand data loading
 * - Virtual scrolling rendering
 * - Template-based item display
 * 
 * Perfect for: ListView, TableView, TreeView, GridView, etc.
 */
class IVirtualComponent : public IVirtualRenderer, public ITemplateRenderer {
public:
    virtual ~IVirtualComponent() = default;
    
    /**
     * @brief Set data provider
     */
    virtual void SetDataProvider(std::shared_ptr<IVirtualDataProvider> provider) = 0;
    
    /**
     * @brief Get current data provider
     */
    virtual std::shared_ptr<IVirtualDataProvider> GetDataProvider() const = 0;
    
    /**
     * @brief Get virtual render configuration
     */
    virtual VirtualRenderConfig GetRenderConfig() const = 0;
    
    /**
     * @brief Set virtual render configuration
     */
    virtual void SetRenderConfig(const VirtualRenderConfig& config) = 0;
    
    /**
     * @brief Refresh data (reload from provider)
     */
    virtual void RefreshData() = 0;
    
    /**
     * @brief Scroll to make item visible
     * @param index Item index
     * @param align Alignment (true = top, false = center)
     */
    virtual void ScrollToItem(size_t index, bool alignTop = true) = 0;
    
    /**
     * @brief Get item at screen position
     * @param x X coordinate
     * @param y Y coordinate
     * @return Item index or -1 if none
     */
    virtual int GetItemAtPosition(int x, int y) const = 0;
};

// =============================================================================
// PART 5: FACTORY HELPERS
// =============================================================================

/**
 * @brief Create a vector-based data provider from a vector
 */
inline std::shared_ptr<IVirtualDataProvider> CreateVectorDataProvider(
    const std::vector<VirtualDataItem>& items
) {
    return std::make_shared<VectorDataProvider>(items);
}

/**
 * @brief Create a callback-based data provider
 */
inline std::shared_ptr<IVirtualDataProvider> CreateCallbackDataProvider(
    size_t itemCount,
    std::function<VirtualDataItem(size_t)> itemGetter,
    std::function<std::vector<VirtualDataItem>(size_t, size_t)> batchGetter = nullptr
) {
    return std::make_shared<CallbackDataProvider>(itemCount, itemGetter, batchGetter);
}

// =============================================================================
// PART 6: CONVERSION HELPERS (for backward compatibility)
// =============================================================================

/**
 * @brief Convert ListItemData to VirtualDataItem (when ListView is migrated)
 */
// This will be in the actual component implementations

/**
 * @brief Convert TableCell to VirtualDataItem (when TableView is migrated)
 */
// This will be in the actual component implementations

} // namespace UltraCanvas
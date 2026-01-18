// include/UltraCanvasNewDocumentDialog.h
// New Document dialog with filterable file type selection
// Version: 1.0.1
// Last Modified: 2025-12-21
// Author: UltraCanvas Framework
//
// IMPORTANT: After creating shared_ptr, call Initialize() to set up callbacks!

#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasScrollbar.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// ===== DOCUMENT TYPE DEFINITION =====

/**
 * @brief Represents a document type that can be created
 */
    struct DocumentTypeInfo {
        std::string name;           // Display name (e.g., "C++ Source File")
        std::string extension;      // Primary extension (e.g., "cpp")
        std::vector<std::string> alternateExtensions;  // Other extensions
        std::string category;       // Category for grouping (e.g., "C/C++")
        std::string description;    // Optional description
        std::string iconPath;       // Optional icon path
        std::string templateContent; // Optional template content for new files

        DocumentTypeInfo() = default;

        DocumentTypeInfo(const std::string& typeName,
                         const std::string& ext,
                         const std::string& cat = "General")
                : name(typeName), extension(ext), category(cat) {}

        DocumentTypeInfo(const std::string& typeName,
                         const std::string& ext,
                         const std::string& cat,
                         const std::string& desc)
                : name(typeName), extension(ext), category(cat), description(desc) {}

        std::string GetDisplayName() const {
            return name + " (." + extension + ")";
        }

        bool MatchesFilter(const std::string& filter) const {
            if (filter.empty()) return true;

            std::string lowerFilter = filter;
            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(lowerFilter) != std::string::npos) return true;

            std::string lowerExt = extension;
            std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
            if (lowerExt.find(lowerFilter) != std::string::npos) return true;

            std::string lowerCat = category;
            std::transform(lowerCat.begin(), lowerCat.end(), lowerCat.begin(), ::tolower);
            if (lowerCat.find(lowerFilter) != std::string::npos) return true;

            for (const auto& altExt : alternateExtensions) {
                std::string lowerAlt = altExt;
                std::transform(lowerAlt.begin(), lowerAlt.end(), lowerAlt.begin(), ::tolower);
                if (lowerAlt.find(lowerFilter) != std::string::npos) return true;
            }

            return false;
        }
    };

// ===== NEW DOCUMENT DIALOG RESULT =====

    struct NewDocumentResult {
        bool accepted = false;
        std::string fileName;
        std::string extension;
        std::string fullPath;
        const DocumentTypeInfo* selectedType = nullptr;
    };

// ===== NEW DOCUMENT DIALOG STYLING =====

    struct NewDocumentDialogStyle {
        Color backgroundColor = Colors::White;
        Color borderColor = Color(180, 180, 180);
        int borderWidth = 1;
        int padding = 16;

        Color titleColor = Colors::Black;
        int titleFontSize = 16;

        Color listBackgroundColor = Color(250, 250, 250);
        Color listBorderColor = Color(200, 200, 200);
        Color itemHoverColor = Color(230, 240, 255);
        Color itemSelectedColor = Color(200, 220, 255);
        Color itemTextColor = Colors::Black;
        Color categoryTextColor = Color(100, 100, 100);
        int itemHeight = 28;
        int itemFontSize = 13;

        Color inputBackgroundColor = Colors::White;
        Color inputBorderColor = Color(180, 180, 180);
        Color inputFocusBorderColor = Color(100, 150, 255);

        Color buttonColor = Color(240, 240, 240);
        Color buttonHoverColor = Color(230, 230, 230);
        Color primaryButtonColor = Color(66, 133, 244);
        Color primaryButtonTextColor = Colors::White;

        static NewDocumentDialogStyle Default() { return NewDocumentDialogStyle(); }
    };

// ===== NEW DOCUMENT DIALOG =====

/**
 * @brief Dialog for creating new documents with file type selection
 *
 * @example
 * auto dialog = CreateNewDocumentDialog("NewDocDlg", 1, 100, 100);
 * dialog->onDocumentCreated = [](const NewDocumentResult& result) { ... };
 * dialog->Show();
 */
    class UltraCanvasNewDocumentDialog : public UltraCanvasContainer {
    public:
        UltraCanvasNewDocumentDialog(const std::string& identifier, long id,
                                     int x, int y, int width = 500, int height = 450);

        virtual ~UltraCanvasNewDocumentDialog() = default;

        // ===== DOCUMENT TYPES =====
        void AddDocumentType(const DocumentTypeInfo& docType);
        void AddDocumentTypes(const std::vector<DocumentTypeInfo>& docTypes);
        void ClearDocumentTypes();
        void LoadDefaultDocumentTypes();
        const std::vector<DocumentTypeInfo>& GetDocumentTypes() const { return documentTypes; }

        // ===== SELECTION =====
        const DocumentTypeInfo* GetSelectedType() const;
        void SetSelectedIndex(int index);
        int GetSelectedIndex() const { return selectedIndex; }

        // ===== FILE NAME =====
        std::string GetFileName() const;
        void SetDefaultFileName(const std::string& name);
        std::string GetFullFileName() const;

        // ===== DIALOG CONTROL =====
        void Show();
        void Hide();
        bool IsDialogVisible() const { return isDialogVisible; }
        NewDocumentResult GetResult() const;

        // ===== STYLING =====
        void SetStyle(const NewDocumentDialogStyle& newStyle) { style = newStyle; }
        const NewDocumentDialogStyle& GetStyle() const { return style; }

        // ===== CALLBACKS =====
        std::function<void(const NewDocumentResult&)> onDocumentCreated;
        std::function<void()> onCancelled;
        std::function<void(const DocumentTypeInfo*)> onSelectionChanged;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

    protected:
        void SetupComponents();
        void SetupLayout();

        void UpdateFilteredList();
        void ApplyFilter(const std::string& filterText);

        void SelectItem(int index);
        void SelectNextItem();
        void SelectPreviousItem();
        void EnsureItemVisible(int index);

        void OnCreateClicked();
        void OnCancelClicked();
        void UpdateFileNameExtension();

        void RenderTitle(IRenderContext* ctx);
        void RenderFilterInput(IRenderContext* ctx);
        void RenderDocumentList(IRenderContext* ctx);
        void RenderFileNameInput(IRenderContext* ctx);
        void RenderButtons(IRenderContext* ctx);
        void RenderListItem(IRenderContext* ctx, const DocumentTypeInfo& docType,
                            int index, int y, bool isSelected, bool isHovered);

        bool HandleKeyDown(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);

        Rect2Di GetTitleBounds() const;
        Rect2Di GetFilterInputBounds() const;
        Rect2Di GetDocumentListBounds() const;
        Rect2Di GetFileNameInputBounds() const;
        Rect2Di GetButtonsBounds() const;
        Rect2Di GetCreateButtonBounds() const;
        Rect2Di GetCancelButtonBounds() const;

    private:
        NewDocumentDialogStyle style;

        std::vector<DocumentTypeInfo> documentTypes;
        std::vector<int> filteredIndices;

        bool isDialogVisible = false;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        int scrollOffset = 0;
        int maxVisibleItems = 10;
        std::string filterText;
        std::string fileName = "untitled";
        bool accepted = false;

        std::shared_ptr<UltraCanvasTextInput> filterInput;
        std::shared_ptr<UltraCanvasTextInput> fileNameInput;
        std::shared_ptr<UltraCanvasButton> createButton;
        std::shared_ptr<UltraCanvasButton> cancelButton;
        std::shared_ptr<UltraCanvasScrollbar> listScrollbar;

        enum class FocusArea { Filter, List, FileName, Buttons };
        FocusArea currentFocus = FocusArea::Filter;

        bool createButtonHovered = false;
        bool cancelButtonHovered = false;
    };

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create a new document dialog (calls Initialize() automatically)
 */
    std::shared_ptr<UltraCanvasNewDocumentDialog> CreateNewDocumentDialog(
            const std::string& identifier,
            long id,
            int x = 100, int y = 100,
            int width = 500, int height = 450
    );

/**
 * @brief Create dialog pre-loaded with programming language types
 */
    std::shared_ptr<UltraCanvasNewDocumentDialog> CreateProgrammingNewDocumentDialog(
            const std::string& identifier,
            long id,
            int x = 100, int y = 100
    );

/**
 * @brief Get default programming document types
 */
    std::vector<DocumentTypeInfo> GetDefaultProgrammingDocumentTypes();

} // namespace UltraCanvas
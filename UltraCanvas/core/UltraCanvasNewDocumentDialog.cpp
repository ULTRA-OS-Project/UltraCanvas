// core/UltraCanvasNewDocumentDialog.cpp
// New Document dialog implementation - FIXED version with deferred initialization
// Version: 1.0.1
// Last Modified: 2025-12-21
// Author: UltraCanvas Framework

#include "UltraCanvasNewDocumentDialog.h"
#include <algorithm>
#include <cctype>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====

    UltraCanvasNewDocumentDialog::UltraCanvasNewDocumentDialog(
            const std::string& identifier, long id,
            int x, int y, int width, int height)
            : UltraCanvasContainer(identifier, id, x, y, width, height)
    {
        SetBackgroundColor(style.backgroundColor);
        SetupComponents();
        SetupLayout();
        LoadDefaultDocumentTypes();
    }

// ===== SETUP =====

    void UltraCanvasNewDocumentDialog::SetupComponents() {
        // Filter input
        filterInput = std::make_shared<UltraCanvasTextInput>(
                "FilterInput", 1, 0, 0, 100, 28);
        filterInput->SetPlaceholder("Filter document types...");
        filterInput->onTextChanged = [this](const std::string& text) {
            ApplyFilter(text);
        };

        // File name input
        fileNameInput = std::make_shared<UltraCanvasTextInput>(
                "FileNameInput", 2, 0, 0, 100, 28);
        fileNameInput->SetText("untitled");
        fileNameInput->onTextChanged = [this](const std::string& text) {
            fileName = text;
        };

        // Create button
        createButton = std::make_shared<UltraCanvasButton>(
                "CreateButton", 3, 0, 0, 90, 32);
        createButton->SetText("Create");
        createButton->onClick = [this]() {
            OnCreateClicked();
        };

        // Cancel button
        cancelButton = std::make_shared<UltraCanvasButton>(
                "CancelButton", 4, 0, 0, 90, 32);
        cancelButton->SetText("Cancel");
        cancelButton->onClick = [this]() {
            OnCancelClicked();
        };

        // Scrollbar for list
        listScrollbar = std::make_shared<UltraCanvasScrollbar>(
                "ListScrollbar", 5, 0, 0, 14, 100);
        listScrollbar->SetOrientation(ScrollbarOrientation::Vertical);

        // Add children
        AddChild(filterInput);
        AddChild(fileNameInput);
        AddChild(createButton);
        AddChild(cancelButton);
        AddChild(listScrollbar);
    }

    void UltraCanvasNewDocumentDialog::SetupLayout() {
        int padding = style.padding;
        int width = GetWidth();
        int height = GetHeight();

        Rect2Di filterBounds = GetFilterInputBounds();
        filterInput->SetBounds(filterBounds);

        Rect2Di fileNameBounds = GetFileNameInputBounds();
        fileNameInput->SetBounds(fileNameBounds);

        Rect2Di createBounds = GetCreateButtonBounds();
        Rect2Di cancelBounds = GetCancelButtonBounds();
        createButton->SetBounds(createBounds);
        cancelButton->SetBounds(cancelBounds);

        Rect2Di listBounds = GetDocumentListBounds();
        listScrollbar->SetBounds(Rect2Di(
                listBounds.x + listBounds.width - 14,
                listBounds.y,
                14,
                listBounds.height
        ));

        maxVisibleItems = listBounds.height / style.itemHeight;
    }

// ===== DOCUMENT TYPES =====

    void UltraCanvasNewDocumentDialog::AddDocumentType(const DocumentTypeInfo& docType) {
        documentTypes.push_back(docType);
        UpdateFilteredList();
    }

    void UltraCanvasNewDocumentDialog::AddDocumentTypes(const std::vector<DocumentTypeInfo>& docTypes) {
        for (const auto& dt : docTypes) {
            documentTypes.push_back(dt);
        }
        UpdateFilteredList();
    }

    void UltraCanvasNewDocumentDialog::ClearDocumentTypes() {
        documentTypes.clear();
        filteredIndices.clear();
        selectedIndex = -1;
        hoveredIndex = -1;
        scrollOffset = 0;
    }

    void UltraCanvasNewDocumentDialog::LoadDefaultDocumentTypes() {
        documentTypes = GetDefaultProgrammingDocumentTypes();
        UpdateFilteredList();

        if (!filteredIndices.empty()) {
            selectedIndex = 0;
            UpdateFileNameExtension();
        }
    }

// ===== SELECTION =====

    const DocumentTypeInfo* UltraCanvasNewDocumentDialog::GetSelectedType() const {
        if (selectedIndex >= 0 && selectedIndex < (int)filteredIndices.size()) {
            int actualIndex = filteredIndices[selectedIndex];
            if (actualIndex >= 0 && actualIndex < (int)documentTypes.size()) {
                return &documentTypes[actualIndex];
            }
        }
        return nullptr;
    }

    void UltraCanvasNewDocumentDialog::SetSelectedIndex(int index) {
        if (index >= 0 && index < (int)filteredIndices.size()) {
            selectedIndex = index;
            UpdateFileNameExtension();

            if (onSelectionChanged) {
                onSelectionChanged(GetSelectedType());
            }

            RequestRedraw();
        }
    }

// ===== FILE NAME =====

    std::string UltraCanvasNewDocumentDialog::GetFileName() const {
        return fileName;
    }

    void UltraCanvasNewDocumentDialog::SetDefaultFileName(const std::string& name) {
        fileName = name;
        if (fileNameInput) {
            fileNameInput->SetText(name);
        }
        UpdateFileNameExtension();
    }

    std::string UltraCanvasNewDocumentDialog::GetFullFileName() const {
        const DocumentTypeInfo* selected = GetSelectedType();
        if (selected && !fileName.empty()) {
            std::string ext = "." + selected->extension;
            if (fileName.length() > ext.length()) {
                std::string ending = fileName.substr(fileName.length() - ext.length());
                std::transform(ending.begin(), ending.end(), ending.begin(), ::tolower);
                std::string lowerExt = ext;
                std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                if (ending == lowerExt) {
                    return fileName;
                }
            }
            return fileName + ext;
        }
        return fileName;
    }

    void UltraCanvasNewDocumentDialog::UpdateFileNameExtension() {
        // Extension preview is shown in rendering
    }

// ===== DIALOG CONTROL =====

    void UltraCanvasNewDocumentDialog::Show() {
        isDialogVisible = true;
        accepted = false;
        SetVisible(true);

        filterText.clear();
        if (filterInput) {
            filterInput->SetText("");
        }
        UpdateFilteredList();

        currentFocus = FocusArea::Filter;
        if (filterInput) {
            filterInput->SetFocus(true);
        }

        RequestRedraw();
    }

    void UltraCanvasNewDocumentDialog::Hide() {
        isDialogVisible = false;
        SetVisible(false);
    }

    NewDocumentResult UltraCanvasNewDocumentDialog::GetResult() const {
        NewDocumentResult result;
        result.accepted = accepted;
        result.fileName = GetFullFileName();
        result.selectedType = GetSelectedType();
        if (result.selectedType) {
            result.extension = result.selectedType->extension;
        }
        return result;
    }

// ===== FILTERING =====

    void UltraCanvasNewDocumentDialog::UpdateFilteredList() {
        filteredIndices.clear();

        for (int i = 0; i < (int)documentTypes.size(); ++i) {
            if (documentTypes[i].MatchesFilter(filterText)) {
                filteredIndices.push_back(i);
            }
        }

        if (selectedIndex >= (int)filteredIndices.size()) {
            selectedIndex = filteredIndices.empty() ? -1 : 0;
        }

        scrollOffset = 0;

        if (listScrollbar) {
            int totalItems = (int)filteredIndices.size();
            int visibleItems = maxVisibleItems;
            if (totalItems > visibleItems) {
                listScrollbar->SetVisible(true);
                listScrollbar->SetContentSize(totalItems * style.itemHeight);
                listScrollbar->SetViewportSize(visibleItems * style.itemHeight);
            } else {
                listScrollbar->SetVisible(false);
            }
        }

        RequestRedraw();
    }

    void UltraCanvasNewDocumentDialog::ApplyFilter(const std::string& text) {
        filterText = text;
        UpdateFilteredList();
    }

// ===== SELECTION NAVIGATION =====

    void UltraCanvasNewDocumentDialog::SelectItem(int index) {
        if (index >= 0 && index < (int)filteredIndices.size()) {
            selectedIndex = index;
            EnsureItemVisible(index);
            UpdateFileNameExtension();

            if (onSelectionChanged) {
                onSelectionChanged(GetSelectedType());
            }

            RequestRedraw();
        }
    }

    void UltraCanvasNewDocumentDialog::SelectNextItem() {
        if (selectedIndex < (int)filteredIndices.size() - 1) {
            SelectItem(selectedIndex + 1);
        }
    }

    void UltraCanvasNewDocumentDialog::SelectPreviousItem() {
        if (selectedIndex > 0) {
            SelectItem(selectedIndex - 1);
        }
    }

    void UltraCanvasNewDocumentDialog::EnsureItemVisible(int index) {
        if (index < scrollOffset) {
            scrollOffset = index;
        } else if (index >= scrollOffset + maxVisibleItems) {
            scrollOffset = index - maxVisibleItems + 1;
        }

        if (listScrollbar) {
            listScrollbar->SetScrollPosition(scrollOffset * style.itemHeight);
        }
    }

// ===== ACTIONS =====

    void UltraCanvasNewDocumentDialog::OnCreateClicked() {
        if (GetSelectedType() != nullptr) {
            accepted = true;
            Hide();

            if (onDocumentCreated) {
                onDocumentCreated(GetResult());
            }
        }
    }

    void UltraCanvasNewDocumentDialog::OnCancelClicked() {
        accepted = false;
        Hide();

        if (onCancelled) {
            onCancelled();
        }
    }

// ===== LAYOUT HELPERS =====

    Rect2Di UltraCanvasNewDocumentDialog::GetTitleBounds() const {
        return Rect2Di(style.padding, style.padding,
                       GetWidth() - style.padding * 2, 30);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetFilterInputBounds() const {
        int y = style.padding + 30 + 20;
        return Rect2Di(style.padding, y,
                       GetWidth() - style.padding * 2, 28);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetDocumentListBounds() const {
        Rect2Di filterBounds = GetFilterInputBounds();
        int y = filterBounds.y + filterBounds.height + 12;
        int bottomY = GetHeight() - style.padding - 32 - 12 - 28 - 24;
        return Rect2Di(style.padding, y,
                       GetWidth() - style.padding * 2, bottomY - y);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetFileNameInputBounds() const {
        int y = GetHeight() - style.padding - 32 - 12 - 28 - 8;
        return Rect2Di(style.padding + 80, y,
                       GetWidth() - style.padding * 2 - 80, 28);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetButtonsBounds() const {
        return Rect2Di(style.padding,
                       GetHeight() - style.padding - 32,
                       GetWidth() - style.padding * 2, 32);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetCreateButtonBounds() const {
        Rect2Di buttons = GetButtonsBounds();
        return Rect2Di(buttons.x + buttons.width - 90 - 8 - 90,
                       buttons.y, 90, 32);
    }

    Rect2Di UltraCanvasNewDocumentDialog::GetCancelButtonBounds() const {
        Rect2Di buttons = GetButtonsBounds();
        return Rect2Di(buttons.x + buttons.width - 90,
                       buttons.y, 90, 32);
    }

// ===== RENDERING =====

    void UltraCanvasNewDocumentDialog::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        ctx->PushState();

        Rect2Di bounds = GetBounds();

        // Shadow
        ctx->SetFillPaint(Color(0, 0, 0, 40));
        ctx->FillRectangle(bounds.x + 4, bounds.y + 4, bounds.width, bounds.height);

        // Background
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(bounds);

        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(style.borderWidth);
        ctx->DrawRectangle(bounds);

        RenderTitle(ctx);
        RenderFilterInput(ctx);
        RenderDocumentList(ctx);
        RenderFileNameInput(ctx);
        RenderButtons(ctx);

        UltraCanvasContainer::Render(ctx);
        ctx->PushState();
    }

    void UltraCanvasNewDocumentDialog::RenderTitle(IRenderContext* ctx) {
        Rect2Di titleBounds = GetTitleBounds();

        ctx->SetFontFace("sans-serif", FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(style.titleFontSize);
        ctx->SetTextPaint(style.titleColor);
        ctx->DrawText("Create New Document", Point2Di(titleBounds.x, titleBounds.y + 20));

        int closeX = titleBounds.x + titleBounds.width - 20;
        int closeY = titleBounds.y + 5;

        ctx->SetFontSize(14);
        ctx->SetTextPaint(Color(150, 150, 150));
        ctx->DrawText("✕", Point2Di(closeX, closeY + 14));
    }

    void UltraCanvasNewDocumentDialog::RenderFilterInput(IRenderContext* ctx) {
        Rect2Di filterBounds = GetFilterInputBounds();

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(12);
        ctx->SetTextPaint(style.itemTextColor);
        ctx->DrawText("Document type:", Point2Di(filterBounds.x, filterBounds.y - 16));
    }

    void UltraCanvasNewDocumentDialog::RenderDocumentList(IRenderContext* ctx) {
        Rect2Di listBounds = GetDocumentListBounds();

        ctx->SetFillPaint(style.listBackgroundColor);
        ctx->FillRectangle(listBounds);

        ctx->SetStrokePaint(style.listBorderColor);
        ctx->DrawRectangle(listBounds);

        ctx->PushState();
        ctx->ClipRect(listBounds.x, listBounds.y, listBounds.width - 14, listBounds.height);

        int y = listBounds.y;
        int startIndex = scrollOffset;
        int endIndex = std::min(startIndex + maxVisibleItems + 1, (int)filteredIndices.size());

        for (int i = startIndex; i < endIndex; ++i) {
            int actualIndex = filteredIndices[i];
            const DocumentTypeInfo& docType = documentTypes[actualIndex];

            bool isSelected = (i == selectedIndex);
            bool isHovered = (i == hoveredIndex);

            RenderListItem(ctx, docType, i, y, isSelected, isHovered);
            y += style.itemHeight;
        }

        ctx->PopState();

        ctx->SetFontSize(10);
        ctx->SetTextPaint(Color(120, 120, 120));
        std::string countText = std::to_string(filteredIndices.size()) + " types";
        if (!filterText.empty()) {
            countText += " (filtered)";
        }
        ctx->DrawText(countText, Point2Di(listBounds.x + 4, listBounds.y + listBounds.height + 12));
    }

    void UltraCanvasNewDocumentDialog::RenderListItem(
            IRenderContext* ctx,
            const DocumentTypeInfo& docType,
            int index, int y, bool isSelected, bool isHovered)
    {
        Rect2Di listBounds = GetDocumentListBounds();
        Rect2Di itemBounds(listBounds.x + 2, y, listBounds.width - 18, style.itemHeight);

        if (isSelected) {
            ctx->SetFillPaint(style.itemSelectedColor);
            ctx->FillRectangle(itemBounds);
        } else if (isHovered) {
            ctx->SetFillPaint(style.itemHoverColor);
            ctx->FillRectangle(itemBounds);
        }

        int iconX = itemBounds.x + 8;
        int iconY = itemBounds.y + (style.itemHeight - 16) / 2;
        ctx->SetFontSize(14);
        ctx->SetTextPaint(Color(100, 130, 180));
        ctx->DrawText("📄", Point2Di(iconX, iconY + 14));

        int textX = iconX + 24;
        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.itemFontSize);
        ctx->SetTextPaint(style.itemTextColor);
        ctx->DrawText(docType.GetDisplayName(), Point2Di(textX, itemBounds.y + 18));

        if (!docType.category.empty()) {
            ctx->SetFontSize(10);
            ctx->SetTextPaint(style.categoryTextColor);
            int catWidth = 0, catHeight = 0;
            ctx->GetTextLineDimensions(docType.category, catWidth, catHeight);
            ctx->DrawText(docType.category,
                          Point2Di(itemBounds.x + itemBounds.width - catWidth - 8, itemBounds.y + 17));
        }

        if (isSelected) {
            ctx->SetTextPaint(Color(66, 133, 244));
            ctx->DrawText("✓", Point2Di(itemBounds.x + itemBounds.width - 24, itemBounds.y + 18));
        }
    }

    void UltraCanvasNewDocumentDialog::RenderFileNameInput(IRenderContext* ctx) {
        Rect2Di bounds = GetFileNameInputBounds();

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(12);
        ctx->SetTextPaint(style.itemTextColor);
        ctx->DrawText("File name:", Point2Di(style.padding, bounds.y + 8));

        const DocumentTypeInfo* selected = GetSelectedType();
        if (selected) {
            ctx->SetFontSize(10);
            ctx->SetTextPaint(Color(100, 100, 100));
            std::string preview = "Will create: " + GetFullFileName();
            ctx->DrawText(preview, Point2Di(bounds.x, bounds.y + bounds.height + 4));
        }
    }

    void UltraCanvasNewDocumentDialog::RenderButtons(IRenderContext* ctx) {
        // Buttons rendered as children
    }

// ===== EVENT HANDLING =====

    bool UltraCanvasNewDocumentDialog::OnEvent(const UCEvent& event) {
        if (!IsVisible()) return false;

        switch (event.type) {
            case UCEventType::KeyDown:
                if (HandleKeyDown(event)) return true;
                break;

            case UCEventType::MouseDown:
                if (HandleMouseDown(event)) return true;
                break;

            case UCEventType::MouseMove:
                if (HandleMouseMove(event)) return true;
                break;

            case UCEventType::MouseWheel:
                if (HandleMouseWheel(event)) return true;
                break;

            default:
                break;
        }

        return UltraCanvasContainer::OnEvent(event);
    }

    bool UltraCanvasNewDocumentDialog::HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Escape:
                OnCancelClicked();
                return true;

            case UCKeys::Return:
                if (currentFocus == FocusArea::List ||
                    currentFocus == FocusArea::FileName ||
                    currentFocus == FocusArea::Buttons) {
                    OnCreateClicked();
                    return true;
                }
                break;

            case UCKeys::Up:
                if (currentFocus == FocusArea::Filter || currentFocus == FocusArea::List) {
                    SelectPreviousItem();
                    return true;
                }
                break;

            case UCKeys::Down:
                if (currentFocus == FocusArea::Filter || currentFocus == FocusArea::List) {
                    SelectNextItem();
                    return true;
                }
                break;

            case UCKeys::Tab:
                if (event.shift) {
                    switch (currentFocus) {
                        case FocusArea::Filter: currentFocus = FocusArea::Buttons; break;
                        case FocusArea::List: currentFocus = FocusArea::Filter; break;
                        case FocusArea::FileName: currentFocus = FocusArea::List; break;
                        case FocusArea::Buttons: currentFocus = FocusArea::FileName; break;
                    }
                } else {
                    switch (currentFocus) {
                        case FocusArea::Filter: currentFocus = FocusArea::List; break;
                        case FocusArea::List: currentFocus = FocusArea::FileName; break;
                        case FocusArea::FileName: currentFocus = FocusArea::Buttons; break;
                        case FocusArea::Buttons: currentFocus = FocusArea::Filter; break;
                    }
                }
                RequestRedraw();
                return true;

            default:
                break;
        }

        return false;
    }

    bool UltraCanvasNewDocumentDialog::HandleMouseDown(const UCEvent& event) {
        Rect2Di listBounds = GetDocumentListBounds();

        Rect2Di titleBounds = GetTitleBounds();
        int closeX = titleBounds.x + titleBounds.width - 24;
        int closeY = titleBounds.y;
        if (event.x >= closeX && event.x <= closeX + 20 &&
            event.y >= closeY && event.y <= closeY + 24) {
            OnCancelClicked();
            return true;
        }

        if (listBounds.Contains(event.x, event.y)) {
            currentFocus = FocusArea::List;

            int clickedIndex = scrollOffset + (event.y - listBounds.y) / style.itemHeight;
            if (clickedIndex >= 0 && clickedIndex < (int)filteredIndices.size()) {
                SelectItem(clickedIndex);
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasNewDocumentDialog::HandleMouseMove(const UCEvent& event) {
        Rect2Di listBounds = GetDocumentListBounds();

        int oldHovered = hoveredIndex;
        hoveredIndex = -1;

        if (listBounds.Contains(event.x, event.y)) {
            int hoverIdx = scrollOffset + (event.y - listBounds.y) / style.itemHeight;
            if (hoverIdx >= 0 && hoverIdx < (int)filteredIndices.size()) {
                hoveredIndex = hoverIdx;
            }
        }

        Rect2Di createBounds = GetCreateButtonBounds();
        Rect2Di cancelBounds = GetCancelButtonBounds();

        createButtonHovered = createBounds.Contains(event.x, event.y);
        cancelButtonHovered = cancelBounds.Contains(event.x, event.y);

        if (hoveredIndex != oldHovered) {
            RequestRedraw();
        }

        return false;
    }

    bool UltraCanvasNewDocumentDialog::HandleMouseWheel(const UCEvent& event) {
        Rect2Di listBounds = GetDocumentListBounds();

        if (listBounds.Contains(event.x, event.y)) {
            int delta = event.wheelDelta > 0 ? -1 : 1;
            int newOffset = scrollOffset + delta;

            int maxOffset = std::max(0, (int)filteredIndices.size() - maxVisibleItems);
            newOffset = std::max(0, std::min(newOffset, maxOffset));

            if (newOffset != scrollOffset) {
                scrollOffset = newOffset;
                if (listScrollbar) {
                    listScrollbar->SetScrollPosition(scrollOffset * style.itemHeight);
                }
                RequestRedraw();
            }
            return true;
        }

        return false;
    }

// ===== FACTORY FUNCTIONS =====

    std::shared_ptr<UltraCanvasNewDocumentDialog> CreateNewDocumentDialog(
            const std::string& identifier,
            long id,
            int x, int y,
            int width, int height)
    {
        return std::make_shared<UltraCanvasNewDocumentDialog>(identifier, id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasNewDocumentDialog> CreateProgrammingNewDocumentDialog(
            const std::string& identifier,
            long id,
            int x, int y)
    {
        auto dialog = std::make_shared<UltraCanvasNewDocumentDialog>(identifier, id, x, y, 520, 500);
        dialog->LoadDefaultDocumentTypes();
        return dialog;
    }

// ===== DEFAULT DOCUMENT TYPES =====

    std::vector<DocumentTypeInfo> GetDefaultProgrammingDocumentTypes() {
        std::vector<DocumentTypeInfo> types;

        // C/C++
        types.push_back({"C Source File", "c", "C/C++", "C language source code"});
        types.push_back({"C Header File", "h", "C/C++", "C/C++ header file"});
        types.push_back({"C++ Source File", "cpp", "C/C++", "C++ source code"});
        types.push_back({"C++ Header File", "hpp", "C/C++", "C++ header file"});

        // Java
        types.push_back({"Java Source File", "java", "Java", "Java source code"});

        // C#
        types.push_back({"C# Source File", "cs", "C#", "C# source code"});

        // Python
        types.push_back({"Python Source File", "py", "Python", "Python script"});
        types.push_back({"Python Stub File", "pyi", "Python", "Python type stub"});

        // JavaScript/TypeScript
        types.push_back({"JavaScript File", "js", "Web", "JavaScript source"});
        types.push_back({"TypeScript File", "ts", "Web", "TypeScript source"});
        types.push_back({"JSX File", "jsx", "Web", "React JSX component"});
        types.push_back({"TSX File", "tsx", "Web", "React TypeScript component"});

        // Web
        types.push_back({"HTML File", "html", "Web", "HTML document"});
        types.push_back({"CSS Stylesheet", "css", "Web", "CSS styles"});
        types.push_back({"JSON File", "json", "Data", "JSON data file"});
        types.push_back({"XML File", "xml", "Data", "XML document"});

        // Pascal
        types.push_back({"Pascal Source File", "pas", "Pascal", "Pascal/Delphi source"});
        types.push_back({"Pascal Program", "dpr", "Pascal", "Delphi project file"});

        // Go
        types.push_back({"Go Source File", "go", "Go", "Go source code"});

        // Rust
        types.push_back({"Rust Source File", "rs", "Rust", "Rust source code"});

        // Swift
        types.push_back({"Swift Source File", "swift", "Swift", "Swift source code"});

        // Kotlin
        types.push_back({"Kotlin Source File", "kt", "Kotlin", "Kotlin source code"});
        types.push_back({"Kotlin Script", "kts", "Kotlin", "Kotlin script"});

        // Ruby
        types.push_back({"Ruby Source File", "rb", "Ruby", "Ruby script"});

        // PHP
        types.push_back({"PHP File", "php", "PHP", "PHP source"});

        // Lua
        types.push_back({"Lua Script", "lua", "Lua", "Lua script"});

        // SQL
        types.push_back({"SQL Script", "sql", "Database", "SQL query file"});

        // Shell
        types.push_back({"Shell Script", "sh", "Shell", "Bash/Shell script"});
        types.push_back({"PowerShell Script", "ps1", "Shell", "PowerShell script"});

        // Markup
        types.push_back({"Markdown File", "md", "Markup", "Markdown document"});
        types.push_back({"YAML File", "yaml", "Data", "YAML configuration"});

        // Assembly
        types.push_back({"Assembly File", "asm", "Assembly", "x86 assembly"});

        // Fortran
        types.push_back({"Fortran Source", "f90", "Fortran", "Fortran 90 source"});

        // BASIC
        types.push_back({"Visual Basic File", "vb", "BASIC", "Visual Basic source"});

        // Dart
        types.push_back({"Dart Source File", "dart", "Dart", "Dart/Flutter source"});

        // Elixir
        types.push_back({"Elixir Source File", "ex", "Elixir", "Elixir source"});
        types.push_back({"Elixir Script", "exs", "Elixir", "Elixir script"});

        // Perl
        types.push_back({"Perl Script", "pl", "Perl", "Perl script"});
        types.push_back({"Perl Module", "pm", "Perl", "Perl module"});

        // Lisp
        types.push_back({"Lisp Source File", "lisp", "Lisp", "Lisp source"});
        types.push_back({"Common Lisp File", "cl", "Lisp", "Common Lisp source"});

        // Prolog
        types.push_back({"Prolog File", "pro", "Prolog", "Prolog source"});

        // Smalltalk
        types.push_back({"Smalltalk File", "st", "Smalltalk", "Smalltalk source"});

        // Plain Text
        types.push_back({"Plain Text File", "txt", "Text", "Plain text file"});
        types.push_back({"Log File", "log", "Text", "Log file"});
        types.push_back({"Config File", "cfg", "Config", "Configuration file"});
        types.push_back({"INI File", "ini", "Config", "INI configuration"});

        return types;
    }

} // namespace UltraCanvas
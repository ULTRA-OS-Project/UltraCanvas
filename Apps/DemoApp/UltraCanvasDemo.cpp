// Apps/DemoApp/UltraCanvasDemo.cpp
// Comprehensive demonstration program implementation
// Version: 1.0.21 - Charts > Ternary Chart: the Density variant (density bins, contours, zoom)
// Version: 1.0.20 - Charts > Ternary Chart: Vote Swings, USDA Soil Texture and De Finetti variants
// Version: 1.0.19 - Charts > Ternary Chart (UltraCanvasTernaryChart)
// Version: 1.0.18 - Info Graphics > Performance Matrix and Charts > Importance-Performance Chart: fully implemented
// Version: 1.0.17 - Info Graphics > Performance Matrix: the Calibration variant
// Version: 1.0.16 - Bitmap Elements > JPEG XL Images (sample: media/images/jxl_art2.jxl)
// Version: 1.0.15 - Info Graphics > Performance Matrix: the element replaces the placeholder
// Version: 1.0.14 - Charts > Importance-Performance Chart (UltraCanvasImportancePerformanceChart)
// Version: 1.0.13 - gray, 8 px scrollbars; the status legend as rows sized to their text
// Version: 1.0.12 - no status bar; a fixed-width page keeps its width (min-width), so the
//                  page area scrolls sideways when the window is narrower
// Version: 1.0.11 - Widgets > Colour Palettes (UltraCanvasColorPaletteManager)
// Version: 1.0.10 - A filter field over the component tree ("Reset" inside it); the List
//                  View page holds the domain dashboard, which is no page of its own
// Version: 1.0.9 - The image performance page is registered only with libvips, as it is built
// Version: 1.0.8 - Browser build: the window fills the viewport; the audio, video and STL pages
//                 stay out, and a category left without pages gets no tree node
// Version: 1.0.7 - Run(showInfoWindow): no About window when the command line opens a page
// Last Modified: 2026-11-05
// V1.0.6: the header title wraps a long page description instead of cutting it off
// V1.0.5: event.targetWindow read via weak_ptr lock()
// V1.0.4: mainContainer scrollbars disabled (it is a pure layout wrapper and must
//   never scroll the header away); displayContainer is now the explicit single
//   scroll region (flex-grow:1, flex-shrink:1) and inserted examples are clamped
//   with flex-shrink:1 so they can't overflow the right-side area.
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextUtils.h"   // Trim - the tree filter ignores surrounding spaces
#include "UltraCanvasUtils.h"   // OpenURL — open markdown links in the system browser
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include <iostream>
#include <sstream>
#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {
    // Demo-wide scrollbar look: a slim, quiet gray bar - a light gray track
    // carrying a mid-gray, round-ended ("pill") thumb that darkens under the
    // pointer and while dragged. Registered as the application-wide default
    // scrollbar style in Initialize() so every element's scrollbars (the content
    // area, the navigation tree, list/tree/menu examples, …) share it.
    //
    // The thumb is a solid rounded rectangle (no grip image): its length tracks the
    // visible fraction of the content (UpdateThumbRect scales it by
    // viewportSize/contentSize), so the bar visibly represents how much of the full
    // area is on screen, and it reacts to hover and drag via the thumb color states
    // below. (Drawing the grip from an SVG would instead show a fixed-size centered
    // capsule that hides the proportional length and can't change color on hover.)
    static ScrollbarStyle DemoScrollbarStyle() {
        ScrollbarStyle s;
        s.trackSize         = 8;
        s.trackColor        = Color(238, 238, 238, 255);
        s.thumbColor        = Color(176, 176, 176, 255);
        s.thumbHoverColor   = Color(150, 150, 150, 255);
        s.thumbPressedColor = Color(128, 128, 128, 255);
        s.thumbCornerRadius = s.trackSize / 2;          // fully rounded ends
        s.trackCornerRadius = s.trackSize / 2;
        return s;
    }

    DemoLegendContainer::DemoLegendContainer(const std::string& identifier)
        : UltraCanvasContainer(identifier) {
        SetBackgroundColor(Color(245, 245, 245, 255));
        // A column of rows - the title, then icon and text per status - each as
        // tall as its text, so a longer translation wraps instead of being cut.
        layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        SetPadding(6, 10, 8, 10);

        legendTitle = std::make_shared<UltraCanvasLabel>("LegendTitle", Tr("Component Status Legend"));
        legendTitle->SetFontSize(12);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(80, 80, 80, 255));
        legendTitle->SetWrap(TextWrap::WrapWord);
        AddChild(legendTitle);

        auto addRow = [this](const std::string& id, std::shared_ptr<UltraCanvasImageElement>& icon,
                             std::shared_ptr<UltraCanvasLabel>& label, const std::string& text,
                             const Color& color) {
            auto row = std::make_shared<UltraCanvasContainer>(id + "Row");
            row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            icon = std::make_shared<UltraCanvasImageElement>(id + "Icon", 16, 16);
            icon->layoutItem.SetFlexShrink(0);
            row->AddChild(icon);
            label = std::make_shared<UltraCanvasLabel>(id + "Label", text);
            label->SetFontSize(11);
            label->SetTextColor(color);
            label->SetWrap(TextWrap::WrapWord);
            label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(label);
            AddChild(row);
        };
        addRow("Implemented", implementedIcon, implementedLabel, Tr("Fully Implemented"), Color(0, 150, 0, 255));
        addRow("Partial", partialIcon, partialLabel, Tr("Partially Implemented"), Color(0x21, 0x96, 0xf3, 255));
        addRow("NotImplemented", notImplementedIcon, notImplementedLabel, Tr("Not Implemented Yet"),
               Color(200, 0, 0, 255));
    }

    void DemoLegendContainer::SetupLegend(const std::string& implementedIconPath,
                                          const std::string& partialIconPath,
                                          const std::string& notImplementedIconPath) {
        // Load icon images
        if (implementedIcon) {
            implementedIcon->LoadFromFile(implementedIconPath);
        }

        if (partialIcon) {
            partialIcon->LoadFromFile(partialIconPath);
        }

        if (notImplementedIcon) {
            notImplementedIcon->LoadFromFile(notImplementedIconPath);
        }
    }

    DemoHeaderContainer::DemoHeaderContainer(const std::string& identifier)
            : UltraCanvasContainer(identifier) {

        // Create title label (left side). It is the page's one-line
        // description, and many are longer than the header is wide: it takes
        // all the width the two buttons leave and wraps there instead of being
        // cut off with an ellipsis, and the header grows to fit the lines.
        titleLabel = std::make_shared<UltraCanvasLabel>("HeaderTitle");
        titleLabel->SetFontSize(14);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetWrap(TextWrap::WrapWord);
        titleLabel->SetText("Demo Title");
        titleLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        AddChild(titleLabel);

        // Create documentation button (right side)
        docButton = std::make_shared<UltraCanvasImageElement>("DocBtn", 21, 21);
        docButton->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/text.png"));
        docButton->SetVisible(false);  // Initially disabled
        docButton->SetClickable(true);
        docButton->onClick = [this]() { ShowDocumentationWindow(); };
        // The title shrinks to make room, the buttons never: a long
        // description would otherwise squash them to a few pixels.
        docButton->layoutItem.SetFlexShrink(0);
        AddChild(docButton);

        // Create source button (right side)
        sourceButton = std::make_shared<UltraCanvasImageElement>("SourceBtn", 21, 28);
        sourceButton->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/c-plus-plus-icon.png"));
        sourceButton->SetVisible(false);  // Initially disabled
        sourceButton->SetClickable(true);
        sourceButton->onClick = [this]() { ShowSourceWindow(); };
        sourceButton->layoutItem.SetFlexShrink(0);
        AddChild(sourceButton);

        // Divider line pinned at the bottom of the header. Absolute-positioned
        // so it doesn't participate in the in-flow flex row (otherwise its
        // width:100% main-axis hypothetical would push the row into shrink
        // mode and the stretch spacer above would collapse to 0).
//        dividerLine = std::make_shared<UltraCanvasUIElement>("Divider");
//        dividerLine->size.height = CSSLayout::Dimension::Px(2);
//        dividerLine->SetBackgroundColor(Color(200, 200, 200, 255));
//        dividerLine->layoutItem.positionType = CSSLayout::PositionType::Absolute;
//        dividerLine->layoutItem.position = CSSLayout::Position{
//            /*top   */ CSSLayout::Dimension::Auto(),
//            /*right */ CSSLayout::Dimension::Px(0),
//            /*bottom*/ CSSLayout::Dimension::Px(0),
//            /*left  */ CSSLayout::Dimension::Px(0),
//        };
//        AddChild(dividerLine);

        // Set container style
        ContainerStyle containerStyle;
        containerStyle.forceShowHorizontalScrollbar = false;
        containerStyle.forceShowVerticalScrollbar = false;
        containerStyle.autoShowScrollbars = false;
        SetContainerStyle(containerStyle);

        SetBackgroundColor(Color(245, 245, 245, 255));
        SetPadding(5,10,5,10);
        SetBorderBottom(2, Colors::Gray);

        layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    }

    void DemoHeaderContainer::SetDemoTitle(const std::string& title) {
        if (titleLabel) {
            titleLabel->SetText(title);
        }
    }

    void DemoHeaderContainer::SetSourceFile(const std::string& sourceFile) {
        if (!sourceFile.empty()) {
            currentSourceFile = NormalizePath(GetResourcesDir()+sourceFile);
        } else {
            currentSourceFile.clear();
        }
        if (sourceButton) {
            sourceButton->SetVisible(!sourceFile.empty());
        }
    }

    void DemoHeaderContainer::SetDocFile(const std::string& docFile) {
        if (!docFile.empty()) {
            currentDocFile = NormalizePath(GetResourcesDir()+docFile);
        } else {
            currentDocFile.clear();
        }
        if (docButton) {
            docButton->SetVisible(!docFile.empty());
        }
    }

    std::string DemoHeaderContainer::LoadFileContent(const std::string& filePath) {
        if (filePath.empty()) return "";

        std::ifstream file(UltraCanvas::PathFromUtf8(filePath));
        if (!file.is_open()) {
            debugOutput << "Failed to open file: " << filePath << std::endl;
            return "// Error: Could not load file: " + filePath;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return buffer.str();
    }

    std::string DemoHeaderContainer::GetFileExtension(const std::string& filePath) {
        size_t lastDot = filePath.find_last_of('.');
        if (lastDot != std::string::npos) {
            return filePath.substr(lastDot + 1);
        }
        return "";
    }

    void DemoHeaderContainer::ShowSourceWindow() {
        if (currentSourceFile.empty()) return;

        std::string content = LoadFileContent(currentSourceFile);
        CreateSourceWindow(content, TrF("Source Code: {0}", currentSourceFile));
    }

    void DemoHeaderContainer::ShowDocumentationWindow() {
        if (currentDocFile.empty()) return;

        std::string content = LoadFileContent(currentDocFile);
        CreateDocumentationWindow(content, TrF("Documentation: {0}", currentDocFile));
    }


    void DemoHeaderContainer::CreateSourceWindow(const std::string& content, const std::string& title) {
        // Create a new window for source code display
        WindowConfig config;
        config.title = title;
        config.width = 1200;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;

        sourceWindow = CreateWindow(config);
        if (!sourceWindow->IsCreated()) {
            debugOutput << "Failed to create source window" << std::endl;
            return;
        }

        // Create text area for source code with syntax highlighting
        auto textArea = std::make_shared<UltraCanvasTextArea>("SourceCode", 5, 5, 1190, 590);
        textArea->SetText(content);
        //textArea->SetReadOnly(true);
        textArea->SetShowLineNumbers(true);
        textArea->SetHighlightSyntax(true);

        // Determine language from file extension and apply syntax highlighting
        std::string ext = GetFileExtension(currentSourceFile);
        if (ext == "cpp" || ext == "c" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp") {
            textArea->SetProgrammingLanguageByExtension(ext);
        } else {
            textArea->ApplyCodeStyle("text");
        }
        textArea->SetReadOnly(true);
        textArea->SetFontSize(10);

        sourceWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                sourceWindow->Close();
                sourceWindow.reset();
                return true;
            }
            return false;
        });

        sourceWindow->AddChild(textArea);
        sourceWindow->Show();
    }

    void DemoHeaderContainer::CreateDocumentationWindow(const std::string& content, const std::string& title) {
        // Create a new window for documentation display
        WindowConfig config;
        config.title = title;
        config.width = 1200;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;

        docWindow = CreateWindow(config);
        if (!docWindow->IsCreated()) {
            debugOutput << "Failed to create documentation window" << std::endl;
            return;
        }
        // Create text area for documentation
        auto markDownTextArea = std::make_shared<UltraCanvasTextArea>("Documentation");
        markDownTextArea->layout.display = CSSLayout::DisplayType::Block;
        markDownTextArea->size.width = CSSLayout::Dimension::Vw(100);
        markDownTextArea->size.height = CSSLayout::Dimension::Vh(100);
        markDownTextArea->SetText(content);
        markDownTextArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        markDownTextArea->SetReadOnly(true);
        markDownTextArea->SetWordWrap(true);
        markDownTextArea->SetCursorPosition(LineColumnIndex::INVALID);

        // Make markdown links clickable: external URLs (http/https/mailto) open in
        // the system browser via OpenURL; in-document anchors (#…) are handled
        // internally by the text area. This is what powers the per-library website
        // and Git links in Docs/Dependencies.md.
        markDownTextArea->onMarkdownLinkClick = [](const std::string& url) {
            if (url.empty()) return;
            if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
                url.rfind("mailto:", 0) == 0 || url.rfind("ftp://", 0) == 0) {
                OpenURL(url);
            }
        };


        docWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock()) static_cast<UltraCanvasWindow*>(tw.get())->Close();
                docWindow.reset();
                return true;
            }
            return false;
        });

        docWindow->AddChild(markDownTextArea);
        docWindow->Show();
    }

// ===== CONSTRUCTOR / DESTRUCTOR =====
    UltraCanvasDemoApplication::UltraCanvasDemoApplication() {
        currentSelectedId = "";
        currentDisplayElement = nullptr;
    }

    UltraCanvasDemoApplication::~UltraCanvasDemoApplication() {
        Shutdown();
    }

// ===== INITIALIZATION =====
    bool UltraCanvasDemoApplication::Initialize() {
        debugOutput << "Initializing UltraCanvas Demo Application..." << std::endl;

        // Register the demo's blue scrollbar style as the application-wide default
        // before any elements are created, so every element that owns a scrollbar
        // (containers, the navigation tree, list/tree/menu/dropdown examples, …)
        // adopts it for a consistent blue look.
        SetDefaultScrollbarStyle(DemoScrollbarStyle());

        // Create main window using proper configuration
        WindowConfig config;
        config.title = TrF("UltraCanvas Framework {0} - Component Demonstration", ULTRACANVAS_DEMO_VERSION);
        config.width = 1400;
        config.height = 880;
        config.resizable = true;
        config.type = WindowType::Standard;

        mainWindow = CreateWindow(config);
        debugOutput << "Creating Main window.." << std::endl;
        if (!mainWindow->IsCreated()) {
            debugOutput << "Failed to create main window" << std::endl;
            return false;
        }
        debugOutput << "Main window created" << std::endl;

        // Jump-to-last-window demo bindings: F6 or the mouse "back" thumb
        // button toggles between the two most recently used demo windows
        // (main window, source window, doc window, ...), restoring focus to
        // the input field that was active in the target window.
        auto* app = UltraCanvasApplication::GetInstance();
        app->SetJumpToLastWindowKey(UCKeys::F6);
        app->SetJumpToLastWindowMouseButton(UCMouseButton::Back);
        // Calculate positions for adjusted layout
        const int treeViewHeight = 740;  // Reduced from 840 to make room for legend
        const int legendHeight = 95;     // Height for legend container
        const int treeViewWidth = 350;   // Width for both treeview and legend

// Create tree view for categories (left side, reduced height)
        categoryTreeView = std::make_shared<UltraCanvasTreeView>("CategoryTree");
        categoryTreeView->SetRowHeight(24);
        categoryTreeView->SetSelectionMode(TreeSelectionMode::Single);
        categoryTreeView->SetLineStyle(TreeLineStyle::Solid);
        categoryTreeView->SetShowFirstChildOnExpand(true);
        categoryTreeView->SetAutoExpandSelectedNode(true);
        //categoryTreeView->SetPadding(1,3,1,3);

        debugOutput << "categoryTreeView created" << std::endl;

        // Filter field over the tree: typing narrows the tree to the entries
        // whose names contain the text, expanded so each one is on show. The
        // "Reset" button inside the field - TextInput's clear button with a
        // word on it - or Escape brings the whole tree back. Built without a
        // width, so the sidebar's column stretches it.
        treeFilterInput = std::make_shared<UltraCanvasTextInput>("CategoryTreeFilter", -1, 30);
        treeFilterInput->SetPlaceholder(Tr("Filter components"));
        treeFilterInput->SetShowClearButton(true);
        treeFilterInput->SetClearButtonText(Tr("Reset"));
        treeFilterInput->SetMargin(6, 8, 6, 8);
        treeFilterInput->layoutItem.SetFlexShrink(0);
        treeFilterInput->onTextChanged = [this](const std::string& text) { OnTreeFilterChanged(text); };
        treeFilterInput->onEscapePressed = [this]() {
            if (treeFilterInput->GetText().empty()) return false;
            treeFilterInput->SetText("");   // SetText does not call onTextChanged
            OnTreeFilterChanged("");
            return true;
        };

        // Create legend container below tree view
        legendContainer = std::make_shared<DemoLegendContainer>("LegendContainer");
        legendContainer->SetBorderTop(1, Colors::Gray);
        // As tall as its rows: the tree takes what is left, never the legend.
        legendContainer->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch).SetFlexShrink(0);
        SetupLegendContainer();

        // No-size constructor — let the grid cell decide categoryContainer's
        // dimensions. If we passed 100, 100 here, the engine would treat
        // size = 100×100 as a CSS-explicit width/height and shrink the
        // sidebar inside its 350-pixel grid column.
        auto categoryContainer = std::make_shared<UltraCanvasContainer>("catcont");

        mainContainer = std::make_shared<UltraCanvasContainer>("MainDisplayArea");
        mainContainer->SetBorderLeft(1, Colors::Gray);
        // mainContainer is a pure layout wrapper (header + display); it must never
        // scroll itself, or the header would scroll away with the content. Only the
        // displayContainer below is allowed to scroll. Disable its scrollbars so an
        // oversized example can never turn the whole right side into a scroll area.
        {
            ContainerStyle mcStyle;
            mcStyle.autoShowScrollbars           = false;
            mcStyle.forceShowVerticalScrollbar   = false;
            mcStyle.forceShowHorizontalScrollbar = false;
            mainContainer->SetContainerStyle(mcStyle);
        }

        // Header sized by its parent row — no explicit w/h.
        headerContainer = std::make_shared<DemoHeaderContainer>("HeaderContainer");

        // Create display container (below header)
        displayContainer = std::make_shared<UltraCanvasContainer>("DisplayArea");
        displayContainer->SetBackgroundColor(Colors::White);
        // displayContainer is the demo's main scroll region — give its scrollbars
        // the shared slim, round-ended demo style. Containers stopped scrolling
        // by default in 0.9.22, so a scroll view has to ask: without this every
        // example page taller than the window was cut off at the window edge.
        {
            ContainerStyle dcStyle = displayContainer->GetContainerStyle();
            dcStyle.autoShowScrollbars = true;
            dcStyle.scrollbarStyle = DemoScrollbarStyle();
            displayContainer->SetContainerStyle(dcStyle);
        }

        // Register all demo items
        RegisterAllDemoItems();

        // Setup tree view with categories
        SetupTreeView();

        // Setup event handlers
        categoryTreeView->onNodeSelected = [this](TreeNode* node) {
            OnTreeNodeSelected(node);
        };

        categoryContainer->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        // flex-grow fills the main (vertical) axis; align-self: stretch fills the
        // cross (horizontal) axis. justify-self is grid-only — adding it here would
        // convert layoutItem into a GridItem and discard the flex props above.
        categoryTreeView->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        categoryContainer->AddChild(treeFilterInput);
        categoryContainer->AddChild(categoryTreeView);
        categoryContainer->AddChild(legendContainer);

        mainContainer->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        // Fixed top bar: never shrink below its content. Without this the header is a
        // flex-shrink:1 item, so a tall displayContainer (whose content overflows the
        // column) would steal negative space from it and clip its top. flex-shrink:0
        // freezes the header at its content height; displayContainer absorbs the
        // overflow and scrolls.
        headerContainer->layoutItem.SetFlexShrink(0);
        // A definite width, not just the column's "at most this wide": only
        // then does the header row shrink its wrapping title to the room the
        // buttons leave and measure the wrapped height. Measured against
        // "at most", the row keeps the title one line long and one line tall,
        // so a long description lost its second line.
        headerContainer->size.width = CSSLayout::Dimension::Pct(100);
        mainContainer->AddChild(headerContainer);
        // displayContainer is the single scroll region: it grows into the space
        // the header leaves and shrinks (flex-shrink:1) to the available height so
        // mainContainer never overflows — its own auto scrollbars then scroll the
        // example content. Shrink is the default, but make it explicit so a future
        // edit can't silently drop it and reintroduce the overflow.
        displayContainer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        displayContainer->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        mainContainer->AddChild(displayContainer);

        mainWindow->layout
            .SetGrid()
            .SetGridColumns({CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fixed, .value=CSSLayout::Dimension::Px(350)},
                             CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fr, .value=CSSLayout::Dimension::Fr(1)}})
             // One Fr(1) row, so it consumes all available vertical space
             // (otherwise Auto would collapse the sidebar to ~4 tree rows).
             .SetGridRows({CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fr, .value=CSSLayout::Dimension::Fr(1)}})
            .SetGridJustifyItems(CSSLayout::JustifyItems::Stretch)
            .SetGridAlignItems(CSSLayout::AlignItems::Stretch);

        categoryContainer->layoutItem.SetGridRowColSimplified(0, 0);
        mainWindow->AddChild(categoryContainer);

        mainContainer->layoutItem.SetGridRowColSimplified(0, 1);
        mainWindow->AddChild(mainContainer);

        debugOutput << "✓ Demo application initialized successfully" << std::endl;
        return true;
    }

    // ===== SETUP LEGEND CONTAINER METHOD =====
    void UltraCanvasDemoApplication::SetupLegendContainer() {
        if (legendContainer) {
            // Get icon paths using the existing GetStatusIcon method
            std::string implementedIcon = GetStatusIcon(ImplementationStatus::FullyImplemented);
            std::string partialIcon = GetStatusIcon(ImplementationStatus::PartiallyImplemented);
            std::string notImplementedIcon = GetStatusIcon(ImplementationStatus::NotImplemented);

            // Setup the legend with the appropriate icons
            legendContainer->SetupLegend(implementedIcon, partialIcon, notImplementedIcon);
        }
    }

    void UltraCanvasDemoApplication::RegisterAllDemoItems() {
        debugOutput << "Registering demo items..." << std::endl;

        // ===== BASIC UI ELEMENTS =====
        auto basicBuilder = DemoCategoryBuilder(this, DemoCategory::BasicUI);

        basicBuilder.AddItem("menu", Tr("Menus"), Tr("Various menu types and styles"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateMenuExamples(); },
                             "DemoApp/UltraCanvasMenuExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasMenuExamples.md")
                .AddVariant("menu", Tr("Context Menu"))
                .AddVariant("menu", Tr("Main Menu Bar"))
                .AddVariant("menu", Tr("Popup Menu"))
                .AddVariant("menu", Tr("Submenu Navigation"))
                .AddVariant("menu", Tr("Checkbox/Radio Items"))
                .AddVariant("menu", Tr("Styled Menus"));

        basicBuilder.AddItem("toolbar", Tr("Toolbar"), Tr("Tool and action bars"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateToolbarExamples(); },
                             "DemoApp/UltraCanvasToolbarExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasToolbarExamples.md")
                .AddVariant("toolbar", Tr("Horizontal Toolbar"))
                .AddVariant("toolbar", Tr("Vertical Toolbar"))
                .AddVariant("toolbar", Tr("Ribbon Style"));

        basicBuilder.AddItem("tabs", Tr("Tabs"), Tr("Tabbed interface containers"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTabExamples(); },
                             "DemoApp/UltraCanvasTabExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTabExamples.md")
                .AddVariant("tabs", Tr("Top Tabs"))
                .AddVariant("tabs", Tr("Side Tabs"))
                .AddVariant("tabs", Tr("Closable Tabs"));

        basicBuilder.AddItem("splitpane", Tr("Split Pane"),
                             Tr("Resizable horizontal/vertical pane splitter (VSCode-style)"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSplitPaneExamples(); },
                             "DemoApp/UltraCanvasSplitPaneExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSplitPane.md")
                .AddVariant("splitpane", Tr("Horizontal Split"))
                .AddVariant("splitpane", Tr("Vertical Split"))
                .AddVariant("splitpane", Tr("Nested Splits"))
                .AddVariant("splitpane", Tr("Splitter Handles"))
                .AddVariant("splitpane", Tr("Split Line Action Icons"));

        basicBuilder.AddItem("layouts", Tr("Layout System"),
                             Tr("Box, Grid, and Flex layout examples"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLayoutExamples(); },
                             "DemoApp/UltraCanvasLayoutExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLayoutExamples.md")
                .AddVariant("layouts", Tr("Vertical Box Layout"))
                .AddVariant("layouts", Tr("Horizontal Box Layout"))
                .AddVariant("layouts", Tr("Grid Layout"))
                .AddVariant("layouts", Tr("Flex Layout"));

        basicBuilder.AddItem("segmentedcontrol", Tr("Segmented Control"),
                             Tr("Compact control for selecting between mutually exclusive options"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSegmentedControlExamples(); },
                             "DemoApp/UltraCanvasSegmentedControlExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSegmentedControlExamples.md")
                .AddVariant("segmentedcontrol", Tr("Bordered Style"))
                .AddVariant("segmentedcontrol", Tr("iOS Style"))
                .AddVariant("segmentedcontrol", Tr("Flat Style"))
                .AddVariant("segmentedcontrol", Tr("Bar Style"))
                .AddVariant("segmentedcontrol", Tr("Toggle Mode"))
                .AddVariant("segmentedcontrol", Tr("FitContent Width"));

        basicBuilder.AddItem("groupbox", Tr("Group Box"),
                             Tr("Titled container that frames a set of child elements"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateGroupBoxExamples(); },
                             "DemoApp/UltraCanvasGroupBoxExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasGroupBoxExamples.md")
                .AddVariant("groupbox", Tr("Framed Style"))
                .AddVariant("groupbox", Tr("Header Style"))
                .AddVariant("groupbox", Tr("Flat Style"))
                .AddVariant("groupbox", Tr("Title Alignment"))
                .AddVariant("groupbox", Tr("Checkable Group"))
                .AddVariant("groupbox", Tr("Collapsible Group"));

        basicBuilder.AddItem("tooltip", Tr("Tooltips"),
                             Tr("Hover tooltips: plain text, inline markup and structured content"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTooltipExamples(); },
                             "DemoApp/UltraCanvasTooltipExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTooltipManager.md")
                .AddVariant("tooltip", Tr("Plain Text"))
                .AddVariant("tooltip", Tr("Inline Pango Markup"))
                .AddVariant("tooltip", Tr("Title + Label/Value Table"))
                .AddVariant("tooltip", Tr("Color Swatch Rows"))
                .AddVariant("tooltip", Tr("Bullet List + Separator"))
                .AddVariant("tooltip", Tr("Light/Custom Style Presets"));

        basicBuilder.AddItem("textinput", Tr("Text Input"), Tr("Text input fields with validation and formatting"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextInputExamples(); },
                             "DemoApp/UltraCanvasTextInputExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTextInputExamples.md"
                )
                .AddVariant("textinput", Tr("Single Line Input"))
                .AddVariant("textinput", Tr("Multi-line Text Area"))
                .AddVariant("textinput", Tr("Password Field"))
                .AddVariant("textinput", Tr("Numeric Input"))
                .AddVariant("textinput", Tr("Clear / \"Reset\" Button in the Field"));

        basicBuilder.AddItem("autocomplete", Tr("AutoComplete"), Tr("Text input with auto-complete suggestions"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAutoCompleteExamples(); },
                             "DemoApp/UltraCanvasAutoCompleteExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAutoCompleteExamples.md")
                .AddVariant("autocomplete", Tr("Static Items"))
                .AddVariant("autocomplete", Tr("Dynamic Provider"))
                .AddVariant("autocomplete", Tr("Interactive Demo"));

        basicBuilder.AddItem("label", Tr("Label"), Tr("Text display with formatting and styling"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLabelExamples(); },
                             "DemoApp/UltraCanvasLabelExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLabelExamples.md")
                .AddVariant("label", Tr("Basic Label"))
                .AddVariant("label", Tr("Header Text"))
                .AddVariant("label", Tr("Status Label"));

        basicBuilder.AddItem("button", Tr("Button"), Tr("Interactive buttons with various styles and states"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateButtonExamples(); },
                             "DemoApp/UltraCanvasButtonExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasButtonExamples.md"
                             )
                .AddVariant("button", Tr("Standard Button"))
                .AddVariant("button", Tr("Icon Button"))
                .AddVariant("button", Tr("Toggle Button"))
                .AddVariant("button", Tr("Three-Section Button"));

        basicBuilder.AddItem("dropdown", Tr("Dropdown / ComboBox"), Tr("Dropdown selection controls"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDropdownExamples(); },
                            "DemoApp/UltraCanvasDropDownExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasDropDownExamples.md")
                .AddVariant("dropdown", Tr("Simple Dropdown"))
                .AddVariant("dropdown", Tr("Editable ComboBox"))
                .AddVariant("dropdown", Tr("Multi-Select"));

        basicBuilder
                .AddItem("checkbox", Tr("Checkbox / Radio / Switch"),
                         Tr("Interactive checkbox controls with multiple states and styles"),
                         ImplementationStatus::FullyImplemented,
                         [this]() { return CreateCheckboxExamples(); },
                         "DemoApp/UltraCanvasCheckboxExamples.cpp",
                         "Docs/UltraCanvas/UltraCanvasCheckbox.md")
                .AddVariant("checkbox", Tr("Standard Checkbox"))
                .AddVariant("checkbox", Tr("Tri-State Checkbox"))
                .AddVariant("checkbox", Tr("Switch Toggle"))
                .AddVariant("checkbox", Tr("Radio Button"));

        basicBuilder.AddItem("slider", Tr("Slider"), Tr("Range and value selection sliders"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSliderExamples(); },
                            "DemoApp/UltraCanvasSliderExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasSliderExamples.md")
                .AddVariant("slider", Tr("Horizontal Slider"))
                .AddVariant("slider", Tr("Vertical Slider"))
                .AddVariant("slider", Tr("Range Slider"));

        basicBuilder.AddItem("spinner", Tr("Spinner / SpinBox"),
                             Tr("Select a value with up/down (or left/right) arrow buttons, "
                             "arrow keys, the mouse wheel, or by typing"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSpinnerExamples(); },
                             "DemoApp/UltraCanvasSpinnerExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSpinner.md")
                .AddVariant("spinner", Tr("Integer Spinner"))
                .AddVariant("spinner", Tr("Decimal Spinner"))
                .AddVariant("spinner", Tr("List / Value Cycle"))
                .AddVariant("spinner", Tr("Horizontal Stepper"));

        basicBuilder.AddItem("scrollbars", Tr("Scrollbars"),
                             Tr("Standalone scrollbars: preset styles, colour options, "
                             "corner-radius / end-shape control and a custom SVG handle"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateScrollbarExamples(); },
                             "DemoApp/UltraCanvasScrollbarExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasScrollbarExamples.md")
                .AddVariant("scrollbars", Tr("Preset Styles"))
                .AddVariant("scrollbars", Tr("Colour Options"))
                .AddVariant("scrollbars", Tr("Corner Radius / End Shape"))
                .AddVariant("scrollbars", Tr("Custom SVG Handle"))
                .AddVariant("scrollbars", Tr("Horizontal Orientation"));

        basicBuilder.AddItem("breadcrumb", Tr("Breadcrumb"),
                             Tr("Hierarchical path navigation with clickable segments"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBreadcrumbExamples(); },
                             "DemoApp/UltraCanvasBreadcrumbExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBreadcrumbExamples.md")
                .AddVariant("breadcrumb", Tr("Default Style"))
                .AddVariant("breadcrumb", Tr("Compact Style"))
                .AddVariant("breadcrumb", Tr("Pills Style"))
                .AddVariant("breadcrumb", Tr("File Explorer Style"))
                .AddVariant("breadcrumb", Tr("Web Docs Style"))
                .AddVariant("breadcrumb", Tr("Separator Styles"))
                .AddVariant("breadcrumb", Tr("Dropdown Items"))
                .AddVariant("breadcrumb", Tr("Icons"))
                .AddVariant("breadcrumb", Tr("Live Navigation"))
                .AddVariant("breadcrumb", Tr("Collapse Overflow"))
                .AddVariant("breadcrumb", Tr("Ellipsize Overflow"))
                .AddVariant("breadcrumb", Tr("ShrinkText Overflow"))
                .AddVariant("breadcrumb", Tr("Rounded Strip"));

        basicBuilder.AddItem("gauges", Tr("Gauges"),
                             Tr("Mode-driven gauges: analog dials, progress/LED bars, "
                             "circular rings, batteries, thermometers, clocks and digital panels"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateGaugeExamples(); },
                             "DemoApp/UltraCanvasGaugeExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasGaugeExamples.md")
                .AddVariant("gauges", Tr("Round Gauges"))
                .AddVariant("gauges", Tr("Progress &amp; Linear"))
                .AddVariant("gauges", Tr("Specialized"))
                .AddVariant("gauges", Tr("Analog"));

        basicBuilder.AddItem("alert", Tr("Alert / Message Box"),
                             Tr("Modal, always-on-top alert dialogs (info, success, warning, error, confirm)"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAlertExamples(); },
                             "DemoApp/UltraCanvasAlertExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAlert.md")
                .AddVariant("alert", Tr("Info / Success"))
                .AddVariant("alert", Tr("Warning / Error"))
                .AddVariant("alert", Tr("Confirm (Yes/No)"))
                .AddVariant("alert", Tr("Rich (details / buttons)"));

        // The non-modal sibling of the Alert: how ULTRA OS shows a
        // notification (the element lives with the message centre in the
        // UltraMessageCenter target; without it the page says so).
        basicBuilder.AddItem("notificationtoast", Tr("Notification Toast"),
                             Tr("Non-modal notifications in a screen corner, never focused, gone by themselves "
                             "- how ULTRA OS shows every application's notification"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateNotificationToastExamples(); },
                             "DemoApp/UltraCanvasNotificationToastExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasNotificationToast.md")
                .AddVariant("notificationtoast", Tr("The element"))
                .AddVariant("notificationtoast", Tr("On the screen (host)"))
                .AddVariant("notificationtoast", Tr("Updates in place"))
                .AddVariant("notificationtoast", Tr("Toast or Alert?"));

        basicBuilder.AddItem("pagination", Tr("Pagination"),
                             Tr("Page-navigation strip with ellipsis windowing, compact and simple modes"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePaginationExamples(); },
                             "DemoApp/UltraCanvasPaginationExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPagination.md")
                .AddVariant("pagination", Tr("Numbered / Ellipsis"))
                .AddVariant("pagination", Tr("From Item Count"))
                .AddVariant("pagination", Tr("Compact"))
                .AddVariant("pagination", Tr("Simple"));

        basicBuilder.AddItem("rating", Tr("Rating"),
                             Tr("Star/shape rating with half steps and custom SVG on/off/half symbols"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateRatingExamples(); },
                             "DemoApp/UltraCanvasRatingExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasRating.md")
                .AddVariant("rating", Tr("Stars (whole / half)"))
                .AddVariant("rating", Tr("Circle / Square"))
                .AddVariant("rating", Tr("Read-only"))
                .AddVariant("rating", Tr("Custom SVG symbols"));

        basicBuilder.AddItem("stepper", Tr("Stepper / Wizard"),
                             Tr("Multi-step progress indicator: numbered/icon/dot markers, horizontal & vertical, linear & non-linear"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateStepperExamples(); },
                             "DemoApp/UltraCanvasStepperExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasStepper.md")
                .AddVariant("stepper", Tr("Horizontal Wizard"))
                .AddVariant("stepper", Tr("Descriptions / Error"))
                .AddVariant("stepper", Tr("Vertical"))
                .AddVariant("stepper", Tr("Dot / Non-linear"));

        basicBuilder.AddItem("chip", Tr("Chip / Tag Input"),
                             Tr("Compact chips (filled/outlined/closable/selectable) and a wrapping tag/token input field"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateChipExamples(); },
                             "DemoApp/UltraCanvasChipExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasChip.md")
                .AddVariant("chip", Tr("Filled / Outlined"))
                .AddVariant("chip", Tr("Closable"))
                .AddVariant("chip", Tr("Filter (selectable)"))
                .AddVariant("chip", Tr("Tag Input"));

        basicBuilder.AddItem("chipgroup", Tr("Chip Group"),
                             Tr("A row of chips with one selection rule: single (categories, filters) or multiple (tags)"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateChipGroupExamples(); },
                             "DemoApp/UltraCanvasChipGroupExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasChipGroup.md")
                .AddVariant("chipgroup", Tr("Single selection"))
                .AddVariant("chipgroup", Tr("Single, none allowed"))
                .AddVariant("chipgroup", Tr("Multiple selection"));

        basicBuilder.AddItem("card", Tr("Card"),
                             Tr("A raised surface with header, content and a row of real buttons; clickable, highlighted, selected"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateCardExamples(); },
                             "DemoApp/UltraCanvasCardExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasCard.md")
                .AddVariant("card", Tr("Header, content, actions"))
                .AddVariant("card", Tr("Clickable surface"))
                .AddVariant("card", Tr("Highlighted / Selected"));

        basicBuilder.AddItem("collapsiblepanel", Tr("Collapsible Panel"),
                             Tr("A panel that folds to its header strip: chevron, title, detail text, header buttons, a resizable top edge"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateCollapsiblePanelExamples(); },
                             "DemoApp/UltraCanvasCollapsiblePanelExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasCollapsiblePanel.md")
                .AddVariant("collapsiblepanel", Tr("Fold / unfold"))
                .AddVariant("collapsiblepanel", Tr("Header buttons"))
                .AddVariant("collapsiblepanel", Tr("Bottom dock, resizable"));

        basicBuilder.AddItem("sidebarnav", Tr("Sidebar Navigation"),
                             Tr("The navigation column of a window: sections of items with icon, label and badge; keyboard; compact mode"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSidebarNavigationExamples(); },
                             "DemoApp/UltraCanvasSidebarNavigationExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSidebarNavigation.md")
                .AddVariant("sidebarnav", Tr("Sections and badges"))
                .AddVariant("sidebarnav", Tr("Compact mode"))
                .AddVariant("sidebarnav", Tr("Keyboard"));

        basicBuilder.AddItem("badge", Tr("Badge"),
                             Tr("Count / status indicators: coloured pills, dots, and overlay badges anchored to icons"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBadgeExamples(); },
                             "DemoApp/UltraCanvasBadgeExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBadge.md")
                .AddVariant("badge", Tr("Status Pills"))
                .AddVariant("badge", Tr("Count (99+)"))
                .AddVariant("badge", Tr("Status Dots"))
                .AddVariant("badge", Tr("Overlay on Icon"));

        basicBuilder.AddItem("busyindicator", Tr("Busy Indicator"),
                             Tr("\"Working on it\" with no percentage: a turning ring, two counter-turning "
                             "rings, swelling dots, a sliding bar, a pulsing circle or a ring of "
                             "circling dots"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBusyIndicatorExamples(); },
                             "DemoApp/UltraCanvasBusyIndicatorExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBusyIndicator.md")
                .AddVariant("busyindicator", Tr("Ring"))
                .AddVariant("busyindicator", Tr("Dual Ring"))
                .AddVariant("busyindicator", Tr("Dots"))
                .AddVariant("busyindicator", Tr("Bar"))
                .AddVariant("busyindicator", Tr("Pulse"))
                .AddVariant("busyindicator", Tr("Dot Ring"));

        // ===== EXTENDED FUNCTIONALITY =====
        auto extendedBuilder = DemoCategoryBuilder(this, DemoCategory::ExtendedFunctionality);

        extendedBuilder.AddItem("textarea", Tr("Advanced Text Area"), Tr("Advanced text editing with syntax highlighting"),
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTextAreaExamples(); },
                                "DemoApp/UltraCanvasTextAreaExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasTextAreaExamples.md")
                .AddVariant("textarea", Tr("C++ Syntax Highlighting"))
                .AddVariant("textarea", Tr("Python Syntax Highlighting"))
                .AddVariant("textarea", Tr("Pascal Syntax Highlighting"))
                .AddVariant("textarea", Tr("Line Numbers Display"))
                .AddVariant("textarea", Tr("Theme Support"));

        extendedBuilder.AddItem("treeview", Tr("Tree View"), Tr("Hierarchical data display with icons"),
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTreeViewExamples(); },
                                "DemoApp/UltraCanvasTreeViewExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasTreeViewExamples.md")
                .AddVariant("treeview", Tr("File Explorer Style"))
                .AddVariant("treeview", Tr("Multi-Selection Tree (arrow expanders)"))
                .AddVariant("treeview", Tr("Debugger Variables (columns)"))
                .AddVariant("treeview", Tr("Connecting Lines (none / dotted / solid)"))
                .AddVariant("treeview", Tr("Checkable Nodes"));

        extendedBuilder.AddItem("spreadsheet", Tr("Spreadsheet engine"),
                                Tr("Editable spreadsheet grid with an Open button that loads .ods/.csv files"),
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateSpreadsheetExamples(); },
                                "DemoApp/UltraCanvasSpreadsheetExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasSpreadsheetExamples.md")
                .AddVariant("spreadsheet", Tr("Sample Data"))
                .AddVariant("spreadsheet", Tr("Open ODS / CSV"));

        // The domain dashboard - the old "Templates demo" (a container of
        // elements per row) ported to the list view, one delegate painting
        // every row - is this page's last section.
        extendedBuilder.AddItem("listview", Tr("List View"),
                                Tr("Model-view-delegate lists: columns, styling, icons, tooltips, a custom-delegate dashboard"),
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateListViewExamples(); },
                                "DemoApp/UltraCanvasListViewExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasListViewExamples.md")
                .AddVariant("listview", Tr("Simple List"))
                .AddVariant("listview", Tr("Detail View (columns + header)"))
                .AddVariant("listview", Tr("Styled List"))
                .AddVariant("listview", Tr("Icon List"))
                .AddVariant("listview", Tr("Domain Dashboard (custom delegate)"));

        // ===== BITMAP ELEMENTS =====
        // Only with libvips: the format pages (UltraCanvasBitmapFormatDemo.cpp)
        // and the performance page benchmark its codecs, and both files are
        // built only with it (CMakeLists.txt, "These files depend on libvips").
        // Without it the category has no pages and gets no tree node.
#ifdef HAS_LIBVIPS
        auto bitmapBuilder = DemoCategoryBuilder(this, DemoCategory::BitmapElements);

        bitmapBuilder.AddItem("pngimages", Tr("PNG Images"), Tr("PNG Image display and manipulation"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("PNG", NormalizePath(GetResourcesDir() + "media/images/dice.png")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("PNG/JPEG Display"));
        bitmapBuilder.AddItem("jpegimages", Tr("JPEG Images"), Tr("JPEG Image display and manipulation"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("JPG", NormalizePath(GetResourcesDir() + "media/images/dice.jpg")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("PNG/JPEG Display"));

// AVIF Images
        bitmapBuilder.AddItem("avifimages", Tr("AVIF Images"),
                              Tr("AVIF next-gen format with superior compression and HDR support"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("AVIF", NormalizePath(GetResourcesDir() + "media/images/dice.avif")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Modern Formats"));

        // WEBP Images
        bitmapBuilder.AddItem("webpimages", Tr("WEBP Images"),
                              Tr("Google WebP format with excellent compression and web optimization"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("WEBP", NormalizePath(GetResourcesDir() + "media/images/dice.webp")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Modern Formats"));

        // HEIF Images
        bitmapBuilder.AddItem("heifimages", Tr("HEIF/HEIC Images"),
                              Tr("HEIF high efficiency format with HEVC compression"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("HEIC", NormalizePath(GetResourcesDir() + "media/images/dice.heic")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Modern Formats"));

        // JPEG XL Images - the sample is JXL art (82 bytes, 2160 x 3840).
        // A build whose libvips lacks libjxl shows the page with a notice
        // in place of the picture.
        bitmapBuilder.AddItem("jxlimages", Tr("JPEG XL Images"),
                              Tr("JPEG XL: lossy and lossless, HDR, and lossless JPEG recompression"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("JXL", NormalizePath(GetResourcesDir() + "media/images/jxl_art2.jxl")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Modern Formats"));

        // GIF Images
        bitmapBuilder.AddItem("gifimages", Tr("GIF Images"),
                              Tr("GIF animated format with 256 color palette"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("GIF", NormalizePath(GetResourcesDir() + "media/images/dice.gif")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Animation Support"));

        // TIFF Images
        bitmapBuilder.AddItem("tiffimages", Tr("TIFF Images"),
                              Tr("TIFF professional format for archival and print"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("TIFF", NormalizePath(GetResourcesDir() + "media/images/dice.tiff")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Professional Formats"));

        // BMP Images
        bitmapBuilder.AddItem("bmpimages", Tr("BMP Images"),
                              Tr("BMP Windows native bitmap format"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("BMP", NormalizePath(GetResourcesDir() + "media/images/dice.bmp")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", Tr("Legacy Formats"));

        // QOI Images (kept as partially implemented if it exists)
        bitmapBuilder.AddItem("qoiimages", Tr("QOI Images"),
                              Tr("QOI Image display and manipulation"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("QOI", NormalizePath(GetResourcesDir() + "media/images/dice.qoi")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md");

        // PSP Images (Paint Shop Pro native format with layers and vector objects)
//        bitmapBuilder.AddItem("pspimages", "PSP Images",
//                              "PSP Paint Shop Pro native layered image format",
//                              ImplementationStatus::FullyImplemented,
//                              [this]() { return CreateBitmapFormatDemoPage("PSP", NormalizePath(GetResourcesDir() + "media/images/dice.pspimage")); },
//                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
//                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md");

        bitmapBuilder.AddItem("imageperformance", Tr("Image Performance Test"),
                              Tr("Benchmark image loading, decompression, and rendering speed"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateImagePerformanceTest(); },
                              "DemoApp/UltraCanvasImagePerformanceTest.cpp",
                              "Docs/UltraCanvas/UltraCanvasImagePerformanceTest.md")
                .AddVariant("imageperformance", Tr("Full Pipeline Test"))
                .AddVariant("imageperformance", Tr("Decompress + Draw Test"))
                .AddVariant("imageperformance", Tr("Draw Only Test"));
#endif  // HAS_LIBVIPS

        // ("drawing" / "Drawing Surface" placeholder removed: no
        // UltraCanvasDrawingSurface was ever built, so the page rendered an
        // empty container. The surface that does exist is
        // UltraCanvasVectorCanvas, registered under Widgets as
        // "vectorcanvas" — draw, select, move, scale, rotate, group, undo.)

        // ===== VECTOR ELEMENTS =====
        auto vectorBuilder = DemoCategoryBuilder(this, DemoCategory::VectorElements);

        vectorBuilder.AddItem("svg", Tr("SVG Graphics"), Tr("Scalable vector graphics rendering"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateSVGVectorExamples(); },
                              "DemoApp/UltraCanvasSVGExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasSVGExamples.md")
                .AddVariant("svg", Tr("SVG File Display"))
                .AddVariant("svg", Tr("Interactive SVG"))
                .AddVariant("svg", Tr("SVG Animations"));
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
        vectorBuilder.AddItem("cdrimages", Tr("CDR Images"), Tr("CDR (CorelDraw) images display and manipulation"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateCDRVectorExamples(); },
                              "DemoApp/UltraCanvasCDRExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasCDRExamples.md");
#endif
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
        vectorBuilder.AddItem("xarimages", Tr("XAR Images"), Tr("XAR Image display and manipulation"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateXARVectorExamples(); },
                              "DemoApp/UltraCanvasXARExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasXARExamples.md");
#endif
#ifdef ULTRACANVAS_HAS_EPS_PLUGIN
        vectorBuilder.AddItem("epsimages", Tr("EPS Images"), Tr("EPS (Encapsulated PostScript) display via the built-in PostScript interpreter"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateEPSVectorExamples(); },
                              "DemoApp/UltraCanvasEPSExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasEPSExamples.md");
#endif
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        // CAD as drawings: four of the five samples are flat by construction -
        // blocks, hatches, LWPOLYLINEs and NURBS splines, every Z at zero - so
        // they belong here rather than among the 3D models, and they live in
        // media/vector/{DWG,DXF} to match. The fifth, a DWG of 3D polyface
        // meshes, stays with the models in media/3D/DWG and is shown here
        // projected to plan view, which is what a CAD reader does with it.
        // The DXF that really is geometry has its own "DXF 3D Models" page
        // under 3D Graphics, read through the Models plugin.
        vectorBuilder.AddItem("dwgdrawings", Tr("DWG / DXF Drawings"),
                              Tr("AutoCAD drawings decoded natively (R13 to R2018) through the Vector plugin's converter matrix"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateDWGVectorExamples(); },
                              "DemoApp/UltraCanvasDWGExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasVectorConverters.md");
#endif
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        vectorBuilder.AddItem("aiartwork", Tr("AI Artwork"), Tr("Adobe Illustrator .ai artwork - the Vector plugin's AIConverter reads Illustrator's art language out of the file's private data, with the PDF engine for PDF-compatible files"),
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateAIVectorExamples(); },
                              "DemoApp/UltraCanvasAIExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasAIExamples.md");
#endif

        // ===== CHARTS =====
        auto chartBuilder = DemoCategoryBuilder(this, DemoCategory::Charts);

        chartBuilder.AddItem("chartengine", Tr("Chart Engine"), Tr("The shared chart engine on one minimal bar chart: projections, axis scales (linear / log / symlog), grid, limiters, legend, label plan, tooltips and animation, all switchable live"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateChartEngineExamples(); },
                             "DemoApp/UltraCanvasChartEngineExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasChartEngine.md");

        chartBuilder.AddItem("chartenginecompare", Tr("Chart Engine Comparison"), Tr("Every chart ported to the chart engine next to the legacy chart it replaces, on the same data: line, area, cumulative flow, scatter, bubble, waterfall, diverging, population, dumbbell, Mekko, financial, heatmap, radar, radial bar, pie and polar"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateChartEngineComparisonExamples(); },
                             "DemoApp/UltraCanvasEngineComparisonExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasEngineCharts.md");

        chartBuilder.AddItem("linecharts", Tr("Line Chart"), Tr("Line chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLineChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLineChartElement.md");

        chartBuilder.AddItem("barcharts", Tr("Bar Chart"), Tr("Bar chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBarChartsExamples(); },
                             "DemoApp/UltraCanvasBarChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasChartEngine.md");

        chartBuilder.AddItem("scattercharts", Tr("Scatter Plot Chart"), Tr("Scatter plot chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateScatterPlotChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasScatterPlotElement.md");

        chartBuilder.AddItem("areacharts", Tr("Area Chart"), Tr("Area chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAreaChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAreaChartElement.md");

        chartBuilder.AddItem("piecharts", Tr("Pie Chart"), Tr("Pie chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePieChartExamples(); },
                             "DemoApp/UltraCanvasPieChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPieChartExamples.md");

        chartBuilder.AddItem("radarcharts", Tr("Radar Chart"), Tr("Radar / spider chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateRadarChartExamples(); },
                             "DemoApp/UltraCanvasRadarChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasRadarChartElement.md");

        chartBuilder.AddItem("parallelcoordinates", Tr("Parallel Coordinates"), Tr("Multi-dimensional records as polylines across parallel axes"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateParallelCoordinateChartExamples(); },
                             "DemoApp/UltraCanvasParallelCoordinateChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasParallelCoordinateChart.md");

        chartBuilder.AddItem("financialcharts", Tr("Candlestick Chart"), Tr("Stock market OHLC and candlestick charts"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateFinancialChartExamples(); },
                             "DemoApp/UltraCanvasFinancialChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasFinancialChart.md")
                .AddVariant("financialcharts", Tr("Candlestick Chart"))
                .AddVariant("financialcharts", Tr("OHLC Bar Chart"))
                .AddVariant("financialcharts", Tr("Heikin-Ashi Chart"))
                .AddVariant("financialcharts", Tr("Volume Analysis"))
                .AddVariant("financialcharts", Tr("Multi-Market View"));

        chartBuilder.AddItem("divergingcharts", Tr("Diverging Bar Charts"), Tr("Likert scale and population pyramid charts"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDivergingChartExamples(); },
                             "DemoApp/UltraCanvasDivergingChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasDivergingChartExamples.md")
                .AddVariant("divergingcharts", Tr("Likert Scale"))
                .AddVariant("divergingcharts", Tr("Population Pyramid"))
                .AddVariant("divergingcharts", Tr("Tornado Chart"));

        chartBuilder.AddItem("waterfallcharts", Tr("Waterfall Charts"), Tr("Cumulative flow visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateWaterfallChartExamples(); },
                             "DemoApp/UltraCanvasWatefallChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasWatefallChartExamples.md")
                .AddVariant("waterfallcharts", Tr("Revenue Flow"))
                .AddVariant("waterfallcharts", Tr("Cash Flow with Subtotals"))
                .AddVariant("waterfallcharts", Tr("Performance Impact"));

        chartBuilder.AddItem("mekkocharts", Tr("Mekko Chart"), Tr("Marimekko / mosaic chart with variable-width stacked columns"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateMekkoChartExamples(); },
                             "DemoApp/UltraCanvasMekkoChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasMekkoChart.md")
                .AddVariant("mekkocharts", Tr("Market Share by Sector"))
                .AddVariant("mekkocharts", Tr("Product vs Region Sales Mix"))
                .AddVariant("mekkocharts", Tr("Bar-Mekko Absolute Mode"));

        chartBuilder.AddItem("nestedareacharts", Tr("Nested Chart"), Tr("Nested proportional area comparison chart"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateNestedAreaChartExamples(); },
                             "DemoApp/UltraCanvasNestedAreaChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasNestedAreaChartExamples.md")
                .AddVariant("nestedareacharts", Tr("Rectangle Mode"))
                .AddVariant("nestedareacharts", Tr("Circle Mode"))
                .AddVariant("nestedareacharts", Tr("Theme Showcase"));

        chartBuilder.AddItem("populationcharts", Tr("Population Chart"), Tr("Population chart data visualization"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePopulationChartExamples(); },
                             "DemoApp/UltraCanvasPopulationChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPopulationChartExamples.md");

        chartBuilder.AddItem("sunburstcharts", Tr("Sunburst Chart"),
                             Tr("Hierarchical radial partition charts with drill-down zoom, "
                             "depth shading, partial sweeps and auto-fitting labels"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSunburstChartExamples(); },
                             "DemoApp/UltraCanvasSunburstChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSunburstChartExamples.md")
                .AddVariant("sunburstcharts", Tr("Three-Level Sales Hierarchy"))
                .AddVariant("sunburstcharts", Tr("Demographic Sunburst"))
                .AddVariant("sunburstcharts", Tr("Partial-Sweep Fan"))
                .AddVariant("sunburstcharts", Tr("Depth-Limited Overview"));

        chartBuilder.AddItem("chordcharts", Tr("Chord Chart"),
                             Tr("Directed flows between categories: throughput-sized arcs, "
                             "proportional bezier ribbons, hover isolation and flow tooltips"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateChordChartExamples(); },
                             "DemoApp/UltraCanvasChordChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasChordChartExamples.md")
                .AddVariant("chordcharts", Tr("Global Merchandise Trade"))
                .AddVariant("chordcharts", Tr("Asymmetric Migration Corridors"))
                .AddVariant("chordcharts", Tr("Project Phase Handovers"))
                .AddVariant("chordcharts", Tr("Cross-Team Handovers"));

        chartBuilder.AddItem("radialbarcharts", Tr("Radial Bar Chart"),
                             Tr("Value-driven rays around a circle: bar and line styles, "
                             "cap shapes, gradient fade, ring guides and peak labels"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateRadialBarChartExamples(); },
                             "DemoApp/UltraCanvasRadialBarChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasRadialBarChartExamples.md")
                .AddVariant("radialbarcharts", Tr("Multi-Sensor Radial Bars"))
                .AddVariant("radialbarcharts", Tr("Geographic Radial Lines"))
                .AddVariant("radialbarcharts", Tr("Partial-Sweep Fan"))
                .AddVariant("radialbarcharts", Tr("Arrow Rays / Manual Range"));

        chartBuilder.AddItem("circularprogresscharts", Tr("Circular Progress Chart"),
                             Tr("Angle-encoded progress: concentric rings, single ring and "
                             "progress pie with tracks, tip callouts, centre disc and legend"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateCircularProgressChartExamples(); },
                             "DemoApp/UltraCanvasCircularProgressChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasCircularProgressChart.md")
                .AddVariant("circularprogresscharts", Tr("Circular Infographic Rings"))
                .AddVariant("circularprogresscharts", Tr("Stacked Start Labels"))
                .AddVariant("circularprogresscharts", Tr("Completion Ring & Progress Pie"))
                .AddVariant("circularprogresscharts", Tr("Donut KPI Pie"));

        chartBuilder.AddItem("ganttcharts", Tr("Gantt Chart"),
                             Tr("Project schedules with task table, hierarchy, dependencies, "
                             "milestones, progress, critical path and design presets"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateGanttChartExamples(); },
                             "DemoApp/UltraCanvasGanttChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasGanttChart.md")
                .AddVariant("ganttcharts", Tr("Design Studio (all options)"))
                .AddVariant("ganttcharts", Tr("Modern Design"))
                .AddVariant("ganttcharts", Tr("Professional / Critical Path"))
                .AddVariant("ganttcharts", Tr("Classic Print Style"))
                .AddVariant("ganttcharts", Tr("Soft Pastel Style"))
                .AddVariant("ganttcharts", Tr("Minimal Project Table"))
                .AddVariant("ganttcharts", Tr("Dark Theme"))
                .AddVariant("ganttcharts", Tr("Palette Gallery"));

        chartBuilder.AddItem("timelinechart", Tr("Timeline Chart"),
                             Tr("Chronological timeline: milestones and spans to scale, packed or in swimlanes"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTimelineChartExamples(); },
                             "DemoApp/UltraCanvasTimelineChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTimelineChart.md")
                .AddVariant("timelinechart", Tr("Modern"))
                .AddVariant("timelinechart", Tr("Classic"))
                .AddVariant("timelinechart", Tr("Minimal"))
                .AddVariant("timelinechart", Tr("Roadmap"))
                .AddVariant("timelinechart", Tr("Dark"))
                .AddVariant("timelinechart", Tr("Swimlanes"));

        chartBuilder.AddItem("polarcharts", Tr("Polar Chart"),
                             Tr("Angle/radius plots: scatter, line, spline, area and "
                             "column series with categorical or numeric angle axes, "
                             "tolerance bands, stacking and dual angle axes"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePolarChartExamples(); },
                             "DemoApp/UltraCanvasPolarChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPolarChart.md")
                .AddVariant("polarcharts", Tr("Polar Scatter"))
                .AddVariant("polarcharts", Tr("Dual Angle Axis Curves"))
                .AddVariant("polarcharts", Tr("Overlapping Polar Areas"))
                .AddVariant("polarcharts", Tr("Categorical Spline Area"))
                .AddVariant("polarcharts", Tr("Radial Tolerance Bands"))
                .AddVariant("polarcharts", Tr("Stacked Polar Columns"));

        chartBuilder.AddItem("heatmapchart", Tr("Heat map"), Tr("Heatmap, spectrogram, calendar & hexbin variants"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateHeatmapExamples(); },
                             "DemoApp/UltraCanvasHeatmapExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasHeatmapChart.md")
                .AddVariant("heatmapchart", Tr("Interactive Heatmap"))
                .AddVariant("heatmapchart", Tr("Spectrogram (STFT)"))
                .AddVariant("heatmapchart", Tr("Calendar Heatmap"))
                .AddVariant("heatmapchart", Tr("Hexbin Density"))
                .AddVariant("heatmapchart", Tr("Job Gains & Losses"));

        chartBuilder.AddItem("contourchart", Tr("Contour Chart"),
                             Tr("Isolines, filled contour bands, kernel-density contours of a "
                             "point cloud, inline labelled curves, zoom/pan with crosshair "
                             "read-out, and 3D contour surfaces (software painter's algorithm "
                             "and the UC OpenGL element) with isolines traced onto the terrain"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateContourChartExamples(); },
                             "DemoApp/UltraCanvasContourChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasContourChart.md")
                .AddVariant("contourchart", Tr("Interactive Contour"))
                .AddVariant("contourchart", Tr("Filled + Labels"))
                .AddVariant("contourchart", Tr("Density (KDE)"))
                .AddVariant("contourchart", Tr("Line Contour"))
                .AddVariant("contourchart", Tr("Dark Density"))
                .AddVariant("contourchart", Tr("3D Bands (software)"))
                .AddVariant("contourchart", Tr("3D Terrain (software)"))
                .AddVariant("contourchart", Tr("3D Surface (UC OpenGL)"));

        chartBuilder.AddItem("jitterchart", Tr("Jitter chart"), Tr("Jitter chart"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateJitterPlotExamples(); },
                             "DemoApp/UltraCanvasJitterPlotExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasJitterPlotExamples.md");

#ifndef __EMSCRIPTEN__
        chartBuilder.AddItem("waveform", Tr("Waveform Chart"),
                             Tr("Audio amplitude min/max envelope with RMS overlay, a "
                             "click-to-seek playhead, and a selectable display range"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateWaveformExamples(); },
                             "DemoApp/UltraCanvasWaveformExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasWaveformElement.md")
                .AddVariant("waveform", Tr("Filled / Outline / Bars"))
                .AddVariant("waveform", Tr("RMS Overlay"))
                .AddVariant("waveform", Tr("Display Range"))
                .AddVariant("waveform", Tr("Click-to-seek Playhead"));
#endif

#ifndef __EMSCRIPTEN__
        chartBuilder.AddItem("spectrogram", Tr("Spectrogram Chart"),
                             Tr("STFT time x frequency analysis of audio signals with every "
                             "transform and display option adjustable live"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSpectrogramExamples(); },
                             "DemoApp/UltraCanvasSpectrogramExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSpectrogram.md")
                .AddVariant("spectrogram", Tr("Sample Track & Synthetic Signals"))
                .AddVariant("spectrogram", Tr("FFT Size & Overlap"))
                .AddVariant("spectrogram", Tr("Window Functions"))
                .AddVariant("spectrogram", Tr("Magnitude & Dynamic Range"))
                .AddVariant("spectrogram", Tr("Frequency Cap & Colour Maps"));
#endif

#ifndef __EMSCRIPTEN__
        chartBuilder.AddItem("audioanalysis", Tr("Audio Analysis Charts"),
                             Tr("Spectrum plot, amplitude envelope and correlogram built with "
                             "the generic area / bar chart elements"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAudioAnalysisExamples(); },
                             "DemoApp/UltraCanvasAudioAnalysisExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAudioAnalysisExamples.md")
                .AddVariant("audioanalysis", Tr("Spectrum Plot (Averaged FFT)"))
                .AddVariant("audioanalysis", Tr("Amplitude Envelope (RMS)"))
                .AddVariant("audioanalysis", Tr("Correlogram (Autocorrelation)"));
#endif

        chartBuilder.AddItem("dumbbell", Tr("Dumbbell Chart"),
                             Tr("Paired value comparison with connected dots - gender gaps, "
                             "before/after studies and min/max ranges"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDumbbellChartExamples(); },
                             "DemoApp/UltraCanvasDumbbellChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasDumbbellChart.md")
                .AddVariant("dumbbell", Tr("Social Media by Age & Gender"))
                .AddVariant("dumbbell", Tr("Before / After"))
                .AddVariant("dumbbell", Tr("Min / Max Range"))
                .AddVariant("dumbbell", Tr("Gap Analysis & Sorting"));

        chartBuilder.AddItem("funnel", Tr("Funnel Chart"),
                             Tr("Staged process funnels - segmented bars with conversion necks, "
                             "solid cones, card rows, presentation infographics and horizontal "
                             "pipelines"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateFunnelChartExamples(); },
                             "DemoApp/UltraCanvasFunnelChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasFunnelChart.md")
                .AddVariant("funnel", Tr("Segmented Funnel"))
                .AddVariant("funnel", Tr("Continuous Cone"))
                .AddVariant("funnel", Tr("Card Rows"))
                .AddVariant("funnel", Tr("Presentation Infographic"))
                .AddVariant("funnel", Tr("Horizontal Funnel"))
                .AddVariant("funnel", Tr("Playground"));

        chartBuilder.AddItem("bubblecharts", Tr("Bubble Chart"),
                             Tr("Scatter bubbles, packed bubbles, bubble matrix and an "
                             "OpenGL 3D bubble chart"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBubbleChartExamples(); },
                             "DemoApp/UltraCanvasBubbleChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBubbleChart.md")
                .AddVariant("bubblecharts", Tr("Bubble Matrix (Mrs. President)"))
                .AddVariant("bubblecharts", Tr("Scatter Bubbles (Travel Concerns)"))
                .AddVariant("bubblecharts", Tr("Packed Bubbles"))
                .AddVariant("bubblecharts", Tr("Hierarchical Packing"))
                .AddVariant("bubblecharts", Tr("Bubble Timeline"))
                .AddVariant("bubblecharts", Tr("3D Bubbles (OpenGL)"));

        chartBuilder.AddItem("importanceperformance", Tr("Importance-Performance Chart"),
                             Tr("Slack's importance-performance matrix: service factors over "
                             "urgent action / improve / appropriate / excess zones"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateImportancePerformanceChartExamples(); },
                             "DemoApp/UltraCanvasImportancePerformanceChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasImportancePerformanceChart.md")
                .AddVariant("importanceperformance", Tr("Slack Zones (smooth)"))
                .AddVariant("importanceperformance", Tr("Slack Zones (stepped)"));

        chartBuilder.AddItem("ternarychart", Tr("Ternary Chart"),
                             Tr("Three-part compositions in a triangle: vote shares, soil texture, "
                             "alloys - every point's three shares add up to 100%"),
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreateTernaryChartExamples(); },
                             "DemoApp/UltraCanvasTernaryChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTernaryChart.md")
                .AddVariant("ternarychart", Tr("Three-Party Votes (anticlockwise)"))
                .AddVariant("ternarychart", Tr("Vote Swings (paths)"))
                .AddVariant("ternarychart", Tr("USDA Soil Texture (regions)"))
                .AddVariant("ternarychart", Tr("De Finetti (bubbles, Hardy-Weinberg curve)"))
                .AddVariant("ternarychart", Tr("Density (2,400 points, contours, zoom)"));

        // ===== DIAGRAMS =====
        auto diagramBuilder = DemoCategoryBuilder(this, DemoCategory::Diagrams);

        diagramBuilder.AddItem(
                        "sankey",
                        Tr("Sankey Flow"),
                        Tr("Interactive flow diagrams showing relationships and value distributions"),
                        ImplementationStatus::FullyImplemented,
                        [this]() { return CreateSankeyExamples(); },
                        "DemoApp/UltraCanvasSankeyExamples.cpp",
                        "Docs/UltraCanvas/UltraCanvasSankeyExamples.md"
                )
                .AddVariant("sankey", Tr("Energy Flow"))
                .AddVariant("sankey", Tr("Financial Flow"))
                .AddVariant("sankey", Tr("Web Traffic"))
                .AddVariant("sankey", Tr("Custom Data"))
                .AddVariant("sankey", Tr("Performance Test"));

//        diagramBuilder.AddItem("plantuml", "PlantUML", "UML and diagram generation",
//                               ImplementationStatus::NotImplemented,
//                               [this]() { return CreateDiagramExamples(); })
//                .AddVariant("plantuml", "Class Diagrams")
//                .AddVariant("plantuml", "Sequence Diagrams")
//                .AddVariant("plantuml", "Activity Diagrams");

        diagramBuilder.AddItem("flowchart", Tr("Flow chart"), Tr("Interactive flowchart with decision points"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateFlowChartExamples(); },
                               "DemoApp/UltraCanvasFlowChartExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasFlowChartExamples.md");

        diagramBuilder.AddItem("pertchart", Tr("PERT chart"), Tr("Project network with critical path analysis, node designs and palettes"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreatePertChartExamples(); },
                               "DemoApp/UltraCanvasPertChartExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasPertChartExamples.md");

        diagramBuilder.AddItem("venndiagram", Tr("Venn Diagram"), Tr("Interactive Venn diagram for set visualization"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateVennDiagramExamples(); },
                               "DemoApp/UltraCanvasVennDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasVennDiagramExamples.md");

        diagramBuilder.AddItem("parliamentdiagram", Tr("Parliament Diagram"),
                               Tr("Legislature seat chart: hemicycle, horseshoe, circle, Westminster "
                               "benches and grid layouts with majority marker, legend and coalition "
                               "building"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateParliamentDiagramExamples(); },
                               "DemoApp/UltraCanvasParliamentDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasParliamentDiagram.md")
                .AddVariant("parliamentdiagram", Tr("Bundestag"))
                .AddVariant("parliamentdiagram", Tr("European Parliament"))
                .AddVariant("parliamentdiagram", Tr("House of Commons"))
                .AddVariant("parliamentdiagram", Tr("Coalition Builder"));

        diagramBuilder.AddItem("packetdiagram", Tr("Packet Diagram"),
                               Tr("Bit-accurate protocol header and frame structure: RFC word grid "
                               "with fields wrapping across rows, proportional byte strips, "
                               "layer brackets, bit and byte rulers, word gutters, dimension "
                               "rails, stacked flag bits, sub-fields, and a Mermaid-compatible "
                               "text spec"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreatePacketDiagramExamples(); },
                               "DemoApp/UltraCanvasPacketDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasPacketDiagram.md")
                .AddVariant("packetdiagram", Tr("TCP/IP Stack"))
                .AddVariant("packetdiagram", Tr("Rails + Payload"))
                .AddVariant("packetdiagram", Tr("RFC Monochrome"))
                .AddVariant("packetdiagram", Tr("Proportional Strip"))
                .AddVariant("packetdiagram", Tr("Byte Columns + Values"))
                .AddVariant("packetdiagram", Tr("Text Spec (Mermaid)"));

        diagramBuilder.AddItem("wordcloud", Tr("Word Cloud"), Tr("Frequency word cloud with shapes, masks and center image"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateWordCloudExamples(); },
                               "DemoApp/UltraCanvasWordCloudExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasWordCloudExamples.md");

        diagramBuilder.AddItem("dendrogram", Tr("Dendrogram"), Tr("Interactive dendrogram / phylogenetic tree visualization"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateDendrogramExamples(); },
                               "DemoApp/UltraCanvasDendrogramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasDendrogramExamples.md");

        diagramBuilder.AddItem("treemap", Tr("TreeMap"), Tr("Hierarchical TreeMap data visualization"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateTreeMapExamples(); },
                               "DemoApp/UltraCanvasTreeMapExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasTreeMapExamples.md");

    diagramBuilder.AddItem("blockdiagram", Tr("Block Diagram"), Tr("Interactive block diagram with shapes and connections"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateBlockDiagramExamples(); },
                           "DemoApp/UltraCanvasBlockDiagramExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasBlockDiagramExamples.md");

    diagramBuilder.AddItem("nodediagram", Tr("Node Diagram"), Tr("Network/graph visualization with nodes and edges"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateNodeDiagramExamples(); },
                           "DemoApp/UltraCanvasNodeDiagramExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasNodeDiagramExamples.md");

    diagramBuilder.AddItem("gourcetree", Tr("Force-Directed Tree"), Tr("Radial tree visualization for file systems inspired by Gource"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateGourceTreeExamples(); },
                           "DemoApp/UltraCanvasGourceTreeExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasGourceTreeExamples.md");
    diagramBuilder.AddItem("gitgraph", Tr("Git Graph"), Tr("Commit graph: git-flow swimlanes, branch operations and the repository lane view"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateGitGraphExamples(); },
                           "DemoApp/UltraCanvasGitGraphExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasGitGraphExamples.md");
    diagramBuilder.AddItem("adjacencydiagrams", Tr("Adjacency Diagram"), Tr("Architectural Adjacency Diagram"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateAdjacencyDiagramExamples(); },
                               "DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasAdjacencyDiagramExamples.md");
    diagramBuilder.AddItem("arcdiagrams", Tr("Arc Diagram"), Tr("Arc Diagram"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateArcDiagramExamples(); },
                               "DemoApp/UltraCanvasArcDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasArcDiagramExamples.md");
    diagramBuilder.AddItem("compositor", Tr("Compositor Diagram"), Tr("Node-based compositor with subgraph, visual scripting, and feature playground"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateCompositorDiagramExamples(); },
                               "DemoApp/UltraCanvasCompositorDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasCompositorDiagramExamples.md")
                .AddVariant("compositor", Tr("Image Compositor"))
                .AddVariant("compositor", Tr("Subgraph / Groups"))
                .AddVariant("compositor", Tr("Visual Scripting"))
                .AddVariant("compositor", Tr("Feature Playground"))
                .AddVariant("compositor", Tr("Logic Diagram"))
                .AddVariant("compositor", Tr("Marketing Funnel"));
    diagramBuilder.AddItem("quadrantcharts", Tr("Quadrant Chart"),
                               Tr("Strategic 2x2 matrices: SWOT, BCG, Eisenhower, risk, priority and custom quadrants"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateQuadrantChartExamples(); },
                               "DemoApp/UltraCanvasQuadrantChartExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasQuadrantChartExamples.md")
                .AddVariant("quadrantcharts", Tr("SWOT Analysis"))
                .AddVariant("quadrantcharts", Tr("BCG Matrix"))
                .AddVariant("quadrantcharts", Tr("Eisenhower Matrix"))
                .AddVariant("quadrantcharts", Tr("Risk Matrix"))
                .AddVariant("quadrantcharts", Tr("Priority Matrix"))
                .AddVariant("quadrantcharts", Tr("Custom Quadrants"));
    diagramBuilder.AddItem("mindmap", Tr("Mind Map"),
                               Tr("Rooted topic tree with balanced, logic, org chart and radial structures, editing and collapse"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMindMapExamples(); },
                               "DemoApp/UltraCanvasMindMapExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasMindMapExamples.md")
                .AddVariant("mindmap", Tr("Editable map"))
                .AddVariant("mindmap", Tr("Logic chart"))
                .AddVariant("mindmap", Tr("Radial"))
                .AddVariant("mindmap", Tr("Relationships"))
                .AddVariant("mindmap", Tr("Presentation"))
                .AddVariant("mindmap", Tr("Themes & structures"));
    diagramBuilder.AddItem("erdiagram", Tr("ER Diagram"), Tr("Entity-Relationship Diagram"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateERDiagramExamples(); },
                               "DemoApp/UltraCanvasERDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasERDiagram.md")
                .AddVariant("erdiagram", Tr("Internet Sales (Chen + crow's foot)"))
                .AddVariant("erdiagram", Tr("Dense Schema (attribute haloes)"))
                .AddVariant("erdiagram", Tr("Student / Course (annotated)"))
                .AddVariant("erdiagram", Tr("Hospital (pastel, spine layout)"))
                .AddVariant("erdiagram", Tr("Academic (min,max + ternary)"))
                .AddVariant("erdiagram", Tr("Crow's Foot (physical schema)"))
                .AddVariant("erdiagram", Tr("Relational (min,max + arrowheads)"))
                .AddVariant("erdiagram", Tr("IDEF1X (key compartment, FK1/FK2)"));
    diagramBuilder.AddItem("sequencediagram", Tr("Sequence Diagram"),
                               Tr("UML sequence diagram: lifelines, execution bars, seven message "
                               "forms, combined fragments and PlantUML/Mermaid export"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateSequenceDiagramExamples(); },
                               "DemoApp/UltraCanvasSequenceDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasSequenceDiagram.md")
                .AddVariant("sequencediagram", Tr("Card Game"))
                .AddVariant("sequencediagram", Tr("User Login"))
                .AddVariant("sequencediagram", Tr("Authentication"))
                .AddVariant("sequencediagram", Tr("Online Shop (loop + alt)"))
                .AddVariant("sequencediagram", Tr("Lifecycle (create/destroy)"))
                .AddVariant("sequencediagram", Tr("UltraCanvas Event Flow"));
    
      diagramBuilder.AddItem("classdiagram", Tr("Class Diagram"),
                               Tr("UML class diagram: compartment boxes, seven relationship kinds, "
                               "automatic layout and C++ reverse engineering"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateClassDiagramExamples(); },
                               "DemoApp/UltraCanvasClassDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasClassDiagramExamples.md")
                .AddVariant("classdiagram", Tr("Banking System"))
                .AddVariant("classdiagram", Tr("Interfaces"))
                .AddVariant("classdiagram", Tr("Service Metadata"))
                .AddVariant("classdiagram", Tr("Domain Model"))
                .AddVariant("classdiagram", Tr("Reverse Engineered"));
      
      diagramBuilder.AddItem("requirementdiagram", Tr("Requirement Diagram"),
                               Tr("SysML requirement diagram: compartmented requirements, containment trees and traceability relationships"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateRequirementDiagramExamples(); },
                               "DemoApp/UltraCanvasRequirementDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasRequirementDiagramExamples.md")
                .AddVariant("requirementdiagram", Tr("HybridSUV (compartments)"))
                .AddVariant("requirementdiagram", Tr("Specification + callout"))
                .AddVariant("requirementdiagram", Tr("SysML taxonomy + legend"))
                .AddVariant("requirementdiagram", Tr("Smart Home traceability"))
                .AddVariant("requirementdiagram", Tr("Coverage + compartments"))
                .AddVariant("requirementdiagram", Tr("Mermaid import + editing"))
                .AddVariant("requirementdiagram", Tr("ReqIF + overlays"));

    diagramBuilder.AddItem("matrixdiagram", Tr("Matrix Diagram"),
                           Tr("Relationships between two to four ordered lists, marked from a "
                           "named scale and rolled up into weighted row and column totals"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateMatrixDiagramExamples(); },
                           "DemoApp/UltraCanvasMatrixDiagramExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasMatrixDiagram.md")
                .AddVariant("matrixdiagram", Tr("L-Shaped"))
                .AddVariant("matrixdiagram", Tr("T-Shaped"))
                .AddVariant("matrixdiagram", Tr("Weighted (QFD)"))
                .AddVariant("matrixdiagram", Tr("X-Shaped"))
                .AddVariant("matrixdiagram", Tr("Y-Shaped"))
                .AddVariant("matrixdiagram", Tr("House of Quality"))
                .AddVariant("matrixdiagram", Tr("C-Shaped (Cube)"));

        diagramBuilder.AddItem("kanbanboards", Tr("Kanban Board"),
                               Tr("Workflow boards with WIP limits, swimlanes, drag & drop, "
                               "a built-in card editor, Mermaid text definitions and "
                               "design presets"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateKanbanBoardExamples(); },
                               "DemoApp/UltraCanvasKanbanBoardExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasKanbanBoard.md")
                .AddVariant("kanbanboards", Tr("Interactive Editor"))
                .AddVariant("kanbanboards", Tr("Sticky Notes"))
                .AddVariant("kanbanboards", Tr("Poster"))
                .AddVariant("kanbanboards", Tr("Schematic"))
                .AddVariant("kanbanboards", Tr("Timeline / Swimlanes"))
                .AddVariant("kanbanboards", Tr("Dark Theme"))
                .AddVariant("kanbanboards", Tr("Text Definition"))
                .AddVariant("kanbanboards", Tr("Cumulative Flow"));

        diagramBuilder.AddItem("pyramid", Tr("Pyramid Diagram"),
                               Tr("Level hierarchies - extruded 3D tiers, colour ribbon and icon "
                               "callouts, outlined and flat presentation layouts, and "
                               "area-proportional data pyramids with a reference shape"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreatePyramidChartExamples(); },
                               "DemoApp/UltraCanvasPyramidChartExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasPyramidChart.md")
                .AddVariant("pyramid", Tr("3D Ribbon Pyramid"))
                .AddVariant("pyramid", Tr("Spectrum 3D"))
                .AddVariant("pyramid", Tr("Outline Hierarchy"))
                .AddVariant("pyramid", Tr("Flat & Minimal"))
                .AddVariant("pyramid", Tr("Alternating Infographic"))
                .AddVariant("pyramid", Tr("Data Pyramid"))
                .AddVariant("pyramid", Tr("Playground"));

    diagramBuilder.AddItem("tickerboard", Tr("Ticker Board"),
                           Tr("Labels, values and changes crawling across a band: set in a font, "
                           "or lit on a 5x7 LED dot grid that the text steps across"),
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateTickerBoardExamples(); },
                           "DemoApp/UltraCanvasTickerBoardExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasTickerBoard.md")
                .AddVariant("tickerboard", Tr("Market Quotes"))
                .AddVariant("tickerboard", Tr("LED Sign"))
                .AddVariant("tickerboard", Tr("Live Dot-Matrix Quotes"))
                .AddVariant("tickerboard", Tr("Headline Crawl"));

//        diagramBuilder.AddItem("mermaid", "Mermaid", "Mermaid",
//                               ImplementationStatus::NotImplemented,
//                               [this]() { return nullptr; });



        // ===== INFO GRAPHICS =====
        auto infoBuilder = DemoCategoryBuilder(this, DemoCategory::InfoGraphics);

//        infoBuilder.AddItem("infographics", "Info Graphics", "Complex data visualizations",
//                            ImplementationStatus::NotImplemented,
//                            [this]() { return CreateInfoGraphicsExamples(); })
//                .AddVariant("infographics", "Dashboard Widgets")
//                .AddVariant("infographics", "Statistical Displays")
//                .AddVariant("infographics", "Interactive Maps");

        infoBuilder.AddItem("swotdiagram", Tr("SWOT Diagram"),
                            Tr("Classic four-panel SWOT analysis infographic with six design presets"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreateSWOTDiagramExamples(); },
                            "DemoApp/UltraCanvasSWOTDiagramExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasSWOTDiagramExamples.md")
                .AddVariant("swotdiagram", Tr("Corner Badges"))
                .AddVariant("swotdiagram", Tr("Classic Matrix"))
                .AddVariant("swotdiagram", Tr("Cards"))
                .AddVariant("swotdiagram", Tr("Center Diamond"))
                .AddVariant("swotdiagram", Tr("Rows"))
                .AddVariant("swotdiagram", Tr("Columns"));

        infoBuilder.AddItem("fishbonediagram", Tr("Fishbone Diagram"),
                            Tr("Ishikawa cause-and-effect analysis with eight design presets, "
                            "category checklists and Mermaid ishikawa-beta interchange"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreateFishboneDiagramExamples(); },
                            "DemoApp/UltraCanvasFishboneDiagramExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasFishboneDiagram.md")
                .AddVariant("fishbonediagram", Tr("Spine Chips"))
                .AddVariant("fishbonediagram", Tr("Classic"))
                .AddVariant("fishbonediagram", Tr("Bracket"))
                .AddVariant("fishbonediagram", Tr("Crossed Ribs"))
                .AddVariant("fishbonediagram", Tr("Chevron Spine"))
                .AddVariant("fishbonediagram", Tr("Columns"))
                .AddVariant("fishbonediagram", Tr("Vertical"))
                .AddVariant("fishbonediagram", Tr("Compact"));

        infoBuilder.AddItem("timelinediagram", Tr("Timeline Diagram"),
                            Tr("Narrative timeline infographic with nine design presets"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreateTimelineDiagramExamples(); },
                            "DemoApp/UltraCanvasTimelineDiagramExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasTimelineDiagram.md")
                .AddVariant("timelinediagram", Tr("Bar"))
                .AddVariant("timelinediagram", Tr("Line"))
                .AddVariant("timelinediagram", Tr("Alternating"))
                .AddVariant("timelinediagram", Tr("Cards"))
                .AddVariant("timelinediagram", Tr("Vertical"))
                .AddVariant("timelinediagram", Tr("Serpentine"))
                .AddVariant("timelinediagram", Tr("Hanging"))
                .AddVariant("timelinediagram", Tr("Chevron"))
                .AddVariant("timelinediagram", Tr("Steps"));

        // ("heatmap" placeholder removed: the heat map is implemented and
        // registered in the Charts section as "heatmapchart" — interactive
        // heatmap, STFT spectrogram, calendar and hexbin variants.)

        infoBuilder.AddItem("circularinfo", Tr("Circular info graphic"),
                            Tr("Concentric rings of styled cells with value tracks, decorative "
                            "rings, group outlines and cross-ring connections"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreateCircularInfoGraphicExamples(); },
                            "DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasCircularInfoGraphic.md")
                .AddVariant("circularinfo", Tr("Corporate Hierarchy"))
                .AddVariant("circularinfo", Tr("Technology Ecosystem (270°)"))
                .AddVariant("circularinfo", Tr("Multi-Track Genomic Style"))
                .AddVariant("circularinfo", Tr("Decorative Mandala"));

        infoBuilder.AddItem("circlediagram", Tr("Circle diagram"),
                            Tr("Hub-and-spoke nodes threaded onto a backbone ring, each with a "
                            "fan of satellites on leader lines"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreateCircleDiagramExamples(); },
                            "DemoApp/UltraCanvasCircleDiagramExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasCircleDiagram.md")
                .AddVariant("circlediagram", Tr("Satellite Wheel"))
                .AddVariant("circlediagram", Tr("Banded Wheel"))
                .AddVariant("circlediagram", Tr("Node Count Sweep"))
                .AddVariant("circlediagram", Tr("Outside Labels"));

        // Nothing is built for this one: no element, no plugin, no renderer —
        // the demo page is the placeholder text itself. "Planned", like the
        // specification-only modules below: there is nothing partial to show.
        infoBuilder.AddItem("wavesinfo", Tr("Waves info graphic"), Tr("Waves info graphic"),
                            ImplementationStatus::Planned,
                            [this]() { return CreatePartiallyImplementedExamples(Tr("Waves info graphic is not ready yet")); });

        // ("matrix" placeholder removed: the matrix diagram is implemented and
        // registered in the Diagrams section as "matrixdiagram".)

        infoBuilder.AddItem("performancematrix", Tr("Performance Matrix"),
                            Tr("9-box, performance vs potential, performance management and talent "
                            "review matrices: named categories where two rated axes cross"),
                            ImplementationStatus::FullyImplemented,
                            [this]() { return CreatePerformanceMatrixExamples(); },
                            "DemoApp/UltraCanvasPerformanceMatrixExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md")
                .AddVariant("performancematrix", Tr("Calibration (move people, open and save files)"))
                .AddVariant("performancematrix", Tr("9-Box (Grid)"))
                .AddVariant("performancematrix", Tr("Performance vs Potential (Tiles)"))
                .AddVariant("performancematrix", Tr("Performance Management (Header Tabs)"))
                .AddVariant("performancematrix", Tr("Talent Review (Cards)"))
                .AddVariant("performancematrix", Tr("Skill / Will"));


        // ===== 3D ELEMENTS =====
        auto graphics3DBuilder = DemoCategoryBuilder(this, DemoCategory::Graphics3D);

#ifdef ULTRACANVAS_ENABLE_GL
        graphics3DBuilder.AddItem("glsurface", Tr("OpenGL 3D support"), Tr("3D models, shader playground, and a Zarch-style simulation"),
                                  ImplementationStatus::FullyImplemented,
                                  [this]() { return CreateGLSurfaceExamples(); },
                                  "DemoApp/UltraCanvasGLSurfaceExamples.cpp",
                                  "Docs/UltraCanvas/UltraCanvasGLSurfaceExamples.md");
#endif

        // Outside the GL guard: UltraCanvasSTLElement falls back to a mesh summary
        // without GL, so the loader and its statistics stay demonstrable there.
        // Not in the browser: the only sample is a 25 MB STL, left out of the page's bundle.
#ifndef __EMSCRIPTEN__
        graphics3DBuilder.AddItem("stlmodels", Tr("STL 3D Models"), Tr("Stereolithography meshes (ASCII and binary) read by the self-contained STL loader and shaded in an orbiting OpenGL view"),
                                  ImplementationStatus::FullyImplemented,
                                  [this]() { return CreateSTLModelExamples(); },
                                  "DemoApp/UltraCanvasSTLExamples.cpp",
                                  "Docs/UltraCanvas/UltraCanvasSTLElement.md");
#endif

#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN
        // The rest of the 3D matrix, one entry per format the way STL has its
        // own: a visitor looking for "does it read FBX" finds a row called FBX
        // rather than a single "3D Model Formats" page they have to open and
        // page through. Each opens the same page filtered to that reader.
        //
        // Also outside the GL guard, and for the same reason as STL:
        // UltraCanvasSTLElement draws a shaded software still without GL, so
        // the readers stay demonstrable in a build that has no OpenGL at all.
        struct ModelFormatEntry {
            const char* id;
            const char* extension;
            std::string displayName;    // translated
            std::string description;    // translated
        };
        // Ordered as the samples are - most compact binary format first.
        // Not static: the names are read through Tr in the language in use.
        const ModelFormatEntry kModelFormats[] = {
                {"stepmodels", "step", Tr("STEP 3D Models"),
                 Tr("ISO 10303 exact B-rep: analytic surfaces closed into solids, tessellated on import because the file carries no mesh")},
                {"ms3dmodels", "ms3d", Tr("MilkShape 3D Models"),
                 Tr("A game format of packed little-endian structs - groups become meshes and smoothing groups arrive as a bitmask")},
                {"fbxmodels", "fbx", Tr("FBX 3D Models"),
                 Tr("Autodesk FBX, binary and ASCII, with what the header declared kept in ModelDocument::Metadata")},
                {"alembicmodels", "abc", Tr("Alembic 3D Models"),
                 Tr("A sampled geometry cache rather than a scene description - this reader takes the first time sample")},
                {"colladamodels", "dae", Tr("COLLADA 3D Models"),
                 Tr("XML interchange with a real scene graph and materials; compile-time optional, since it needs tinyxml2")},
                {"3dsmodels", "3ds", Tr("3D Studio Models"),
                 Tr("Chunked binary from 1990s 3D Studio: sixteen-bit indices and a flat object list, so a mesh per object and no hierarchy")},
                {"xfilemodels", "x", Tr("DirectX .x 3D Models"),
                 Tr("DirectX retained-mode .x - templates declare their own layout, so the parser is driven by the file")},
                {"dxfmodels", "dxf", Tr("DXF 3D Models"),
                 Tr("AutoCAD DXF carrying 3D geometry - 8110 3DFACE entities read as a mesh, not as a drawing")},
        };
        for (const ModelFormatEntry& format : kModelFormats) {
            const std::string extension = format.extension;
            graphics3DBuilder.AddItem(format.id, format.displayName, format.description,
                                      ImplementationStatus::FullyImplemented,
                                      [this, extension]() { return CreateModelFormatsExamples(extension); },
                                      "DemoApp/UltraCanvasModelFormatsExamples.cpp",
                                      "Docs/UltraCanvas/UltraCanvasModelFormats.md");
        }
#endif


        // ===== VIDEO ELEMENTS =====
        auto videoBuilder = DemoCategoryBuilder(this, DemoCategory::VideoElements);

        // Not in the browser: no video backend (ULTRACANVAS_ENABLE_VIDEO is off).
#ifndef __EMSCRIPTEN__
        videoBuilder.AddItem("video", Tr("Video Player"), Tr("Video playback and recording"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateVideoExamples(); },
                             "DemoApp/UltraCanvasVideoExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasVideo.md")
                .AddVariant("video", Tr("MP4 Playback"))
                .AddVariant("video", Tr("Camera Recording"))
                .AddVariant("video", Tr("Custom Controls"));
#endif

        videoBuilder.AddItem("videotimeline", Tr("Video Timeline"),
                             Tr("A media timeline to edit: clips on tracks under a ruler, trim, reorder, "
                             "split, transitions, markers, beats, a playhead; every edit reported, never applied"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateVideoTimelineExamples(); },
                             "DemoApp/UltraCanvasVideoTimelineExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasVideoTimeline.md")
                .AddVariant("videotimeline", Tr("Magnetic main track, free lanes"))
                .AddVariant("videotimeline", Tr("Trim, split, transitions"))
                .AddVariant("videotimeline", Tr("Light and dark palettes"));

        // ===== TEXT DOCUMENTS =====
        auto textDocBuilder = DemoCategoryBuilder(this, DemoCategory::TextDocuments);

        textDocBuilder.AddItem("markdown", Tr("Markdown"), Tr("Markdown document rendering"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"media/docs/MarkdownExample.md")); });

//        textDocBuilder.AddItem("codeeditor", "Code Editor", "Syntax highlighting text editor",
//                               ImplementationStatus::PartiallyImplemented,
//                               [this]() { return CreateCodeEditorExamples(); })
//                .AddVariant("codeeditor", "C++ Syntax")
//                .AddVariant("codeeditor", "Pascal Syntax")
//                .AddVariant("codeeditor", "COBOL Syntax");

        textDocBuilder.AddItem("textdocuments", Tr("Text Documents"),
                               Tr("Plain text and source-code file rendering via UltraCanvasTextArea's syntax renderer"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateTextDocumentExamples(); },
                               "DemoApp/UltraCanvasDemoExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasTextAreaExamples.md");

        // The WYSIWYG element. "Partially implemented" is its own
        // documentation's verdict rather than a guess: see "What is not
        // implemented yet" in Docs/UltraCanvas/UltraCanvasRichTextEdit.md
        // (unbalanced columns, RTL indents, no screen-reader bridge).
        textDocBuilder.AddItem("wysiwyg", Tr("WYSIWYG Editor"),
                               Tr("Formatted documents edited as they look — the caret sits in "
                               "rendered text and bold is a state of the selection, over the same "
                               "UCRichDocument the ODT/DOCX/LaTeX readers produce"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateWYSIWYGExamples(); },
                               "DemoApp/UltraCanvasWYSIWYGExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasRichTextEdit.md")
                .AddVariant("wysiwyg", Tr("Character formatting"))
                .AddVariant("wysiwyg", Tr("Paragraph styles & lists"))
                .AddVariant("wysiwyg", Tr("Tables, rules & pictures"))
                .AddVariant("wysiwyg", Tr("Open & save .odt / .docx"))
                .AddVariant("wysiwyg", Tr("Read-only preview"));

        textDocBuilder.AddItem("wysiwygintl", Tr("WYSIWYG — Chinese, Arabic & Myanmar"),
                               Tr("The WYSIWYG editor on Chinese, Arabic and Myanmar text: Chinese lines "
                               "break between any two characters, Arabic paragraphs run right to left, "
                               "Myanmar syllables are shaped from stacked letters, and one line can mix "
                               "both directions"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateWYSIWYGInternationalExamples(); },
                               "DemoApp/UltraCanvasWYSIWYGExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasRichTextEdit.md");

      textDocBuilder.AddItem("ebook", Tr("eBook Reader"),
                               Tr("EPUB/FB2/MOBI/TXT reading with chapters, TOC, themes and font scaling "
                               "rendered natively through the CSSLayout engine"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateEBookExamples(); },
                               "DemoApp/UltraCanvasEBookExamples.cpp",
                               "UltraCanvas/eBook_README.md")
                .AddVariant("ebook", Tr("EPUB 2 / EPUB 3"))
                .AddVariant("ebook", Tr("FictionBook 2 (FB2)"))
                .AddVariant("ebook", Tr("Kindle (MOBI / AZW)"))
                .AddVariant("ebook", Tr("Plain Text"));

// The page's source is only built with the plugin (CMakeLists.txt): its
// CreateLaTeXView() comes from the on-demand loader the plugin brings.
#ifdef ULTRACANVAS_PLUGIN_LATEX
        textDocBuilder.AddItem("textdocuments_latex", Tr("LaTeX Documents"),
                               Tr("LaTeX documents typeset live from source by the UltraCanvas math "
                               "engine — rendered output and source per document"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateLaTeXExamples(); },
                               "DemoApp/UltraCanvasLaTeXExamples.cpp");
#endif

#ifdef ULTRACANVAS_PLUGIN_PDF
        textDocBuilder.AddItem("textdocuments_pdf", Tr("PDF Documents"),
                               Tr("PDF document viewing, navigation, zoom & search"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreatePDFExamples(); },
                               "DemoApp/UltraCanvasPDFExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasPDFExamples.md");
#else
        textDocBuilder.AddItem("textdocuments_pdf", Tr("PDF Documents"), Tr("PDF document support"),
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });
#endif

        textDocBuilder.AddItem("textdocuments_ods", Tr("ODS Documents"),
                               Tr("OpenDocument Spreadsheet (.ods) viewing & editing: opens the "
                               "bundled media/docs/spreadsheet.ods demo in the Spreadsheet "
                               "widget with live SUM totals, and loads any .ods/.xlsx/.csv "
                               "via a file dialog"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateSpreadsheetExamples(); },
                               "DemoApp/UltraCanvasSpreadsheetExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasSpreadsheetExamples.md");

        textDocBuilder.AddItem("textdocuments_odt", Tr("ODT Documents"),
                               Tr("OpenDocument Text / Word document viewing: loads "
                               "media/docs/document.odt and any .odt/.docx/.doc via a "
                               "file dialog, rendered through UCRichDocument as markdown"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateODTExamples(); },
                               "DemoApp/UltraCanvasODTExamples.cpp",
                               "Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md");


        // ===== AUDIO ELEMENTS =====
        // Not in the browser: no audio backend (ULTRACANVAS_ENABLE_AUDIO is off),
        // so the players and meters would stay silent.
#ifndef __EMSCRIPTEN__
        auto audioBuilder = DemoCategoryBuilder(this, DemoCategory::AudioElements);

        audioBuilder.AddItem("audio", Tr("Audio Player"), Tr("Audio playback and waveform display"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAudioExamples(); },
                             "DemoApp/UltraCanvasAudioExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAudio.md")
                .AddVariant("audio", Tr("FLAC Support"))
                .AddVariant("audio", Tr("MP3 Playback"))
                .AddVariant("audio", Tr("Waveform Visualization"));

        audioBuilder.AddItem("levelmeter", Tr("Level Meter / VU Strip"),
                             Tr("Three-zone VU meter and scrolling waveform strip with themeable "
                             "colours, driven by playback, synthetic or manual levels"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLevelMeterExamples(); },
                             "DemoApp/UltraCanvasLevelMeterExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLevelMeter.md")
                .AddVariant("levelmeter", Tr("Three-zone VU Meter"))
                .AddVariant("levelmeter", Tr("Scrolling Waveform Strip"))
                .AddVariant("levelmeter", Tr("Custom Colour Themes"))
                .AddVariant("levelmeter", Tr("Live Playback Levels"))
                .AddVariant("levelmeter", Tr("Manual Control & Reset"));
#endif

        auto toolsBuilder = DemoCategoryBuilder(this, DemoCategory::Tools);

        toolsBuilder.AddItem("qrcode", Tr("QR code"), Tr("QR code generator and decoder"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateQRCodeExamples(); },
                             "DemoApp/UltraCanvasQRCodeExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasQRCodeExamples.md");

        toolsBuilder.AddItem("barcode", Tr("Bar code"),
                               Tr("1D barcode widget — 12 symbologies"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateBarcodeExamples(); },
                               "DemoApp/UltraCanvasBarcodeExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasBarcodeElement.md")
                .AddVariant("barcode", Tr("Code 39"))
                .AddVariant("barcode", Tr("Code 93"))
                .AddVariant("barcode", Tr("Code 128 / GS1-128"))
                .AddVariant("barcode", Tr("EAN-13 / EAN-8"))
                .AddVariant("barcode", Tr("UPC-A / UPC-E"))
                .AddVariant("barcode", Tr("ISBN-13"))
                .AddVariant("barcode", Tr("ITF / ITF-14"))
                .AddVariant("barcode", Tr("Standard 2 of 5"))
                .AddVariant("barcode", Tr("Codabar"))
                .AddVariant("barcode", Tr("MSI Plessey"))
                .AddVariant("barcode", Tr("Pharmacode"));

#ifdef ULTRACANVAS_HAS_OCR_PLUGIN
        toolsBuilder.AddItem("ocr", Tr("OCR"), Tr("Optical Character Recognition"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateOCRExamples(); },
                             "DemoApp/UltraCanvasOCRExamples.cpp",
                             "Docs/Modules/OCR/README.md");
#else
        toolsBuilder.AddItem("ocr", Tr("OCR"), Tr("Optical Character Recognition"),
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/OCR/README.md")); });
#endif

#ifdef ULTRACANVAS_HAS_VECTORIZER_PLUGIN
        toolsBuilder.AddItem("vectorizer", Tr("Vectorizer"), Tr("Raster image to SVG vector tracer"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateVectorizerExamples(); },
                             "DemoApp/UltraCanvasVectorizerExamples.cpp",
                             "Docs/Modules/Vectorizer/README.md");
#else
        toolsBuilder.AddItem("vectorizer", Tr("Vectorizer"), Tr("Raster image to SVG vector tracer"),
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/Vectorizer/README.md")); });
#endif

        toolsBuilder.AddItem("textrenderingsettings", Tr("Text Rendering"),
                             Tr("Configure text antialiasing, hinting style, and hint metrics"),
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextRenderingSettingsExamples(); },
                             "DemoApp/UltraCanvasTextRenderingExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTextRenderingExamples.md");

        auto modulesBuilder = DemoCategoryBuilder(this, DemoCategory::Modules);
        // Not in the browser: no audio backend (ULTRACANVAS_ENABLE_AUDIO is off).
#ifndef __EMSCRIPTEN__
        modulesBuilder.AddItem("audiofx", Tr("Audio FX"), Tr("Audio FX"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateAudioFXExamples(); },
                               "DemoApp/UltraCanvasAudioFXExamples.cpp",
                               "Docs/Modules/AudioFX/README.md");
#endif
        modulesBuilder.AddItem("fileloader", Tr("File Loader"), Tr("File Loader"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateFileLoaderExamples(); },
                               "DemoApp/UltraCanvasFileLoaderExamples.cpp",
                               "Docs/Modules/FileLoader/README.md");
        // The text of every application in the person's language: Tr, TrC,
        // TrN and TrF live in a picked language, the catalogue files of the
        // three layers, the scripts around them. The editor over the sets is
        // Widgets > Localisation Editor.
        modulesBuilder.AddItem("localisation", Tr("Localisation"),
                               Tr("UltraLocalisation - Tr, TrC, TrN and TrF in a picked language, with the "
                               "layer that answered each; the catalogue files of the three layers"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateLocalisationExamples(); },
                               "DemoApp/UltraCanvasLocalisationExamples.cpp",
                               "Docs/Modules/UltraLocalisation/README.md");
        // Built into the core (core/IODeviceManager + OS backends): printers on
        // Linux, macOS and Windows plus IPP everywhere, scanners on Linux plus
        // eSCL, webcams on Linux; the other categories and platform backends are
        // still planned (Docs/Modules/IODeviceManager/Gaps.md) - partial. The
        // page is the DeviceExplorer application's own panel, compiled in from
        // Apps/DeviceExplorer/ui.
#ifdef ULTRACANVAS_DEMO_HAS_DEVICEEXPLORER
        modulesBuilder.AddItem("iodevicemanager", Tr("IODeviceManager support"),
                               Tr("IODeviceManager - the printers, scanners and cameras on this "
                               "computer, shown by the DeviceExplorer application's panel"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateIODeviceManagerExamples(); },
                               "DemoApp/UltraCanvasIODeviceManagerExamples.cpp",
                               "Docs/Modules/IODeviceManager/README.md");
#else
        modulesBuilder.AddItem("iodevicemanager", Tr("IODeviceManager support"), Tr("IODeviceManager support"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/IODeviceManager"); });
#endif
#ifdef HAS_LIBVIPS
        modulesBuilder.AddItem("pixelfx", Tr("Pixel FX"), Tr("Pixel FX"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreatePixelFXExamples(); },
                               "DemoApp/UltraCanvasPixelFXExamples.cpp",
                               "Docs/Modules/PixelFX/README.md");
#else
        modulesBuilder.AddItem("pixelfx", Tr("Pixel FX"), Tr("Pixel FX"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/PixelFX"); });
#endif
        // The engine, the facade, the store, the automation engine, the rooms,
        // the sixteen widgets (SmartHome/ui) and the SmartHome Center app
        // (0.1.1: dashboard, rooms, network map) run on the simulated demo
        // home; the radio backends (KNX and Z-Wave build, Matter, Thread and
        // Zigbee wait for their SDKs) have met no hardware yet
        // (SmartHome/STATUS.md) - partial still. With the widgets in the build
        // the entry opens their panel on that home.
#ifdef ULTRACANVAS_DEMO_HAS_SMARTHOME
        modulesBuilder.AddItem("smarthome", Tr("Smart Home module"), Tr("UltraCanvas Smart Home Module"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateSmartHomeExamples(); },
                               "DemoApp/UltraCanvasSmartHomeExamples.cpp",
                               "Docs/Modules/Smarthome/README.md");
#else
        modulesBuilder.AddItem("smarthome", Tr("Smart Home module"), Tr("UltraCanvas Smart Home Module"),
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/Smarthome"); });
#endif
        // The dialogs every application asks the system for: Open / Save /
        // Select folder (UltraCanvasFileLoader), Print (RequestPrintSettings +
        // IODeviceManager) and the message / input dialogs, each shown either
        // as the ULTRA OS dialog or the host platform's.
        modulesBuilder.AddItem("systemdialogs", Tr("System dialogs"),
                               Tr("ULTRA OS system dialogs — File Open, File Save, Select Folder, "
                               "Print, messages and input, as ULTRA OS or native dialogs"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateSystemDialogsExamples(); },
                               "DemoApp/UltraCanvasSystemDialogsExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasSystemDialogs.md")
                .AddVariant("systemdialogs", Tr("File Open / Open multiple"))
                .AddVariant("systemdialogs", Tr("File Save"))
                .AddVariant("systemdialogs", Tr("Select folder"))
                .AddVariant("systemdialogs", Tr("Print settings / test page"))
                .AddVariant("systemdialogs", Tr("Messages and input"));
        // Built module (UltraAI/, targets UltraAI + UltraAI_Core) shipped in an
        // application: Apps/UltraAIApp is written entirely against <UltraAI.h>.
        modulesBuilder.AddItem("ultraai", Tr("Ultra AI"), Tr("Ultra AI Module"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraAI"); });
        // Built module (UltraCloud/, targets UltraCloud + UltraCloudUI) used by
        // UltraMail: account setup and cloud-link attachments go through
        // <UltraCloud/UltraCloudService.h> and UltraCloudPickerDialog.
        modulesBuilder.AddItem("ultracloud", Tr("Ultra Cloud"),
                               Tr("Ultra Cloud Module — cloud accounts, uploads and share links "
                               "(Nextcloud/ownCloud, WebDAV, Dropbox, OneDrive, Google Drive)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraCloud"); });
        // Built module (UltraCanvas/{include,core}/UltraCrypt, target UltraCrypt)
        // used by UltraAuthenticator (secret store, OTP) and AnchorPoint
        // (transfer protocol), and underneath UltraVault.
        modulesBuilder.AddItem("ultracrypt", Tr("Ultra Crypt"),
                               Tr("Ultra Crypt Module — hashing, HMAC, AEAD, key derivation "
                               "and secure random for every ULTRA OS module"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraCrypt"); });
        // Built module (target UltraDatabase) backing three applications:
        // UltraMail, EmailCleaner and UltraSocial all persist through
        // <UltraDatabase/UltraDatabase.h>.
        modulesBuilder.AddItem("ultradatabase", Tr("Ultra Database"),
                               Tr("Ultra Database Module — named connections, parameterized "
                               "queries, transactions and migrations over SQLite (Stage 1)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraDatabase"); });
        // Built module (target UltraMessage): the ULTRA OS message channel.
        // Its page is the Message Centre element (target UltraMessageCenter)
        // on a private broker, since that element is the module's view.
        modulesBuilder.AddItem("ultramessage", Tr("Ultra Message"),
                               Tr("Ultra Message Module — the message channel and its Message Centre: "
                               "chats, mail and notifications with sources, filters, search and actions"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMessageCenterExamples(); },
                               "DemoApp/UltraCanvasMessageCenterExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasMessageCenter.md")
                .AddVariant("ultramessage", Tr("All / Chats / Mail / System"))
                .AddVariant("ultramessage", Tr("Sources tree and service chips"))
                .AddVariant("ultramessage", Tr("Detail pane with actions"))
                .AddVariant("ultramessage", Tr("Live: post a sample message"));
        // Built module (target UltraNet) used by UltraSocial (HTTP, OAuth2,
        // sockets), UltraMail, AnchorPoint and this demo's own live loader.
        modulesBuilder.AddItem("ultranet", Tr("Ultra Net"),
                               Tr("Ultra Net Module — overview, documentation and a live "
                               "remote-resource loader (UltraCanvasFileLoader::LoadFile + "
                               "UltraNet_HttpGet + UCImageRaster::LoadFromMemory)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateNetworkingExamples(); },
                               "DemoApp/UltraCanvasNetworkingExamples.cpp",
                               "Docs/Modules/UltraNet/README.md");
        // Built module (target UltraVault) used by UltraAIApp for provider API
        // keys and by UltraMail for account credentials.
        modulesBuilder.AddItem("ultravault", Tr("Ultra Vault"),
                               Tr("Ultra Vault Module — the system-level home for API keys, "
                               "tokens and passphrases (memory and encrypted-file backends)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraVault"); });
        // Built module (target UltraWin) with three consumers: the
        // UltraWinManager application, the ultrawin-setup tool and
        // UltraFiler's "Run with Windows" action.
        modulesBuilder.AddItem("ultrawin", Tr("Ultra Win"),
                               Tr("Ultra Win Module — Windows applications on Linux / ULTRA OS "
                               "as single native windows (Wine tier)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/UltraWin"); });
        // Built module (VideoFX/, target VideoFX, on FFmpeg) with every stage
        // of its roadmap done but keyframed parameters: probe, frames,
        // waveforms, the segment timeline with effects, speed, transitions and
        // overlays (picture-in-picture), photos with pan and zoom keeping faces
        // in shot, slideshows, music with ducking, beats, voice-overs, project
        // files, preview, proxies, hardware encoders, the timeline element and
        // the videofx CLI. The UltraVideo editor (Apps/UltraVideo) is built on
        // it. Docs/Modules/VideoFX/README.md, *Roadmap*, is the record.
        modulesBuilder.AddItem("videofx", Tr("VideoFX"), Tr("VideoFX Module"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/VideoFX"); });
        // Built module (VirtualFS/, target VirtualFS) reaching applications through
        // the core: UltraCanvasFilerWidget browses archive interiors via
        // UltraCanvasVirtualFSBridge (UltraFiler), and UltraCanvasFileLoader
        // decompresses through VirtualFS_DecompressBuffer.
        modulesBuilder.AddItem("virtualfs", Tr("VirtualFS"), Tr("VirtualFS Module"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateModuleDocScreen("Docs/Modules/VirtualFS"); });
//        modulesBuilder.AddItem("gpio", "GPIO support", "GPIO support",
//                             ImplementationStatus::PartiallyImplemented,
//                             [this]() { return CreatePartiallyImplementedExamples(Tr("## GPIO support")); });

        // ===== DEPENDENCIES & THIRD PARTY =====
        // Single entry rendering the full dependency / third-party library table for
        // the UltraCanvas core and every additional ULTRA OS module, built with the
        // UltraCanvasListView multi-column element.
        auto dependenciesBuilder = DemoCategoryBuilder(this, DemoCategory::Dependencies);
        dependenciesBuilder.AddItem("dependencies", Tr("Dependencies &amp; Third Party"),
                               Tr("Third-party libraries used by UltraCanvas and its modules"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateDependenciesExamples(); },
                               "DemoApp/UltraCanvasDependenciesExamples.cpp",
                               "Docs/Dependencies.md");

        auto widgetsBuilder = DemoCategoryBuilder(this, DemoCategory::Widgets);

        // UltraCanvasVectorCanvas is a widget an application drops into a
        // window, not a file format, so it lives here rather than under
        // Vector Graphics (which shows what the vector *readers* produce).
        widgetsBuilder.AddItem("vectorcanvas", Tr("Vector Editing"),
                               Tr("Edit a vector drawing on an UltraCanvasVectorCanvas: select, move, "
                               "scale, rotate, draw, undo, group, gradients"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateVectorCanvasExamples(); },
                               "DemoApp/UltraCanvasVectorCanvasExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasVectorCanvas.md");

        widgetsBuilder.AddItem("menuconfig", Tr("Menus and Toolbars"),
                               Tr("One editor for a program's menus and toolbars: command registry, groups, live Apply"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMenuConfigExamples(); },
                               "DemoApp/UltraCanvasMenuConfigExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasCommandConfigWidget.md");

        widgetsBuilder.AddItem("localisationeditor", Tr("Localisation Editor"),
                               Tr("Every set an application's texts come from in one grid: edit, take a base set, promote, demote, save"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateLocalisationEditorExamples(); },
                               "DemoApp/UltraCanvasLocalisationEditorExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasLocalisationEditor.md");

        widgetsBuilder.AddItem("colorpicker", Tr("Colour Picker"),
                               Tr("HSV colour wheel with saturation/value square, preview "
                               "swatches, hex input, HSV/HSL/RGB channel sliders and alpha; "
                               "hue x lightness field and colour + intensity slider styles"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateColorPickerExamples(); },
                               "DemoApp/UltraCanvasColorPickerExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasColorPicker.md")
                .AddVariant("colorpicker", Tr("Full Picker (Wheel + Sliders)"))
                .AddVariant("colorpicker", Tr("HSV / HSL / RGB Modes"));

        // The picker's companion: the palette rows UltraPaint and ArtCreator
        // show under it.
        widgetsBuilder.AddItem("colorpalettes", Tr("Colour Palettes"),
                               Tr("Palette rows under a colour picker: a gallery of colour schemes "
                               "(up to 256-colour GIF palettes) behind +, - to remove a row, and "
                               "a right click that edits one entry"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateColorPaletteManagerExamples(); },
                               "DemoApp/UltraCanvasColorPaletteManagerExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasColorPaletteManager.md")
                .AddVariant("colorpalettes", Tr("Under a Colour Picker"))
                .AddVariant("colorpalettes", Tr("Several Palettes"))
                .AddVariant("colorpalettes", Tr("Indexed (GIF) Palettes"));

        widgetsBuilder.AddItem("datepicker", Tr("Date Picker / Calendar"),
                               Tr("Calendar widgets covering single, range, multiple and week selection"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateDatePickerExamples(); },
                               "DemoApp/UltraCanvasDatePickerExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasDatePicker.md")
                .AddVariant("datepicker", Tr("Dropdown Calendar"))
                .AddVariant("datepicker", Tr("Inline Calendar"))
                .AddVariant("datepicker", Tr("Date Range"))
                .AddVariant("datepicker", Tr("Multiple Dates"))
                .AddVariant("datepicker", Tr("Keyboard / Text Entry"))
                .AddVariant("datepicker", Tr("Hotel Stay / Blocked Dates"))
                .AddVariant("datepicker", Tr("Multi-Month / Scroll"));

        widgetsBuilder.AddItem("timepicker", Tr("Time Picker"),
                               Tr("Time-of-day field with hour/minute/second spinners, 12/24h and AM/PM"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateTimePickerExamples(); },
                               "DemoApp/UltraCanvasTimePickerExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasTimePicker.md")
                .AddVariant("timepicker", Tr("24-Hour"))
                .AddVariant("timepicker", Tr("12-Hour AM/PM"))
                .AddVariant("timepicker", Tr("With Seconds"))
                .AddVariant("timepicker", Tr("Minute Step / Constraints"))
                .AddVariant("timepicker", Tr("Clock Dial (24h / 12h AM-PM)"));

        widgetsBuilder.AddItem("mediaviewer", Tr("Media Viewer"),
                               Tr("Comprehensive media viewer for images, documents (PDF), audio and "
                               "video: folder browsing, next/previous (arrows or mouse, even while "
                               "zoomed), slideshow with transitions, manual + automatic zoom, "
                               "rotation, mirroring, gamma / brightness / colour correction, "
                               "auto-optimise, sharpening, save-as and a detailed info popup. "
                               "PDFs render via UltraCanvasPDFView and audio/video via the player "
                               "elements. Drag a folder or files onto it."),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMediaViewerExamples(); },
                               "DemoApp/UltraCanvasMediaViewerExamples.cpp");

        widgetsBuilder.AddItem("slideshow", Tr("Slideshow"),
                               Tr("Timed image slideshow with info text panel and selectable indicator styles"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateSlideshowExamples(); },
                               "DemoApp/UltraCanvasSlideshowExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasSlideshowExamples.md")
                .AddVariant("slideshow", Tr("Bars Indicator"))
                .AddVariant("slideshow", Tr("Dots Indicator"))
                .AddVariant("slideshow", Tr("Progress Bar"))
                .AddVariant("slideshow", Tr("Story Bars"))
                .AddVariant("slideshow", Tr("Counter"))
                .AddVariant("slideshow", Tr("Thumbnails"))
                .AddVariant("slideshow", Tr("Labels"));

        widgetsBuilder.AddItem("album", Tr("Album"),
                               Tr("Photo / video / music album with selectable layout designs, "
                               "crop / zoom / stretch fitting, action icons and visitor / edit / admin modes"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateAlbumExamples(); },
                               "DemoApp/UltraCanvasAlbumExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasAlbumExamples.md")
                .AddVariant("album", Tr("Uniform Grid"))
                .AddVariant("album", Tr("Justified"))
                .AddVariant("album", Tr("Masonry"))
                .AddVariant("album", Tr("Mosaic"))
                .AddVariant("album", Tr("Filmstrip"))
                .AddVariant("album", Tr("Cards"));

        widgetsBuilder.AddItem("filer", Tr("Filer"),
                               Tr("Folder-content widget with details / list / thumbnail / "
                               "bar-size / treemap view types, sorting by name / size / type / "
                               "dates, hover icon menu and a full file context menu "
                               "(Copy, Cut, Delete, Duplicate, Rename, New, Display, Open with, "
                               "Compress, Extract, Print, Extras, Settings)"),
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateFilerExamples(); },
                               "DemoApp/UltraCanvasFilerExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasFilerWidget.md")
                .AddVariant("filer", Tr("Details"))
                .AddVariant("filer", Tr("List"))
                .AddVariant("filer", Tr("Thumbnails (S/M/B/XL)"))
                .AddVariant("filer", Tr("Bar size"))
                .AddVariant("filer", Tr("Treemap"))
                .AddVariant("filer", Tr("Force-directed tree (planned)"))
                .AddVariant("filer", Tr("3D (planned)"));

        debugOutput << "✓ Registered " << demoItems.size() << " demo items across "
                  << categoryItems.size() << " categories" << std::endl;
    }

    void UltraCanvasDemoApplication::SetupTreeView() {
        // Setup root node
        TreeNodeData rootData("root", Tr("UltraCanvas Components"));
        rootData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/ultracanvas.png"), 16, 16);
        TreeNode* rootNode = categoryTreeView->SetRootNode(rootData);

        // Add category nodes
        std::vector<std::pair<DemoCategory, std::string>> categoryNames = {
                {DemoCategory::BasicUI, Tr("Basic UI Elements")},
                {DemoCategory::ExtendedFunctionality, Tr("Complex UI Elements")},
                {DemoCategory::BitmapElements, Tr("Bitmap Elements")},
                {DemoCategory::VectorElements, Tr("Vector Graphics")},
                {DemoCategory::Charts, Tr("Charts")},
                {DemoCategory::Diagrams, Tr("Diagrams")},
                {DemoCategory::InfoGraphics, Tr("Info Graphics")},
                {DemoCategory::Graphics3D, Tr("3D Graphics")},
                {DemoCategory::AudioElements, Tr("Audio Elements")},
                {DemoCategory::VideoElements, Tr("Video Elements")},
                {DemoCategory::TextDocuments, Tr("Document support")},
                {DemoCategory::Tools, Tr("Tools")},
                {DemoCategory::Widgets, Tr("Widgets")},
                {DemoCategory::Modules, Tr("ULTRA OS modules")},
                {DemoCategory::Dependencies, Tr("Dependencies &amp; Third Party")}
        };


        TreeNode* modulesNode = nullptr;
        for (const auto& [category, catName] : categoryNames) {
            // A category with no pages in this build (the browser build
            // leaves the audio pages out) gets no node: a childless folder
            // in the tree is a page that cannot be opened.
            const auto itemsIt = categoryItems.find(category);
            if (itemsIt == categoryItems.end() || itemsIt->second.empty()) {
                continue;
            }
            const auto& items = itemsIt->second;
            TreeNodeData categoryData(
                    "cat_" + std::to_string(static_cast<int>(category)),
                    catName
            );
            categoryData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder.png"), 16, 16);
            TreeNode* categoryNode = categoryTreeView->AddNode("root", categoryData);
            if (category == DemoCategory::Modules) {
                modulesNode = categoryNode;
            }

            // Add items for this category
            for (const std::string& itemId : items) {
                const auto& demoItem = demoItems[itemId];
                TreeNodeData itemData(itemId, demoItem->displayName);
                if (demoItem->id == "imageperformance") {
                    itemData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/clock-five.svg"), 16, 16);
                } else if (demoItem->id == "chartengine") {
                    // The chart engine page is a configuration console for the
                    // engine's services rather than a single component demo.
                    itemData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.svg"), 16, 16);
                } else {
                    itemData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/document.svg"), 16, 16);
                }
                itemData.rightIcon = TreeNodeIcon(GetStatusIcon(demoItem->status), 12, 12);
                categoryTreeView->AddNode(categoryData.nodeId, itemData);
            }
            if (category == DemoCategory::Charts || category == DemoCategory::Diagrams ||
                category == DemoCategory::InfoGraphics || category == DemoCategory::Widgets) {
                categoryNode->SortChildNodes();
            }
        }

        // Expand root node
        rootNode->Expand();
        rootNode->FirstChild()->Expand();
        rootNode->FirstChild()->FirstChild()->Expand();
        // The "ULTRA OS modules" node carries its own overview page, so disable the
        // tree's "jump to first entry" behaviour for it: clicking the label keeps the
        // selection on the node itself (showing the ULTRA OS overview) instead of
        // jumping to the first module. Expanded by default for convenience.
        if (modulesNode) {
            modulesNode->data.showFirstChildOnExpand = false;
            modulesNode->Expand();
        }
        categoryTreeView->SelectNode(rootNode->FirstChild()->FirstChild());
        OnTreeNodeSelected(rootNode->FirstChild()->FirstChild());
    }

// ===== EVENT HANDLERS =====
    void UltraCanvasDemoApplication::OnTreeNodeSelected(TreeNode* node) {
        if (!node || !node->data.nodeId.length()) return;

        std::string nodeId = node->data.nodeId;

        const std::string modulesNodeId =
                "cat_" + std::to_string(static_cast<int>(DemoCategory::Modules));

        // Check if this is a demo item (not a category)
        if (demoItems.find(nodeId) != demoItems.end()) {
            DisplayDemoItem(nodeId);
            UpdateHeaderDisplay(nodeId);
        } else if (nodeId == modulesNodeId) {
            // "ULTRA OS modules" category: show the ULTRA OS overview page instead of
            // clearing the display / jumping to the first module example.
            ClearDisplay();
            currentDisplayElement = CreateUltraOSInfoScreen();
            if (currentDisplayElement) {
                displayContainer->AddChild(currentDisplayElement);
                currentDisplayElement->layoutItem
                    .SetFlexGrow(1)
                    .SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            }
            headerContainer->SetDemoTitle("ULTRA OS");
            headerContainer->SetSourceFile("");
            headerContainer->SetDocFile("");
        } else if (node->IsExpanded() && node->data.showFirstChildOnExpand && node->FirstShownChild()) {
            // Parent category with no display content of its own: "jump to first
            // entry" so selecting it reveals its first child example instead of
            // blanking the display. The tree already jumps when a node is first
            // expanded; this also covers re-selecting an already-open parent.
            // Only "ULTRA OS modules" carries its own content (handled above) and
            // opts out via showFirstChildOnExpand = false. Under a filter the
            // first child may be hidden, so it is the first one on show.
            categoryTreeView->SelectNode(node->FirstShownChild());
        } else {
            // Category selected - clear display
            ClearDisplay();
            headerContainer->SetDemoTitle(TrF("Category: {0}", node->data.text));
            headerContainer->SetSourceFile("");
            headerContainer->SetDocFile("");        }
    }

    void UltraCanvasDemoApplication::OnTreeFilterChanged(const std::string& text) {
        if (!categoryTreeView) return;
        categoryTreeView->SetFilterText(Trim(text));
    }

    bool UltraCanvasDemoApplication::SelectDemoItem(const std::string& itemId) {
        if (demoItems.find(itemId) == demoItems.end()) return false;

        // DisplayDemoItem alone only swaps the page: the tree would keep the
        // startup selection and the header would still name it, so
        // `--component listview` opened the ListView page under the title
        // "Various menu types and styles". Take the same path a click takes.
        TreeNode* node = categoryTreeView ? categoryTreeView->FindNode(itemId) : nullptr;
        if (node) {
            for (TreeNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
                categoryTreeView->ExpandNode(ancestor);
            }
            // SelectNode() fires onNodeSelected, which is OnTreeNodeSelected:
            // calling both built every --component page twice, the first copy
            // thrown away at once - the Ultra Message page then showed its
            // seeded messages twice. Select silently, then display once.
            auto onSelected = std::move(categoryTreeView->onNodeSelected);
            categoryTreeView->onNodeSelected = nullptr;
            categoryTreeView->SelectNode(node);
            categoryTreeView->onNodeSelected = std::move(onSelected);
            OnTreeNodeSelected(node);
        } else {
            DisplayDemoItem(itemId);
            UpdateHeaderDisplay(itemId);
        }
        return true;
    }

    void UltraCanvasDemoApplication::DisplayDemoItem(const std::string& itemId) {
        ClearDisplay();

        auto it = demoItems.find(itemId);
        if (it == demoItems.end()) return;

        const auto& item = it->second;

        if (item->createExample && item->status != ImplementationStatus::NotImplemented) {
            try {
                currentDisplayElement = item->createExample();
                if (currentDisplayElement) {
                    displayContainer->AddChild(currentDisplayElement);
                    // Grow to fill. A page built at a fixed pixel height (the
                    // usual `(id, 0, 0, 1000, 1400)` example root) keeps that
                    // height, so it overflows displayContainer, which scrolls it.
                    // Shrinking it to the window would clamp the page instead,
                    // and a page that places its children absolutely does not
                    // scroll itself, so its lower rows were unreachable. A page
                    // with no fixed height is fitted to the window and scrolls
                    // its own content (the album, the editors).
                    // Likewise a fixed pixel width is kept rather than stretched
                    // to the window: stretched, it fills the viewport exactly,
                    // and the vertical scrollbar then narrows the viewport by its
                    // track and fabricates a horizontal overflow of that much.
                    // A page built with the page kit says how it wants to sit:
                    // as tall as its content and scrolled by the area (the
                    // default), or filling the area and scrolling inside.
                    auto* kitPage = dynamic_cast<DemoKit::DemoPage*>(currentDisplayElement.get());
                    const bool fixedHeight = kitPage
                        ? !kitPage->FillsArea()
                        : currentDisplayElement->size.height.unit == CSSLayout::DimensionUnit::Pixels;
                    const bool fixedWidth =
                        currentDisplayElement->size.width.unit == CSSLayout::DimensionUnit::Pixels;
                    currentDisplayElement->layoutItem
                        .SetFlexGrow(1)
                        .SetFlexShrink(fixedHeight ? 0 : 1)
                        .SetAlignSelf(fixedWidth ? CSSLayout::AlignSelf::Start
                                                 : CSSLayout::AlignSelf::Stretch);
                    // Kept means kept: the column measures the page "at most"
                    // as wide as the area, which cuts a fixed width down to it,
                    // so the page never overflowed and the horizontal scrollbar
                    // never showed - the right-hand drawings were simply cut
                    // off. As its min-width the page's width outlasts that, and
                    // displayContainer scrolls it sideways when it is wider.
                    // A kit page is stretched to the area, but neither it
                    // nor a card in it narrower than its fixed-size content.
                    if (kitPage) DemoKit::ApplyContentMinWidth(*kitPage);
                    if (fixedWidth) {
                        CSSLayout::BoxConstraints limits =
                            currentDisplayElement->boxConstraints.value_or(CSSLayout::BoxConstraints{});
                        limits.minWidth = currentDisplayElement->size.width;
                        currentDisplayElement->boxConstraints = limits;
                    }
                }
                currentSelectedId = itemId;
            } catch (const std::exception& e) {
                debugOutput << "Error creating example for " << itemId << ": " << e.what() << std::endl;
            }
        } else {
            // Show placeholder for not implemented items
            auto placeholder = std::make_shared<UltraCanvasLabel>("placeholder", 20, 20, 600, 200);
            placeholder->SetText(Tr("This component is not yet implemented.\nPlanned for future release."));
            placeholder->SetAlignment(TextAlignment::Center);
            placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
            placeholder->SetBorders(2.0f);
            placeholder->SetBordersColor(Color(200, 200, 0, 255));
            placeholder->SetPadding(10);
            displayContainer->AddChild(placeholder);
            currentDisplayElement = placeholder;
            currentSelectedId = itemId;
        }
    }

    void UltraCanvasDemoApplication::ClearDisplay() {
        if (currentDisplayElement) {
            displayContainer->RemoveChild(currentDisplayElement);
            currentDisplayElement = nullptr;
        }
        currentSelectedId = "";
    }

    void UltraCanvasDemoApplication::UpdateHeaderDisplay(const std::string& itemId) {
        auto it = demoItems.find(itemId);
        if (it != demoItems.end()) {
            const auto& item = it->second;

            // Set title from description field
            headerContainer->SetDemoTitle(item->description);

            // Set source and doc files
            headerContainer->SetSourceFile(item->demoSource);
            headerContainer->SetDocFile(item->demoDoc);
        }
    }

// ===== UTILITY METHODS =====
    std::string UltraCanvasDemoApplication::GetStatusIcon(ImplementationStatus status) const {
        switch (status) {
            case ImplementationStatus::FullyImplemented: return NormalizePath(GetResourcesDir() + "media/icons/check.png");
            case ImplementationStatus::PartiallyImplemented: return NormalizePath(GetResourcesDir() + "media/icons/warning-blue.png");
            case ImplementationStatus::NotImplemented: return NormalizePath(GetResourcesDir() + "media/icons/x.png");
            case ImplementationStatus::Planned: return NormalizePath(GetResourcesDir() + "media/icons/info.png");
            default: return NormalizePath(GetResourcesDir() + "media/icons/unknown.png");
        }
    }

// ===== APPLICATION LIFECYCLE =====
    void UltraCanvasDemoApplication::Run(bool showInfoWindow) {
        // Run application main loop
        debugOutput << "Running UltraCanvas Demo Application..." << std::endl;
        debugOutput << "Select items from the tree view to see implementation examples." << std::endl;

        if (mainWindow) {
#ifdef __EMSCRIPTEN__
            // The page is the screen: fill the browser viewport.
            mainWindow->Maximize();
#endif
            mainWindow->Show();
            // The application will handle the event loop
        }

        // Show the info window at startup, unless the command line asked for a
        // page or for no About window - it is modal and would cover the page
        if (showInfoWindow) {
            ShowInfoWindow();
        }

        auto app = UltraCanvasApplication::GetInstance();
        app->Run();
    }

    void UltraCanvasDemoApplication::Shutdown() {
        debugOutput << "Shutting down Demo Application..." << std::endl;

        ClearDisplay();
        demoItems.clear();
        categoryItems.clear();
    }

// ===== DEMO ITEM REGISTRATION =====
    void UltraCanvasDemoApplication::RegisterDemoItem(std::unique_ptr<DemoItem> item) {
        if (!item) return;

        std::string itemId = item->id;
        DemoCategory category = item->category;

        // Add to items registry
        demoItems[itemId] = std::move(item);

        // Add to category index
        categoryItems[category].push_back(itemId);
    }

// ===== DEMO CATEGORY BUILDER IMPLEMENTATION =====
    DemoCategoryBuilder& DemoCategoryBuilder::AddItem(const std::string& id, const std::string& name,
                                                      const std::string& description, ImplementationStatus status,
                                                      std::function<std::shared_ptr<UltraCanvasUIElement>()> creator,
                                                      const std::string& sourceFile,
                                                      const std::string& docFile) {
        auto item = std::make_unique<DemoItem>(id, name, description, category, status);
        item->createExample = creator;
        item->demoSource = sourceFile;
        item->demoDoc = docFile;
        app->RegisterDemoItem(std::move(item));
        return *this;
    }

    DemoCategoryBuilder& DemoCategoryBuilder::AddVariant(const std::string& itemId, const std::string& variant) {
        auto it = app->demoItems.find(itemId);
        if (it != app->demoItems.end()) {
            it->second->variants.push_back(variant);
        }
        return *this;
    }

// ===== FACTORY FUNCTION =====
    std::unique_ptr<UltraCanvasDemoApplication> CreateDemoApplication() {
        return std::make_unique<UltraCanvasDemoApplication>();
    }

} // namespace UltraCanvas
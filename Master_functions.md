# UltraCanvas Complete Master Function Registry

## **VERSION CONTROL**
```cpp
// Version: 3.2.0
// Last Modified: 2025-08-25
// Author: UltraCanvas Framework
// Purpose: Complete registry of ALL existing functions to prevent duplicates
```

---

## **CORE RENDERING FUNCTIONS** 
*Source: UltraCanvasRenderContext.h*
```cpp
// **Path functions**
void ClearPath();
void ClosePath();
void MoveTo(float x, float y);
void RelMoveTo(float x, float y);
void LineTo(float x, float y);
void RelLineTo(float x, float y);
void QuadraticCurveTo(float cpx, float cpy, float x, float y);
void BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
void RelBezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
void Arc(float cx, float cy, float radius, float startAngle, float endAngle);
void ArcTo(float x1, float y1, float x2, float y2, float radius);
void Circle(float x, float y, float radius);
void Ellipse(float cx, float cy, float rx, float ry, float rotation, float startAngle, float endAngle);
void Rect(float x, float y, float width, float height);
void RoundedRect(float x, float y, float width, float height, float radius);

void FillPath();
void StrokePathPreserve();
void GetPathExtents(float &x, float &y, float &width, float &height);

// text path drawing
virtual void FillText(const std::string& text, float x, float y) = 0;
virtual void StrokeText(const std::string& text, float x, float y) = 0;


// **Drawing**
// lines
void DrawLine(float x1, float y1, float x2, float y2);
void DrawLine(float x1, float y1, float x2, float y2, const Color& col);
void DrawLine(const Point2Df& start, const Point2Df& end);
void DrawLine(const Point2Di& start, const Point2Di& end);
void DrawLine(const Point2Df& start, const Point2Df& end, const Color& col);
void DrawLine(const Point2Di& start, const Point2Di& end, const Color& col);
// Basic rectangles
void DrawRectangle(float x, float y, float w, float h);
void DrawRectangle(int x, int y, int w, int h);
void DrawRectangle(const Rect2Df& rect);
void DrawRectangle(const Rect2Di& rect);
// Filled rectangles
void FillRectangle(float x, float y, float w, float h);
void FillRectangle(int x, int y, int w, int h);
void FillRectangle(const Rect2Df& rect);
void FillRectangle(const Rect2Di& rect);
// Rounded rectangles
void DrawRoundedRectangle(float x, float y, float w, float h, float radius);
void DrawRoundedRectangle(int x, int y, int w, int h, int radius);
void DrawRoundedRectangle(const Rect2Df& rect, float radius);
void DrawRoundedRectangle(const Rect2Di& rect, float radius);
// Filled rectangles
void FillRoundedRectangle(float x, float y, float w, float h, float radius);
void FillRoundedRectangle(int x, int y, int w, int h, int radius);
void FillRoundedRectangle(const Rect2Df& rect, float radius);
void FillRoundedRectangle(const Rect2Di& rect, float radius);
// circle
void DrawCircle(float x, float y, float radius);
void DrawCircle(int x, int y, float radius);
void DrawCircle(const Point2Df& center, float radius);
void DrawCircle(const Point2Di& center, float radius);

void FillCircle(float x, float y, float radius);
void FillCircle(int x, int y, int radius);
void FillCircle(const Point2Df& center, float radius);
void FillCircle(const Point2Di& center, float radius);
// ellipse
void DrawEllipse(float x, float y, float w, float h);
void FillEllipse(float x, float y, float w, float h);
// arc
void DrawArc(float x, float y, float radius, float startAngle, float endAngle);
void FillArc(float x, float y, float radius, float startAngle, float endAngle);

// **Bezier & Complex Shapes**
void DrawBezierCurve(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end);
void DrawLinePath(const std::vector<Point2Df>& points, bool closePath = false);
void FillLinePath(const std::vector<Point2Df>& points);

// **Text Rendering**
void DrawText(const std::string& text, float x, float y);
void DrawText(const std::string& text, int x, int y);
void DrawText(const std::string& text, const Point2Di& position);
void DrawText(const std::string& text, const Point2Df& position);
void DrawTextInRect(const std::string& text, float x, float y, float w, float h);
void DrawTextInRect(const std::string& text, const Rect2Df& rect);
void DrawTextInRect(const std::string& text, const Rect2Di& rect);

// **Image Rendering**
void DrawImage(const std::string &imagePath, float x, float y);
void DrawImage(const std::string &imagePath, float x, float y, float w, float h);
void DrawImage(const std::string &imagePath, const Rect2Df &srcRect, const Rect2Df &destRect);
void DrawImage(const std::string& imagePath, int x, int y);
void DrawImage(const std::string& imagePath, const Point2Df& position);
void DrawImage(const std::string& imagePath, const Point2Di& position);
void DrawImage(const std::string& imagePath, int x, int y, int w, int h);
void DrawImage(const std::string& imagePath, const Rect2Df& position);
void DrawImage(const std::string& imagePath, const Rect2Di& position);
void DrawImage(std::shared_ptr<UCImage> image, float x, float y) = 0;
void DrawImage(std::shared_ptr<UCImage> image, float x, float y, float w, float h) = 0;
void DrawImage(std::shared_ptr<UCImage> image, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;


// **Painting with colors/patterns/gradients functions**
std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(float x1, float y1, float x2, float y2,
                                                      const std::vector<GradientStop>& stops);
std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(float cx1, float cy1, float r1,
                                                      float cx2, float cy2, float r2,
                                                      const std::vector<GradientStop>& stops);
void SetFillPaint(const Color& color);
void SetFillPaint(std::shared_ptr<IPaintPattern> pattern);
void SetStrokePaint(const Color& color);
void SetStrokePaint(std::shared_ptr<IPaintPattern> pattern);
void SetTextPaint(const Color& color);
void SetTextPaint(std::shared_ptr<IPaintPattern> pattern);


// **Style & State Functions**

// Fill and stroke
void SetStrokeWidth(float width);
void SetAlpha(float alpha);
virtual void SetStrokeWidth(float width) = 0;
virtual void SetLineCap(LineCap cap) = 0;
virtual void SetLineJoin(LineJoin join) = 0;
virtual void SetMiterLimit(float limit) = 0;
virtual void SetLineDash(const std::vector<float>& pattern, float offset = 0) = 0;

// Text styling
virtual void SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) = 0;
virtual void SetFontSize(float size) = 0;
virtual void SetFontWeight(FontWeight fw) = 0;
virtual void SetFontSlant(FontSlant fs) = 0;

virtual const TextStyle& GetTextStyle() const = 0;
virtual void SetTextStyle(const TextStyle& style) = 0;
virtual void SetTextAlignment(TextAlignment align) = 0;


// **Transformations**
void Translate(float x, float y);
void Rotate(float angle);
void Scale(float sx, float sy);
void SetTransform(float a, float b, float c, float d, float e, float f);
// set matrix to
void SetTransform(float a, float b, float c, float d, float e, float f);
// adjust current matrix by this one
void Transform(float a, float b, float c, float d, float e, float f); 
void ResetTransform();

// **Clipping**
void SetClipRect(float x, float y, float w, float h);
void SetClipRect(int x, int y, int w, int h);
void SetClipRect(const Rect2Df& rect);
void SetClipRect(const Rect2Di& rect);
void ClearClipRect();
void ClipRect(float x, float y, float w, float h);
void ClipRect(int x, int y, int w, int h);
void ClipRect(const Rect2Df& rect);
void ClipRect(const Rect2Di& rect);


// State management
void PushState();
void PopState();
void ResetState();


// PIXEL OPERATIONS (on whole pixel buffer)
void Clear(const Color& color);
bool PaintPixelBuffer(int x, int y, int width, int height, uint32_t* pixels);
bool PaintPixelBuffer(int x, int y, IPixelBuffer& pxBuf);
IPixelBuffer* SavePixelRegion(const Rect2Di& region);
bool RestorePixelRegion(const Rect2Di& region, IPixelBuffer* buf);
```

### Pixel Buffer Operations
*Interface: IPixelBuffer*
```cpp
void Init(int w, int h, bool clear = 0);
uint32_t GetPixel(int x, int y) const;
void SetPixel(int x, int y, uint32_t pixel);
uint32_t *GetPixelData();
int GetWidth();
int GetHeight();

````

### **Text Measurement**
```cpp
int GetTextLineWidth(const std::string& text);
int GetTextLineHeight(const std::string& text);
bool GetTextLineDimensions(const std::string& text, int& w, int& h);
int GetTextIndexForXY(const std::string &text, int x, int y, int w = 0, int h = 0);
Point2Df CalculateCenteredTextPosition(const std::string& text, const Rect2Df& bounds);


//  **Enhanced Drawing Helpers**
void DrawFilledRectangle(const Rect2Df& rect, const Color& fillColor, float borderWidth, const Color& borderColor, float borderRadius);
void DrawFilledRectangle(const Rect2Di& rect, const Color& fillColor, float borderWidth, const Color& borderColor, float borderRadius);
void DrawFilledCircle(const Point2Df& center, float radius, const Color& fillColor);
void DrawFilledCircle(const Point2Di& center, float radius, const Color& fillColor);
```

---

### **UI Component Factories**

#### **Button Factories** 
*Source: UltraCanvasButton.h*
```cpp
std::shared_ptr<UltraCanvasButton> CreateButton(const std::string& identifier, long id, long x, long y, long w, long h, const std::string& text);
std::shared_ptr<UltraCanvasButton> CreateAutoButton(const std::string& identifier, long x, long y, const std::string& text);
```

#### **Label Factories** 
*Source: UltraCanvasLabel.h*
```cpp
std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasLabel> CreateAutoLabel(const std::string& identifier, long x, long y, const std::string& text);
```

#### **Image Element Factories** 
*Source: UltraCanvasImageElement.h*
```cpp
std::shared_ptr<UltraCanvasImageElement> CreateImageElement(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasImageElement> CreateImageFromFile(const std::string& identifier, long id, long x, long y, long w, long h, const std::string& imagePath);
std::shared_ptr<UltraCanvasImageElement> CreateImageFromMemory(const std::string& identifier, long id, long x, long y, long w, long h, const std::vector<uint8_t>& imageData, ImageFormat format);
std::shared_ptr<UltraCanvasImageElement> CreateScaledImage(const std::string& identifier, long id, long x, long y, long w, long h, const std::string& imagePath, ImageScaleMode scaleMode);
std::shared_ptr<UltraCanvasImageElement> CreateClickableImage(const std::string& identifier, long id, long x, long y, long w, long h, const std::string& imagePath, std::function<void()> clickCallback);
```

#### **Text Area Factories** 
*Source: UltraCanvasTextArea.h*
```cpp
std::shared_ptr<UltraCanvasTextArea> CreateTextArea(const std::string& id, long uid, long x, long y, long width, long height);
std::shared_ptr<UltraCanvasTextArea> CreateTextArea(const std::string& id, long uid, const Rect2D& bounds);
```

#### **Menu Factories** 
*Source: UltraCanvasMenu.h*
```cpp
std::shared_ptr<UltraCanvasMenu> CreateMenu(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasMenu> CreateMenuBar(const std::string& identifier, long id, long x, long y, long w);
```

#### **Dropdown Factories** 
*Source: UltraCanvasDropdown.h*
```cpp
std::shared_ptr<UltraCanvasDropdown> CreateDropdown(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasDropdown> CreateAutoDropdown(const std::string& identifier, long x, long y);
```

#### **Slider Factories** 
*Source: UltraCanvasSlider.h*
```cpp
std::shared_ptr<UltraCanvasSlider> CreateSlider(const std::string& identifier, long id, long x, long y, long width, long height);
std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(const std::string& identifier, long id, long x, long y, long width, long height, float min, float max);
std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(const std::string& identifier, long id, long x, long y, long width, long height, float min, float max);
std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(const std::string& identifier, long id, long x, long y, long size, float min, float max);
std::shared_ptr<UltraCanvasSlider> CreateRoundedSlider(const std::string& identifier, long id, long x, long y, long width, long height, float min, float max);
```

#### **Advanced Control Factories** 
*Source: UltraCanvasAdvancedControls.h*
```cpp
std::shared_ptr<UltraCanvasAdvancedSlider> CreateAdvancedSlider(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasRadioButton> CreateRadioButton(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasSwitch> CreateSwitch(const std::string& identifier, long id, long x, long y, long w, long h);
std::shared_ptr<UltraCanvasRadioGroup> CreateRadioGroup(const std::string& identifier, long id, long x, long y, long w, long h);
```

#### **SpinBox Factories** 
*Source: UltraCanvasSpinBox.h*
```cpp
std::shared_ptr<UltraCanvasSpinBox> CreateSpinBox(const std::string& identifier, long id, long x, long y, long width, long height);
```

#### **TreeView Factories** 
*Source: UltraCanvasTreeView.h*
```cpp
std::shared_ptr<UltraCanvasTreeView> CreateTreeView(const std::string& identifier, long id, long x, long y, long width, long height);
```

#### **File Dialog Factories** 
*Source: UltraCanvasFileDialog.h*
```cpp
std::shared_ptr<UltraCanvasFileDialog> CreateFileDialog(const std::string& id, long uid, long x, long y, long width, long height);
std::shared_ptr<UltraCanvasFileDialog> CreateOpenFileDialog(const std::string& id, long uid, const Rect2D& bounds);
std::shared_ptr<UltraCanvasFileDialog> CreateSaveFileDialog(const std::string& id, long uid, const Rect2D& bounds, const std::string& defaultName);
```

#### **Layout Panel Factories** 
*Source: UltraCanvasLayoutPanels.h*
```cpp
std::shared_ptr<UltraCanvasGridPanel> CreateGridPanel(const std::string& identifier, long id, long x, long y, long w, long h, int columns, int rows);
std::shared_ptr<UltraCanvasGridPanel> CreateFixedGridPanel(const std::string& identifier, long id, long x, long y, long w, long h, const std::vector<float>& columnWidths, const std::vector<float>& rowHeights);
```

#### **Styled Paragraph Factories** 
*Source: UltraCanvasStyledParagraph.h*
```cpp
std::shared_ptr<UltraCanvasStyledParagraph> CreateStyledParagraph(const std::string& id, long uid, long x, long y, long width, long height);
std::shared_ptr<UltraCanvasStyledParagraph> CreateStyledParagraph(const std::string& id, long uid, const Rect2D& bounds);
```

---

## **ELEMENT MANAGEMENT FUNCTIONS**

### **Core Element Factory System** 
*Source: UltraCanvasUIElement.h*
```cpp
// Generic factory templates
template<typename ElementType, typename... Args>
static std::shared_ptr<ElementType> Create(Args&&... args);

template<typename ElementType, typename... Args>
static std::shared_ptr<ElementType> CreateWithID(long id, Args&&... args);

template<typename ElementType, typename... Args>
static std::shared_ptr<ElementType> CreateWithIdentifier(const std::string& identifier, Args&&... args);
```

### **Element Hierarchy Functions** 
*Source: UltraCanvasUIElement.h*
```cpp
UltraCanvasUIElement* FindElementByID(UltraCanvasUIElement* root, const std::string& id);
Rect2Di CalculateTotalBounds(UltraCanvasUIElement* root);
```

### **Container Functions**
*Source: UltraCanvasContainer.h*
```cpp
UltraCanvasUIElement* FindElementAtPoint(int x, int y);
void AddChild(std::shared_ptr<UltraCanvasUIElement> child);
void RemoveChild(std::shared_ptr<UltraCanvasUIElement> child);
void ClearChildren();
```

---


### **File I/O**
```cpp
bool SaveToFile(const std::string& filename);
bool LoadFromFile(const std::string& filename);
```

---

## **TEXT PROCESSING FUNCTIONS** 
*Source: UltraCanvasTextShaper.h*
### **Text Shaping**
```cpp
bool ShapeText(const std::string& text, const std::string& fontPath, int fontSize, ShapingResult& result, const ShapingFeatures& features);
bool ShapeText(const char* text, const char* fontPath, int fontSize, std::vector<int>& glyphIndices, std::vector<int>& positionsX);
```

### **Text Layout**
```cpp
bool LayoutText(const std::string& text, const std::string& fontPath, int fontSize, float maxWidth, LayoutResult& result, const ShapingFeatures& features);
std::vector<int> FindLineBreaks(const std::string& text, const std::string& fontPath, int fontSize, float maxWidth);
TextScript DetectTextScript(const std::string& text);
std::string FindSystemFont(const std::string& fontName);
```

---

## **EVENT MANAGEMENT FUNCTIONS** 
*Source: UltraCanvasEventDispatcher.h*

### **Event Dispatch**
```cpp
void DispatchEventToElements(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleMouseDown(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleMouseUp(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleMouseMove(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleMouseDoubleClick(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleMouseWheel(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
bool HandleKeyboardEvent(const UCEvent& event, std::vector<UltraCanvasUIElement*>& elements);
```

### **Focus Management**
```cpp
void FocusNextElement(std::vector<UltraCanvasUIElement*>& elements, bool reverse = false);
void FocusPreviousElement(std::vector<UltraCanvasUIElement*>& elements);
UltraCanvasUIElement* GetFocusedElement();
UltraCanvasUIElement* GetHoveredElement();
void SetGlobalFocus(UltraCanvasUIElement* element);
void ClearGlobalFocus();
```

### **Mouse & Keyboard State**
```cpp
bool IsKeyPressed(int keyCode);
bool IsShiftHeld();
bool IsCtrlHeld();
bool IsAltHeld();
bool IsMetaHeld();
void CaptureMouse(UltraCanvasUIElement* element);
void ReleaseMouse();
```

### **Global Event Handlers**
```cpp
void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
void ClearGlobalEventHandlers();
```

### **Debug Utilities**
```cpp
void PrintEventInfo(const UCEvent& event);
void PrintFocusInfo();
void ResetEventDispatcher();
UltraCanvasUIElement* FindElementAtPoint(int x, int y, std::vector<UltraCanvasUIElement*>& elements);
```

---

## **CLIPBOARD FUNCTIONS** 
*Source: UltraCanvasClipboard.h*

### **Global Clipboard**
```cpp
bool InitializeClipboard();
void ShutdownClipboard();
UltraCanvasClipboard* GetClipboard();
```

### **Convenience Functions**
```cpp
bool GetClipboardText(std::string& text);
bool SetClipboardText(const std::string& text);
void AddClipboardEntry(const ClipboardData& entry);
```

---

## **DIALOG CONVENIENCE FUNCTIONS** 
*Source: UltraCanvasFileDialog.h*

```cpp
std::string OpenFileDialog(const std::vector<FileFilter>& filters);
std::string SaveFileDialog(const std::string& defaultName, const std::vector<FileFilter>& filters);
std::vector<std::string> OpenMultipleFilesDialog(const std::vector<FileFilter>& filters);
std::string SelectFolderDialog();
```

---

## **UTILITY & CONVENIENCE FUNCTIONS**

### **Component State Helpers**
```cpp
// Text Area helpers
void SetTextAreaContent(UltraCanvasTextArea* textArea, const std::string& content);
std::string GetTextAreaContent(const UltraCanvasTextArea* textArea);

// Slider helpers
void SetSliderValue(UltraCanvasSlider* slider, float value);
float GetSliderValue(const UltraCanvasSlider* slider);
void SetSliderRange(UltraCanvasSlider* slider, float min, float max);

// SpinBox helpers
int GetSpinBoxValue(const UltraCanvasSpinBox* spinBox);
void SetSpinBoxRange(UltraCanvasSpinBox* spinBox, int min, int max);

// Styled Paragraph helpers
std::string GetPlainTextFromParagraph(const UltraCanvasStyledParagraph* paragraph);
```

---

## **MENU ITEM FACTORIES** 
*Source: UltraCanvasMenu.h*

```cpp
// Static factory methods for MenuItemData
static MenuItemData Action(const std::string& label, std::function<void()> callback);
static MenuItemData Action(const std::string& label, const std::string& iconPath, std::function<void()> callback);
static MenuItemData Separator();
static MenuItemData Checkbox(const std::string& label, bool checked, std::function<void(bool)> callback);
static MenuItemData Radio(const std::string& label, int group, std::function<void()> callback);
static MenuItemData Submenu(const std::string& label, const std::vector<MenuItemData>& items);
static MenuItemData Submenu(const std::string& label, const std::string& iconPath, const std::vector<MenuItemData>& items);
static MenuItemData Input(const std::string& label, const std::string& placeholder, std::function<void(const std::string&)> callback);
```

---

## **TEMPLATE SYSTEM FUNCTIONS** 
*Source: UltraCanvasTemplate.h*

### **Template Element Creation**
```cpp
std::shared_ptr<UltraCanvasUIElement> CreateButtonElement(const TemplateElementDescriptor& desc);
std::shared_ptr<UltraCanvasUIElement> CreateLabelElement(const TemplateElementDescriptor& desc);
std::shared_ptr<UltraCanvasUIElement> CreateDropDownElement(const TemplateElementDescriptor& desc);
std::shared_ptr<UltraCanvasUIElement> CreateSeparatorElement(const TemplateElementDescriptor& desc);
std::shared_ptr<UltraCanvasUIElement> CreateSpacerElement(const TemplateElementDescriptor& desc);
```

---

## **RESERVED FUNCTION PATTERNS**

### **NEVER CREATE FUNCTIONS WITH THESE PATTERNS:**
```cpp
â�Œ Create[BasicShape]()        // Use existing CreateRectangleShape, CreateCircleShape, etc.
â�Œ Draw[BasicShape]()         // Use existing DrawCircle, DrawRectangle, etc.
â�Œ Fill[BasicShape]()         // Use existing FillCircle, FillRectangle, etc.
â�Œ Set[RenderState]()         // Use existing SetColor, PaintWithColor, etc.
â�Œ Get[RenderState]()         // Use existing GetTextLineWidth, GetTextLineDimensions, etc.
â�Œ Handle[StandardEvent]()    // Use existing HandleMouseDown, HandleKeyboardEvent, etc.
â�Œ [Component]Factory()       // Use existing Create[Component] pattern
â�Œ Measure[Text]()            // Use existing GetTextLineDimensions, GetTextLineWidth, etc.
```

### **ALLOWED NEW FUNCTION PATTERNS:**
```cpp
âœ… Create[NewComponentType]()     // Only for genuinely new components
âœ… Handle[CustomEvent]()          // Only for new custom events
âœ… Set[NewProperty]()             // Only for new properties
âœ… [Platform]Specific()           // Platform implementations in OS folders
âœ… [CustomDomain]Helper()         // Domain-specific utilities
```

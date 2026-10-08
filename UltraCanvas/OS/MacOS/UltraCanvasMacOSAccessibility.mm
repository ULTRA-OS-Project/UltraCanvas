// OS/MacOS/UltraCanvasMacOSAccessibility.mm
// The NSAccessibility bridge. A window's content view (UltraCanvasView) asks
// this file for its children, for the element under a point and for the
// focused element; every UltraCanvas element is answered by one
// UCAccessibilityElement (an NSAccessibilityElement subclass), kept for the
// element's lifetime so VoiceOver keeps its place.
//
// What an element says comes from UltraCanvasAccessibility.h (role, name,
// description, toggle state, range value, value text, default action and the
// IAccessibleText); the tree and ids from UltraCanvasAccessibilityBridge.h,
// shared with the AT-SPI and UI Automation bridges. Text ranges are
// NSString (UTF-16) ranges here and code points in UltraCanvas; the helpers
// below convert. Element objects only hold an id: one whose element is gone
// answers as an empty, ignored element.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSAccessibility.h"
#include "UltraCanvasAccessibility.h"
#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasMacOSWindow.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#if __has_feature(objc_arc)
#define UC_AUTORELEASE(x) (x)
#define UC_RELEASE(x)
#else
#define UC_AUTORELEASE(x) [(x) autorelease]
#define UC_RELEASE(x) [(x) release]
#endif

using UltraCanvas::AccessibleRange;
using UltraCanvas::AccessibleRole;
using UltraCanvas::AccessibleTextBoundary;
using UltraCanvas::AccessibleToggleState;
using UltraCanvas::IAccessibleText;
using UltraCanvas::Point2Df;
using UltraCanvas::Rect2Df;
using UltraCanvas::UltraCanvasMacOSWindow;
using UltraCanvas::UltraCanvasUIElement;
using UltraCanvas::UltraCanvasWindowBase;
namespace AB = UltraCanvas::AccessibilityBridge;

namespace {

// ===== STATE =====

AB::IdMap gIds;
NSMutableDictionary* gObjects = nil;   // NSNumber(element id) -> UCAccessibilityElement
int gListener = 0;

UltraCanvasUIElement* Lookup(uint32_t id) {
    UltraCanvasUIElement* element = gIds.ElementOf(id);
    return element && AB::IsLive(element) ? element : nullptr;
}

UltraCanvasMacOSWindow* MacWindowOf(UltraCanvasUIElement* element) {
    return dynamic_cast<UltraCanvasMacOSWindow*>(AB::WindowOf(element));
}

NSView* ViewOf(UltraCanvasUIElement* element) {
    UltraCanvasMacOSWindow* window = MacWindowOf(element);
    return window ? window->GetContentView() : nil;
}

// An element worth announcing: one that described itself, or holds others.
bool IsReported(UltraCanvasUIElement* element) {
    return element && (element->GetAccessibleRole() != AccessibleRole::Unknown || !AB::Children(element).empty());
}

// ===== UTF-16 <-> CODE POINTS =====

int Utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    return 4;
}

// The UTF-16 index of code point `offset` in `utf8`.
NSUInteger Utf16Index(const std::string& utf8, int offset) {
    NSUInteger units = 0;
    int points = 0;
    for (size_t i = 0; i < utf8.size() && points < offset; points++) {
        const int length = Utf8Length(static_cast<unsigned char>(utf8[i]));
        units += length == 4 ? 2 : 1;
        i += length;
    }
    return units;
}

// The code point holding UTF-16 index `index` in `utf8`.
int CodePointIndex(const std::string& utf8, NSUInteger index) {
    NSUInteger units = 0;
    int points = 0;
    for (size_t i = 0; i < utf8.size(); points++) {
        const int length = Utf8Length(static_cast<unsigned char>(utf8[i]));
        const NSUInteger width = length == 4 ? 2 : 1;
        if (units + width > index) return points;
        units += width;
        i += length;
    }
    return points;
}

NSString* ToNSString(const std::string& utf8) {
    NSString* s = [[NSString alloc] initWithBytes:utf8.data() length:utf8.size() encoding:NSUTF8StringEncoding];
    return s ? UC_AUTORELEASE(s) : @"";
}

// A code-point range [start, end) as an NSRange over `utf8`.
NSRange ToNSRange(const std::string& utf8, int start, int end) {
    const NSUInteger from = Utf16Index(utf8, start);
    const NSUInteger to = Utf16Index(utf8, std::max(start, end));
    return NSMakeRange(from, to - from);
}

// ===== GEOMETRY =====

// A rectangle in the element's window (logical points, top-left origin) on
// the screen, in AppKit screen coordinates.
NSRect ScreenRect(UltraCanvasUIElement* element, const Rect2Df& rect) {
    NSView* view = ViewOf(element);
    if (!view || !view.window) return NSZeroRect;
    // The view is flipped: window coordinates are its own.
    const NSRect inView = NSMakeRect(rect.x, rect.y, rect.width, rect.height);
    return [view.window convertRectToScreen:[view convertRect:inView toView:nil]];
}

// A screen point in a window's logical coordinates.
Point2Df WindowPoint(NSView* view, NSPoint screen) {
    if (!view || !view.window) return Point2Df(-1, -1);
    const NSRect inWindow = [view.window convertRectFromScreen:NSMakeRect(screen.x, screen.y, 0, 0)];
    const NSPoint inView = [view convertPoint:inWindow.origin fromView:nil];
    return Point2Df(static_cast<float>(inView.x), static_cast<float>(inView.y));
}

// The deepest visible element under a window point, among `root`'s
// descendants (null when none contains it).
UltraCanvasUIElement* HitTestIn(UltraCanvasUIElement* root, const Point2Df& point) {
    const auto children = AB::Children(root);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UltraCanvasUIElement* child = *it;
        if (!child->IsVisible() || !child->GetBoundsInWindow().Contains(point)) continue;
        if (UltraCanvasUIElement* deeper = HitTestIn(child, point)) return deeper;
        return child;
    }
    return nullptr;
}

// ===== TEXT LINES =====

// The line number holding code point `offset`.
NSInteger LineOf(IAccessibleText* text, int offset) {
    const int count = text->GetCharacterCount();
    NSInteger line = 0;
    int position = 0;
    while (position < count && line < 100000) {
        int start = 0, end = 0;
        text->GetTextAtOffset(position, AccessibleTextBoundary::Line, start, end);
        if (end <= position) end = position + 1;
        if (offset < end) return line;
        position = end;
        line++;
    }
    return std::max<NSInteger>(0, line - (offset >= count && count > 0 ? 1 : 0));
}

// The code-point extent of line `line`; false past the last.
bool LineExtent(IAccessibleText* text, NSInteger line, int& start, int& end) {
    const int count = text->GetCharacterCount();
    int position = 0;
    for (NSInteger current = 0; position < count && current < 100000; current++) {
        text->GetTextAtOffset(position, AccessibleTextBoundary::Line, start, end);
        if (end <= position) end = position + 1;
        if (current == line) return true;
        position = end;
    }
    start = end = count;
    return line == 0;
}

void EnsureListening();

} // namespace

// ===== ELEMENT OBJECTS =====

@interface UCAccessibilityElement : NSAccessibilityElement
@property (nonatomic, assign) uint32_t elementId;
@end

namespace {

UCAccessibilityElement* ObjectFor(UltraCanvasUIElement* element) {
    if (!element) return nil;
    if (!gObjects) gObjects = [[NSMutableDictionary alloc] init];
    const uint32_t id = gIds.IdOf(element);
    NSNumber* key = [NSNumber numberWithUnsignedInt:id];
    UCAccessibilityElement* object = [gObjects objectForKey:key];
    if (!object) {
        object = [[UCAccessibilityElement alloc] init];
        object.elementId = id;
        [gObjects setObject:object forKey:key];
        UC_RELEASE(object);   // the dictionary holds it
    }
    return object;
}

// The parent VoiceOver walks up to: the element's parent object, or the
// window's content view for a top-level element.
id ParentObjectOf(UltraCanvasUIElement* element) {
    UltraCanvasUIElement* parent = AB::Parent(element);
    if (!parent || AB::AsWindow(parent)) return ViewOf(element);
    // Skip layout-only containers that are not reported.
    while (parent && !AB::AsWindow(parent) && !IsReported(parent)) parent = AB::Parent(parent);
    if (!parent || AB::AsWindow(parent)) return ViewOf(element);
    return ObjectFor(parent);
}

// The children VoiceOver sees: reported elements, with the children of
// unreported layout containers lifted into their place.
void CollectChildren(UltraCanvasUIElement* element, NSMutableArray* out) {
    for (UltraCanvasUIElement* child : AB::Children(element)) {
        if (!child->IsVisible()) continue;
        if (child->GetAccessibleRole() != AccessibleRole::Unknown) {
            [out addObject:ObjectFor(child)];
        } else {
            CollectChildren(child, out);
        }
    }
}

} // namespace

@implementation UCAccessibilityElement

@synthesize elementId;

- (UltraCanvasUIElement*)ucElement {
    return Lookup(self.elementId);
}

// ----- identity -----

- (BOOL)isAccessibilityElement {
    UltraCanvasUIElement* element = [self ucElement];
    return element && element->GetAccessibleRole() != AccessibleRole::Unknown;
}

- (NSAccessibilityRole)accessibilityRole {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element) return NSAccessibilityUnknownRole;
    switch (element->GetAccessibleRole()) {
        case AccessibleRole::Button:      return NSAccessibilityButtonRole;
        case AccessibleRole::CheckBox:    return NSAccessibilityCheckBoxRole;
        case AccessibleRole::RadioButton: return NSAccessibilityRadioButtonRole;
        case AccessibleRole::Switch:      return NSAccessibilityCheckBoxRole;   // subrole AXSwitch
        case AccessibleRole::Label:       return NSAccessibilityStaticTextRole;
        case AccessibleRole::TextField:   return NSAccessibilityTextFieldRole;
        case AccessibleRole::TextArea:
        case AccessibleRole::Document:    return NSAccessibilityTextAreaRole;
        case AccessibleRole::List:        return NSAccessibilityListRole;
        case AccessibleRole::ListItem:    return NSAccessibilityGroupRole;
        case AccessibleRole::Table:       return NSAccessibilityTableRole;
        case AccessibleRole::Image:       return NSAccessibilityImageRole;
        case AccessibleRole::Link:        return NSAccessibilityLinkRole;
        case AccessibleRole::Menu:        return NSAccessibilityMenuRole;
        case AccessibleRole::MenuItem:    return NSAccessibilityMenuItemRole;
        case AccessibleRole::ComboBox:    return NSAccessibilityPopUpButtonRole;
        case AccessibleRole::Slider:      return NSAccessibilitySliderRole;
        case AccessibleRole::SpinButton:  return NSAccessibilityIncrementorRole;
        case AccessibleRole::ProgressBar: {
            AccessibleRange range;
            return element->GetAccessibleRange(range) ? NSAccessibilityProgressIndicatorRole
                                                      : NSAccessibilityBusyIndicatorRole;
        }
        case AccessibleRole::Toolbar:     return NSAccessibilityToolbarRole;
        case AccessibleRole::TabList:     return NSAccessibilityTabGroupRole;
        case AccessibleRole::Tree:        return NSAccessibilityOutlineRole;
        case AccessibleRole::Window:
        case AccessibleRole::Group:       return NSAccessibilityGroupRole;
        case AccessibleRole::Unknown:     break;
    }
    return AB::Children(element).empty() ? NSAccessibilityUnknownRole : NSAccessibilityGroupRole;
}

- (NSAccessibilitySubrole)accessibilitySubrole {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element) return nil;
    if (element->IsAccessiblePassword()) return NSAccessibilitySecureTextFieldSubrole;
    if (element->GetAccessibleRole() == AccessibleRole::Switch) return @"AXSwitch";
    if (element->GetAccessibleRole() == AccessibleRole::Button &&
        element->GetAccessibleToggleState() != AccessibleToggleState::NotToggleable) {
        return NSAccessibilityToggleSubrole;
    }
    return nil;
}

- (NSString*)accessibilityLabel {
    UltraCanvasUIElement* element = [self ucElement];
    return element ? ToNSString(AB::Name(element)) : @"";
}

- (NSString*)accessibilityHelp {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element) return nil;
    const std::string help = element->GetAccessibleDescription();
    return help.empty() ? nil : ToNSString(help);
}

- (NSString*)accessibilityIdentifier {
    UltraCanvasUIElement* element = [self ucElement];
    return element ? ToNSString(element->GetIdentifier()) : @"";
}

// ----- tree and place -----

- (id)accessibilityParent {
    UltraCanvasUIElement* element = [self ucElement];
    return element ? ParentObjectOf(element) : nil;
}

- (NSArray*)accessibilityChildren {
    UltraCanvasUIElement* element = [self ucElement];
    NSMutableArray* children = [NSMutableArray array];
    if (element) CollectChildren(element, children);
    return children;
}

- (id)accessibilityWindow {
    UltraCanvasUIElement* element = [self ucElement];
    NSView* view = element ? ViewOf(element) : nil;
    return view ? view.window : nil;
}

- (id)accessibilityTopLevelUIElement {
    return [self accessibilityWindow];
}

- (NSRect)accessibilityFrame {
    UltraCanvasUIElement* element = [self ucElement];
    return element ? ScreenRect(element, element->GetBoundsInWindow()) : NSZeroRect;
}

- (id)accessibilityHitTest:(NSPoint)point {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element) return nil;
    UltraCanvasUIElement* hit = HitTestIn(element, WindowPoint(ViewOf(element), point));
    while (hit && hit != element && !IsReported(hit)) hit = AB::Parent(hit);
    return hit && hit != element ? ObjectFor(hit) : self;
}

// ----- state -----

- (BOOL)isAccessibilityEnabled {
    UltraCanvasUIElement* element = [self ucElement];
    return element && !element->IsDisabled();
}

- (BOOL)isAccessibilityFocused {
    UltraCanvasUIElement* element = [self ucElement];
    return element && element->IsFocused();
}

- (void)setAccessibilityFocused:(BOOL)focused {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element || !focused || !element->CanReceiveFocus()) return;
    if (UltraCanvasWindowBase* window = element->GetWindow()) window->SetFocusedElement(element);
}

// ----- value -----

- (id)accessibilityValue {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element || element->IsAccessiblePassword()) return nil;
    switch (element->GetAccessibleToggleState()) {
        case AccessibleToggleState::On:    return [NSNumber numberWithInt:1];
        case AccessibleToggleState::Off:   return [NSNumber numberWithInt:0];
        case AccessibleToggleState::Mixed: return [NSNumber numberWithInt:2];
        case AccessibleToggleState::NotToggleable: break;
    }
    AccessibleRange range;
    if (element->GetAccessibleRange(range)) return [NSNumber numberWithDouble:range.value];
    if (IAccessibleText* text = AB::TextInterface(element)) return ToNSString(text->GetAccessibleText());
    const std::string value = element->GetAccessibleValueText();
    return value.empty() ? nil : ToNSString(value);
}

- (void)setAccessibilityValue:(id)value {
    UltraCanvasUIElement* element = [self ucElement];
    if (!element) return;
    if ([value isKindOfClass:[NSNumber class]]) {
        element->SetAccessibleValue([(NSNumber*)value doubleValue]);
    } else if ([value isKindOfClass:[NSString class]]) {
        const char* utf8 = [(NSString*)value UTF8String];
        element->SetAccessibleValueText(utf8 ? utf8 : "");
    }
}

- (id)accessibilityMinValue {
    UltraCanvasUIElement* element = [self ucElement];
    AccessibleRange range;
    return element && element->GetAccessibleRange(range) ? [NSNumber numberWithDouble:range.minimum] : nil;
}

- (id)accessibilityMaxValue {
    UltraCanvasUIElement* element = [self ucElement];
    AccessibleRange range;
    return element && element->GetAccessibleRange(range) ? [NSNumber numberWithDouble:range.maximum] : nil;
}

// ----- actions -----

- (BOOL)accessibilityPerformPress {
    UltraCanvasUIElement* element = [self ucElement];
    return element && !element->GetAccessibleActionName().empty() && element->DoAccessibleAction();
}

- (BOOL)stepBy:(double)direction {
    UltraCanvasUIElement* element = [self ucElement];
    AccessibleRange range;
    if (!element || !element->GetAccessibleRange(range) || range.readOnly) return NO;
    const double step = range.step > 0 ? range.step : (range.maximum - range.minimum) / 20.0;
    const double next = std::clamp(range.value + direction * step, range.minimum, range.maximum);
    return element->SetAccessibleValue(next);
}

- (BOOL)accessibilityPerformIncrement { return [self stepBy:1.0]; }
- (BOOL)accessibilityPerformDecrement { return [self stepBy:-1.0]; }

// ----- text (elements with an IAccessibleText, and text fields) -----

- (IAccessibleText*)ucText {
    UltraCanvasUIElement* element = [self ucElement];
    return element ? AB::TextInterface(element) : nullptr;
}

- (NSInteger)accessibilityNumberOfCharacters {
    IAccessibleText* text = [self ucText];
    return text ? static_cast<NSInteger>(Utf16Index(text->GetAccessibleText(), text->GetCharacterCount())) : 0;
}

- (NSRange)accessibilitySelectedTextRange {
    IAccessibleText* text = [self ucText];
    if (!text) return NSMakeRange(0, 0);
    int start = 0, end = 0;
    if (!text->GetSelection(start, end)) start = end = text->GetCaretOffset();
    if (start > end) std::swap(start, end);
    return ToNSRange(text->GetAccessibleText(), start, end);
}

- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    IAccessibleText* text = [self ucText];
    if (!text) return;
    const std::string all = text->GetAccessibleText();
    const int start = CodePointIndex(all, range.location);
    const int end = CodePointIndex(all, range.location + range.length);
    if (start == end) text->SetCaretOffset(start);
    else text->SetSelection(start, end);
}

- (NSString*)accessibilitySelectedText {
    IAccessibleText* text = [self ucText];
    if (!text) return nil;
    int start = 0, end = 0;
    if (!text->GetSelection(start, end) || start == end) return @"";
    return ToNSString(AB::Substring(text->GetAccessibleText(), std::min(start, end), std::max(start, end)));
}

- (NSRange)accessibilityVisibleCharacterRange {
    IAccessibleText* text = [self ucText];
    if (!text) return NSMakeRange(0, 0);
    return NSMakeRange(0, static_cast<NSUInteger>([self accessibilityNumberOfCharacters]));
}

- (NSInteger)accessibilityInsertionPointLineNumber {
    IAccessibleText* text = [self ucText];
    return text ? LineOf(text, text->GetCaretOffset()) : 0;
}

- (NSString*)accessibilityStringForRange:(NSRange)range {
    IAccessibleText* text = [self ucText];
    if (!text) return nil;
    NSString* all = ToNSString(text->GetAccessibleText());
    if (range.location > all.length) return @"";
    range.length = std::min(range.length, all.length - range.location);
    return [all substringWithRange:range];
}

- (NSAttributedString*)accessibilityAttributedStringForRange:(NSRange)range {
    NSString* plain = [self accessibilityStringForRange:range];
    if (!plain) return nil;
    NSAttributedString* attributed = [[NSAttributedString alloc] initWithString:plain];
    return UC_AUTORELEASE(attributed);
}

- (NSInteger)accessibilityLineForIndex:(NSInteger)index {
    IAccessibleText* text = [self ucText];
    if (!text || index < 0) return 0;
    return LineOf(text, CodePointIndex(text->GetAccessibleText(), static_cast<NSUInteger>(index)));
}

- (NSRange)accessibilityRangeForLine:(NSInteger)line {
    IAccessibleText* text = [self ucText];
    if (!text || line < 0) return NSMakeRange(0, 0);
    int start = 0, end = 0;
    if (!LineExtent(text, line, start, end)) return NSMakeRange(NSNotFound, 0);
    return ToNSRange(text->GetAccessibleText(), start, end);
}

- (NSRange)accessibilityRangeForIndex:(NSInteger)index {
    IAccessibleText* text = [self ucText];
    if (!text || index < 0) return NSMakeRange(0, 0);
    const std::string all = text->GetAccessibleText();
    const int point = CodePointIndex(all, static_cast<NSUInteger>(index));
    return ToNSRange(all, point, std::min(point + 1, text->GetCharacterCount()));
}

- (NSRange)accessibilityStyleRangeForIndex:(NSInteger)index {
    IAccessibleText* text = [self ucText];
    if (!text || index < 0) return NSMakeRange(0, 0);
    const std::string all = text->GetAccessibleText();
    int start = 0, end = 0;
    text->GetAttributesAt(CodePointIndex(all, static_cast<NSUInteger>(index)), start, end);
    return ToNSRange(all, start, end);
}

- (NSRect)accessibilityFrameForRange:(NSRange)range {
    UltraCanvasUIElement* element = [self ucElement];
    IAccessibleText* text = [self ucText];
    if (!element || !text) return NSZeroRect;
    const std::string all = text->GetAccessibleText();
    const int start = CodePointIndex(all, range.location);
    const int end = std::max(start + 1, CodePointIndex(all, range.location + range.length));
    const Rect2Df box = AB::RangeBounds(text, start, end);
    if (box.width <= 0 && box.height <= 0) return NSZeroRect;
    return ScreenRect(element, box);
}

- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
    UltraCanvasUIElement* element = [self ucElement];
    IAccessibleText* text = [self ucText];
    if (!element || !text) return NSMakeRange(0, 0);
    const int offset = AB::CharacterAtPoint(text, WindowPoint(ViewOf(element), point));
    if (offset < 0) return NSMakeRange(NSNotFound, 0);
    const std::string all = text->GetAccessibleText();
    return ToNSRange(all, offset, std::min(offset + 1, text->GetCharacterCount()));
}

@end

// ===== EVENTS =====

namespace {

void Post(UltraCanvasUIElement* element, NSString* notification) {
    if (!element) return;
    NSAccessibilityPostNotification(ObjectFor(element), notification);
}

void OnAccessibilityEvent(const UltraCanvas::AccessibilityEvent& event) {
    using UltraCanvas::AccessibilityEventType;
    UltraCanvasUIElement* element = event.element;
    switch (event.type) {
        case AccessibilityEventType::ElementDestroyed: {
            NSNumber* key = [NSNumber numberWithUnsignedInt:gIds.IdOf(element)];
            if (UCAccessibilityElement* object = [gObjects objectForKey:key]) {
                NSAccessibilityPostNotification(object, NSAccessibilityUIElementDestroyedNotification);
                [gObjects removeObjectForKey:key];
            }
            gIds.Forget(element);
            break;
        }
        case AccessibilityEventType::FocusChanged:
            Post(element, NSAccessibilityFocusedUIElementChangedNotification);
            break;
        case AccessibilityEventType::TextChanged:
        case AccessibilityEventType::ValueChanged:
        case AccessibilityEventType::StateChanged:
            Post(element, NSAccessibilityValueChangedNotification);
            break;
        case AccessibilityEventType::CaretMoved:
        case AccessibilityEventType::SelectionChanged:
            Post(element, NSAccessibilitySelectedTextChangedNotification);
            break;
        case AccessibilityEventType::NameChanged:
            Post(element, NSAccessibilityTitleChangedNotification);
            break;
    }
}

void EnsureListening() {
    // Only once VoiceOver (or another client) has asked: until then elements
    // need not build events at all.
    if (!gListener) gListener = UltraCanvas::UltraCanvasAccessibility::AddListener(OnAccessibilityEvent);
}

} // namespace

// ===== ENTRY POINTS =====

namespace UltraCanvas {
namespace MacOSAccessibility {

NSArray* WindowChildren(UltraCanvasMacOSWindow* window) {
    if (!window) return nil;
    EnsureListening();
    NSMutableArray* children = [NSMutableArray array];
    CollectChildren(window, children);
    return children;
}

id HitTest(UltraCanvasMacOSWindow* window, NSPoint screenPoint) {
    if (!window) return nil;
    EnsureListening();
    UltraCanvasUIElement* hit = HitTestIn(window, WindowPoint(window->GetContentView(), screenPoint));
    while (hit && !AB::AsWindow(hit) && !IsReported(hit)) hit = AB::Parent(hit);
    return hit && !AB::AsWindow(hit) ? ObjectFor(hit) : nil;
}

id FocusedElement(UltraCanvasMacOSWindow* window) {
    if (!window) return nil;
    EnsureListening();
    UltraCanvasUIElement* focused = window->GetFocusedElement();
    return focused && focused != window ? ObjectFor(focused) : nil;
}

void Shutdown() {
    if (gListener) UltraCanvasAccessibility::RemoveListener(gListener);
    gListener = 0;
    [gObjects removeAllObjects];
    gIds.Clear();
}

} // namespace MacOSAccessibility
} // namespace UltraCanvas

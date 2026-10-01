// OS/MSWindows/UltraCanvasWindowsAccessibility.cpp
// The UI Automation bridge. Each window answers WM_GETOBJECT(UiaRootObjectId)
// with a fragment root; below it every element is a fragment, and an element
// with an IAccessibleText offers the Text pattern, whose ranges walk the text
// by character, format run, word, line, paragraph and document.
//
// What an element says comes from UltraCanvasAccessibility.h (role, name,
// IAccessibleText); the tree, ids and geometry from
// UltraCanvasAccessibilityBridge.h, shared with the AT-SPI bridge. Providers
// only hold an element id: a provider whose element is gone answers
// UIA_E_ELEMENTNOTAVAILABLE instead of touching freed memory.
//
// UIAutomationCore.dll is loaded at run time, so there is no import library
// to link and nothing changes for a process no client ever asks.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasWindowsAccessibility.h"

#include <ole2.h>
#include <oleauto.h>
#include <uiautomation.h>

#include "UltraCanvasAccessibility.h"
#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace WindowsAccessibility {

namespace {

namespace AB = AccessibilityBridge;

// ===== CONSTANTS =====
// Values from UIAutomationClient.h that older MinGW headers lack. Named here,
// not as the SDK's macros, so newer headers that do define them are no clash.
constexpr long kRootObjectId = -25;                 // UiaRootObjectId
constexpr int kAppendRuntimeId = 3;                 // UiaAppendRuntimeId
constexpr HRESULT kElementNotAvailable = static_cast<HRESULT>(0x80040201);   // UIA_E_ELEMENTNOTAVAILABLE

constexpr TEXTATTRIBUTEID kBackgroundColor = 40001;
constexpr TEXTATTRIBUTEID kFontName = 40005;
constexpr TEXTATTRIBUTEID kFontSize = 40006;
constexpr TEXTATTRIBUTEID kFontWeight = 40007;
constexpr TEXTATTRIBUTEID kForegroundColor = 40008;
constexpr TEXTATTRIBUTEID kIsItalic = 40014;
constexpr TEXTATTRIBUTEID kIsReadOnly = 40015;
constexpr TEXTATTRIBUTEID kIsSubscript = 40016;
constexpr TEXTATTRIBUTEID kIsSuperscript = 40017;
constexpr TEXTATTRIBUTEID kStrikethroughStyle = 40026;
constexpr TEXTATTRIBUTEID kUnderlineStyle = 40030;
constexpr TEXTATTRIBUTEID kAnnotationTypes = 40031;
constexpr TEXTATTRIBUTEID kStyleId = 40034;

constexpr int kStyleHeading1 = 70001;               // StyleId_Heading1; _Heading9 = 70009
constexpr int kStyleNormal = 70012;
constexpr int kStyleBulletedList = 70015;
constexpr int kAnnotationSpellingError = 60001;
constexpr int kAnnotationComment = 60003;
constexpr int kAnnotationInsertionChange = 60011;
constexpr int kAnnotationDeletionChange = 60012;

// Our text ranges answer this private interface id with themselves, so
// Compare/CompareEndpoints can read another range's offsets.
const IID kTextRangeIid = {0x6b2f0c41, 0x8d1e, 0x4c5a, {0x9a, 0x3e, 0x51, 0x0c, 0x2d, 0x77, 0x1f, 0x84}};

// ===== UIAUTOMATIONCORE =====

struct UiaApi {
    HMODULE module = nullptr;
    bool tried = false;
    LRESULT (WINAPI* ReturnRawElementProvider)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*) = nullptr;
    HRESULT (WINAPI* HostProviderFromHwnd)(HWND, IRawElementProviderSimple**) = nullptr;
    HRESULT (WINAPI* RaiseAutomationEvent)(IRawElementProviderSimple*, EVENTID) = nullptr;
    BOOL (WINAPI* ClientsAreListening)() = nullptr;
    HRESULT (WINAPI* GetReservedNotSupportedValue)(IUnknown**) = nullptr;
    HRESULT (WINAPI* GetReservedMixedAttributeValue)(IUnknown**) = nullptr;
    HRESULT (WINAPI* DisconnectAllProviders)() = nullptr;
};

UiaApi gUia;

template <typename T>
void Resolve(T& function, const char* name) {
    function = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(gUia.module, name)));
}

bool LoadUia() {
    if (gUia.tried) return gUia.ReturnRawElementProvider != nullptr;
    gUia.tried = true;
    gUia.module = LoadLibraryW(L"UIAutomationCore.dll");
    if (!gUia.module) return false;
    Resolve(gUia.ReturnRawElementProvider, "UiaReturnRawElementProvider");
    Resolve(gUia.HostProviderFromHwnd, "UiaHostProviderFromHwnd");
    Resolve(gUia.RaiseAutomationEvent, "UiaRaiseAutomationEvent");
    Resolve(gUia.ClientsAreListening, "UiaClientsAreListening");
    Resolve(gUia.GetReservedNotSupportedValue, "UiaGetReservedNotSupportedValue");
    Resolve(gUia.GetReservedMixedAttributeValue, "UiaGetReservedMixedAttributeValue");
    Resolve(gUia.DisconnectAllProviders, "UiaDisconnectAllProviders");
    return gUia.ReturnRawElementProvider != nullptr;
}

// ===== STATE =====

struct Bridge {
    AB::IdMap ids;
    int listener = 0;
};

Bridge gBridge;

UltraCanvasUIElement* Lookup(uint32_t id) {
    UltraCanvasUIElement* element = gBridge.ids.ElementOf(id);
    return element && AB::IsLive(element) ? element : nullptr;
}

HWND HwndOf(UltraCanvasUIElement* element) {
    UltraCanvasWindowBase* window = AB::WindowOf(element);
    return window ? static_cast<HWND>(window->GetNativeHandle()) : nullptr;
}

// ===== VALUES =====

BSTR ToBstr(const std::string& utf8) {
    const std::wstring wide = Utf8ToWide(utf8);
    return SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
}

void SetBool(VARIANT* v, bool value) {
    v->vt = VT_BOOL;
    v->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetInt(VARIANT* v, int value) {
    v->vt = VT_I4;
    v->lVal = value;
}

void SetString(VARIANT* v, const std::string& utf8) {
    v->vt = VT_BSTR;
    v->bstrVal = ToBstr(utf8);
}

void SetReserved(VARIANT* v, HRESULT (WINAPI* source)(IUnknown**)) {
    IUnknown* reserved = nullptr;
    if (source && SUCCEEDED(source(&reserved)) && reserved) {
        v->vt = VT_UNKNOWN;
        v->punkVal = reserved;
    } else {
        v->vt = VT_EMPTY;
    }
}

// A COLORREF (0x00BBGGRR) from "#RRGGBB", or -1.
long ColorRef(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return -1;
    const long rgb = std::strtol(hex.c_str() + 1, nullptr, 16);
    return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

SAFEARRAY* ProviderArray(const std::vector<IUnknown*>& providers) {
    SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(providers.size()));
    if (!array) return nullptr;
    for (LONG i = 0; i < static_cast<LONG>(providers.size()); i++) {
        SafeArrayPutElement(array, &i, providers[i]);   // AddRefs
    }
    return array;
}

// ===== TEXT UNITS =====

struct Span {
    int start = 0;
    int end = 0;
};

// The unit [start, end) containing `offset` (the last character's at the end).
Span UnitAt(IAccessibleText* text, int offset, TextUnit unit) {
    const int count = text->GetCharacterCount();
    if (count == 0 || unit == TextUnit_Document || unit == TextUnit_Page) return {0, count};
    offset = std::clamp(offset, 0, count - 1);
    Span span;
    switch (unit) {
        case TextUnit_Character:
            return {offset, offset + 1};
        case TextUnit_Format:
            text->GetAttributesAt(offset, span.start, span.end);
            break;
        case TextUnit_Word:
            text->GetTextAtOffset(offset, AccessibleTextBoundary::Word, span.start, span.end);
            break;
        case TextUnit_Line:
            text->GetTextAtOffset(offset, AccessibleTextBoundary::Line, span.start, span.end);
            break;
        default:   // Paragraph
            text->GetTextAtOffset(offset, AccessibleTextBoundary::Paragraph, span.start, span.end);
            break;
    }
    // Never an empty or backwards unit, whatever the element reported.
    if (span.end <= span.start || span.start > offset || span.end <= offset) return {offset, offset + 1};
    return span;
}

int NextBoundary(IAccessibleText* text, int position, TextUnit unit) {
    const int count = text->GetCharacterCount();
    if (position >= count) return count;
    return std::max(UnitAt(text, position, unit).end, position + 1);
}

int PreviousBoundary(IAccessibleText* text, int position, TextUnit unit) {
    if (position <= 0) return 0;
    const Span span = UnitAt(text, position - 1, unit);
    return std::min(span.start, position - 1);
}

// ===== PROVIDERS =====

class ElementProvider;
IRawElementProviderSimple* ProviderFor(UltraCanvasUIElement* element);

class TextRange final : public ITextRangeProvider {
public:
    TextRange(uint32_t elementId, int start, int end) : id(elementId), start(start), end(end) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ITextRangeProvider) || riid == kTextRangeIid) {
            *out = static_cast<ITextRangeProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs;
        if (left == 0) delete this;
        return left;
    }

    // ITextRangeProvider
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = new TextRange(id, start, end);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range, BOOL* out) override {
        if (!out) return E_POINTER;
        TextRange* other = Ours(range);
        *out = other && other->id == id && other->start == start && other->end == end;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
                                               TextPatternRangeEndpoint targetEndpoint, int* out) override {
        if (!out) return E_POINTER;
        TextRange* other = Ours(target);
        if (!other || other->id != id) return E_INVALIDARG;
        *out = Endpoint(endpoint) - other->Endpoint(targetEndpoint);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override {
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        const int count = text->GetCharacterCount();
        if (unit == TextUnit_Document || unit == TextUnit_Page || count == 0) {
            start = 0;
            end = count;
            return S_OK;
        }
        const Span span = UnitAt(text, std::min(start, count - 1), unit);
        start = span.start;
        end = span.end;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;   // not found: a valid answer
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE FindText(BSTR needle, BOOL backward, BOOL ignoreCase, ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        std::wstring haystack = Utf8ToWide(AB::Substring(text->GetAccessibleText(), start, end));
        std::wstring wanted(needle ? needle : L"", needle ? SysStringLen(needle) : 0);
        if (wanted.empty()) return S_OK;
        if (ignoreCase) {
            for (auto& c : haystack) c = static_cast<wchar_t>(std::towlower(c));
            for (auto& c : wanted) c = static_cast<wchar_t>(std::towlower(c));
        }
        const size_t at = backward ? haystack.rfind(wanted) : haystack.find(wanted);
        if (at == std::wstring::npos) return S_OK;
        // UTF-16 index -> character offset.
        const std::string before = WideToUtf8(haystack.substr(0, at));
        const std::string match = WideToUtf8(haystack.substr(at, wanted.size()));
        const int from = start + UltraCanvasAccessibility::CharacterCount(before);
        *out = new TextRange(id, from, from + UltraCanvasAccessibility::CharacterCount(match));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        if (attribute == kIsReadOnly) {
            SetBool(out, text->IsReadOnly());
            return S_OK;
        }
        if (!Supported(attribute)) {
            SetReserved(out, gUia.GetReservedNotSupportedValue);
            return S_OK;
        }
        // The value over the whole range, or "mixed" where its runs differ.
        const int count = text->GetCharacterCount();
        int offset = std::min(start, std::max(0, count - 1));
        const int stop = std::max(end, offset + 1);
        bool first = true;
        VARIANT value;
        VariantInit(&value);
        while (offset < stop && offset < count) {
            int runStart = 0, runEnd = 0;
            const AccessibleTextAttributes attributes = text->GetAttributesAt(offset, runStart, runEnd);
            VARIANT here;
            VariantInit(&here);
            Value(attribute, attributes, &here);
            if (first) {
                VariantCopy(&value, &here);
                first = false;
            } else if (VarCmp(&value, &here, LOCALE_INVARIANT, 0) != static_cast<HRESULT>(VARCMP_EQ)) {
                VariantClear(&here);
                VariantClear(&value);
                SetReserved(out, gUia.GetReservedMixedAttributeValue);
                return S_OK;
            }
            VariantClear(&here);
            offset = std::max(runEnd, offset + 1);
        }
        if (first) SetReserved(out, gUia.GetReservedNotSupportedValue);
        else *out = value;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        IAccessibleText* text = element ? element->GetAccessibleTextInterface() : nullptr;
        if (!text) return kElementNotAvailable;
        // One rectangle per line the range covers.
        std::vector<double> coordinates;
        int position = start;
        const int count = text->GetCharacterCount();
        for (int lines = 0; position < std::min(end, count) && lines < 500; lines++) {
            const Span line = UnitAt(text, position, TextUnit_Line);
            const int to = std::min(line.end, end);
            const Rect2Df box = AB::RangeBounds(text, position, to);
            if (box.width > 0 || box.height > 0) {
                const AB::ScreenRect r = AB::WindowRectToScreen(AB::WindowOf(element), box);
                coordinates.insert(coordinates.end(), {double(r.x), double(r.y), double(r.width), double(r.height)});
            }
            position = std::max(to, position + 1);
        }
        SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(coordinates.size()));
        if (!array) return E_OUTOFMEMORY;
        for (LONG i = 0; i < static_cast<LONG>(coordinates.size()); i++) SafeArrayPutElement(array, &i, &coordinates[i]);
        *out = array;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        UltraCanvasUIElement* element = Lookup(id);
        *out = element ? ProviderFor(element) : nullptr;
        return element ? S_OK : kElementNotAvailable;
    }

    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* out) override {
        if (!out) return E_POINTER;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        std::wstring wide = Utf8ToWide(AB::Substring(text->GetAccessibleText(), start, end));
        if (maxLength >= 0 && wide.size() > static_cast<size_t>(maxLength)) wide.resize(maxLength);
        *out = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* out) override {
        if (!out) return E_POINTER;
        *out = 0;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        const bool degenerate = start == end;
        int position = start;
        if (!degenerate && count > 0) {
            // Moving forward starts from the unit after the one we are in.
            position = UnitAt(text, start, unit).start;
        }
        for (int i = 0; i < std::abs(count); i++) {
            const int next = count > 0 ? NextBoundary(text, position, unit) : PreviousBoundary(text, position, unit);
            if (next == position || (count > 0 && next >= text->GetCharacterCount() && !degenerate)) {
                if (next != position && degenerate) { position = next; (*out)++; }
                break;
            }
            position = next;
            (*out)++;
        }
        if (degenerate) {
            start = end = position;
        } else {
            const Span span = UnitAt(text, position, unit);
            start = span.start;
            end = span.end;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
                                                 int* out) override {
        if (!out) return E_POINTER;
        *out = 0;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        int position = Endpoint(endpoint);
        for (int i = 0; i < std::abs(count); i++) {
            const int next = count > 0 ? NextBoundary(text, position, unit) : PreviousBoundary(text, position, unit);
            if (next == position) break;
            position = next;
            (*out)++;
        }
        SetEndpoint(endpoint, position);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
                                                  TextPatternRangeEndpoint targetEndpoint) override {
        TextRange* other = Ours(target);
        if (!other || other->id != id) return E_INVALIDARG;
        SetEndpoint(endpoint, other->Endpoint(targetEndpoint));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override {
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        return text->SetSelection(start, end) ? S_OK : E_FAIL;
    }

    // One selection only (SupportedTextSelection_Single).
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return S_OK;
    }

private:
    std::atomic<ULONG> refs{1};
    uint32_t id;
    int start;
    int end;

    IAccessibleText* Text() const {
        UltraCanvasUIElement* element = Lookup(id);
        return element ? element->GetAccessibleTextInterface() : nullptr;
    }

    static TextRange* Ours(ITextRangeProvider* range) {
        if (!range) return nullptr;
        void* ours = nullptr;
        if (FAILED(range->QueryInterface(kTextRangeIid, &ours)) || !ours) return nullptr;
        auto* result = static_cast<TextRange*>(static_cast<ITextRangeProvider*>(ours));
        result->Release();   // the caller still holds `range`
        return result;
    }

    int Endpoint(TextPatternRangeEndpoint endpoint) const {
        return endpoint == TextPatternRangeEndpoint_Start ? start : end;
    }

    void SetEndpoint(TextPatternRangeEndpoint endpoint, int position) {
        // Moving one end past the other drags it along.
        if (endpoint == TextPatternRangeEndpoint_Start) {
            start = position;
            if (end < start) end = start;
        } else {
            end = position;
            if (start > end) start = end;
        }
    }

    static bool Supported(TEXTATTRIBUTEID attribute) {
        switch (attribute) {
            case kFontName: case kFontSize: case kFontWeight: case kForegroundColor: case kBackgroundColor:
            case kIsItalic: case kIsSubscript: case kIsSuperscript: case kStrikethroughStyle:
            case kUnderlineStyle: case kStyleId: case kAnnotationTypes:
                return true;
            default:
                return false;
        }
    }

    static void Value(TEXTATTRIBUTEID attribute, const AccessibleTextAttributes& a, VARIANT* out) {
        switch (attribute) {
            case kFontName: SetString(out, a.fontFamily); break;
            case kFontSize: out->vt = VT_R8; out->dblVal = a.fontSizePt; break;
            case kFontWeight: SetInt(out, a.bold ? FW_BOLD : FW_NORMAL); break;
            case kForegroundColor: SetInt(out, static_cast<int>(std::max(0L, ColorRef(a.color)))); break;
            case kBackgroundColor: {
                const long color = ColorRef(a.backgroundColor);
                SetInt(out, static_cast<int>(color < 0 ? 0xFFFFFF : color));
                break;
            }
            case kIsItalic: SetBool(out, a.italic); break;
            case kIsSubscript: SetBool(out, a.subscript); break;
            case kIsSuperscript: SetBool(out, a.superscript); break;
            case kStrikethroughStyle: SetInt(out, a.strikethrough || a.deleted ? 1 : 0); break;   // TextDecorationLineStyle_Single
            case kUnderlineStyle: SetInt(out, a.underline || a.inserted ? 1 : 0); break;
            case kStyleId:
                SetInt(out, a.headingLevel >= 1 && a.headingLevel <= 9 ? kStyleHeading1 + a.headingLevel - 1
                           : a.listItem ? kStyleBulletedList : kStyleNormal);
                break;
            case kAnnotationTypes: {
                std::vector<int> types;
                if (a.misspelled) types.push_back(kAnnotationSpellingError);
                if (a.commented) types.push_back(kAnnotationComment);
                if (a.inserted) types.push_back(kAnnotationInsertionChange);
                if (a.deleted) types.push_back(kAnnotationDeletionChange);
                SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(types.size()));
                for (LONG i = 0; array && i < static_cast<LONG>(types.size()); i++) SafeArrayPutElement(array, &i, &types[i]);
                out->vt = VT_ARRAY | VT_I4;
                out->parray = array;
                break;
            }
            default: break;
        }
    }
};

class ElementProvider final : public IRawElementProviderSimple,
                        public IRawElementProviderFragment,
                        public IRawElementProviderFragmentRoot,
                        public ITextProvider {
public:
    explicit ElementProvider(uint32_t elementId) : id(elementId) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragment)) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragmentRoot) && element && AB::AsWindow(element)) {
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (riid == __uuidof(ITextProvider) && element && element->GetAccessibleTextInterface()) {
            *out = static_cast<ITextProvider*>(this);
        }
        if (!*out) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs;
        if (left == 0) delete this;
        return left;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        if (pattern == UIA_TextPatternId && element->GetAccessibleTextInterface()) {
            *out = static_cast<ITextProvider*>(this);
            AddRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        const bool window = AB::AsWindow(element) != nullptr;
        IAccessibleText* text = element->GetAccessibleTextInterface();
        switch (property) {
            case UIA_ControlTypePropertyId: SetInt(out, ControlType(element)); break;
            case UIA_NamePropertyId: SetString(out, AB::Name(element)); break;
            case UIA_AutomationIdPropertyId: SetString(out, element->GetIdentifier()); break;
            case UIA_FrameworkIdPropertyId: SetString(out, "UltraCanvas"); break;
            case UIA_ClassNamePropertyId: SetString(out, window ? "UltraCanvasWindow" : "UltraCanvasElement"); break;
            case UIA_IsEnabledPropertyId: SetBool(out, !element->IsDisabled()); break;
            case UIA_IsKeyboardFocusablePropertyId: SetBool(out, element->AcceptsFocus()); break;
            case UIA_HasKeyboardFocusPropertyId: SetBool(out, element->IsFocused()); break;
            case UIA_IsOffscreenPropertyId: SetBool(out, !element->IsVisible()); break;
            case UIA_IsTextPatternAvailablePropertyId: SetBool(out, text != nullptr); break;
            case UIA_IsPasswordPropertyId: SetBool(out, false); break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                // Elements that never described themselves and hold nothing
                // are layout, not content: keep them out of the reader's way.
                SetBool(out, window || element->GetAccessibleRole() != AccessibleRole::Unknown ||
                             !AB::Children(element).empty());
                break;
            default: break;   // VT_EMPTY: let UI Automation answer
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (element && AB::AsWindow(element) && gUia.HostProviderFromHwnd) {
            return gUia.HostProviderFromHwnd(HwndOf(element), out);
        }
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        UltraCanvasUIElement* target = nullptr;
        // A window is its own fragment root: UI Automation reaches its parent
        // and siblings through the HWND, not through us.
        const bool window = AB::AsWindow(element) != nullptr;
        switch (direction) {
            case NavigateDirection_Parent:
                if (!window) target = AB::Parent(element);
                break;
            case NavigateDirection_FirstChild:
            case NavigateDirection_LastChild: {
                const auto children = AB::Children(element);
                if (!children.empty()) target = direction == NavigateDirection_FirstChild ? children.front() : children.back();
                break;
            }
            case NavigateDirection_NextSibling:
            case NavigateDirection_PreviousSibling: {
                if (window) break;
                const auto siblings = AB::Children(AB::Parent(element));
                const auto it = std::find(siblings.begin(), siblings.end(), element);
                if (it == siblings.end()) break;
                if (direction == NavigateDirection_NextSibling && it + 1 != siblings.end()) target = *(it + 1);
                if (direction == NavigateDirection_PreviousSibling && it != siblings.begin()) target = *(it - 1);
                break;
            }
            default: break;
        }
        if (target) *out = FragmentFor(target);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        if (AB::AsWindow(element)) return S_OK;   // the HWND's provider supplies the root's id
        int parts[2] = {kAppendRuntimeId, static_cast<int>(id)};
        SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!array) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; i++) SafeArrayPutElement(array, &i, &parts[i]);
        *out = array;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        *out = UiaRect{0, 0, 0, 0};
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        if (AB::AsWindow(element)) return S_OK;   // the HWND's provider supplies the root's bounds
        const AB::ScreenRect r = AB::ScreenBounds(element);
        *out = UiaRect{double(r.x), double(r.y), double(r.width), double(r.height)};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        if (!element->CanReceiveFocus()) return S_OK;
        if (UltraCanvasWindowBase* window = element->GetWindow()) window->SetFocusedElement(element);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        UltraCanvasWindowBase* window = AB::WindowOf(element);
        if (!window) return S_OK;
        IRawElementProviderFragment* root = FragmentFor(window);
        HRESULT hr = root->QueryInterface(__uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void**>(out));
        root->Release();
        return hr;
    }

    // IRawElementProviderFragmentRoot (windows only)
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        if (!element) return kElementNotAvailable;
        UltraCanvasUIElement* hit = AB::HitTest(element, static_cast<int>(x), static_cast<int>(y));
        if (hit && hit != element) *out = FragmentFor(hit);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasWindowBase* window = AB::AsWindow(Lookup(id));
        if (!window) return kElementNotAvailable;
        UltraCanvasUIElement* focused = window->GetFocusedElement();
        if (focused && focused != window) *out = FragmentFor(focused);
        return S_OK;
    }

    // ITextProvider
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        int start = 0, end = 0;
        if (!text->GetSelection(start, end)) start = end = text->GetCaretOffset();
        if (start > end) std::swap(start, end);
        TextRange* range = new TextRange(id, start, end);
        *out = ProviderArray({range});
        range->Release();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        TextRange* range = new TextRange(id, 0, text->GetCharacterCount());
        *out = ProviderArray({range});
        range->Release();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return E_INVALIDARG;   // text elements have no embedded children
    }

    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UltraCanvasUIElement* element = Lookup(id);
        IAccessibleText* text = element ? element->GetAccessibleTextInterface() : nullptr;
        if (!text) return kElementNotAvailable;
        const Point2Df at = AB::ScreenToWindow(AB::WindowOf(element), static_cast<int>(point.x), static_cast<int>(point.y));
        const int offset = std::max(0, AB::CharacterAtPoint(text, at));
        *out = new TextRange(id, offset, offset);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        IAccessibleText* text = Text();
        if (!text) return kElementNotAvailable;
        *out = new TextRange(id, 0, text->GetCharacterCount());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* out) override {
        if (!out) return E_POINTER;
        *out = SupportedTextSelection_Single;
        return S_OK;
    }

    static IRawElementProviderFragment* FragmentFor(UltraCanvasUIElement* element) {
        return static_cast<IRawElementProviderFragment*>(new ElementProvider(gBridge.ids.IdOf(element)));
    }

private:
    std::atomic<ULONG> refs{1};
    uint32_t id;

    IAccessibleText* Text() const {
        UltraCanvasUIElement* element = Lookup(id);
        return element ? element->GetAccessibleTextInterface() : nullptr;
    }

    static int ControlType(UltraCanvasUIElement* element) {
        if (AB::AsWindow(element)) return UIA_WindowControlTypeId;
        switch (element->GetAccessibleRole()) {
            case AccessibleRole::Window:    return UIA_WindowControlTypeId;
            case AccessibleRole::Button:    return UIA_ButtonControlTypeId;
            case AccessibleRole::CheckBox:  return UIA_CheckBoxControlTypeId;
            case AccessibleRole::Label:     return UIA_TextControlTypeId;
            case AccessibleRole::TextField: return UIA_EditControlTypeId;
            case AccessibleRole::TextArea:  return UIA_EditControlTypeId;
            case AccessibleRole::Document:  return UIA_DocumentControlTypeId;
            case AccessibleRole::List:      return UIA_ListControlTypeId;
            case AccessibleRole::ListItem:  return UIA_ListItemControlTypeId;
            case AccessibleRole::Table:     return UIA_TableControlTypeId;
            case AccessibleRole::Image:     return UIA_ImageControlTypeId;
            case AccessibleRole::Link:      return UIA_HyperlinkControlTypeId;
            case AccessibleRole::Menu:      return UIA_MenuControlTypeId;
            case AccessibleRole::MenuItem:  return UIA_MenuItemControlTypeId;
            case AccessibleRole::Unknown:   break;
        }
        return AB::Children(element).empty() ? UIA_CustomControlTypeId : UIA_PaneControlTypeId;
    }
};

IRawElementProviderSimple* ProviderFor(UltraCanvasUIElement* element) {
    return static_cast<IRawElementProviderSimple*>(new ElementProvider(gBridge.ids.IdOf(element)));
}

// ===== EVENTS =====

void Raise(UltraCanvasUIElement* element, EVENTID event) {
    if (!element || !gUia.RaiseAutomationEvent) return;
    if (gUia.ClientsAreListening && !gUia.ClientsAreListening()) return;
    IRawElementProviderSimple* provider = ProviderFor(element);
    gUia.RaiseAutomationEvent(provider, event);
    provider->Release();
}

void OnAccessibilityEvent(const AccessibilityEvent& event) {
    UltraCanvasUIElement* element = event.element;
    switch (event.type) {
        case AccessibilityEventType::ElementDestroyed:
            gBridge.ids.Forget(element);
            break;
        case AccessibilityEventType::FocusChanged:
            Raise(element, UIA_AutomationFocusChangedEventId);
            break;
        case AccessibilityEventType::TextChanged:
            Raise(element, UIA_Text_TextChangedEventId);
            break;
        case AccessibilityEventType::CaretMoved:
        case AccessibilityEventType::SelectionChanged:
            // UI Automation reports the caret as a (degenerate) selection.
            Raise(element, UIA_Text_TextSelectionChangedEventId);
            break;
        case AccessibilityEventType::NameChanged:
            break;   // read again on the next property request
    }
}

} // namespace

LRESULT HandleGetObject(UltraCanvasWindowBase* window, HWND hwnd, WPARAM wParam, LPARAM lParam, bool& handled) {
    handled = false;
    if (!window || static_cast<long>(lParam) != kRootObjectId || !LoadUia()) return 0;
    // A client is here: from now on the elements' changes are worth reporting.
    if (!gBridge.listener) gBridge.listener = UltraCanvasAccessibility::AddListener(OnAccessibilityEvent);
    IRawElementProviderSimple* root = ProviderFor(window);
    const LRESULT result = gUia.ReturnRawElementProvider(hwnd, wParam, lParam, root);
    root->Release();
    handled = true;
    return result;
}

void WindowDestroyed(HWND hwnd) {
    if (gUia.ReturnRawElementProvider) gUia.ReturnRawElementProvider(hwnd, 0, 0, nullptr);
}

void Shutdown() {
    if (gBridge.listener) UltraCanvasAccessibility::RemoveListener(gBridge.listener);
    gBridge.listener = 0;
    if (gUia.DisconnectAllProviders) gUia.DisconnectAllProviders();
    gBridge.ids.Clear();
}

} // namespace WindowsAccessibility
} // namespace UltraCanvas

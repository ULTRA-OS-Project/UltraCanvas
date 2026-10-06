// Apps/UltraWeb/host/UltraWebGuest.cpp
// The host side of the UltraWeb element ABI (UltraWeb/guest/ultraweb.h):
// a handle table over real UltraCanvas elements, the eleven imports, and
// the element callbacks that become uc_event calls.
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebGuest.h"

#include "ultraweb.h"   // UltraWeb/guest: the ids both sides share

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

constexpr uint32_t kMaxKindBytes = 64;
constexpr uint32_t kMaxLogBytes = 4096;
constexpr double kMaxLength = 100000.0;   // px; anything larger is a guest bug
constexpr float kInputWidth = 240.0f;     // a text input has no intrinsic width
constexpr float kInputHeight = 28.0f;

bool IsContainerKind(const std::string& kind) { return kind == UC_KIND_CONTAINER || kind == "Root"; }

bool InRange(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }

CSSLayout::Dimension Length(double px) {
    return px > 0 ? CSSLayout::Dimension::Px(static_cast<float>(px)) : CSSLayout::Dimension::Auto();
}

WasmValue I32(int32_t v) { return WasmI32(v); }

} // namespace

UltraWebGuest::UltraWebGuest(GuestOptions options) : options_(std::move(options)) {}

std::unique_ptr<UltraWebGuest> UltraWebGuest::Start(const std::vector<uint8_t>& module,
                                                    std::shared_ptr<UltraCanvasContainer> root,
                                                    GuestOptions options, std::string& error) {
    if (!root) { error = "no root container"; return nullptr; }
    std::unique_ptr<UltraWebGuest> guest(new UltraWebGuest(std::move(options)));
    guest->entries_.resize(UC_ROOT_HANDLE + 1);
    Entry& rootEntry = guest->entries_[UC_ROOT_HANDLE];
    rootEntry.element = root;
    rootEntry.kind = "Root";
    rootEntry.live = true;

    WasmStatus status;
    guest->instance_ = UltraCanvasWasmInstance::Create(module, guest->MakeImports(), guest->options_.limits, status);
    if (!guest->instance_) { error = status.message; return nullptr; }

    if (guest->instance_->HasFunction("uc_abi_version")) {
        std::vector<WasmValue> results;
        status = guest->instance_->Call("uc_abi_version", {}, &results);
        if (!status.ok || results.size() != 1) { error = "uc_abi_version: " + status.message; return nullptr; }
        if (results[0].i32 > UC_ABI_VERSION) {
            error = "the app needs element ABI version " + std::to_string(results[0].i32)
                    + "; this UltraWeb has version " + std::to_string(UC_ABI_VERSION);
            return nullptr;
        }
    }
    if (!guest->instance_->HasFunction("uc_main")) {
        error = "the module exports no uc_main, so it is not an UltraWeb app";
        return nullptr;
    }
    guest->inGuest_ = true;
    status = guest->instance_->Call("uc_main", {});
    guest->inGuest_ = false;
    if (!status.ok) { error = status.message; return nullptr; }
    return guest;
}

UltraWebGuest::~UltraWebGuest() {
    // Callbacks first, so nothing calls back into a guest being destroyed;
    // then the root's children - everything else hangs below them.
    for (uint32_t h = UC_ROOT_HANDLE + 1; h < entries_.size(); ++h) {
        Entry& entry = entries_[h];
        if (!entry.live) continue;
        if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(entry.element)) button->onClick = nullptr;
        if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(entry.element)) {
            input->onTextChanged = nullptr;
            input->onEnterPressed = nullptr;
        }
        if (auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(entry.element)) box->onStateChanged = nullptr;
    }
    if (entries_.size() > UC_ROOT_HANDLE) {
        auto root = std::dynamic_pointer_cast<UltraCanvasContainer>(entries_[UC_ROOT_HANDLE].element);
        if (root) {
            for (uint32_t child : entries_[UC_ROOT_HANDLE].children) {
                if (child < entries_.size() && entries_[child].live) root->RemoveChild(entries_[child].element);
            }
            root->InvalidateLayout();
            root->RequestRedraw();
        }
    }
}

size_t UltraWebGuest::ElementCount() const { return liveCount_; }

std::shared_ptr<UltraCanvasUIElement> UltraWebGuest::ElementForTest(uint32_t handle) const {
    const Entry* entry = Find(handle);
    return entry ? entry->element : nullptr;
}

// ===== IMPORTS =====

std::vector<WasmImport> UltraWebGuest::MakeImports() {
    using T = WasmValueType;
    std::vector<WasmImport> imports;
    auto add = [&imports](const char* name, std::vector<T> params, std::vector<T> results, WasmHostFunction fn) {
        imports.push_back({"ultracanvas", name, std::move(params), std::move(results), std::move(fn)});
    };
    // `this` outlives the instance: the instance is a member, destroyed first.
    add("uc_create", {T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        std::string kind;
        const uint32_t length = uint32_t(a[1].i32);
        if (length > kMaxKindBytes || !c.ReadMemory(uint32_t(a[0].i32), length, kind)) { r[0].i32 = int32_t(UC_NO_HANDLE); return; }
        r[0].i32 = int32_t(Create(kind));
    });
    add("uc_release", {T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = Release(uint32_t(a[0].i32));
    });
    add("uc_insert", {T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = Insert(uint32_t(a[0].i32), uint32_t(a[1].i32), uint32_t(a[2].i32));
    });
    add("uc_remove", {T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = Remove(uint32_t(a[0].i32));
    });
    add("uc_set_text", {T::I32, T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        const uint32_t length = uint32_t(a[3].i32);
        if (length > options_.maxTextBytes) { r[0].i32 = UC_ERR_LIMIT; return; }
        std::string value;
        if (!c.ReadMemory(uint32_t(a[2].i32), length, value)) { r[0].i32 = UC_ERR_MEMORY; return; }
        r[0].i32 = SetText(uint32_t(a[0].i32), uint32_t(a[1].i32), value);
    });
    add("uc_set_number", {T::I32, T::I32, T::F64}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = SetNumber(uint32_t(a[0].i32), uint32_t(a[1].i32), a[2].f64);
    });
    add("uc_get_text", {T::I32, T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        std::string value;
        const int32_t result = GetText(uint32_t(a[0].i32), uint32_t(a[1].i32), value);
        if (result != UC_OK) { r[0].i32 = result; return; }
        const uint32_t capacity = uint32_t(a[3].i32);
        const uint32_t copied = uint32_t(std::min<size_t>(value.size(), capacity));
        if (!c.WriteMemory(uint32_t(a[2].i32), value.data(), copied)) { r[0].i32 = UC_ERR_MEMORY; return; }
        r[0].i32 = int32_t(std::min<size_t>(value.size(), size_t(std::numeric_limits<int32_t>::max())));
    });
    add("uc_get_number", {T::I32, T::I32}, {T::F64}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].f64 = GetNumber(uint32_t(a[0].i32), uint32_t(a[1].i32));
    });
    add("uc_listen", {T::I32, T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = Listen(uint32_t(a[0].i32), uint32_t(a[1].i32));
    });
    add("uc_bounds", {T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        float box[4] = {0, 0, 0, 0};
        const int32_t result = Bounds(uint32_t(a[0].i32), box);
        if (result != UC_OK) { r[0].i32 = result; return; }
        r[0].i32 = c.WriteMemory(uint32_t(a[1].i32), box, sizeof(box)) ? UC_OK : UC_ERR_MEMORY;
    });
    add("uc_log", {T::I32, T::I32}, {}, [this](WasmCaller& c, const WasmValue* a, WasmValue*) {
        std::string line;
        const uint32_t length = std::min(uint32_t(a[1].i32), kMaxLogBytes);
        if (c.ReadMemory(uint32_t(a[0].i32), length, line) && options_.onLog) options_.onLog(line);
    });
    return imports;
}

// ===== HANDLES =====

UltraWebGuest::Entry* UltraWebGuest::Find(uint32_t handle) {
    if (handle == UC_NO_HANDLE || handle >= entries_.size() || !entries_[handle].live) return nullptr;
    return &entries_[handle];
}

const UltraWebGuest::Entry* UltraWebGuest::Find(uint32_t handle) const {
    if (handle == UC_NO_HANDLE || handle >= entries_.size() || !entries_[handle].live) return nullptr;
    return &entries_[handle];
}

bool UltraWebGuest::IsAncestor(uint32_t ancestor, uint32_t handle) const {
    for (uint32_t h = handle; h != UC_NO_HANDLE; h = entries_[h].parent) {
        if (h == ancestor) return true;
    }
    return false;
}

uint32_t UltraWebGuest::Create(const std::string& kind) {
    if (liveCount_ >= options_.maxElements) return UC_NO_HANDLE;
    uint32_t handle;
    if (!freeHandles_.empty()) {
        handle = freeHandles_.back();
        freeHandles_.pop_back();
    } else {
        handle = uint32_t(entries_.size());
        entries_.emplace_back();
    }
    const std::string id = "uw" + std::to_string(handle);
    std::shared_ptr<UltraCanvasUIElement> element;
    // Zero sizes leave both dimensions automatic: each element measures its
    // own content, and the guest sets UC_PROP_WIDTH / HEIGHT when it wants.
    if (kind == UC_KIND_CONTAINER) {
        auto container = CreateContainer(id, 0, 0, 0, 0);
        container->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        ContainerStyle plain;
        plain.autoShowScrollbars = false;
        container->SetContainerStyle(plain);
        element = container;
    } else if (kind == UC_KIND_LABEL) {
        // Text wraps at the width the layout gives it, as it does on a page.
        auto label = CreateLabel(id, "");
        label->SetWrap(TextWrap::WrapWord);
        element = label;
    } else if (kind == UC_KIND_BUTTON) {
        element = CreateButton(id, 0, 0, 0, 0, "");
    } else if (kind == UC_KIND_TEXT_INPUT) {
        element = CreateTextInput(id, 0, 0, kInputWidth, kInputHeight);
    } else if (kind == UC_KIND_CHECKBOX) {
        element = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, 0, 0, "", false);
    }
    if (!element) {
        freeHandles_.push_back(handle);
        return UC_NO_HANDLE;
    }
    Entry& entry = entries_[handle];
    entry = Entry{};
    entry.element = element;
    entry.kind = kind;
    entry.live = true;
    ++liveCount_;
    WireCallbacks(handle);
    return handle;
}

int32_t UltraWebGuest::Release(uint32_t handle) {
    if (handle == UC_ROOT_HANDLE) return UC_ERR_STATE;
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    const std::vector<uint32_t> children = entry->children;   // Release edits the list
    for (uint32_t child : children) Release(child);
    Detach(handle);
    // The element may be the one whose callback is running this guest call
    // (a button that removes itself on click): keep it alive until the call
    // has unwound, and drop it on a later turn.
    std::shared_ptr<UltraCanvasUIElement> element = std::move(entry->element);
    *entry = Entry{};
    freeHandles_.push_back(handle);
    --liveCount_;
    if (element && options_.defer) options_.defer([element]() mutable { element.reset(); });
    return UC_OK;
}

void UltraWebGuest::Detach(uint32_t handle) {
    Entry& entry = entries_[handle];
    if (entry.parent == UC_NO_HANDLE) return;
    Entry& parent = entries_[entry.parent];
    auto container = std::dynamic_pointer_cast<UltraCanvasContainer>(parent.element);
    if (container) {
        container->RemoveChild(entry.element);
        container->InvalidateLayout();
        container->RequestRedraw();
    }
    parent.children.erase(std::remove(parent.children.begin(), parent.children.end(), handle), parent.children.end());
    entry.parent = UC_NO_HANDLE;
}

int32_t UltraWebGuest::Insert(uint32_t parentHandle, uint32_t childHandle, uint32_t beforeHandle) {
    Entry* parent = Find(parentHandle);
    Entry* child = Find(childHandle);
    if (!parent || !child) return UC_ERR_HANDLE;
    if (childHandle == UC_ROOT_HANDLE || !IsContainerKind(parent->kind)) return UC_ERR_STATE;
    // A child may not become its own ancestor.
    if (IsAncestor(childHandle, parentHandle)) return UC_ERR_STATE;
    if (beforeHandle != UC_NO_HANDLE) {
        const Entry* before = Find(beforeHandle);
        if (!before) return UC_ERR_HANDLE;
        if (before->parent != parentHandle || beforeHandle == childHandle) return UC_ERR_STATE;
    }
    auto container = std::dynamic_pointer_cast<UltraCanvasContainer>(parent->element);
    if (!container) return UC_ERR_STATE;

    Detach(childHandle);
    parent = &entries_[parentHandle];
    std::vector<uint32_t>& order = parent->children;
    auto at = beforeHandle == UC_NO_HANDLE ? order.end() : std::find(order.begin(), order.end(), beforeHandle);
    // The container appends only, so an insert before an existing child
    // takes the children from there to the end off and puts them back after.
    const std::vector<uint32_t> tail(at, order.end());
    for (uint32_t h : tail) container->RemoveChild(entries_[h].element);
    container->AddChild(entries_[childHandle].element);
    for (uint32_t h : tail) container->AddChild(entries_[h].element);
    order.insert(order.begin() + (at - order.begin()), childHandle);
    entries_[childHandle].parent = parentHandle;
    container->InvalidateLayout();
    container->RequestRedraw();
    return UC_OK;
}

int32_t UltraWebGuest::Remove(uint32_t childHandle) {
    if (childHandle == UC_ROOT_HANDLE) return UC_ERR_STATE;
    if (!Find(childHandle)) return UC_ERR_HANDLE;
    Detach(childHandle);
    return UC_OK;
}

// ===== PROPERTIES =====

int32_t UltraWebGuest::SetText(uint32_t handle, uint32_t property, const std::string& value) {
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    const std::shared_ptr<UltraCanvasUIElement>& element = entry->element;
    if (property == UC_PROP_TEXT) {
        if (auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element)) label->SetText(value);
        else if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element)) button->SetText(value);
        else if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element)) input->SetText(value);
        else if (auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element)) box->SetText(value);
        else return UC_ERR_PROPERTY;
    } else if (property == UC_PROP_PLACEHOLDER) {
        auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element);
        if (!input) return UC_ERR_PROPERTY;
        input->SetPlaceholder(value);
    } else {
        return UC_ERR_PROPERTY;
    }
    element->InvalidateLayout();
    element->RequestRedraw();
    return UC_OK;
}

int32_t UltraWebGuest::SetNumber(uint32_t handle, uint32_t property, double value) {
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    const std::shared_ptr<UltraCanvasUIElement>& element = entry->element;
    const bool isContainer = IsContainerKind(entry->kind);
    switch (property) {
        case UC_PROP_CHECKED: {
            auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element);
            if (!box) return UC_ERR_PROPERTY;
            box->SetChecked(value != 0);
            break;
        }
        case UC_PROP_ENABLED:
            if (handle == UC_ROOT_HANDLE) return UC_ERR_STATE;
            element->SetDisabled(value == 0);
            break;
        case UC_PROP_VISIBLE:
            if (handle == UC_ROOT_HANDLE) return UC_ERR_STATE;
            element->SetVisible(value != 0);
            break;
        case UC_PROP_WIDTH:
        case UC_PROP_HEIGHT:
            if (handle == UC_ROOT_HANDLE) return UC_ERR_STATE;
            if (!InRange(value, 0, kMaxLength)) return UC_ERR_LIMIT;
            if (property == UC_PROP_WIDTH) element->SetElementSize(Length(value), element->size.height);
            else element->SetElementSize(element->size.width, Length(value));
            break;
        case UC_PROP_GROW:
            if (handle == UC_ROOT_HANDLE) return UC_ERR_STATE;
            if (!InRange(value, 0, 1000)) return UC_ERR_LIMIT;
            element->layoutItem.SetFlexGrow(static_cast<float>(value));
            if (value > 0) element->layoutItem.SetFlexShrink(1.0f);
            break;
        case UC_PROP_DIRECTION:
            if (!isContainer) return UC_ERR_PROPERTY;
            if (value == UC_DIRECTION_ROW) element->layout.SetFlexRow();
            else if (value == UC_DIRECTION_COLUMN) element->layout.SetFlexColumn();
            else return UC_ERR_LIMIT;
            break;
        case UC_PROP_GAP:
            if (!isContainer) return UC_ERR_PROPERTY;
            if (!InRange(value, 0, kMaxLength)) return UC_ERR_LIMIT;
            element->layout.SetFlexGap(static_cast<float>(value));
            break;
        case UC_PROP_PADDING:
            if (!isContainer) return UC_ERR_PROPERTY;
            if (!InRange(value, 0, kMaxLength)) return UC_ERR_LIMIT;
            element->SetPadding(static_cast<float>(value));
            break;
        case UC_PROP_FONT_SIZE: {
            if (!InRange(value, 1, 500)) return UC_ERR_LIMIT;
            const float size = static_cast<float>(value);
            if (auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element)) label->SetFontSize(size);
            else if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element)) button->SetFontSize(size);
            else if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element)) input->SetFontSize(size);
            else return UC_ERR_PROPERTY;
            break;
        }
        case UC_PROP_TEXT_COLOR: {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element);
            if (!label) return UC_ERR_PROPERTY;
            if (!InRange(value, 0, 4294967295.0)) return UC_ERR_LIMIT;
            label->SetTextColor(Color::FromRGBA(static_cast<uint32_t>(value)));
            break;
        }
        case UC_PROP_BACKGROUND:
            if (!isContainer && !std::dynamic_pointer_cast<UltraCanvasLabel>(element)) return UC_ERR_PROPERTY;
            if (!InRange(value, 0, 4294967295.0)) return UC_ERR_LIMIT;
            element->SetBackgroundColor(Color::FromRGBA(static_cast<uint32_t>(value)));
            break;
        default:
            return UC_ERR_PROPERTY;
    }
    element->InvalidateLayout();
    element->RequestRedraw();
    return UC_OK;
}

int32_t UltraWebGuest::GetText(uint32_t handle, uint32_t property, std::string& out) {
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    const std::shared_ptr<UltraCanvasUIElement>& element = entry->element;
    if (property == UC_PROP_TEXT) {
        if (auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element)) out = label->GetText();
        else if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element)) out = button->GetText();
        else if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element)) out = input->GetText();
        else if (auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element)) out = box->GetText();
        else return UC_ERR_PROPERTY;
        return UC_OK;
    }
    if (property == UC_PROP_PLACEHOLDER) {
        auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element);
        if (!input) return UC_ERR_PROPERTY;
        out = input->GetPlaceholder();
        return UC_OK;
    }
    return UC_ERR_PROPERTY;
}

double UltraWebGuest::GetNumber(uint32_t handle, uint32_t property) {
    const double none = std::numeric_limits<double>::quiet_NaN();
    Entry* entry = Find(handle);
    if (!entry) return none;
    const std::shared_ptr<UltraCanvasUIElement>& element = entry->element;
    switch (property) {
        case UC_PROP_CHECKED: {
            auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element);
            return box ? (box->IsChecked() ? 1.0 : 0.0) : none;
        }
        case UC_PROP_ENABLED: return element->IsDisabled() ? 0.0 : 1.0;
        case UC_PROP_VISIBLE: return element->IsVisible() ? 1.0 : 0.0;
        // The laid-out size, which is what a guest positioning things needs.
        case UC_PROP_WIDTH:   return element->GetBounds().width;
        case UC_PROP_HEIGHT:  return element->GetBounds().height;
        default:              return none;
    }
}

// ===== EVENTS =====

int32_t UltraWebGuest::Listen(uint32_t handle, uint32_t mask) {
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    constexpr uint32_t known = UC_EVENT_CLICK | UC_EVENT_CHANGE | UC_EVENT_SUBMIT | UC_EVENT_TOGGLE;
    if (mask & ~known) return UC_ERR_PROPERTY;
    if (mask != 0 && !instance_->HasFunction("uc_event")) return UC_ERR_STATE;
    entry->listening = mask;
    return UC_OK;
}

int32_t UltraWebGuest::Bounds(uint32_t handle, float out[4]) {
    Entry* entry = Find(handle);
    if (!entry) return UC_ERR_HANDLE;
    const Rect2Df box = entry->element->GetBoundsInWindow();
    const Rect2Df root = entries_[UC_ROOT_HANDLE].element->GetBoundsInWindow();
    out[0] = box.x - root.x;
    out[1] = box.y - root.y;
    out[2] = box.width;
    out[3] = box.height;
    return UC_OK;
}

// The callbacks capture the guest raw, with the handle: the element owns the
// callback, the guest owns the element, and ~UltraWebGuest clears them.
void UltraWebGuest::WireCallbacks(uint32_t handle) {
    const std::shared_ptr<UltraCanvasUIElement>& element = entries_[handle].element;
    if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element)) {
        button->SetOnClick([this, handle]() { Deliver(handle, UC_EVENT_CLICK, 0); });
    } else if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(element)) {
        input->onTextChanged = [this, handle](const std::string&) { Deliver(handle, UC_EVENT_CHANGE, 0); };
        input->onEnterPressed = [this, handle](const std::string&) {
            const Entry* entry = Find(handle);
            if (!entry || !(entry->listening & UC_EVENT_SUBMIT)) return false;
            Deliver(handle, UC_EVENT_SUBMIT, 0);
            return true;
        };
    } else if (auto box = std::dynamic_pointer_cast<UltraCanvasCheckbox>(element)) {
        box->onStateChanged = [this, handle](CheckedState, CheckedState now) {
            Deliver(handle, UC_EVENT_TOGGLE, now == CheckedState::Checked ? 1 : 0);
        };
    }
}

void UltraWebGuest::Deliver(uint32_t handle, uint32_t event, int32_t detail) {
    // A change the guest made itself (uc_set_text on an input) is not news
    // to it, and calling back in while it runs would re-enter it.
    if (inGuest_ || failed_ || !instance_) return;
    const Entry* entry = Find(handle);
    if (!entry || !(entry->listening & event)) return;
    inGuest_ = true;
    const WasmStatus status = instance_->Call("uc_event", {I32(int32_t(handle)), I32(int32_t(event)), I32(detail)});
    inGuest_ = false;
    if (!status.ok) Fail(status.message);
}

void UltraWebGuest::Fail(const std::string& message) {
    if (failed_) return;
    failed_ = true;
    if (!options_.onFailure) return;
    auto report = options_.onFailure;
    if (options_.defer) options_.defer([report, message]() { report(message); });
    else report(message);
}

} // namespace UltraWeb

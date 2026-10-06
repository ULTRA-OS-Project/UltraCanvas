// Apps/UltraWeb/host/UltraWebGuest.cpp
// The host side of the UltraWeb element ABI (UltraWeb/guest/ultraweb.h):
// a handle table over real UltraCanvas elements, the element imports, the
// element callbacks that become uc_event calls, and (ABI v2) the app's
// timers, fetches, storage and clipboard.
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebGuest.h"

#include "ultraweb.h"   // UltraWeb/guest: the ids both sides share

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cctype>
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
constexpr size_t kMaxTimers = 256;
constexpr uint32_t kMinTimerMs = 4;
constexpr size_t kMaxOpenFetches = 16;
constexpr uint32_t kMaxHeaderNameBytes = 256;
// User actions: the events a guest may answer with a clipboard write.
constexpr uint32_t kUserActions = UC_EVENT_CLICK | UC_EVENT_CHANGE | UC_EVENT_SUBMIT | UC_EVENT_TOGGLE;

bool IsContainerKind(const std::string& kind) { return kind == UC_KIND_CONTAINER || kind == "Root"; }

bool InRange(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }

CSSLayout::Dimension Length(double px) {
    return px > 0 ? CSSLayout::Dimension::Px(static_cast<float>(px)) : CSSLayout::Dimension::Auto();
}

WasmValue I32(int32_t v) { return WasmI32(v); }

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// What a guest's (pointer, length) names, or false for memory it does not
// have - read into bytes for a body or a stored value.
bool ReadBytes(WasmCaller& c, uint32_t at, uint32_t length, std::vector<uint8_t>& out) {
    std::string bytes;
    if (!c.ReadMemory(at, length, bytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// Copies what fits of `data` to the guest and answers its full length, the
// way every "out, capacity" import does.
int32_t CopyOut(WasmCaller& c, uint32_t at, uint32_t capacity, const void* data, size_t size) {
    const uint32_t copied = uint32_t(std::min<size_t>(size, capacity));
    if (!c.WriteMemory(at, data, copied)) return UC_ERR_MEMORY;
    return int32_t(std::min<size_t>(size, size_t(std::numeric_limits<int32_t>::max())));
}

// The next id from `next` on that `used` does not hold; after the largest
// int32_t it starts again at 1.
template <class Map> int32_t NextId(int32_t& next, const Map& used) {
    for (;;) {
        const int32_t id = next;
        next = next == std::numeric_limits<int32_t>::max() ? 1 : next + 1;
        if (!used.count(id)) return id;
    }
}

} // namespace

UltraWebGuest::UltraWebGuest(GuestOptions options)
    : options_(std::move(options)), alive_(std::make_shared<int>(0)) {}

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
    guest->DrainAppEvents();
    return guest;
}

UltraWebGuest::~UltraWebGuest() {
    // Timers and requests first: what is already on its way finds the
    // guest gone (alive_) rather than calling into it.
    alive_.reset();
    for (auto& [id, timer] : timers_) {
        if (options_.services.stopTimer) options_.services.stopTimer(timer.hostId);
    }
    for (auto& [id, fetch] : fetches_) {
        if (fetch.cancel) fetch.cancel();
    }
    if (options_.services.storage) options_.services.storage->Flush();
    // Element callbacks next, so nothing calls back into a guest being
    // destroyed; then the root's children - everything else hangs below them.
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
    AddServiceImports(imports);
    return imports;
}

// ABI v2: timers, fetch, storage, clipboard.
void UltraWebGuest::AddServiceImports(std::vector<WasmImport>& imports) {
    using T = WasmValueType;
    auto add = [&imports](const char* name, std::vector<T> params, std::vector<T> results, WasmHostFunction fn) {
        imports.push_back({"ultracanvas", name, std::move(params), std::move(results), std::move(fn)});
    };
    add("uc_timer_start", {T::I32, T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = StartTimer(uint32_t(a[0].i32), a[1].i32 != 0);
    });
    add("uc_timer_stop", {T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = StopTimer(a[0].i32);
    });

    add("uc_fetch", {T::I32, T::I32, T::I32, T::I32, T::I32, T::I32, T::I32}, {T::I32},
        [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
            const uint32_t urlLength = uint32_t(a[1].i32), bodyLength = uint32_t(a[4].i32), typeLength = uint32_t(a[6].i32);
            if (urlLength > FetchRules::kMaxUrlBytes || bodyLength > FetchRules::kMaxRequestBytes
                || typeLength > FetchRules::kMaxContentTypeBytes) { r[0].i32 = UC_ERR_LIMIT; return; }
            std::string url, contentType;
            std::vector<uint8_t> body;
            if (!c.ReadMemory(uint32_t(a[0].i32), urlLength, url) || !ReadBytes(c, uint32_t(a[3].i32), bodyLength, body)
                || !c.ReadMemory(uint32_t(a[5].i32), typeLength, contentType)) { r[0].i32 = UC_ERR_MEMORY; return; }
            r[0].i32 = StartFetch(url, uint32_t(a[2].i32), std::move(body), std::move(contentType));
        });
    add("uc_fetch_status", {T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        auto at = fetches_.find(a[0].i32);
        r[0].i32 = at == fetches_.end() ? UC_ERR_NOT_FOUND : at->second.status;
    });
    add("uc_fetch_body", {T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        int32_t result = UC_OK;
        const Fetch* fetch = FinishedFetch(a[0].i32, result);
        r[0].i32 = fetch ? CopyOut(c, uint32_t(a[1].i32), uint32_t(a[2].i32), fetch->body.data(), fetch->body.size()) : result;
    });
    add("uc_fetch_header", {T::I32, T::I32, T::I32, T::I32, T::I32}, {T::I32},
        [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
            int32_t result = UC_OK;
            const Fetch* fetch = FinishedFetch(a[0].i32, result);
            if (!fetch) { r[0].i32 = result; return; }
            const uint32_t nameLength = uint32_t(a[2].i32);
            std::string name;
            if (nameLength > kMaxHeaderNameBytes) { r[0].i32 = UC_ERR_NOT_FOUND; return; }
            if (!c.ReadMemory(uint32_t(a[1].i32), nameLength, name)) { r[0].i32 = UC_ERR_MEMORY; return; }
            name = Lower(name);
            std::string value;
            bool found = false;
            for (const auto& [header, v] : fetch->headers) {
                if (header != name) continue;
                value += (found ? ", " : "") + v;
                found = true;
            }
            r[0].i32 = found ? CopyOut(c, uint32_t(a[3].i32), uint32_t(a[4].i32), value.data(), value.size()) : UC_ERR_NOT_FOUND;
        });
    add("uc_fetch_close", {T::I32}, {T::I32}, [this](WasmCaller&, const WasmValue* a, WasmValue* r) {
        r[0].i32 = CloseFetch(a[0].i32);
    });

    add("uc_storage_get", {T::I32, T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        UltraWebStorage* storage = options_.services.storage.get();
        if (!storage) { r[0].i32 = UC_ERR_DENIED; return; }
        const uint32_t keyLength = uint32_t(a[1].i32);
        if (keyLength > UltraWebStorage::kMaxKeyBytes) { r[0].i32 = UC_ERR_LIMIT; return; }
        std::string key, value;
        if (!c.ReadMemory(uint32_t(a[0].i32), keyLength, key)) { r[0].i32 = UC_ERR_MEMORY; return; }
        if (!storage->Get(key, value)) { r[0].i32 = UC_ERR_NOT_FOUND; return; }
        r[0].i32 = CopyOut(c, uint32_t(a[2].i32), uint32_t(a[3].i32), value.data(), value.size());
    });
    add("uc_storage_set", {T::I32, T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        UltraWebStorage* storage = options_.services.storage.get();
        if (!storage) { r[0].i32 = UC_ERR_DENIED; return; }
        const uint32_t keyLength = uint32_t(a[1].i32), valueLength = uint32_t(a[3].i32);
        if (keyLength > UltraWebStorage::kMaxKeyBytes || valueLength > UltraWebStorage::kQuotaBytes) { r[0].i32 = UC_ERR_LIMIT; return; }
        std::string key, value;
        if (!c.ReadMemory(uint32_t(a[0].i32), keyLength, key) || !c.ReadMemory(uint32_t(a[2].i32), valueLength, value)) {
            r[0].i32 = UC_ERR_MEMORY;
            return;
        }
        r[0].i32 = storage->Set(key, value);
        if (r[0].i32 == UC_OK) StorageChanged();
    });
    add("uc_storage_remove", {T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        UltraWebStorage* storage = options_.services.storage.get();
        if (!storage) { r[0].i32 = UC_ERR_DENIED; return; }
        const uint32_t keyLength = uint32_t(a[1].i32);
        if (keyLength > UltraWebStorage::kMaxKeyBytes) { r[0].i32 = UC_ERR_NOT_FOUND; return; }
        std::string key;
        if (!c.ReadMemory(uint32_t(a[0].i32), keyLength, key)) { r[0].i32 = UC_ERR_MEMORY; return; }
        if (!storage->Remove(key)) { r[0].i32 = UC_ERR_NOT_FOUND; return; }
        StorageChanged();
        r[0].i32 = UC_OK;
    });
    add("uc_storage_key", {T::I32, T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        UltraWebStorage* storage = options_.services.storage.get();
        if (!storage) { r[0].i32 = UC_ERR_DENIED; return; }
        std::string key;
        if (!storage->KeyAt(uint32_t(a[0].i32), key)) { r[0].i32 = UC_ERR_NOT_FOUND; return; }
        r[0].i32 = CopyOut(c, uint32_t(a[1].i32), uint32_t(a[2].i32), key.data(), key.size());
    });
    add("uc_storage_clear", {}, {T::I32}, [this](WasmCaller&, const WasmValue*, WasmValue* r) {
        UltraWebStorage* storage = options_.services.storage.get();
        if (!storage) { r[0].i32 = UC_ERR_DENIED; return; }
        if (storage->KeyCount() > 0) {
            storage->Clear();
            StorageChanged();
        }
        r[0].i32 = UC_OK;
    });

    add("uc_clipboard_write", {T::I32, T::I32}, {T::I32}, [this](WasmCaller& c, const WasmValue* a, WasmValue* r) {
        const uint32_t length = uint32_t(a[1].i32);
        if (length > options_.maxTextBytes) { r[0].i32 = UC_ERR_LIMIT; return; }
        std::string text;
        if (!c.ReadMemory(uint32_t(a[0].i32), length, text)) { r[0].i32 = UC_ERR_MEMORY; return; }
        r[0].i32 = WriteClipboard(text);
    });
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
    if (kind == UC_KIND_TEXT_INPUT) {
        entry.width = kInputWidth;
        entry.height = kInputHeight;
    }
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
    UpdateCrossAlignment(childHandle);
    container->InvalidateLayout();
    container->RequestRedraw();
    return UC_OK;
}

// A container stretches its children across its line (AlignItems::Stretch),
// which CSS does only for an item whose cross size is automatic (Flexbox
// 9.4 step 11); the layout engine stretches a sized one too. So a child
// the guest gave a width in a column, or a height in a row, opts out.
void UltraWebGuest::UpdateCrossAlignment(uint32_t handle) {
    Entry& entry = entries_[handle];
    if (handle == UC_ROOT_HANDLE || entry.parent == UC_NO_HANDLE) return;
    const bool sized = entries_[entry.parent].row ? entry.height > 0 : entry.width > 0;
    entry.element->layoutItem.SetAlignSelf(sized ? CSSLayout::AlignSelf::Start : CSSLayout::AlignSelf::Auto);
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
            if (property == UC_PROP_WIDTH) {
                element->SetElementSize(Length(value), element->size.height);
                entry->width = static_cast<float>(value);
            } else {
                element->SetElementSize(element->size.width, Length(value));
                entry->height = static_cast<float>(value);
            }
            UpdateCrossAlignment(handle);
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
            entry->row = value == UC_DIRECTION_ROW;
            for (uint32_t child : entry->children) UpdateCrossAlignment(child);
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

// ===== SERVICES =====

void UltraWebGuest::Log(const std::string& line) {
    if (options_.onLog) options_.onLog("UltraWeb: " + line);
}

int32_t UltraWebGuest::StartTimer(uint32_t delayMs, bool repeat) {
    const GuestServices& services = options_.services;
    if (!services.startTimer) return UC_ERR_DENIED;
    if (!instance_->HasFunction("uc_event")) return UC_ERR_STATE;
    if (timers_.size() >= kMaxTimers) return UC_ERR_LIMIT;
    if (delayMs > uint32_t(std::numeric_limits<int32_t>::max())) return UC_ERR_LIMIT;
    const int32_t id = NextId(nextTimerId_, timers_);
    std::weak_ptr<int> alive = alive_;
    // Raw `this` behind the alive token: the host timer outlives nothing it
    // reaches once ~UltraWebGuest has stopped it.
    const uint32_t hostId = services.startTimer(std::max(delayMs, kMinTimerMs), repeat, [this, alive, id]() {
        if (!alive.expired()) TimerFired(id);
    });
    if (hostId == 0) return UC_ERR_STATE;
    timers_[id] = Timer{hostId, repeat};
    return id;
}

int32_t UltraWebGuest::StopTimer(int32_t id) {
    auto at = timers_.find(id);
    if (at == timers_.end()) return UC_ERR_NOT_FOUND;
    if (options_.services.stopTimer) options_.services.stopTimer(at->second.hostId);
    timers_.erase(at);
    return UC_OK;
}

void UltraWebGuest::TimerFired(int32_t id) {
    auto at = timers_.find(id);
    if (at == timers_.end()) return;   // stopped meanwhile
    // A one-shot timer is gone once it has fired.
    if (!at->second.repeat) timers_.erase(at);
    DeliverAppEvent(UC_EVENT_TIMER, id);
}

int32_t UltraWebGuest::StartFetch(const std::string& url, uint32_t method, std::vector<uint8_t> body,
                                  std::string contentType) {
    if (!options_.services.fetch) return UC_ERR_DENIED;
    if (!instance_->HasFunction("uc_event")) return UC_ERR_STATE;
    if (fetches_.size() >= kMaxOpenFetches) {
        Log("fetch " + url + ": " + std::to_string(kMaxOpenFetches) + " fetches are open already; close some");
        return UC_ERR_LIMIT;
    }
    FetchRequest request;
    std::string reason;
    const int32_t result = FetchRules::Prepare(options_.address, url, method, std::move(body), std::move(contentType),
                                               request, reason);
    if (result != UC_OK) {
        Log("fetch " + url + " refused: " + reason);
        return result;
    }
    const int32_t id = NextId(nextFetchId_, fetches_);
    Fetch& fetch = fetches_[id];
    fetch.request = request;
    std::weak_ptr<int> alive = alive_;
    std::function<void()> cancel = options_.services.fetch(request, [this, alive, id](FetchResponse response) {
        if (!alive.expired()) FetchDone(id, std::move(response));
    });
    // The service may have answered already, and the entry with it.
    auto at = fetches_.find(id);
    if (at != fetches_.end() && at->second.status == 0) at->second.cancel = std::move(cancel);
    return id;
}

void UltraWebGuest::FetchDone(int32_t id, FetchResponse response) {
    auto at = fetches_.find(id);
    if (at == fetches_.end() || at->second.status != 0) return;   // closed meanwhile
    Fetch& fetch = at->second;
    fetch.cancel = nullptr;
    std::string reason;
    const int32_t result = FetchRules::Admit(fetch.request, response, fetch.headers, reason);
    if (result == UC_OK) {
        fetch.status = response.status;
        fetch.body = std::move(response.body);
    } else {
        fetch.status = result;
        fetch.headers.clear();
        Log("fetch " + fetch.request.url + ": " + reason);
    }
    DeliverAppEvent(UC_EVENT_FETCH, id);
}

const UltraWebGuest::Fetch* UltraWebGuest::FinishedFetch(int32_t id, int32_t& result) const {
    auto at = fetches_.find(id);
    if (at == fetches_.end()) { result = UC_ERR_NOT_FOUND; return nullptr; }
    if (at->second.status == 0) { result = UC_ERR_STATE; return nullptr; }
    if (at->second.status < 0) { result = at->second.status; return nullptr; }
    return &at->second;
}

int32_t UltraWebGuest::CloseFetch(int32_t id) {
    auto at = fetches_.find(id);
    if (at == fetches_.end()) return UC_ERR_NOT_FOUND;
    std::function<void()> cancel = std::move(at->second.cancel);
    fetches_.erase(at);
    if (cancel) cancel();
    return UC_OK;
}

void UltraWebGuest::StorageChanged() {
    const std::shared_ptr<UltraWebStorage>& storage = options_.services.storage;
    if (!storage || !storage->TakeFlushRequest()) return;
    // One write for a run of changes, on a later turn; the store flushes
    // itself when it closes, so a lost task loses nothing.
    std::weak_ptr<UltraWebStorage> weak = storage;
    auto flush = [weak]() {
        if (auto s = weak.lock()) s->Flush();
    };
    if (options_.defer) options_.defer(flush);
    else flush();
}

int32_t UltraWebGuest::WriteClipboard(const std::string& text) {
    if (!inUserAction_) {
        Log("clipboard: an app may write it only while it handles a click, a toggle or an edit");
        return UC_ERR_DENIED;
    }
    if (!options_.services.writeClipboard) return UC_ERR_DENIED;
    return options_.services.writeClipboard(text) ? UC_OK : UC_ERR_STATE;
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
    inUserAction_ = (event & kUserActions) != 0;
    CallEvent(handle, event, detail);
    inUserAction_ = false;
    DrainAppEvents();
}

void UltraWebGuest::DeliverAppEvent(uint32_t event, int32_t detail) {
    if (failed_ || !instance_) return;
    appEvents_.emplace_back(event, detail);
    DrainAppEvents();
}

void UltraWebGuest::DrainAppEvents() {
    // While a call runs the events wait; the call's caller drains them once
    // it has returned, one call at a time.
    while (!inGuest_ && !failed_ && !appEvents_.empty()) {
        const auto [event, detail] = appEvents_.front();
        appEvents_.pop_front();
        CallEvent(UC_NO_HANDLE, event, detail);
    }
}

void UltraWebGuest::CallEvent(uint32_t handle, uint32_t event, int32_t detail) {
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

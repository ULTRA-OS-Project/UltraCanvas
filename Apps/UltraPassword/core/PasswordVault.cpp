// Apps/UltraPassword/core/PasswordVault.cpp
// Payload layout (all integers little-endian):
//
//   4 bytes  magic "UPWD"
//   1 byte   payload version (1)
//   record*  until the end of the buffer
//
//   record:  1 byte kind (1 = vault info, 2 = group, 3 = entry)
//            4 bytes body length
//            body = field*
//   field:   1 byte tag, 4 bytes length, bytes
//
// A reader skips a field tag it does not know and a record kind it does not
// know, so a later version can add both without this one refusing the file.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PasswordVault.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>

namespace UltraPassword {

// ===== SIGN-IN METHODS =====
namespace {

const std::vector<SignInMethodInfo>& MethodTable() {
    static const std::vector<SignInMethodInfo> table = {
        {SignInMethod::Password, "Password only", "Password",
         "Only a password protects this account. Anyone who phishes, guesses or "
         "finds it in a breach is in - turn on two-factor sign-in or a passkey "
         "if the site offers one.",
         SecurityLevel::Weak, true},
        {SignInMethod::PasswordAuthenticatorApp, "Password + authenticator app", "2FA app",
         "A password plus a six-digit code from an authenticator app (TOTP). "
         "A leaked password alone is not enough, but a fake login page can still "
         "ask for the code.",
         SecurityLevel::Good, true},
        {SignInMethod::PasswordSms, "Password + SMS code", "2FA SMS",
         "A password plus a code sent by text message or call. Better than a "
         "password alone, but SIM swapping and phishing can intercept the code.",
         SecurityLevel::Fair, true},
        {SignInMethod::PasswordHardwareKey, "Password + security key", "Security key",
         "A password plus a FIDO2 / U2F hardware key. The key checks the site's "
         "address, so a phishing page cannot use it.",
         SecurityLevel::VeryStrong, true},
        {SignInMethod::Passkey, "Passkey", "Passkey",
         "A passkey (WebAuthn): no shared secret leaves your device, and it only "
         "works on the real site, so it cannot be phished or reused.",
         SecurityLevel::VeryStrong, false},
        {SignInMethod::PasskeyPasswordFallback, "Passkey + password fallback", "Passkey + pw",
         "A passkey, but the site still accepts the password too - the account is "
         "only as strong as that password. Remove it at the site if you can.",
         SecurityLevel::Strong, true},
        {SignInMethod::SingleSignOn, "Single sign-on", "SSO",
         "Signs in through another account (Google, Apple, Microsoft, ...). "
         "This account is exactly as safe as that one.",
         SecurityLevel::Good, false},
        {SignInMethod::EmailLink, "E-mail link or code", "E-mail link",
         "No password: the site e-mails a link or code each time. Whoever can "
         "read your mailbox can sign in.",
         SecurityLevel::Fair, false},
        {SignInMethod::PasswordEmailCode, "Password + e-mail code", "2FA e-mail",
         "A password plus a code sent by e-mail. Stops a leaked password alone, "
         "but not someone who also controls your mailbox.",
         SecurityLevel::Fair, true},
    };
    return table;
}

} // namespace

const SignInMethodInfo& GetSignInMethodInfo(SignInMethod method) {
    for (const auto& info : MethodTable())
        if (info.method == method) return info;
    return MethodTable().front();
}

const std::vector<SignInMethod>& AllSignInMethods() {
    // Ordered for the picker: the common cases first, strongest last.
    static const std::vector<SignInMethod> order = {
        SignInMethod::Password,
        SignInMethod::PasswordAuthenticatorApp,
        SignInMethod::PasswordSms,
        SignInMethod::PasswordEmailCode,
        SignInMethod::PasswordHardwareKey,
        SignInMethod::Passkey,
        SignInMethod::PasskeyPasswordFallback,
        SignInMethod::SingleSignOn,
        SignInMethod::EmailLink,
    };
    return order;
}

const char* SecurityLevelName(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::Weak:       return "Weak";
        case SecurityLevel::Fair:       return "Fair";
        case SecurityLevel::Good:       return "Good";
        case SecurityLevel::Strong:     return "Strong";
        case SecurityLevel::VeryStrong: return "Very strong";
    }
    return "Unknown";
}

// ===== HELPERS =====
void WipeString(std::string& text) {
    if (!text.empty()) UltraCrypt_SecureZero(text.data(), text.size());
    text.clear();
    text.shrink_to_fit();
}

void PasswordEntry::Wipe() {
    WipeString(id);
    WipeString(groupId);
    WipeString(title);
    WipeString(url);
    WipeString(username);
    WipeString(password);
    WipeString(notes);
    WipeString(ssoProvider);
}

int64_t NowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

namespace {

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool Contains(const std::string& haystack, const std::string& lowerNeedle) {
    return Lower(haystack).find(lowerNeedle) != std::string::npos;
}

} // namespace

// ===== VAULT =====
PasswordVault& PasswordVault::operator=(PasswordVault&& other) noexcept {
    if (this != &other) {
        Clear();
        name_    = std::move(other.name_);
        groups_  = std::move(other.groups_);
        entries_ = std::move(other.entries_);
    }
    return *this;
}

void PasswordVault::Clear() {
    for (auto& e : entries_) e.Wipe();
    entries_.clear();
    groups_.clear();
    name_.clear();
}

std::string PasswordVault::NewId() const {
    std::string id;
    if (!UltraCrypt_GenerateUuidV4(id) || id.empty()) {
        // Without a CSPRNG nothing else in the app works either, but an id is
        // not a secret: fall back to something unique within this vault.
        static uint64_t counter = 0;
        id = "id-" + std::to_string(NowSeconds()) + "-" + std::to_string(++counter);
    }
    return id;
}

bool PasswordVault::GroupExists(const std::string& groupId) const {
    return groupId == kRootGroupId || FindGroup(groupId) != nullptr;
}

const PasswordGroup* PasswordVault::FindGroup(const std::string& groupId) const {
    for (const auto& g : groups_)
        if (g.id == groupId) return &g;
    return nullptr;
}

std::string PasswordVault::AddGroup(const std::string& name, const std::string& parentId) {
    if (!GroupExists(parentId)) return {};
    PasswordGroup g;
    g.id = NewId();
    g.name = name;
    g.parentId = parentId;
    groups_.push_back(g);
    return g.id;
}

bool PasswordVault::RenameGroup(const std::string& groupId, const std::string& name) {
    for (auto& g : groups_) {
        if (g.id == groupId) { g.name = name; return true; }
    }
    return false;
}

bool PasswordVault::IsDescendant(const std::string& groupId, const std::string& ancestorId) const {
    // Walk up from groupId; the depth bound stops a corrupt cycle from looping.
    std::string current = groupId;
    for (size_t depth = 0; depth <= groups_.size() && current != kRootGroupId; ++depth) {
        if (current == ancestorId) return true;
        const PasswordGroup* g = FindGroup(current);
        if (!g) return false;
        current = g->parentId;
    }
    return false;
}

bool PasswordVault::MoveGroup(const std::string& groupId, const std::string& newParentId) {
    if (groupId == kRootGroupId || !FindGroup(groupId) || !GroupExists(newParentId)) return false;
    if (newParentId != kRootGroupId && IsDescendant(newParentId, groupId)) return false;
    for (auto& g : groups_) {
        if (g.id == groupId) { g.parentId = newParentId; return true; }
    }
    return false;
}

void PasswordVault::CollectSubtree(const std::string& groupId, std::vector<std::string>& out) const {
    out.push_back(groupId);
    for (const auto& g : groups_) {
        if (g.parentId == groupId &&
            std::find(out.begin(), out.end(), g.id) == out.end()) {
            CollectSubtree(g.id, out);
        }
    }
}

bool PasswordVault::RemoveGroup(const std::string& groupId) {
    if (groupId == kRootGroupId || !FindGroup(groupId)) return false;
    std::vector<std::string> doomed;
    CollectSubtree(groupId, doomed);
    auto inDoomed = [&doomed](const std::string& id) {
        return std::find(doomed.begin(), doomed.end(), id) != doomed.end();
    };
    for (auto& e : entries_)
        if (inDoomed(e.groupId)) e.Wipe();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const PasswordEntry& e) { return e.id.empty(); }),
                   entries_.end());
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                                 [&inDoomed](const PasswordGroup& g) { return inDoomed(g.id); }),
                  groups_.end());
    return true;
}

std::vector<const PasswordGroup*> PasswordVault::ChildGroups(const std::string& parentId) const {
    std::vector<const PasswordGroup*> out;
    for (const auto& g : groups_)
        if (g.parentId == parentId) out.push_back(&g);
    std::sort(out.begin(), out.end(), [](const PasswordGroup* a, const PasswordGroup* b) {
        return Lower(a->name) < Lower(b->name);
    });
    return out;
}

std::string PasswordVault::GroupPath(const std::string& groupId) const {
    std::vector<std::string> names;
    std::string current = groupId;
    for (size_t depth = 0; depth <= groups_.size() && current != kRootGroupId; ++depth) {
        const PasswordGroup* g = FindGroup(current);
        if (!g) break;
        names.push_back(g->name);
        current = g->parentId;
    }
    std::string path;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (!path.empty()) path += " / ";
        path += *it;
    }
    return path;
}

std::string PasswordVault::AddEntry(PasswordEntry entry) {
    if (!GroupExists(entry.groupId)) return {};
    if (entry.id.empty() || FindEntry(entry.id)) entry.id = NewId();
    const int64_t now = NowSeconds();
    if (entry.created == 0) entry.created = now;
    entry.modified = now;
    entries_.push_back(std::move(entry));
    return entries_.back().id;
}

bool PasswordVault::UpdateEntry(const PasswordEntry& entry) {
    if (!GroupExists(entry.groupId)) return false;
    for (auto& e : entries_) {
        if (e.id != entry.id) continue;
        const int64_t created = e.created;
        e.Wipe();
        e = entry;
        e.created = created;
        e.modified = NowSeconds();
        return true;
    }
    return false;
}

bool PasswordVault::MoveEntry(const std::string& entryId, const std::string& groupId) {
    if (!GroupExists(groupId)) return false;
    for (auto& e : entries_) {
        if (e.id == entryId) { e.groupId = groupId; e.modified = NowSeconds(); return true; }
    }
    return false;
}

bool PasswordVault::RemoveEntry(const std::string& entryId) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == entryId) {
            it->Wipe();
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

const PasswordEntry* PasswordVault::FindEntry(const std::string& entryId) const {
    for (const auto& e : entries_)
        if (e.id == entryId) return &e;
    return nullptr;
}

std::vector<const PasswordEntry*> PasswordVault::EntriesIn(const std::string& groupId) const {
    std::vector<const PasswordEntry*> out;
    for (const auto& e : entries_)
        if (e.groupId == groupId) out.push_back(&e);
    std::sort(out.begin(), out.end(), [](const PasswordEntry* a, const PasswordEntry* b) {
        return Lower(a->title) < Lower(b->title);
    });
    return out;
}

std::vector<const PasswordEntry*> PasswordVault::Search(const std::string& text) const {
    std::vector<const PasswordEntry*> out;
    const std::string needle = Lower(text);
    if (needle.empty()) return out;
    for (const auto& e : entries_) {
        if (Contains(e.title, needle) || Contains(e.url, needle) ||
            Contains(e.username, needle) || Contains(e.notes, needle) ||
            Contains(e.ssoProvider, needle)) {
            out.push_back(&e);
        }
    }
    std::sort(out.begin(), out.end(), [](const PasswordEntry* a, const PasswordEntry* b) {
        return Lower(a->title) < Lower(b->title);
    });
    return out;
}

std::string PasswordVault::ImportFrom(const PasswordVault& other, const std::string& intoGroupName) {
    const std::string target = AddGroup(intoGroupName, kRootGroupId);
    if (target.empty()) return {};

    // Old id -> new id, parents before children (the source may list them in
    // any order, so resolve breadth-first from its root).
    std::vector<std::pair<std::string, std::string>> mapping = {{kRootGroupId, target}};
    auto mapped = [&mapping](const std::string& oldId) -> std::string {
        for (const auto& m : mapping)
            if (m.first == oldId) return m.second;
        return {};
    };
    for (size_t i = 0; i < mapping.size(); ++i) {
        const std::string oldParent = mapping[i].first;
        const std::string newParent = mapping[i].second;
        for (const auto& g : other.groups_) {
            if (g.parentId != oldParent) continue;
            const std::string id = AddGroup(g.name, newParent);
            if (!id.empty()) mapping.emplace_back(g.id, id);
        }
    }
    for (const auto& e : other.entries_) {
        std::string group = mapped(e.groupId);
        if (group.empty()) group = target;   // orphaned in the source: keep it anyway
        PasswordEntry copy = e;
        copy.id.clear();
        copy.groupId = group;
        const int64_t created = copy.created;
        AddEntry(std::move(copy));
        if (created != 0) entries_.back().created = created;
    }
    return target;
}

// ===== SERIALISATION =====
namespace {

constexpr char    kPayloadMagic[4] = {'U', 'P', 'W', 'D'};
constexpr uint8_t kPayloadVersion  = 1;

constexpr uint8_t kRecordInfo  = 1;
constexpr uint8_t kRecordGroup = 2;
constexpr uint8_t kRecordEntry = 3;

// Field tags. Append; never reuse a number.
constexpr uint8_t kTagId       = 1;
constexpr uint8_t kTagName     = 2;   // vault name / group name / entry title
constexpr uint8_t kTagParent   = 3;   // group parent / entry group
constexpr uint8_t kTagUrl      = 4;
constexpr uint8_t kTagUser     = 5;
constexpr uint8_t kTagPassword = 6;
constexpr uint8_t kTagNotes    = 7;
constexpr uint8_t kTagSso      = 8;
constexpr uint8_t kTagMethod   = 9;
constexpr uint8_t kTagCreated  = 10;
constexpr uint8_t kTagModified = 11;

constexpr size_t kMaxFieldBytes = 1024 * 1024;   // a note can be long; not this long

// Grows a secure buffer by doubling, wiping each discarded copy (Resize does).
class SecureWriter {
public:
    explicit SecureWriter(UltraCryptSecureBuffer& out) : out_(out) { out_.Clear(); }

    void Bytes(const void* data, size_t size) {
        if (size == 0) return;
        Reserve(used_ + size);
        std::memcpy(out_.Data() + used_, data, size);
        used_ += size;
    }
    void U8(uint8_t v) { Bytes(&v, 1); }
    void U32(uint32_t v) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        Bytes(b, 4);
    }
    void U64(uint64_t v) {
        uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = uint8_t(v >> (8 * i));
        Bytes(b, 8);
    }
    size_t Used() const { return used_; }
    // Overwrite a 4-byte length written earlier as a placeholder.
    void PatchU32(size_t at, uint32_t v) {
        uint8_t* p = out_.Data() + at;
        p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
    }
    void Finish() { out_.Resize(used_); }

private:
    void Reserve(size_t needed) {
        if (needed <= out_.GetSize()) return;
        size_t capacity = std::max<size_t>(256, out_.GetSize());
        while (capacity < needed) capacity *= 2;
        out_.Resize(capacity);
    }

    UltraCryptSecureBuffer& out_;
    size_t used_ = 0;
};

void Field(SecureWriter& w, uint8_t tag, const std::string& value) {
    if (value.empty()) return;
    w.U8(tag);
    w.U32(static_cast<uint32_t>(value.size()));
    w.Bytes(value.data(), value.size());
}

void FieldU64(SecureWriter& w, uint8_t tag, uint64_t value) {
    w.U8(tag);
    w.U32(8);
    w.U64(value);
}

void FieldU8(SecureWriter& w, uint8_t tag, uint8_t value) {
    w.U8(tag);
    w.U32(1);
    w.U8(value);
}

// Writes a record header with a placeholder length; returns where the length is.
size_t BeginRecord(SecureWriter& w, uint8_t kind) {
    w.U8(kind);
    const size_t at = w.Used();
    w.U32(0);
    return at;
}

void EndRecord(SecureWriter& w, size_t lengthAt) {
    w.PatchU32(lengthAt, static_cast<uint32_t>(w.Used() - lengthAt - 4));
}

uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t ReadU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

// Calls `onField(tag, data, size)` for every field of a record body.
bool ForEachField(const uint8_t* body, size_t size,
                  const std::function<void(uint8_t, const uint8_t*, size_t)>& onField) {
    size_t pos = 0;
    while (pos < size) {
        if (size - pos < 5) return false;
        const uint8_t tag = body[pos];
        const uint32_t len = ReadU32(body + pos + 1);
        pos += 5;
        if (len > kMaxFieldBytes || len > size - pos) return false;
        onField(tag, body + pos, len);
        pos += len;
    }
    return true;
}

} // namespace

void PasswordVault::Serialize(UltraCryptSecureBuffer& out) const {
    SecureWriter w(out);
    w.Bytes(kPayloadMagic, sizeof(kPayloadMagic));
    w.U8(kPayloadVersion);

    size_t at = BeginRecord(w, kRecordInfo);
    Field(w, kTagName, name_);
    EndRecord(w, at);

    for (const auto& g : groups_) {
        at = BeginRecord(w, kRecordGroup);
        Field(w, kTagId, g.id);
        Field(w, kTagName, g.name);
        Field(w, kTagParent, g.parentId);
        EndRecord(w, at);
    }
    for (const auto& e : entries_) {
        at = BeginRecord(w, kRecordEntry);
        Field(w, kTagId, e.id);
        Field(w, kTagName, e.title);
        Field(w, kTagParent, e.groupId);
        Field(w, kTagUrl, e.url);
        Field(w, kTagUser, e.username);
        Field(w, kTagPassword, e.password);
        Field(w, kTagNotes, e.notes);
        Field(w, kTagSso, e.ssoProvider);
        FieldU8(w, kTagMethod, static_cast<uint8_t>(e.method));
        FieldU64(w, kTagCreated, static_cast<uint64_t>(e.created));
        FieldU64(w, kTagModified, static_cast<uint64_t>(e.modified));
        EndRecord(w, at);
    }
    w.Finish();
}

bool PasswordVault::Deserialize(const UltraCryptSecureBuffer& in) {
    Clear();
    const uint8_t* p = in.Data();
    const size_t size = in.GetSize();
    if (size < 5 || std::memcmp(p, kPayloadMagic, 4) != 0 || p[4] != kPayloadVersion) return false;

    auto asString = [](const uint8_t* d, size_t n) {
        return std::string(reinterpret_cast<const char*>(d), n);
    };

    size_t pos = 5;
    while (pos < size) {
        if (size - pos < 5) { Clear(); return false; }
        const uint8_t kind = p[pos];
        const uint32_t len = ReadU32(p + pos + 1);
        pos += 5;
        if (len > size - pos) { Clear(); return false; }
        const uint8_t* body = p + pos;
        bool ok = true;

        if (kind == kRecordInfo) {
            ok = ForEachField(body, len, [&](uint8_t tag, const uint8_t* d, size_t n) {
                if (tag == kTagName) name_ = asString(d, n);
            });
        } else if (kind == kRecordGroup) {
            PasswordGroup g;
            ok = ForEachField(body, len, [&](uint8_t tag, const uint8_t* d, size_t n) {
                if (tag == kTagId)          g.id = asString(d, n);
                else if (tag == kTagName)   g.name = asString(d, n);
                else if (tag == kTagParent) g.parentId = asString(d, n);
            });
            if (ok && !g.id.empty()) groups_.push_back(std::move(g));
        } else if (kind == kRecordEntry) {
            PasswordEntry e;
            ok = ForEachField(body, len, [&](uint8_t tag, const uint8_t* d, size_t n) {
                switch (tag) {
                    case kTagId:       e.id = asString(d, n); break;
                    case kTagName:     e.title = asString(d, n); break;
                    case kTagParent:   e.groupId = asString(d, n); break;
                    case kTagUrl:      e.url = asString(d, n); break;
                    case kTagUser:     e.username = asString(d, n); break;
                    case kTagPassword: e.password = asString(d, n); break;
                    case kTagNotes:    e.notes = asString(d, n); break;
                    case kTagSso:      e.ssoProvider = asString(d, n); break;
                    case kTagMethod:
                        if (n == 1 && d[0] < kSignInMethodCount)
                            e.method = static_cast<SignInMethod>(d[0]);
                        break;
                    case kTagCreated:  if (n == 8) e.created = static_cast<int64_t>(ReadU64(d)); break;
                    case kTagModified: if (n == 8) e.modified = static_cast<int64_t>(ReadU64(d)); break;
                    default: break;   // a later version's field
                }
            });
            if (ok && !e.id.empty()) entries_.push_back(std::move(e));
        }
        // An unknown record kind is skipped whole.
        if (!ok) { Clear(); return false; }
        pos += len;
    }

    // Repair references a damaged or hand-edited payload could carry: a group
    // whose parent is gone moves to the top level, as does an entry whose
    // group is gone. Nothing is dropped.
    for (auto& g : groups_)
        if (g.parentId != kRootGroupId && !FindGroup(g.parentId)) g.parentId = kRootGroupId;
    for (auto& g : groups_)
        if (g.parentId != kRootGroupId && IsDescendant(g.parentId, g.id)) g.parentId = kRootGroupId;
    for (auto& e : entries_)
        if (!GroupExists(e.groupId)) e.groupId = kRootGroupId;
    return true;
}

} // namespace UltraPassword

// Apps/UltraPassword/core/PasswordVault.h
// The decrypted contents of a password vault: a tree of groups, and password
// entries that live in any group (or at the top level).
//
// A group can hold sub-groups and entries at the same time, which is what the
// tree view on the left of the window shows: "Work" can contain the entries
// for the company mail and the HR portal *and* a "Cloud consoles" sub-group.
//
// Every entry records how the website lets the user sign in (SignInMethod):
// a plain password, a password plus a second factor, a passkey, a single
// sign-on button, a link sent by e-mail. The card in the window shows that
// choice with a security rating, so the user can see at a glance which
// accounts still rest on a password alone.
//
// Nothing in here touches the disk or the UI; VaultFile seals the serialised
// form and the window renders it. That keeps the whole security-relevant core
// buildable and testable without the framework (Tests/UltraPasswordTests.cpp).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAPASSWORD_PASSWORDVAULT_H
#define ULTRAPASSWORD_PASSWORDVAULT_H

#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraPassword {

// ===== HOW A WEBSITE SIGNS THE USER IN =====
// Stored by number in the vault file: append new values, never renumber.
enum class SignInMethod : uint8_t {
    Password                  = 0,   // password only
    PasswordAuthenticatorApp  = 1,   // password + TOTP code (authenticator app)
    PasswordSms               = 2,   // password + code by SMS / phone call
    PasswordHardwareKey       = 3,   // password + FIDO2 / U2F security key
    Passkey                   = 4,   // passkey only (WebAuthn), no password
    PasskeyPasswordFallback   = 5,   // passkey, but a password still works too
    SingleSignOn              = 6,   // "Sign in with Google / Apple / Microsoft"
    EmailLink                 = 7,   // magic link or one-time code by e-mail
    PasswordEmailCode         = 8,   // password + code by e-mail
};

constexpr int kSignInMethodCount = 9;

// How well the method resists the attacks that actually take accounts over:
// phishing, credential stuffing and SIM swapping. Shown on every card.
enum class SecurityLevel : uint8_t {
    Weak       = 1,   // a password alone: phishable, reusable, stuffable
    Fair       = 2,   // second factor that can be phished or SIM-swapped
    Good       = 3,   // a TOTP code or a delegated sign-in
    Strong     = 4,   // a passkey with a password still accepted beside it
    VeryStrong = 5,   // phishing-resistant: passkey or hardware key
};

struct SignInMethodInfo {
    SignInMethod  method;
    const char*   name;          // "Passkey", "Password + authenticator app"
    const char*   shortName;     // for the chip on the card: "Passkey", "2FA app"
    const char*   description;   // one sentence: what it is and why it rates as it does
    SecurityLevel level;
    bool          usesPassword;  // the entry's password field is meaningful
};

const SignInMethodInfo& GetSignInMethodInfo(SignInMethod method);
const std::vector<SignInMethod>& AllSignInMethods();
const char* SecurityLevelName(SecurityLevel level);

// ===== GROUPS AND ENTRIES =====
// The id of the invisible top level. Groups and entries whose parent is this
// sit directly under the vault's root node in the tree.
inline const std::string kRootGroupId;

struct PasswordGroup {
    std::string id;
    std::string name;
    std::string parentId;    // kRootGroupId for a top-level group
};

struct PasswordEntry {
    std::string  id;
    std::string  groupId;    // kRootGroupId for an entry at the top level
    std::string  title;      // "GitHub"
    std::string  url;        // "https://github.com/login"
    std::string  username;
    std::string  password;
    std::string  notes;
    std::string  ssoProvider;   // "Google", for SignInMethod::SingleSignOn
    SignInMethod method = SignInMethod::Password;
    int64_t      created  = 0;  // seconds since the Unix epoch
    int64_t      modified = 0;

    // Overwrites every string, then empties it, so a lock does not leave the
    // passwords in heap pages that later allocations hand out again.
    void Wipe();
};

// Overwrite a string's bytes in place before it is released.
void WipeString(std::string& text);

class PasswordVault {
public:
    PasswordVault() = default;
    ~PasswordVault() { Clear(); }

    PasswordVault(const PasswordVault&) = delete;
    PasswordVault& operator=(const PasswordVault&) = delete;
    PasswordVault(PasswordVault&& other) noexcept = default;
    PasswordVault& operator=(PasswordVault&& other) noexcept;

    // ----- vault metadata -----
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }

    // ----- groups -----
    // Returns the new group's id, or empty when `parentId` does not exist.
    std::string AddGroup(const std::string& name, const std::string& parentId = kRootGroupId);
    bool RenameGroup(const std::string& groupId, const std::string& name);
    // Refuses to move a group into itself or one of its own descendants.
    bool MoveGroup(const std::string& groupId, const std::string& newParentId);
    // Removes the group, its sub-groups and every entry in them.
    bool RemoveGroup(const std::string& groupId);
    const PasswordGroup* FindGroup(const std::string& groupId) const;
    std::vector<const PasswordGroup*> ChildGroups(const std::string& parentId) const;
    // "Work / Cloud consoles" — the names from the top down.
    std::string GroupPath(const std::string& groupId) const;
    bool GroupExists(const std::string& groupId) const;
    const std::vector<PasswordGroup>& Groups() const { return groups_; }

    // ----- entries -----
    // Stamps id (when empty), created and modified. Returns the id, or empty
    // when the entry's group does not exist.
    std::string AddEntry(PasswordEntry entry);
    // Replaces the entry with the same id; keeps `created`, stamps `modified`.
    bool UpdateEntry(const PasswordEntry& entry);
    bool MoveEntry(const std::string& entryId, const std::string& groupId);
    bool RemoveEntry(const std::string& entryId);
    const PasswordEntry* FindEntry(const std::string& entryId) const;
    std::vector<const PasswordEntry*> EntriesIn(const std::string& groupId) const;
    // Case-insensitive match on title, URL, username, notes and SSO provider.
    // Never matches on the password itself.
    std::vector<const PasswordEntry*> Search(const std::string& text) const;
    const std::vector<PasswordEntry>& Entries() const { return entries_; }

    size_t GroupCount() const { return groups_.size(); }
    size_t EntryCount() const { return entries_.size(); }

    // Adds every group and entry of `other` under a new group named
    // `intoGroupName` (at the top level), so an import never overwrites or
    // silently merges into what is already here. Ids are re-issued.
    // Returns the id of the group created.
    std::string ImportFrom(const PasswordVault& other, const std::string& intoGroupName);

    // ----- serialisation -----
    // The plaintext payload VaultFile encrypts. A tagged binary encoding rather
    // than JSON so the passwords pass through one secure buffer, not through a
    // value-semantic document tree that copies them around the heap; unknown
    // tags are skipped, so a later version can add fields without breaking
    // this reader.
    void Serialize(UltraCryptSecureBuffer& out) const;
    // Replaces the contents. False (and an empty vault) on malformed input.
    bool Deserialize(const UltraCryptSecureBuffer& in);

    // Wipes and drops every group and entry.
    void Clear();

private:
    std::string NewId() const;
    bool IsDescendant(const std::string& groupId, const std::string& ancestorId) const;
    void CollectSubtree(const std::string& groupId, std::vector<std::string>& out) const;

    std::string                name_;
    std::vector<PasswordGroup> groups_;
    std::vector<PasswordEntry> entries_;
};

int64_t NowSeconds();

} // namespace UltraPassword

#endif // ULTRAPASSWORD_PASSWORDVAULT_H

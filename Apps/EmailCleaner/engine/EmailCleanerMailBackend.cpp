// Apps/EmailCleaner/engine/EmailCleanerMailBackend.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerMailBackend.h"

#include <UltraNet/UltraNetHttp.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The last path element of a folder, whatever the server's delimiter is:
// "[Gmail]/Bin" and "INBOX.Trash" both reduce to their leaf.
std::string LeafName(const std::string& folder) {
    const size_t slash = folder.find_last_of("/.\\");
    return (slash == std::string::npos) ? folder : folder.substr(slash + 1);
}

} // namespace

bool MailBackend::LooksLikeTrash(const std::string& folderName) {
    // Names servers actually use, in the languages this is most likely to meet.
    // The SPECIAL-USE role is preferred over any of these; this is the fallback
    // for servers that do not advertise one.
    static const char* kNames[] = {
        "trash", "bin", "deleted", "deleted items", "deleted messages",
        "papierkorb", "gel\xc3\xb6scht", "corbeille", "papelera", "cestino",
        "prullenbak", "papperskorg"
    };
    const std::string leaf = Lower(LeafName(folderName));
    for (const char* n : kNames) {
        if (leaf == n) return true;
    }
    return false;
}

void MailBackend::SetAccount(const MailAccountAccess& access) {
    if (access.accountId.empty()) return;
    accounts_[access.accountId] = access;
}

MailAccountAccess* MailBackend::Find(const std::string& accountId) {
    auto it = accounts_.find(accountId);
    return it == accounts_.end() ? nullptr : &it->second;
}

std::string MailBackend::ResolvedTrash(const std::string& accountId) const {
    auto it = accounts_.find(accountId);
    return it == accounts_.end() ? std::string() : it->second.trashFolder;
}

std::string MailBackend::ResolveTrashFolder(const MailAccountAccess& access,
                                            std::string& outError) {
    std::vector<UltraNetMailFolder> folders;
    const UltraNetResult r = mailbox_.ListFolders(access.serverUrl, folders, access.options);
    if (!r) {
        outError = "could not list folders: " + r.message;
        return "";
    }

    // The role the server itself declares is authoritative.
    for (const UltraNetMailFolder& f : folders) {
        if (Lower(f.role) == "trash" && f.selectable) return f.name;
    }
    // Otherwise fall back to the names Trash is commonly given.
    for (const UltraNetMailFolder& f : folders) {
        if (f.selectable && LooksLikeTrash(f.name)) return f.name;
    }

    outError = "no Trash folder found on the server";
    return "";
}

bool MailBackend::MoveToTrash(const std::string& accountId, const std::string& folder,
                              int64_t uid, std::string& outError) {
    MailAccountAccess* access = Find(accountId);
    if (!access) {
        outError = "account '" + accountId + "' has no mail connection configured";
        return false;
    }

    if (access->trashFolder.empty()) {
        access->trashFolder = ResolveTrashFolder(*access, outError);
        if (access->trashFolder.empty()) return false;   // outError already set
    }

    // Moving a message that is already in Trash would be a no-op at best and
    // an error at worst; treat it as done.
    if (folder == access->trashFolder) return true;

    const UltraNetResult r = mailbox_.MoveMessage(access->serverUrl, folder,
                                                  static_cast<uint32_t>(uid),
                                                  access->trashFolder, access->options);
    if (!r) {
        outError = r.message;
        return false;
    }
    return true;
}

bool MailBackend::PostOneClick(const std::string& url, std::string& outError) {
    if (url.empty()) {
        outError = "no unsubscribe URL";
        return false;
    }
    // RFC 8058 refuses to grant one-click over plaintext, and so do we: the
    // request carries a token that identifies the subscriber.
    if (Lower(url).rfind("https://", 0) != 0) {
        outError = "one-click unsubscribe requires https";
        return false;
    }

    // The body is exactly this, per RFC 8058 §3.1.
    const std::string payload = "List-Unsubscribe=One-Click";
    const std::vector<uint8_t> body(payload.begin(), payload.end());

    UltraNetHttpOptions options;
    options.headers.Set("Content-Type", "application/x-www-form-urlencoded");
    // A redirect here would be the sender bouncing us somewhere unverified;
    // the RFC expects the POST to be handled at the URL it gave.
    options.followRedirects = false;

    UltraNetResponse response;
    const UltraNetResult r = UltraNet_HttpPost(url, body, response, options);
    if (!r) {
        outError = r.message;
        return false;
    }
    if (!response.IsSuccess()) {
        outError = "server answered " + std::to_string(response.statusCode) + " " +
                   response.statusMessage;
        return false;
    }
    return true;
}

bool MailBackend::SendUnsubscribeMail(const std::string& accountId,
                                      const std::string& address,
                                      const std::string& subject,
                                      std::string& outError) {
    if (address.empty()) {
        outError = "no unsubscribe address";
        return false;
    }
    MailAccountAccess* access = Find(accountId);
    if (!access) {
        outError = "account '" + accountId + "' has no mail connection configured";
        return false;
    }
    if (access->ownerAddress.empty()) {
        // The list identifies the subscriber by the From address; sending
        // without one would be answered by nothing.
        outError = "the account has no address to unsubscribe with";
        return false;
    }

    UltraNetMailMessage message;
    message.from        = access->ownerAddress;
    message.to          = { address };
    message.subject     = subject;
    message.body        = "Please remove this address from the list.";
    message.contentType = "text/plain";

    const UltraNetResult r = mailbox_.SendMail(message, access->options);
    if (!r) {
        outError = r.message;
        return false;
    }
    return true;
}

} // namespace EmailCleaner

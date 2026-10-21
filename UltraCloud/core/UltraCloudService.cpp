// UltraCloud/core/UltraCloudService.cpp
// Version: 0.8.0 - an upload complete under its part name when the connection
//                  went is given its name by the next call, not removed
// Version: 0.7.0 - uploads to FTP / SFTP by way of a part name; what an
//                  unfinished one left is removed by the next call
// Version: 0.6.0 - TransfersInProgress: uploads and downloads count while
//                  they run
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudService.h>
#include <UltraCloud/UltraCloudLog.h>
#include <UltraCloud/UltraCloudWebDav.h>   // NormalizePath

#include "core/UltraCloudInternal.h"        // UploadReport

#include <algorithm>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraCloud {

class CloudService::TransferScope {
public:
    TransferScope(CloudService& service, Transfer transfer) : service_(service) {
        std::lock_guard<std::mutex> lk(service_.transfersMutex_);
        id_ = ++service_.nextTransferId_;
        service_.transfers_[id_] = std::move(transfer);
    }
    ~TransferScope() {
        std::lock_guard<std::mutex> lk(service_.transfersMutex_);
        service_.transfers_.erase(id_);
    }
    TransferScope(const TransferScope&) = delete;
    TransferScope& operator=(const TransferScope&) = delete;

private:
    CloudService& service_;
    uint64_t id_ = 0;
};

std::vector<CloudService::Transfer> CloudService::TransfersInProgress() const {
    std::lock_guard<std::mutex> lk(transfersMutex_);
    std::vector<Transfer> out;
    out.reserve(transfers_.size());
    for (const auto& entry : transfers_) out.push_back(entry.second);
    return out;
}

std::shared_ptr<ICloudProvider> CloudService::ProviderFor(const Account& account) const {
    return GetProvider(account.providerId);
}

Result CloudService::Resolve(const std::string& accountId, Account& account,
                             Credentials& credentials,
                             std::shared_ptr<ICloudProvider>& provider, bool* renewed) {
    if (renewed) *renewed = false;
    Result got = accounts_.Get(accountId, account);
    if (!got) return got;
    provider = ProviderFor(account);
    if (!provider)
        return Result::Error(ResultCode::Unsupported,
                             "no provider registered for '" + account.providerId + "'");
    secrets_.Retrieve(accountId, credentials);
    if (credentials.username.empty()) credentials.username = account.username;

    // An expired OAuth token is renewed before it is used, and the renewal
    // persisted so the next call starts fresh. Said in the log either way: a
    // renewal the service refuses is the usual reason a cloud drive that
    // worked yesterday stops working today.
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (credentials.TokenExpired(now)) {
        Result refreshed = RenewToken(accountId, account, credentials, *provider,
                                      "The access token has expired - renewing it");
        if (!refreshed) return refreshed;
        if (renewed) *renewed = true;
    }
    return Result::Ok();
}

Result CloudService::RenewToken(const std::string& accountId, const Account& account,
                                Credentials& credentials, ICloudProvider& provider,
                                const std::string& why) {
    LogToThread(LogKind::Step, why);
    Result refreshed = provider.RefreshCredentials(account, credentials);
    if (!refreshed) {
        LogToThread(LogKind::Step, "Renewing the access token failed: " + refreshed.message);
        return refreshed;
    }
    LogToThread(LogKind::Step, "Access token renewed");
    secrets_.Store(accountId, credentials);
    return Result::Ok();
}

Result CloudService::Run(const std::string& accountId, const AccountCall& call,
                         std::vector<std::string>* removed) {
    Account a; Credentials c; std::shared_ptr<ICloudProvider> p;
    bool renewed = false;
    Result r = Resolve(accountId, a, c, p, &renewed);
    if (!r) return r;
    // What uploads that did not finish left on a server that keeps it goes
    // first: before a listing would show it, before a change trips over it.
    const bool partial = p->Capabilities().partialUploadsRemain;
    if (partial) CleanUpPendingUploads(a, c, *p, removed);
    r = call(a, c, *p);
    // A 401 for a token that had not expired here: revoked early, a clock
    // that is off, or one that ran out while a request waited on a service
    // limiting requests. Renewed once - not when it was renewed a moment ago,
    // which a second renewal would not change - and only an OAuth account has
    // anything to renew it from.
    if (r.code == ResultCode::AuthFailed && r.httpStatus == 401 && !renewed &&
        !c.refreshToken.empty()) {
        Result refreshed = RenewToken(accountId, a, c, *p,
                                      "The service rejected the access token - renewing it "
                                      "and trying again");
        if (!refreshed) return refreshed;
        r = call(a, c, *p);
    }
    // The server answered this time: a cleanup it did not answer before is
    // tried again now, rather than before the call after this one.
    if (partial && r.IsOk() && ReleaseWaitingCleanups(accountId))
        CleanUpPendingUploads(a, c, *p, removed);
    return r;
}

Result CloudService::SignInAccount(Account& account,
                                   const std::function<void(const std::string& url)>& openUrl) {
    if (account.providerId.empty())
        return Result::Error(ResultCode::InvalidArgument, "account needs a provider");
    auto provider = GetProvider(account.providerId);
    if (!provider)
        return Result::Error(ResultCode::Unsupported,
                             "no provider registered for '" + account.providerId + "'");
    Credentials creds;
    Result signedIn = provider->SignIn(account, openUrl, creds);
    if (!signedIn) return signedIn;
    if (SignInCancelled()) {
        LogToThread(LogKind::Step, "The sign-in was cancelled - the account is not stored");
        return Result::Error(ResultCode::Cancelled, "sign-in cancelled");
    }

    std::string username, displayName;
    if (provider->AccountInfo(account, creds, username, displayName)) {
        if (account.username.empty()) account.username = username;
        if (account.displayName.empty() && !displayName.empty())
            account.displayName = provider->DisplayName() + " (" + displayName + ")";
    }
    if (account.username.empty()) account.username = "me";
    creds.username = account.username;
    return AddAccount(account, creds, /*verify=*/true);
}

Result CloudService::AddAccount(Account& account, const Credentials& credentials, bool verify) {
    if (account.providerId.empty())
        return Result::Error(ResultCode::InvalidArgument, "account needs a provider");
    auto provider = GetProvider(account.providerId);
    if (!provider)
        return Result::Error(ResultCode::Unsupported,
                             "no provider registered for '" + account.providerId + "'");
    if (account.accountId.empty())
        account.accountId = MakeAccountId(account.providerId, account.username, account.serverUrl);
    if (account.displayName.empty())
        account.displayName = provider->DisplayName() + " (" + account.username + ")";
    if (account.remoteFolder.empty()) account.remoteFolder = "/Shared from ULTRA OS";

    Credentials creds = credentials;
    if (creds.username.empty()) creds.username = account.username;
    if (verify) {
        Result v = provider->Verify(account, creds);
        if (!v) return v;
    }
    // Asked last, right before anything is stored: a sign-in the user gave up
    // on while it was being checked leaves no account behind.
    if (SignInCancelled()) {
        LogToThread(LogKind::Step, "The sign-in was cancelled - the account is not stored");
        return Result::Error(ResultCode::Cancelled, "sign-in cancelled");
    }
    if (!secrets_.Store(account.accountId, creds))
        return Result::Error(ResultCode::IoError, "could not store the account's credentials");
    Result stored = accounts_.Upsert(account);
    if (!stored) { secrets_.Remove(account.accountId); return stored; }
    // Reflect the default flag the store decided on.
    Account fresh;
    if (accounts_.Get(account.accountId, fresh)) account.isDefault = fresh.isDefault;
    return Result::Ok();
}

Result CloudService::RemoveAccount(const std::string& accountId) {
    Result r = accounts_.Remove(accountId);
    secrets_.Remove(accountId);
    return r;
}

Result CloudService::List(const std::string& accountId, const std::string& path,
                          std::vector<Entry>& out) {
    std::vector<std::string> removed;
    Result r = Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return p.List(a, c, NormalizePath(path), out);
    }, &removed);
    // A leftover removed after the listing came back is not shown in it.
    if (r && !removed.empty()) {
        out.erase(std::remove_if(out.begin(), out.end(), [&removed](const Entry& e) {
                      return std::find(removed.begin(), removed.end(), NormalizePath(e.path)) !=
                             removed.end();
                  }),
                  out.end());
    }
    return r;
}

Result CloudService::Delete(const std::string& accountId, const std::string& path,
                            bool isDirectory) {
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return p.Delete(a, c, NormalizePath(path), isDirectory);
    });
}

Result CloudService::Rename(const std::string& accountId, const std::string& path,
                            const std::string& newName) {
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return p.Rename(a, c, NormalizePath(path), newName);
    });
}

Result CloudService::MakeDirectory(const std::string& accountId,
                                   const std::string& path) {
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return p.MakeDirectory(a, c, NormalizePath(path));
    });
}

Result CloudService::Upload(const std::string& accountId, const std::string& localPath,
                            const std::string& remotePath) {
    TransferScope counted(*this, {Transfer::Kind::Upload, accountId, localPath,
                                  NormalizePath(remotePath)});
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return UploadWith(a, c, p, localPath, NormalizePath(remotePath));
    });
}

Result CloudService::CreateShareLink(const std::string& accountId, const std::string& remotePath,
                                     const ShareLinkOptions& options, ShareLink& out) {
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return p.CreateShareLink(a, c, NormalizePath(remotePath), options, out);
    });
}

Result CloudService::Download(const std::string& accountId, const std::string& remotePath,
                              const std::string& localPath) {
    TransferScope counted(*this, {Transfer::Kind::Download, accountId, localPath,
                                  NormalizePath(remotePath)});
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        if (localPath.empty())
            return Result::Error(ResultCode::InvalidArgument, "no local file given");
        // The folder has to exist before the provider opens the file in it:
        // every provider but FTP writes with an ofstream, which fails with
        // nothing more useful than "cannot write" when the directory is
        // missing.
        std::error_code ec;
        const std::filesystem::path parent = PathFromUtf8(localPath).parent_path();
        if (!parent.empty() && !std::filesystem::is_directory(parent, ec))
            return Result::Error(ResultCode::IoError,
                                 "no such folder: " + PathToUtf8(parent));
        return p.Download(a, c, NormalizePath(remotePath), localPath);
    });
}
Result CloudService::UploadAndShare(const std::string& accountId, const std::string& localPath,
                                    const std::string& remoteFolder,
                                    const ShareLinkOptions& options, ShareLink& out,
                                    std::string* remotePathOut) {
    // Counted as an upload into the folder from the start: making the folder
    // and the link around it are part of the same wait.
    TransferScope counted(*this, {Transfer::Kind::Upload, accountId, localPath,
                                  remoteFolder.empty() ? std::string() : NormalizePath(remoteFolder)});
    // The three steps run again as a whole when the token is renewed part of
    // the way: the folder is "already there" then, and the upload replaces
    // what the first try left.
    return Run(accountId, [&](const Account& a, const Credentials& c, ICloudProvider& p) {
        return UploadAndShareWith(a, c, p, localPath, remoteFolder, options, out, remotePathOut);
    });
}

Result CloudService::UploadAndShareWith(const Account& a, const Credentials& c, ICloudProvider& p,
                                        const std::string& localPath,
                                        const std::string& remoteFolder,
                                        const ShareLinkOptions& options, ShareLink& out,
                                        std::string* remotePathOut) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(localPath), ec))
        return Result::Error(ResultCode::IoError, "not a file: " + localPath);

    const std::string folder = NormalizePath(remoteFolder.empty() ? a.remoteFolder : remoteFolder);
    if (folder != "/") {
        // Create the folder if it is missing; "already there" is not an error.
        Result mk = p.MakeDirectory(a, c, folder);
        if (!mk && mk.httpStatus != 405 && mk.httpStatus != 409 && mk.code != ResultCode::Unsupported)
            return mk;
    }
    const std::string name = PathToUtf8(PathFromUtf8(localPath).filename());
    const std::string remotePath = (folder == "/" ? "" : folder) + "/" + name;

    Result up = UploadWith(a, c, p, localPath, remotePath);
    if (!up) return up;
    if (remotePathOut) *remotePathOut = remotePath;
    return p.CreateShareLink(a, c, remotePath, options, out);
}

// ---- Uploads to a server that writes them as they arrive ---------------------

namespace {

std::string PendingKey(const std::string& accountId, const std::string& remotePath) {
    return accountId + "\n" + remotePath;
}

int64_t Now() { return static_cast<int64_t>(std::time(nullptr)); }

// The folder ("" at the root) and the name of a normalised path.
void SplitRemotePath(const std::string& path, std::string& folder, std::string& name) {
    const std::string::size_type slash = path.find_last_of('/');
    folder = slash == std::string::npos ? std::string() : path.substr(0, slash);
    name = slash == std::string::npos ? path : path.substr(slash + 1);
}

// A provider is network code: whatever it throws costs the one step.
template <typename Fn>
Result Guarded(Fn&& fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        return Result::Error(ResultCode::Unknown, e.what());
    } catch (...) {
        return Result::Error(ResultCode::Unknown, "the provider failed");
    }
}

// Whether a failed step was not an answer from the server: it did not
// answer, or the calling thread called the step off first (its abort check -
// the application quitting). Either way nothing was refused, and the step is
// asked again rather than taken as the server's no.
bool NoAnswer(const Result& r) {
    return r.code == ResultCode::Network || r.code == ResultCode::Cancelled;
}

// The steps PutUploadInPlace and PlaceCompletePart take, on `p`'s server.
// `calledOff`, when given, is set once a step is called off.
InPlaceOps ServerSteps(const Account& a, const Credentials& c, ICloudProvider& p,
                       bool* calledOff = nullptr) {
    InPlaceOps ops;
    ops.rename = [&a, &c, &p, calledOff](const std::string& from, const std::string& newName,
                                         bool& unreachable) {
        const Result rr = Guarded([&]() { return p.Rename(a, c, from, newName); });
        unreachable = NoAnswer(rr);
        if (calledOff && rr.code == ResultCode::Cancelled) *calledOff = true;
        return rr.IsOk();
    };
    ops.remove = [&a, &c, &p, calledOff](const std::string& target, bool& unreachable) {
        const Result rr = Guarded([&]() { return p.Delete(a, c, target, false); });
        unreachable = NoAnswer(rr);
        if (calledOff && rr.code == ResultCode::Cancelled) *calledOff = true;
        return rr.IsOk();
    };
    ops.say = [](const std::string& line) { LogToThread(LogKind::Step, line); };
    return ops;
}

} // namespace

void CloudService::NotePending(const PendingUpload& upload) {
    accounts_.AddPendingUpload(upload);
    std::lock_guard<std::mutex> lk(pendingMutex_);
    const std::string key = PendingKey(upload.accountId, upload.remotePath);
    writing_.insert(key);
    attempts_.erase(key);
    clean_.erase(upload.accountId);
    ++pendingGeneration_;
}

void CloudService::ForgetPending(const std::string& accountId, const std::string& remotePath) {
    accounts_.RemovePendingUpload(accountId, remotePath);
    std::lock_guard<std::mutex> lk(pendingMutex_);
    const std::string key = PendingKey(accountId, remotePath);
    writing_.erase(key);
    attempts_.erase(key);
}

void CloudService::DoneWriting(const std::string& accountId, const std::string& remotePath) {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    writing_.erase(PendingKey(accountId, remotePath));
}

Result CloudService::UploadWith(const Account& a, const Credentials& c, ICloudProvider& p,
                                const std::string& localPath, const std::string& remotePath) {
    if (!p.Capabilities().partialUploadsRemain) return p.Upload(a, c, localPath, remotePath);

    const std::string path = NormalizePath(remotePath);
    std::string folder, name;
    SplitRemotePath(path, folder, name);
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(PathFromUtf8(localPath), ec);
    const uint64_t fileSize = ec ? 0 : static_cast<uint64_t>(size);
    const int64_t now = Now();
    const std::string partName = UploadPartName(name);
    const bool viaPart = !partName.empty() && !IsAnonymousFtpLogin(a.serverUrl, a.username) &&
                         !accounts_.UploadsDirect(a.accountId, a.serverUrl, now);
    if (!viaPart) return UploadStraight(a, c, p, localPath, path, fileSize);

    const std::string partPath = folder + "/" + partName;
    LogToThread(LogKind::Step, "Uploading as \"" + partName + "\" - renamed to \"" + name +
                               "\" once it is complete");
    // Noted before the first byte: nothing else writes a part name, and a
    // run that ends during the upload - quit, killed - leaves the note for
    // the next one to act on.
    NotePending({a.accountId, partPath, true, fileSize, now});
    internal::UploadReport report;
    internal::UploadReport* previous = internal::SetThreadUploadReport(&report);
    Result r = p.Upload(a, c, localPath, partPath);
    internal::SetThreadUploadReport(previous);
    if (!r) {
        // The server would not create the part name - a hidden name it does
        // not allow, one already there it will not overwrite - and nothing
        // was written: the file goes straight to its own name instead.
        if (report.refused && !report.opened) {
            ForgetPending(a.accountId, partPath);
            LogToThread(LogKind::Step, "The server refused that name: uploading straight to \"" +
                                       name + "\"");
            return UploadStraight(a, c, p, localPath, path, fileSize);
        }
        // Stopped before the server created the part (the line was down, the
        // sign-in refused): there is nothing to remove.
        if (report.watched && !report.opened) {
            ForgetPending(a.accountId, partPath);
            return r;
        }
        DoneWriting(a.accountId, partPath);   // the part is removed later
        return r;
    }

    bool calledOff = false;
    switch (PutUploadInPlace(folder, name, ServerSteps(a, c, p, &calledOff))) {
        case InPlace::Done:
            ForgetPending(a.accountId, partPath);
            return Result::Ok();
        case InPlace::Unreachable:
            // Complete, but still under the part name - the server stopped
            // answering, or the rename was called off (quitting): noted as
            // complete, so the next call that reaches the server gives it its
            // name (PlaceCompletePart) rather than removing it as a part.
            accounts_.AddPendingUpload({a.accountId, partPath, true, fileSize, now, Now()});
            DoneWriting(a.accountId, partPath);
            return Result::Error(calledOff ? ResultCode::Cancelled : ResultCode::Network,
                                 std::string("the upload is complete, but ") +
                                         (calledOff ? "was called off"
                                                    : "the server stopped answering") +
                                         " before it could be renamed to \"" + name +
                                         "\" - it is given that name the next time this "
                                         "account reaches the server");
        case InPlace::Refused:
            break;
    }

    // The server takes uploads but will not rename them: this one is sent
    // again, straight to its name, and so is every later one to this account
    // (until kPendingUploadMaxAgeSeconds has gone by).
    LogToThread(LogKind::Step, "The server would not rename the upload to \"" + name +
                               "\": sending it again straight to that name. Later uploads to "
                               "this account go straight to their names");
    accounts_.SetUploadsDirect(a.accountId, a.serverUrl, now);
    const Result removed = Guarded([&]() { return p.Delete(a, c, partPath, false); });
    if (removed) {
        ForgetPending(a.accountId, partPath);
    } else {
        // Kept as pending: a server that refuses renames often refuses
        // deletes too, and this one is asked again each run until it agrees
        // or the record is too old to act on.
        DoneWriting(a.accountId, partPath);
        LogToThread(LogKind::Step, "The copy under \"" + partName + "\" could not be removed (" +
                                   removed.message + "): it is tried again the next time this "
                                   "account is used");
    }
    return UploadStraight(a, c, p, localPath, path, fileSize);
}

Result CloudService::UploadStraight(const Account& a, const Credentials& c, ICloudProvider& p,
                                    const std::string& localPath, const std::string& path,
                                    uint64_t fileSize) {
    // Noted only once the server has created the file: until then the name
    // still holds whatever it held - perhaps a small file of the user's,
    // which a note made too early would have removed.
    internal::UploadReport report;
    report.checkFirst = true;
    bool noted = false;
    const PendingUpload pending{a.accountId, path, false, fileSize, Now()};
    report.onOpened = [this, &noted, &pending]() {
        noted = true;
        NotePending(pending);
    };
    internal::UploadReport* previous = internal::SetThreadUploadReport(&report);
    const Result r = p.Upload(a, c, localPath, path);
    internal::SetThreadUploadReport(previous);
    if (noted) {
        if (r) ForgetPending(a.accountId, path);
        else DoneWriting(a.accountId, path);
    }
    return r;
}

bool CloudService::ReleaseWaitingCleanups(const std::string& accountId) {
    std::lock_guard<std::mutex> lk(pendingMutex_);
    const std::string prefix = accountId + "\n";
    bool any = false;
    for (auto& [key, attempt] : attempts_) {
        if (key.compare(0, prefix.size(), prefix) != 0 || !attempt.waits) continue;
        attempt.waits = false;
        any = true;
    }
    return any;
}

void CloudService::CleanUpPendingUploads(const Account& a, const Credentials& c,
                                         ICloudProvider& p, std::vector<std::string>* removed) {
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        if (clean_.count(a.accountId)) return;
    }
    std::unique_lock<std::mutex> busy(cleanupMutex_, std::try_to_lock);
    if (!busy.owns_lock()) return;

    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        generation = pendingGeneration_;
    }
    std::vector<PendingUpload> all;
    if (!accounts_.ListPendingUploads(a.accountId, Now(), all)) return;
    std::vector<PendingUpload> due;
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        if (all.empty()) {
            // Clean - unless a record was written while the store was asked.
            if (generation == pendingGeneration_) clean_.insert(a.accountId);
            return;
        }
        for (const PendingUpload& u : all) {
            const std::string key = PendingKey(u.accountId, u.remotePath);
            if (writing_.count(key)) continue;   // an upload of this run, still going
            auto it = attempts_.find(key);
            if (it != attempts_.end() && (it->second.waits || it->second.nextRun)) continue;
            due.push_back(u);
        }
    }

    enum class Keep { No, UntilItAnswers, UntilNextRun };
    auto settle = [this](const PendingUpload& u, Keep keep) {
        if (keep == Keep::No) {
            ForgetPending(u.accountId, u.remotePath);
            return;
        }
        std::lock_guard<std::mutex> lk(pendingMutex_);
        CleanupAttempt& attempt = attempts_[PendingKey(u.accountId, u.remotePath)];
        if (keep == Keep::UntilItAnswers && ++attempt.failures < kPendingCleanupTriesPerRun) {
            attempt.waits = true;
        } else {
            attempt.waits = false;
            attempt.nextRun = true;
        }
    };

    bool noAnswer = false;
    for (const PendingUpload& u : due) {
        // A server that did not answer the last one is not asked again
        // straight away: the rest wait for it to answer.
        if (noAnswer) {
            std::lock_guard<std::mutex> lk(pendingMutex_);
            attempts_[PendingKey(u.accountId, u.remotePath)].waits = true;
            continue;
        }
        std::string folder, name;
        SplitRemotePath(u.remotePath, folder, name);
        const std::string forName = u.partName ? UploadNameOfPart(name) : name;
        const std::string aside = forName.empty() ? std::string() : UploadSetAsideName(forName);
        LogToThread(LogKind::Step,
                    u.completedAt != 0
                            ? "An upload of \"" + forName + "\" was complete when the server "
                              "stopped answering, before it had its name: giving it that name (" +
                              u.remotePath + ")"
                            : "An upload of \"" + (forName.empty() ? name : forName) +
                              "\" did not finish: removing what it left (" + u.remotePath + ")");

        std::vector<Entry> entries;
        Result r = Guarded([&]() {
            return p.List(a, c, folder.empty() ? "/" : folder, entries);
        });
        if (!r) {
            noAnswer = NoAnswer(r);
            settle(u, noAnswer ? Keep::UntilItAnswers : Keep::UntilNextRun);
            LogToThread(LogKind::Step, "Cannot look in the folder: " + r.message +
                                       (noAnswer ? " - asked again once the server answers"
                                                 : " - asked again next time"));
            continue;
        }
        auto find = [&entries](const std::string& n) -> const Entry* {
            if (n.empty()) return nullptr;
            for (const Entry& e : entries)
                if (e.name == n) return &e;
            return nullptr;
        };

        const Entry* there = find(name);
        const uint64_t size = there && there->size > 0 ? static_cast<uint64_t>(there->size) : 0;
        if (there && there->isDirectory) {
            LogToThread(LogKind::Step, "A folder of that name is there now: it is kept");
            settle(u, Keep::No);
            continue;
        }

        // The whole file under the part name: the connection went between
        // the last byte and the rename. It is given its name - in place of
        // the file there if it finished moments ago, as the upload was
        // doing; beside it otherwise - and removed only if the server will
        // not rename it.
        if (!forName.empty() && IsCompletePart(u, there != nullptr, size)) {
            if (u.completedAt == 0)
                LogToThread(LogKind::Step, "It is all there - " + std::to_string(size) +
                                           " bytes: giving it its name instead");
            const Entry* target = find(forName);
            const AtName atName = !target              ? AtName::Nothing
                                  : target->isDirectory ? AtName::Folder
                                                        : AtName::File;
            const bool replace = CompletePartReplaces(u, Now());
            const std::string kept = UploadKeptName(forName, [&find](const std::string& n) {
                return find(n) != nullptr;
            });
            const CompletePart placed = PlaceCompletePart(folder, forName, atName,
                                                          find(aside) != nullptr, replace, kept,
                                                          ServerSteps(a, c, p));
            if (placed == CompletePart::Unreachable) {
                noAnswer = true;
                settle(u, Keep::UntilItAnswers);
                LogToThread(LogKind::Step, "Cannot rename it: the server did not answer - asked "
                                           "again once it does");
                continue;
            }
            if (placed != CompletePart::Refused) {
                LogToThread(LogKind::Step,
                            placed == CompletePart::Named
                                    ? "\"" + forName + "\" is in place"
                                    : placed == CompletePart::Replaced
                                              ? "\"" + forName + "\" is in place, replacing the "
                                                "file that was there"
                                              : "\"" + forName + "\" is taken by " +
                                                (atName == AtName::Folder
                                                         ? std::string("a folder")
                                                         : std::string("a file that may have "
                                                                       "been written since")) +
                                                ": the upload is kept beside it as \"" + kept +
                                                "\"");
                settle(u, Keep::No);
                if (removed) removed->push_back(u.remotePath);
                continue;
            }
            LogToThread(LogKind::Step, "The server would not rename it: removing it - \"" +
                                       forName + "\" has to be sent again");
        } else if (u.partName && find(aside) && !find(forName)) {
            // The connection went between setting the old file aside and
            // giving the new one, unfinished, its name: the old one goes back.
            const Result back = Guarded([&]() {
                return p.Rename(a, c, folder + "/" + aside, forName);
            });
            LogToThread(LogKind::Step,
                        back ? "\"" + forName + "\" was set aside as \"" + aside +
                                       "\" when the upload stopped: it has its name back"
                             : "\"" + forName + "\" is still set aside as \"" + aside +
                                       "\": " + back.message);
        }
        if (!u.partName) {
            if (!there) {
                LogToThread(LogKind::Step, "No file of that name is there any more: nothing to remove");
                settle(u, Keep::No);
                continue;
            }
            if (!IsLeftByPendingUpload(u, size)) {
                LogToThread(LogKind::Step, "The file there is " + std::to_string(size) +
                                           " bytes - finished, or put there since: it is kept");
                settle(u, Keep::No);
                continue;
            }
        } else if (!there) {
            // Many servers leave names starting with a dot out of a listing
            // (vsftpd does by default), and a part name is one: it is removed
            // by name, and a server that has no such file says so.
            LogToThread(LogKind::Step, "Not in the listing - the server may leave out names that "
                                       "start with a dot: removing it by name");
        }

        r = Guarded([&]() { return p.Delete(a, c, u.remotePath, false); });
        if (r) {
            LogToThread(LogKind::Step, "Removed " + u.remotePath);
            settle(u, Keep::No);
            if (removed) removed->push_back(u.remotePath);
        } else if (NoAnswer(r)) {
            noAnswer = true;
            settle(u, Keep::UntilItAnswers);
            LogToThread(LogKind::Step, "Cannot remove it: " + r.message +
                                       " - asked again once the server answers");
        } else {
            // Refused - a server that lets anyone upload but nobody delete -
            // or, for a part the listing left out, perhaps not there at all:
            // the two answer alike, so it is asked again at the next run
            // until it goes or the record is too old to act on.
            LogToThread(LogKind::Step, "Not removed: " + r.message + " - asked again next time");
            settle(u, Keep::UntilNextRun);
        }
    }
}

} // namespace UltraCloud

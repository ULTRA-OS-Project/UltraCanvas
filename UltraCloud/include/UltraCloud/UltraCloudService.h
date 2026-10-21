// UltraCloud/include/UltraCloud/UltraCloudService.h
// The app-facing facade: accounts + secrets + providers behind one object.
// "Share this file through my default cloud account" is one call.
// Version: 0.7.0 - uploads to FTP / SFTP by way of a part name; what an
//                  unfinished one left is removed by the next call
// Version: 0.6.0 - TransfersInProgress: the uploads and downloads under way
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudAccounts.h"
#include "UltraCloudProvider.h"
#include "UltraCloudSecrets.h"
#include "UltraCloudUploads.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraCloud {

class CloudService {
public:
    // A service over stores the caller keeps: they must outlive it, and so
    // must the service outlive every call made on it.
    CloudService(AccountStore& accounts, ISecretStore& secrets)
        : accounts_(accounts), secrets_(secrets) {}

    // A service that keeps its stores alive for as long as it lives itself.
    // Made with std::make_shared, it can be handed to work that may outlive
    // whoever made it - the add-account dialog's sign-in thread, which waits
    // minutes on the browser, holds it until the sign-in has ended
    // (ShowAddAccountDialog's shared_ptr form).
    CloudService(std::shared_ptr<AccountStore> accounts, std::shared_ptr<ISecretStore> secrets)
        : ownedAccounts_(std::move(accounts)), ownedSecrets_(std::move(secrets)),
          accounts_(*ownedAccounts_), secrets_(*ownedSecrets_) {}

    AccountStore& Accounts() { return accounts_; }
    ISecretStore& Secrets()  { return secrets_; }

    // Store an account and its credentials. Fills accountId when empty; the
    // first account becomes the default. With `verify` the provider is asked
    // to sign in first and nothing is stored if that fails - nor when the
    // calling thread's sign-in has been called off meanwhile
    // (SetThreadSignInCancel): that ends as ResultCode::Cancelled.
    Result AddAccount(Account& account, const Credentials& credentials, bool verify);
    // Sign in through the provider's OAuth2 flow (`openUrl` shows the consent
    // page), fill the account's user name from the provider, then store it
    // like AddAccount. For providers whose Capabilities().needsOAuth is set.
    Result SignInAccount(Account& account,
                         const std::function<void(const std::string& url)>& openUrl);
    Result RemoveAccount(const std::string& accountId);

    std::shared_ptr<ICloudProvider> ProviderFor(const Account& account) const;

    // Every call below runs with the account's credentials resolved
    // (Resolve). One whose access token the service rejects (HTTP 401) while
    // it had not expired here - revoked early, a clock that is off, or expired
    // during a long wait for a service that limits requests - is not failed
    // at once: the token is renewed from the refresh token, stored, and the
    // call made once more. Said in the thread's log (UltraCloudLog.h).
    //
    // On a provider whose uploads land as they arrive (FTP, FTPS, SFTP:
    // partialUploadsRemain) every call first removes what uploads to that
    // account that did not finish left there - in this run or an earlier
    // one (UltraCloudUploads.h) - and says so in the thread's log. One whose
    // server did not answer is asked again straight after a later call that
    // got through; a listing then leaves out what was removed.
    Result List(const std::string& accountId, const std::string& path, std::vector<Entry>& out);
    // ---- Changing what is there ---------------------------------------------
    // Forwarded to the provider, which answers Unsupported unless it can do
    // it (ProviderCapabilities::modify). `isDirectory` is what the caller
    // already knows from the Entry; `newName` is a bare name, so Rename never
    // moves an entry to another folder.
    Result Delete(const std::string& accountId, const std::string& path,
                  bool isDirectory);
    Result Rename(const std::string& accountId, const std::string& path,
                  const std::string& newName);
    Result MakeDirectory(const std::string& accountId, const std::string& path);

    // To a provider whose uploads land as they arrive, by way of a hidden
    // part name and renamed into place once complete, so a file it replaces
    // stays whole until then; straight to the name where that cannot work.
    // Each step is in the thread's log (UltraCloudUploads.h). UploadAndShare
    // uploads the same way.
    Result Upload(const std::string& accountId, const std::string& localPath,
                  const std::string& remotePath);
    // The other direction. `localPath` is a full local path including the
    // name to save under, not a folder: the caller knows what it wants the
    // file called, and a provider that had to invent the name would have to
    // guess at a collision it cannot see. An existing file there is
    // overwritten, so a caller that must not lose one picks a free name
    // before calling.
    Result Download(const std::string& accountId, const std::string& remotePath,
                    const std::string& localPath);
    Result CreateShareLink(const std::string& accountId, const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out);

    // Upload `localPath` into `remoteFolder` (empty = the account's remoteFolder,
    // created if missing) and return a share link for it.
    Result UploadAndShare(const std::string& accountId, const std::string& localPath,
                          const std::string& remoteFolder, const ShareLinkOptions& options,
                          ShareLink& out, std::string* remotePathOut = nullptr);

    // ---- What is under way ---------------------------------------------------
    // An upload or a download a call on this service is running right now, on
    // any thread: Upload, Download and UploadAndShare each count from the moment
    // they start until they return. A host asks before it quits - a transfer
    // ends with the process, and the file does not arrive (UltraMail: "An
    // upload is still running - quit anyway?").
    struct Transfer {
        enum class Kind { Upload, Download };
        Kind kind = Kind::Upload;
        std::string accountId;
        std::string localPath;    // the file read (upload) or written (download)
        std::string remotePath;   // where it goes, or comes from, on the account
    };
    // In the order they started. Any thread.
    std::vector<Transfer> TransfersInProgress() const;

private:
    // Counts a transfer for as long as it lives (TransfersInProgress).
    class TransferScope;
    // Account + credentials + provider for an id. An expired OAuth token is
    // refreshed here and the new one stored, so callers never see a stale one;
    // `renewed`, when given, says whether that happened.
    Result Resolve(const std::string& accountId, Account& account, Credentials& credentials,
                   std::shared_ptr<ICloudProvider>& provider, bool* renewed = nullptr);
    // Resolve, then `call` - and once more with a renewed token when the
    // service rejected the one it had (see above).
    using AccountCall =
            std::function<Result(const Account&, const Credentials&, ICloudProvider&)>;
    // Removes what unfinished uploads left first, on a provider whose uploads
    // land as they arrive; the paths it removed go into `removed`.
    Result Run(const std::string& accountId, const AccountCall& call,
               std::vector<std::string>* removed = nullptr);
    // An upload with the account resolved: by way of a part name to a
    // provider whose uploads land as they arrive, as the provider does it
    // to any other.
    Result UploadWith(const Account& account, const Credentials& credentials,
                      ICloudProvider& provider, const std::string& localPath,
                      const std::string& remotePath);
    // ... and straight to the name, noted as pending once the server has
    // created the file there.
    Result UploadStraight(const Account& account, const Credentials& credentials,
                          ICloudProvider& provider, const std::string& localPath,
                          const std::string& path, uint64_t fileSize);
    // What unfinished uploads to `account` left, removed where it is still
    // theirs (IsLeftByPendingUpload) - and an upload that was complete under
    // its part name when the connection went given its name
    // (PlaceCompletePart). One cleanup at a time: a call that finds another
    // one running goes on without it.
    void CleanUpPendingUploads(const Account& account, const Credentials& credentials,
                               ICloudProvider& provider, std::vector<std::string>* removed);
    // A call on `accountId` got through: the pending uploads whose server did
    // not answer may be tried again. True if there are any.
    bool ReleaseWaitingCleanups(const std::string& accountId);
    // The pending records this process keeps: noted (before or while an
    // upload writes), forgotten (nothing of it is left), done writing (the
    // upload returned, its record stays for the cleanup).
    void NotePending(const PendingUpload& upload);
    void ForgetPending(const std::string& accountId, const std::string& remotePath);
    void DoneWriting(const std::string& accountId, const std::string& remotePath);
    // UploadAndShare's three steps with the account resolved.
    Result UploadAndShareWith(const Account& account, const Credentials& credentials,
                              ICloudProvider& provider, const std::string& localPath,
                              const std::string& remoteFolder, const ShareLinkOptions& options,
                              ShareLink& out, std::string* remotePathOut);
    // Renews the access token and stores it, saying so in the log.
    Result RenewToken(const std::string& accountId, const Account& account,
                      Credentials& credentials, ICloudProvider& provider,
                      const std::string& why);

    // Set by the shared_ptr constructor only. Declared before the references
    // below, which are bound to what they hold.
    std::shared_ptr<AccountStore> ownedAccounts_;
    std::shared_ptr<ISecretStore> ownedSecrets_;
    AccountStore& accounts_;
    ISecretStore& secrets_;

    mutable std::mutex transfersMutex_;
    std::map<uint64_t, Transfer> transfers_;   // by start order
    uint64_t nextTransferId_ = 0;

    // ---- Pending uploads ------------------------------------------------------
    // Keyed by account id + '\n' + path. `writing_`: uploads of this process
    // still running, which the cleanup leaves alone. `clean_`: accounts known
    // to have no pending upload, so a call does not ask the store each time.
    // `attempts_`: cleanups that did not happen this run - one whose server
    // did not answer `waits` for a call that gets through
    // (kPendingCleanupTriesPerRun times), one it refused waits for the next
    // run.
    struct CleanupAttempt {
        int failures = 0;
        bool waits = false;
        bool nextRun = false;
    };
    std::mutex pendingMutex_;
    std::set<std::string> writing_;
    std::set<std::string> clean_;
    std::map<std::string, CleanupAttempt> attempts_;
    // Counts NotePending calls, so a cleanup that found nothing does not mark
    // an account clean over a record another thread wrote meanwhile.
    uint64_t pendingGeneration_ = 0;
    std::mutex cleanupMutex_;
};

} // namespace UltraCloud

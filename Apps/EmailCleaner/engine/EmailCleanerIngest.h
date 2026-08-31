// Apps/EmailCleaner/engine/EmailCleanerIngest.h
// The loader: raw RFC 5322 messages in, analysed rows in the database out.
//
// EmailCleaner deliberately does not speak IMAP itself — UltraMail already
// does. Its SyncEngine drives a mailbox into the UltraMail LocalStore and
// caches every body as <emlDir>/<accountId>/<folder>/<uid>.eml; this ingest
// reads that cache, parses each message (UltraNet's MIME parser), classifies
// it (EmailCleaner::Classifier) and writes the result to the analysis store.
// So "manage several accounts" is UltraMail's account list and credential
// vault, and "load the mail into a database to analyse it" is this file.
//
// Three entry points, in increasing order of how much they do:
//   Analyze()            - pure: raw message -> AnalyzedMessage, no I/O
//   IngestRaw() / File() - analyse and store one message
//   IngestMailCache()    - analyse and store everything UltraMail has cached
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerClassifier.h"
#include "EmailCleanerStore.h"
#include "EmailCleanerTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace EmailCleaner {

struct IngestStats {
    int filesSeen   = 0;   // .eml files visited
    int analysed    = 0;   // messages parsed and classified
    int stored      = 0;   // rows written
    int skipped     = 0;   // already in the database (incremental re-scan)
    int failed      = 0;   // unreadable or unparsable
    int unwanted    = 0;   // of the stored messages, how many were flagged
    int attachments = 0;

    void Add(const IngestStats& other);
};

struct IngestOptions {
    // The account's own address; a message addressed to it is "to the owner",
    // which keeps ordinary correspondence out of the bulk buckets.
    std::string ownerAddress;

    // Skip messages already present in the analysis database. Turn off to
    // re-classify an existing corpus after editing the rules.
    bool skipExisting = true;

    // Store the individual keyword hits (the evidence behind a verdict).
    bool storeHits = true;

    // Stop after this many messages (0 = no limit) — the UI's "scan the last
    // N" option and a safety valve on a very large mailbox.
    int maxMessages = 0;
};

class Ingestor {
public:
    Ingestor(AnalysisStore& store, Classifier classifier = Classifier())
        : store_(store), classifier_(std::move(classifier)) {}

    const Classifier& GetClassifier() const { return classifier_; }
    void SetClassifier(Classifier classifier) { classifier_ = std::move(classifier); }

    // ---- Pure analysis (no database) --------------------------------------
    // Parse and classify one raw message. `uid`, `accountId` and `folder`
    // become the row's key; they are not read from the message.
    AnalyzedMessage Analyze(const std::string& rawMessage,
                            const std::string& accountId,
                            const std::string& folder,
                            int64_t uid,
                            const std::string& ownerAddress = "") const;

    // ---- Single messages ---------------------------------------------------
    bool IngestRaw(const std::string& rawMessage, const std::string& accountId,
                   const std::string& folder, int64_t uid,
                   const IngestOptions& options, AnalyzedMessage* out = nullptr);

    // Read one .eml file and ingest it. When `uid` is 0 it is taken from the
    // file name (UltraMail names cached bodies "<uid>.eml").
    bool IngestFile(const std::string& path, const std::string& accountId,
                    const std::string& folder, int64_t uid,
                    const IngestOptions& options, AnalyzedMessage* out = nullptr);

    // ---- Whole mailboxes ---------------------------------------------------
    // Walk one folder directory of .eml files.
    IngestStats IngestFolderDirectory(const std::string& directory,
                                      const std::string& accountId,
                                      const std::string& folder,
                                      const IngestOptions& options);

    // Walk UltraMail's body cache for one account:
    //   <mailCacheDir>/<accountId>/<folder>/<uid>.eml
    // Folder names are recovered from the directory names (UltraMail replaces
    // '/' in IMAP paths with '_', which is preserved here as-is).
    IngestStats IngestMailCache(const std::string& mailCacheDir,
                                const std::string& accountId,
                                const IngestOptions& options);

    // Progress callback: (messagesDone, currentFolder). Set it to drive a
    // progress bar; it is called from the ingesting thread.
    std::function<void(int, const std::string&)> onProgress;

private:
    AnalysisStore& store_;
    Classifier     classifier_;
};

// Read a whole file into a string. Returns false when it cannot be opened.
bool ReadFileBytes(const std::string& path, std::string& out);

// The uid encoded in an UltraMail cache file name ("1234.eml" -> 1234; 0 when
// the name is not numeric).
int64_t UidFromFileName(const std::string& fileName);

} // namespace EmailCleaner

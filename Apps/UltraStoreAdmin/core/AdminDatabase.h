// Apps/UltraStoreAdmin/core/AdminDatabase.h
// What the admin backend keeps: the publishers, every app and component with
// all its settings, the files behind the packages, the signing keys (the seed
// sealed with a passphrase), the store's settings and the catalog's sequence.
// On UltraDatabase (SQLite), one file.
//
// A component is kept whole, as the JSON object the catalog writes
// (Catalog.h's SerializeComponent), beside the few columns the lists need:
// the catalog's component object *is* the admin's settings record, so what
// the admin backend keeps is by construction what the store reads, and a
// setting the catalog grows is kept without a schema change. The tables that
// are queried on their own - package files by hash, keys, settings - are
// tables.
// Version: 0.5.1 - ComponentUpdates
// Version: 0.5.0 - the reversals (refunds and chargebacks) and the payouts to the partners, schema 5
// Version: 0.4.0 - the pages ever published (published_pages), schema 4
// Version: 0.3.0 - a sale's reference (the payment service's order), schema 3
// Version: 0.2.0 - affiliates, codes, sales (schema 2)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::Catalog;
using UltraStore::Component;
using UltraStore::ComponentKind;
using UltraStore::ErrorCode;
using UltraStore::Publisher;
using UltraStore::Result;

inline constexpr int kAdminSchemaVersion = 5;   // 2: affiliates, codes, sales; 3: sales.reference; 4: published_pages; 5: reversals, payouts

// A signing key: the public half in the open, the seed sealed (KeyVault.h).
struct KeyRecord {
    int64_t id = 0;
    std::string name;
    std::string publicKeyHex;
    std::string createdAt;
    std::string retiredAt;      // empty while the key signs
    bool Active() const { return retiredAt.empty(); }
};

// The file behind a package, by the package's SHA-256: where it is in the
// package store and whether it is served to everyone ("public") or only to
// the buyers ("restricted", the packages server's access scheme).
struct PackageFile {
    std::string sha256;
    std::string file;           // UTF-8, in the package store
    std::string access = "public";
    uint64_t size = 0;
    std::string format;
};

// ===== AFFILIATES AND CODES =====
// A partner who brings buyers: a share of each sale made with one of their
// codes. Two tiers and no more: an affiliate recruited by another names
// them in recruitedBy, and the recruiter earns recruitSharePercent of the
// sales the recruit's codes make - the recruiter's own recruiter earns
// nothing from those (Docs/UltraStore/README.md, *Affiliates and codes*).
struct Affiliate {
    std::string id;                  // "aff-anna", reverse-DNS-free, an opaque handle in the catalog
    std::string name;
    std::string contact;             // an email, a page
    int sharePercent = 0;            // of a sale made with the affiliate's code
    int recruitSharePercent = 0;     // of a sale made with a recruit's code, to this affiliate
    std::string recruitedBy;         // the affiliate who brought this one in, or ""
    bool enabled = true;
    std::string createdAt;
};

// A code as the admin keeps it: the catalog's PromoCode, plus whether it is
// in use. A disabled code is left out of the catalog.
struct CodeRecord {
    UltraStore::PromoCode code;
    bool enabled = true;
    std::string createdAt;
};

// A sale as the payment service reports it (or the admin records it by
// hand until then), with what it credits: the affiliate whose code was used
// and, one level up, their recruiter. Amounts are decimals in the currency.
// A page the publish folder once carried under the catalog base - "go/ANNA-20",
// "join/aff-anna" - with what it was. The uploader never deletes on the
// server, so a page once published stays reachable; this list is what lets
// publish put a page that says "no longer valid" or "this invitation is
// closed" in place of one whose code or partner is gone from the database.
struct PublishedPage {
    std::string path;                // relative to the publish folder, '/' separated, no index.html
    std::string kind;                // "code" (go/<CODE>) or "join" (join/<affiliate>)
    std::string firstPublishedAt;    // ISO 8601
};

struct SaleRecord {
    int64_t id = 0;
    std::string at;                  // ISO 8601
    std::string code;                // as used, normalized
    std::string app;
    std::string amount;              // what the buyer paid, after the discount
    std::string currency;
    std::string affiliate;           // credited for the code, or ""
    std::string recruiter;           // the affiliate's recruiter, credited one level up, or ""
    std::string affiliateCredit;     // sharePercent of amount
    std::string recruiterCredit;     // recruitSharePercent of amount
    std::string reference;           // the payment service's order id, "" for a sale recorded by hand
};

// A refund or a chargeback as the payment service reports it
// (GET /v1/reversals): what it takes back of a sale or a renewal in the
// ledger, named by that sale's reference, and the partners' credits it
// takes back with it - the same share of the amount reversed that the sale
// credited them with, so a partner never keeps a credit on money the store
// gave back. Amounts are positive decimals; the finance report subtracts.
struct ReversalRecord {
    int64_t id = 0;
    std::string at;                  // ISO 8601
    std::string kind;                // "refund" or "chargeback"
    std::string reference;           // the payment service's reference of the reversal itself, "" for one recorded by hand
    std::string reverses;            // the reference of the sale or renewal it takes back
    std::string app;
    std::string amount;              // taken back from the buyer's payment
    std::string currency;
    std::string affiliate;           // whose credit is taken back, or ""
    std::string recruiter;
    std::string affiliateCredit;     // taken back
    std::string recruiterCredit;
    bool revoked = false;            // the licence went with it
};

// Money paid out to a partner, so the report says what is still owed:
// what the ledger credited them, less what was taken back, less this.
struct PayoutRecord {
    int64_t id = 0;
    std::string at;                  // ISO 8601
    std::string affiliate;
    std::string amount;
    std::string currency;
    std::string note;                // the bank reference, a word
};

class AdminDatabase {
public:
    AdminDatabase() = default;
    ~AdminDatabase();
    AdminDatabase(const AdminDatabase&) = delete;
    AdminDatabase& operator=(const AdminDatabase&) = delete;

    Result<void> Open(const std::string& databasePathUtf8);
    void Close();
    bool IsOpen() const { return !connection_.empty(); }

    // ----- settings (catalog base URL, packages base URL, languages, ...) -----
    std::string Setting(const std::string& key, const std::string& fallback = {}) const;
    Result<void> SetSetting(const std::string& key, const std::string& value);
    Result<std::vector<std::pair<std::string, std::string>>> Settings() const;

    // ----- publishers -----
    Result<void> PutPublisher(const Publisher& publisher);
    Result<void> RemovePublisher(const std::string& id);
    Result<std::vector<Publisher>> Publishers() const;

    // ----- apps and components, whole -----
    Result<void> PutComponent(const Component& component);     // adds or replaces
    Result<Component> GetComponent(const std::string& id) const;
    Result<std::vector<Component>> Components() const;         // apps and components, by id
    Result<std::vector<std::pair<std::string, std::string>>> ComponentUpdates() const;   // id -> when it was last written (ISO 8601), by id
    Result<void> RemoveComponent(const std::string& id);

    // ----- the files behind the packages -----
    Result<void> PutPackageFile(const PackageFile& file);
    Result<PackageFile> GetPackageFile(const std::string& sha256) const;
    Result<std::vector<PackageFile>> PackageFiles() const;
    Result<void> RemovePackageFile(const std::string& sha256);

    // ----- signing keys -----
    Result<KeyRecord> AddKey(const std::string& name, const std::string& publicKeyHex,
                             const std::vector<uint8_t>& sealedSeed);
    Result<std::vector<KeyRecord>> Keys() const;
    Result<KeyRecord> GetKey(int64_t id) const;
    Result<std::vector<uint8_t>> SealedSeed(int64_t id) const;
    Result<void> RetireKey(int64_t id);

    // ----- affiliates, codes, sales -----
    Result<void> PutAffiliate(const Affiliate& affiliate);    // adds or replaces; recruitedBy must exist and not form a chain to itself
    Result<Affiliate> GetAffiliate(const std::string& id) const;
    Result<std::vector<Affiliate>> Affiliates() const;
    Result<void> RemoveAffiliate(const std::string& id);      // refused while a code or a recruit names it
    Result<void> PutCode(const CodeRecord& record);           // adds or replaces; an affiliate code's affiliate must exist
    Result<CodeRecord> GetCode(const std::string& code) const;
    Result<std::vector<CodeRecord>> Codes() const;
    Result<void> RemoveCode(const std::string& code);
    Result<SaleRecord> AddSale(const SaleRecord& sale);       // as given; the credits are the caller's (Affiliates.h, RecordSale)
    Result<std::vector<SaleRecord>> Sales() const;
    bool HasSaleReference(const std::string& reference) const;   // a sale with this reference is in the ledger
    Result<SaleRecord> SaleByReference(const std::string& reference) const;   // NotFound when none

    // ----- the reversals and the payouts (Finance.h records them) -----
    Result<ReversalRecord> AddReversal(const ReversalRecord& reversal);   // as given; the credits taken back are the caller's
    Result<std::vector<ReversalRecord>> Reversals() const;
    bool HasReversalReference(const std::string& reference) const;
    Result<PayoutRecord> AddPayout(const PayoutRecord& payout);
    Result<std::vector<PayoutRecord>> Payouts() const;

    // ----- the pages ever published -----
    Result<void> RememberPage(const std::string& path, const std::string& kind);   // once; the first date stays
    Result<std::vector<PublishedPage>> PublishedPages() const;

    // ----- the catalog's sequence -----
    uint64_t Sequence() const;                 // the last published; 0 before the first
    Result<void> SetSequence(uint64_t sequence);

    // ----- everything at once, for the catalog -----
    Result<void> BeginTransaction();
    Result<void> Commit();
    Result<void> Rollback();

    const std::string& Path() const { return path_; }

private:
    std::string connection_;
    std::string path_;
};

// "2026-10-27T10:00:00Z".
std::string NowIso8601();

} // namespace UltraStoreAdmin

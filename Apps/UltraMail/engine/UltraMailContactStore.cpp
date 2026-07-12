// Apps/UltraMail/engine/UltraMailContactStore.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <map>
#include <string>

namespace UltraMail {

namespace {

void FillContactRow(const UltraDbRow& row, Contact& c) {
    c.id           = row["id"].AsInt64();
    c.displayName  = row["display_name"].AsString();
    c.organization = row["organization"].AsString();
    c.notes        = row["notes"].AsString();
    c.section      = ContactSectionFromString(row["section"].AsString());
}

const char* kContactCols = "id, display_name, organization, notes, section";

} // namespace

UltraDbResult ContactStore::Open(const std::string& connectionName,
                                 const std::string& databasePath) {
    UltraDbConnectionConfig cfg;
    cfg.name = connectionName;
    cfg.driver = "sqlite";
    cfg.database = databasePath;
    UltraDbResult reg = UltraDb_RegisterConnection(cfg);
    if (!reg) return reg;
    connection_ = connectionName;

    std::vector<UltraDbMigration> steps = {
        { 1, "contacts schema",
          "CREATE TABLE contacts("
          "  id INTEGER PRIMARY KEY,"
          "  display_name TEXT NOT NULL,"
          "  organization TEXT,"
          "  notes TEXT,"
          "  section TEXT NOT NULL DEFAULT 'other',"
          "  created_at TEXT);"
          "CREATE TABLE contact_emails("
          "  id INTEGER PRIMARY KEY,"
          "  contact_id INTEGER NOT NULL,"
          "  address TEXT NOT NULL,"
          "  label TEXT,"
          "  is_primary INTEGER DEFAULT 0);"
          "CREATE TABLE contact_phones("
          "  id INTEGER PRIMARY KEY,"
          "  contact_id INTEGER NOT NULL,"
          "  number TEXT NOT NULL,"
          "  label TEXT);"
          "CREATE INDEX idx_contacts_section ON contacts(section);"
          "CREATE INDEX idx_emails_contact ON contact_emails(contact_id);"
          "CREATE INDEX idx_phones_contact ON contact_phones(contact_id);" },
    };
    return UltraDb_Migrate(connection_, steps);
}

UltraDbResult ContactStore::LoadChildren(Contact& c) const {
    c.emails.clear();
    c.phones.clear();

    UltraDbResultSet er;
    UltraDbResult e = UltraDb_Query(connection_,
        "SELECT address, label, is_primary FROM contact_emails "
        "WHERE contact_id=? ORDER BY is_primary DESC, id", { c.id }, er);
    if (!e) return e;
    for (const auto& row : er) {
        ContactEmail em;
        em.address = row["address"].AsString();
        em.label   = row["label"].AsString();
        em.primary = row["is_primary"].AsInt64() != 0;
        c.emails.push_back(std::move(em));
    }

    UltraDbResultSet pr;
    UltraDbResult p = UltraDb_Query(connection_,
        "SELECT number, label FROM contact_phones WHERE contact_id=? ORDER BY id",
        { c.id }, pr);
    if (!p) return p;
    for (const auto& row : pr) {
        ContactPhone ph;
        ph.number = row["number"].AsString();
        ph.label  = row["label"].AsString();
        c.phones.push_back(std::move(ph));
    }
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::ReplaceChildren(UltraDbHandle tx, int64_t contactId,
                                            const Contact& c) {
    UltraDbResult r;
    r = UltraDb_ExecInTx(tx, "DELETE FROM contact_emails WHERE contact_id=?", { contactId });
    if (!r) return r;
    r = UltraDb_ExecInTx(tx, "DELETE FROM contact_phones WHERE contact_id=?", { contactId });
    if (!r) return r;

    for (const auto& e : c.emails) {
        r = UltraDb_ExecInTx(tx,
            "INSERT INTO contact_emails(contact_id, address, label, is_primary) "
            "VALUES(?, ?, ?, ?)",
            { contactId, e.address, e.label, e.primary ? 1 : 0 });
        if (!r) return r;
    }
    for (const auto& p : c.phones) {
        r = UltraDb_ExecInTx(tx,
            "INSERT INTO contact_phones(contact_id, number, label) VALUES(?, ?, ?)",
            { contactId, p.number, p.label });
        if (!r) return r;
    }
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::Save(Contact& c) {
    if (c.displayName.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "contact display name is required");

    UltraDbResult beginErr;
    UltraDbHandle tx = UltraDb_Begin(connection_, &beginErr);
    if (tx == UltraDbInvalidHandle) return beginErr;

    if (c.id == 0) {
        UltraDbResult ins = UltraDb_ExecInTx(tx,
            "INSERT INTO contacts(display_name, organization, notes, section, created_at) "
            "VALUES(?, ?, ?, ?, datetime('now'))",
            { c.displayName, c.organization, c.notes, ToString(c.section) });
        if (!ins) { UltraDb_Rollback(tx); return ins; }
        c.id = ins.lastInsertId;
    } else {
        UltraDbResult upd = UltraDb_ExecInTx(tx,
            "UPDATE contacts SET display_name=?, organization=?, notes=?, section=? WHERE id=?",
            { c.displayName, c.organization, c.notes, ToString(c.section), c.id });
        if (!upd) { UltraDb_Rollback(tx); return upd; }
    }

    UltraDbResult kids = ReplaceChildren(tx, c.id, c);
    if (!kids) { UltraDb_Rollback(tx); return kids; }

    return UltraDb_Commit(tx);
}

UltraDbResult ContactStore::Get(int64_t id, Contact& out) const {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        std::string("SELECT ") + kContactCols + " FROM contacts WHERE id=?", { id }, rs);
    if (!q) return q;
    if (rs.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound, "contact not found");
    FillContactRow(rs.Row(0), out);
    return LoadChildren(out);
}

UltraDbResult ContactStore::Remove(int64_t id) {
    UltraDbHandle tx = UltraDb_Begin(connection_);
    if (tx == UltraDbInvalidHandle)
        return UltraDbResult::Error(UltraDbResultCode::Internal, "begin failed");
    UltraDb_ExecInTx(tx, "DELETE FROM contact_emails WHERE contact_id=?", { id });
    UltraDb_ExecInTx(tx, "DELETE FROM contact_phones WHERE contact_id=?", { id });
    UltraDb_ExecInTx(tx, "DELETE FROM contacts WHERE id=?", { id });
    return UltraDb_Commit(tx);
}

UltraDbResult ContactStore::ListBySection(ContactSection section,
                                          std::vector<Contact>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        std::string("SELECT ") + kContactCols +
        " FROM contacts WHERE section=? ORDER BY display_name COLLATE NOCASE",
        { ToString(section) }, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Contact c;
        FillContactRow(row, c);
        UltraDbResult kids = LoadChildren(c);
        if (!kids) return kids;
        out.push_back(std::move(c));
    }
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::Search(const std::string& query,
                                   std::vector<Contact>& out) const {
    out.clear();
    const std::string like = "%" + query + "%";
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT DISTINCT c.id AS id, c.display_name AS display_name, "
        "c.organization AS organization, c.notes AS notes, c.section AS section "
        "FROM contacts c LEFT JOIN contact_emails e ON e.contact_id = c.id "
        "WHERE c.display_name LIKE ? OR c.organization LIKE ? OR e.address LIKE ? "
        "ORDER BY c.display_name COLLATE NOCASE",
        { like, like, like }, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Contact c;
        FillContactRow(row, c);
        UltraDbResult kids = LoadChildren(c);
        if (!kids) return kids;
        out.push_back(std::move(c));
    }
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::GetSectionCounts(std::vector<SectionCount>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT section, COUNT(*) AS n FROM contacts GROUP BY section", rs);
    if (!q) return q;

    std::map<std::string, int> counts;
    for (const auto& row : rs)
        counts[row["section"].AsString()] = row["n"].AsInt();

    for (ContactSection s : PrimarySections()) {
        SectionCount sc;
        sc.section = s;
        auto it = counts.find(ToString(s));
        sc.count = (it == counts.end()) ? 0 : it->second;
        out.push_back(sc);
    }
    // Include Other only when it has contacts.
    auto other = counts.find(ToString(ContactSection::Other));
    if (other != counts.end() && other->second > 0)
        out.push_back({ ContactSection::Other, other->second });
    return UltraDbResult::Ok();
}

} // namespace UltraMail

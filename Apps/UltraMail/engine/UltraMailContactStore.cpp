// Apps/UltraMail/engine/UltraMailContactStore.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactStore.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetMime.h>

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
    c.group        = row["group_name"].AsString();
}

const char* kContactCols = "id, display_name, organization, notes, section, group_name";

std::string LowerAscii(std::string s) {
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return s;
}

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
        { 2, "groups of the user's own",
          "CREATE TABLE contact_groups("
          "  name TEXT PRIMARY KEY,"
          "  created_at TEXT);"
          "ALTER TABLE contacts ADD COLUMN group_name TEXT NOT NULL DEFAULT '';"
          "CREATE INDEX idx_contacts_group ON contacts(group_name);" },
    };
    UltraDbResult migrated = UltraDb_Migrate(connection_, steps);
    if (!migrated) return migrated;
    RepairJisNames();
    return migrated;
}

void ContactStore::RepairJisNames() {
    // Names collected before UltraNet converted ISO-2022-JP were stored as the
    // raw JIS bytes ("\x1b$B3t<02q...\x1b(B"). The header decoder converts that
    // form now; run it over the stored names that still carry the escape.
    UltraDbResultSet rs;
    if (!UltraDb_Query(connection_,
            "SELECT id, display_name FROM contacts "
            "WHERE instr(display_name, char(27)) > 0", rs))
        return;
    for (const auto& row : rs) {
        const std::string before = row["display_name"].AsString();
        const std::string after = UltraNet_MimeDecodeHeader(before);
        if (after.empty() || after == before) continue;   // this build cannot convert
        UltraDb_Exec(connection_, "UPDATE contacts SET display_name=? WHERE id=?",
                     { after, row["id"].AsInt64() });
    }
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
            "INSERT INTO contacts(display_name, organization, notes, section, group_name, "
            "created_at) VALUES(?, ?, ?, ?, ?, datetime('now'))",
            { c.displayName, c.organization, c.notes, ToString(c.section), c.group });
        if (!ins) { UltraDb_Rollback(tx); return ins; }
        c.id = ins.lastInsertId;
    } else {
        UltraDbResult upd = UltraDb_ExecInTx(tx,
            "UPDATE contacts SET display_name=?, organization=?, notes=?, section=?, "
            "group_name=? WHERE id=?",
            { c.displayName, c.organization, c.notes, ToString(c.section), c.group, c.id });
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

UltraDbResult ContactStore::ListWhere(const std::string& where, const UltraDbParams& params,
                                      std::vector<Contact>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        std::string("SELECT ") + kContactCols + " FROM contacts WHERE " + where +
        " ORDER BY display_name COLLATE NOCASE", params, rs);
    if (!q) return q;
    std::map<int64_t, std::size_t> index;
    out.reserve(rs.Size());
    for (const auto& row : rs) {
        Contact c;
        FillContactRow(row, c);
        index[c.id] = out.size();
        out.push_back(std::move(c));
    }
    if (out.empty()) return UltraDbResult::Ok();

    // The children of all of them at once: a section of a thousand contacts is
    // three queries, not two thousand and one.
    const std::string ids = "SELECT id FROM contacts WHERE " + where;
    UltraDbResultSet er;
    UltraDbResult e = UltraDb_Query(connection_,
        "SELECT contact_id, address, label, is_primary FROM contact_emails "
        "WHERE contact_id IN (" + ids + ") ORDER BY contact_id, is_primary DESC, id",
        params, er);
    if (!e) return e;
    for (const auto& row : er) {
        auto it = index.find(row["contact_id"].AsInt64());
        if (it == index.end()) continue;
        ContactEmail em;
        em.address = row["address"].AsString();
        em.label   = row["label"].AsString();
        em.primary = row["is_primary"].AsInt64() != 0;
        out[it->second].emails.push_back(std::move(em));
    }
    UltraDbResultSet pr;
    UltraDbResult p = UltraDb_Query(connection_,
        "SELECT contact_id, number, label FROM contact_phones "
        "WHERE contact_id IN (" + ids + ") ORDER BY contact_id, id", params, pr);
    if (!p) return p;
    for (const auto& row : pr) {
        auto it = index.find(row["contact_id"].AsInt64());
        if (it == index.end()) continue;
        ContactPhone ph;
        ph.number = row["number"].AsString();
        ph.label  = row["label"].AsString();
        out[it->second].phones.push_back(std::move(ph));
    }
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::ListBySection(ContactSection section,
                                          std::vector<Contact>& out) const {
    return ListWhere("section=? AND group_name=''", { ToString(section) }, out);
}

UltraDbResult ContactStore::ListByGroup(const std::string& name,
                                        std::vector<Contact>& out) const {
    return ListWhere("group_name=?", { name }, out);
}

UltraDbResult ContactStore::ListAll(std::vector<Contact>& out) const {
    return ListWhere("1=1", {}, out);
}

UltraDbResult ContactStore::ListGroups(std::vector<GroupCount>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT g.name AS name, "
        "(SELECT COUNT(*) FROM contacts c WHERE c.group_name = g.name) AS n "
        "FROM contact_groups g ORDER BY g.name COLLATE NOCASE", rs);
    if (!q) return q;
    for (const auto& row : rs) out.push_back({ row["name"].AsString(), row["n"].AsInt() });
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::AddGroup(const std::string& name) {
    if (name.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "a group needs a name");
    const std::string lower = LowerAscii(name);
    for (ContactSection s : { ContactSection::Family, ContactSection::Friends,
                              ContactSection::Work, ContactSection::Leisure,
                              ContactSection::Services, ContactSection::Other })
        if (LowerAscii(DisplayName(s)) == lower)
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "\"" + DisplayName(s) + "\" is already a section");
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT 1 FROM contact_groups WHERE LOWER(name) = LOWER(?)", { name }, rs);
    if (!q) return q;
    if (!rs.Empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "a group called \"" + name + "\" already exists");
    return UltraDb_Exec(connection_,
        "INSERT INTO contact_groups(name, created_at) VALUES(?, datetime('now'))", { name });
}

UltraDbResult ContactStore::RemoveGroup(const std::string& name) {
    UltraDbResult beginErr;
    UltraDbHandle tx = UltraDb_Begin(connection_, &beginErr);
    if (tx == UltraDbInvalidHandle) return beginErr;
    UltraDbResult r = UltraDb_ExecInTx(tx,
        "UPDATE contacts SET group_name='' WHERE group_name=?", { name });
    if (!r) { UltraDb_Rollback(tx); return r; }
    r = UltraDb_ExecInTx(tx, "DELETE FROM contact_groups WHERE name=?", { name });
    if (!r) { UltraDb_Rollback(tx); return r; }
    return UltraDb_Commit(tx);
}

UltraDbResult ContactStore::MoveToSection(int64_t id, ContactSection section) {
    return UltraDb_Exec(connection_,
        "UPDATE contacts SET section=?, group_name='' WHERE id=?",
        { ToString(section), id });
}

UltraDbResult ContactStore::MoveToGroup(int64_t id, const std::string& group) {
    return UltraDb_Exec(connection_,
        "UPDATE contacts SET group_name=? WHERE id=?", { group, id });
}

UltraDbResult ContactStore::Search(const std::string& query,
                                   std::vector<Contact>& out) const {
    out.clear();
    const std::string like = "%" + query + "%";
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT DISTINCT c.id AS id, c.display_name AS display_name, "
        "c.organization AS organization, c.notes AS notes, c.section AS section, "
        "c.group_name AS group_name "
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

UltraDbResult ContactStore::FindByEmail(const std::string& address, Contact& out,
                                        bool& found) const {
    found = false;
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT c.id AS id, c.display_name AS display_name, "
        "c.organization AS organization, c.notes AS notes, c.section AS section, "
        "c.group_name AS group_name "
        "FROM contacts c JOIN contact_emails e ON e.contact_id = c.id "
        "WHERE LOWER(e.address) = LOWER(?) ORDER BY c.id LIMIT 1",
        { address }, rs);
    if (!q) return q;
    if (rs.Empty()) return UltraDbResult::Ok();
    Contact c;
    FillContactRow(rs.Row(0), c);
    UltraDbResult kids = LoadChildren(c);
    if (!kids) return kids;
    out = std::move(c);
    found = true;
    return UltraDbResult::Ok();
}

UltraDbResult ContactStore::GetSectionCounts(std::vector<SectionCount>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT section, COUNT(*) AS n FROM contacts WHERE group_name='' GROUP BY section", rs);
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

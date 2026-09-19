// Apps/UltraFIBU/engine/UltraFIBUStore.cpp
// Schema, migrations and queries. See the header for the four rules that keep
// this file portable between SQLite and PostgreSQL; they are why there is a
// `sequenz` table instead of AUTOINCREMENT and ISO text instead of a date type.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUStore.h"
#include "UltraFIBUUstIdNr.h"

#include <UltraDatabase/UltraDatabaseConnection.h>
#include <UltraDatabase/UltraDatabaseMigrate.h>
#include <UltraDatabase/UltraDatabaseQuery.h>
#include <UltraDatabase/UltraDatabaseTransaction.h>

#include <UltraCrypt/UltraCryptCore.h>

#include <ctime>

namespace UltraFIBU {

namespace {

int64_t NowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string Number(int64_t value) {
    std::string digits;
    int64_t v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (value < 0) digits.insert(digits.begin(), '-');
    return digits;
}

std::string ToLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// An ISO date column, read back. An empty or unreadable column yields an
// invalid Date, which every caller already treats as "not set".
Date DateFrom(const UltraDbValue& value) {
    Date date;
    if (value.IsNull()) return date;
    Date::TryParseIso(value.AsString(), date);
    return date;
}

// A date as a column value: ISO text, or NULL when it is not set. Never an
// empty string, so "not set" is one thing in the database and not two.
UltraDbValue DateValue(const Date& date) {
    if (!date.Valid()) return UltraDbValue::Null();
    return UltraDbValue(date.ToIso());
}

const char* const kSchemaV1 =
    // Surrogate keys and document counters. One mechanism for both, portable
    // to every engine, and the only place a "next number" lives.
    "CREATE TABLE sequenz("
    "  name TEXT PRIMARY KEY,"
    "  naechste BIGINT NOT NULL);"

    "CREATE TABLE mandant("
    "  id BIGINT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  rechtsform TEXT,"
    "  strasse TEXT, plz TEXT, ort TEXT, land TEXT,"
    "  steuernummer TEXT, ust_idnr TEXT,"
    "  finanzamt TEXT, finanzamt_nummer TEXT,"
    "  beraternummer TEXT, mandantennummer TEXT,"
    "  waehrung TEXT,"
    "  besteuerung TEXT, gewinnermittlung TEXT, ustva_rhythmus TEXT,"
    "  dauerfristverlaengerung INTEGER DEFAULT 0,"
    "  kleinunternehmer INTEGER DEFAULT 0,"
    "  oss_registriert INTEGER DEFAULT 0,"
    "  ioss_registriert INTEGER DEFAULT 0,"
    "  telefon TEXT, email TEXT, webseite TEXT,"
    "  iban TEXT, bic TEXT, bank TEXT);"

    // The fiscal year. beginn/ende are ISO dates, so a 1 April year is an
    // ordinary row and no column implies January.
    "CREATE TABLE geschaeftsjahr("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  beginn TEXT NOT NULL,"
    "  ende TEXT NOT NULL,"
    "  status TEXT NOT NULL,"
    "  festschreibung_bis TEXT,"
    "  skr TEXT,"
    "  sachkontenlaenge INTEGER DEFAULT 4,"
    "  bezeichnung TEXT);"
    "CREATE INDEX ix_gj_mandant ON geschaeftsjahr(mandant_id, beginn);"

    "CREATE TABLE konto("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  nummer TEXT NOT NULL,"
    "  bezeichnung TEXT,"
    "  typ TEXT,"
    "  skr TEXT,"
    "  steuerschluessel TEXT,"
    "  euer_zeile TEXT,"
    "  bwa_position TEXT,"
    "  bilanz_position TEXT,"
    "  aktiv INTEGER DEFAULT 1,"
    "  notiz TEXT);"
    "CREATE UNIQUE INDEX ux_konto ON konto(mandant_id, nummer);"

    "CREATE TABLE steuerschluessel("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  schluessel TEXT NOT NULL,"
    "  bezeichnung TEXT,"
    "  art TEXT,"
    "  satz_promille INTEGER DEFAULT 0,"
    "  land TEXT,"
    "  vorsteuer INTEGER DEFAULT 0,"
    "  datev_bu TEXT,"
    "  kz_bemessung TEXT,"
    "  kz_steuer TEXT,"
    "  konto_umsatz TEXT,"
    "  konto_steuer TEXT,"
    "  gueltig_von TEXT,"
    "  gueltig_bis TEXT);"
    "CREATE UNIQUE INDEX ux_steuerschluessel ON steuerschluessel(mandant_id, schluessel, gueltig_von);"

    "CREATE TABLE partner("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  typ TEXT NOT NULL,"
    "  konto TEXT,"
    "  name TEXT NOT NULL,"
    "  name2 TEXT, anrede TEXT, kontaktperson TEXT, abteilung TEXT,"
    "  strasse TEXT, plz TEXT, ort TEXT, land TEXT,"
    "  steuerkategorie TEXT,"
    "  ust_idnr TEXT, ust_idnr_status TEXT, ust_idnr_geprueft_am TEXT,"
    "  ust_idnr_protokoll TEXT,"
    "  steuernummer TEXT,"
    "  email TEXT, telefon TEXT, webseite TEXT,"
    "  iban TEXT, bic TEXT,"
    "  zahlungsfrist_tage INTEGER DEFAULT 14,"
    "  skonto_promille INTEGER DEFAULT 0,"
    "  skonto_tage INTEGER DEFAULT 0,"
    "  sprache TEXT, notiz TEXT,"
    "  aktiv INTEGER DEFAULT 1,"
    "  angelegt_am BIGINT DEFAULT 0,"
    "  geaendert_am BIGINT DEFAULT 0,"
    "  version BIGINT DEFAULT 1);"
    "CREATE UNIQUE INDEX ux_partner_konto ON partner(mandant_id, konto);"
    "CREATE INDEX ix_partner_name ON partner(mandant_id, name);"
    "CREATE INDEX ix_partner_ustid ON partner(mandant_id, ust_idnr);"

    // Users from the first schema, not from the day the server arrives: GoBD
    // attribution cannot be backfilled.
    "CREATE TABLE benutzer("
    "  id BIGINT PRIMARY KEY,"
    "  anmeldename TEXT NOT NULL,"
    "  anzeigename TEXT,"
    "  email TEXT,"
    "  rolle TEXT NOT NULL,"
    "  aktiv INTEGER DEFAULT 1,"
    "  angelegt_am BIGINT DEFAULT 0,"
    "  letzter_login BIGINT DEFAULT 0,"
    "  passwort_hash TEXT,"
    "  passwort_salt TEXT,"
    "  kdf_algorithmus TEXT,"
    "  kdf_iterationen INTEGER DEFAULT 0,"
    "  kdf_speicher_kib INTEGER DEFAULT 0,"
    "  kdf_laenge INTEGER DEFAULT 32);"
    "CREATE UNIQUE INDEX ux_benutzer_name ON benutzer(anmeldename);"

    "CREATE TABLE nummernkreis("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  kreis TEXT NOT NULL,"
    "  praefix TEXT,"
    "  naechste BIGINT DEFAULT 1,"
    "  stellen INTEGER DEFAULT 4,"
    "  jaehrlich_zuruecksetzen INTEGER DEFAULT 0,"
    "  letztes_jahr INTEGER DEFAULT 0);"
    "CREATE UNIQUE INDEX ux_nummernkreis ON nummernkreis(mandant_id, kreis);"

    "CREATE TABLE audit("
    "  id BIGINT PRIMARY KEY,"
    "  zeit BIGINT NOT NULL,"
    "  benutzer_id BIGINT DEFAULT 0,"
    "  benutzer TEXT,"
    "  tabelle TEXT,"
    "  row_id BIGINT DEFAULT 0,"
    "  aktion TEXT,"
    "  details TEXT);"
    "CREATE INDEX ix_audit_zeit ON audit(zeit DESC);";

} // namespace

// ===== OPENING =====

StoreResult Store::Open(const std::string& connectionName, const std::string& databasePath) {
    // Re-registering a name replaces the pooled entry and drops the physical
    // connection - which for ":memory:" would throw the database away. So it
    // only happens when the name is new or now points somewhere else.
    bool needsRegistration = true;
    if (UltraDb_HasConnection(connectionName)) {
        UltraDbConnectionInfo info;
        if (UltraDb_GetConnectionInfo(connectionName, info) && info.database == databasePath)
            needsRegistration = false;
    }
    if (needsRegistration) {
        UltraDbConnectionConfig cfg;
        cfg.name     = connectionName;
        cfg.driver   = "sqlite";
        cfg.database = databasePath;
        const UltraDbResult reg = UltraDb_RegisterConnection(cfg);
        if (!reg)
            return StoreResult::Fail("Die Datenbank konnte nicht geöffnet werden: " +
                                     reg.message);
    }
    connection_ = connectionName;

    // An old binary must never write into a newer schema: the columns it does
    // not know about would silently stay empty, and in a ledger that is
    // corruption rather than inconvenience.
    const int existing = SchemaVersion();
    if (existing > kSchemaVersion) {
        const std::string fehler =
            "Die Datenbank hat Schema-Version " + Number(existing) +
            ", dieses Programm kennt nur " + Number(kSchemaVersion) +
            ". Bitte UltraFIBU aktualisieren.";
        connection_.clear();
        return StoreResult::Fail(fehler);
    }

    const std::vector<UltraDbMigration> steps = {
        { 1, "UltraFIBU Stammdaten", kSchemaV1 }
    };
    const UltraDbResult migrated = UltraDb_Migrate(connection_, steps);
    if (!migrated) {
        connection_.clear();
        return StoreResult::Fail("Das Datenbank-Schema konnte nicht angelegt werden: " +
                                 migrated.message);
    }
    return StoreResult::Ok();
}

StoreResult Store::OpenServer(const std::string& connectionName,
                              const std::string& host, int port,
                              const std::string& database,
                              const std::string& user,
                              const std::string& credentialsRef) {
    (void)connectionName; (void)host; (void)port; (void)database; (void)user;
    // Deliberately explicit rather than a silent fallback to SQLite: a
    // multi-user installation that quietly became single-user would be
    // discovered by two people overwriting each other's work.
    if (credentialsRef.find("vault:") != 0)
        return StoreResult::Fail("Server-Zugangsdaten müssen als UltraVault-Schlüssel "
                                 "angegeben werden (vault:...), nicht als Passwort.");
    return StoreResult::Fail(
        "Der Mehrplatz-Betrieb ist vorbereitet, aber der PostgreSQL-Treiber von "
        "UltraDatabase ist noch nicht gebaut (Stage 2). Bis dahin bitte eine lokale "
        "Datenbank verwenden - das Schema ist dasselbe, ein Wechsel erfordert keine "
        "Änderung an den Daten.");
}

void Store::Close() {
    if (connection_.empty()) return;
    UltraDb_CloseConnection(connection_);
    connection_.clear();
}

int Store::SchemaVersion() const {
    if (connection_.empty()) return 0;
    int version = 0;
    if (!UltraDb_GetSchemaVersion(connection_, version)) return 0;
    return version;
}

// ===== QUERY HELPERS =====

StoreResult Store::Exec(const std::string& sql, const UltraDbParams& params,
                        const std::string& wobei) const {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    const UltraDbResult result = UltraDb_Exec(connection_, sql, params);
    if (!result) return StoreResult::Fail(wobei + ": " + result.message);
    return StoreResult::Ok();
}

bool Store::Query(const std::string& sql, const UltraDbParams& params,
                  UltraDbResultSet& out) const {
    if (connection_.empty()) return false;
    return static_cast<bool>(UltraDb_Query(connection_, sql, params, out));
}

bool Store::QueryOne(const std::string& sql, const UltraDbParams& params,
                     UltraDbRow& out) const {
    UltraDbResultSet rs;
    if (!Query(sql, params, rs) || rs.Empty()) return false;
    out = rs.Row(0);
    return true;
}

// ===== SEQUENCES AND NUMBERS =====

StoreResult Store::NextSequenceValue(const std::string& name, int64_t& out) {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " + error.message);

    UltraDbResultSet rs;
    const UltraDbResult read = UltraDb_QueryInTx(
        tx, "SELECT naechste FROM sequenz WHERE name = ?", { name }, rs);
    if (!read) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Nummernvergabe fehlgeschlagen: " + read.message);
    }

    int64_t value = 1;
    if (rs.Empty()) {
        const UltraDbResult insert = UltraDb_ExecInTx(
            tx, "INSERT INTO sequenz(name, naechste) VALUES(?, ?)", { name, int64_t(2) });
        if (!insert) {
            UltraDb_Rollback(tx);
            return StoreResult::Fail("Nummernkreis konnte nicht angelegt werden: " + insert.message);
        }
    } else {
        value = rs.Row(0)["naechste"].AsInt64();
        const UltraDbResult bump = UltraDb_ExecInTx(
            tx, "UPDATE sequenz SET naechste = naechste + 1 WHERE name = ?", { name });
        if (!bump) {
            UltraDb_Rollback(tx);
            return StoreResult::Fail("Nummernkreis konnte nicht fortgeschrieben werden: " +
                                     bump.message);
        }
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit) return StoreResult::Fail("Nummernvergabe nicht bestätigt: " + commit.message);
    out = value;
    return StoreResult::Ok();
}

StoreResult Store::NextBelegnummer(int64_t mandantId, const std::string& kreisName,
                                   const Date& datum, std::string& out) {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!datum.Valid()) return StoreResult::Fail("Ohne gültiges Datum kann keine Nummer "
                                                 "vergeben werden.");

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " + error.message);

    UltraDbResultSet rs;
    const UltraDbResult read = UltraDb_QueryInTx(
        tx,
        "SELECT id, kreis, praefix, naechste, stellen, jaehrlich_zuruecksetzen, letztes_jahr "
        "FROM nummernkreis WHERE mandant_id = ? AND kreis = ?",
        { mandantId, kreisName }, rs);
    if (!read || rs.Empty()) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Der Nummernkreis \"" + kreisName + "\" ist nicht angelegt.");
    }

    const UltraDbRow& row = rs.Row(0);
    Nummernkreis kreis;
    kreis.id        = row["id"].AsInt64();
    kreis.mandantId = mandantId;
    kreis.kreis     = row["kreis"].AsString();
    kreis.praefix   = row["praefix"].AsString();
    kreis.naechste  = row["naechste"].AsInt64();
    kreis.stellen   = row["stellen"].AsInt();
    kreis.jaehrlichZuruecksetzen = row["jaehrlich_zuruecksetzen"].AsInt() != 0;
    kreis.letztesJahr = row["letztes_jahr"].AsInt();

    int64_t wert = kreis.naechste;
    if (kreis.jaehrlichZuruecksetzen && kreis.letztesJahr != datum.year) {
        wert = 1;                                  // a new year starts at one
    }

    const UltraDbResult bump = UltraDb_ExecInTx(
        tx, "UPDATE nummernkreis SET naechste = ?, letztes_jahr = ? WHERE id = ?",
        { wert + 1, datum.year, kreis.id });
    if (!bump) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Nummernkreis konnte nicht fortgeschrieben werden: " +
                                 bump.message);
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit) return StoreResult::Fail("Nummernvergabe nicht bestätigt: " + commit.message);

    out = FormatNummer(kreis, wert, datum);
    return StoreResult::Ok();
}

StoreResult Store::SaveNummernkreis(Nummernkreis& kreis, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Nummernkreise ändern.");
    if (!kreis.Valid()) return StoreResult::Fail("Der Nummernkreis braucht einen Namen.");

    if (kreis.id == 0) {
        const StoreResult seq = NextSequenceValue("nummernkreis", kreis.id);
        if (!seq) return seq;
        const StoreResult inserted = Exec(
            "INSERT INTO nummernkreis(id, mandant_id, kreis, praefix, naechste, stellen,"
            " jaehrlich_zuruecksetzen, letztes_jahr) VALUES(?,?,?,?,?,?,?,?)",
            { kreis.id, kreis.mandantId, kreis.kreis, kreis.praefix, kreis.naechste,
              kreis.stellen, kreis.jaehrlichZuruecksetzen ? 1 : 0, kreis.letztesJahr },
            "Nummernkreis konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE nummernkreis SET praefix = ?, stellen = ?, jaehrlich_zuruecksetzen = ? "
            "WHERE id = ?",
            { kreis.praefix, kreis.stellen, kreis.jaehrlichZuruecksetzen ? 1 : 0, kreis.id },
            "Nummernkreis konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "nummernkreis", kreis.id, "speichern", kreis.kreis);
}

std::vector<Nummernkreis> Store::Nummernkreise(int64_t mandantId) const {
    std::vector<Nummernkreis> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, mandant_id, kreis, praefix, naechste, stellen,"
               " jaehrlich_zuruecksetzen, letztes_jahr FROM nummernkreis "
               "WHERE mandant_id = ? ORDER BY kreis", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        Nummernkreis kreis;
        kreis.id        = row["id"].AsInt64();
        kreis.mandantId = row["mandant_id"].AsInt64();
        kreis.kreis     = row["kreis"].AsString();
        kreis.praefix   = row["praefix"].AsString();
        kreis.naechste  = row["naechste"].AsInt64();
        kreis.stellen   = row["stellen"].AsInt();
        kreis.jaehrlichZuruecksetzen = row["jaehrlich_zuruecksetzen"].AsInt() != 0;
        kreis.letztesJahr = row["letztes_jahr"].AsInt();
        liste.push_back(kreis);
    }
    return liste;
}

// ===== AUDIT =====

StoreResult Store::WriteAudit(const Akteur& akteur, const std::string& tabelle,
                              int64_t rowId, const std::string& aktion,
                              const std::string& details) {
    int64_t id = 0;
    const StoreResult seq = NextSequenceValue("audit", id);
    if (!seq) return seq;
    return Exec("INSERT INTO audit(id, zeit, benutzer_id, benutzer, tabelle, row_id, aktion,"
                " details) VALUES(?,?,?,?,?,?,?,?)",
                { id, NowSeconds(), akteur.benutzerId, akteur.anmeldename, tabelle, rowId,
                  aktion, details },
                "Der Protokolleintrag konnte nicht geschrieben werden");
}

std::vector<Store::AuditEintrag> Store::AuditListe(size_t limit) const {
    std::vector<AuditEintrag> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, zeit, benutzer_id, benutzer, tabelle, row_id, aktion, details "
               "FROM audit ORDER BY zeit DESC, id DESC LIMIT ?",
               { static_cast<int64_t>(limit) }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        AuditEintrag eintrag;
        eintrag.id         = row["id"].AsInt64();
        eintrag.zeit       = row["zeit"].AsInt64();
        eintrag.benutzerId = row["benutzer_id"].AsInt64();
        eintrag.benutzer   = row["benutzer"].AsString();
        eintrag.tabelle    = row["tabelle"].AsString();
        eintrag.rowId      = row["row_id"].AsInt64();
        eintrag.aktion     = row["aktion"].AsString();
        eintrag.details    = row["details"].AsString();
        liste.push_back(eintrag);
    }
    return liste;
}

// ===== MANDANT =====

StoreResult Store::SaveMandant(Mandant& mandant, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Stammdaten ändern.");
    if (!mandant.Valid()) return StoreResult::Fail("Der Mandant braucht einen Namen.");

    const UltraDbParams felder = {
        mandant.name, mandant.rechtsform, mandant.strasse, mandant.plz, mandant.ort,
        mandant.land, mandant.steuernummer, mandant.ustIdNr, mandant.finanzamt,
        mandant.finanzamtNummer, mandant.beraternummer, mandant.mandantennummer,
        mandant.waehrung, BesteuerungsartToText(mandant.besteuerung),
        GewinnermittlungToText(mandant.gewinnermittlung),
        UstvaRhythmusToText(mandant.ustvaRhythmus),
        mandant.dauerfristverlaengerung ? 1 : 0, mandant.kleinunternehmer ? 1 : 0,
        mandant.ossRegistriert ? 1 : 0, mandant.iossRegistriert ? 1 : 0,
        mandant.telefon, mandant.email, mandant.webseite,
        mandant.iban, mandant.bic, mandant.bank
    };

    if (mandant.id == 0) {
        const StoreResult seq = NextSequenceValue("mandant", mandant.id);
        if (!seq) return seq;
        UltraDbParams params = { mandant.id };
        params.insert(params.end(), felder.begin(), felder.end());
        const StoreResult inserted = Exec(
            "INSERT INTO mandant(id, name, rechtsform, strasse, plz, ort, land, steuernummer,"
            " ust_idnr, finanzamt, finanzamt_nummer, beraternummer, mandantennummer, waehrung,"
            " besteuerung, gewinnermittlung, ustva_rhythmus, dauerfristverlaengerung,"
            " kleinunternehmer, oss_registriert, ioss_registriert, telefon, email, webseite,"
            " iban, bic, bank) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            params, "Der Mandant konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        UltraDbParams params = felder;
        params.push_back(mandant.id);
        const StoreResult updated = Exec(
            "UPDATE mandant SET name = ?, rechtsform = ?, strasse = ?, plz = ?, ort = ?,"
            " land = ?, steuernummer = ?, ust_idnr = ?, finanzamt = ?, finanzamt_nummer = ?,"
            " beraternummer = ?, mandantennummer = ?, waehrung = ?, besteuerung = ?,"
            " gewinnermittlung = ?, ustva_rhythmus = ?, dauerfristverlaengerung = ?,"
            " kleinunternehmer = ?, oss_registriert = ?, ioss_registriert = ?, telefon = ?,"
            " email = ?, webseite = ?, iban = ?, bic = ?, bank = ? WHERE id = ?",
            params, "Der Mandant konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "mandant", mandant.id, "speichern", mandant.name);
}

namespace {

Mandant MandantFromRow(const UltraDbRow& row) {
    Mandant m;
    m.id              = row["id"].AsInt64();
    m.name            = row["name"].AsString();
    m.rechtsform      = row["rechtsform"].AsString();
    m.strasse         = row["strasse"].AsString();
    m.plz             = row["plz"].AsString();
    m.ort             = row["ort"].AsString();
    m.land            = row["land"].AsString();
    m.steuernummer    = row["steuernummer"].AsString();
    m.ustIdNr         = row["ust_idnr"].AsString();
    m.finanzamt       = row["finanzamt"].AsString();
    m.finanzamtNummer = row["finanzamt_nummer"].AsString();
    m.beraternummer   = row["beraternummer"].AsString();
    m.mandantennummer = row["mandantennummer"].AsString();
    m.waehrung        = row["waehrung"].AsString();
    BesteuerungsartFromText(row["besteuerung"].AsString(), m.besteuerung);
    GewinnermittlungFromText(row["gewinnermittlung"].AsString(), m.gewinnermittlung);
    UstvaRhythmusFromText(row["ustva_rhythmus"].AsString(), m.ustvaRhythmus);
    m.dauerfristverlaengerung = row["dauerfristverlaengerung"].AsInt() != 0;
    m.kleinunternehmer        = row["kleinunternehmer"].AsInt() != 0;
    m.ossRegistriert          = row["oss_registriert"].AsInt() != 0;
    m.iossRegistriert         = row["ioss_registriert"].AsInt() != 0;
    m.telefon  = row["telefon"].AsString();
    m.email    = row["email"].AsString();
    m.webseite = row["webseite"].AsString();
    m.iban     = row["iban"].AsString();
    m.bic      = row["bic"].AsString();
    m.bank     = row["bank"].AsString();
    return m;
}

const char* const kMandantColumns =
    "id, name, rechtsform, strasse, plz, ort, land, steuernummer, ust_idnr, finanzamt,"
    " finanzamt_nummer, beraternummer, mandantennummer, waehrung, besteuerung,"
    " gewinnermittlung, ustva_rhythmus, dauerfristverlaengerung, kleinunternehmer,"
    " oss_registriert, ioss_registriert, telefon, email, webseite, iban, bic, bank";

} // namespace

bool Store::LoadMandant(int64_t id, Mandant& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kMandantColumns + " FROM mandant WHERE id = ?",
                  { id }, row))
        return false;
    out = MandantFromRow(row);
    return true;
}

std::vector<Mandant> Store::Mandanten() const {
    std::vector<Mandant> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kMandantColumns + " FROM mandant ORDER BY name", {}, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(MandantFromRow(row));
    return liste;
}

// ===== GESCHAEFTSJAHR =====

namespace {

Geschaeftsjahr GeschaeftsjahrFromRow(const UltraDbRow& row) {
    Geschaeftsjahr jahr;
    jahr.id        = row["id"].AsInt64();
    jahr.mandantId = row["mandant_id"].AsInt64();
    jahr.beginn    = DateFrom(row["beginn"]);
    jahr.ende      = DateFrom(row["ende"]);
    GeschaeftsjahrStatusFromText(row["status"].AsString(), jahr.status);
    jahr.festschreibungBis = DateFrom(row["festschreibung_bis"]);
    jahr.skr               = row["skr"].AsString();
    jahr.sachkontenlaenge  = row["sachkontenlaenge"].AsInt();
    jahr.bezeichnung       = row["bezeichnung"].AsString();
    return jahr;
}

const char* const kGjColumns =
    "id, mandant_id, beginn, ende, status, festschreibung_bis, skr, sachkontenlaenge,"
    " bezeichnung";

} // namespace

StoreResult Store::SaveGeschaeftsjahr(Geschaeftsjahr& jahr, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf kein Geschäftsjahr anlegen.");
    if (!jahr.Valid())
        return StoreResult::Fail(
            "Das Geschäftsjahr muss am Monatsersten beginnen, am Monatsletzten enden "
            "und darf höchstens zwölf Monate umfassen.");
    if (jahr.bezeichnung.empty()) jahr.bezeichnung = jahr.DefaultBezeichnung();

    // No overlap, and no gap after the latest existing year: a posting that
    // belongs to two fiscal years, or to none, has no correct report.
    const std::vector<Geschaeftsjahr> vorhanden = Geschaeftsjahre(jahr.mandantId);
    const Geschaeftsjahr* letztes = nullptr;
    for (const Geschaeftsjahr& other : vorhanden) {
        if (other.id == jahr.id) continue;
        if (GeschaeftsjahreOverlap(other, jahr))
            return StoreResult::Fail("Das Geschäftsjahr überschneidet sich mit \"" +
                                     other.bezeichnung + "\" (" + FormatDateGerman(other.beginn) +
                                     " - " + FormatDateGerman(other.ende) + ").");
        if (!letztes || other.ende > letztes->ende) letztes = &other;
    }
    if (letztes && jahr.beginn > letztes->ende && !GeschaeftsjahreContiguous(*letztes, jahr))
        return StoreResult::Fail("Zwischen \"" + letztes->bezeichnung + "\" (endet " +
                                 FormatDateGerman(letztes->ende) + ") und dem neuen "
                                 "Geschäftsjahr liegt eine Lücke. "
                                 "Geschäftsjahre müssen lückenlos "
                                 "aufeinander folgen.");

    if (jahr.id == 0) {
        const StoreResult seq = NextSequenceValue("geschaeftsjahr", jahr.id);
        if (!seq) return seq;
        const StoreResult inserted = Exec(
            "INSERT INTO geschaeftsjahr(id, mandant_id, beginn, ende, status,"
            " festschreibung_bis, skr, sachkontenlaenge, bezeichnung) "
            "VALUES(?,?,?,?,?,?,?,?,?)",
            { jahr.id, jahr.mandantId, jahr.beginn.ToIso(), jahr.ende.ToIso(),
              GeschaeftsjahrStatusToText(jahr.status), DateValue(jahr.festschreibungBis),
              jahr.skr, jahr.sachkontenlaenge, jahr.bezeichnung },
            "Das Geschäftsjahr konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE geschaeftsjahr SET beginn = ?, ende = ?, status = ?, skr = ?,"
            " sachkontenlaenge = ?, bezeichnung = ? WHERE id = ?",
            { jahr.beginn.ToIso(), jahr.ende.ToIso(), GeschaeftsjahrStatusToText(jahr.status),
              jahr.skr, jahr.sachkontenlaenge, jahr.bezeichnung, jahr.id },
            "Das Geschäftsjahr konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "geschaeftsjahr", jahr.id, "speichern",
                      jahr.bezeichnung + " " + jahr.beginn.ToIso() + ".." + jahr.ende.ToIso());
}

std::vector<Geschaeftsjahr> Store::Geschaeftsjahre(int64_t mandantId) const {
    std::vector<Geschaeftsjahr> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kGjColumns +
               " FROM geschaeftsjahr WHERE mandant_id = ? ORDER BY beginn", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(GeschaeftsjahrFromRow(row));
    return liste;
}

bool Store::GeschaeftsjahrById(int64_t id, Geschaeftsjahr& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kGjColumns + " FROM geschaeftsjahr WHERE id = ?",
                  { id }, row))
        return false;
    out = GeschaeftsjahrFromRow(row);
    return true;
}

bool Store::GeschaeftsjahrAt(int64_t mandantId, const Date& datum, Geschaeftsjahr& out) const {
    if (!datum.Valid()) return false;
    // String comparison on ISO dates is chronological, which is exactly why
    // the dates are stored this way - and it works identically on both engines.
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kGjColumns +
                  " FROM geschaeftsjahr WHERE mandant_id = ? AND beginn <= ? AND ende >= ?",
                  { mandantId, datum.ToIso(), datum.ToIso() }, row))
        return false;
    out = GeschaeftsjahrFromRow(row);
    return true;
}

StoreResult Store::Festschreiben(int64_t geschaeftsjahrId, const Date& bis,
                                 const Akteur& akteur) {
    if (!akteur.Darf(Recht::Festschreiben))
        return StoreResult::Fail("Diese Rolle darf nicht festschreiben.");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrById(geschaeftsjahrId, jahr))
        return StoreResult::Fail("Das Geschäftsjahr wurde nicht gefunden.");
    if (!jahr.Contains(bis))
        return StoreResult::Fail("Das Datum liegt nicht im Geschäftsjahr \"" +
                                 jahr.bezeichnung + "\".");
    // Festschreibung only ever moves forward. Un-freezing is not a feature
    // that was forgotten - it is what the GoBD forbid, and a correction is a
    // Storno.
    if (jahr.festschreibungBis.Valid() && bis <= jahr.festschreibungBis) {
        const StoreResult refused = StoreResult::Fail(
            "Bis " + FormatDateGerman(jahr.festschreibungBis) +
            " ist bereits festgeschrieben. Eine Festschreibung kann nicht "
            "zurückgenommen werden - Korrekturen erfolgen über eine Stornierung.");
        WriteAudit(akteur, "geschaeftsjahr", jahr.id, "abgelehnt",
                   "Festschreibung rückwärts auf " + bis.ToIso());
        return refused;
    }

    const StoreResult updated = Exec(
        "UPDATE geschaeftsjahr SET festschreibung_bis = ?, status = ? WHERE id = ?",
        { bis.ToIso(), GeschaeftsjahrStatusToText(GeschaeftsjahrStatus::Festgeschrieben),
          jahr.id },
        "Die Festschreibung konnte nicht gespeichert werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "geschaeftsjahr", jahr.id, "festschreiben", bis.ToIso());
}

// ===== KONTENRAHMEN =====

namespace {

Konto KontoFromRow(const UltraDbRow& row) {
    Konto konto;
    konto.id               = row["id"].AsInt64();
    konto.mandantId        = row["mandant_id"].AsInt64();
    konto.nummer           = row["nummer"].AsString();
    konto.bezeichnung      = row["bezeichnung"].AsString();
    KontoTypFromText(row["typ"].AsString(), konto.typ);
    konto.skr              = row["skr"].AsString();
    konto.steuerschluessel = row["steuerschluessel"].AsString();
    konto.eurZeile         = row["euer_zeile"].AsString();
    konto.bwaPosition      = row["bwa_position"].AsString();
    konto.bilanzPosition   = row["bilanz_position"].AsString();
    konto.aktiv            = row["aktiv"].AsInt() != 0;
    konto.notiz            = row["notiz"].AsString();
    return konto;
}

const char* const kKontoColumns =
    "id, mandant_id, nummer, bezeichnung, typ, skr, steuerschluessel, euer_zeile,"
    " bwa_position, bilanz_position, aktiv, notiz";

} // namespace

StoreResult Store::SaveKonto(Konto& konto, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf den Kontenrahmen nicht ändern.");
    if (!konto.Valid())
        return StoreResult::Fail("Ein Konto braucht Nummer und Bezeichnung.");

    Konto vorhanden;
    if (konto.id == 0 && KontoByNummer(konto.mandantId, konto.nummer, vorhanden))
        konto.id = vorhanden.id;                  // same number, same account

    if (konto.id == 0) {
        const StoreResult seq = NextSequenceValue("konto", konto.id);
        if (!seq) return seq;
        return Exec(
            "INSERT INTO konto(id, mandant_id, nummer, bezeichnung, typ, skr,"
            " steuerschluessel, euer_zeile, bwa_position, bilanz_position, aktiv, notiz) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            { konto.id, konto.mandantId, konto.nummer, konto.bezeichnung,
              KontoTypToText(konto.typ), konto.skr, konto.steuerschluessel, konto.eurZeile,
              konto.bwaPosition, konto.bilanzPosition, konto.aktiv ? 1 : 0, konto.notiz },
            "Das Konto konnte nicht angelegt werden");
    }
    return Exec(
        "UPDATE konto SET bezeichnung = ?, typ = ?, skr = ?, steuerschluessel = ?,"
        " euer_zeile = ?, bwa_position = ?, bilanz_position = ?, aktiv = ?, notiz = ? "
        "WHERE id = ?",
        { konto.bezeichnung, KontoTypToText(konto.typ), konto.skr, konto.steuerschluessel,
          konto.eurZeile, konto.bwaPosition, konto.bilanzPosition, konto.aktiv ? 1 : 0,
          konto.notiz, konto.id },
        "Das Konto konnte nicht geändert werden");
}

StoreResult Store::ImportKonten(int64_t mandantId, const std::vector<Konto>& konten,
                                const Akteur& akteur, int& outWritten) {
    outWritten = 0;
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keinen Kontenrahmen importieren.");

    for (const Konto& source : konten) {
        Konto konto = source;
        konto.mandantId = mandantId;
        const StoreResult saved = SaveKonto(konto, akteur);
        if (!saved)
            return StoreResult::Fail("Konto " + source.nummer + ": " + saved.fehler);
        ++outWritten;
    }
    return WriteAudit(akteur, "konto", 0, "import",
                      "Kontenrahmen: " + std::to_string(outWritten) + " Konten");
}

std::vector<Konto> Store::Konten(int64_t mandantId) const {
    std::vector<Konto> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kKontoColumns +
               " FROM konto WHERE mandant_id = ? ORDER BY nummer", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(KontoFromRow(row));
    return liste;
}

bool Store::KontoByNummer(int64_t mandantId, const std::string& nummer, Konto& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kKontoColumns +
                  " FROM konto WHERE mandant_id = ? AND nummer = ?", { mandantId, nummer }, row))
        return false;
    out = KontoFromRow(row);
    return true;
}

// ===== STEUERSCHLUESSEL =====

namespace {

Steuerschluessel SteuerschluesselFromRow(const UltraDbRow& row) {
    Steuerschluessel key;
    key.id           = row["id"].AsInt64();
    key.mandantId    = row["mandant_id"].AsInt64();
    key.schluessel   = row["schluessel"].AsString();
    key.bezeichnung  = row["bezeichnung"].AsString();
    SteuerArtFromText(row["art"].AsString(), key.art);
    key.satzPromille = row["satz_promille"].AsInt();
    key.land         = row["land"].AsString();
    key.vorsteuer    = row["vorsteuer"].AsInt() != 0;
    key.datevBu      = row["datev_bu"].AsString();
    key.kzBemessung  = row["kz_bemessung"].AsString();
    key.kzSteuer     = row["kz_steuer"].AsString();
    key.kontoUmsatz  = row["konto_umsatz"].AsString();
    key.kontoSteuer  = row["konto_steuer"].AsString();
    key.gueltigVon   = DateFrom(row["gueltig_von"]);
    key.gueltigBis   = DateFrom(row["gueltig_bis"]);
    return key;
}

const char* const kSteuerColumns =
    "id, mandant_id, schluessel, bezeichnung, art, satz_promille, land, vorsteuer,"
    " datev_bu, kz_bemessung, kz_steuer, konto_umsatz, konto_steuer, gueltig_von, gueltig_bis";

} // namespace

StoreResult Store::SaveSteuerschluessel(Steuerschluessel& key, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (!key.Valid()) return StoreResult::Fail("Ein Steuerschlüssel braucht einen Schlüssel.");

    // The same key with the same validity start is the same row: a rate change
    // is a new row with a new gueltig_von, which is what keeps a closed year
    // reportable after the change.
    if (key.id == 0) {
        UltraDbRow row;
        if (QueryOne(std::string("SELECT ") + kSteuerColumns +
                     " FROM steuerschluessel WHERE mandant_id = ? AND schluessel = ?"
                     " AND gueltig_von IS ?",
                     { key.mandantId, key.schluessel, DateValue(key.gueltigVon) }, row))
            key.id = row["id"].AsInt64();
    }

    if (key.id == 0) {
        const StoreResult seq = NextSequenceValue("steuerschluessel", key.id);
        if (!seq) return seq;
        return Exec(
            "INSERT INTO steuerschluessel(id, mandant_id, schluessel, bezeichnung, art,"
            " satz_promille, land, vorsteuer, datev_bu, kz_bemessung, kz_steuer, konto_umsatz,"
            " konto_steuer, gueltig_von, gueltig_bis) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { key.id, key.mandantId, key.schluessel, key.bezeichnung, SteuerArtToText(key.art),
              key.satzPromille, key.land, key.vorsteuer ? 1 : 0, key.datevBu, key.kzBemessung,
              key.kzSteuer, key.kontoUmsatz, key.kontoSteuer, DateValue(key.gueltigVon),
              DateValue(key.gueltigBis) },
            "Der Steuerschlüssel konnte nicht angelegt werden");
    }
    return Exec(
        "UPDATE steuerschluessel SET bezeichnung = ?, art = ?, satz_promille = ?, land = ?,"
        " vorsteuer = ?, datev_bu = ?, kz_bemessung = ?, kz_steuer = ?, konto_umsatz = ?,"
        " konto_steuer = ?, gueltig_bis = ? WHERE id = ?",
        { key.bezeichnung, SteuerArtToText(key.art), key.satzPromille, key.land,
          key.vorsteuer ? 1 : 0, key.datevBu, key.kzBemessung, key.kzSteuer, key.kontoUmsatz,
          key.kontoSteuer, DateValue(key.gueltigBis), key.id },
        "Der Steuerschlüssel konnte nicht geändert werden");
}

StoreResult Store::ImportSteuerschluessel(int64_t mandantId,
                                          const std::vector<Steuerschluessel>& schluessel,
                                          const Akteur& akteur, int& outWritten) {
    outWritten = 0;
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel importieren.");
    for (const Steuerschluessel& source : schluessel) {
        Steuerschluessel key = source;
        key.mandantId = mandantId;
        const StoreResult saved = SaveSteuerschluessel(key, akteur);
        if (!saved)
            return StoreResult::Fail("Steuerschlüssel " + source.schluessel + ": " +
                                     saved.fehler);
        ++outWritten;
    }
    return WriteAudit(akteur, "steuerschluessel", 0, "import",
                      std::to_string(outWritten) + " Schlüssel");
}

std::vector<Steuerschluessel> Store::SteuerschluesselListe(int64_t mandantId,
                                                           const Date& gueltigAm) const {
    std::vector<Steuerschluessel> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kSteuerColumns +
               " FROM steuerschluessel WHERE mandant_id = ? ORDER BY schluessel, gueltig_von",
               { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        const Steuerschluessel key = SteuerschluesselFromRow(row);
        // Validity is decided in C++, not in SQL: the rule involves an open
        // end and an unset start, and expressing that in portable SQL costs
        // more than it saves.
        if (gueltigAm.Valid() && !key.GueltigAm(gueltigAm)) continue;
        liste.push_back(key);
    }
    return liste;
}

bool Store::SteuerschluesselByKey(int64_t mandantId, const std::string& schluessel,
                                  const Date& gueltigAm, Steuerschluessel& out) const {
    const std::vector<Steuerschluessel> liste = SteuerschluesselListe(mandantId, gueltigAm);
    for (const Steuerschluessel& key : liste) {
        if (key.schluessel == schluessel) { out = key; return true; }
    }
    return false;
}

// ===== PARTNER =====

namespace {

Partner PartnerFromRow(const UltraDbRow& row) {
    Partner p;
    p.id            = row["id"].AsInt64();
    p.mandantId     = row["mandant_id"].AsInt64();
    PartnerTypFromText(row["typ"].AsString(), p.typ);
    p.konto         = row["konto"].AsString();
    p.name          = row["name"].AsString();
    p.name2         = row["name2"].AsString();
    p.anrede        = row["anrede"].AsString();
    p.kontaktperson = row["kontaktperson"].AsString();
    p.abteilung     = row["abteilung"].AsString();
    p.strasse       = row["strasse"].AsString();
    p.plz           = row["plz"].AsString();
    p.ort           = row["ort"].AsString();
    p.land          = row["land"].AsString();
    SteuerkategorieFromText(row["steuerkategorie"].AsString(), p.steuerkategorie);
    p.ustIdNr           = row["ust_idnr"].AsString();
    p.ustIdNrStatus     = row["ust_idnr_status"].AsString();
    p.ustIdNrGeprueftAm = DateFrom(row["ust_idnr_geprueft_am"]);
    p.ustIdNrProtokoll  = row["ust_idnr_protokoll"].AsString();
    p.steuernummer      = row["steuernummer"].AsString();
    p.email    = row["email"].AsString();
    p.telefon  = row["telefon"].AsString();
    p.webseite = row["webseite"].AsString();
    p.iban     = row["iban"].AsString();
    p.bic      = row["bic"].AsString();
    p.zahlungsfristTage = row["zahlungsfrist_tage"].AsInt();
    p.skontoPromille    = row["skonto_promille"].AsInt();
    p.skontoTage        = row["skonto_tage"].AsInt();
    p.sprache     = row["sprache"].AsString();
    p.notiz       = row["notiz"].AsString();
    p.aktiv       = row["aktiv"].AsInt() != 0;
    p.angelegtAm  = row["angelegt_am"].AsInt64();
    p.geaendertAm = row["geaendert_am"].AsInt64();
    p.version     = row["version"].AsInt64();
    return p;
}

const char* const kPartnerColumns =
    "id, mandant_id, typ, konto, name, name2, anrede, kontaktperson, abteilung, strasse,"
    " plz, ort, land, steuerkategorie, ust_idnr, ust_idnr_status, ust_idnr_geprueft_am,"
    " ust_idnr_protokoll, steuernummer, email, telefon, webseite, iban, bic,"
    " zahlungsfrist_tage, skonto_promille, skonto_tage, sprache, notiz, aktiv, angelegt_am,"
    " geaendert_am, version";

} // namespace

StoreResult Store::NextPersonenkonto(int64_t mandantId, PartnerTyp typ,
                                     int sachkontenlaenge, std::string& out) const {
    if (sachkontenlaenge < 4 || sachkontenlaenge > 8)
        return StoreResult::Fail("Die Sachkontenlänge muss zwischen 4 und 8 liegen.");

    // Person accounts are one digit wider than the G/L accounts, and the two
    // ranges are the customary DATEV ones scaled to that width: with 4-digit
    // G/L accounts, customers run from 10000 and suppliers from 70000. Derived
    // from the setting rather than hard-coded, so a company with 5- or 6-digit
    // accounts gets the right ranges too.
    const int width = sachkontenlaenge + 1;
    int64_t faktor = 1;
    for (int i = 0; i < width - 1; ++i) faktor *= 10;
    const int64_t von = (typ == PartnerTyp::Lieferant) ? 7 * faktor : 1 * faktor;
    const int64_t bis = (typ == PartnerTyp::Lieferant) ? 10 * faktor - 1 : 7 * faktor - 1;

    // The highest number already used in the range, so a deleted partner's
    // number is not handed out twice - person accounts appear in exported
    // bookings and must stay unique over the life of the books.
    UltraDbResultSet rs;
    int64_t hoechste = von - 1;
    if (Query("SELECT konto FROM partner WHERE mandant_id = ?", { mandantId }, rs)) {
        for (const UltraDbRow& row : rs) {
            const std::string konto = row["konto"].AsString();
            if (konto.empty()) continue;
            bool digits = true;
            for (char c : konto) if (c < '0' || c > '9') { digits = false; break; }
            if (!digits) continue;
            int64_t value = 0;
            for (char c : konto) value = value * 10 + (c - '0');
            if (value >= von && value <= bis && value > hoechste) hoechste = value;
        }
    }
    const int64_t naechste = hoechste + 1;
    if (naechste > bis)
        return StoreResult::Fail("Der Nummernbereich für " +
                                 std::string(typ == PartnerTyp::Lieferant ? "Kreditoren"
                                                                          : "Debitoren") +
                                 " ist erschöpft.");

    std::string digits;
    int64_t v = naechste;
    while (v > 0) { digits.insert(digits.begin(), static_cast<char>('0' + (v % 10))); v /= 10; }
    out = digits;
    return StoreResult::Ok();
}

StoreResult Store::SavePartner(Partner& partner, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Stammdaten ändern.");
    if (!partner.Valid()) return StoreResult::Fail("Der Name darf nicht leer sein.");

    // Check the VAT number offline and record what came out. This is not the
    // BZSt confirmation - that is a separate, stored protocol - but it catches
    // the typing errors before they reach an invoice.
    if (!partner.ustIdNr.empty()) {
        const UstIdNrPruefung pruefung = PruefeUstIdNr(partner.ustIdNr);
        partner.ustIdNr       = pruefung.normalisiert;
        partner.ustIdNrStatus = UstIdNrStatusToText(pruefung.status);
        if (!pruefung.Plausibel())
            return StoreResult::Fail("USt-IdNr.: " + pruefung.hinweis);
        // An EU VAT number and a domestic tax category contradict each other;
        // the category decides the taxation, so it is worth saying out loud
        // rather than booking 19 % onto an intra-community supply.
        if (!pruefung.land.empty() && pruefung.land != "DE" &&
            partner.steuerkategorie == Steuerkategorie::Inland)
            return StoreResult::Fail("Die USt-IdNr. gehört zu " + pruefung.land +
                                     ", die Steuerkategorie ist aber \"Inland\". "
                                     "Bitte die Kategorie anpassen.");
    }

    const int64_t now = NowSeconds();
    if (partner.id == 0) {
        if (partner.konto.empty()) {
            Geschaeftsjahr jahr;
            int laenge = 4;
            const std::vector<Geschaeftsjahr> jahre = Geschaeftsjahre(partner.mandantId);
            if (!jahre.empty()) laenge = jahre.back().sachkontenlaenge;
            const StoreResult konto = NextPersonenkonto(partner.mandantId, partner.typ,
                                                        laenge, partner.konto);
            if (!konto) return konto;
        }
        const StoreResult seq = NextSequenceValue("partner", partner.id);
        if (!seq) return seq;
        partner.angelegtAm  = now;
        partner.geaendertAm = now;
        partner.version     = 1;
        const StoreResult inserted = Exec(
            "INSERT INTO partner(id, mandant_id, typ, konto, name, name2, anrede,"
            " kontaktperson, abteilung, strasse, plz, ort, land, steuerkategorie, ust_idnr,"
            " ust_idnr_status, ust_idnr_geprueft_am, ust_idnr_protokoll, steuernummer, email,"
            " telefon, webseite, iban, bic, zahlungsfrist_tage, skonto_promille, skonto_tage,"
            " sprache, notiz, aktiv, angelegt_am, geaendert_am, version) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { partner.id, partner.mandantId, PartnerTypToText(partner.typ), partner.konto,
              partner.name, partner.name2, partner.anrede, partner.kontaktperson,
              partner.abteilung, partner.strasse, partner.plz, partner.ort, partner.land,
              SteuerkategorieToText(partner.steuerkategorie), partner.ustIdNr,
              partner.ustIdNrStatus, DateValue(partner.ustIdNrGeprueftAm),
              partner.ustIdNrProtokoll, partner.steuernummer, partner.email, partner.telefon,
              partner.webseite, partner.iban, partner.bic, partner.zahlungsfristTage,
              partner.skontoPromille, partner.skontoTage, partner.sprache, partner.notiz,
              partner.aktiv ? 1 : 0, partner.angelegtAm, partner.geaendertAm, partner.version },
            "Der Partner konnte nicht angelegt werden");
        if (!inserted) return inserted;
        return WriteAudit(akteur, "partner", partner.id, "anlegen",
                          partner.konto + " " + partner.name);
    }

    // Optimistic locking: the update only bites when the row still carries the
    // version we read. Zero rows changed means somebody else saved in the
    // meantime, and overwriting their work silently is the one outcome nobody
    // wants.
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    const UltraDbResult updated = UltraDb_Exec(connection_,
        "UPDATE partner SET typ = ?, konto = ?, name = ?, name2 = ?, anrede = ?,"
        " kontaktperson = ?, abteilung = ?, strasse = ?, plz = ?, ort = ?, land = ?,"
        " steuerkategorie = ?, ust_idnr = ?, ust_idnr_status = ?, ust_idnr_geprueft_am = ?,"
        " ust_idnr_protokoll = ?, steuernummer = ?, email = ?, telefon = ?, webseite = ?,"
        " iban = ?, bic = ?, zahlungsfrist_tage = ?, skonto_promille = ?, skonto_tage = ?,"
        " sprache = ?, notiz = ?, aktiv = ?, geaendert_am = ?, version = version + 1 "
        "WHERE id = ? AND version = ?",
        { PartnerTypToText(partner.typ), partner.konto, partner.name, partner.name2,
          partner.anrede, partner.kontaktperson, partner.abteilung, partner.strasse,
          partner.plz, partner.ort, partner.land,
          SteuerkategorieToText(partner.steuerkategorie), partner.ustIdNr,
          partner.ustIdNrStatus, DateValue(partner.ustIdNrGeprueftAm),
          partner.ustIdNrProtokoll, partner.steuernummer, partner.email, partner.telefon,
          partner.webseite, partner.iban, partner.bic, partner.zahlungsfristTage,
          partner.skontoPromille, partner.skontoTage, partner.sprache, partner.notiz,
          partner.aktiv ? 1 : 0, now, partner.id, partner.version });
    if (!updated)
        return StoreResult::Fail("Der Partner konnte nicht geändert werden: " +
                                 updated.message);
    if (updated.affectedRows == 0) {
        Partner aktuell;
        const bool exists = PartnerById(partner.id, aktuell);
        if (!exists) return StoreResult::Fail("Der Partner existiert nicht mehr.");
        return StoreResult::Fail("Dieser Partner wurde zwischenzeitlich von jemand anderem "
                                 "geändert (Version " + std::to_string(aktuell.version) +
                                 "). Bitte neu laden und die Änderung wiederholen.");
    }
    partner.geaendertAm = now;
    ++partner.version;
    return WriteAudit(akteur, "partner", partner.id, "aendern",
                      partner.konto + " " + partner.name);
}

StoreResult Store::SpeichereUstIdNrBestaetigung(int64_t partnerId,
                                                const Bestaetigung& bestaetigung,
                                                const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Bestätigungen speichern.");
    if (!bestaetigung.ok)
        return StoreResult::Fail("Eine fehlgeschlagene Abfrage wird nicht als Bestätigung "
                                 "gespeichert: " + bestaetigung.fehler);

    Partner partner;
    if (!PartnerById(partnerId, partner))
        return StoreResult::Fail("Der Partner wurde nicht gefunden.");

    // The status records *what kind* of confirmation this was, because a VIES
    // "valid" and a BZSt qualified confirmation protect entirely different
    // things.
    std::string status = BestaetigungsQuelleToText(bestaetigung.quelle);
    status += bestaetigung.gueltig ? ":gueltig" : ":ungueltig";
    if (bestaetigung.IstQualifiziert()) status += ":qualifiziert";

    const StoreResult updated = Exec(
        "UPDATE partner SET ust_idnr_status = ?, ust_idnr_geprueft_am = ?,"
        " ust_idnr_protokoll = ?, geaendert_am = ?, version = version + 1 WHERE id = ?",
        { status, DateValue(bestaetigung.anfrageDatum), bestaetigung.protokoll,
          NowSeconds(), partnerId },
        "Die Bestätigung konnte nicht gespeichert werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "partner", partnerId, "ustidnr-bestaetigung",
                      status + " " + bestaetigung.Zusammenfassung());
}

bool Store::PartnerById(int64_t id, Partner& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kPartnerColumns + " FROM partner WHERE id = ?",
                  { id }, row))
        return false;
    out = PartnerFromRow(row);
    return true;
}

bool Store::PartnerByKonto(int64_t mandantId, const std::string& konto, Partner& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kPartnerColumns +
                  " FROM partner WHERE mandant_id = ? AND konto = ?", { mandantId, konto }, row))
        return false;
    out = PartnerFromRow(row);
    return true;
}

std::vector<Partner> Store::PartnerListe(int64_t mandantId, PartnerTyp typ,
                                         const std::string& suche) const {
    std::vector<Partner> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kPartnerColumns +
               " FROM partner WHERE mandant_id = ? ORDER BY name", { mandantId }, rs))
        return liste;

    // Filtering happens here rather than in SQL because case-insensitive
    // matching over several columns is where SQLite and PostgreSQL disagree
    // most (LIKE, ILIKE, collations), and a master-data list is never large
    // enough for that to matter. The journal, when it comes, will need indexed
    // SQL filtering and its own portable predicate.
    const std::string needle = ToLower(suche);
    for (const UltraDbRow& row : rs) {
        const Partner partner = PartnerFromRow(row);
        const bool typOk = (typ == PartnerTyp::Beides) ||
                           (typ == PartnerTyp::Kunde     && partner.IstKunde()) ||
                           (typ == PartnerTyp::Lieferant && partner.IstLieferant());
        if (!typOk) continue;
        if (!needle.empty()) {
            const std::string haystack = ToLower(partner.name + " " + partner.name2 + " " +
                                                 partner.ort + " " + partner.ustIdNr + " " +
                                                 partner.konto + " " + partner.kontaktperson);
            if (haystack.find(needle) == std::string::npos) continue;
        }
        liste.push_back(partner);
    }
    return liste;
}

// ===== BENUTZER =====

namespace {

Benutzer BenutzerFromRow(const UltraDbRow& row) {
    Benutzer user;
    user.id           = row["id"].AsInt64();
    user.anmeldename  = row["anmeldename"].AsString();
    user.anzeigename  = row["anzeigename"].AsString();
    user.email        = row["email"].AsString();
    BenutzerRolleFromText(row["rolle"].AsString(), user.rolle);
    user.aktiv        = row["aktiv"].AsInt() != 0;
    user.angelegtAm   = row["angelegt_am"].AsInt64();
    user.letzterLogin = row["letzter_login"].AsInt64();
    return user;
}

const char* const kBenutzerColumns =
    "id, anmeldename, anzeigename, email, rolle, aktiv, angelegt_am, letzter_login";

// Argon2id through UltraCrypt. The parameters are stored per user so they can
// be raised later without invalidating existing passwords.
bool DeriveePasswort(const std::string& passwort, UltraCryptKdfParams& params,
                     std::string& outHex) {
    static bool initialised = false;
    if (!initialised) { UltraCrypt_Initialize(); initialised = true; }

    const UltraCryptSecureBuffer secret(passwort.data(), passwort.size());
    UltraCryptSecureBuffer key;
    const UltraCryptResult result = UltraCrypt_DeriveKeyFromPassword(secret, params, key);
    if (!result) return false;
    outHex = UltraCrypt_ToHex(std::vector<uint8_t>(key.Data(), key.Data() + key.GetSize()));
    return true;
}

} // namespace

StoreResult Store::SaveBenutzer(Benutzer& benutzer, const Akteur& akteur) {
    // The first user is created before anybody can be authorised to do it -
    // otherwise a fresh installation could never be set up.
    const bool erstbenutzer = !HatBenutzer();
    if (!erstbenutzer && !akteur.Darf(Recht::BenutzerVerwalten))
        return StoreResult::Fail("Diese Rolle darf keine Benutzer verwalten.");
    if (!benutzer.Valid()) return StoreResult::Fail("Der Benutzer braucht einen Anmeldenamen.");
    if (erstbenutzer && benutzer.rolle != BenutzerRolle::Administrator)
        benutzer.rolle = BenutzerRolle::Administrator;

    if (benutzer.id == 0) {
        Benutzer vorhanden;
        if (BenutzerByName(benutzer.anmeldename, vorhanden))
            return StoreResult::Fail("Der Anmeldename \"" + benutzer.anmeldename +
                                     "\" ist bereits vergeben.");
        const StoreResult seq = NextSequenceValue("benutzer", benutzer.id);
        if (!seq) return seq;
        benutzer.angelegtAm = NowSeconds();
        const StoreResult inserted = Exec(
            "INSERT INTO benutzer(id, anmeldename, anzeigename, email, rolle, aktiv,"
            " angelegt_am, letzter_login) VALUES(?,?,?,?,?,?,?,?)",
            { benutzer.id, benutzer.anmeldename, benutzer.anzeigename, benutzer.email,
              BenutzerRolleToText(benutzer.rolle), benutzer.aktiv ? 1 : 0,
              benutzer.angelegtAm, benutzer.letzterLogin },
            "Der Benutzer konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE benutzer SET anzeigename = ?, email = ?, rolle = ?, aktiv = ? WHERE id = ?",
            { benutzer.anzeigename, benutzer.email, BenutzerRolleToText(benutzer.rolle),
              benutzer.aktiv ? 1 : 0, benutzer.id },
            "Der Benutzer konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "benutzer", benutzer.id, "speichern",
                      benutzer.anmeldename + " (" + BenutzerRolleToText(benutzer.rolle) + ")");
}

StoreResult Store::SetPasswort(int64_t benutzerId, const std::string& passwort,
                               const Akteur& akteur) {
    // A user may always set their own password; changing somebody else's needs
    // the user-management right.
    if (akteur.benutzerId != benutzerId && !akteur.Darf(Recht::BenutzerVerwalten))
        return StoreResult::Fail("Diese Rolle darf fremde Passwörter nicht ändern.");
    if (passwort.size() < 8)
        return StoreResult::Fail("Das Passwort muss mindestens 8 Zeichen lang sein.");
    if (!UltraCrypt_IsAvailable())
        return StoreResult::Fail("Die Kryptobibliothek ist nicht verfügbar, es kann kein "
                                 "Passwort gesetzt werden.");

    UltraCryptKdfParams params = UltraCrypt_RecommendedKdfParams();
    std::string hex;
    if (!DeriveePasswort(passwort, params, hex))
        return StoreResult::Fail("Das Passwort konnte nicht verschlüsselt werden.");

    const StoreResult updated = Exec(
        "UPDATE benutzer SET passwort_hash = ?, passwort_salt = ?, kdf_algorithmus = ?,"
        " kdf_iterationen = ?, kdf_speicher_kib = ?, kdf_laenge = ? WHERE id = ?",
        { hex, UltraCrypt_ToHex(params.salt), std::string("argon2id"),
          static_cast<int64_t>(params.iterations), static_cast<int64_t>(params.memoryKiB),
          static_cast<int64_t>(params.outputLength), benutzerId },
        "Das Passwort konnte nicht gespeichert werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "benutzer", benutzerId, "passwort", std::string());
}

bool Store::Anmelden(const std::string& anmeldename, const std::string& passwort,
                     Benutzer& out) {
    UltraDbRow row;
    if (!QueryOne("SELECT id, anmeldename, anzeigename, email, rolle, aktiv, angelegt_am,"
                  " letzter_login, passwort_hash, passwort_salt, kdf_iterationen,"
                  " kdf_speicher_kib, kdf_laenge FROM benutzer WHERE anmeldename = ?",
                  { anmeldename }, row))
        return false;                      // unknown user and wrong password look alike
    if (row["aktiv"].AsInt() == 0) return false;

    const std::string stored = row["passwort_hash"].AsString();
    if (stored.empty()) return false;

    UltraCryptKdfParams params = UltraCrypt_RecommendedKdfParams();
    std::vector<uint8_t> salt;
    if (!UltraCrypt_FromHex(row["passwort_salt"].AsString(), salt)) return false;
    params.salt         = salt;
    params.iterations   = static_cast<uint32_t>(row["kdf_iterationen"].AsInt64());
    params.memoryKiB    = static_cast<uint32_t>(row["kdf_speicher_kib"].AsInt64());
    params.outputLength = static_cast<size_t>(row["kdf_laenge"].AsInt64());
    if (params.outputLength == 0) params.outputLength = 32;

    std::string hex;
    if (!DeriveePasswort(passwort, params, hex)) return false;
    // Constant-time comparison: the hex strings are the same length whenever
    // the parameters are, and a timing signal on a password check is free to
    // avoid.
    if (hex.size() != stored.size()) return false;
    if (!UltraCrypt_ConstantTimeEquals(hex.data(), hex.size(),
                                      stored.data(), stored.size())) return false;

    out = BenutzerFromRow(row);
    out.letzterLogin = NowSeconds();
    Exec("UPDATE benutzer SET letzter_login = ? WHERE id = ?",
         { out.letzterLogin, out.id }, "Anmeldezeit");
    return true;
}

bool Store::BenutzerById(int64_t id, Benutzer& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBenutzerColumns + " FROM benutzer WHERE id = ?",
                  { id }, row))
        return false;
    out = BenutzerFromRow(row);
    return true;
}

bool Store::BenutzerByName(const std::string& anmeldename, Benutzer& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBenutzerColumns +
                  " FROM benutzer WHERE anmeldename = ?", { anmeldename }, row))
        return false;
    out = BenutzerFromRow(row);
    return true;
}

std::vector<Benutzer> Store::BenutzerListe() const {
    std::vector<Benutzer> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kBenutzerColumns + " FROM benutzer ORDER BY anmeldename",
               {}, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BenutzerFromRow(row));
    return liste;
}

bool Store::HatBenutzer() const {
    UltraDbRow row;
    if (!QueryOne("SELECT COUNT(*) AS anzahl FROM benutzer", {}, row)) return false;
    return row["anzahl"].AsInt64() > 0;
}

} // namespace UltraFIBU

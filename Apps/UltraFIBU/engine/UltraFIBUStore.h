// Apps/UltraFIBU/engine/UltraFIBUStore.h
// The UltraFIBU database: schema, migrations and every query the application
// runs. Nothing above this file writes SQL, and nothing in this file draws.
//
// **Written for two storage modes from the first commit** (proposal §10): the
// same schema and the same API sit behind one named UltraDatabase connection
// whose driver is either "sqlite" (a local single-user file) or "postgresql" (a
// shared server several users work on). Keeping that true costs four
// disciplines, all of them observed here:
//
//   1. **No SQLite-specific SQL.** No AUTOINCREMENT, no INSERT OR REPLACE, no
//      strftime/julianday. Surrogate keys come from the `sequenz` table, dates
//      are ISO-8601 text, money is BIGINT minor units, booleans are 0/1, and
//      every date computation happens in C++ where the Geschaeftsjahr logic
//      already lives.
//   2. **Every value is bound**, never concatenated into SQL.
//   3. **Every write is a transaction**, so a half-written document cannot
//      exist.
//   4. **No assumption that this process is the only writer**: document numbers
//      are allocated inside the transaction that uses them, editable rows carry
//      a `version` for optimistic locking, and the schema version is checked
//      before anything is written.
//
// The PostgreSQL driver is UltraDatabase Stage 2 and does not exist yet;
// OpenServer() therefore reports what is missing rather than pretending. The
// application is portable in the meantime, which is the whole point of writing
// it this way now.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"
#include "UltraFIBUUstIdNrOnline.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraDatabase/UltraDatabaseValue.h>

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// Every mutating call reports either success or one German sentence fit to put
// in front of the user. The diagnostic from the driver is appended when there
// is one, because "konnte nicht gespeichert werden" alone has never helped
// anybody.
struct StoreResult {
    bool        ok = true;
    std::string fehler;

    explicit operator bool() const { return ok; }
    static StoreResult Ok() { return StoreResult(); }
    static StoreResult Fail(const std::string& fehler) {
        StoreResult r;
        r.ok = false;
        r.fehler = fehler;
        return r;
    }
};

// Who is acting. Passed to every mutating call rather than kept as store state,
// so a batch job and a UI session cannot get confused about it, and so no row
// can be written without an author (GoBD attribution - see the proposal §8.4).
struct Akteur {
    int64_t       benutzerId = 0;
    std::string   anmeldename;
    BenutzerRolle rolle = BenutzerRolle::Administrator;

    bool Darf(Recht recht) const { return RolleHatRecht(rolle, recht); }
};

class Store {
public:
    // The schema version Open() migrates to. Bumped with every migration step
    // added in the .cpp, so a test can assert that the database matches the
    // code without a literal that has to be chased.
    static constexpr int kSchemaVersion = 1;

    Store() = default;
    ~Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // ---- Opening -----------------------------------------------------------

    // Local, single-user: one SQLite file. ":memory:" works and is what the
    // tests use.
    StoreResult Open(const std::string& connectionName, const std::string& databasePath);

    // Shared server. `credentialsRef` is an UltraVault key ("vault:fibu-rw"),
    // never a literal password. Returns a clear refusal while the PostgreSQL
    // driver is not built, listing what is missing - the application is
    // portable, the driver is not there yet.
    StoreResult OpenServer(const std::string& connectionName,
                           const std::string& host, int port,
                           const std::string& database,
                           const std::string& user,
                           const std::string& credentialsRef);

    void Close();
    bool IsOpen() const { return !connection_.empty(); }
    const std::string& ConnectionName() const { return connection_; }

    // The schema version in the database (0 when nothing has run).
    int  SchemaVersion() const;

    // In server mode an old binary must never write into a newer schema. Open()
    // refuses that case; this reports it for a UI that wants to explain why.
    bool SchemaIsNewerThanCode() const { return SchemaVersion() > kSchemaVersion; }

    // ---- Portable id and number allocation ---------------------------------

    // The next value of a named sequence, allocated atomically. This is how
    // surrogate keys are made: AUTOINCREMENT is SQLite's, SERIAL is
    // PostgreSQL's, and one mechanism that works on both is worth more than
    // either.
    StoreResult NextSequenceValue(const std::string& name, int64_t& out);

    // The next document number of a Nummernkreis, rendered through
    // FormatNummer, allocated inside one transaction so two writers can never
    // receive the same number. Resets the counter when the kreis is yearly and
    // the year has turned.
    StoreResult NextBelegnummer(int64_t mandantId, const std::string& kreis,
                                const Date& datum, std::string& out);

    StoreResult SaveNummernkreis(Nummernkreis& kreis, const Akteur& akteur);
    std::vector<Nummernkreis> Nummernkreise(int64_t mandantId) const;

    // ---- Mandant -----------------------------------------------------------

    StoreResult SaveMandant(Mandant& mandant, const Akteur& akteur);
    bool        LoadMandant(int64_t id, Mandant& out) const;
    std::vector<Mandant> Mandanten() const;

    // ---- Geschaeftsjahr ----------------------------------------------------

    // Refuses a year that is malformed, that overlaps an existing one, or that
    // leaves a gap after the latest one - a posting that belongs to two fiscal
    // years, or to none, has no correct report.
    StoreResult SaveGeschaeftsjahr(Geschaeftsjahr& jahr, const Akteur& akteur);
    std::vector<Geschaeftsjahr> Geschaeftsjahre(int64_t mandantId) const;
    bool GeschaeftsjahrById(int64_t id, Geschaeftsjahr& out) const;
    // The fiscal year a date falls in, which is the only correct way to find it.
    bool GeschaeftsjahrAt(int64_t mandantId, const Date& datum, Geschaeftsjahr& out) const;

    // Freeze everything up to and including `bis`. Only ever moves forward:
    // un-freezing is not a feature, it is a GoBD violation.
    StoreResult Festschreiben(int64_t geschaeftsjahrId, const Date& bis, const Akteur& akteur);

    // ---- Kontenrahmen ------------------------------------------------------

    StoreResult SaveKonto(Konto& konto, const Akteur& akteur);
    // Bulk import of a chart of accounts; existing numbers are updated rather
    // than duplicated. Reports how many rows were written.
    StoreResult ImportKonten(int64_t mandantId, const std::vector<Konto>& konten,
                             const Akteur& akteur, int& outWritten);
    std::vector<Konto> Konten(int64_t mandantId) const;
    bool KontoByNummer(int64_t mandantId, const std::string& nummer, Konto& out) const;

    // ---- Steuerschluessel --------------------------------------------------

    StoreResult SaveSteuerschluessel(Steuerschluessel& schluessel, const Akteur& akteur);
    StoreResult ImportSteuerschluessel(int64_t mandantId,
                                       const std::vector<Steuerschluessel>& schluessel,
                                       const Akteur& akteur, int& outWritten);
    // All keys, or only those valid on a given date - which is how a prior
    // year stays reportable after a rate change.
    std::vector<Steuerschluessel> SteuerschluesselListe(int64_t mandantId,
                                                        const Date& gueltigAm = Date()) const;
    bool SteuerschluesselByKey(int64_t mandantId, const std::string& schluessel,
                               const Date& gueltigAm, Steuerschluessel& out) const;

    // ---- Partner (Kunden / Lieferanten) ------------------------------------

    // Allocates a Personenkonto when the partner has none, validates the
    // USt-IdNr. offline, and enforces optimistic locking: saving a row someone
    // else has changed in the meantime is refused, not overwritten.
    StoreResult SavePartner(Partner& partner, const Akteur& akteur);
    bool PartnerById(int64_t id, Partner& out) const;
    bool PartnerByKonto(int64_t mandantId, const std::string& konto, Partner& out) const;
    // `suche` matches name, Ort, USt-IdNr. or Konto, case-insensitively; empty
    // matches everything. `typ` filters, with Beides matching both sides.
    std::vector<Partner> PartnerListe(int64_t mandantId, PartnerTyp typ,
                                      const std::string& suche = std::string()) const;

    // Record an authority's confirmation of a partner's VAT number: the
    // status, the date and - above all - the response verbatim, because that
    // data set is the evidence § 18e UStG asks for. Writes only those three
    // columns, so it cannot fail on an optimistic-locking conflict with
    // somebody editing the address at the same time; an enquiry answered by an
    // authority must never be lost to a version clash.
    StoreResult SpeichereUstIdNrBestaetigung(int64_t partnerId, const Bestaetigung& bestaetigung,
                                             const Akteur& akteur);

    // The next free person account in the range the Sachkontenlaenge implies:
    // with 4-digit G/L accounts, customers start at 10000 and suppliers at
    // 70000. Never hard-coded for one width - the range follows the setting.
    StoreResult NextPersonenkonto(int64_t mandantId, PartnerTyp typ,
                                  int sachkontenlaenge, std::string& out) const;

    // ---- Benutzer ----------------------------------------------------------

    StoreResult SaveBenutzer(Benutzer& benutzer, const Akteur& akteur);
    StoreResult SetPasswort(int64_t benutzerId, const std::string& passwort,
                            const Akteur& akteur);
    // Verifies a password against the stored Argon2id parameters. A wrong
    // password and an unknown user are the same answer, deliberately.
    bool Anmelden(const std::string& anmeldename, const std::string& passwort,
                  Benutzer& out);
    bool BenutzerById(int64_t id, Benutzer& out) const;
    bool BenutzerByName(const std::string& anmeldename, Benutzer& out) const;
    std::vector<Benutzer> BenutzerListe() const;
    // True when no user exists yet - the point at which the first
    // administrator may be created without being authorised by anybody.
    bool HatBenutzer() const;

    // ---- Audit -------------------------------------------------------------

    struct AuditEintrag {
        int64_t     id = 0;
        int64_t     zeit = 0;          // epoch seconds
        int64_t     benutzerId = 0;
        std::string benutzer;
        std::string tabelle;
        int64_t     rowId = 0;
        std::string aktion;            // "insert" | "update" | "festschreiben" | "abgelehnt"
        std::string details;
    };

    StoreResult WriteAudit(const Akteur& akteur, const std::string& tabelle,
                           int64_t rowId, const std::string& aktion,
                           const std::string& details);
    std::vector<AuditEintrag> AuditListe(size_t limit = 200) const;

private:
    // Small wrappers so the call sites stay readable and every failure carries
    // the driver's message.
    StoreResult Exec(const std::string& sql, const UltraDbParams& params,
                     const std::string& wobei) const;
    bool        QueryOne(const std::string& sql, const UltraDbParams& params,
                         UltraDbRow& out) const;
    bool        Query(const std::string& sql, const UltraDbParams& params,
                      UltraDbResultSet& out) const;

    std::string connection_;
};

} // namespace UltraFIBU

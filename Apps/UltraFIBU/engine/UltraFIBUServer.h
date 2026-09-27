// Apps/UltraFIBU/engine/UltraFIBUServer.h
// Everything the multi-user mode needs around Store::OpenServer: which server,
// where its password lives, and who is signed in.
//
// **One way to name a server**, wherever a file path would otherwise go:
//
//     postgresql://fibu@db.kanzlei.local:5432/buchhaltung
//
// The CLI takes it in place of the file, the window takes it as its one
// argument and fills its connection form from it, and the settings file keeps
// the last one. It never carries a password: a `postgresql://user:secret@...`
// is refused rather than read, because a password typed into a command line
// ends up in the shell history and in every process listing.
//
// **The database password lives in UltraFIBU's own vault** - an
// UltraVault::DeviceKeyVault under the configuration directory, the same kind
// UltraMail and UltraFiler keep their account secrets in. It is asked for once
// and stored; after that the PostgreSQL driver reads it from the vault by key
// (`vault:<key>`) and it never passes through this program again.
//
// **Two passwords, two different things.** The database password lets this
// computer talk to the server at all; it belongs to the installation and is
// stored. The user's password says which person is working, for the audit
// trail and the role checks (GoBD attribution); it is typed at every start
// and never stored. A shared database with one stored password and no
// personal login would record every booking as the same anonymous somebody.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUStore.h"

#include <string>

namespace UltraFIBU {

// ===== WHICH SERVER =====

struct ServerZiel {
    std::string host;
    int         port = 5432;
    std::string datenbank;
    std::string benutzer;        // the PostgreSQL role this computer connects as

    // Host, database and user are all there. Says nothing about whether the
    // server exists.
    bool Vollstaendig() const {
        return !host.empty() && !datenbank.empty() && !benutzer.empty();
    }

    // "postgresql://fibu@db.local:5432/buchhaltung". The port is written only
    // when it is not the default, so the text a user copies is the short one.
    std::string ToUrl() const;

    // The name the database password is stored under in the vault, below the
    // vault's own prefix: "server.fibu@db.local:5432/buchhaltung". One entry
    // per server, role and database, so two installations on one computer do
    // not overwrite each other's password.
    std::string TresorKonto() const;
};

// True when `text` is a server address rather than a file path.
bool IstServerUrl(const std::string& text);

// Parse "postgresql://user@host[:port]/db" (also "postgres://"). IPv6 hosts go
// in brackets: "postgresql://fibu@[::1]:5432/buch". Returns false with a German
// sentence in `fehler` for anything else - including a password in the URL.
bool ParseServerUrl(const std::string& url, ServerZiel& out, std::string& fehler);

// ===== WHERE THINGS ARE KEPT =====

// The configuration directory: $ULTRAFIBU_CONFIG_DIR if set (tests, portable
// installs), else $XDG_CONFIG_HOME/UltraFIBU, ~/.config/UltraFIBU, or
// %APPDATA%\UltraFIBU on Windows. Empty when none of them can be found.
std::string KonfigurationsVerzeichnis();

// The last server this computer connected to, so the form is filled in next
// time. `server.ini` in the configuration directory; plain `key = value` text
// with nothing secret in it. False when there is none yet.
bool LadeLetztenServer(ServerZiel& out);
bool SpeichereLetztenServer(const ServerZiel& ziel, std::string& fehler);

// ===== THE VAULT =====

// UltraFIBU's credential vault. Opened without a prompt (device key), as the
// other ULTRA OS apps do; see UltraVaultDeviceKeyVault.h for what that
// protects against and what it does not.
class ServerTresor {
public:
    // Open (or create) the vault under `verzeichnis`, or under
    // KonfigurationsVerzeichnis()/vault when empty.
    bool Oeffnen(std::string& fehler, const std::string& verzeichnis = std::string());
    bool IstOffen() const;

    // Store the database password for `ziel`. An empty password is refused:
    // it is never what the user meant, and it would shadow the driver's own
    // fallbacks (.pgpass, peer) with a guaranteed failure.
    bool SpeicherePasswort(const ServerZiel& ziel, const std::string& passwort,
                           std::string& fehler);
    bool HatPasswort(const ServerZiel& ziel) const;
    bool VergissPasswort(const ServerZiel& ziel);

    // "vault:fibu.ultrafibu.server.fibu@db.local:5432/buch" - what
    // Store::OpenServer and the PostgreSQL driver take.
    static std::string CredentialsRef(const ServerZiel& ziel);

private:
    bool offen_ = false;
    std::string verzeichnis_;
};

// Open `ziel` in `store`, with the password from `tresor` (which must be
// open and hold one - see SpeicherePasswort). On success the address is also
// remembered for the next start. `fehler` is the store's German message.
StoreResult VerbindeMitServer(Store& store, const ServerZiel& ziel,
                              const ServerTresor& tresor);

// ===== WHO IS WORKING =====

// Sign `anmeldename` in with `passwort` and fill `akteur` with the user, the
// role and the name the audit trail records. Wrong name and wrong password are
// one answer, as in Store::Anmelden. A user without a password cannot sign in
// at all; an administrator sets one first (`ultrafibu passwort`).
//
// Server mode always asks. A local file still opens as its first
// administrator, as it did before there was a server mode: a single-user file
// on one computer has nobody else to tell apart.
//
// With a second factor enrolled (Store::ZweitenFaktorAktivieren) the code from
// the authenticator app is needed too. Without one, `codeNoetig` is set and
// the call fails with a message that asks for it - the caller asks and calls
// again with the code.
StoreResult Anmelden(Store& store, const std::string& anmeldename,
                     const std::string& passwort, const std::string& code,
                     Akteur& akteur, bool* codeNoetig = nullptr);

} // namespace UltraFIBU

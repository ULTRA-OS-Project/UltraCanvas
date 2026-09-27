// Apps/UltraFIBU/engine/UltraFIBUServer.cpp
// The multi-user mode's address, vault and login. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUServer.h"

#include <UltraVault/UltraVaultDeviceKeyVault.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace UltraFIBU {

namespace {

// The vault profile. The prefix follows UltraVault's "<vendor>.<app>."
// convention; the file name keeps it apart from the other apps' vaults if a
// directory is ever shared.
const UltraVault::DeviceKeyVaultProfile kTresorProfil{ "ultrafibu.vault", "fibu.ultrafibu." };

// UltraVault is one store per process; the DeviceKeyVault that opened it is
// kept here so its lifetime matches the process, not a dialog's.
std::unique_ptr<UltraVault::DeviceKeyVault>& Tresor() {
    static auto* tresor = new std::unique_ptr<UltraVault::DeviceKeyVault>();
    return *tresor;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                     s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool NurZiffern(const std::string& s) {
    if (s.empty()) return false;
    for (const char c : s) if (c < '0' || c > '9') return false;
    return true;
}

// A host, user or database name may not carry anything that would change the
// meaning of the URL or of the vault key: no whitespace, no '/', '@', '?',
// '#', and no control characters.
bool SauberesTeil(const std::string& s, const char* verboten) {
    if (s.empty()) return false;
    for (const unsigned char c : s) {
        if (c <= ' ' || c == 0x7f) return false;
        for (const char* v = verboten; *v; ++v) if (c == static_cast<unsigned char>(*v)) return false;
    }
    return true;
}

std::string ServerIniPfad() {
    const std::string dir = KonfigurationsVerzeichnis();
    return dir.empty() ? std::string() : dir + "/server.ini";
}

} // namespace

// ===== WHICH SERVER =====

std::string ServerZiel::ToUrl() const {
    const bool v6 = host.find(':') != std::string::npos;
    std::string url = "postgresql://" + benutzer + "@" + (v6 ? "[" + host + "]" : host);
    if (port != 5432) url += ":" + std::to_string(port);
    return url + "/" + datenbank;
}

std::string ServerZiel::TresorKonto() const {
    // Always with the port, so "db" and "db:5432" are the same entry.
    const bool v6 = host.find(':') != std::string::npos;
    return "server." + benutzer + "@" + (v6 ? "[" + host + "]" : host) + ":" +
           std::to_string(port) + "/" + datenbank;
}

bool IstServerUrl(const std::string& text) {
    return text.rfind("postgresql://", 0) == 0 || text.rfind("postgres://", 0) == 0;
}

bool ParseServerUrl(const std::string& url, ServerZiel& out, std::string& fehler) {
    out = ServerZiel();
    std::string rest;
    if (url.rfind("postgresql://", 0) == 0)      rest = url.substr(13);
    else if (url.rfind("postgres://", 0) == 0)   rest = url.substr(11);
    else {
        fehler = "\"" + url + "\" ist keine Serveradresse (postgresql://benutzer@host/datenbank).";
        return false;
    }

    const size_t at = rest.find('@');
    if (at == std::string::npos) {
        fehler = "In der Serveradresse fehlt der Datenbankbenutzer "
                 "(postgresql://benutzer@host/datenbank).";
        return false;
    }
    const std::string wer = rest.substr(0, at);
    if (wer.find(':') != std::string::npos) {
        fehler = "Die Serveradresse enthält ein Passwort. Passwörter gehören nicht in "
                 "eine Adresse - sie landen in der Shell-Historie. UltraFIBU fragt "
                 "einmal danach und legt es im Tresor ab.";
        return false;
    }
    rest = rest.substr(at + 1);

    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        fehler = "In der Serveradresse fehlt der Name der Datenbank "
                 "(postgresql://benutzer@host/datenbank).";
        return false;
    }
    std::string hostPort = rest.substr(0, slash);
    const std::string db = rest.substr(slash + 1);

    std::string host, portText;
    if (!hostPort.empty() && hostPort[0] == '[') {
        const size_t zu = hostPort.find(']');
        if (zu == std::string::npos) {
            fehler = "Die IPv6-Adresse in der Serveradresse ist nicht geschlossen (']' fehlt).";
            return false;
        }
        host = hostPort.substr(1, zu - 1);
        const std::string danach = hostPort.substr(zu + 1);
        if (!danach.empty()) {
            if (danach[0] != ':') { fehler = "Nach der IPv6-Adresse wird \":port\" erwartet."; return false; }
            portText = danach.substr(1);
        }
    } else {
        const size_t doppel = hostPort.find(':');
        host = hostPort.substr(0, doppel);
        if (doppel != std::string::npos) portText = hostPort.substr(doppel + 1);
    }

    if (!SauberesTeil(wer, "/@?#:[]")) { fehler = "Der Datenbankbenutzer in der Serveradresse ist leer oder enthält unzulässige Zeichen."; return false; }
    if (!SauberesTeil(host, "/@?#[]")) { fehler = "Der Host in der Serveradresse ist leer oder enthält unzulässige Zeichen."; return false; }
    if (!SauberesTeil(db, "/@?#:[]"))  { fehler = "Der Datenbankname in der Serveradresse ist leer oder enthält unzulässige Zeichen."; return false; }

    int port = 5432;
    if (!portText.empty()) {
        if (!NurZiffern(portText) || portText.size() > 5) { fehler = "Der Port \"" + portText + "\" ist keine Zahl."; return false; }
        port = std::atoi(portText.c_str());
        if (port < 1 || port > 65535) { fehler = "Der Port " + portText + " liegt nicht zwischen 1 und 65535."; return false; }
    }

    out.host      = host;
    out.port      = port;
    out.datenbank = db;
    out.benutzer  = wer;
    return true;
}

// ===== WHERE THINGS ARE KEPT =====

std::string KonfigurationsVerzeichnis() {
    if (const char* eigen = std::getenv("ULTRAFIBU_CONFIG_DIR"); eigen && *eigen)
        return eigen;
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "\\UltraFIBU";
    return std::string();
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg) + "/UltraFIBU";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config/UltraFIBU";
    return std::string();
#endif
}

bool LadeLetztenServer(ServerZiel& out) {
    const std::string pfad = ServerIniPfad();
    if (pfad.empty()) return false;
    std::ifstream in(pfad);
    if (!in) return false;
    ServerZiel ziel;
    std::string zeile;
    while (std::getline(in, zeile)) {
        const std::string t = Trim(zeile);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t gleich = t.find('=');
        if (gleich == std::string::npos) continue;
        const std::string key = Trim(t.substr(0, gleich));
        const std::string val = Trim(t.substr(gleich + 1));
        if (key == "url") {
            // The one field that matters; the address is re-validated exactly
            // as if it had been typed, so a hand-edited file cannot smuggle in
            // what the form would refuse.
            std::string fehler;
            if (!ParseServerUrl(val, ziel, fehler)) return false;
        }
    }
    if (!ziel.Vollstaendig()) return false;
    out = ziel;
    return true;
}

bool SpeichereLetztenServer(const ServerZiel& ziel, std::string& fehler) {
    const std::string pfad = ServerIniPfad();
    if (pfad.empty()) { fehler = "Es gibt kein Konfigurationsverzeichnis."; return false; }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(pfad).parent_path(), ec);
    const std::string temp = pfad + ".neu";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) { fehler = "\"" + temp + "\" kann nicht geschrieben werden."; return false; }
        out << "# UltraFIBU - zuletzt verwendeter Server. Kein Passwort: das liegt im Tresor.\n";
        out << "url = " << ziel.ToUrl() << "\n";
        if (!out) { fehler = "\"" + temp + "\" konnte nicht vollständig geschrieben werden."; return false; }
    }
    std::filesystem::rename(temp, pfad, ec);
    if (ec) { fehler = "\"" + pfad + "\" konnte nicht ersetzt werden: " + ec.message(); return false; }
    return true;
}

// ===== THE VAULT =====

bool ServerTresor::Oeffnen(std::string& fehler, const std::string& verzeichnis) {
    std::string dir = verzeichnis;
    if (dir.empty()) {
        const std::string konfig = KonfigurationsVerzeichnis();
        if (konfig.empty()) {
            fehler = "Es gibt kein Konfigurationsverzeichnis, in dem der Tresor liegen könnte.";
            return false;
        }
        dir = konfig + "/vault";
    }
    auto& tresor = Tresor();
    if (tresor && offen_ && verzeichnis_ == dir && tresor->IsUnlocked()) return true;

    tresor = std::make_unique<UltraVault::DeviceKeyVault>(dir, kTresorProfil);
    if (!tresor->TryAutoUnlock()) {
        fehler = "Der Tresor in \"" + dir + "\" lässt sich nicht öffnen.";
        tresor.reset();
        offen_ = false;
        return false;
    }
    verzeichnis_ = dir;
    offen_ = true;
    return true;
}

bool ServerTresor::IstOffen() const {
    return offen_ && Tresor() && Tresor()->IsUnlocked();
}

bool ServerTresor::SpeicherePasswort(const ServerZiel& ziel, const std::string& passwort,
                                     std::string& fehler) {
    if (!IstOffen()) { fehler = "Der Tresor ist nicht geöffnet."; return false; }
    if (!ziel.Vollstaendig()) { fehler = "Host, Datenbank und Datenbankbenutzer werden gebraucht."; return false; }
    if (passwort.empty()) { fehler = "Das Datenbankpasswort ist leer."; return false; }
    if (!Tresor()->Store(ziel.TresorKonto(), passwort)) {
        fehler = "Das Passwort konnte nicht im Tresor abgelegt werden.";
        return false;
    }
    return true;
}

bool ServerTresor::HatPasswort(const ServerZiel& ziel) const {
    return IstOffen() && Tresor()->Has(ziel.TresorKonto());
}

bool ServerTresor::VergissPasswort(const ServerZiel& ziel) {
    return IstOffen() && Tresor()->Remove(ziel.TresorKonto());
}

std::string ServerTresor::CredentialsRef(const ServerZiel& ziel) {
    return "vault:" + kTresorProfil.keyPrefix + ziel.TresorKonto();
}

StoreResult VerbindeMitServer(Store& store, const ServerZiel& ziel,
                              const ServerTresor& tresor) {
    if (!ziel.Vollstaendig())
        return StoreResult::Fail("Für den Mehrplatz-Betrieb werden Host, Datenbank und "
                                 "Datenbankbenutzer gebraucht.");
    if (!tresor.IstOffen())
        return StoreResult::Fail("Der Tresor ist nicht geöffnet.");
    if (!tresor.HatPasswort(ziel))
        return StoreResult::Fail("Für " + ziel.ToUrl() + " ist auf diesem Rechner noch "
                                 "kein Datenbankpasswort hinterlegt.");

    const StoreResult r = store.OpenServer("ultrafibu-server", ziel.host, ziel.port,
                                           ziel.datenbank, ziel.benutzer,
                                           ServerTresor::CredentialsRef(ziel));
    if (!r) return r;

    // Remembered only once it worked: a typo should not become next start's
    // suggestion. A failure to write it is not a failure to connect.
    std::string egal;
    SpeichereLetztenServer(ziel, egal);
    return r;
}

// ===== WHO IS WORKING =====

StoreResult Anmelden(Store& store, const std::string& anmeldename,
                     const std::string& passwort, const std::string& code,
                     Akteur& akteur, bool* codeNoetig) {
    if (codeNoetig) *codeNoetig = false;
    if (anmeldename.empty() || passwort.empty())
        return StoreResult::Fail("Anmeldename und Passwort werden gebraucht.");
    Benutzer benutzer;
    switch (store.Anmelden(anmeldename, passwort, code, benutzer)) {
        case Store::AnmeldeErgebnis::Ok:
            break;
        case Store::AnmeldeErgebnis::CodeNoetig:
            if (codeNoetig) *codeNoetig = true;
            return StoreResult::Fail("Bitte den Code aus UltraAuthenticator eingeben.");
        case Store::AnmeldeErgebnis::CodeFalsch:
            if (codeNoetig) *codeNoetig = true;
            return StoreResult::Fail("Der Code ist falsch, abgelaufen oder schon benutzt. "
                                     "Den nächsten abwarten und neu eingeben.");
        case Store::AnmeldeErgebnis::Gesperrt: {
            const int64_t rest = store.SperreRestSekunden(anmeldename);
            const std::string wann = rest >= 120 ? std::to_string((rest + 59) / 60) + " Minuten"
                                                 : std::to_string(rest) + " Sekunden";
            return StoreResult::Fail("Zu viele Fehlversuche - die Anmeldung für \"" +
                                     anmeldename + "\" ist für " + wann + " gesperrt. "
                                     "Ein Administrator kann die Sperre aufheben.");
        }
        case Store::AnmeldeErgebnis::Abgelehnt:
        default:
            return StoreResult::Fail("Anmeldename oder Passwort ist falsch, oder der "
                                     "Benutzer ist gesperrt.");
    }
    akteur.benutzerId  = benutzer.id;
    akteur.anmeldename = benutzer.anmeldename;
    akteur.rolle       = benutzer.rolle;
    return StoreResult::Ok();
}

} // namespace UltraFIBU

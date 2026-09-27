// Apps/UltraFIBU/ui/UltraFIBUServerFenster.cpp
// The connect-and-sign-in window. See the header for the two steps.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUServerFenster.h"

#include <cstdlib>

using namespace UltraCanvas;

namespace UltraFIBU {

namespace {

constexpr int   kBreite = 620;
constexpr int   kHoehe  = 640;
constexpr float kRand   = 24.0f;
constexpr float kZeile  = 28.0f;
constexpr float kLabelB = 170.0f;

std::string Zahl(int n) { return std::to_string(n); }

} // namespace

std::shared_ptr<UltraCanvasWindow> ServerFenster::Bauen(const ServerZiel& vorgabe,
                                                        const std::string& hinweis) {
    WindowConfig config;
    config.title  = "UltraFIBU - Mit einem Server verbinden";
    config.width  = kBreite;
    config.height = kHoehe;
    fenster_ = CreateWindow(config);

    const float feldB = kBreite - 2 * kRand - kLabelB;
    float y = kRand;

    auto titel = CreateLabel("srvTitel", kRand, y, kBreite - 2 * kRand, 30,
                             "Gemeinsame Buchhaltung auf einem Server");
    titel->SetFontSize(18);
    fenster_->AddChild(titel);
    y += 36;

    auto text = CreateLabel("srvHinweis", kRand, y, kBreite - 2 * kRand, 66, hinweis);
    text->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(text);
    y += 74;

    // ---- step 1: the server ----
    auto feld = [&](const char* id, const char* label) {
        fenster_->AddChild(CreateLabel(std::string(id) + "L", kRand, y, kLabelB, kZeile, label));
        auto eingabe = CreateTextInput(id, static_cast<int>(kRand + kLabelB), static_cast<int>(y),
                                       static_cast<int>(feldB), static_cast<int>(kZeile));
        fenster_->AddChild(eingabe);
        y += kZeile + 8;
        return eingabe;
    };

    host_ = feld("srvHost", "Server");
    host_->SetPlaceholder("z. B. db.kanzlei.local");
    host_->SetText(vorgabe.host);

    port_ = feld("srvPort", "Port");
    port_->SetText(Zahl(vorgabe.port));
    port_->SetMaxLength(5);

    datenbank_ = feld("srvDb", "Datenbank");
    datenbank_->SetPlaceholder("z. B. buchhaltung");
    datenbank_->SetText(vorgabe.datenbank);

    dbBenutzer_ = feld("srvDbUser", "Datenbankbenutzer");
    dbBenutzer_->SetPlaceholder("die PostgreSQL-Rolle dieses Rechners");
    dbBenutzer_->SetText(vorgabe.benutzer);

    fenster_->AddChild(CreateLabel("srvDbPwL", kRand, y, kLabelB, kZeile, "Datenbankpasswort"));
    dbPasswort_ = CreatePasswordInput("srvDbPw", static_cast<int>(kRand + kLabelB),
                                      static_cast<int>(y), static_cast<int>(feldB),
                                      static_cast<int>(kZeile));
    fenster_->AddChild(dbPasswort_);
    y += kZeile + 4;

    // Whether the vault already holds one, said next to the field - so the
    // user knows an empty field is fine rather than guessing.
    tresorHinweis_ = CreateLabel("srvTresor", kRand + kLabelB, y, feldB, 40, "");
    tresorHinweis_->SetWrap(TextWrap::WrapWord);
    tresorHinweis_->SetFontSize(11);
    fenster_->AddChild(tresorHinweis_);
    y += 46;

    for (auto& f : { host_, port_, datenbank_, dbBenutzer_ }) {
        f->onTextChanged = [this](const std::string&) {
            // A different server is a different connection: whatever was
            // connected no longer applies to what the fields now say.
            if (verbunden_) { verbunden_ = false; AnmeldungFreigeben(false); }
            TresorHinweisAktualisieren();
        };
    }

    verbindenKnopf_ = CreateButton("srvVerbinden", kRand + kLabelB, y, 160, kZeile, "Verbinden");
    verbindenKnopf_->SetOnClick([this]() { Verbinden(); });
    fenster_->AddChild(verbindenKnopf_);
    y += kZeile + 22;

    // ---- step 2: the person ----
    auto anmeldungTitel = CreateLabel("srvAnmTitel", kRand, y, kBreite - 2 * kRand, 24,
                                      "Anmelden");
    anmeldungTitel->SetFontSize(14);
    fenster_->AddChild(anmeldungTitel);
    y += 30;

    name_ = feld("srvName", "Anmeldename");
    fenster_->AddChild(CreateLabel("srvPwL", kRand, y, kLabelB, kZeile, "Passwort"));
    passwort_ = CreatePasswordInput("srvPw", static_cast<int>(kRand + kLabelB),
                                    static_cast<int>(y), static_cast<int>(feldB),
                                    static_cast<int>(kZeile));
    passwort_->onEnterPressed = [this](const std::string&) { Anmelden(); return true; };
    fenster_->AddChild(passwort_);
    y += kZeile + 8;

    anmeldenKnopf_ = CreateButton("srvAnmelden", kRand + kLabelB, y, 160, kZeile, "Anmelden");
    anmeldenKnopf_->SetOnClick([this]() { Anmelden(); });
    fenster_->AddChild(anmeldenKnopf_);
    y += kZeile + 12;

    status_ = CreateLabel("srvStatus", kRand, y, kBreite - 2 * kRand, 60, "");
    status_->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(status_);

    std::string fehler;
    if (!tresor_.Oeffnen(fehler)) Melden(fehler, true);
    TresorHinweisAktualisieren();
    AnmeldungFreigeben(false);
    return fenster_;
}

void ServerFenster::Schliessen() {
    if (fenster_) fenster_->PerformClose();
}

bool ServerFenster::LiesZiel(ServerZiel& out, std::string& fehler) const {
    // Built as an address and parsed, so the form accepts exactly what the
    // command line accepts - one set of rules for host, user and database.
    const std::string port = port_ ? port_->GetText() : std::string();
    const std::string url = "postgresql://" + dbBenutzer_->GetText() + "@" +
                            (host_->GetText().find(':') != std::string::npos
                                 ? "[" + host_->GetText() + "]" : host_->GetText()) +
                            (port.empty() ? std::string() : ":" + port) + "/" +
                            datenbank_->GetText();
    return ParseServerUrl(url, out, fehler);
}

void ServerFenster::TresorHinweisAktualisieren() {
    if (!tresorHinweis_) return;
    ServerZiel ziel;
    std::string egal;
    if (!LiesZiel(ziel, egal) || !tresor_.IstOffen()) {
        tresorHinweis_->SetText("Wird nur beim ersten Mal gebraucht und dann im Tresor "
                                "dieses Rechners abgelegt.");
        return;
    }
    tresorHinweis_->SetText(tresor_.HatPasswort(ziel)
        ? "Im Tresor hinterlegt - leer lassen, oder neu eingeben, um es zu ersetzen."
        : "Noch nicht hinterlegt. Wird beim Verbinden im Tresor dieses Rechners "
          "abgelegt.");
}

void ServerFenster::Verbinden() {
    ServerZiel ziel;
    std::string fehler;
    if (!LiesZiel(ziel, fehler)) { Melden(fehler, true); return; }
    const std::string dbPasswort = dbPasswort_->GetText();
    if (dbPasswort.empty() && !tresor_.HatPasswort(ziel)) {
        Melden("Für diesen Server ist noch kein Datenbankpasswort hinterlegt.", true);
        return;
    }

    Melden("Verbindung wird aufgebaut ...", false);
    bool leer = false;
    fehler = onVerbinden ? onVerbinden(ziel, dbPasswort, leer)
                         : std::string("Es ist niemand da, der die Verbindung aufbaut.");
    // The field is cleared either way: the password is in the vault now, or
    // it was wrong - and a password left in a widget is a copy nobody wiped.
    dbPasswort_->SetText("");
    TresorHinweisAktualisieren();
    if (!fehler.empty()) { Melden(fehler, true); return; }

    verbunden_ = true;
    if (leer) {
        AnmeldungFreigeben(false);
        Melden("Verbunden - aber in dieser Datenbank ist noch keine Buchhaltung "
               "eingerichtet. Einrichten mit:\nultrafibu einrichten " + ziel.ToUrl() +
               " --firma \"...\" --gj-beginn 01.01.2026", true);
        return;
    }
    AnmeldungFreigeben(true);
    Melden("Verbunden mit " + ziel.ToUrl() + ". Jetzt anmelden.", false);
}

void ServerFenster::Anmelden() {
    if (!verbunden_) { Melden("Erst mit dem Server verbinden.", true); return; }
    const std::string name = name_->GetText();
    const std::string pw   = passwort_->GetText();
    // Never kept in the widget longer than this call.
    passwort_->SetText("");
    const std::string fehler = onAnmelden
        ? onAnmelden(name, pw)
        : std::string("Es ist niemand da, der die Anmeldung prüft.");
    if (!fehler.empty()) { Melden(fehler, true); return; }
    Schliessen();
}

void ServerFenster::AnmeldungFreigeben(bool frei) {
    if (name_) name_->SetDisabled(!frei);
    if (passwort_) passwort_->SetDisabled(!frei);
    if (anmeldenKnopf_) anmeldenKnopf_->SetDisabled(!frei);
}

void ServerFenster::Melden(const std::string& text, bool fehler) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(fehler ? Color(176, 32, 32) : Color(40, 40, 40));
}

} // namespace UltraFIBU

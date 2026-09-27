// Apps/UltraFIBU/ui/UltraFIBUZweiterFaktorFenster.cpp
// The enrolment window. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUZweiterFaktorFenster.h"

using namespace UltraCanvas;

namespace UltraFIBU {

namespace {

constexpr int   kBreite = 560;
constexpr int   kHoehe  = 640;
constexpr float kRand   = 24.0f;
constexpr float kZeile  = 28.0f;
constexpr float kLabelB = 190.0f;
constexpr float kQr     = 220.0f;

// The key in groups of four, as authenticator apps show and accept it.
std::string InGruppen(const std::string& schluessel) {
    std::string out;
    for (size_t i = 0; i < schluessel.size(); ++i) {
        if (i > 0 && i % 4 == 0) out.push_back(' ');
        out.push_back(schluessel[i]);
    }
    return out;
}

} // namespace

std::shared_ptr<UltraCanvasWindow> ZweiterFaktorFenster::Bauen(Store& store,
                                                               const Akteur& akteur,
                                                               const std::string& konto) {
    store_  = &store;
    akteur_ = akteur;

    WindowConfig config;
    config.title  = "UltraFIBU - Zweiten Faktor einrichten";
    config.width  = kBreite;
    config.height = kHoehe;
    fenster_ = CreateWindow(config);
    fenster_->onWindowClosed = [this]() { Wischen(); };

    float y = kRand;
    auto titel = CreateLabel("zfTitel", kRand, y, kBreite - 2 * kRand, 30,
                             "Zweiter Faktor für " + akteur.anmeldename);
    titel->SetFontSize(18);
    fenster_->AddChild(titel);
    y += 36;

    auto text = CreateLabel("zfText", kRand, y, kBreite - 2 * kRand, 44,
        "1. In UltraAuthenticator \"Konto hinzufügen\" wählen und diesen Code scannen - "
        "oder den Schlüssel darunter eintippen.");
    text->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(text);
    y += 50;

    const StoreResult vorbereitet = store.ZweitenFaktorVorbereiten(akteur, konto, einrichtung_);

    qr_ = std::make_shared<UltraCanvasQRCode>("zfQr", static_cast<int>((kBreite - kQr) / 2),
                                              static_cast<int>(y), static_cast<int>(kQr),
                                              static_cast<int>(kQr));
    // Medium correction: plenty for a screen, and a smaller symbol scans
    // faster from a phone held at arm's length.
    qr_->SetErrorCorrection(QRErrorCorrection::Medium);
    if (vorbereitet) qr_->SetContent(einrichtung_.otpauthUri);
    fenster_->AddChild(qr_);
    y += kQr + 8;

    schluessel_ = CreateLabel("zfSchluessel", kRand, y, kBreite - 2 * kRand, 24,
                              vorbereitet ? InGruppen(einrichtung_.geheimnisBase32) : "");
    schluessel_->SetAlignment(TextAlignment::Center);
    fenster_->AddChild(schluessel_);
    y += 36;

    auto text2 = CreateLabel("zfText2", kRand, y, kBreite - 2 * kRand, 44,
        "2. Den sechsstelligen Code, den die App jetzt zeigt, eingeben und mit dem "
        "eigenen Passwort bestätigen.");
    text2->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(text2);
    y += 50;

    const float feldB = kBreite - 2 * kRand - kLabelB;
    fenster_->AddChild(CreateLabel("zfCodeL", kRand, y, kLabelB, kZeile, "Code aus der App"));
    code_ = CreateTextInput("zfCode", static_cast<int>(kRand + kLabelB), static_cast<int>(y),
                            static_cast<int>(feldB), static_cast<int>(kZeile));
    code_->SetMaxLength(7);   // "123 456" as some apps show it
    fenster_->AddChild(code_);
    y += kZeile + 8;

    fenster_->AddChild(CreateLabel("zfPwL", kRand, y, kLabelB, kZeile, "Eigenes Passwort"));
    passwort_ = CreatePasswordInput("zfPw", static_cast<int>(kRand + kLabelB),
                                    static_cast<int>(y), static_cast<int>(feldB),
                                    static_cast<int>(kZeile));
    passwort_->onEnterPressed = [this](const std::string&) { Aktivieren(); return true; };
    fenster_->AddChild(passwort_);
    y += kZeile + 12;

    aktivierenKnopf_ = CreateButton("zfAktivieren", kRand + kLabelB, y, 180, kZeile,
                                    "Aktivieren");
    aktivierenKnopf_->SetOnClick([this]() { Aktivieren(); });
    fenster_->AddChild(aktivierenKnopf_);
    y += kZeile + 12;

    status_ = CreateLabel("zfStatus", kRand, y, kBreite - 2 * kRand, 50, "");
    status_->SetWrap(TextWrap::WrapWord);
    fenster_->AddChild(status_);

    if (!vorbereitet) {
        aktivierenKnopf_->SetDisabled(true);
        Melden(vorbereitet.fehler, true);
    }
    return fenster_;
}

void ZweiterFaktorFenster::Schliessen() {
    if (fenster_) fenster_->PerformClose();
}

void ZweiterFaktorFenster::Wischen() {
    // The secret was on screen; it does not stay in the widgets or here.
    if (qr_) qr_->SetContent(std::string());
    if (schluessel_) schluessel_->SetText(std::string());
    if (passwort_) passwort_->SetText(std::string());
    std::fill(einrichtung_.geheimnisBase32.begin(), einrichtung_.geheimnisBase32.end(), '\0');
    std::fill(einrichtung_.otpauthUri.begin(), einrichtung_.otpauthUri.end(), '\0');
    einrichtung_ = Store::ZweiterFaktorEinrichtung();
}

void ZweiterFaktorFenster::Aktivieren() {
    if (store_ == nullptr) return;
    const std::string pw = passwort_->GetText();
    passwort_->SetText(std::string());
    const StoreResult r = store_->ZweitenFaktorAktivieren(akteur_, pw, einrichtung_.geheimnisBase32,
                                                          code_->GetText());
    if (!r) { Melden(r.fehler, true); return; }
    if (onFertig)
        onFertig("Zweiter Faktor eingerichtet - ab der nächsten Anmeldung wird der Code "
                 "aus UltraAuthenticator verlangt.");
    Schliessen();
}

void ZweiterFaktorFenster::Melden(const std::string& text, bool fehler) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(fehler ? Color(176, 32, 32) : Color(40, 40, 40));
}

} // namespace UltraFIBU

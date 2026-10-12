# Datenschutzerklärung — UltraMail

**Version 1.0 (Entwurf zur rechtlichen Prüfung) · 10. Oktober 2026**

Diese Erklärung beschreibt, welche personenbezogenen Daten die Anwendung
**UltraMail** verarbeitet, wo diese Verarbeitung stattfindet, welche Dritten
sie zu welchem Zweck kontaktiert und welche Rechte Sie nach der
Datenschutz-Grundverordnung (DSGVO) haben. Sie wird unter www.ultraos.eu
veröffentlicht und gilt für UltraMail unter Linux, Windows, macOS und
ULTRA OS. Sie gilt nicht für die Website selbst, für andere
ULTRA-OS-Anwendungen oder für Online-Dienste von ULTRA OS; diese haben jeweils
eine eigene Erklärung.

> **Prüfstatus.** Dieser Text wurde aus dem Quellcode und der Dokumentation
> von UltraMail erstellt. Die Angaben in eckigen Klammern (`[…]`) sind
> auszufüllen, und der gesamte Text ist vor der Veröffentlichung anwaltlich zu
> prüfen.

## 1. Verantwortlicher

ULTRA OS Development GmbH
An den Klippen 23
57462 Olpe
Deutschland

E-Mail: [privacy@ultraos.eu]
Geschäftsführung: […]
Handelsregister: […]

Ein Datenschutzbeauftragter ist nicht bestellt, da keine Pflicht zur
Bestellung besteht [in der rechtlichen Prüfung bestätigen].

## 2. Das Wichtigste in Kürze

- UltraMail ist ein E-Mail-Programm, das **auf Ihrem eigenen Rechner** läuft.
  Ihre Konten, Passwörter, Nachrichten, Anhänge und Kontakte werden auf Ihrem
  Gerät gespeichert und nirgendwo sonst.
- UltraMail verbindet sich mit den **Servern Ihres E-Mail-Anbieters**, um
  Ihre Post abzurufen und zu versenden. Dafür ist ein E-Mail-Programm da.
- UltraMail hat **kein Benutzerkonto bei uns**, sendet uns **keine
  Nutzungsstatistiken, Absturzberichte oder Telemetrie**, enthält **keine
  Werbung** und führt **keine Update-Prüfung** durch, die unsere Server
  kontaktieren würde. Die ULTRA OS Development GmbH erhält, speichert oder
  sieht über UltraMail keine Ihrer Daten.
- UltraMail kontaktiert für bestimmte Funktionen eine kleine Zahl **anderer
  Server** (Ermittlung der Servereinstellungen Ihres Anbieters, Anmeldung bei
  Gmail, Outlook oder Yahoo, Herunterladen von Absender-Symbolen, Laden von
  Bildern in HTML-Mails, Anhängen eines Cloud-Links). Jeder ist in Abschnitt 5
  aufgeführt, zusammen mit der Einstellung, die ihn abschaltet, wo es eine
  gibt.
- UltraMail ist quelloffene Software. Jeder kann diese Aussagen am Quellcode
  nachprüfen.

## 3. Was UltraMail auf Ihrem Gerät speichert

Alles Folgende liegt im Datenordner von UltraMail auf Ihrem Rechner:
`~/.local/share/UltraMail/` unter Linux und macOS (bzw.
`$XDG_DATA_HOME/UltraMail/`, wenn diese Variable gesetzt ist),
`%APPDATA%\UltraMail\` unter Windows.

| Daten | Was es ist | Wo |
|---|---|---|
| Kontoeinstellungen | Ihr Name, Ihre E-Mail-Adresse, Servernamen, Ports und Verschlüsselungseinstellungen Ihres Anbieters, Ihre Signatur, das Abrufintervall | `mail.db` |
| Zugangsdaten | Ihr Kontopasswort, App-Passwort oder die OAuth2-Token aus der Anmeldung im Browser | `vault/ultramail.vault`, verschlüsselt (siehe 3.1) |
| Nachrichten | Die Kopfzeilen der Nachrichten in Ihren Ordnern (der Index) und der vollständige Inhalt (Text, HTML, Anhänge) der von UltraMail abgerufenen Nachrichten als `.eml`-Dateien | `mail.db`, `mail/<Konto>/` |
| Anhänge | Anhänge, die Sie öffnen, werden in einen Ordner ausgepackt; bei jedem Start werden die seit einer Woche nicht geöffneten entfernt, dann die ältesten, bis der Ordner unter 256 MB liegt | `attachments/` |
| Postausgang | Von Ihnen geschriebene und gesendete Nachrichten, bis der Server sie angenommen hat; Entwürfe | `outbox.db` |
| Kontakte | Ihr Adressbuch: Namen, E-Mail-Adressen, Telefonnummern, Organisationen und Notizen, die Sie eingeben, sowie die aus Ihrer eigenen Post abgeleiteten Angaben „wartet auf Antwort“ und „angeschrieben“ | `contacts.db` |
| Absender-Symbole | Symbole der Dienste und Websites, die Ihnen schreiben (siehe 5.4) | `cache/sender-icons/` |
| Einstellungen | Ihre Einstellungen, darunter welche Absender Bilder aus dem Netz laden dürfen und welchen Websites Sie vertrauen | `preferences.ini` |
| Cloud-Speicherkonten | Die Cloud-Speicherkonten, die Sie für „Cloud-Link anhängen“ hinzufügen (siehe 5.6) | `cloud.db` |
| OAuth-Client-Registrierung | Falls Sie einen eigenen OAuth-Client bei Google oder Microsoft registrieren: dessen Client-ID und Secret | `oauth.ini` |
| Zeitprotokoll | Ein Protokoll, was UltraMail gerade tut und wie lange jeder Schritt dauert, zur Leistungsdiagnose. Es nennt Konten, Ordner und Zeiten, keine Nachrichteninhalte. Es wird bei jedem Start geleert und verlässt Ihr Gerät nie. `ULTRAMAIL_TRACE=0` schaltet es ab | `trace.log` |

### 3.1 Wie Zugangsdaten geschützt sind

Passwörter und Anmelde-Token werden nie in die Kontoeinstellungen
geschrieben. Sie liegen in einer verschlüsselten Tresordatei (Schlüssel
abgeleitet mit Argon2id, Inhalt verschlüsselt mit XChaCha20-Poly1305). Der
Tresor wird entweder mit einem von Ihnen gewählten Master-Passwort entsperrt,
das nie gespeichert wird, oder mit einer Geräteschlüsseldatei, die nur Ihr
Benutzerkonto auf dem Rechner lesen kann. Wenn Sie das Master-Passwort
vergessen, kann niemand, auch wir nicht, die gespeicherten Passwörter
wiederherstellen; Sie geben sie erneut ein.

### 3.2 Wer diese Daten lesen kann

Nur Sie und jeder, der Zugriff auf Ihr Benutzerkonto auf dem Rechner oder auf
dessen Sicherungen hat. UltraMail lädt den Datenordner nirgendwohin hoch,
synchronisiert oder kopiert ihn nicht. Das Entfernen eines Kontos in UltraMail
löscht dessen Nachrichten und Zugangsdaten; das Löschen des Datenordners
entfernt alles, was UltraMail kennt.

### 3.3 Unsere Rolle

Da diese Verarbeitung auf Ihrem Gerät, unter Ihrer Kontrolle und zu Ihren
eigenen Zwecken stattfindet, ist die ULTRA OS Development GmbH für diese
Daten **nicht Verantwortlicher** im Sinne von Art. 4 Nr. 7 DSGVO. Wir
beschreiben sie hier der Transparenz halber und weil die Plattformen und
Stores, über die UltraMail verbreitet wird, eine Datenschutzerklärung
verlangen.

## 4. Daten, die UltraMail an Ihren E-Mail-Anbieter sendet

Um seine Aufgabe zu erfüllen, verbindet sich UltraMail mit den Servern des von
Ihnen eingerichteten E-Mail-Anbieters und übermittelt:

- Ihre Zugangsdaten (Passwort, App-Passwort oder OAuth2-Token) zur Anmeldung;
- die Nachrichten, die Sie senden, mit Empfängern, Betreff, Text und Anhängen;
- Anfragen nach Ihren Ordnern, Nachrichtenkopfzeilen, Nachrichteninhalten und
  Markierungen (gelesen, beantwortet, gelöscht) sowie die Änderungen, die Sie
  daran vornehmen.

**Empfänger:** Ihr E-Mail-Anbieter als eigener Verantwortlicher. Für den
Umgang mit Ihrer Post gilt dessen Datenschutzerklärung. **Verschlüsselung:**
UltraMail verwendet standardmäßig für jede Verbindung TLS und prüft das
Zertifikat des Servers. **Rechtsgrundlage:** Dies ist der Dienst, den Sie mit
dem Hinzufügen des Kontos anfordern (Art. 6 Abs. 1 lit. b DSGVO).
**Einstellung:** keine; ohne diese Verbindung kann UltraMail nicht arbeiten.

## 5. Weitere Server, die UltraMail kontaktiert, und warum

### 5.1 Ermittlung der Servereinstellungen Ihres Anbieters (Kontoeinrichtung)

Beim Hinzufügen eines Kontos ermittelt UltraMail die Servereinstellungen aus
Ihrer Adresse. Für die häufigsten Anbieter (Gmail, Outlook, Yahoo, iCloud,
GMX, web.de, mailbox.org, Posteo und andere) nutzt es eine eingebaute Tabelle
und kontaktiert niemanden. Für jede andere Domain fragt es in dieser
Reihenfolge:

1. `https://autoconfig.<Ihre Domain>/mail/config-v1.1.xml` und
   `https://<Ihre Domain>/.well-known/autoconfig/mail/config-v1.1.xml` — die
   Server Ihres eigenen Anbieters.
2. `https://autoconfig.thunderbird.net/v1.1/<Ihre Domain>` — die
   Thunderbird-Anbieterdatenbank, betrieben von der MZLA Technologies
   Corporation (Mozilla). Für die Anfrage gilt die Datenschutzerklärung von
   Mozilla.

   **Alle diese Anfragen enthalten nur die Domain Ihrer Adresse**
   (`example.com`), nie die Adresse selbst. Das Autoconfig-Format erlaubt
   einem Programm, die vollständige Adresse mitzusenden; UltraMail tut das
   nicht.
3. DNS-Abfragen (SRV- und MX-Einträge Ihrer Domain) über den Resolver Ihres
   Systems sowie Verbindungsversuche zu den üblichen Servernamen und Ports.

**Übermittelte Daten:** die Domain Ihrer E-Mail-Adresse und Ihre
IP-Adresse. **Zweck:** das Konto einzurichten, ohne
Sie nach Servereinstellungen zu fragen. **Rechtsgrundlage:** Art. 6 Abs. 1
lit. b und f DSGVO; die Abfrage erfolgt einmalig bei der Einrichtung.
**Einstellung:** keine, aber Sie können die Abfrage abbrechen und die
Servereinstellungen von Hand eingeben; die Abfrage unterbleibt dann.

### 5.2 Anmeldung bei Google, Microsoft oder Yahoo (OAuth2)

Gmail-, Outlook-/Microsoft-365- und Yahoo-Konten melden sich über die
Einwilligungsseite des Anbieters in Ihrem Webbrowser an (OAuth2 Authorization
Code mit PKCE). UltraMail öffnet die Seite, Sie melden sich beim Anbieter an,
und der Anbieter übergibt UltraMail ein Token, das im Tresor gespeichert und
für IMAP und SMTP verwendet wird. Ihr Anbieter-Passwort sieht UltraMail nie.

**Übermittelte Daten:** die von Ihnen eingegebene E-Mail-Adresse (als
Anmeldehinweis) und alles, was Sie auf der Seite des Anbieters eingeben, nur
an den Anbieter. **Empfänger:** Google LLC, Microsoft Corporation oder Yahoo
(Oath Holdings Inc.) als eigene Verantwortliche nach ihren eigenen
Datenschutzerklärungen. **Rechtsgrundlage:** Art. 6 Abs. 1 lit. b DSGVO; der
Anbieter verlangt dieses Verfahren. **Einstellung:** Wo der Anbieter ein
App-Passwort anbietet, können Sie stattdessen dieses verwenden; dann findet
keine Anmeldung im Browser statt.

### 5.3 Bilder in HTML-Mails („externe Bilder“)

Eine Nachricht kann auf Bilder verweisen, die auf dem Webserver des Absenders
liegen. Das Laden eines solchen Bildes verrät diesem Server, dass und wann die
Nachricht geöffnet wurde, und Ihre IP-Adresse; Werbe-Mails nutzen das, um
Öffnungen nachzuverfolgen. UltraMail lädt externe Bilder von unbekannten
Absendern deshalb **standardmäßig nicht**. Die Voreinstellung
(*Settings → Privacy → Images*, Einstellungen → Datenschutz → Bilder) ist *nur vertrauenswürdige
Absender*: Bilder laden automatisch nur von Personen in Ihrem Adressbuch, von
Absendern, die Sie mit „Immer von …“ erlaubt haben, und von Websites, die Sie
als vertrauenswürdig eingetragen haben. Bei allen anderen fragt eine Leiste im
Lesebereich jedes Mal nach. Sie können die Einstellung auf *nie* oder *immer*
ändern; auch *immer* lässt Junk und verdächtige Mails aus.

**Übermittelte Daten:** Ihre IP-Adresse und die Adresse des Bildes an den
Server, der das Bild bereitstellt, in dem Moment, in dem Sie (oder die
Einstellung) es laden. **Empfänger:** der Absender oder dessen
Hosting-Anbieter. **Rechtsgrundlage:** Ihre Entscheidung (Art. 6 Abs. 1
lit. a DSGVO). **Einstellung:** *Settings → Privacy → Images*.

### 5.4 Absender-Symbole

Neben jedem Absender zeigt UltraMail ein Abzeichen: für etwa 400 bekannte
Dienste (Banken, Shops, Paketdienste, soziale Netzwerke …) deren Symbol, für
alle anderen ein Monogramm. Dabei kommen zwei optionale Downloads vor:

- **Symbole bekannter Dienste.** Wenn ein bekannter Dienst zum ersten Mal in
  Ihrem Posteingang erscheint, lädt UltraMail dessen Symbol (Favicon) einmal
  von der Website des Dienstes selbst herunter — zum Beispiel
  `https://www.paypal.com/favicon.ico`. Der Webserver des Dienstes erfährt
  Ihre IP-Adresse und dass eine Installation von UltraMail sein Symbol
  angefordert hat. Er erfährt **nicht**, welche Nachricht Sie erhalten haben
  oder wie Ihre E-Mail-Adresse lautet. Das Symbol wird danach im Cache
  behalten.
- **Website-Symbole anderer Absender.** Für einen Absender, der kein
  bekannter Dienst ist, kann UltraMail die Startseite der Domain, von der er
  schreibt, lesen, um das Symbol der Website zu finden. Der Webserver des
  Absenders erfährt, dass jemand mit Ihrer IP-Adresse seine Startseite
  aufgerufen hat, nicht aber welche Nachricht oder wer Sie sind. Das geschieht
  höchstens einmal pro Woche und Domain, nur für Mails, die die Spam- und
  Phishing-Prüfung von UltraMail bestanden haben, und nie für den
  Junk-Ordner.

Beide Downloads sind **standardmäßig ausgeschaltet**: Eine neue
Installation kontaktiert niemanden außer Ihrem E-Mail-Anbieter, bis Sie sie
unter *Settings → Privacy → Sender icons* (Einstellungen → Datenschutz →
Absender-Symbole) getrennt einschalten (*Download the icons of known
senders* — Symbole bekannter Absender herunterladen; *Show other senders'
website icons* — Website-Symbole anderer Absender anzeigen). Ist der erste
Schalter aus, wird für Abzeichen gar nichts heruntergeladen. Bereits heruntergeladene Symbole werden weiter angezeigt, bis
Sie den Cache-Ordner löschen.

**Übermittelte Daten:** Ihre IP-Adresse und eine Anfrage nach dem Symbol oder
der Startseite. **Empfänger:** die Betreiber der jeweiligen Websites.
**Rechtsgrundlage:** Ihre Einwilligung, erteilt durch Einschalten des
Schalters und widerrufen durch Ausschalten (Art. 6 Abs. 1 lit. a DSGVO). **Einstellung:** *Settings → Privacy →
Sender icons*.

### 5.5 Spam- und Phishing-Prüfung

UltraMail bewertet jede eingehende Nachricht auf Anzeichen von Spam und
Phishing (gefälschte Absenderdomains, Aufforderungen zur Eingabe von
Zugangsdaten, von Ihrem eigenen Mailserver gemeldete fehlgeschlagene
DKIM-/SPF-/DMARC-Prüfungen und andere) und zeigt ein Warnabzeichen. **Diese
Prüfung läuft vollständig auf Ihrem Gerät.** Kein Nachrichteninhalt, keine
Adresse und kein Link wird an uns oder an einen Reputationsdienst gesendet.

### 5.6 Einen Cloud-Link anhängen

„Cloud-Link anhängen…“ im Editor lädt eine Datei in ein Cloud-Speicherkonto
von Ihnen hoch (Nextcloud / ownCloud, ein WebDAV-Server, Dropbox, OneDrive
oder Google Drive) und fügt den Freigabelink in die Nachricht ein. Das
geschieht nur, wenn Sie die Funktion verwenden. Die Datei und Ihre
Zugangsdaten für diesen Dienst gehen an den von Ihnen gewählten Dienst nach
dessen eigener Datenschutzerklärung. Dropbox, OneDrive und Google Drive melden
sich wie in 5.2 beschrieben über den Browser an.

### 5.7 Links, die Sie anklicken

Ein Link in einer Nachricht öffnet sich in Ihrem Webbrowser. Ab dann gelten
die Datenschutzerklärungen des Browsers und der verlinkten Website. UltraMail
prüft Links nicht gegen einen Online-Dienst.

### 5.8 Benachrichtigungen und der Desktop-Feed

Unter ULTRA OS meldet UltraMail neue Post über eine lokale Schnittstelle
(UltraMessage) an den Benachrichtigungsdienst und die Nachrichtenzentrale des
Desktops. Das bleibt auf Ihrem Gerät.

### 5.9 Rechtschreibprüfung

Die Rechtschreibprüfung im Editor verwendet auf Ihrem Rechner installierte
Wörterbücher (Hunspell oder die Rechtschreibprüfung des Systems). Nichts, was
Sie tippen, wird irgendwohin gesendet.

## 6. Was wir nicht tun

UltraMail

- legt kein Benutzerkonto bei der ULTRA OS Development GmbH an und verlangt
  keines;
- sendet keine Nutzungsstatistiken, Analysen, Absturzberichte oder
  Diagnosedaten an uns oder andere;
- prüft nicht auf Updates, indem es unsere Server kontaktiert;
- zeigt keine Werbung und enthält keine Werbe- oder Tracking-SDKs Dritter;
- liest Ihre Nachrichten zu keinem anderen Zweck als sie Ihnen anzuzeigen,
  sie für die Suche und die lokale Spam-Prüfung zu indizieren und für die
  lokale Funktion „wartet auf Antwort“;
- gibt Ihre Daten nicht weiter, verkauft sie nicht und legt sie nicht
  anderweitig offen.

Da keine Daten bei uns ankommen, speichern wir keine personenbezogenen Daten
über Nutzer von UltraMail und können Sie anhand Ihrer Nutzung der Anwendung
nicht identifizieren.

## 7. Ihre Rechte

Nach der DSGVO haben Sie das Recht auf Auskunft (Art. 15), Berichtigung
(Art. 16), Löschung (Art. 17), Einschränkung der Verarbeitung (Art. 18),
Datenübertragbarkeit (Art. 20) und Widerspruch (Art. 21) sowie das Recht auf
Beschwerde bei einer Aufsichtsbehörde (Art. 77). Die für uns zuständige
Behörde ist die Landesbeauftragte für Datenschutz und Informationsfreiheit
Nordrhein-Westfalen, Kavalleriestraße 2–4, 40213 Düsseldorf.

Da UltraMail Ihre Daten nur auf Ihrem eigenen Gerät speichert, üben Sie diese
Rechte unmittelbar in der Anwendung aus: Sie können alles selbst lesen,
berichtigen, exportieren (Ihre Nachrichten sind gewöhnliche `.eml`-Dateien)
und löschen. Für Daten, die Ihr E-Mail-Anbieter, Google, Microsoft, Yahoo,
Mozilla oder die Betreiber der in Abschnitt 5 genannten Websites halten,
wenden Sie sich bitte an diese; sie sind für diese Verarbeitung die
Verantwortlichen.

Bei Fragen zu dieser Erklärung schreiben Sie an [privacy@ultraos.eu].

## 8. Kinder

UltraMail ist ein allgemeines E-Mail-Programm und richtet sich nicht an
Kinder. Da es keine Daten für uns erhebt, findet keine Altersprüfung statt.

## 9. Änderungen dieser Erklärung

Wir aktualisieren diese Erklärung, wann immer sich die Datenflüsse von
UltraMail ändern, etwa wenn eine Funktion hinzukommt, die einen Server
kontaktiert. Version und Datum am Anfang kennzeichnen den aktuellen Text;
frühere Fassungen sind im Quellcode-Repository des Projekts verfügbar.

#### 2026-09-20 *0.13.0*
- **Eine Rechnung, die 19 % ausweist und zugleich schreibt, der Empfaenger
  schulde die Steuer, wird nicht mehr erzeugt.** Genau das hat das Programm
  getan: 1.000,00 netto, "zzgl. 19 % USt 190,00", 1.190,00 gesamt - und
  darunter, an der Stelle, die § 14 Abs. 4 Nr. 8 UStG fuer den
  Befreiungshinweis vorsieht, der Satz "Steuerschuldnerschaft des
  Leistungsempfaengers". Welche Haelfte der Leser glaubt, die andere war
  falsch. Nichts im Programm hat widersprochen.
- **Die Ursache war ein Schluessel fuer drei Regeln.** "Reverse Charge"
  heissen drei verschiedene Dinge, und RC13b war nur eines davon:
  - `EURC` - sonstige Leistung an ein EU-Unternehmen. Leistungsort ist das
    Land des Kunden (§ 3a Abs. 2 UStG), also 0 % auf der deutschen Rechnung;
    der Kunde versteuert sie bei sich.
  - `RC13b` - EINGANG: wir beziehen eine Leistung und schulden die deutsche
    Steuer selbst (§ 13b UStG), deshalb 19 %.
  - `RC13bAus` - AUSGANG: wir erbringen eine Leistung, bei der der deutsche
    Empfaenger die Steuer schuldet, deshalb 0 %.
  Dazu `SteuerArt::EuSonstigeLeistung` und der Hinweis auf der Rechnung.
- **`PruefeSteuerlicheStimmigkeit` ist das, was die naechste Variante
  verhindert.** Der Widerspruch ist ohne Steuerrecht sichtbar: ein Beleg kann
  nicht Steuer berechnen und zugleich sagen, es werde keine berechnet.
  Geprueft wird ausserdem: Vorsteuerschluessel auf einer Ausgangsrechnung,
  Ausgangsschluessel auf einem Eingangsbeleg, ein Schluessel, der am
  Belegdatum nicht galt, eine OSS-Zeile ohne Bestimmungsland, und die
  Steuerbefreiung fuer einen Inlandskunden.
- **Ohne USt-IdNr. des Kunden keine Steuerfreiheit.** Sie ist die Bedingung
  der Befreiung, nicht eine Formalie: fehlt sie, wird die Rechnung nicht
  geschrieben und nicht gebucht. Bisher wurde sie nur als fehlende
  Pflichtangabe vermerkt und die Rechnung trotzdem gedruckt.
- **Gebucht wird auch nicht.** Der Belegdruck haelt eine falsche Rechnung vom
  Kunden fern, aber buchen ist der unumkehrbare Schritt: danach laesst sich
  der Vorgang nur noch stornieren. Beide Wege pruefen dasselbe.
- **Die Steuerschluessel-Auswahl richtet sich nach dem Partner.**
  `SteuerschluesselFuerPartner` liefert die Schluessel, die zu Land,
  Unternehmereigenschaft, USt-IdNr. und Belegrichtung passen - mit einem
  deutschen Satz, warum. Das ist das, woran ein Auswahlfeld haengt: wer eine
  Rechnung nach Wien schreibt, soll nicht wissen muessen, dass eine
  Dienstleistung "EURC" und eine Warenlieferung "IGL" ist.
- **Wo das Programm es nicht wissen kann, gibt es keine Vorauswahl.** Ware
  oder Dienstleistung entscheidet zwischen zwei rechtlich verschiedenen
  Behandlungen, und die Reihenfolge einer Liste ist kein Grund, eine davon zu
  waehlen. Bei einem Inlandskunden ist 19 % die Vorgabe, 7 % eine
  Entscheidung; bei einem EU-Unternehmen ohne USt-IdNr. ist deutsche
  Umsatzsteuer die Vorgabe.
- **Ein Vorschlag ist nie etwas, das das Buchen ablehnen wuerde.** Die
  Auswahl fragt dieselbe Pruefung, die beim Buchen entscheidet - sonst
  empfiehlt das eine, was das andere verweigert. Ein Test geht alle
  Kombinationen durch.
- Neu: `ultrafibu steuerwahl <datei> --partner ... --datum ...` zeigt, was das
  Auswahlfeld anbieten wird. 32 weitere Pruefungen (1251 insgesamt).
- **Offen und wichtig:** die Rechnungserfassung selbst hat noch kein
  Eingabefenster - Belege entstehen ueber die Kommandozeile, und das
  Auswahlfeld, an dem das hier haengt, gibt es damit noch nicht. Die
  Kennzahlen 21, 46 und 47 (§ 18b, § 13b) bleiben ungeprueft und damit leer;
  eine Voranmeldung, die sie braucht, verweigert die Datei. Die Formulierung
  der Befreiungshinweise gehoert vor dem ersten Echteinsatz dem
  Steuerberater vorgelegt.

#### 2026-09-20 *0.12.0*
- **EU-Steuersaetze als Tabelle mit Editor.** Schema v6 (`eu_steuersatz`),
  `Store::EuSteuersatzSetzen/Loeschen/AusDatei`, ein eigener Reiter unter
  Konfiguration -> EU-Steuersaetze mit dem Knopf "Neuen Steuersatz setzen",
  dazu `ultrafibu eu-saetze`, `eu-satz-neu`, `eu-satz-loeschen` und
  `eu-saetze-uebernehmen`. 32 weitere Pruefungen (1219 insgesamt).
- **Warum ueberhaupt eine Tabelle.** Sechsundzwanzig Parlamente setzen diese
  Saetze, mit ein paar Wochen Vorlauf. Wer auf ein neues Programm warten
  muss, um eine Aenderung einzutragen, stellt bis dahin jede Rechnung mit dem
  falschen Satz aus. `data/EU-Steuersaetze.csv` ist jetzt der
  Anfangsbestand; gepflegt wird in der Datenbank.
- **Ein geaenderter Satz ist eine neue Zeile, nie eine Aenderung.** Der
  Schluessel ist (Land, Art, gueltig ab). Der bisherige Satz behaelt seinen
  Zeitraum und wird am Vortag geschlossen, so dass jeder Tag genau einen Satz
  hat. Wuerde man ihn ueberschreiben, rechnete eine laengst abgegebene
  Meldung ploetzlich anders - und nichts auf dem Bildschirm wuerde zeigen,
  dass sie es tut. Deshalb hat der Dialog auch kein "Satz bearbeiten": es
  gibt nur Hinzufuegen und, fuer einen Tippfehler, Loeschen.
- **Was eingereicht ist, bleibt nachrechenbar.** Ein Satz, dessen Beginn in
  einen Zeitraum faellt, fuer den bereits eine Meldung abgegeben wurde, wird
  abgelehnt und die Ablehnung nennt die Meldung samt Transferticket. Dasselbe
  gilt fuers Loeschen. Zu korrigieren ist so etwas ueber eine berichtigte
  Meldung, nicht ueber die Stammdaten.
- **Loeschen macht den Vorgaenger wieder auf.** Sonst haette das Land ab dem
  Tag, an dem der geloeschte Satz begann, gar keinen mehr - ein stiller
  Ausfall, der erst bei der naechsten Meldung auffiele.
- **Uebernehmen ueberschreibt nichts.** Wer einen Satz von Hand geprueft hat,
  verliert das nicht dadurch, dass die mitgelieferte Datei noch einmal
  eingelesen wird.
- **Ohne Quelle keine Pruefung.** Der Dialog macht die Quelle zu dem, was den
  Satz ueberhaupt verwendbar macht: ohne sie wird er gespeichert, aber nicht
  zum Vergleich herangezogen - und die Fusszeile sagt bei jedem Aufbau, wie
  viele der angezeigten Saetze das betrifft.
- **Der Satz wird ziffernweise gelesen, nicht ueber ein double.** 8,1 % ist
  als Gleitkommazahl nicht darstellbar, und ein Steuersatz, der ein
  Zehntausendstel danebenliegt, ist eine Rundungsdifferenz in jeder Rechnung,
  die ihn verwendet.
- **Die OSS-Meldung liest jetzt die Tabelle.** Die CSV bleibt Rueckfall fuer
  eine Datenbank, in die nie uebernommen wurde - und sagt dann, dass sie es
  ist. Ein gepflegter Satz, den die Meldung zugunsten der Auslieferungsdatei
  ignoriert, waere schlimmer als gar kein Editor.

#### 2026-09-20 *0.11.0*
- **Mehrbenutzerbetrieb: derselbe Bestand auf einem Server.**
  `UltraCanvas/core/UltraDatabase/UltraDatabasePostgresDriver.cpp` und
  `...PostgresSql.cpp`, `Store::OpenServer()`, die Acceptance-Tests in
  `Tests/UltraFIBU/UltraFIBUServerTests.cpp`. Phase A8. Lokal bleibt SQLite,
  gemeinsam ist es PostgreSQL - dieselben Migrationen, dasselbe Schema,
  derselbe Code.
- **Zwei Benutzer haben dieselbe Rechnungsnummer bekommen.** Das ist keine
  Anekdote, sondern der Befund des ersten Laufs mit zwei echten Prozessen:
  80 vergebene Nummern, davon 40 verschiedene. Der Zaehler wurde gelesen und
  zurueckgeschrieben - unter SQLite sicher, weil `BEGIN IMMEDIATE` Schreiber
  serialisiert, unter PostgreSQL bei READ COMMITTED nicht. Der Treiber
  liefert jetzt `FOR UPDATE` als Zeilensperre, SQLite liefert dafuer nichts,
  und jede Nummernvergabe liest gesperrt.
- **Den Fehler hat die Verdopplung ueberlebt.** Die Sperre lag beim ersten
  Anlauf nur in den In-Transaktions-Helfern, waehrend `NextBelegnummer` und
  `NextSequenceValue` zweite Kopien derselben Logik ohne Sperre waren - der
  Test blieb rot, obwohl "der Fehler behoben" war. Es gibt jetzt genau eine
  Stelle, die eine Nummer vergibt.
- **Der Test forkt, weil nichts anderes das beweist.** Zwei Prozesse, je 40
  Nummern, aus einem Nummernkreis; geprueft wird auf eindeutig **und**
  lueckenlos, denn eine fehlende Nummer ist das, wonach eine Pruefung fragt.
  Die einbenutzige SQLite-Suite war die ganze Zeit gruen.
- **Ein uebersprungener Test ist kein bestandener Test.** Ohne konfigurierten
  Server meldet die Suite SKIP und ist damit fertig - in CI setzt
  `ULTRAFIBU_TEST_PG_REQUIRED=1` den Sprung auf Fehler, und der Workflow
  prueft ausserdem, dass `UltraDatabase` ueberhaupt mit PostgreSQL gebaut
  wurde. Ohne libpq baut es stillschweigend ohne den Treiber, und genau
  dieser Test faellt dann weg.
- **Kein Passwort in einer Konfigurationsdatei.** `OpenServer()` verlangt fuer
  die Zugangsdaten ein `vault:`-Praefix und der Treiber weist ein
  literales Passwort ab; der aufgeloeste Wert wird nach dem Verbinden
  ueberschrieben.
- **`?` wird zu `$1` - aber nur, wo es ein Platzhalter ist.** Ein
  Fragezeichen in einem Literal, einem Bezeichner, einem Kommentar oder einem
  Dollar-Quoting ist Text. Der Umschreiber steht in einer eigenen
  Uebersetzungseinheit ohne libpq, damit er auch dort gebaut und geprueft
  wird, wo kein PostgreSQL installiert ist: er verfaelscht eine Abfrage
  lautlos, und eine verfaelschte Abfrage laeuft trotzdem.

#### 2026-09-20 *0.10.0*
- **One-Stop-Shop: die Quartalsmeldung fuer Steuer, die anderen
  Mitgliedstaaten zusteht.** `Apps/UltraFIBU/engine/UltraFIBUOss.{h,cpp}`,
  `data/EU-Steuersaetze.csv`, die Befehle `ultrafibu oss` und
  `ultrafibu lieferschwelle`, 58 weitere Pruefungen (1187 insgesamt).
  Phase A6; IOSS ist dasselbe mit monatlichem Zeitraum und laeuft ueber
  dieselben Typen statt ueber eine Kopie.
- **Es gibt keine Maschinenschnittstelle.** Das BZSt nimmt OSS-Meldungen
  ueber Mein BOP entgegen, und der einzige Massenweg dorthin ist eine
  CSV-Transportdatei, die von Hand hochgeladen wird. Die ehrliche Form ist
  deshalb: rechnen, erzeugen, uebergeben - und bei den ersten beiden genau
  sein, weil danach nichts mehr prueft.
- **Gemeldet wird der Satz, der berechnet wurde - auch wenn er falsch war.**
  Der Satz steht im Steuerschluessel, mit dem die Rechnung gebucht wurde;
  `data/EU-Steuersaetze.csv` ist eine **Pruefung dagegen**, nie ein Ersatz.
  Wuerde die Meldung stillschweigend einen anderen Betrag ausweisen als die
  Rechnung, staenden Buch, Rechnung und Meldung an drei verschiedenen
  Stellen. Weicht der berechnete Satz vom Satz des Ziellandes ab, wird das
  gemeldet: zu korrigieren ist die Rechnung.
- **Ein ungeprueffter Satz wird nicht zum Vergleich herangezogen.** Keiner
  der 26 Saetze in der mitgelieferten Datei steht auf "ja" - sie konnten
  hier an keiner amtlichen Quelle geprueft werden. Ein geratener Satz, der
  eine richtige Rechnung als falsch meldet, wuerde dazu erziehen, die
  Warnung zu ignorieren, und waere an dem Tag wertlos, an dem sie stimmt.
- **Ohne Zielland keine Meldung.** Ein OSS-Steuerschluessel ohne Land haelt
  die Meldung an: "Steuer, die irgendwo in der EU geschuldet wird" ist keine
  Abgabe, und ein geratenes Land schickt das Geld eines anderen Staates an
  die falsche Stelle. Je Zielland ein eigener Schluessel, z. B. `OSS-AT-20`.
- **Die Meldung prueft sich gegen die UStVA.** OSS-Umsatz gehoert in
  Kennzahl 45 - nur Bemessungsgrundlage, keine Steuer, keine Wirkung auf die
  Zahllast. Beide Zahlen kommen aus demselben Journal auf verschiedenen
  Wegen; gehen sie auseinander, ist eine der beiden Meldungen falsch, und
  das gehoert vor die Abgabe. Beim Ausprobieren hat genau diese Pruefung
  sofort angeschlagen, weil ein neu angelegter Landesschluessel die
  Kennzahl 45 noch nicht trug.
- **Die Lieferschwelle wird beobachtet, bevor sie reisst** (§ 3c UStG,
  10.000 EUR EU-weit). Ab 80 % kommt die Warnung, und beim Ueberschreiten
  nennt sie **den Tag und die Rechnung**: ab dieser Rechnung ist im Zielland
  zu versteuern, sofort und nicht ab dem naechsten Quartal. Was die Zaehlung
  nicht sieht - EU-Privatverkaeufe, die noch auf einem Inlandsschluessel
  gebucht sind - steht dabei, statt verschwiegen zu werden.
- **Der Spaltenaufbau der BOP-Datei ist nicht veroeffentlicht.** Das BZSt
  bietet die Importfunktion an, aber nicht ihre Spezifikation. Die erzeugte
  Datei traegt diesen Hinweis in sich selbst - dieselbe Haltung wie bei der
  DATEV-Spaltendefinition. Die Zahlen darin stammen unmittelbar aus dem
  Journal; zu pruefen ist die Anordnung, an einer echten Exportdatei.
- Ein Storno zieht auch hier ab, und Inlandsumsatz bleibt draussen.

#### 2026-09-20 *0.9.0*
- **Belege als PDF hochladen - per Knopf oder per Drag & Drop, mehrere auf
  einmal.** `Apps/UltraFIBU/engine/UltraFIBUBelegArchiv.{h,cpp}`,
  `Store::ImportiereBelegDateien()`, der Befehl `ultrafibu beleg-import`,
  der Knopf **"Beleg hochladen"** und ein Drop-Ziel auf dem ganzen Fenster.
- **Die Dateien werden hineinkopiert, nicht verwiesen.** Bisher merkte sich
  der Beleg einen **Pfad** dorthin, wo die Datei gerade lag. Das reicht fuer
  einen Link und fuer sonst nichts: Ordner verschoben, Downloads geleert,
  Rechner gewechselt - und der Beleg zu einer zehn Jahre alten Buchung ist
  weg. § 147 AO verlangt zehn Jahre Aufbewahrung, also muss die Datei ins
  Archiv.
- **Das Archiv ist inhaltsadressiert.** Jede Datei liegt unter ihrem eigenen
  SHA-256 (`<datenbank>-belege/<jahr>/<hash>.pdf`), und zwar unter dem Jahr
  des Belegs, nicht dem von heute. Damit ist derselbe Beleg zweimal genau
  eine Datei - und zweimal denselben Ordner hineinzuziehen ist die normale
  Art, einen Import-Knopf zu benutzen. Nach dem Schreiben wird die Kopie
  erneut gehasht: eine von einer vollen Platte abgeschnittene Datei ist genau
  der Fehler, den ein Archiv verhindern soll.
- **Derselbe Beleg zweimal ist ein Beleg** - erkannt am Hash, nicht am
  Dateinamen. Ein zweiter Entwurf zu einer bereits abgelegten Datei ist der
  Weg, auf dem eine doppelte Ausgabe ins Hauptbuch kommt.
- **Ein PDF wird an seinen Bytes erkannt, nicht an der Endung.** `.pdf` auf
  einem JPEG macht ein Telefon beilaeufig. Ein verschluesseltes PDF wird
  abgelegt, aber gemeldet: ohne Passwort ist es in zehn Jahren nicht lesbar,
  und dann wird es gebraucht.
- **Aus dem PDF wird nichts ausgelesen, und das steht auch so da.** Je Datei
  entsteht ein **Entwurf**; Betrag, Konto und Steuerschluessel fehlen noch.
  Erfundene Zahlen in einem Hauptbuch waeren schlimmer als gar keine.
- **Ein Entwurf darf leer sein, ein gebuchter Beleg nicht.** `SaveBeleg` wies
  bisher jeden Beleg ohne Positionen ab, womit sich ein empfangenes PDF erst
  ablegen liess, nachdem jemand es gelesen und die Betraege getippt hatte.
  Die Regel, auf die es ankommt, steht dort, wo sie hingehoert: `Buchen()`
  weist einen Beleg ohne Positionen weiterhin ab, ein leerer Entwurf kann
  also nie zu einer Buchung werden. Der Test prueft nicht nur **dass**,
  sondern **warum** abgewiesen wird - eine Ablehnung aus einem anderen Grund
  haette einen schwaecheren Test bestehen lassen.
- **Die Belegliste zeigt den Dateinamen**, solange kein Partner feststeht
  (Spalte "Partner / Beleg"). Ohne ihn sind frisch hochgeladene Belege eine
  Spalte identischer Zeilen, und der Import waere nutzlos.
- **`einrichten` legt jetzt den Nummernkreis `eingang` an** (`E-{JJJJ}`).
  Eine Lieferantenrechnung traegt die Nummer des Lieferanten; dies ist die
  eigene, und sie mit den Ausgangsrechnungsnummern zu mischen macht beide
  wertlos.
- Knopf und Drop-Ziel benutzen die Bordmittel des Frameworks
  (`UltraCanvasFileLoader::OpenMultipleFilesDialog`,
  `InstallEventFilter` auf `UCEventType::Drop`) - nichts davon ist
  nachgebaut. 58 weitere Pruefungen (1129 insgesamt).

#### 2026-09-20 *0.8.0*
- **Umsatzsteuer-Voranmeldung: berechnen, pruefen, als ELSTER-XML abgeben.**
  `Apps/UltraFIBU/engine/UltraFIBUUstva.{h,cpp}`, Schema v5 (`meldung`),
  `data/UStVA-Kennzahlen-2026.csv`, die Befehle `ustva`, `ustva-xml`,
  `meldungen` und `meldung-quittung`. Die Abgabe-Haelfte von Phase A5.
- **Die Kennzahlen-Zuordnung ist eine Datendatei je Jahr.** Das BMF gibt den
  Vordruck jaehrlich neu heraus - 2026 ist die Kennzahl 43 dazugekommen -,
  also waere eine Zuordnung in C++ jedes Jahr ein neues Programm. Die Datei
  sagt ausserdem je Kennzahl, ob sie **geprueft** ist.
- **Ein Betrag, der nirgendwohin gehoert, haelt die Meldung an.** Traegt eine
  Buchung einen Steuerschluessel ohne Kennzahl - oder mit einer, die noch
  nicht am BMF-Vordruckmuster geprueft ist -, dann wird die Datei **nicht**
  geschrieben. Eine Voranmeldung, die diesen Umsatz weglaesst, meldet zu wenig
  und sieht dabei voellig in Ordnung aus. Das ist der schlimmste Ausgang, und
  deshalb ist Verweigern hier billiger als Weitermachen. Die Meldung nennt den
  Schluessel und den Betrag, der gefehlt haette.
- **Die Meldung prueft sich gegen die Buecher.** Zu jeder Kennzahl mit
  Bemessungsgrundlage wird die Steuer aus dem Satz nachgerechnet und gegen die
  tatsaechlich gebuchte gehalten. Ein, zwei Cent sind Rundung; mehr heisst,
  dass in den Buechern ein anderer Satz steht als im Formular - und dass die
  beiden auseinanderlaufen, darf nicht das Finanzamt zuerst merken.
- **Ein Storno zieht ab.** Eine Rueckbuchung liegt auf der anderen Seite und
  mindert den Umsatz; addierte sie, wuerde ein korrigierter Monat den Umsatz
  doppelt melden. Bei der Vorsteuer ist die "normale" Seite die andere, weil
  eine Eingangsrechnung anders herum bucht - beides ist geprueft.
- **Jede Zahl nennt ihre Buchungen.** Die Frage, woher ein Betrag kommt, wird
  Monate spaeter gestellt; `ustva --details` beantwortet sie aus der Meldung
  selbst statt aus einer Nachrechnung.
- **Eine Testuebermittlung sagt, dass sie eine ist.** Ohne Testmerker wird eine
  Probe echt abgegeben, mit Testmerker kommt eine echte Abgabe nie an - beides
  faellt hinterher nicht auf. Deshalb ist der Testmerker die Vorgabe und die
  echte Abgabe braucht `--echtfall`.
- **Was abgegeben wurde, bleibt nachweisbar.** Schema v5 haelt die Kennzahlen
  **wie abgegeben** (nicht als Verweis ins Journal, das sich weiterbewegt),
  den SHA-256 der geschriebenen Datei und das Transferticket. Eine bereits
  eingereichte Meldung wird nicht ueberschrieben: eine Aenderung ist eine
  berichtigte Meldung, dieselbe Regel wie beim Storno.
- **Zwei Transportwege, und der immer funktionierende braucht nichts.**
  `ElsterDateiTransport` schreibt die XML-Datei fuer den Upload in Mein ELSTER.
  **ERiC liegt nicht in diesem Repository und wird es nie**: die
  Ueberlassungsbedingungen erlauben das nicht. `ElsterEricTransport` sucht die
  Bibliothek an einem konfigurierten Pfad und sagt genau, was fehlt - und
  ruft ERiC bewusst noch nicht auf, weil dessen C-Schnittstelle hier nur aus
  Sekundaerquellen bekannt ist. Eine geratene Schnittstelle wuerde vor einer
  Finanzbehoerde scheitern.
- **Der ELSTER-Rahmen sagt in sich selbst, dass er ungeprueft ist.** Die
  amtlichen Schemata liegen im ERiC-SDK, das hier nicht vorliegt; die
  geschriebene Datei traegt diesen Hinweis als Kommentar. Die Zahlen darin
  stammen unmittelbar aus dem Journal und sind nachvollziehbar.
- **Betragsformate nach Formular:** Bemessungsgrundlagen in vollen Euro und
  **abgeschnitten**, nicht gerundet - Aufrunden wuerde Umsatz melden, den es
  nicht gab; Steuerbetraege auf den Cent mit Punkt als Dezimaltrenner.
- **Die Anzeige nennt die Kennzahlen so wie das Formular**, damit die Liste
  Zeile fuer Zeile neben dem Papiervordruck gelesen werden kann, und
  `ustva --details` nennt je Kennzahl die Buchungen dahinter.
- 74 weitere Pruefungen (1071 insgesamt).
- **`einrichten` kennt jetzt `--finanzamt-nr`.** Ohne die Finanzamtsnummer
  weiss ELSTER nicht, wohin die Anmeldung geht.

#### 2026-09-19 *0.7.0*
- **Bankimport: Kontoauszüge lesen und Zahlungen zuordnen.**
  `Apps/UltraFIBU/engine/UltraFIBUBank.{h,cpp}`, Schema v4 (`bankkonto`,
  `bankumsatz`, `zuordnung`, `bank_import`), die Befehle `bankkonto-neu`,
  `bankkonten`, `bank-import`, `bank-importe`, `umsaetze` und `zuordnen`,
  164 weitere Prüfungen (997 insgesamt). Die Importhälfte von Phase A4.
- **Drei Leser, eine Form.** **CAMT.053** (ISO 20022, XML) ist die richtige
  Datei - die deutschen Banken haben MT940 im November 2025 abgelöst. **MT940**
  bleibt für das Archiv: das Format hört an dem Tag auf, neu zu entstehen,
  aber die Jahre davor liegen darin. **CSV** für Banken, die nichts anderes
  anbieten; dessen Spaltenzuordnung ist eine **Datendatei**
  (`data/Bankprofil-Standard.csv`), dieselbe Entscheidung wie beim
  DATEV-Format und aus demselben Grund. Welcher Leser drankommt, entscheidet
  der **Inhalt** der Datei, nicht ihre Endung.
- **Vier Eigenschaften der Formate sind eingebaut, nicht später entdeckt** -
  jede ergibt sonst einen plausibel aussehenden, falschen Kontostand:
  - **Der Betrag ist vorzeichenlos, die Richtung ein eigenes Feld**
    (`CdtDbtInd` CRDT/DBIT bei CAMT, C/D bei MT940). Das Vorzeichen wird
    genau einmal gesetzt, beim Lesen. MT940 kennt zusätzlich **RC/RD**: eine
    Rücklastschrift läuft andersherum als das C, das in ihr steht.
  - **CAMT rechnet mit Punkt, MT940 mit Komma**, und keines von beiden geht
    die Locale des Prozesses etwas an.
  - **Die Gegenseite wechselt mit der Richtung.** Bei Geldeingang ist sie der
    *Debtor*, bei Geldausgang der *Creditor*. Ein Leser, der immer denselben
    nimmt, schreibt auf der halben Datei **uns selbst** als Zahlungsempfänger
    - und diese Hälfte ist still falsch, weil die Beträge weiter aufgehen.
  - **Ein Auszug prüft sich selbst.** Er bringt Anfangssaldo, Endsaldo und
    alle Buchungen mit, und `Anfangssaldo + Buchungen = Endsaldo` muss
    aufgehen. Das ist die eine Prüfung, die eine verlorene Buchung, eine
    doppelte Buchung und ein gedrehtes Vorzeichen auf einmal fängt. Geht ein
    Auszug nicht auf, wird er **nicht** eingelesen.
- **Dieselbe Datei zweimal ändert nichts**, und zwar **je Zeile** statt je
  Datei. Der überlappende Download ist der Normalfall - wer wöchentlich "die
  letzten 30 Tage" herunterlädt, liefert dieselben Zeilen viermal ab. Eine
  Prüfung auf Dateiebene müsste den ganzen Download ablehnen oder drei Wochen
  verdoppeln. Der Schlüssel ist die Bankreferenz (`AcctSvcrRef`), und wo die
  Datei keine mitbringt, ein abgeleiteter - **einschließlich der Position im
  Auszug**, weil zwei identische Zeilen an einem Tag möglich sind und ein
  Schlüssel, der sie nicht unterscheiden kann, eine Zahlung spurlos
  verschluckt.
- **Vorgemerkte Buchungen (`PDNG`) werden nicht übernommen.** Sie haben das
  Konto noch nicht berührt und können sich noch ändern oder verschwinden;
  eine übernommene und später von der Bank fallengelassene Buchung ist eine
  Differenz, die hinterher niemand erklären kann.
- **SEPA-Tags werden ausgepackt.** Deutsche Banken pressen mehrere Felder in
  eine Zeile (`EREF+… MREF+… SVWZ+…`); der von Hand geschriebene Teil ist
  der nach `SVWZ+`. Mehrere `<Ustrd>` gehören zu **einem** Verwendungszweck,
  der bei 140 Zeichen geteilt wurde, und bei MT940 gilt dasselbe für `?20`
  bis `?29` und für den in `?32`/`?33` zerlegten Namen.
- **Die automatische Zuordnung schlägt vor und bucht nichts.** Ein falscher
  automatischer Beleg in einem festgeschriebenen Zeitraum lässt sich nur
  durch Storno beheben, also ist eine Bestätigung billiger als eine
  Selbstsicherheit. **Sicher** heißt: die Belegnummer steht im
  Verwendungszweck **und** der Betrag stimmt - der Betrag allein ist es
  nicht, weil zwei Rechnungen gleich viel kosten können.
- **Die Richtung ist Voraussetzung, keine Punktzahl.** Eingehendes Geld kann
  keine Eingangsrechnung bezahlen. Dabei fragt der Abgleich, ob der Ausgleich
  Geld **abfließen** lässt, und nicht, ob wir den Beleg ausgestellt haben:
  eine **Ausgangsgutschrift** ist beides zugleich - unser Beleg und Geld, das
  hinausgeht (`GeldAbgangBeimAusgleich()`).
- **Eine zu kurze Belegnummer gilt nicht als Fund.** Nummern werden auf
  Buchstaben und Ziffern reduziert und in Großschreibung verglichen, damit
  Schreibweise und Leerzeichen des Zahlenden nichts ausmachen - aber eine
  Nummer mit weniger als vier Zeichen wird gar nicht erst gesucht: "1" steht
  in fast jedem Verwendungszweck und würde einen Beleg auf alles passen
  lassen.
- **Bestätigen bucht über `ZahlungErfassen`**, den Weg, der Überzahlung,
  festgeschriebene Zeiträume, den Belegstatus und die Prüfsummenkette schon
  kennt. Eine Zuordnung ist ein *Grund*, eine Zahlung zu buchen, und kein
  zweiter Weg, sie zu buchen - zwei Wege driften auseinander, und der
  ungetestete gewinnt.
- **`partner-neu` kennt jetzt `--iban` und `--bic`.** Ohne sie war die
  IBAN-Regel des Abgleichs nicht benutzbar, und die Option wurde vorher
  stillschweigend verworfen.
- **Ansehen schreibt nicht.** `bank-import` ist ohne `--uebernehmen` ein
  Trockenlauf und zeigt die ersten Zeilen so, wie das Profil sie liest - bei
  CSV ist genau das die Prüfung der Spaltenzuordnung.

#### 2026-09-19 *0.6.0*
- **DATEV-Import: der Buchungsstapel zurück ins Hauptbuch.**
  `LeseBuchungsstapel()` in `Apps/UltraFIBU/engine/UltraFIBUDatev.{h,cpp}`,
  Schema v3 mit `Store::ImportiereDatevStapel()`, `ultrafibu datev-import` und
  `ultrafibu datev-importe`, 126 weitere Prüfungen (833 insgesamt).
- **Der Import hängt nicht an der geratenen Spaltenreihenfolge.** Eine echte
  DATEV-Datei benennt ihre Spalten selbst, und gelesen wird über diese Namen.
  Die Unsicherheit, die über `data/DATEV-Buchungsstapel-v700.csv` und dem
  Export steht, gilt hier also gar nicht.
- **Das Jahr hinter TTMM kommt aus dem Zeitraum der Datei.** Beide in Frage
  kommenden Jahre werden probiert, und das genommen, das im Zeitraum liegt; ein
  Datum, das in keines passt, wird gemeldet statt geraten. Ein Stapel ohne
  Zeitraum in der Kopfzeile wird abgelehnt, weil TTMM ohne ihn nicht auflösbar
  ist.
- **Zwei Regeln schützen das Hauptbuch, und beide sind gegen eine echte
  Datenbank geprüft:**
  - **Entweder ganz oder gar nicht.** Geschäftsjahr, Abschluss und
    Festschreibung werden für *alle* Zeilen geprüft, bevor eine einzige
    geschrieben wird. Der Test legt die gute Zeile absichtlich vor die
    gesperrte: ein Import, der beim Schreiben prüft, hätte die erste längst
    übernommen und ein halb gefülltes Hauptbuch hinterlassen.
  - **Dieselbe Datei nicht zweimal.** Der Stapel wird über seinen SHA-256
    erkannt; ein zweiter Import verdoppelt einen Monat und fällt nur als
    falscher Saldo auf. `--nochmal` gibt es trotzdem, für den einen echten Fall:
    ein rückgängig gemachter Import, der wiederholt werden muss.
- **Importierte Buchungen sind gewöhnliche Buchungen.** Sie laufen über denselben
  append-only-Pfad, reihen sich in die Prüfsummenkette ein und werden von
  `ultrafibu pruefen` mit abgedeckt. Festgeschrieben werden sie hier und nicht
  durch ein Kennzeichen in einer fremden Datei.
- **Der BU-Schlüssel wird zurückübersetzt, wo die Zuordnung existiert - und
  nicht erfunden, wo sie fehlt.** Aus `data/Steuerschluessel.csv`, Spalte
  `datev_bu`. Fehlt sie, wird der Betrag ungeteilt übernommen, der
  BU-Schlüssel bleibt wörtlich erhalten und die Warnung **nennt die
  Schlüssel**, um die es geht - die Zuordnung lässt sich nachtragen und die
  Datei erneut einlesen.
- **Warum ein Erlöskonto nach dem Import brutto dasteht, steht im Bericht.**
  `mitSteuer` zählt die Buchungen mit Steueraufteilung; ist es null, sagt der
  Import das ausdrücklich und nennt die noch leere Spalte `datev_bu` als
  wahrscheinliche Ursache. Ohne diesen Satz ist ein Bruttobetrag auf 8400 ein
  Rätsel, das lange dauert.
- **Sachkonten aus der Datei, die der Kontenrahmen nicht hat**, werden vor dem
  Schreiben genannt (`UnbekannteSachkonten()`). Personenkonten bleiben dabei
  außen vor: ein Debitor gehört zu einem Partner und steht nie im
  Kontenrahmen, ihn zu melden würde den einen Fall zudecken, der zählt - ein
  falsch getipptes Sachkonto, das hinterher nur als Nummer ohne Bezeichnung in
  der Saldenliste auftaucht.
- **Ansehen schreibt nicht.** `ultrafibu datev-import` ist ohne
  `--uebernehmen` ein Trockenlauf: erst der Bericht über eine fremde Datei,
  dann die Entscheidung. `ultrafibu datev-importe` zeigt, was wann von wem
  eingelesen wurde.
- **Der Rundlauf ist jetzt ein Test, keine Absicht.** Ein Stapel wird
  geschrieben, wieder eingelesen und Buchung für Buchung verglichen - Datum,
  Betrag, Soll/Haben, Konten, Buchungstext, Steueraufteilung. Der Vorschlag
  nennt das den einen Test, der Vorzeichen-, Soll/Haben-, Komma- und
  TTMM-Fehler auf einmal fängt, und er hat recht: jeder davon ergibt eine
  völlig plausibel aussehende Datei. Ein absichtlich vertauschtes
  Soll/Haben-Kennzeichen im Export wird von genau diesem Test gemeldet.
- **Unlesbare Zeilen werden gemeldet, nicht stillschweigend übergangen** - jede
  mit ihrer Zeilennummer, und zusammengefasst als Warnung, damit die Zahl unter
  den übernommenen Buchungen nicht als vollständig gelesen wird.

#### 2026-09-19 *0.5.0*
- **DATEV-Export: der Buchungsstapel.** `Apps/UltraFIBU/engine/UltraFIBUDatev.{h,cpp}`,
  `ultrafibu datev-export` und `ultrafibu datev-pruefen`, 49 weitere Prüfungen
  (707 insgesamt). Phase A3 des Vorschlags, Kategorie 21 (Buchungsstapel) und
  20 (Kontenbeschriftungen).
- **Die Spaltenreihenfolge ist eine Datendatei, kein Quelltext**
  (`data/DATEV-Buchungsstapel-v700.csv`). DATEV gibt rund 120 Spalten in fester
  Reihenfolge vor, und **diese Datei ist noch nicht an einer echten DATEV-Datei
  geprüft** - sie sagt das oben in sich selbst. Der Export schreibt jeden Wert
  **über den Spaltennamen**, nicht über die Position, also wandern die Werte
  beim Korrigieren mit und es braucht keinen neuen Build.
  - **`ultrafibu datev-pruefen <echte_datei.csv>`** liest die Kopf- und die
    Spaltenzeile einer echten DATEV-Datei und meldet jede Abweichung mit ihrer
    Position. Damit hört die Reihenfolge in dem Moment auf, eine Vermutung zu
    sein, in dem eine echte Datei vorliegt - ein Befehl, keine Nachprogrammierung.
    Der Test korrumpiert einen Spaltennamen und prüft, dass genau diese Position
    gemeldet wird.
- **Drei Formatregeln, die eingebaut und nicht später entdeckt sind:**
  - **Belegdatum ist TTMM - vier Stellen, kein Jahr.** DATEV leitet das Jahr aus
    dem Wirtschaftsjahr ab. Deshalb schreibt der Export **eine Datei je
    Kalendermonat** und weist einen Monat zurück, der nicht vollständig im
    Geschäftsjahr liegt. Für ein Geschäftsjahr ab 1. April ist das der
    Unterschied zwischen einem richtigen Export und einem, der Dezember
    stillschweigend in den Januar bucht.
  - **Umsatz ist vorzeichenlos**; die Richtung trägt das
    Soll-/Haben-Kennzeichen. Ein vorzeichenbehafteter Betrag ergibt ein
    plausibel aussehendes, falsches Hauptbuch. Der Test prüft, dass in der
    ganzen Datei kein Minuszeichen steht.
  - **Die Datei ist CP1252 mit CRLF**, nicht UTF-8. Ein Umlaut als UTF-8
    geschrieben kommt in der Kanzlei als zwei falsche Zeichen an und bleibt dort
    zehn Jahre stehen. Derselbe CP1252-Bereich wie beim Euro-Zeichen im
    PDF-Writer; hier wird er direkt getestet, Byte für Byte.
- **Was fehlt, wird gesagt statt weggelassen.** Ohne Berater- und
  Mandantennummer - beide vergibt die Kanzlei - lehnt DATEV den Import ab, also
  lehnt der Export vorher ab und nennt beide. Hat eine Buchung einen
  Steuerschlüssel, aber keinen DATEV-BU-Schlüssel, warnt der Export: die Datei
  importiert, aber DATEV bucht ohne Steuerautomatik. Die Zuordnung steht in
  `data/Steuerschluessel.csv`, Spalte `datev_bu`, und ist dort noch leer.
- **Kategorie 16 (Debitoren/Kreditoren) ist bewusst nicht dabei.** Rund 240
  Spalten, deren Reihenfolge zu raten schlechter wäre als sie nicht anzubieten.
  Sie wartet auf eine echte Datei - dann ist sie eine weitere Datendatei.
- **`ultrafibu einrichten` kennt jetzt Berater- und Mandantennummer**, wie
  zuvor schon die Pflichtangaben nach § 14 UStG.

#### 2026-09-19 *0.4.0*
- **Die Oberfläche: vier Bildschirme über der Engine.** `Apps/UltraFIBU/ui/`,
  target `ultrafibu-ui`. Belege, Journal, Summen und Salden, Partner - each a
  sortable, filterable table over `UltraCanvasListView` and
  `UltraCanvasListSortFilterProxy`, in German, reading a real bookkeeping file.
  This is what phase B1's sorting work was built for.
- **One panel, four screens.** `TabellenPanel` is a search box, a table and a
  summary line; the screens differ only in the columns they declare and the
  store call that fills them. Writing that four times is how the fourth one
  ends up subtly different from the first.
- **The columns sort by value, not by their text.** `TabellenModell` answers
  `ListDataRole::SortRole`, so `1.232,80` sorts after `404,60` instead of
  before it the way those read as text, and `15.06.2026` sorts by its day
  number rather than by its day of month.
  - `ListDataValue` has no `int64` alternative - its choices are string, int,
    float and Color - and an amount in minor units passes 2^31 at
    **21.474.836,47**, a figure a company can genuinely invoice in a year.
    Truncating there would have mis-sorted silently. So the panel installs a
    per-column comparator through the proxy's `SetColumnComparator` seam, which
    reads the exact `int64` from the model; `SortRole` stays as the fallback
    for anything driving the model without one.
- **A row carries its record's id, never its position.** The proxy re-orders
  rows, so a selection is mapped back through `MapToSource` before anything
  acts on it. That is the mistake the proxy's own documentation warns about,
  and a sorted table that opens the wrong invoice is how it looks.
- **Two actions, and only two**: post the selected draft, and print it as a
  PDF. Each is one engine call, and every rule that protects the ledger - the
  draft state, the frozen period, the role - stays in the store, so the button
  can do nothing the CLI could not. Entering a document still belongs to
  `ultrafibu beleg-neu` until the position editor exists.
- **The journal screen verifies the hash chain and the double entry every time
  it loads**, and says so in its summary line. Both are statements about the
  whole ledger, so they are deliberately not recomputed when a filter narrows
  the view - half a ledger has no reason to balance.
- **A bug found by looking at the running window, not by reading the code.**
  With a static summary line, filtering the Belege list to one customer left
  *"4 Beleg(e), offen insgesamt 3.904,60 EUR"* under a single row. A total that
  does not describe what is above it is worse than no total, because somebody
  reads it. The summary is now a function of the visible rows, and says
  "1 Beleg(e) von 4 (gefiltert)" when it is showing a subset. Two column widths
  that clipped `05.08.20…` and `Teilweise bez…` came from the same look.

#### 2026-09-19 *0.3.0*
- **Die Rechnung als PDF.** `Apps/UltraFIBU/report/UltraFIBURechnungPdf.{h,cpp}`,
  the target `UltraFIBUReport`, `ultrafibu rechnung-pdf`, and 32 further checks
  (658 in total). Phase A2's remaining half, minus the screens.
- **There is no PDF writer in it.** The framework already has one -
  `UltraCanvas::VectorConverter::PDFVectorConverter`, which writes a
  self-contained PDF 1.4 from a `VectorStorage::VectorDocument`. This module
  builds that document and hands it over. Writing a second emitter beside the
  existing one would have been the same mistake as building a second data grid
  beside `UltraCanvasListView`, and this time the tree was checked first: both
  sources the writer needs reference nothing outside `VectorStorage`, so an
  invoice can still be produced on a server with no display, no pango and no
  vips. The build compiles those two files directly rather than linking the
  Vector plugin, because the plugin links the UltraCanvas core and would drag
  the whole rendering stack in behind it.
- **§ 14 UStG decides the content, not taste.** `PruefePflichtangaben` checks
  every mandatory field - both addresses in full, the supplier's Steuernummer
  or USt-IdNr., the date of issue, the sequential number, quantity and
  description, the date of supply, the base and rate per tax rate, and the
  recipient's USt-IdNr. when the supply is an intra-community one. What is
  missing is **named**, in German, and `ultrafibu rechnung-pdf` exits non-zero
  for it: an invoice missing these is legally deficient and its recipient
  cannot deduct the input tax from it, which is the customer's problem as much
  as ours.
- **A zero-rated line names its exemption**, which § 14 Abs. 4 Nr. 8 requires:
  innergemeinschaftliche Lieferung, Ausfuhr, § 13b reverse charge, One-Stop-Shop,
  § 19 Kleinunternehmer and nicht steuerbar each have their wording, taken from
  the Steuerschlüssel valid on the Belegdatum rather than from today's table.
- **The VAT summary is per rate**, one line per tax key in the order the
  positions introduced them - so the invoice and the journal read the same way
  round. A Gutschrift says "Gutschrift" and a reversal says "Stornorechnung",
  because a credit note that looks like an invoice is how a customer pays twice.
- **Amounts line up.** Nothing is embedded, so the writer approximates centre
  and right anchoring from an average glyph width - which a money column cannot
  be. Right alignment is therefore computed here from the base-14 Helvetica
  advance widths: every digit is 556/1000 em, which is exactly the property a
  column of amounts depends on, and the test pins it.
- **Three layout faults found by looking at the rendered page**, not by reading
  the code: the Leistungsdatum value printed straight through its own label, the
  ENTWURF mark was drawn across the opening paragraph, and the standard sentence
  about the date of supply sat in a field where it belonged in a note. A meta
  value too wide for its row now takes the next line, the document's state is a
  line under the heading instead of a watermark, and the sentence moved under
  the total where a German invoice puts it.
- **An invoice that does not fit one page is refused**, with the reason. The
  framework's writer emits a single page (proposal §3.2); dropping the last
  positions off the bottom of an invoice is a failure nobody notices until the
  customer pays the wrong amount, and shrinking the type until it fits produces
  something nobody can read. Multi-page output is the writer's to grow.
- **`ultrafibu einrichten` can now record the company's own address, tax
  number, telephone, e-mail, IBAN, BIC and bank** - the fields § 14 asks for,
  which until now could not be entered at all.

#### 2026-09-19 *0.2.0*
- **Belege und Buchungen: the documents and the journal they produce.** Phase
  A2 of `Docs/Research/UltraFIBUDesignProposal.md`, minus the invoice PDF and
  the screens. `UltraFIBUBeleg.{h,cpp}`, `UltraFIBUBuchung.{h,cpp}`, schema
  version 2 (`beleg`, `beleg_position`, `buchung`, `zahlung`), nine new
  commands in `ultrafibu`, and 126 further checks in
  `Tests/UltraFIBU/UltraFIBUEngineTests.cpp` (626 in total).
- **A document produces postings; it is never derived from them.** A `Beleg`
  has positions, a partner, a currency, a file, a status and a payment history;
  a `Buchung` has two accounts and an amount. While a document is a draft it can
  be edited and re-priced freely. `Buchen()` turns it into journal rows, and
  from that moment the store refuses every change to it - a correction is a
  Storno, which is what the GoBD require and what `SaveBeleg` answers with
  rather than leaving it to the UI.
- **The journal row is DATEV-shaped**: `umsatz` (always positive) with a
  Soll-/Haben-Kennzeichen, a `konto`, a `gegenkonto` and a BU-Schlüssel - the
  shape the Buchungsstapel exports and the Kanzlei reads. In that shape the tax
  is not a third row: one account is net, the other gross, and the difference is
  an automatic posting to a tax account nobody types. The row therefore also
  records what that automatic posting was - `netto`, `steuer`, `steuerkonto`,
  `satzPromille` and which side was the net one - because a Saldenliste has to
  show the tax account, because the rate that applied on the Belegdatum is
  history rather than configuration, and because rounding happened once and
  re-deriving it later can differ by a cent.
- **Tax is computed per rate, not per position.** Three lines of 33,33 EUR at
  19 % owe **19,00 EUR**; rounding each line first gives 18,99. The one tax
  figure is then distributed back over the lines by largest remainder, so the
  invoice's tax column adds up to its tax total - the property the recipient's
  own system checks. The test asserts both numbers, so the difference cannot be
  optimised away by accident.
- **Storno is a reversal, never a deletion or a negative amount.** A new
  document with the amounts negated and its own number, postings with Soll and
  Haben exchanged at the *same* positive amounts, both documents and both
  postings pointing at each other, dated into an open period while the original
  may sit in a frozen one. Reversing the same document twice is refused.
  - **A payment already recorded is not reversed with it.** The money arrived;
    reversing the bank leg would make the bank balance disagree with the bank
    statement, which is the one figure in a bookkeeping system that is checked
    against the outside world. What remains is a credit on the person account -
    the customer paid for an invoice that no longer exists and is owed the
    money - which is the true position and the start of a refund. This was
    wrong in the first version of the code and is now pinned by a test that
    asserts the bank is untouched.
- **A hash chain over the journal, and something that checks it.**
  `hash = SHA-256(prevHash ‖ KanonischeForm(buchung))` over a length-prefixed
  canonical form, so a Buchungstext containing a semicolon cannot imitate a
  field separator. `PruefeHashKette` walks the chain from the first row and
  re-computes every link; the tests edit an amount and then delete a row with
  plain SQL - exactly what somebody with the database file would do - and assert
  that both are detected, the first by the hash and the second by the gap in the
  running numbers. It is tamper *evidence*, not a qualified signature, and the
  CLI says so.
- **Festschreibung now reaches the rows.** Freezing a period sets the date on
  the Geschäftsjahr *and* marks every posting and posted document in it, in one
  transaction. Afterwards the store refuses a document dated into the period, a
  posting dated into it, a reversal dated into it and a payment dated into it -
  each with the date it is refusing and why.
- **Payments and the overdue list.** `ZahlungErfassen` posts the money account
  against the person account and carries the document from Offen through
  Teilweise bezahlt to Bezahlt; an over-payment is refused, because the usual
  cause is the same payment entered twice. The due date is derived once from the
  partner's terms and then stored, so changing a customer's terms next year does
  not move when last year's invoices were due. `BelegFilter` covers the
  Rechnungen screen - open, overdue, by kind, by partner, by date range, by
  search text - and takes the cut-off date as a parameter rather than reading
  the clock, so a report is reproducible and a test is not flaky.
- **`SummenUndSalden` expands the automatic tax posting** into the leg it always
  was, which is what makes the list balance, and names a Personenkonto after its
  partner since it is not in the chart of accounts.
  `Buchungskreisdifferenz` is the one-line answer to whether the ledger is still
  a ledger; every test in this area ends with it, and so does `ultrafibu salden`.
- **Nine commands**: `beleg-neu`, `belege`, `buchen`, `storno`, `zahlung`,
  `journal`, `salden`, `pruefen`, beside the existing ones. A whole month can be
  entered, posted, paid, partly reversed, frozen and verified without a window -
  which is what keeps the engine honest about being headless.

#### 2026-09-18 *0.1.0*
- **UltraFIBU exists: the engine a German bookkeeping program is built on, and
  a command line that already uses it.** Phase A1 of
  `Docs/Research/UltraFIBUDesignProposal.md` - master data, the two calendars,
  and a store written for both a local file and a shared server. No UI yet: the
  German screens wait for the framework's data grid (proposal §3.3), and
  everything below is reachable through `ultrafibu` in the meantime.
- **A Geschäftsjahr whose start date is free.** `01.04.2026 - 31.03.2027` is an
  ordinary row: period 1 is April, period 10 is January, and a
  Rumpfwirtschaftsjahr is simply a year of fewer than twelve periods. The store
  refuses an overlapping year and one that leaves a gap, because a posting that
  belongs to two fiscal years - or to none - has no correct report.
- **Two calendars over one journal.** The Umsatzsteuer-Voranmeldung is always a
  calendar month or quarter (§ 18 UStG) whatever the fiscal year is, so
  `Voranmeldungszeitraum` is a separate type: ELSTER period codes (01..12 for
  months, 41..44 for quarters), the deadline on the 10th, a month later with a
  Dauerfristverlängerung, moved off a Saturday or Sunday (§ 108 Abs. 3 AO) and
  deliberately *not* off a public holiday, since those differ by Bundesland.
  OSS quarters and their end-of-following-month deadline sit beside it. For a
  1 April fiscal year the reporting periods span two calendar years, which the
  tests assert rather than hope for.
- **Festschreibung only moves forward.** Freezing a period is irreversible by
  design (GoBD): an attempt to move the date backwards is refused *and*
  recorded in the audit trail, and every correction after that is a Storno.
- **European VAT numbers, checked offline.** Formats for all 27 member states
  plus Northern Ireland's XI, and check digits for the fifteen whose rule is
  unambiguous (DE, AT, BE, DK, EE, EL, FI, FR, HU, IT, LU, PL, PT, SE, SI, SK).
  Where a state relaxed its rule - the Netherlands - or mixes several schemes -
  Spain, Ireland - the answer is "format correct, no check digit computed"
  rather than an invented verdict: a validator that rejects a valid number is
  worse than one that admits what it cannot tell. Every verdict carries a
  German sentence for the UI.
- **VIES and the BZSt, with the transport injected.** `PruefeUstIdNrVies` builds
  the European Commission's REST request (the simple `GET .../ms/{LAND}/vat/{NR}`,
  or the `POST` that carries the enquirer's own number and therefore returns a
  consultation number) and reads the answer; `PruefeUstIdNrBzst` does the same
  for the qualified confirmation under § 18e UStG, the only one that actually
  protects a zero-rated intra-community supply. The response is stored **byte
  for byte**, because the authority's data set *is* the evidence. A member state
  that is not answering (`MS_UNAVAILABLE`) is a failed enquiry and never an
  invalid customer - the tests pin that distinction down, along with a
  non-JSON answer, a partly confirmed qualified enquiry, and the cases that are
  refused before a request is made.
- **Money is never a double**, anywhere: amounts are `UltraCanvasMoney`
  (framework 0.8.92), and a Steuerschlüssel computes its tax through it.
- **The chart of accounts and the tax keys are data, not code.** `data/SKR03.csv`
  ships 74 of the most-used SKR03 accounts - each with its BWA position and its
  balance-sheet classification, because the Jahresabschluss is in scope -
  and `data/Steuerschluessel.csv` 14 tax keys. Only UStVA Kennzahlen that have
  been verified are filled in (81, 86, 87, 41, 66, 45); the rest are
  deliberately empty, and a test fails if an unverified one ever appears. A
  guessed Kennzahl produces a wrong return, an empty one is caught when the
  return is built. Both files are a starting point that the Kanzlei's own
  category-20 export overrides.
- **The store is written for a server from the first commit.** Surrogate keys
  come from a `sequenz` table rather than AUTOINCREMENT, dates are ISO text,
  money is BIGINT minor units, every value is bound and every write is a
  transaction - so moving from SQLite to PostgreSQL is a change of one
  configuration field once UltraDatabase's Stage 2 driver exists. Until then
  `OpenServer` says exactly that instead of silently opening a local file, and
  refuses a literal password outright (credentials belong in UltraVault).
- **What multi-user needs is already here**: document numbers allocated inside
  the transaction that uses them (never `SELECT MAX(...) + 1`, which duplicates
  under two writers) with an optional yearly reset; optimistic locking on
  partner records, so saving a row somebody else changed is refused with a
  German message instead of overwriting their work; users, five roles and a
  permission matrix enforced **in the store**, not only in the UI, because in
  server mode the database is reachable without it; Argon2id passwords through
  UltraCrypt with the parameters stored per user; an audit trail on every write
  and on every refusal; and a startup gate that refuses to open a database
  whose schema is newer than the binary.
- **Person accounts follow the Sachkontenlänge** rather than being hard-coded
  for four digits: customers from 10000 and suppliers from 70000 with a 4-digit
  chart, 100000/700000 with five, and so on.
- **`ultrafibu`** (`cli/main.cpp`): `einrichten` (company, fiscal year, chart,
  tax keys, number ranges in one call), `info`, `konten`, `partner`,
  `partner-neu`, `ustid`, `termine`, `perioden`, `festschreiben` (which asks
  before doing something irreversible) and `protokoll`. German output
  throughout.
- **`Tests/UltraFIBU`**: 493 checks over the calendar, the fiscal year, both
  reporting calendars, the VAT-number rules, the data files, the online
  confirmation readers and the store. The VAT check-digit tests assert the
  *properties* a correct rule must have - exactly one accepted check digit per
  prefix, every single mistyped digit detected - rather than quoting test
  vectors from memory, which would prove nothing if the memory were wrong.
  Enabled with `-DULTRACANVAS_BUILD_ULTRAFIBU_TESTS=ON`; runs headless, without
  the UI library.

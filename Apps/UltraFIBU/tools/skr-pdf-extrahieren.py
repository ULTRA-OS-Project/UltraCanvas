# -*- coding: utf-8 -*-
"""SKR-Kontenrahmen aus dem DATEV-PDF extrahieren.

Das PDF ist ein Poster: je Seite zwei Spaltengruppen, je Gruppe
[Bilanz-/GuV-Posten] [Programmverbindung/Abschlusszweck] [Konto] [Bezeichnung].
Bezeichnungen laufen ueber mehrere Zeilen; Funktionen (AV/AM/KU/...) gelten fuer
Kontenbereiche. Die Spaltengrenzen werden je Seite aus den Positionen der
Kontonummern hergeleitet, nicht fest verdrahtet.
"""
import re, sys, collections

FUNKTIONEN = {"KU","V","M","AV","AM","S","F","R"}
ABSCHLUSS  = {"HB","SB","EÜR"}
PROGRAMM   = {"U","G","K"}

def lies_woerter(xml_pfad):
    xml = open(xml_pfad, encoding="utf-8").read()
    for pg in xml.split("<page ")[1:]:
        yield [(float(a), float(b), float(c), float(d), w)
               for a,b,c,d,w in re.findall(
                   r'<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
                   r'yMax="([\d.]+)">(.*?)</word>', pg)]

def entwirre(text):
    """Worttrennung am Zeilenende zusammenfuehren: 'Zuschussver-' + 'pflichtungen'."""
    # "Zuschussver- pflichtungen" ist eine Worttrennung, "Betriebs- und" ist
    # ein echter Bindestrich vor einem Bindewort. Nur die erste wird gefuegt.
    text = re.sub(r'(\w)-\s+(?!und\b|oder\b|sowie\b|bzw\b|bis\b)(?=[a-zäöüß])',
                  r'\1', text)
    return re.sub(r'\s+', ' ', text).strip()

def konto_anker(worte):
    """Die x-Positionen der beiden Kontonummern-Spalten dieser Seite."""
    xs = sorted(x for x,y,X,Y,w in worte if re.fullmatch(r'\d{4}', w))
    if len(xs) < 8: return None
    mitte = (min(xs) + max(xs)) / 2
    links  = [x for x in xs if x < mitte]
    rechts = [x for x in xs if x >= mitte]
    if not links or not rechts: return None
    # Der Median ist robust gegen die paar Zahlen, die im Fliesstext stehen.
    return (sorted(links)[len(links)//2], sorted(rechts)[len(rechts)//2])

def zeilen(worte, tol=2.5):
    """Woerter zu visuellen Zeilen gruppieren."""
    zs = collections.defaultdict(list)
    for x,y,X,Y,w in worte: zs[round(y/tol)].append((x,w,y))
    return [sorted(zs[k]) for k in sorted(zs)]

def extrahiere(xml_pfad):
    konten = {}          # nummer -> dict
    funktionen = []      # (funktion, von, bis)
    for seite, worte in enumerate(lies_woerter(xml_pfad), 1):
        anker = konto_anker(worte)
        if anker is None: continue
        # Zusatzfunktionen stehen als Block ueber den Kontenklassen, nicht in
        # einer Spalte - also ueber die ganze Seite suchen.
        flach = sorted(worte, key=lambda t: (round(t[1]/2.5), t[0]))
        for i, (x,y,X,Y,w) in enumerate(flach):
            if w in FUNKTIONEN and i+1 < len(flach):
                nx, ny, _, _, nw = flach[i+1]
                if abs(ny-y) > 3 or nx - X > 30: continue
                m = re.fullmatch(r'(\d{4})-(\d{4})', nw)
                if m: funktionen.append((w, m.group(1), m.group(2)))
                elif re.fullmatch(r'(\d{4})-(\d{2})', nw):
                    a, b = nw.split('-'); funktionen.append((w, a, a[:2]+b))
                elif re.fullmatch(r'\d{4}', nw): funktionen.append((w, nw, nw))

        for gi, kx in enumerate(anker):
            # Spaltenbaender relativ zum Kontenanker dieser Gruppe.
            bez_von, bez_bis = kx + 12, kx + 115      # Bezeichnung
            az_von,  az_bis  = kx - 60, kx - 2        # Abschlusszweck/Funktion
            # Der Bilanz-Posten steht irgendwo links davon - SKR03 setzt ihn
            # enger als SKR04. Die Spalte wird deshalb weit gefasst und ueber
            # den INHALT abgegrenzt: Codes sind eine geschlossene Menge,
            # Bilanztext ist freier Text.
            bil_von, bil_bis = kx - 145, kx - 2
            # Die zweite Gruppe darf nicht in die Bezeichnung der ersten
            # hineinreichen - sonst landen deren Wortreste ("und", "%") im
            # Bilanz-Posten.
            if gi > 0: bil_von = max(bil_von, anker[gi-1] + 120)
            gruppe = [(x,y,X,Y,w) for x,y,X,Y,w in worte if bil_von <= x < bez_bis]
            # Ein Bilanz-Posten ist ein Textblock ueber mehreren Konten, nicht
            # eine Angabe je Zeile. Erst die Bloecke bilden (zusammenhaengende
            # y-Laeufe), dann jedem Konto den Block geben, in dessen Spanne es
            # liegt - sonst bekommt jedes Konto nur ein Wortfragment.
            # Das Kopfband jeder Seite traegt die Spaltentitel ("Bilanz-",
            # "Posten2)", "Programmverbindung4)") - die sind keine Gliederung.
            kopf_ende = min((y for x,y,X,Y,w in gruppe
                             if re.fullmatch(r'\d{4}', w) and abs(x-kx) < 8),
                            default=0) - 4
            CODES = FUNKTIONEN | ABSCHLUSS | PROGRAMM
            def ist_code(w):
                # DATEV kombiniert Funktionen: "S/AV" ist Sammelkonto mit
                # automatischer Vorsteuer.
                return all(t in CODES for t in w.split("/")) if "/" in w else w in CODES
            bil_worte = sorted(((y, x, w) for x,y,X,Y,w in gruppe
                                if bil_von <= x < bil_bis and y >= kopf_ende
                                and not ist_code(w)
                                and not re.fullmatch(r'[\d.,)\-]+', w)))
            bloecke, lauf = [], []
            for y, x, w in bil_worte:
                if lauf and y - lauf[-1][0] > 14:
                    bloecke.append(lauf); lauf = []
                lauf.append((y, x, w))
            if lauf: bloecke.append(lauf)
            # Der Posten gilt ab seinem Beginn bis zum naechsten - die Spalte
            # ist eine laufende Gliederung, kein Etikett je Zeile.
            spannen = []
            for i, b in enumerate(bloecke):
                start = b[0][0] - 6
                ende = bloecke[i+1][0][0] - 6 if i+1 < len(bloecke) else 10**6
                spannen.append((start, ende, entwirre(" ".join(w for _,_,w in b))))

            aktuell = None
            offen_az = []
            for zl in zeilen(gruppe):
                nummer = next((w for x,w,y in zl
                               if re.fullmatch(r'\d{4}', w) and abs(x-kx) < 8), None)
                bez = " ".join(w for x,w,y in zl if bez_von <= x < bez_bis)
                az  = [w for x,w,y in zl if az_von <= x < az_bis]
                # "0040" auf einer Zeile, "-42" auf der naechsten: ein
                # Kontenbereich, kein Konto ohne Namen.
                fort = next((w for x,w,y in zl
                             if re.fullmatch(r'-\d{2,4}', w) and abs(x-kx) < 12), None)
                if fort and aktuell is not None and not nummer:
                    a = aktuell["nummer"]; b = fort[1:]
                    aktuell["bis"] = a[:len(a)-len(b)] + b
                    # Diese Zeile traegt ausser "-42" oft auch Text weiter -
                    # sie zu ueberspringen zerreisst die Bezeichnung.
                    if bez: aktuell["bezeichnung"] += " " + bez
                    continue

                # Funktion mit Kontenbereich, z. B. "AM 8400-8409"
                for i, w in enumerate(az):
                    if w in FUNKTIONEN and i+1 < len(az):
                        m = re.fullmatch(r'(\d{4})-(\d{4})', az[i+1])
                        if m: funktionen.append((w, m.group(1), m.group(2)))
                        elif re.fullmatch(r'\d{4}', az[i+1]):
                            funktionen.append((w, az[i+1], az[i+1]))

                if az and aktuell is None:
                    offen_az.extend(az)          # steht vor dem ersten Konto
                if nummer:
                    aktuell = konten.setdefault(nummer, {
                        "nummer": nummer, "bezeichnung": "", "bilanz": "",
                        "abschlusszweck": "", "programm": "", "funktion": "",
                        "seite": seite})
                    aktuell["bezeichnung"] += " " + bez
                    ky = zl[0][2]
                    for a, b, txt in spannen:
                        if a <= ky <= b: aktuell["bilanz"] = txt; break
                    for w in list(offen_az) + az:
                        if "/" in w and all(t in FUNKTIONEN for t in w.split("/")):
                            aktuell["funktion"] = w
                        elif w in ABSCHLUSS: aktuell["abschlusszweck"] = w
                        elif w in PROGRAMM: aktuell["programm"] += w
                        elif w in FUNKTIONEN and not re.search(r'\d', "".join(az)):
                            aktuell["funktion"] = w
                    offen_az = []
                elif aktuell is not None:
                    if bez: aktuell["bezeichnung"] += " " + bez
                    # Ein HB/SB unter dem Konto gehoert noch zu ihm.
                    for w in az:
                        if w in ABSCHLUSS and not aktuell["abschlusszweck"]:
                            aktuell["abschlusszweck"] = w
                        elif w in PROGRAMM and w not in aktuell["programm"]:
                            aktuell["programm"] += w
    # Kontenklassen-Ueberschriften tragen als "Bezeichnung" nur die Klassenziffer
    # oder den Klassentitel; sie sind Gliederung, kein Konto.
    for n in list(konten):
        b = konten[n]["bezeichnung"].strip()
        if re.fullmatch(r'\d(\s.*)?', b) and n.endswith("000"):
            del konten[n]
    for k in konten.values():
        k["bezeichnung"] = entwirre(k["bezeichnung"])
        k["bilanz"] = entwirre(k["bilanz"])
    return konten, funktionen

if __name__ == "__main__":
    konten, funktionen = extrahiere(sys.argv[1])
    print(f"{len(konten)} Konten, {len(funktionen)} Funktions-Bereiche")
    for n in sorted(konten)[:6]:
        k = konten[n]
        print(f"  {n} | {k['bezeichnung'][:52]!r} | AZ={k['abschlusszweck']} F={k['funktion']}")
    print("  Funktionen (Probe):", funktionen[:6])

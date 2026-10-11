# Umsetzungsplan: W2-Modulprotokoll auf der Pico-2-W-Brücke

Stand: 11.10.2026, Entscheidungen E1–E12 getroffen, E3 und E7 überarbeitet. Grundlage ist [PROTOCOL_ANALYSIS_W2_MODULE.md](https://github.com/sbohlma/FireAngel/blob/main/wisafe-mm/docs/PROTOCOL_ANALYSIS_W2_MODULE.md) (im Folgenden „Protokollanalyse“) mit den dort genannten Quellen, vor allem NikNakks [fireangel-pro-connected-esphome](https://github.com/NikNakk/fireangel-pro-connected-esphome), Tho85s [ws2mqtt](https://github.com/Tho85/ws2mqtt) und [wisafe-mm](https://github.com/sbohlma/FireAngel/tree/main/wisafe-mm).

Ziel: Alle Melder im Netz erscheinen in ESPHome und Home Assistant mit Zustand, Fehlern, Batterie und Ereignissen. Diagnose und Netztest stehen als Funktionen bereit.

**Sicherheitsrahmen.** Die zertifizierten Melder bleiben die Sicherheitsanlage. Die Brücke ist ein zusätzlicher Beobachter und darf nie der einzige Alarmweg sein. Auslösende Funktionen sind auf Diagnose und Netztest begrenzt (E1), netzverändernde Befehle bleiben ausgeschlossen (E10).

Kennzeichnung: **[E1]** usw. verweist auf eine Entscheidung in Abschnitt 6, **[R1]** usw. auf ein Risiko in Abschnitt 7.

## 1. Ausgangslage

Funktioniert und ist auf der Hardware bestätigt:

- Kern 1 ist SPI-Slave mit IRQ-Quittung, sendet Befehle aus einer Warteschlange und meldet pro Befehl ein Ergebnis.
- Kern 0 startet nach dem WLAN-Aufbau das SPI und den Init `D3 19 50 00 7E`, erkennt `46 7E` und loggt Rahmen.
- Beobachtet nach dem Init: `D4 10 BF 7F 0E 05 00 00 00 00 14 7E`.

Abweichungen des Ist-Stands von der Protokollanalyse, die Stufe 0 und 1 beheben:

| Thema | Ist-Stand | Protokollanalyse |
| --- | --- | --- |
| Maskierung | keine; `7D 01`/`7D 02` erscheinen roh | `7E` im Inhalt als `7D 01`, `7D` als `7D 02`, in beide Richtungen |
| Füllbytes | `00` vor einem Rahmen landen im Rahmen | `00` vor Rahmen verwerfen |
| `D2` | als „Gerät fehlt“ gedeutet | Status des eigenen RM, kommt auch unaufgefordert alle ~57 min |
| `D4` | pauschal als „Kopplung“ gedeutet | Untertyp in d1: `01`, `03`, `04`, `06`, `09`, `11` |
| `41` | als „Antwort bestätigt“ gedeutet | Identitätsanfrage des RM, erwartet `91` vom Host |
| Modelle | 5 Einträge, `11 04` als FP1720W2-R | 14 Modelle, `11 04` (0x0411) ist WHT-630 |
| Rahmenende | Lücke 80 ms | Arduino verwirft unvollständige Rahmen nach 100 ms |
| Sendetakt | Befehle direkt hintereinander | 500 ms Ruhe nach jedem Rahmen, höchstens eine Fernanfrage offen |

## 2. Zielarchitektur

Kern 1 bleibt ein reiner Byte-Transport und wird nicht erweitert. Die gesamte Protokolllogik liegt auf Kern 0, gegliedert in Schichten, die sich ohne Hardware auf dem PC testen lassen.

| Schicht | Datei (geplant) | Aufgabe |
| --- | --- | --- |
| Rahmen | `framing.h/.cpp` | Bytes zu Rahmen: Füllbytes, Demaskierung, Ende `7E`, Zeitlimit. Beim Senden maskieren und `7E` anhängen |
| Nachrichten | `messages.h/.cpp` | Rahmen zu typisierten Nachrichten und zurück. Ein `struct` je Typ, SID und Sequenznummer von hinten gelesen |
| Sendeplanung | `scheduler.h/.cpp` | Warteschlange mit Prioritäten, 500 ms Ruhe, eine offene Fernanfrage, Zuordnung von Antworten, Zeitlimits |
| Melderregister | `registry.h/.cpp` | Tabelle mit 50 festen Plätzen (Netzvorgabe) mit SID, Gerät, Modell, Klasse und Zuständen, Laden und Speichern von `nodes.bin` |
| Abläufe | `discovery.cpp`, `host_identity.cpp` | Netzerkundung, Antworten auf `41`, `C3`, `72`, `C7`, `C5`, `C1` |
| ESPHome | `wisafe2.cpp`, Plattformdateien | Entitäten, Aktionen, Buttons, Ereignisse |

Die Schichten Rahmen bis Melderregister kommen ohne ESPHome-Header aus. So laufen sie in Host-Tests mit den Beispielrahmen aus der Protokollanalyse.

Die Sendeplanung kennt drei Klassen von Befehlen:

| Klasse | Beispiele | Regel |
| --- | --- | --- |
| Hostantwort | `91` auf `41`, `C4` auf `C3`, `73`/`C8`/`C6`/`C2` | sofort, vor allem anderen |
| Lokal | `D1`, `D3 03` | Antwort nach ~70 ms ohne `46`, Zeitlimit 500 ms |
| Fernanfrage | `D3 06 sid 00/01`, `72`, `C7`, `C5`, `C1`, `70` | `46` binnen 150 ms (bei `47`: SID unbekannt), Antwort asynchron bis ~50 s, nur eine gleichzeitig |

## 3. Implementierungsstufen

Jede Stufe endet mit einem lauffähigen Stand, Host-Tests und einer Prüfung auf der Hardware.

### Stufe 0: Rahmenschicht und Sendeplanung

Ziel: Rahmen werden korrekt erkannt und gesendet, Befehle laufen im richtigen Takt.

- Rahmenschicht mit Demaskierung, Verwerfen führender `00`, Zeitlimit 100 ms und Maskierung beim Senden **[E11]**.
- Typisierte Nachrichten für die lokalen Antworten `46`, `47`, `41`, `D2`, `D4 xx`.
- Sendeplanung mit den drei Klassen aus Abschnitt 2. Der Init läuft als erster Befehl über sie, ohne vorangestellte `00` **[E2]**.
- Der Status-Sensor zeigt den Zustand des Moduls: „Init läuft“, „Modul bereit“, „keine Antwort“, „ungekoppelt“.
- Host-Tests für Rahmen und Sendeplanung, darunter maskierte Rahmen und Antworten, die sich überholen.

Abnahme auf der Hardware:

- Init wie bisher, danach `D1` und `D3 03` per Button ohne verlorene Antwort.
- Mitschnitt nach einem Neustart: Flutet der Init der Pico-Brücke das Netz (`D3 19 50 00 sid` bei anderen Knoten)? Ergebnis wird in E2 nachgetragen.

### Stufe 1: Vollständiges Parsing, Identität und manuelle Abfragen

Ziel: Jede bekannte Nachricht wird verstanden und lesbar geloggt. Die Brücke tritt mit eigener Identität im Netz auf. Jede lesende Abfrage lässt sich von Hand auslösen.

- Decoder für alle Typen der Protokollanalyse: `50`, `51`, `61`, `70`, `71`, `72`, `73`, `C1` bis `C8`, `D2`, `D4 01/03/04/06/09/11`, `D5`. Unbekannte Typen und Untertypen wie `D4 10` als `Roh:` **[E9]**.
- Feldauswertung: Geräte-ID in Anzeigereihenfolge, Modelltabelle mit allen 14 Modellen der Protokollanalyse (`11 04` = WHT-630 **[E12]**), Klassen, Fehlerflags `71`, Fehlerdetails `73` je Modell, Batterie `C8` in mV, RM-Batterie nach U = 1048,576 / (BVNL + 285) V, RSSI.
- Entfernen von Duplikaten über (SID, Typ, Sequenznummer) mit einem Fenster von 60 s.
- Eigenes RM: `D1` und `D3 03` beim Start und auf Abruf. Daraus entstehen Sensoren für eigene SID, Kopplung, RM-Batterie, RSSI, Firmware, gelernte Geräte-ID und Funkfehler.
- Identität **[E3]**: Jedes `41` wird sofort mit `91 dev model FF 05 01 01` beantwortet.
  - Die Geräte-ID steht in der YAML in Anzeigereihenfolge, wie das Gateway sie zeigt. Für dieses Netz ist das `device_id: "14B8A5"`, im Paket `A5 B8 14`.
  - Das Modul am Pico trägt diese ID bereits aus dem Pairing. Ein neues Pairing ist nicht nötig.
  - Nach `D1` vergleicht die Brücke die gelernte ID im Modul (`D2` d6–d8) mit der YAML und meldet eine Abweichung als Warnung im Log und im Status.
- Antworten auf Anfragen an die eigene Geräte-ID **[E4]**, jeweils höchstens einmal je 60 s:

  | Anfrage | Antwort |
  | --- | --- |
  | `C3` | `C4 dev FF model` |
  | `72` | `73 dev model 00 00` (keine Fehler) |
  | `C7` | `C8 dev 06 mv_lo mv_hi 00 00`, fest 3000 mV |
  | `C5` | `C6 dev 74 00 00 00 00` (keine Ereignisse) |
  | `C1` | `C2 dev FF model 01` (bestanden) |

- Manuelle Abfragen als ESPHome-Aktionen, die sich über `api: actions` an Home Assistant durchreichen lassen:
  - `wisafe2.query_remote_status` mit `sid` (`D3 06 sid 00`)
  - `wisafe2.query_identity` mit `sid` (`D3 06 sid 01`)
  - `wisafe2.query_battery`, `wisafe2.query_fault_details`, `wisafe2.query_fault_events` mit SID; Geräte-ID und Modell kommen aus dem Register oder als Parameter (`C7`, `72`, `C5`)
- Die Antworten erscheinen im Log und als Text-Sensor „letzte Antwort“.

Abnahme:

- Testtaste an einem Melder ergibt einen vollständig gedeuteten `70`-Rahmen.
- Melder vom Sockel nehmen ergibt `71` mit Flags.
- Handabfragen liefern `D4 06`, `C4`, `C8`, `73`.
- `D1` zeigt im Feld d6–d8 die ID aus der YAML (`A5 B8 14`), ohne Warnung.
- Eine ID-Anfrage eines anderen Knotens an die Brücke erhält `C4`.

### Stufe 2: Melderregister und Netzerkundung

Ziel: Die Brücke kennt alle Knoten des Netzes und hält deren Zustand aktuell.

Melderregister je SID:

| Feld | Quelle |
| --- | --- |
| SID, im Netz ja/nein | `D3 03`, `D4 04`, `D4 01` |
| Geräte-ID | `D4 06` d8–d10, `D4 09`, `C4`, passive Rahmen |
| Klasse, Modell | `C4`, `70`, `71`, `73` |
| RM-Batterie, RSSI, RM-Firmware, Funkfehler, kritische Bits | `D4 06` |
| Melder-Batterie in mV | `C8` |
| Fehlerflags, Sockel | `71` |
| Fehlerdetails, Fehlerereignisse | `73`, `C6` |
| letzter Test mit Ergebnis | `70` |
| Alarm, Stummschaltung | `50`, `51`, `61` |
| zuletzt gesehen | jede Nachricht mit SID |

Nachrichten mit der Geräte-ID `01 02 03` stammen von Gateway-RMs. Das Register führt sie als Gateway, ohne Melder-Entitäten.

Ablauf einer Netzerkundung:

1. `D3 03`: Liste der SIDs.
2. Je SID `D3 06 sid 00`: Geräte-ID, RM-Batterie, RSSI. Ohne Antwort nach ~45 s kommt `D4 01`.
3. Je SID mit unbekannter Klasse `D3 06 sid 01`: `C4` mit Klasse und Modell. RMs ohne Melder antworten nicht, sie bleiben „Typ unbekannt“.
4. Je Melder `C7` (Batterie) und `72` (Fehlerdetails). `C5` nur bei WST-630 und ST-630-DE.

Dauer: Mit einer offenen Fernanfrage und 4–28 s je Antwort braucht eine vollständige Erkundung von 22 Meldern rund 10–20 min.

Wann erkundet wird **[E5]**, gesteuert über die YAML-Option `auto_discovery`:

| | `auto_discovery: false` (Entwicklung und Test, Vorgabe) | `auto_discovery: true` (später) |
| --- | --- | --- |
| Start | Register aus `nodes.bin` laden, dann Erkundung nur für SIDs ohne Eintrag | wie links |
| Button „Discover Network“ | Erkundung aller SIDs, Register bleibt erhalten | wie links |
| Periodisch | keine Abfragen | `D3 06` je SID alle 19 h, `C7` je Melder alle 24 h, über den Tag verteilt, Intervalle per YAML |
| Ereignisgesteuert | keine Abfragen | nach `71` mit Flag 0x02: `72` und `C5`; nach `D4 04` oder `D4 09` mit unbekannter SID: Erkundung dieser SIDs |

Passive Nachrichten aktualisieren das Register in beiden Betriebsarten sofort: `70`, `71`, `50`, `51`, `61`, `D2`, `D4 04`, `D4 09`, sowie Kopien von `C4`, `73`, `C6`, `C8` nach dem Init.

Erreichbarkeit **[E8]**: Ein Knoten gilt als „nicht erreichbar“ nach `D4 01` oder zwei ausbleibenden Antworten in Folge. Aus dem Register entfernt wird er nur über „Rediscover Network“.

Persistenz **[E7]**:

- Binärdatei `nodes.bin` im LittleFS des Pico mit fester Größe von 408 Byte: Kopf mit 8 Byte und 50 Plätze zu je 8 Byte (Netzvorgabe: höchstens 50 Knoten). Inhaltlich entspricht ein Platz den Identitätsfeldern von wisafe-mm; Namen kommen aus der YAML (E6).

  Kopf:

  | Byte | Inhalt |
  | --- | --- |
  | 0–3 | Kennung `W2NR` |
  | 4 | Formatversion, beginnt bei 1 |
  | 5 | Anzahl belegter Plätze |
  | 6–7 | CRC-16 über die 400 Byte der Plätze |

  Platz:

  | Byte | Inhalt |
  | --- | --- |
  | 0 | SID; `FF` = Platz frei |
  | 1 | Flags: Bit 0 Gateway-RM, Bit 1 Typ unbekannt (RM ohne Melder) |
  | 2–4 | Geräte-ID in Paket-Reihenfolge |
  | 5–6 | Modell-ID in Paket-Reihenfolge |
  | 7 | Klasse |

- Passen Kennung, Version oder CRC nicht, startet die Brücke mit leerem Register und erkundet alle SIDs.
- Geschrieben wird nur bei Änderung von Geräte-ID, Modell oder Klasse, gebündelt mit einer Verzögerung von einigen Sekunden.
- Button „Rediscover Network“: löscht `nodes.bin` und das Register im RAM und startet eine vollständige Erkundung.

Abnahme:

- Nach einer Erkundung stimmt das Register mit der bekannten Melderliste überein (Anzahl, IDs, Modelle).
- Nach einem Neustart ist das Register ohne Erkundung wieder vollständig.
- „Rediscover Network“ baut es neu auf.

### Stufe 3: Melder als Geräte in Home Assistant

Ziel: Jeder Melder erscheint mit eigenen Entitäten über die native API **[E6]**.

- Die Melder werden in der YAML aufgelistet, mit SID oder Geräte-ID und einem Namen.
- Nach einer Erkundung loggt die Brücke für alle gefundenen Melder einen fertigen YAML-Block als Vorlage.
- Zusätzlich gibt ein Text-Sensor das ganze Register als JSON aus. Darin erscheinen auch Melder, die noch nicht in der YAML stehen.

Entitäten je Melder:

| Art | Entität |
| --- | --- |
| binary_sensor | Alarm (Feuer/CO), Fehler, Sockel-Problem, Melder-Batterie schwach, RM-Batterie schwach, im Netz |
| sensor | Melder-Batterie (mV), RM-Batterie (V), Empfangspegel, zuletzt gesehen |
| text_sensor | Modell, Fehlerdetails, letzter Test mit Ergebnis |
| button | Status aktualisieren, Diagnose (Stufe 4) |

Netzweite Entitäten:

- Alarm aktiv im Netz
- Stummschaltung aktiv
- letztes Ereignis
- Anzahl Knoten
- Erkundung läuft
- Status des eigenen RM
- Register (JSON)
- Buttons „Discover Network“ und „Rediscover Network“

Home-Assistant-Ereignisse für Alarm, Alarmende, Stummschaltung, Test und Fehlerbericht, damit Automationen darauf reagieren können.

Regeln für den Alarmzustand (übernommen aus NikNakk und der Protokollanalyse):

- Er gilt nach dem Start als unbekannt.
- Er wechselt nur durch `50` und `51`.
- `61` (Stummschaltung) beendet ihn nicht.
- Abfragen setzen ihn nie auf „aus“.

Abnahme: Alle Melder erscheinen in Home Assistant. Testtaste und Sockel-Entnahme verändern die passenden Entitäten.

### Stufe 4: Diagnose und Netztest

Umfang nach **[E1]**: Diagnose und Netztest. Beide sind per YAML einzeln zuschaltbar und lösen beim Drücken sofort aus. Jede Auslösung wird geloggt und als Home-Assistant-Ereignis gemeldet.

| Funktion | Befehl | Wirkung im Netz | Bedienung |
| --- | --- | --- | --- |
| Diagnose | `C1` an eine SID | bisher nur WHT-630: piept und meldet `C2` | Button je Melder, Aktion `wisafe2.diagnose` mit `sid` |
| Netztest | `70 dev FF 01 model`, danach `91` auf `41` | alle Melder ertönen (Klasse `FF` aus der Identität) | Button „Netztest“ |

Bewusst ausgelassen: Alarm auslösen (`50`), Stummschalten (`61`), Alarm beenden (`51`).

Abnahme: Diagnose an den WHT-630 ergibt Piepen und `C2`. Netztest lässt alle Melder ertönen.

### Stufe 5 (später): Pairing-Assistent

Vorgemerkt nach **[E10]**. Bis dahin bleiben netzverändernde Befehle ausgeschlossen: `D3 12 01` (Pairing), `D3 04` (Netzkarte), `D3 19 01 00` (Kopplung löschen) sowie die unbekannten Codes `D3 12 65`, `E6`, `E8`, `E2`. Eingelernt wird über die Taste am Funkmodul.

Ein späterer Assistent bekommt eine eigene Absicherung und eine eigene Planung, gestützt auf den Ablauf „Pairing über den Host“ der Protokollanalyse.

## 4. Querschnittsthemen

- **Host-Tests:** Jede Stufe erweitert die Tests auf dem PC. Testdaten sind die Beispielrahmen der Protokollanalyse und Mitschnitte aus dem eigenen Netz. Vorschlag: zusätzlich eine GitHub Action, die die Tests und einen ESPHome-Build ausführt.
- **Mitschnitte:** Optional ein Debug-Schalter, der jeden Rohrahmen mit Zeitstempel loggt. Er hilft bei offenen Punkten wie `D4 10` und `D5`.
- **Dateisystem:** Für `nodes.bin` braucht der Pico ein LittleFS im Flash. Die Größe wird in der YAML über `platformio_options` festgelegt; die kleinste Stufe reicht.
- **Speicher:** Das Register mit 50 Plätzen braucht wenige KB. Entitäten für 22 Melder mit je ~13 Einträgen sind rund 290 Entitäten. Ob das mit der ESPHome-API auf dem RP2350 flüssig läuft, wird in Stufe 3 gemessen **[R6]**.
- **Dokumentation:** README und Hardware-Unterlage wachsen mit jeder Stufe.

## 5. Reihenfolge

Alle Entscheidungen sind getroffen. Die Stufen bauen aufeinander auf: 0 → 1 → 2 → 3 → 4. Stufe 4 braucht nur Stufe 1 (Identität) und kann bei Bedarf vor Stufe 3 kommen. Stufe 5 ist für später vorgemerkt.

## 6. Entscheidungen

Getroffen am 11.10.2026.

| Nr. | Frage | Entscheidung |
| --- | --- | --- |
| E1 | Welche aktiven Funktionen bietet die Brücke an? | Diagnose (`C1`) und Netztest (`70`), per YAML einzeln zuschaltbar, direkt auslösbar. Alarm auslösen, Stummschalten und Alarm beenden werden nicht angeboten |
| E2 | Init `D3 19 50 00` bei jedem Start? | Ja, ohne vorangestellte `00`. Beim nächsten Start wird mitgeschnitten, ob das Netz geflutet wird |
| E3 | Identität der Brücke (`91` auf `41`)? | Geräte-ID fest in der YAML, hier `14B8A5` (die ID, die das Modul am Pico bereits aus dem Pairing trägt). Klasse `FF`, jedes `41` beantworten. Modell per YAML, Vorgabe `11 03` (wie Arduino-Firmware und NikNakk) |
| E4 | Antworten auf Anfragen an die eigene Geräte-ID? | Alle beantworten: `C3`→`C4`, `72`→`73`, `C7`→`C8`, `C5`→`C6`, `C1`→`C2`, mit festen Werten (keine Fehler, 3000 mV, keine Ereignisse, Diagnose bestanden) |
| E5 | Wie oft werden Melder aus der Ferne abgefragt? | Zielbetrieb: Erkundung beim Start, `D3 06` je SID alle 19 h, `C7` je Melder alle 24 h, ereignisgesteuerte Abfragen. In Entwicklung und Test nur beim Start und auf Knopfdruck. Umschaltung über YAML `auto_discovery` |
| E6 | Wie werden die Melder zu Entitäten? | Melder in der YAML auflisten, Vorlage aus dem Log, dazu das Register als JSON-Text-Sensor. Alles über die native API |
| E7 | Melderregister speichern? | Ja, als Binärdatei `nodes.bin` mit festen 50 Plätzen (408 Byte), nur bei Änderung, gebündelt. Button „Rediscover Network“ löscht das Register und erkundet neu |
| E8 | Wann gilt ein Knoten als nicht erreichbar? | Nach `D4 01` oder zwei ausbleibenden Antworten in Folge. Entfernen nur über „Rediscover Network“ |
| E9 | Unbekannter Rahmen `D4 10 BF 7F 0E 05 00 00 00 00 14`? | Weiter mitschneiden, roh loggen, Auswertung erst bei geklärter Bedeutung |
| E10 | Netzverändernde Befehle? | Vorerst ausgeschlossen. Ein Pairing-Assistent ist als Stufe 5 vorgemerkt |
| E11 | Beim Senden maskieren? | Ja. Bei SIDs oder IDs mit `7D`/`7E` wird gezielt geprüft |
| E12 | Modell `11 04`? | WHT-630. Der FP1720W2-R ist ein Kit, das den WHT-630 mit Funkmodul enthält |

## 7. Risiken

| Nr. | Risiko | Gegenmaßnahme |
| --- | --- | --- |
| R1 | Fernabfragen belasten die Batterien aller Melder | in der Entwicklung keine periodischen Abfragen (E5), Start-Erkundung nur für unbekannte SIDs, Zähler für gesendete Fernanfragen als Sensor |
| R2 | Parallele Fernanfragen gehen verloren | Sendeplanung mit genau einer offenen Fernanfrage und 500 ms Ruhe |
| R3 | Feldbedeutungen sind teils vermutet (`50` d5, `61` SID, `D2` d12, `D4 06` d5, `71` Bit 0x80, `C6`-Ereignisse) | unsichere Felder als „roh“ ausgeben und kennzeichnen, mit Mitschnitten nachschärfen |
| R4 | Doppelte Geräte-ID der Brücke im Netz | eigene ID aus der YAML per `91` (E3), Abgleich mit `D1` in Stufe 1 |
| R5 | Flash-Schreibvorgänge für `nodes.bin` halten Kern 1 an, SPI-Bytes können verloren gehen | seltene, gebündelte Schreibvorgänge (E7). In Stufe 2 messen, ob das RM verlorene Bytes wiederholt. Falls nötig: Schreiben nur in Ruhephasen ohne offene Anfrage |
| R6 | Viele Entitäten belasten API und Speicher | Messung in Stufe 3, Diagnoseentitäten optional |
| R7 | Ein Alarm kann übersehen werden, wenn die Brücke ausfällt oder Rahmen verliert | Brücke bleibt Zusatz. Ereignisse für Alarm und Verbindung zur Brücke in Home Assistant überwachen |
| R8 | Das eigene RM hängt nach dem Einlernen eines anderen Knotens | Watchdog: bleibt `46` mehrfach aus, Hinweis im Status. Ein automatischer Neustart des RM braucht eine schaltbare Versorgung |
| R9 | Die Brücke wird nach dem Init zum Empfänger aller Hostantworten. Ob das andere Abfragende stört, ist offen | bei den Tests von Stufe 2 beobachten, im Zweifel E2 neu entscheiden |
| R10 | Mit Klasse `FF` blinkt die LED des Funkmoduls vermutlich alle paar Sekunden | bekannt und beabsichtigt |
| R11 | Netztest und Diagnose lösen ohne Rückfrage aus, auch über Home-Assistant-Automationen | bekannt und beabsichtigt; Funktionen per YAML zuschaltbar, jede Auslösung im Log und als Ereignis |
| R12 | Feste Antworten auf `72`/`C7`/`C5`/`C1` könnten andere Werkzeuge (Gateway, wisafe-mm) über den Zustand der Brücke täuschen | bekannt und beabsichtigt; die Werte entsprechen einem fehlerfreien Gerät |

## 8. Nächster Schritt

Umsetzung von Stufe 0: Rahmenschicht, Maskierung und Sendeplanung, mit Host-Tests und dem Mitschnitt zur Flutung beim Init.

# Umsetzungsplan: W2-Modulprotokoll auf der Pico-2-W-Brücke

Stand: 11.10.2026. Grundlage ist [PROTOCOL_ANALYSIS_W2_MODULE.md](https://github.com/sbohlma/FireAngel/blob/main/wisafe-mm/docs/PROTOCOL_ANALYSIS_W2_MODULE.md) (im Folgenden „Protokollanalyse“) mit den dort genannten Quellen, vor allem NikNakks [fireangel-pro-connected-esphome](https://github.com/NikNakk/fireangel-pro-connected-esphome) und Tho85s [ws2mqtt](https://github.com/Tho85/ws2mqtt).

Ziel: Alle Melder im Netz erscheinen in ESPHome und Home Assistant mit Zustand, Fehlern, Batterie und Ereignissen. Aktive Funktionen wie der Test stehen bereit, sobald die zugehörigen Entscheidungen getroffen sind.

**Sicherheitsrahmen.** Die zertifizierten Melder bleiben die Sicherheitsanlage. Die Brücke ist ein zusätzlicher Beobachter und darf nie der einzige Alarmweg sein. Befehle, die Melder auslösen oder das Netz verändern, sind in diesem Plan eigene Stufen mit eigener Entscheidung (siehe Abschnitt 6).

Kennzeichnung: **[E1]** usw. verweist auf eine offene Entscheidung in Abschnitt 6, **[R1]** usw. auf ein Risiko in Abschnitt 7.

## 1. Ausgangslage

Funktioniert und ist auf der Hardware bestätigt:

- Kern 1 ist SPI-Slave mit IRQ-Quittung, sendet Befehle aus einer Warteschlange und meldet pro Befehl ein Ergebnis.
- Kern 0 startet nach dem WLAN-Aufbau das SPI und den Init `D3 19 50 00 7E`, erkennt `46 7E` und loggt Rahmen.
- Beobachtet nach dem Init: `D4 10 BF 7F 0E 05 00 00 00 00 14 7E`.

Abweichungen des Ist-Stands von der Protokollanalyse, die Stufe 0 behebt:

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

Der beobachtete Untertyp `D4 10` steht nicht in der Protokollanalyse **[E9]**.

## 2. Zielarchitektur

Kern 1 bleibt ein reiner Byte-Transport und wird nicht erweitert. Die gesamte Protokolllogik liegt auf Kern 0, gegliedert in Schichten, die sich ohne Hardware auf dem PC testen lassen.

| Schicht | Datei (geplant) | Aufgabe |
| --- | --- | --- |
| Rahmen | `framing.h/.cpp` | Bytes zu Rahmen: Füllbytes, Demaskierung, Ende `7E`, Zeitlimit. Beim Senden maskieren und `7E` anhängen |
| Nachrichten | `messages.h/.cpp` | Rahmen zu typisierten Nachrichten und zurück. Ein `struct` je Typ, SID und Sequenznummer von hinten gelesen |
| Sendeplanung | `scheduler.h/.cpp` | Warteschlange mit Prioritäten, 500 ms Ruhe, eine offene Fernanfrage, Zuordnung von Antworten, Zeitlimits |
| Melderregister | `registry.h/.cpp` | Tabelle aller Knoten (SID 0–63) mit Gerät, Modell, Klasse und Zuständen |
| Abläufe | `discovery.cpp`, `host_identity.cpp` | Netzerkundung, periodische Abfragen, Antworten auf `41`, `C3` und andere Hostanfragen |
| ESPHome | `wisafe2.cpp`, Plattformdateien | Entitäten, Aktionen, Buttons, Ereignisse |

Die Schichten Rahmen bis Melderregister kommen ohne ESPHome-Header aus. So laufen sie in Host-Tests mit den Beispielrahmen aus der Protokollanalyse.

Die Sendeplanung kennt drei Klassen von Befehlen:

| Klasse | Beispiele | Regel |
| --- | --- | --- |
| Hostantwort | `91` auf `41`, `C4` auf `C3` | sofort, vor allem anderen |
| Lokal | `D1`, `D3 03` | Antwort nach ~70 ms ohne `46`, Zeitlimit 500 ms |
| Fernanfrage | `D3 06 sid 00/01`, `72`, `C7`, `C5` | `46` binnen 150 ms (bei `47`: SID unbekannt), Antwort asynchron bis ~50 s, nur eine gleichzeitig |

## 3. Implementierungsstufen

Jede Stufe endet mit einem lauffähigen Stand, Host-Tests und einer Prüfung auf der Hardware.

### Stufe 0: Rahmenschicht und Sendeplanung

Ziel: Rahmen werden korrekt erkannt und gesendet, Befehle laufen im richtigen Takt.

- Rahmenschicht mit Demaskierung, Verwerfen führender `00`, Zeitlimit 100 ms und Maskierung beim Senden **[E11]**.
- Typisierte Nachrichten für die lokalen Antworten `46`, `47`, `41`, `D2`, `D4 xx`.
- Sendeplanung mit den drei Klassen aus Abschnitt 2. Der Init läuft als erster Befehl über sie.
- Der Status-Sensor zeigt den Zustand des Moduls: „Init läuft“, „Modul bereit“, „keine Antwort“, „ungekoppelt“.
- Host-Tests für Rahmen und Sendeplanung, darunter maskierte Rahmen und Antworten, die sich überholen.

Abnahme auf der Hardware: Init wie bisher, danach `D1` und `D3 03` per Button ohne verlorene Antwort.

### Stufe 1: Vollständiges Parsing und manuelle Abfragen

Ziel: Jede bekannte Nachricht wird verstanden und lesbar geloggt. Jede lesende Abfrage lässt sich von Hand auslösen.

- Decoder für alle Typen der Protokollanalyse: `50`, `51`, `61`, `70`, `71`, `72`, `73`, `C1` bis `C8`, `D2`, `D4 01/03/04/06/09/11`, `D5`. Unbekannte Typen als `Roh:`.
- Feldauswertung: Geräte-ID in Anzeigereihenfolge, Modell- und Klassentabelle, Fehlerflags `71`, Fehlerdetails `73` je Modell, Batterie `C8` in mV, RM-Batterie nach U = 1048,576 / (BVNL + 285) V, RSSI.
- Entfernen von Duplikaten über (SID, Typ, Sequenznummer) mit einem Fenster von 60 s.
- Eigenes RM: `D1` und `D3 03` beim Start und auf Abruf. Daraus entstehen Sensoren für eigene SID, Kopplung, RM-Batterie, RSSI, Firmware, gelernte Geräte-ID und Funkfehler.
- Manuelle Abfragen als ESPHome-Aktionen, die sich über `api: actions` an Home Assistant durchreichen lassen:
  - `wisafe2.query_remote_status` mit `sid` (`D3 06 sid 00`)
  - `wisafe2.query_identity` mit `sid` (`D3 06 sid 01`)
  - `wisafe2.query_battery`, `wisafe2.query_fault_details`, `wisafe2.query_fault_events` mit Geräte-ID und Modell (`C7`, `72`, `C5`)
- Die Antworten erscheinen im Log und als Text-Sensor „letzte Antwort“.

Bewusst offen in dieser Stufe:

- Antwort auf `41` mit `91` **[E3]**. Bis zur Entscheidung wird `41` nur geloggt.
- Antwort auf `C3`, `72`, `C7`, `C5`, `C1` an die eigene Geräte-ID **[E4]**.

Abnahme: Testtaste an einem Melder ergibt einen vollständig gedeuteten `70`-Rahmen. Melder vom Sockel nehmen ergibt `71` mit Flags. Handabfragen liefern `D4 06`, `C4`, `C8`, `73`.

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

Netzerkundung nach dem Start, in dieser Reihenfolge:

1. `D3 03`: Liste der SIDs.
2. Je SID `D3 06 sid 00`: Geräte-ID, RM-Batterie, RSSI. Ohne Antwort nach ~45 s kommt `D4 01`, die SID gilt dann als nicht erreichbar.
3. Je SID mit unbekannter Klasse `D3 06 sid 01`: `C4` mit Klasse und Modell. RMs ohne Melder antworten nicht, sie bleiben „Typ unbekannt“.
4. Je Melder `C7` (Batterie) und `72` (Fehlerdetails). `C5` nur bei WST-630 und ST-630-DE.

Laufende Aktualisierung:

- Passive Nachrichten aktualisieren das Register sofort: `70`, `71`, `50`, `51`, `61`, `D2`, `D4 04`, `D4 09`, sowie Kopien von `C4`, `73`, `C6`, `C8` nach dem Init.
- Nach `71` mit Flag 0x02 folgen `72` und `C5` für diesen Melder.
- `D4 04` (neue Netzkarte) und `D4 09` mit unbekannter SID lösen eine Erkundung der betroffenen SIDs aus.
- Periodische Fernabfragen in großem Abstand **[E5]**.

Dauer: Mit einer offenen Fernanfrage und 4–28 s je Antwort braucht eine vollständige Erkundung von 22 Meldern rund 10–20 min.

Persistenz: Geräte-ID, Modell und Klasse je SID werden gespeichert, damit nach einem Neustart keine vollständige Erkundung nötig ist **[E7]**.

Abnahme: Nach einer Erkundung stimmt das Register mit der bekannten Melderliste überein (Anzahl, IDs, Modelle).

### Stufe 3: Melder als Geräte in Home Assistant

Ziel: Jeder Melder erscheint mit eigenen Entitäten. Der Weg dorthin ist die größte offene Entscheidung **[E6]**.

Geplante Entitäten je Melder:

| Art | Entität |
| --- | --- |
| binary_sensor | Alarm (Feuer/CO), Fehler, Sockel-Problem, Melder-Batterie schwach, RM-Batterie schwach, im Netz |
| sensor | Melder-Batterie (mV), RM-Batterie (V), Empfangspegel, zuletzt gesehen |
| text_sensor | Modell, Fehlerdetails, letzter Test mit Ergebnis |
| button | Status aktualisieren |

Netzweite Entitäten:

- Alarm aktiv im Netz
- Stummschaltung aktiv
- letztes Ereignis
- Anzahl Knoten
- Erkundung läuft
- Status des eigenen RM

Home-Assistant-Ereignisse für Alarm, Alarmende, Stummschaltung, Test und Fehlerbericht, damit Automationen darauf reagieren können.

Regeln für den Alarmzustand (übernommen aus NikNakk und der Protokollanalyse):

- Er gilt nach dem Start als unbekannt.
- Er wechselt nur durch `50` und `51`.
- `61` (Stummschaltung) beendet ihn nicht.
- Abfragen setzen ihn nie auf „aus“.

Abnahme: Alle Melder erscheinen in Home Assistant. Testtaste und Sockel-Entnahme verändern die passenden Entitäten.

### Stufe 4: Aktive Funktionen (nach Entscheidung)

Jede Funktion ist einzeln per YAML zuschaltbar und standardmäßig aus. Umfang und Schutz klärt **[E1]**.

| Funktion | Befehl | Wirkung im Netz |
| --- | --- | --- |
| Diagnose | `C1` an eine SID | bisher nur WHT-630: piept und meldet `C2` |
| Netztest | `70 dev klasse 01 model`, danach `91` auf `41` | alle Melder ertönen; Klasse `FF` alle, `81` Feuer, `41` CO |
| Stummschalten | `61 dev klasse 01` | stummgeschaltete Melder ertönen nach spätestens 4 min wieder, solange ein Melder im Alarm ist |
| Alarm beenden | `51 dev klasse` | am Netz unbestätigt |
| Alarm auslösen | `50 dev klasse 00` | echter Alarm in allen Meldern |

Netzverändernde Befehle sind in keiner Stufe vorgesehen **[E10]**: `D3 12 01` (Pairing), `D3 04` (Netzkarte), `D3 19 01 00` (Kopplung löschen) sowie die unbekannten Codes `D3 12 65`, `E6`, `E8`, `E2`.

## 4. Querschnittsthemen

- **Host-Tests:** Jede Stufe erweitert die Tests auf dem PC. Testdaten sind die Beispielrahmen der Protokollanalyse und Mitschnitte aus dem eigenen Netz. Vorschlag: zusätzlich eine GitHub Action, die die Tests und einen ESPHome-Build ausführt.
- **Mitschnitte:** Optional ein Debug-Schalter, der jeden Rohrahmen mit Zeitstempel loggt. Er hilft bei offenen Punkten wie `D4 10` und `D5`.
- **Speicher:** Das Register für 64 SIDs braucht wenige KB. Entitäten für 22 Melder mit je ~12 Einträgen sind rund 270 Entitäten. Ob das mit der ESPHome-API auf dem RP2350 flüssig läuft, wird in Stufe 3 gemessen **[R6]**.
- **Dokumentation:** README und Hardware-Unterlage wachsen mit jeder Stufe.

## 5. Reihenfolge und Abhängigkeiten

| Stufe | baut auf | benötigte Entscheidungen |
| --- | --- | --- |
| 0 Rahmen und Takt | – | E11 |
| 1 Parsing und Handabfragen | 0 | E2, E12; E3 und E4 für Hostantworten |
| 2 Register und Erkundung | 1 | E5, E7 |
| 3 Entitäten in Home Assistant | 2 | E6 |
| 4 Aktive Funktionen | 1 (Netztest braucht E3) | E1 |

## 6. Offene Entscheidungen

| Nr. | Frage | Hintergrund | Vorschlag |
| --- | --- | --- | --- |
| E1 | Welche aktiven Funktionen soll die Brücke anbieten, und wie werden sie geschützt? | Der bisherige Rahmen schloss Test- und Alarmsendungen aus. Ein Netztest lässt alle Melder ertönen, ein Alarm löst einen echten Alarm aus. Eine Stummschaltung aus der Ferne kann einen echten Alarm überdecken | Diagnose und Netztest einzeln zuschaltbar, mit Bestätigung in zwei Schritten (Scharfschalten, dann Auslösen binnen 30 s) und Sperrzeit. Alarm auslösen nicht anbieten. Stummschalten und Alarm beenden zurückstellen |
| E2 | Init `D3 19 50 00` bei jedem Start beibehalten? | Die Kopplung übersteht Stromausfälle, das RM antwortet auch ohne Init. Der Init wurde am Nano netzweit geflutet, vermutlich wegen der `00`-Füllbytes. Nach dem Init erhielt der Host Kopien aller Hostantworten, was für die Erkundung nützlich ist | Init beibehalten, ohne Füllbytes senden. Beim ersten Start mit Mitschnitt prüfen, ob die Pico-Brücke flutet |
| E3 | Mit welcher Identität antwortet die Brücke auf `41`, und beantwortet sie jedes `41`? | Das RM übernimmt die Geräte-ID aus `91` und behält sie. Ohne Antwort trägt es die ID seines früheren Melders, die ID ist dann doppelt im Netz. Klasse `FF` lässt die RM-LED blinken (vermutet) | Eigene, feste Geräte-ID aus der YAML (z. B. aus der Pico-Seriennummer abgeleitet), Modell und Klasse konfigurierbar, Vorgabe Klasse `81`. Jedes `41` beantworten wie ws2mqtt und NikNakk |
| E4 | Beantwortet die Brücke Anfragen an ihre eigene Geräte-ID (`C3`, `72`, `C7`, `C5`, `C1`)? | Ohne `C4` bleibt die Brücke für andere Knoten „Typ unbekannt“. Die Brücke ist kein Melder und hat keine echten Werte für Batterie und Fehler | `C3` mit `C4` beantworten. `72`, `C7`, `C5`, `C1` vorerst nicht beantworten |
| E5 | Wie oft fragt die Brücke Melder aus der Ferne ab? | Jede Fernanfrage erzeugt Funkverkehr bei allen Knoten (65–97 Rahmen in 30 s) und kostet Batterie. Das Gateway prüft fehlende Geräte etwa alle 19 h | Erkundung beim Start, danach `D3 06` je SID alle 19 h verteilt, `C7` je Melder alle 24 h. Zusätzlich nur ereignisgesteuert. Intervalle per YAML einstellbar |
| E6 | Wie werden die Melder zu Entitäten? | ESPHome legt Entitäten beim Kompilieren fest | (a) Melder in der YAML auflisten, die Brücke loggt die gefundenen IDs als Vorlage. (b) Dynamisch über MQTT-Discovery wie bei NikNakk, braucht einen MQTT-Broker. (c) Feste Plätze „Melder 1…N“. (d) Zusätzlich das ganze Register als JSON-Text-Sensor. Vorschlag: (a) mit (d), weil beides allein mit der nativen API auskommt. (b) als spätere Option |
| E7 | Melderregister im Flash speichern? | Ein Flash-Schreibvorgang hält Kern 1 an, in dieser Zeit gehen SPI-Bytes verloren | Speichern nur bei Änderung von Geräte-ID, Modell oder Klasse, gebündelt und verzögert. Alternativ ohne Persistenz und Erkundung bei jedem Start |
| E8 | Wie lange gilt ein Melder als „im Netz“ ohne Antwort? | Der Host sieht keinen periodischen Verkehr anderer Knoten | „Nicht erreichbar“ nach `D4 01` oder zwei ausbleibenden Antworten in Folge. Kein Entfernen aus dem Register ohne Bestätigung |
| E9 | Was bedeutet `D4 10 BF 7F 0E 05 00 00 00 00 14`? | Kam einmal direkt nach dem Init, Untertyp unbekannt | Weiter mitschneiden, als `Roh:` loggen, vorerst nicht auswerten |
| E10 | Netzverändernde Befehle dauerhaft ausschließen? | `D3 04` mit unvollständiger Karte entfernt aktive Knoten. Ein RM hing einmal beim Einlernen eines anderen Knotens | Dauerhaft ausschließen. Pairing nur über die Einlerntaste |
| E11 | Beim Senden maskieren? | ws2mqtt maskiert, an einer Bridge verifiziert ist nur die Richtung RM → Host | Maskieren. Bei SIDs oder IDs mit `7D`/`7E` gezielt prüfen |
| E12 | Ist `11 04` ein WHT-630 oder ein FP1720W2-R? | Der bisherige Code folgt C19HOP, die Protokollanalyse nennt 0x0411 = WHT-630 und bestätigt das mit einem `C2` aus dem eigenen Netz | Protokollanalyse übernehmen. Klärt sich spätestens mit der ersten Erkundung (`C4` liefert Klasse und Modell) |

## 7. Risiken

| Nr. | Risiko | Gegenmaßnahme |
| --- | --- | --- |
| R1 | Fernabfragen belasten die Batterien aller Melder | seltene, verteilte Abfragen (E5), Zähler für gesendete Fernanfragen als Sensor |
| R2 | Parallele Fernanfragen gehen verloren | Sendeplanung mit genau einer offenen Fernanfrage und 500 ms Ruhe |
| R3 | Feldbedeutungen sind teils vermutet (`50` d5, `61` SID, `D2` d12, `D4 06` d5, `71` Bit 0x80, `C6`-Ereignisse) | unsichere Felder als „roh“ ausgeben und kennzeichnen, mit Mitschnitten nachschärfen |
| R4 | Doppelte Geräte-ID der Brücke im Netz | Identität per `91` setzen (E3), gelernte ID aus `D2` anzeigen |
| R5 | Flash-Schreibvorgänge halten Kern 1 an | seltene, gebündelte Schreibvorgänge (E7). Messen, ob das RM verlorene Bytes wiederholt |
| R6 | Viele Entitäten belasten API und Speicher | Messung in Stufe 3, Diagnoseentitäten optional |
| R7 | Ein Alarm kann übersehen werden, wenn die Brücke ausfällt oder Rahmen verliert | Brücke bleibt Zusatz. Ereignisse für Alarm und Verbindung zur Brücke in Home Assistant überwachen |
| R8 | Das eigene RM hängt nach dem Einlernen eines anderen Knotens | Watchdog: bleibt `46` mehrfach aus, Hinweis im Status. Ein automatischer Neustart des RM ist ohne Schaltung für dessen Versorgung nicht möglich |
| R9 | Die Brücke wird nach dem Init zum Empfänger aller Hostantworten. Ob das andere Abfragende stört, ist offen | bei den Tests von Stufe 2 beobachten, im Zweifel E2 neu entscheiden |

## 8. Nächster Schritt

Für Stufe 0 genügt die Entscheidung E11. Für Stufe 1 sollten E2, E3, E4 und E12 feststehen. Die übrigen Entscheidungen werden vor der jeweiligen Stufe gebraucht.

# Umsetzungsplan: W2-Modulprotokoll auf der Pico-2-W-Brücke

Stand: 12.10.2026, Entscheidungen E1–E12 getroffen, E3 und E7 überarbeitet, Stufe 0 umgesetzt und auf der Hardware abgenommen, Init-Sequenz korrigiert (Abschnitt 9), Stufe 1 im Code umgesetzt und zum Teil auf der Hardware abgenommen. Grundlage ist [PROTOCOL_ANALYSIS_W2_MODULE.md](https://github.com/sbohlma/FireAngel/blob/main/wisafe-mm/docs/PROTOCOL_ANALYSIS_W2_MODULE.md) (im Folgenden „Protokollanalyse“) mit den dort genannten Quellen, vor allem NikNakks [fireangel-pro-connected-esphome](https://github.com/NikNakk/fireangel-pro-connected-esphome), Tho85s [ws2mqtt](https://github.com/Tho85/ws2mqtt) und [wisafe-mm](https://github.com/sbohlma/FireAngel/tree/main/wisafe-mm).

Ziel: Alle Melder im Netz erscheinen in ESPHome und Home Assistant mit Zustand, Fehlern, Batterie und Ereignissen. Diagnose und Netztest stehen als Funktionen bereit.

**Sicherheitsrahmen.** Die zertifizierten Melder bleiben die Sicherheitsanlage. Die Brücke ist ein zusätzlicher Beobachter und darf nie der einzige Alarmweg sein. Auslösende Funktionen sind auf Diagnose und Netztest begrenzt (E1), netzverändernde Befehle bleiben ausgeschlossen (E10).

Kennzeichnung: **[E1]** usw. verweist auf eine Entscheidung in Abschnitt 6, **[R1]** usw. auf ein Risiko in Abschnitt 7, P1 usw. auf eine Protokoll-Erkenntnis in Abschnitt 9.

## 1. Ausgangslage

Funktioniert und ist auf der Hardware bestätigt:

- Kern 1 ist SPI-Slave mit IRQ-Quittung, sendet Befehle aus einer Warteschlange und meldet pro Befehl ein Ergebnis.
- Kern 0 startet nach dem WLAN-Aufbau das SPI und den Init `D3 19 50 00 7E`, erkennt `46 7E` und loggt Rahmen.
- Beobachtet nach dem Init: `D4 10 BF 7F 0E 05 00 00 00 00 14 7E` (Deutung in Abschnitt 9, P2 und P3).

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

Stand: umgesetzt (`framing`, `messages`, `scheduler`, Host-Tests unter `tests/host`) und am 12.10.2026 auf der Hardware abgenommen.

Ziel: Rahmen werden korrekt erkannt und gesendet, Befehle laufen im richtigen Takt.

- Rahmenschicht mit Demaskierung, Verwerfen führender `00`, Zeitlimit 100 ms und Maskierung beim Senden **[E11]**.
- Typisierte Nachrichten für die lokalen Antworten `46`, `47`, `41`, `D2`, `D4 xx`.
- Sendeplanung mit den drei Klassen aus Abschnitt 2. Der Init läuft als erster Befehl über sie, ohne vorangestellte `00` **[E2]**.
- Init-Sequenz, korrigiert nach der Hardware-Abnahme (Abschnitt 9, P1):
  1. `D3 19 50 00 7E` senden, `46` abwarten.
  2. Auf `D4 10 …` warten, höchstens 10 s. Bis dahin nimmt das Modul keine Bytes vom Host an. Bleibt `D4 10` aus, folgt eine Warnung im Log und die Abfrage trotzdem.
  3. Erst dann `D1` und `D3 03`.
- Nimmt das Modul kein einziges Byte eines Befehls an, wiederholt die Sendeplanung ihn bis zu 20-mal im Abstand von mindestens 500 ms. Kern 1 meldet dazu den Grund („CS belegt“, „Modul taktet nicht“, „Byte unvollständig“).
- Der Status-Sensor zeigt den Zustand des Moduls: „Init läuft“, „Modul bereit“, „keine Antwort“, „ungekoppelt“.
- Host-Tests für Rahmen und Sendeplanung, darunter maskierte Rahmen und Antworten, die sich überholen.

Abnahme auf der Hardware (erledigt am 12.10.2026):

- Init mit Quittung `46`, danach `D4 10`, dann `D1` und `D3 03` automatisch und per Button jeweils im ersten Versuch beantwortet. Erfüllt.
- Mitschnitt nach einem Neustart: Flutet der Init der Pico-Brücke das Netz? Indirekt belegt: Der Init löst eine Netzrunde aus, `D4 10` meldet die antwortenden Knoten (Abschnitt 9, P3 und P5). In E2 nachgetragen. Ein Mitschnitt an einem zweiten Knoten folgt bei den Tests von Stufe 2 **[R9]**.

### Stufe 1: Vollständiges Parsing, Identität und manuelle Abfragen

Stand: im Code umgesetzt (`catalog`, `messages`, `frame_analyzer`, `duplicate_filter`, `host_identity`, `node_cache`, Aktionen in `automation.h`, Host-Tests unter `tests/host`). Die Abnahme auf der Hardware läuft seit dem 12.10.2026, drei von fünf Punkten sind erfüllt.

Ziel: Jede bekannte Nachricht wird verstanden und lesbar geloggt. Die Brücke tritt mit eigener Identität im Netz auf. Jede lesende Abfrage lässt sich von Hand auslösen.

- Decoder für alle Typen der Protokollanalyse: `50`, `51`, `61`, `70`, `71`, `72`, `73`, `C1` bis `C8`, `D2`, `D4 01/03/04/06/09/10/11`, `D5`. `D4 10` erscheint als Liste der Init-Antworten mit eigener SID (P2, P3), ohne Einfluss auf das Register **[E9]**. Unbekannte Typen und Untertypen als `Roh:`.
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

Umsetzung im Detail:

- Bis zum Melderregister aus Stufe 2 merkt sich ein kleiner Speicher im RAM (`node_cache`) Geräte-ID, Modell und Klasse je SID aus gehörten Nachrichten. Damit brauchen `query_battery`, `query_fault_details` und `query_fault_events` nur die SID, sobald der Knoten einmal mit Geräte-ID und Modell gehört wurde.
- Antworten werden genau zugeordnet: `D4 06` über die SID, `C4` über die SID am Rahmenende, `C8`, `73` und `C6` über die Geräte-ID. Kopien von Hostantworten auf Anfragen anderer Knoten beenden so keine eigene Anfrage.
- `D4 01` mit der Ziel-SID beendet `D3 06` als „nicht erreichbar“ **[E8]**.
- `C1` trägt keine Geräte-ID. Wird es geflutet, weil das Ziel abgenommen ist, antwortet auch die Brücke mit `C2` (höchstens einmal je 60 s) **[R12]**.
- Neue YAML-Optionen: `device_id`, `model` (Vorgabe `0x0311`), Sensoren `last_reply`, `rm_sid`, `rm_battery`, `rm_rssi`, `rm_radio_faults`, `rm_firmware`, `rm_device_id`, `rm_paired`.

Abnahme (Stand 12.10.2026, Log 02:48–03:02):

- Testtaste an einem Melder ergibt einen vollständig gedeuteten `70`-Rahmen. Offen.
- Melder vom Sockel nehmen ergibt `71` mit Flags. Erfüllt: SID 14 und SID 26, jeweils Flags `01` (abgenommen, kalibriert). Offen ist noch `71 … 05` nach dem Zurücksetzen.
- Handabfragen liefern `D4 06`, `C4`, `C8`, `73`. Offen, die Aktionen brauchen `api:` mit `actions` in der YAML.
- `D1` zeigt im Feld d6–d8 die ID aus der YAML (`A5 B8 14`), ohne Warnung. Erfüllt.
- Eine ID-Anfrage eines anderen Knotens an die Brücke erhält `C4`. Erfüllt: Der W2-MM-Dongle (SID 15) fragte `C3`, `C7`, `C5` und `72` und erhielt `C4`, `C8`, `C6` und `73`. Das W2-MM zeigt die Brücke korrekt an (Abschnitt 9, P7 und P8).

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
- **Mitschnitte:** Optional ein Debug-Schalter, der jeden Rohrahmen mit Zeitstempel loggt. Er hilft bei offenen Punkten wie `D5` und der Bestätigung von `D4 10` (P3). Neue Befunde werden in Abschnitt 9 gesammelt.
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
| E2 | Init `D3 19 50 00` bei jedem Start? | Ja, ohne vorangestellte `00`. Nachtrag 12.10.2026: Nach `46` wartet die Brücke auf `D4 10`, erst dann folgen weitere Befehle (P1). Der Init löst eine Netzrunde aus, an der die Knoten mit einer Antwort teilnehmen (P3, P5). Der direkte Mitschnitt an einem zweiten Knoten folgt in Stufe 2 |
| E3 | Identität der Brücke (`91` auf `41`)? | Geräte-ID fest in der YAML, hier `14B8A5` (die ID, die das Modul am Pico bereits aus dem Pairing trägt). Klasse `FF`, jedes `41` beantworten. Modell per YAML, Vorgabe `11 03` (wie Arduino-Firmware und NikNakk) |
| E4 | Antworten auf Anfragen an die eigene Geräte-ID? | Alle beantworten: `C3`→`C4`, `72`→`73`, `C7`→`C8`, `C5`→`C6`, `C1`→`C2`, mit festen Werten (keine Fehler, 3000 mV, keine Ereignisse, Diagnose bestanden) |
| E5 | Wie oft werden Melder aus der Ferne abgefragt? | Zielbetrieb: Erkundung beim Start, `D3 06` je SID alle 19 h, `C7` je Melder alle 24 h, ereignisgesteuerte Abfragen. In Entwicklung und Test nur beim Start und auf Knopfdruck. Umschaltung über YAML `auto_discovery` |
| E6 | Wie werden die Melder zu Entitäten? | Melder in der YAML auflisten, Vorlage aus dem Log, dazu das Register als JSON-Text-Sensor. Alles über die native API |
| E7 | Melderregister speichern? | Ja, als Binärdatei `nodes.bin` mit festen 50 Plätzen (408 Byte), nur bei Änderung, gebündelt. Button „Rediscover Network“ löscht das Register und erkundet neu |
| E8 | Wann gilt ein Knoten als nicht erreichbar? | Nach `D4 01` oder zwei ausbleibenden Antworten in Folge. Entfernen nur über „Rediscover Network“ |
| E9 | Unbekannter Rahmen `D4 10 BF 7F 0E 05 00 00 00 00 14`? | Weiter mitschneiden, roh loggen, Auswertung erst bei geklärter Bedeutung. Nachtrag 12.10.2026: Format und wahrscheinliche Bedeutung in P2 und P3. Die Brücke nutzt `D4 10` als Bereitschaftssignal nach dem Init (P1). Ab Stufe 1 wird `D4 10` lesbar geloggt, etwa „Init-Antworten: 20 Knoten (…), eigene SID 20“. Ins Register geht es vorerst nicht ein |
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

Rest der Hardware-Abnahme von Stufe 1 (Abschnitt 3): Testtaste, Melder zurück auf den Sockel, Handabfragen über `api: actions`. Danach folgt Stufe 2 nach Freigabe.

## 9. Neue Erkenntnisse zum Protokoll

Dieser Abschnitt sammelt Befunde aus dem eigenen Netz, die in der Protokollanalyse noch fehlen oder sie präzisieren. Er dient als Vorlage, um die Protokollanalyse später zu ergänzen. Jeder Eintrag nennt Beleg und Sicherheit: „bestätigt“ heißt mehrfach reproduziert, „vermutet“ heißt schlüssig gedeutet und noch offen für eine Gegenprobe.

SID-Bitkarten: Bit n von Byte k steht für SID 8k + n, also Byte 0 Bit 0 für SID 0.

### P1: Sperrzeit nach dem Init, `D4 10` als Bereitschaftssignal

Bestätigt am 12.10.2026, reproduzierbar bei jedem Start.

- Nach der Quittung `46` auf `D3 19 50 00` nimmt das Modul etwa 3–4 s keine Bytes vom Host an: Hebt der Host IRQ, beginnt das Modul keinen Takt.
- Das Ende der Sperrzeit meldet das Modul mit `D4 10 …`. Ein `D1` direkt danach wird sofort beantwortet, `D3 03` ebenso.
- Im Ruhezustand beantwortet das Modul `D1` und `D3 03` sofort.
- Beleg: Log vom 12.10.2026, 01:33. `46` um 01:33:49, sechs Versuche `D1` bis 01:33:52 jeweils ohne Takt, `D4 10` um 01:33:53, danach `D1` im ersten Versuch mit Antwort `D2`. Im Folgelauf um 01:46 mit Warten auf `D4 10` gingen `D1` und `D3 03` jeweils im ersten Versuch durch.
- Folgerung für einen Host: Nach dem Init auf `D4 10` warten und erst dann senden. Die Brücke wartet höchstens 10 s.

### P2: Format von `D4 10`

Bestätigt.

`D4 10 d2 … d9 d10 7E`, Zählung wie in der Protokollanalyse (d1 = `10`)

- d2–d9: SID-Bitkarte mit 8 Byte, aufgebaut wie die Karte in `D4 03`.
- d10: eigene SID des Moduls. Im eigenen Netz `14` = SID 20, übereinstimmend mit `D2` d11.

### P3: Bedeutung der Karte in `D4 10`

Vermutet, gut gestützt.

Die Karte enthält die Knoten, die in der Netzrunde nach dem Init geantwortet haben. Das eigene Modul fehlt darin immer.

Mitschnitte im eigenen Netz, verglichen mit `D4 03 FF 7F 1F 05 00 00 00 00` (22 Knoten):

| Mitschnitt | Karte in `D4 10` | Knoten | fehlende SIDs |
| --- | --- | --- | --- |
| 1, vor Stufe 0 | `BF 7F 0E 05 00 00 00 00` | 19 | 6, 16, 20 |
| 2, erster Test von Stufe 0 | `FF 7F 0E 05 00 00 00 00` | 20 | 16, 20 |
| 3, 12.10.2026 01:33 | `FF 7F 0E 05 00 00 00 00` | 20 | 16, 20 |
| 4, 12.10.2026 01:46 | `FD 7F 0E 05 00 00 00 00` | 19 | 1, 16, 20 |

- SID 20 ist das eigene Modul.
- SID 16 ist ein Knoten ohne Batterie, der zum Netz gehört. Er fehlt in jedem Mitschnitt.
- SID 6 und SID 1 fehlen je einmal. Einzelne Antworten gehen in einer Runde also verloren. `D4 10` ist damit eine Momentaufnahme und taugt allein nicht als Nachweis, dass ein Knoten fehlt.
- Gegenprobe: Batterie in SID 16 einsetzen und prüfen, ob SID 16 danach in `D4 10` erscheint.

### P4: `D4 03` zeigt die Mitgliedschaft

Bestätigt.

Die Karte in `D4 03` (Antwort auf `D3 03`) enthält alle Knoten, die zum Netz gehören, auch solche, die gerade nicht erreichbar sind. Beleg: SID 16 ohne Batterie erscheint in allen Mitschnitten von `D4 03`. Erreichbarkeit lässt sich daraus nicht ablesen; dafür dienen `D4 10` (P3), `D4 01` und Antworten auf Fernanfragen.

### P5: Der Init löst eine Netzrunde aus

Vermutet, indirekt belegt.

- Die Antwortkarte in `D4 10` (P3) und die Sperrzeit von 3–4 s (P1) sprechen dafür, dass das Modul nach `D3 19 50 00` alle Knoten anspricht und deren Antworten sammelt.
- Offen ist, was andere Knoten dabei empfangen (etwa `D3 19 50 00 sid` an deren Host). Ein Mitschnitt an einem zweiten Knoten folgt in Stufe 2 **[R9]**.

### P6: Werte in `D2` über mehrere Starts

Beobachtung, Deutung der Felder nach der Protokollanalyse.

| Mitschnitt | d1 BVNL | d2 BVWL | d3 | d4 RSSI | d5 FW | d6–d8 ID | d9 kritisch | d10 Funkfehler | d11 SID | d12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Button im Ruhezustand, vor 01:33 | `3E` | `4F` | `4F` | `57` (87) | `EF` | `A5 B8 14` | `00` | `00` | `14` | `00` |
| 12.10.2026 01:33 | `3E` | `4F` | `4F` | `60` (96) | `EF` | `A5 B8 14` | `00` | `00` | `14` | `00` |
| 12.10.2026 01:46 | `3E` | `4E` | `4F` | `65` (101) | `EF` | `A5 B8 14` | `00` | `00` | `14` | `00` |

- d3 bleibt in allen Mitschnitten `4F`, seine Bedeutung ist offen.
- d11 enthält die eigene SID, gleich d10 in `D4 10` (P2).
- d12 (in R3 als vermutet geführt) ist in allen Mitschnitten `00`.
- RSSI schwankt zwischen den Starts um 14 Stufen, BVWL um eine Stufe.

### P7: Hostantworten `C4`, `C8`, `C6` und `73` kommen beim Anfragenden an

Bestätigt am 12.10.2026, Ende-zu-Ende über das W2-MM geprüft. Die Protokollanalyse führt `73`, `C8` und `C6` bisher als „ungeprüft“.

| Anfrage von SID 15 | Antwort der Brücke |
| --- | --- |
| `C3 00 00 00 0F 0B` | `C4 A5 B8 14 FF 11 03` |
| `C7 A5 B8 14 11 03 06 7E 0F 0D` | `C8 A5 B8 14 06 B8 0B 00 00` (3000 mV) |
| `C5 A5 B8 14 11 03 74 7E 0F 0E` | `C6 A5 B8 14 74 00 00 00 00` |
| `72 A5 B8 14 11 03 00 7E 0F 0F` | `73 A5 B8 14 11 03 00 00` |

- Das RM quittiert jede dieser Antworten mit genau einem `46`.
- Das W2-MM zeigt danach für Knoten 20: Modell WST-630, Geräte-ID 14B8A5, Klasse `FF` („all“), Batterie 3000 mV, Fault details `0x0000` (alle Bits OK), Fault events „none“. Damit sind die Formate der Hostantworten in der Protokollanalyse bestätigt, einschließlich mV = d6<<8 | d5 in `C8` und FS = d7<<8 | d6 in `73`.
- Das W2-MM zeigt zusätzlich RM-Spannung 3,02 V, Firmware `EF`, „Status RSSI“ 91 und eine Remote-SID-Map gleich `D4 03`. Diese Werte liefert das RM selbst, ohne den Host: Die Brücke sah dazu keinen Rahmen. 3,02 V passt zur Formel U = 1048,576 / (BVNL + 285) V mit BVNL `3E`. Das `D2` dieses Starts zeigte `3D` (3,03 V).
- Die Sequenznummern von SID 15 an die Brücke laufen 11, 13, 14, 15. Folge 12 war vermutlich eine dieser Anfragen, die das RM selbst beantwortet (etwa der Remote-Status, den ein Host mit `D3 06 14 00` anfragt). Vermutet.
- Die Zeitstempel dieses Logs vergibt der Log-Client beim Empfang. Ohne `api:` liest er über `/events` des web_server, und die Zeilen kommen gebündelt an. Laufzeiten misst erst ein Log über die native API.

### P8: SID 15 ist ein W2-MM-Service-Dongle außerhalb der SID-Maps

Bestätigt durch den Nutzer und das Log vom 12.10.2026.

- SID 15 gehört zu einem FireAngel-W2-MM-Service-Dongle für die Diagnose. Er sendet Anfragen ins Netz und empfängt die Antworten.
- SID 15 steht weder in `D4 03` (Mitgliedschaft, P4) noch in `D4 10` (Init-Antworten, P3). Das W2-MM ist also Teilnehmer ohne Eintrag in den SID-Maps der RMs. Das passt zur Protokollanalyse: Ein solcher Teilnehmer kann seine SID an ein neu eingelerntes RM verlieren, dort beobachtet als Doppelvergabe von SID 15.
- Die geräteadressierten Anfragen des W2-MM (`C7`, `C5`, `72`) tragen vor SID und seq das zusätzliche Byte `7E`, die ID-Anfrage `C3` trägt es nicht. Die Protokollanalyse nennt dieses Byte für „Anfragen mancher Absender“, ihr Beispiel `72 … 00 7E 0F 04` stammt ebenfalls von SID 15. Der Absender mit dem zusätzlichen `7E` ist damit, zumindest in diesem Netz, das W2-MM.
- Erkundungsablauf des W2-MM gegen die Brücke: `C3`, `C7`, `C5`, `72` in dieser Reihenfolge, laut Log zwischen 02:52 und 02:56 (Zeiten gebündelt, siehe P7).
- Vorschlag für Stufe 2, zur Entscheidung: Anfragen von SIDs außerhalb von `D4 03` lösen keine Erkundung aus, und das Register legt für sie keinen Melder an. Die Brücke protokolliert sie als Dienstgerät.

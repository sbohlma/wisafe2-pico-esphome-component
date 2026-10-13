# Umsetzungsplan: W2-Modulprotokoll auf der Pico-2-W-Brücke

Stand: 13.10.2026, Entscheidungen E1–E12 getroffen, E3 und E7 überarbeitet, Stufe 0 umgesetzt und auf der Hardware abgenommen, Init-Sequenz korrigiert (Abschnitt 9), Stufe 1 bis auf die Testtaste abgenommen, Stufe 2 umgesetzt, nach der ersten Hardware-Prüfung überarbeitet (Zeitlimit, Erkundung und Netzdetails getrennt) und nach der zweiten nachgeschärft (Zeitlimit 240 s, Abfragen je Modell und Erreichbarkeit), Stufe 3 umgesetzt und in der Abnahme (erste Hardware-Prüfung ausgewertet). Grundlage ist [PROTOCOL_ANALYSIS_W2_MODULE.md](https://github.com/sbohlma/FireAngel/blob/main/wisafe-mm/docs/PROTOCOL_ANALYSIS_W2_MODULE.md) (im Folgenden „Protokollanalyse“) mit den dort genannten Quellen, vor allem NikNakks [fireangel-pro-connected-esphome](https://github.com/NikNakk/fireangel-pro-connected-esphome), Tho85s [ws2mqtt](https://github.com/Tho85/ws2mqtt) und [wisafe-mm](https://github.com/sbohlma/FireAngel/tree/main/wisafe-mm).

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
| Fernanfrage | `D3 06 sid 00/01`, `72`, `C7`, `C5`, `C1`, `70` | `46` binnen 150 ms (bei `47`: SID unbekannt), Antwort asynchron nach 3 s bis knapp 3 min (P10, P16), Zeitlimit per YAML, Vorgabe 240 s, nur eine gleichzeitig |

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
- Nimmt das Modul kein einziges Byte eines Befehls an, wiederholt die Sendeplanung ihn bis zu 120-mal im Abstand von mindestens 500 ms, also etwa 60 s (anfangs 20-mal; die Sperrphasen während laufender Fernanfragen dauerten bis zu 18 s, P11). Kern 1 meldet dazu den Grund („CS belegt“, „Modul taktet nicht“, „Byte unvollständig“).
- Der Status-Sensor zeigt den Zustand des Moduls: „Init läuft“, „Modul bereit“, „keine Antwort“, „ungekoppelt“.
- Host-Tests für Rahmen und Sendeplanung, darunter maskierte Rahmen und Antworten, die sich überholen.

Abnahme auf der Hardware (erledigt am 12.10.2026):

- Init mit Quittung `46`, danach `D4 10`, dann `D1` und `D3 03` automatisch und per Button jeweils im ersten Versuch beantwortet. Erfüllt.
- Mitschnitt nach einem Neustart: Flutet der Init der Pico-Brücke das Netz? Indirekt belegt: Der Init löst eine Netzrunde aus, `D4 10` meldet die antwortenden Knoten (Abschnitt 9, P3 und P5). In E2 nachgetragen. Ein Mitschnitt an einem zweiten Knoten folgt bei den Tests von Stufe 2 **[R9]**.

### Stufe 1: Vollständiges Parsing, Identität und manuelle Abfragen

Stand: im Code umgesetzt (`catalog`, `messages`, `frame_analyzer`, `duplicate_filter`, `host_identity`, Aktionen in `automation.h`, Host-Tests unter `tests/host`). Die Abnahme auf der Hardware ist bis auf die Testtaste erfüllt (vier von fünf Punkten, Stand 12.10.2026).

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

- Geräte-ID und Modell für `query_battery`, `query_fault_details` und `query_fault_events` kommen aus dem Melderregister (`registry`, Stufe 2). Die Aktionen brauchen damit nur die SID, sobald der Knoten einmal mit Geräte-ID und Modell bekannt ist. Der Zwischenspeicher `node_cache` aus der ersten Fassung von Stufe 1 ist im Register aufgegangen.
- Antworten werden genau zugeordnet: `D4 06` über die SID, `C4` über die SID am Rahmenende, `C8`, `73` und `C6` über die Geräte-ID. Kopien von Hostantworten auf Anfragen anderer Knoten beenden so keine eigene Anfrage.
- `D4 01` mit der Ziel-SID beendet `D3 06` als „nicht erreichbar“ **[E8]**.
- `C1` trägt keine Geräte-ID. Wird es geflutet, weil das Ziel abgenommen ist, antwortet auch die Brücke mit `C2` (höchstens einmal je 60 s) **[R12]**.
- Neue YAML-Optionen: `device_id`, `model` (Vorgabe `0x0311`), Sensoren `last_reply`, `rm_sid`, `rm_battery`, `rm_rssi`, `rm_radio_faults`, `rm_firmware`, `rm_device_id`, `rm_paired`.

Abnahme (Stand 12.10.2026, Logs 02:48–03:02 und 03:39–04:30):

- Testtaste an einem Melder ergibt einen vollständig gedeuteten `70`-Rahmen. Offen, folgt mit dem Meldertest.
- Melder vom Sockel nehmen ergibt `71` mit Flags. Erfüllt: SID 14 und SID 26 mit Flags `01` (abgenommen, kalibriert). SID 14 zusätzlich mit beiden Richtungen in einem Lauf: `71 3D E4 2F 7C 04 01 0E 0A` um 04:29:37 (Folge 10), zurück auf dem Sockel `71 … 05 0E 0B` um 04:30:30 (Folge 11, auf Sockel, kalibriert). Die Kopie von Folge 10 in derselben Millisekunde erkennt der Duplikatfilter.
- Handabfragen liefern `D4 06`, `C4`, `C8`, `73`. Erfüllt über `api: actions` an SID 14 (ST-630-DE, Gerät 2FE43D): `C4` nach 3,2 s, `D4 06` nach 21 s, `C8` mit 2954 mV nach 6,4 s, `73` mit FS `0000` nach 9,5 s. Jede Antwort beendet genau ihre Anfrage, Geräte-ID und Modell für `C7` und `72` kamen aus den vorher gehörten Antworten (Abschnitt 9, P9).
- `D1` zeigt im Feld d6–d8 die ID aus der YAML (`A5 B8 14`), ohne Warnung. Erfüllt.
- Eine ID-Anfrage eines anderen Knotens an die Brücke erhält `C4`. Erfüllt: Der W2-MM-Dongle (SID 15) fragte `C3`, `C7`, `C5` und `72` und erhielt `C4`, `C8`, `C6` und `73`. Das W2-MM zeigt die Brücke korrekt an (Abschnitt 9, P7 und P8).

### Stufe 2: Melderregister und Netzerkundung

Stand: im Code umgesetzt (`registry`, `discovery`, `node_store`, Buttons in `buttons.h`, Host-Tests `test_registry.cpp` und `test_discovery.cpp`). Nach der ersten Prüfung auf der Hardware am 12.10.2026 überarbeitet: Zeitlimit für Fernantworten per YAML einstellbar, Erkundung und Netzdetails als getrennte Abläufe (siehe „Überarbeitung nach der ersten Hardware-Prüfung“). Nach der zweiten Prüfung am 13.10.2026 nachgeschärft: Zeitlimit 240 s, kein `C7` an den WHT-630, unerreichbare SIDs nur in der Erkundung, `C5` nur bei gemeldeten Fehlern (siehe „Überarbeitung nach der zweiten Hardware-Prüfung“). Die weitere Abnahme folgt.

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

Abgefragt wird in zwei getrennten Abläufen, beide beginnend mit `D3 03` (Liste der SIDs):

| Ablauf | Zweck | Abfragen je SID |
| --- | --- | --- |
| Erkundung | Kennung für `nodes.bin`: Geräte-ID, Klasse, Modell. Ändert sich nur beim Tausch eines Melders. Dazu die Erreichbarkeit der als unerreichbar geführten SIDs | `D3 06 sid 00` ohne bekannte Geräte-ID und an jede unerreichbare SID, danach `D3 06 sid 01` (`C4`) nur ohne bekannte Klasse. RMs ohne Melder beantworten `C4` nicht und bleiben „Typ unbekannt“ |
| Netzdetails | Werte, die sich ändern | `D3 06 sid 00` an jede erreichbare SID (RM-Batterie, RSSI), dann `C7` (Batterie, ohne WHT-630, P13) und `72` (Fehlerdetails) an jeden Melder. `C5` nur, wenn `73` Fehler meldet, und nur an WST-630 und ST-630-DE |

Bleibt `D3 06 sid 00` ohne Antwort, entfallen die übrigen Schritte dieser SID. Unerreichbare SIDs überspringen die Netzdetails, bis sie in einer Erkundung wieder antworten. Laufen Erkundung und Netzdetails gemeinsam, bekommt eine SID, die in der Erkundung antwortet, gleich die Netzdetails.

Dauer: Die Antworten kamen im eigenen Netz nach 4 s bis über 3 min (P10). Die Erkundung braucht je Melder meist unter einer Minute (`C4` nach 9–34 s), jede SID ohne Antwort kostet das volle Zeitlimit. Die Netzdetails brauchen für 21 SIDs rund eine Stunde, je Melder 1,5 bis 4 min, weil der erste Kontakt bis knapp 3 min und `C6` oft 30–130 s dauert (P16).

Wann erkundet wird **[E5]**, gesteuert über die YAML-Option `auto_discovery`:

| | `auto_discovery: false` (Entwicklung und Test, Vorgabe) | `auto_discovery: true` (später) |
| --- | --- | --- |
| Start | Register aus `nodes.bin` laden, dann Erkundung nur für SIDs ohne Eintrag | wie links |
| Button „Discover Network“ | Erkundung der SIDs ohne vollständigen Eintrag, Register bleibt erhalten | wie links |
| Button „Netzdetails“ | Netzdetails aller SIDs | wie links |
| Periodisch | keine Abfragen | `D3 06` je SID alle 19 h, `C7` je Melder alle 24 h, über den Tag verteilt, Intervalle per YAML |
| Ereignisgesteuert | keine Abfragen | nach `71` mit Flag 0x02: `72`, und `C5`, wenn `73` Fehler meldet; nach `D4 04` oder `D4 09` mit unbekannter SID: Erkundung dieser SIDs |

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
  | 1 | Flags: Bit 0 Gateway-RM, Bit 1 Typ unbekannt (RM ohne Melder), Bit 2 eigenes RM der Brücke |
  | 2–4 | Geräte-ID in Paket-Reihenfolge |
  | 5–6 | Modell-ID in Paket-Reihenfolge |
  | 7 | Klasse |

- Passen Kennung, Version oder CRC nicht, startet die Brücke mit leerem Register und erkundet alle SIDs.
- Geschrieben wird nur bei Änderung von Geräte-ID, Modell oder Klasse, gebündelt mit einer Verzögerung von einigen Sekunden.
- Button „Rediscover Network“: löscht `nodes.bin` und das Register im RAM und startet eine vollständige Erkundung.

Umsetzung im Detail:

- `registry` (Kern 0, ohne ESPHome-Header, auf dem Host getestet) führt 50 Plätze. Gespeichert werden SID, Flags, Geräte-ID, Modell und Klasse; alles Weitere lebt nur im RAM: im Netz, Erreichbarkeit, ausgebliebene Antworten, zuletzt gesehen, RM-Werte aus `D4 06`, Melder-Batterie, Fehlerflags, Fehlerdetails, Fehlerereignisse, letzter Test, Alarm und Stummschaltung.
- Regeln der passiven Auswertung:
  - Die Klasse kommt nur aus `70`, `C4` und `C2`; `71`, `73`, `C8` und `C6` liefern Geräte-ID und Modell.
  - Anfragen anderer Knoten (`72`, `C3`, `C5`, `C7`, `C1`) legen keinen Platz an. Der W2-MM-Dongle (SID 15, P8) erscheint damit nicht im Register.
  - Meldet eine SID eine neue Geräte-ID, setzt das Register Modell, Klasse und Melderwerte zurück und protokolliert eine Warnung. Die nächste Erkundung ermittelt sie neu.
  - `61` setzt nur „stummgeschaltet“; als beendet gilt ein Alarm erst mit `51`.
  - `D4 09` mit SID 0 und der Gateway-ID `01 02 03` wird übergangen, eine Gateway-ID an einer echten SID setzt das Flag „Gateway-RM“.
  - `D2` und `D4 10` liefern die eigene SID. Das eigene RM bekommt das Flag „eigenes RM“ (Bit 2) und die Identität aus der YAML und wird nie abgefragt.
- Erreichbarkeit **[E8]**: Jede Nachricht der SID und jede Antwort setzt „erreichbar“ und den Zähler zurück. `D4 01` setzt sofort „nicht erreichbar“, sonst nach zwei Zeitüberschreitungen in Folge. Bleibt nur `C4` aus, zählt das nicht: Das Register setzt dann „Typ unbekannt“.
- `discovery` plant die Abfragen je SID in der Reihenfolge Remote-Status, ID-Information, Batterie, Fehlerdetails, Fehlerereignisse. Vor jeder Abfrage wird erneut geprüft, ob sie nötig ist: Remote-Status der Erkundung nur ohne Geräte-ID oder bei unerreichbarer SID, ID-Information nur ohne Klasse und nie an ein RM ohne Melder, `C7` nur für Melder außer dem WHT-630, `72` nur für Melder, `C5` nur für WST-630 und ST-630-DE und nur nach einem `73` mit FS ungleich 0 zur eigenen Abfrage, Gerätefragen nicht an unerreichbare Knoten. `D3 06 sid 00` läuft je SID höchstens einmal, auch wenn Erkundung und Netzdetails gemeinsam anstehen. Scheitert der Remote-Status oder lehnt das RM mit `47` ab, entfallen die übrigen Schritte dieser SID.
- Beide Abläufe beginnen mit `D3 03`. Die Erkundung (Start, „Discover Network“, „Rediscover Network“, neue SIDs aus `D4 04`/`D4 09`) nimmt nur SIDs ohne vollständigen Eintrag, die Netzdetails alle SIDs der Karte außer der eigenen. Vollständig heißt: Geräte-ID und Klasse bekannt, oder Gateway-RM, oder Typ unbekannt. Das Log zählt mit („Erkundung SID 3 (2/21): ID-Information“, „Netzdetails SID 3 (2/21): Batterie“) und gibt am Ende Dauer und Register aus.
- Periodik (nur `auto_discovery: true`): erste Runde 15 min nach dem Start, danach reihum je ein Knoten im Abstand Intervall geteilt durch die Zahl passender Knoten. Remote-Status gilt allen Netzmitgliedern außer dem eigenen RM, also auch unerreichbaren, damit sie wieder als erreichbar erkannt werden. Batterie gilt nur erreichbaren Meldern. Während einer Erkundung pausiert die Periodik.
- Erkundung und Periodik laufen über denselben Scheduler wie die Handabfragen: eine offene Fernanfrage, Hostantworten mit Vorrang. Eine neue Abfrage startet erst, wenn der Scheduler leer ist.
- `node_store` (nur Gerät) nutzt LittleFS von arduino-pico im Dateisystembereich, den ESPHome für den RP2350 reserviert (1 MB). Geschrieben wird `/nodes.tmp` und dann in `/nodes.bin` umbenannt, damit ein Stromausfall die alte Datei erhält. ESPHome-Preferences scheiden aus, sie fassen nur 512 Byte.
- Schreibzeitpunkt **[R5]**: frühestens 5 s nach der ersten Änderung, nur ohne halben Rahmen, ohne laufenden Sendevorgang und mindestens 1 s nach dem letzten Rahmen. Bei offener Fernanfrage wartet die Brücke bis zu 60 s. Das Log nennt Dauer und belegte Plätze jedes Schreibvorgangs.
- Neue YAML-Optionen: `auto_discovery` (Vorgabe `false`), `remote_status_interval` (Vorgabe 19 h), `battery_interval` (Vorgabe 24 h), jeweils 10 min bis 20 Tage, `remote_timeout` (Vorgabe 240 s, 30 s bis 10 min). Buttons `discover_button`, `rediscover_button`, `details_button`, `registry_log_button` (gibt das Register ins Log aus).

Erste Prüfung auf der Hardware (Log vom 12.10.2026, 07:58–10:12, erste Fassung mit 55 s Zeitlimit und gemeinsamer Erkundung):

- Erster Start mit leerem Register: Die Erkundung aller 21 SIDs dauerte 1897 s (32 min). Am Ende standen 22 Knoten im Register, davon 10 vollständig. 16 SIDs beantworteten den Remote-Status, sieben davon erst nach dem Zeitlimit (SID 2, 8, 9, 12, 13, 17, 26); für sie entfielen ID-Information, Batterie und Fehler. Ohne Antwort blieben SID 5, 6, 10 und 16. Für SID 18 ging die Anfrage nicht hinaus, weil das Modul 10 s lang keine Bytes annahm (P11).
- Späte Antworten trug das Register passiv ein (Geräte-ID, RM-Werte), danach auch in `nodes.bin`.
- `nodes.bin`: 13 Schreibvorgänge mit 47–138 ms, dabei keine Rahmenfehler und keine `Roh:`-Zeilen. **[R5]** bisher unauffällig.
- Zweite Erkundung per „Discover Network“ um 10:03: Sie begann mit SID 26 als „0/21“ und wäre nie als abgeschlossen gemeldet worden. Ursache: Die zuletzt bearbeitete SID der vorigen Runde wurde ohne Zählung fortgesetzt und blieb als offen vermerkt. Behoben, mit Host-Test.

Überarbeitung nach der ersten Hardware-Prüfung:

- Zeitlimit für Fernantworten 180 s statt 55 s, per YAML `remote_timeout` einstellbar. Mit dem kurzen Limit startete die nächste Anfrage, während die vorige noch im Netz unterwegs war; die Antworten stauten sich (P10).
- Wiederholung nicht angenommener Befehle bis etwa 60 s statt 10 s (P11).
- Erkundung und Netzdetails getrennt: Die Erkundung liefert nur die Kennung für `nodes.bin` und läuft beim Start. Die Netzdetails laufen per Button `details_button` und, mit `auto_discovery: true`, verteilt über die periodischen Abfragen.
- Handabfragen mit fester Geräte-ID nehmen das Modell aus dem Eintrag mit Modell, falls zwei SIDs dieselbe ID führen (P14).

Abnahme:

- Start der überarbeiteten Firmware mit der vorhandenen `nodes.bin` (17 Plätze): Erkundung nur der unvollständigen SIDs, erwartet 5, 6, 8, 9, 10, 12, 13, 16, 17, 18. Für 8, 9, 12, 13, 17 ist die Geräte-ID bekannt, dort läuft nur `C4`. Prüfen, ob alle Antworten innerhalb von 180 s kommen und die Sperrphasen „Modul taktet nicht“ ausbleiben.
- „Netzdetails“ per Button: Dauer, Anteil der Antworten, Zahl der Zeitüberschreitungen.
- „Rediscover Network“ oder erster Start ohne `nodes.bin`: „nodes.bin fehlt, Register startet leer“, danach erkundet die Brücke alle 21 SIDs außer der eigenen. Am Ende stimmt das Register mit der bekannten Melderliste überein (Anzahl, IDs, Modelle), SID 16 ist „nicht erreichbar“, „nodes.bin gespeichert“ erscheint.
- Nach einem Neustart: „Register aus nodes.bin geladen“ und „Register vollständig, keine Erkundung nötig“. Es läuft keine Fernanfrage.
- „Rediscover Network“ löscht Register und Datei und baut beides neu auf.
- Bei den Schreibvorgängen prüfen, ob Rahmen verloren gehen (Rahmenfehler im Log, Lücken in den Folgenummern) **[R5]**.

Zweite Prüfung auf der Hardware (Log vom 13.10.2026, 02:12–02:35, Netzdetails noch laufend):

- Start mit `nodes.bin` (21 Knoten): Die Erkundung nahm nur SID 16 und endete nach 48 s mit `D4 01` als „nicht erreichbar“. „Discover Network“ um 02:17 lief ebenso, 44 s. Kein Schreibvorgang, weil sich keine Kennung änderte.
- „Netzdetails“ um 02:19: SID 0 bis 5 in 15,5 min. Alle Antworten kamen innerhalb von 180 s, die längste nach 175,7 s (P16). Keine Sperrphase „Modul taktet nicht“, keine Warnung außer SID 16.
- Der WHT-630 an SID 3 meldet in `C8` erneut 0 mV (P13). Die Brücke zeigt „Batterie 0 mV“.
- Weitere Befunde aus diesem Log: `D4 01` nach 44–48 s (P12), Laufzeiten mit genau einer offenen Anfrage (P16), 500-ms-Raster der Fernantworten (P17).

Überarbeitung nach der zweiten Hardware-Prüfung:

- Zeitlimit für Fernantworten 240 s. Die längste Antwort kam nach 175,7 s, nur 4 s unter dem bisherigen Limit (P16). Unerreichbare Ziele kosten trotzdem nur etwa 45 s, weil `D4 01` das Warten beendet (P12).
- Kein `C7` an den WHT-630, seine `C8` enthalten keine Spannung (P13).
- Unerreichbare SIDs: Die Erkundung fragt bei jedem Lauf ihren Remote-Status. Die Netzdetails überspringen sie, solange keine Antwort kam.
- `C5` nur bei Bedarf: erst wenn `73` zur eigenen Abfrage Fehler meldet. Das spart je fehlerfreiem Melder die längste Anfrage (`C6` 32–127 s, P16).
- Host-Tests ergänzt (Netzdetails ohne unerreichbare SIDs, WHT-630 ohne Batterie, `C5` nach `73` mit Fehlern).

### Stufe 3: Melder als Geräte in Home Assistant

Stand: im Code umgesetzt (`node_view`, `detector`, Integration in `wisafe2.cpp`, Schema in `__init__.py`, Host-Tests `test_node_view.cpp`; alle Host-Tests zusammen 818 Prüfungen). ESPHome-Build mit 21 Meldern als Sub-Devices ohne Warnungen. Die Abnahme auf der Hardware läuft, erste Prüfung siehe unten.

Ziel: Jeder Melder erscheint mit eigenen Entitäten über die native API **[E6]**.

- Die Melder werden in der YAML aufgelistet, mit SID oder Geräte-ID und einem Namen.
- Nach einer Erkundung loggt die Brücke für alle gefundenen Melder einen fertigen YAML-Block als Vorlage.
- Ein Text-Sensor nennt die Knoten im Register, die noch nicht in der YAML stehen. Geplant war das ganze Register als JSON; Home Assistant begrenzt Zustände aber auf 255 Zeichen, das reicht für 22 Knoten nicht (Nachtrag in E6).

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
- nicht zugeordnete Melder (statt Register als JSON)
- Buttons „Discover Network“, „Rediscover Network“ und „YAML-Vorlage“

Home-Assistant-Ereignisse für Alarm, Alarmende, Stummschaltung, Test und Fehlerbericht, damit Automationen darauf reagieren können.

Regeln für den Alarmzustand (übernommen aus NikNakk und der Protokollanalyse):

- Er gilt nach dem Start als unbekannt.
- Er wechselt nur durch `50` und `51`.
- `61` (Stummschaltung) beendet ihn nicht.
- Abfragen setzen ihn nie auf „aus“.

Umsetzung im Detail:

- `node_view` (ohne ESPHome-Header, auf dem Host getestet) leitet aus einem Registerplatz die Zustände ab, jeweils mit „unbekannt“, solange keine passende Nachricht kam:
  - Alarm aus `50`/`51`.
  - Fehler aus `71` Flag 0x02 oder `73` mit FS ungleich 0.
  - Sockel-Problem aus `71` ohne Flag 0x04.
  - Melder-Batterie schwach aus `71` Flag 0x08 oder dem Batterie-Bit in `73` (je Modell; bei W2-SVP-630, SVP-630-B und W2-LFS-630 ohne Bit).
  - RM-Batterie schwach aus `71` Flag 0x20 oder dem RM-Batterie-Bit in `73`.
  - Im Netz: in der SID-Map und nicht unerreichbar.
  - Dazu die Texte für Modell, Fehlerdetails, letzten Test, letztes Ereignis, Zeitstempel und die YAML-Vorlage.
- Zuordnung (`Registry::resolve`): über die Geräte-ID, sonst über die SID. Mit beiden Angaben gilt die SID nur, solange dort keine andere Geräte-ID steht. Bei zwei SIDs mit derselben ID gilt der Eintrag mit Modell (P14). Die Zuordnung wird nach jeder Änderung einer Kennung neu berechnet.
- `Detector` (ESPHome) hält die Entitäten eines Melders. Geändert wird gesammelt in `loop()`: Nachrichten und Abfrageergebnisse markieren ihre SID, Karten und Zuordnungsänderungen alles. Veröffentlicht werden nur geänderte Werte. „Im Netz“ erst nach der ersten SID-Map, „zuletzt gesehen“ höchstens einmal je Minute.
- Zeitangaben („zuletzt gesehen“ als Text-Sensor mit `device_class: timestamp`, Uhrzeit des letzten Tests) nutzen die Systemzeit, die eine `time`-Komponente stellt. Ohne gestellte Uhr bleiben sie leer bzw. ohne Uhrzeit.
- Ereignisse: Je Melder und netzweit eine Event-Entität mit den Typen `alarm`, `alarm_ende`, `stummschaltung`, `test`, `fehlerbericht`. Sie folgen den Funknachrichten `50`, `51`, `61`, `70`, `71` nach dem Duplikatfilter. Der Text-Sensor „letztes Ereignis“ zeigt sie lesbar, etwa „Alarm: SID 14 (Flur), Rauch“.
- Button „Status aktualisieren“ je Melder: Remote-Status, Batterie und Fehlerdetails über die Planung der Netzdetails, mit denselben Regeln (kein `C7` an den WHT-630, `C5` nur bei Fehlern).
- YAML: `detectors` mit `sid` und/oder `device_id`, `name` und optional `device` (Sub-Device aus `esphome: devices`). Ohne `device` tragen die Entitäten den Meldernamen als Präfix. Jede Entität entsteht ohne Angabe, lässt sich anpassen oder mit `false` abwählen. Name und Sub-Device werden vor der Prüfung der Entitäten gesetzt, damit die Duplikatprüfung von ESPHome greift. Doppelte SIDs oder Geräte-IDs in `detectors` weist die Prüfung ab.
- YAML-Vorlage: nach jeder Erkundung und per Button, nur für Knoten ohne Eintrag. Sie erscheint in kurzen Abständen als mehrere Logmeldungen, zuerst `esphome: devices`, dann `wisafe2: detectors`. Die Alarm-Entität bekommt `device_class: heat` bzw. `carbon_monoxide` nach der Klasse.
- Speicher **[R6]**: 21 Melder mit allen Entitäten (313) und acht netzweiten Entitäten belegen 16 KB RAM und 27 KB Flash mehr als ohne Melder; die Firmware nutzt damit 103 KB von 512 KB RAM.

Abnahme:

- Alle Melder erscheinen in Home Assistant, mit `device` als eigene Geräte.
- Testtaste und Sockel-Entnahme verändern die passenden Entitäten und lösen Ereignisse aus.
- Die YAML-Vorlage nach der Erkundung lässt sich übernehmen; danach ist „nicht zugeordnete Melder“ leer.
- Antwortzeit der API und freier Speicher mit allen Entitäten im Betrieb **[R6]**.

Erste Hardware-Prüfung am 13.10.2026 (Log über die native API, 08:10–08:35, drei Melder in `detectors`):

- Start mit 21 Knoten aus `nodes.bin`. Die Erkundung fragte nur SID 16 ab, `D4 01` nach 43,9 s.
- Zuordnung: Die drei Melder aus der YAML fanden ihre Plätze. „Im Netz“ kam nach der ersten SID-Map, „Anzahl Knoten“ 22, „nicht zugeordnete Melder“ nannte 16 SIDs; das eigene RM und die RMs ohne Melder (SID 1 und 24) zählten richtig nicht mit.
- „Status aktualisieren“ an SID 26 (ST-630-DE): `D4 06` nach 5,8 s, `C8` (3022 mV) nach 12,7 s, `73` (FS 0000) nach 20,5 s; kein `C5`, wie vorgesehen. Die Entitäten erhielten ihre Werte, „zuletzt gesehen“ mit Uhrzeit über SNTP.
- Sockel-Entnahme an SID 26: `71` mit Flags 01 setzte „Sockel-Problem“ auf ON, `71` mit Flags 05 nach 69 s wieder auf OFF. Beide Male kamen das Melder-Ereignis und das netzweite Ereignis `fehlerbericht` sowie „letztes Ereignis“.
- Im Log fehlte beim ersten Fehlerbericht die Zeile `[S][event]`. Ursache ist der Log-Client von ESPHome (`aioesphomeapi`, `log_runner.py`): Er übergeht den ersten Zustand jeder Entität nach dem Verbinden als Anfangsstand. Events haben keinen Anfangsstand, also verschluckt er das erste Ereignis jeder Event-Entität. Die Firmware löst es aus und die API überträgt es sofort; Home Assistant erhält es. Beim netzweiten Event zeigte sich dasselbe: Sichtbar wurde erst das zweite Ereignis (`test` um 08:35).
- Zweite Erkundung per Button: `D4 01` für SID 16 erst nach 120,8 s (Nachtrag in P12). Danach die YAML-Vorlage für 16 Melder, der WHT-630 mit `device_class: heat`.
- Der Log-Client setzt vor eingerückte Zeilen einer Meldung deren Präfix (`aioesphomeapi`, `log_parser.py`), Zeilen ohne Einrückung stehen ohne Präfix. Die Kopfzeilen der Vorlage waren Text wie `YAML-Vorlage, SID 0:` und blieben nach dem Entfernen des Präfixes als ungültiges YAML stehen.
- `70` vom W2-MM an SID 15 mit der Gateway-ID legte einen Registerplatz „Gateway, außerhalb der SID-Map“ an und löste das Speichern von `nodes.bin` aus (P18).
- `nodes.bin` speichern dauerte 51 ms. ESPHome warnte „took a long time for an operation (52 ms)“ (R5).

Änderungen danach:

- Kopfzeilen der YAML-Vorlage als YAML-Kommentar (`# SID 14`, `# YAML-Vorlage, Fortsetzung`). Ohne Zeitstempel und Präfix ist die Vorlage gültiges YAML; die README nennt einen regulären Ausdruck zum Entfernen.
- Nachrichten mit der Gateway-ID legen für eine SID ohne Platz keinen Eintrag an (P8, P18). „letztes Ereignis“ nennt bei der Gateway-ID ohne Meldernamen `(Gateway)`, etwa „Test bestanden: SID 15 (Gateway)“.
- README: Hinweise zum ersten Ereignis im Log-Client und zur Warnung beim Speichern.

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
- **Speicher:** Das Register mit 50 Plätzen braucht wenige KB. Gemessen im Build von Stufe 3: 21 Melder mit je 15 Entitäten kosten 16 KB RAM und 27 KB Flash. Wie flüssig die API damit läuft, zeigt die Abnahme **[R6]**.
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
| E5 | Wie oft werden Melder aus der Ferne abgefragt? | Zielbetrieb: Erkundung beim Start, `D3 06` je SID alle 19 h, `C7` je Melder alle 24 h, ereignisgesteuerte Abfragen. In Entwicklung und Test nur beim Start und auf Knopfdruck. Umschaltung über YAML `auto_discovery`. Nachtrag 12.10.2026: Die Erkundung beim Start ermittelt nur die Kennung (Geräte-ID, Klasse, Modell). Batterie, Fehler und RM-Werte kommen über den getrennten Ablauf „Netzdetails“ auf Knopfdruck und über die periodischen Abfragen |
| E6 | Wie werden die Melder zu Entitäten? | Melder in der YAML auflisten, Vorlage aus dem Log, dazu das Register als JSON-Text-Sensor. Alles über die native API. Nachtrag 13.10.2026: Home Assistant begrenzt Zustände auf 255 Zeichen. Statt des JSON-Registers nennt ein Text-Sensor die Knoten ohne Eintrag in der YAML; das ganze Register steht per Button im Log |
| E7 | Melderregister speichern? | Ja, als Binärdatei `nodes.bin` mit festen 50 Plätzen (408 Byte), nur bei Änderung, gebündelt. Button „Rediscover Network“ löscht das Register und erkundet neu |
| E8 | Wann gilt ein Knoten als nicht erreichbar? | Nach `D4 01` oder zwei ausbleibenden Antworten in Folge. Entfernen nur über „Rediscover Network“. Nachtrag 13.10.2026: `D4 01` kommt bei nur einer offenen Anfrage nach 44–121 s und beendet das Warten sofort (P12). Ohne `D4 01` zählt das Ausbleiben der Antwort innerhalb von `remote_timeout` |
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
| R5 | Flash-Schreibvorgänge für `nodes.bin` halten Kern 1 an, SPI-Bytes können verloren gehen | seltene, gebündelte Schreibvorgänge (E7), nur in Ruhephasen ohne laufenden Rahmen und Sendevorgang (umgesetzt in Stufe 2). Bei der Abnahme von Stufe 2 messen, ob das RM verlorene Bytes wiederholt. Gemessen: Ein Schreibvorgang dauert 47–138 ms. ESPHome warnt ab 50 ms („took a long time for an operation“) und hebt die Schwelle danach je Komponente selbst an; die Warnung ist erwartet und in der README erklärt |
| R6 | Viele Entitäten belasten API und Speicher | Speicher gemessen (Stufe 3: 103 KB von 512 KB RAM mit 21 Meldern); jede Entität je Melder mit `false` abwählbar. Das Verhalten der API im Betrieb prüft die Abnahme |
| R7 | Ein Alarm kann übersehen werden, wenn die Brücke ausfällt oder Rahmen verliert | Brücke bleibt Zusatz. Ereignisse für Alarm und Verbindung zur Brücke in Home Assistant überwachen |
| R8 | Das eigene RM hängt nach dem Einlernen eines anderen Knotens | Watchdog: bleibt `46` mehrfach aus, Hinweis im Status. Ein automatischer Neustart des RM braucht eine schaltbare Versorgung |
| R9 | Die Brücke wird nach dem Init zum Empfänger aller Hostantworten. Ob das andere Abfragende stört, ist offen | bei den Tests von Stufe 2 beobachten, im Zweifel E2 neu entscheiden |
| R10 | Mit Klasse `FF` blinkt die LED des Funkmoduls vermutlich alle paar Sekunden | bekannt und beabsichtigt |
| R11 | Netztest und Diagnose lösen ohne Rückfrage aus, auch über Home-Assistant-Automationen | bekannt und beabsichtigt; Funktionen per YAML zuschaltbar, jede Auslösung im Log und als Ereignis |
| R12 | Feste Antworten auf `72`/`C7`/`C5`/`C1` könnten andere Werkzeuge (Gateway, wisafe-mm) über den Zustand der Brücke täuschen | bekannt und beabsichtigt; die Werte entsprechen einem fehlerfreien Gerät |
| R13 | Mit 240 s Zeitlimit kostet jede SID ohne Antwort bis zu 4 min, die Netzdetails dauern damit lange | Erkundung beim Start nur für unvollständige Einträge, Netzdetails nur auf Knopfdruck oder verteilt; unerreichbare SIDs nur in der Erkundung, wo `D4 01` nach 44–121 s das Warten beendet (P12); `C5` nur bei Fehlern; Zeitlimit per YAML anpassbar |

## 8. Nächster Schritt

Hardware-Abnahme mit der Firmware von Stufe 3 (Abschnitt 3) fortsetzen. Geprüft sind Zuordnung, „Status aktualisieren“, Sockel-Entnahme und Ereignisse. Offen: aus Stufe 2 eine vollständige Runde „Netzdetails“ mit 240 s und „Rediscover Network“; für Stufe 3 die YAML-Vorlage übernehmen, Melder als Geräte in Home Assistant prüfen und die Testtaste an einem Melder. Die Testtaste schließt zugleich die Abnahme von Stufe 1 ab. Danach folgt Stufe 4 nach Freigabe.

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
| 5, 12.10.2026 03:40 | `FF 7F 0E 05 00 00 00 00` | 20 | 16, 20 |
| 6, 13.10.2026 02:13 | `FF 6D 0E 05 00 00 00 00` | 18 | 9, 12, 16, 20 |

- SID 20 ist das eigene Modul.
- SID 16 ist ein Knoten ohne Batterie, der zum Netz gehört. Er fehlt in jedem Mitschnitt.
- SID 6 und SID 1 fehlen je einmal, SID 9 und SID 12 einmal gemeinsam. Einzelne Antworten gehen in einer Runde also verloren. `D4 10` ist damit eine Momentaufnahme und taugt allein nicht als Nachweis, dass ein Knoten fehlt.
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
- Umgesetzt in Stufe 2: Anfragen legen keinen Platz im Register an und lösen keine Erkundung aus. Der Dongle erscheint nur im Log. Nachtrag: Der Dongle sendet auch `70` mit der Gateway-ID; seit Stufe 3 legt das ebenfalls keinen Platz an (P18).

### P9: Antworten einer ST-630-DE auf Handabfragen

Beobachtung vom 12.10.2026, Log über die native API (Zeitstempel ohne Bündelung), Ziel SID 14, ST-630-DE, Gerät 2FE43D.

| Anfrage | Antwort | Laufzeit |
| --- | --- | --- |
| `D3 06 0E 01` | `C4 3D E4 2F 81 7C 04 0E 05` | 3,2 s |
| `D3 06 0E 00` | `D4 06 0E 64 8C 8C 7D EF 3D E4 2F 00 00` | 21,0 s |
| `C7 3D E4 2F 7C 04 06` | `C8 3D E4 2F 06 8A 0B 00 00 0E 08` | 6,4 s |
| `72 3D E4 2F 7C 04 00` | `73 3D E4 2F 7C 04 00 00 0E 09` | 9,5 s |

- `C8` meldet 2954 mV (`8A 0B`), genau den Wert, den die Protokollanalyse für eine andere ST-630-DE (SID 11, Gerät 33F0DC) zeigt. Vermutet: Die ST-630-DE meldet bei intakter Batterie einen festen Wert. Gegenprobe: `C7` an weitere ST-630-DE.
- `D4 06`: d3 `64` ergibt nach der Formel 2,72 V (BVNL). d4 und d5 sind beide `8C`. Das entspricht `D2`, wo d3 „oft gleich d2“ ist (P6: `4F`/`4F`), denn D4 06 d3–d12 spiegelt D2 d1–d10. Vermutet: D4 06 d5 ist dasselbe Feld wie D2 d3 und folgt meist BVWL. Damit schließt sich der offene Punkt „D4 06 d5“ der Protokollanalyse zum Teil.
- d6 (RSSI am Ziel) `7D` = 125 liegt über dem in der Protokollanalyse genannten Bereich 68–117 für Melder.
- Die Laufzeit des Remote-Status lag mit 21 s deutlich über den verifizierten 4–6 s und innerhalb der 8–28 s nach dem Umstecken.
- Folgenummern von SID 14: `C4` 5, `C8` 8, `73` 9, `71` 10 und 11. Zwischen `C4` und `C8` fehlen 6 und 7, dazwischen lag nur der Remote-Status. Vermutet: Das Ziel-RM verbraucht für den Remote-Status Folgenummern, obwohl `D4 06` selbst keine trägt. Das passt zur Lücke bei Folge 12 in P7. Gegenprobe: zwei Remote-Status-Abfragen hintereinander und danach `C7` an dasselbe Ziel.
- Sockel-Zeiten: `71 … 01` kam einmal mit einer Kopie in derselben Millisekunde (gleiche SID, gleicher Typ, gleiche Folge). `71 … 05` folgte 53 s später. Die Abstände zum Handgriff hat der Nutzer nicht gemessen.

### P10: Laufzeiten der Fernantworten und Stau bei überlappenden Anfragen

Beobachtung vom 12.10.2026, Log über die native API, 07:58–10:12, zwei Erkundungen mit 55 s Zeitlimit.

| Antwort | Laufzeit nach der Quittung `46` | Anzahl |
| --- | --- | --- |
| `D4 06` | 4–162 s | 20 |
| `C4` | 9–34 s | 9 |
| `C8` | 7–68 s | 10 |
| `73` | 21–100 s | 8 |
| `C6` | 26–219 s | 7 |

- Auch ohne weitere Anfrage in den ersten 55 s kamen Antworten erst nach 86 s (`D4 06` von SID 2) und 100 s (`73` von SID 3). Die Protokollanalyse nennt bis etwa 50 s.
- Startet nach dem Zeitlimit die nächste Anfrage, kommen die Antworten gebündelt: etwa `D4 06` von SID 9 nach 140 s, drei Sekunden vor der Antwort von SID 11, die zwei Anfragen später gesendet wurde. `C6` von SID 4 kam nach 219 s, während drei weitere Remote-Status-Anfragen liefen. Vermutet: Das Netz arbeitet Fernanfragen nacheinander ab, eine neue Anfrage verlängert die Laufzeit der offenen.
- Einige Antworten kamen nie, etwa `73` von SID 7 und SID 26 und der Remote-Status von SID 10, jeweils nachdem die Brücke nach 55 s die nächste Anfrage gesendet hatte.
- `73` und `C6` brauchen meist deutlich länger als `C4` und `C8`.
- Folgerung für einen Host: auf jede Antwort warten, Zeitlimit deutlich über 100 s. Die Brücke nutzt seit dem 13.10.2026 240 s. Messungen mit nur einer offenen Anfrage stehen in P16.

### P11: Das Modul nimmt während laufender Fernanfragen zeitweise keine Bytes an

Beobachtung vom 12.10.2026, viermal im Log, vermutet.

- Um 08:11:02 für 3 s, um 08:25:50 für 18 s, um 10:04:43 für 10 s und um 10:09:12 für 13 s antwortete das Modul auf das Heben von IRQ nicht mit einem Takt („Modul taktet nicht“), wie in der Sperrzeit nach dem Init (P1).
- Jedes Mal war mindestens eine frühere Fernanfrage nach dem Zeitlimit der Brücke noch im Netz unterwegs, ihre Antwort kam später oder nie. Die Sperre trat nur auf, solange solche Anfragen offen waren.
- Folgerung für einen Host: nicht angenommene Befehle bis etwa eine Minute lang wiederholen. Die Brücke tut das seit der Überarbeitung von Stufe 2.

### P12: `D4 01` meldet eine nicht erreichbare SID nach 44–121 s

Bestätigt am 13.10.2026, Gegenprobe zum Befund vom 12.10.2026.

- Am 12.10.2026 kam in gut zwei Stunden Log kein `D4 01`, obwohl SID 16 (ohne Batterie) und SID 5, 6 und 10 auf `D3 06 sid 00` innerhalb von 55 s nie antworteten. Damals liefen meist mehrere Fernanfragen gleichzeitig (P10).
- Am 13.10.2026 lief jeweils nur eine Anfrage. `D3 06 10 00` an SID 16 ergab zweimal `D4 01 00 00 00 01 00 00 00 00 00`, nach 47,4 s und nach 44,1 s. Das Format entspricht der Protokollanalyse: d2 Grund (`00`), d3–d10 SID-Bitkarte, hier Byte 2 Bit 0 = SID 16.
- SID 5 antwortete am 13.10.2026 nach 175,7 s. Ihr Schweigen am 12.10.2026 lag also am Zeitlimit von 55 s.
- Bis zu einer Antwort nach 176 s kam für SID 4 und SID 5 kein `D4 01`. Vermutet: Das RM erzeugt `D4 01` selbst, wenn es das Ziel im Netz gar nicht findet. Bei einem erreichbaren Ziel wartet es mindestens 176 s und meldet in dieser Zeit kein `D4 01`. Dazu passt das Zeitraster in P17.
- Vermutet: Überlappende Anfragen unterdrücken `D4 01`.
- Nachtrag vom 13.10.2026, Log 08:10–08:35, Firmware von Stufe 3, jeweils nur diese eine Anfrage offen: `D3 06 10 00` an SID 16 ergab `D4 01` beim Start nach 43,9 s (08:10:48 → 08:11:31,9) und bei der zweiten Erkundung desselben Laufs nach 120,8 s (08:17:46,1 → 08:19:46,9). Die letzte Antwort davor (`73` von SID 26) kam 2 min 18 s vorher. Die Spanne reicht damit von 44 s bis 121 s. Wovon sie abhängt, ist offen; 120,8 s sind kein Vielfaches der 44–48 s.
- Folgerung für einen Host: `D4 01` beendet das Warten auf die genannte SID. Fehlt es, kann die Antwort noch nach drei Minuten kommen. Ein Zeitlimit unter 121 s kann einem `D4 01` zuvorkommen.

### P13: Der WHT-630 liefert in `C8` keine Batteriespannung

Beobachtung vom 12.10.2026, SID 3, WHT-630, Gerät 132F08, vermutet.

| Uhrzeit | Antwort | d5–d6 |
| --- | --- | --- |
| 08:04:34 | `C8 08 2F 13 06 11 04 00 00` | `11 04`, gleich der Modell-ID |
| 10:09:55 | `C8 08 2F 13 06 00 00 00 00` | `00 00` |
| 13.10.2026 02:26:59 | `C8 08 2F 13 06 00 00 00 00 03 07` | `00 00` |

- Die ST-630-DE melden plausible Werte von 2945 bis 3039 mV. Der WHT-630 meldet einmal seine Modell-ID und zweimal 0.
- Vermutet: Der WHT-630 misst in `C8` keine Spannung, die Felder sind unbelegt. Die Brücke sendet ihm deshalb kein `C7` mehr, und seine Batterie-Entität bleibt unbekannt.
- Gegenprobe: `C7` an weitere WHT-630, Vergleich mit der Anzeige im W2-MM.

### P14: Zwei SIDs mit derselben Geräte-ID

Beobachtung vom 12.10.2026.

- SID 24 meldet in `D4 06` die Geräte-ID 2FE43D, dieselbe wie SID 14 (ST-630-DE). SID 24 beantwortet `C3` nicht, hat RM-Batterie 2490 mV, RSSI 45 und das kritische Bit `01`.
- `D4 06` d8–d10 ist die vom Host gelernte Geräte-ID (wie `D2` d6–d8). Vermutet: Das RM an SID 24 steckte früher am Melder 2FE43D, behielt die gelernte ID und trägt heute keinen Melder. Das kritische Bit passt zur schwachen RM-Batterie.
- Antworten des Melders (`C8`, `73`, `C6`) tragen am Ende die SID des sendenden RM und lassen sich damit eindeutig SID 14 zuordnen.
- Gegenprobe: SID 24 im Haus suchen, etwa über die Diagnose im W2-MM.

### P15: RMs mit Firmware `DF`

Beobachtung vom 12.10.2026.

- SID 1 und SID 12 melden in `D4 06` die Firmware `DF`, alle anderen `EF`. Beide haben BVNL `46` (2953 mV) und einen BVWL-Wert um `5A`–`5E`, deutlich unter dem der übrigen RMs (`8C`–`C4`).
- SID 1 beantwortet `C3` nicht und gilt als RM ohne Melder. SID 12 ist noch nicht abgefragt.
- Vermutet: eine ältere RM-Generation oder ein anderer Gerätetyp, etwa ein netzbetriebenes Modul. Offen.

### P16: Laufzeiten mit genau einer offenen Fernanfrage

Beobachtung vom 13.10.2026, Log über die native API, 02:13–02:35, Netzdetails für SID 0 bis 5, Zeitlimit 180 s, immer nur eine Anfrage im Netz.

| SID, Gerät | `D3 06` → `D4 06` | `C7` → `C8` | `72` → `73` | `C5` → `C6` | Dauer je SID |
| --- | --- | --- | --- | --- | --- |
| 0, ST-630-DE | 2,8 s | 25,5 s | 6,7 s | 118,6 s | 156 s |
| 1, RM ohne Melder | 40,1 s | – | – | – | 40 s |
| 2, ST-630-DE | 25,0 s | 31,7 s | 38,3 s | 126,9 s | 223 s |
| 3, WHT-630 | 27,6 s | 21,5 s | 42,3 s | – | 93 s |
| 4, ST-630-DE | 167,3 s | 10,2 s | 15,4 s | 32,1 s | 226 s |
| 5, ST-630-DE | 175,7 s | 8,8 s | offen | offen | über 197 s |

- Alle Antworten kamen, keine überschritt das Zeitlimit. Die längste lag mit 175,7 s nur 4 s darunter.
- Der Remote-Status als erster Kontakt streut von 3 s bis 176 s. Die Anfragen direkt danach an dasselbe Ziel kommen meist in 7–42 s zurück, auch nach einem sehr langsamen ersten Kontakt (SID 4 und SID 5). Vermutet: Das Ziel-RM wacht nur in Abständen auf und bleibt nach einem Kontakt eine Weile wach. Die Bündelung aller Anfragen je SID ist damit günstig.
- `C6` braucht mit 32–127 s am längsten, wie schon in P10.
- Ein Melder kostet mit vier Anfragen etwa 1,5 bis 4 min. Eine Runde über 21 SIDs dauert damit rund eine Stunde.
- Folgenummern: SID 2 liefert 1, 2, 3 und SID 4 liefert 12, 13, 14 ohne Lücke direkt nach dem Remote-Status. SID 0 springt von 4 auf 6, SID 3 von 7 auf 9, jeweils zwischen zwei Melderantworten. Die Vermutung aus P9, der Remote-Status verbrauche Folgenummern, trägt damit allein nicht.
- Um 02:15:39 kam ein `D4 06` von SID 0, ohne dass die Brücke eine Anfrage an SID 0 offen hatte. Offen stand nur `D3 06 10 00` an SID 16, das schon mit `D4 01` beendet war. Herkunft offen, vermutlich eine Anfrage aus der Zeit vor dem Neustart der Brücke um 02:12. Die Brücke hat den Wert passiv ins Register übernommen.

### P17: Fernantworten kommen in einem 500-ms-Raster

Beobachtung vom 13.10.2026, vermutet.

- Die Empfangszeiten von 18 der 19 Fernantworten (`D4 06`, `C8`, `73`, `C6`) im Log vom 13.10.2026 liegen modulo 0,5 s auf einer gemeinsamen Phase. Sie wandert gleichmäßig von 0,427 s (02:19:39) bis 0,527 s (02:34:33), also um 0,1 s in 15 min, etwa 110 ppm Gangunterschied zwischen Modul und Pico. Auch `D4 10` um 02:13:00,881 liegt auf diesem Raster.
- Die beiden `D4 01` (Phase 0,240 und 0,022) und der Remote-Status von SID 0 nach 2,8 s (0,226) liegen außerhalb.
- Die eigenen Sendungen der Brücke fallen auf beliebige Zeitpunkte, denn das Modul taktet auf Anforderung über IRQ.
- Vermutet: Das Funknetz arbeitet mit Zeitschlitzen von 500 ms, und das Modul reicht Funkrahmen im Takt dieser Schlitze an den Host weiter. `D4 01` erzeugt das RM lokal, unabhängig vom Raster (P12).
- Gegenprobe: dieselbe Auswertung an einem längeren Log mit mehr Antworten.
- Gegenprobe vom 13.10.2026, Log 08:10–08:35 über die native API: Die sieben Funkrahmen (`D4 10`, `D4 06`, `C8`, `73`, zweimal `71`, `70`) liegen modulo 0,5 s bei 0,013, 0,205, 0,304, 0,096, 0,058, 0,147 und 0,257 s. Zwischen `C8` und `73` von SID 26 liegen nur 21 s, die Phase springt dabei von 0,304 auf 0,096. Ein gemeinsames Raster zeigt dieses Log nicht. Die Vermutung bleibt offen; die Stichprobe ist klein, und die Zeitstempel vergibt der Log-Client beim Empfang über WLAN.

### P18: Das W2-MM sendet `70` mit der Gateway-ID

Beobachtung vom 13.10.2026, einmal im Log, Deutung vermutet.

- Um 08:35:12,757 kam `70 01 02 03 81 01 00 00 0F 02`: Test bestanden, Gerät `01 02 03` (Gateway-ID), Klasse `81` (Rauch), Modell `00 00`, SID 15, Folge 2.
- SID 15 stand weder in der lokalen SID-Map dieses Laufs noch in `D4 10`. Nach P8 gehört SID 15 zum W2-MM-Service-Dongle. Vermutet: Das W2-MM meldet mit `70` einen eigenen Test oder einen Test, den es im Netz anstößt, und trägt dabei die Gateway-ID, wie Gateway-RMs beim Einlernen (`D4 09 00 01 02 03`).
- Modell 0 passt zu keinem Melder der Modelltabelle. Die Folgenummer 2 ist sehr klein, vermutlich kurz nach dem Einschalten des Dongles.
- Die Brücke legte dafür einen Registerplatz „Gateway, außerhalb der SID-Map“ an und speicherte ihn in `nodes.bin`. Seit der Änderung nach der ersten Prüfung von Stufe 3 legen Nachrichten mit der Gateway-ID für SIDs ohne Platz keinen Eintrag mehr an. Ereignis und „letztes Ereignis“ kommen weiterhin, mit `(Gateway)` statt eines Meldernamens.
- Offen: Was löste die Nachricht aus? Gegenprobe: am W2-MM gezielt einen Test starten und das Log mitschneiden.

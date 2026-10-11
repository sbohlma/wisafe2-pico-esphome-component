# wisafe2-pico-esphome-component

**WIP.** ESPHome-External-Component für eine WiSafe2-Brücke auf dem Raspberry Pi Pico 2 W.

Die zertifizierten Melder bleiben die Sicherheitsanlage. Diese Brücke hört im 868-MHz-Netz mit und ist kein Ersatz für die Melder.

## Stand

Umgesetzt ist Stufe 0 des [Umsetzungsplans](doc/Protokoll-Umsetzungsplan.md): Rahmenschicht, Maskierung und Sendeplanung.

Beim Start hält Kern 1 nur IRQ auf Low. Die Kommunikation mit dem Modul beginnt erst, wenn ESPHome hochgefahren und das WLAN verbunden ist. 5 s danach, damit sich Log-Clients verbinden können, fordert Kern 0 die Einrichtung des SPI-Slaves an und sendet den Init-Befehl `D3 19 50 00`. Ohne Quittung `46` wiederholt Kern 0 den Befehl bis zu 50-mal im Abstand von etwa 0,5 s. Nach der Quittung fragt Kern 0 den Status des eigenen Funkmoduls (`D1`) und die lokale SID-Map (`D3 03`) ab. Beides sind lokale Abfragen ohne Funkverkehr. Der Button `init_button` startet den Init jederzeit neu.

Kern 1 ist SPI-Slave und quittiert jedes empfangene Byte mit einem IRQ-Puls von 8 µs. Auf MISO liegt im Empfang `0x00`. Die empfangenen Bytes gehen über eine Warteschlange nach Kern 0.

Kern 0 setzt daraus Rahmen bis `7E` zusammen:

- Füllbytes `00` vor einem Rahmen werden verworfen.
- `7D 01` wird zu `7E` und `7D 02` zu `7D` demaskiert.
- Ein Rahmen ohne weiteres Byte nach 100 ms gilt als unvollständig.

Beim Senden maskiert Kern 0 die Bytes und hängt `7E` an.

Die Sendeplanung auf Kern 0 schickt höchstens einen Befehl gleichzeitig und hält höchstens eine Fernanfrage offen. Nach jedem gesendeten oder empfangenen Rahmen wartet sie 500 ms, bevor der nächste Befehl startet. Antworten an das Funkmodul, etwa später `91` auf `41`, gehen ohne diese Wartezeit vor allen anderen Befehlen hinaus. Je Befehl wartet sie auf die passende Antwort:

| Befehl | erwartete Antwort | Zeitlimit |
| --- | --- | --- |
| lokale Abfrage, etwa `D1`, `D3 03` | `D2` bzw. `D4 03` | 500 ms |
| Funkbefehl, etwa der Init | `46` (angenommen) oder `47` (abgelehnt) | 150 ms |
| Fernanfrage | `46`, danach die Antwort aus dem Netz | 55 s |

Kern 1 sendet die Bytes eines Befehls direkt hintereinander, bis zu 32 Bytes, und meldet danach zurück, ob alle Bytes abgenommen wurden. Ein Byte zum Modul hält IRQ für die Dauer dieses Bytes auf High.

## Hardware

Schaltplan und Aufbau: [doc/WiSafe2-Pico2W-Hardware.md](doc/WiSafe2-Pico2W-Hardware.md), druckbar als [HTML](doc/WiSafe2-Pico2W-Hardware.html).

| Signal | Pico | Modulpad |
| --- | --- | --- |
| MOSI, Modul → Pico | GP16, Pin 21 | 5 |
| CS | GP17, Pin 22 | 1 |
| SCK | GP18, Pin 24 | 7 |
| MISO, Pico → Modul | GP19, Pin 25 | 10 |
| IRQ, Pico → Modul | GP20, Pin 26 | 3 |
| 3,3 V | Pin 36 | 2 |
| GND | Pin 38 und 23 | 9 |

3,3 V zum Modul erst anlegen, wenn die Firmware läuft und IRQ als Ausgang auf Low liegt. JP1 bleibt offen, solange die Lithiumzelle im Modul steckt.

## ESPHome

```yaml
external_components:
  - source: github://sbohlma/wisafe2-pico-esphome-component@main
    components: [wisafe2]
    refresh: 5min

wisafe2:
  status:
    name: "WiSafe2 Status"
  init_button:
    name: "WiSafe2 Init senden"
  spi_button:
    name: "WiSafe2 SPI neu starten"
  rm_status_button:
    name: "WiSafe2 RM-Status lesen"
  sid_map_button:
    name: "WiSafe2 SID-Map lesen"
```

Der Ordner `components/wisafe2` wird darüber geladen. Alle Einträge sind optional.

Der Status-Sensor zeigt den Zustand des Moduls:

| Status | Bedeutung |
| --- | --- |
| `warte auf WLAN` | ESPHome läuft, die Kommunikation mit dem Modul hat noch nicht begonnen |
| `Init läuft` | Init gesendet, Quittung steht aus |
| `Modul bereit` | Init quittiert, Modul gekoppelt |
| `ungekoppelt` | `D2` meldet SID `40` oder die lokale SID-Map ist leer |
| `keine Antwort` | Init nach 50 Versuchen ohne Quittung |

`rm_status_button` fragt den Status des eigenen Funkmoduls ab (`D1`), `sid_map_button` die lokale SID-Map (`D3 03`).

`spi_button` richtet SPI-Slave und IRQ auf Kern 1 neu ein, zum Testen, sobald kein Befehl unterwegs ist. Danach steht im Log `SPI-Slave gestartet, CS …, SCK …, MOSI …` mit den gelesenen Pegeln. Im Ruhezustand sind CS High und SCK Low.

Verbindet sich der Log-Client erst später, `WiSafe2 Init senden` drücken. Ein Druck sendet den Befehl bis zu 50-mal und hört bei `46 7E` auf.

## Log

Rahmen erscheinen demaskiert und ohne `7E`. Geräte-IDs stehen in Anzeigereihenfolge, wie das Gateway sie zeigt: Paket `3D E4 2F` erscheint als `2FE43D`.

Bekannte Rahmen erscheinen als Text:

| Rahmen | Text |
| --- | --- |
| `46`, `47` | Quittung, Abgelehnt |
| `41` | Identitätsanfrage |
| `D2` | RM-Status mit SID, Batterie in mV, RSSI, Firmware, gelernter Geräte-ID, kritischen Bits, Funkfehlern |
| `D4 01` | nicht erreichbare SIDs |
| `D4 03`, `D4 04` | lokale SID-Map, neue Netzkarte |
| `D4 06` | Remote-Status |
| `D4 09` | Präsenzmeldung |
| `D4 11` | Remote-SID-Map |
| `70`, `71`, `50`, `61` | Test, Sockel, Alarm, Stille |

Alles andere erscheint als `Roh:` mit Hex-Bytes. Fehlerhafte Rahmen erscheinen als `Roh unvollständig:`, `Roh zu lang:` oder `Roh ungültige Maskierung:`. Gesendete Bytes stehen auf Stufe DEBUG als `Sende …` im Log, maskiert und mit `7E`.

Fehler auf Kern 1, etwa eine volle Warteschlange oder ein SPI-Überlauf, kommen über `core1_log` ins selbe Log.

## Tests

Rahmenschicht, Nachrichten, Sendeplanung und Log-Text lassen sich ohne Hardware auf dem PC prüfen:

```sh
make -C tests/host
```

Die Tests nutzen die Beispielrahmen aus der Protokollanalyse und brauchen nur g++.

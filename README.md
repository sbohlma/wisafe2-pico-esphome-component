# wisafe2-pico-esphome-component

**WIP.** ESPHome-External-Component für eine WiSafe2-Brücke auf dem Raspberry Pi Pico 2 W.

Die zertifizierten Melder bleiben die Sicherheitsanlage. Diese Brücke hört im 868-MHz-Netz mit und ist kein Ersatz für die Melder.

## Stand

Umgesetzt sind Stufe 0 und Stufe 1 des [Umsetzungsplans](doc/Protokoll-Umsetzungsplan.md):

- Stufe 0: Rahmenschicht, Maskierung und Sendeplanung, auf der Hardware abgenommen.
- Stufe 1: alle bekannten Nachrichten lesbar im Log, Identität der Brücke im Netz, Antworten auf Anfragen an die eigene Geräte-ID, Sensoren des eigenen Funkmoduls und manuelle Abfragen. Die Hardware-Abnahme steht aus.

Beim Start hält Kern 1 nur IRQ auf Low. Die Kommunikation mit dem Modul beginnt erst, wenn ESPHome hochgefahren und das WLAN verbunden ist. 5 s danach, damit sich Log-Clients verbinden können, fordert Kern 0 die Einrichtung des SPI-Slaves an und sendet den Init-Befehl `D3 19 50 00`. Ohne Quittung `46` wiederholt Kern 0 den Befehl bis zu 50-mal im Abstand von etwa 0,5 s. Nach der Quittung meldet das Modul nach etwa 3 s `D4 10` und nimmt bis dahin keine Bytes an. Kern 0 wartet auf diesen Rahmen, höchstens 10 s, und fragt danach den Status des eigenen Funkmoduls (`D1`) und die lokale SID-Map (`D3 03`) ab. Beides sind lokale Abfragen ohne Funkverkehr. Der Button `init_button` startet den Init jederzeit neu.

Kern 1 ist SPI-Slave und quittiert jedes empfangene Byte mit einem IRQ-Puls von 8 µs. Auf MISO liegt im Empfang `0x00`. Die empfangenen Bytes gehen über eine Warteschlange nach Kern 0.

Kern 0 setzt daraus Rahmen bis `7E` zusammen:

- Füllbytes `00` vor einem Rahmen werden verworfen.
- `7D 01` wird zu `7E` und `7D 02` zu `7D` demaskiert.
- Ein Rahmen ohne weiteres Byte nach 100 ms gilt als unvollständig.

Beim Senden maskiert Kern 0 die Bytes und hängt `7E` an.

Die Sendeplanung auf Kern 0 schickt höchstens einen Befehl gleichzeitig und hält höchstens eine Fernanfrage offen. Nach jedem gesendeten oder empfangenen Rahmen wartet sie 500 ms, bevor der nächste Befehl startet. Antworten an das Funkmodul, etwa `91` auf `41`, gehen ohne diese Wartezeit vor allen anderen Befehlen hinaus. Je Befehl wartet sie auf die passende Antwort:

| Befehl | erwartete Antwort | Zeitlimit |
| --- | --- | --- |
| lokale Abfrage, etwa `D1`, `D3 03` | `D2` bzw. `D4 03` | 500 ms |
| Funkbefehl, etwa der Init | `46` (angenommen) oder `47` (abgelehnt) | 150 ms |
| Fernanfrage | `46`, danach die Antwort aus dem Netz | 55 s |

Antworten auf Fernanfragen ordnet die Sendeplanung genau zu: `D4 06` über die SID, `C4` über die SID am Ende des Rahmens, `C8`, `73` und `C6` über die Geräte-ID. Nach dem Init bekommt die Brücke Kopien aller Hostantworten im Netz, auch auf Anfragen anderer Knoten; diese Kopien beenden keine eigene Anfrage. Meldet das Modul `D4 01` mit der Ziel-SID, endet die Anfrage als „nicht erreichbar“.

Kern 1 sendet die Bytes eines Befehls direkt hintereinander, bis zu 32 Bytes, und meldet danach zurück, ob alle Bytes abgenommen wurden. Ein Byte zum Modul hält IRQ für die Dauer dieses Bytes auf High. Beginnt das Modul innerhalb von 5 ms nicht zu takten, bricht Kern 1 den Befehl ab und meldet den Grund.

Ein Befehl, von dem noch kein Byte hinausging, etwa ein Button-Druck direkt nach dem Init, wird bis zu 20-mal im Abstand von mindestens 500 ms wiederholt. Bricht ein Befehl nach dem ersten Byte ab, gilt er sofort als nicht gesendet.

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
  device_id: "14B8A5"
  model: 0x0311
  status:
    name: "WiSafe2 Status"
  last_reply:
    name: "WiSafe2 letzte Antwort"
  rm_sid:
    name: "WiSafe2 RM-SID"
  rm_battery:
    name: "WiSafe2 RM-Batterie"
  rm_rssi:
    name: "WiSafe2 RM-RSSI"
  rm_radio_faults:
    name: "WiSafe2 RM-Funkfehler"
  rm_firmware:
    name: "WiSafe2 RM-Firmware"
  rm_device_id:
    name: "WiSafe2 RM-Geräte-ID"
  rm_paired:
    name: "WiSafe2 RM gekoppelt"
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

### Identität

`device_id` ist die Geräte-ID der Brücke in Anzeigereihenfolge, wie das Gateway sie zeigt. Am besten die ID, die das Funkmodul am Pico schon aus dem Pairing trägt; sie steht nach dem Start im Sensor `rm_device_id`. `model` ist die Modell-ID, Vorgabe `0x0311` (WST-630, im Paket `11 03`). Die Klasse ist fest `FF`.

Mit `device_id` antwortet die Brücke:

| Anfrage | Antwort |
| --- | --- |
| `41` vom eigenen Modul | `91 dev model FF 05 01 01`, sofort und jedes Mal |
| `C3` (ID-Anfrage an die eigene SID) | `C4 dev FF model` |
| `72` an die eigene Geräte-ID | `73 dev model 00 00`, keine Fehler |
| `C7` an die eigene Geräte-ID | `C8 dev 06 B8 0B 00 00`, 3000 mV |
| `C5` an die eigene Geräte-ID | `C6 dev 74 00 00 00 00`, keine Ereignisse |
| `C1` (Diagnoseanfrage) | `C2 dev FF model 01`, bestanden |

`72`, `C5` und `C7` werden an jeden Host geflutet; die Brücke antwortet nur, wenn die Geräte-ID ihre eigene ist. `C1` trägt keine Geräte-ID: Ist das Ziel einer Diagnose vom Sockel genommen, wird `C1` geflutet, und dann antwortet auch die Brücke mit `C2`. Jede Art von Anfrage beantwortet sie höchstens einmal je 60 s. Ohne `device_id` bleiben `41` und alle Anfragen unbeantwortet.

Nach jedem `D2` vergleicht die Brücke die Geräte-ID im Modul mit `device_id`. Eine Abweichung steht als Warnung im Log und im Status.

### Sensoren und Status

| Eintrag | Inhalt |
| --- | --- |
| `status` | Zustand des Moduls, siehe unten |
| `last_reply` | Ergebnis der letzten manuellen Abfrage, als Text |
| `rm_sid` | eigene SID; leer, solange das Modul ungekoppelt ist |
| `rm_battery` | Batterie des eigenen Funkmoduls in V |
| `rm_rssi` | Empfangspegel aus `D2` |
| `rm_radio_faults` | Zähler der Funkfehler aus `D2` |
| `rm_firmware` | Firmware des Funkmoduls, etwa `EF` |
| `rm_device_id` | Geräte-ID, die das Modul gelernt hat |
| `rm_paired` | Modul gekoppelt |

Die Werte kommen aus `D2`, nach dem Start, auf Knopfdruck und unaufgefordert etwa stündlich.

| Status | Bedeutung |
| --- | --- |
| `warte auf WLAN` | ESPHome läuft, die Kommunikation mit dem Modul hat noch nicht begonnen |
| `Init läuft` | Init gesendet, Quittung steht aus |
| `Modul bereit` | Init quittiert, Modul gekoppelt |
| `ungekoppelt` | `D2` meldet SID `40` oder die lokale SID-Map ist leer |
| `Geräte-ID weicht ab` | die Geräte-ID im Modul ist eine andere als `device_id` |
| `keine Antwort` | Init nach 50 Versuchen ohne Quittung |

`rm_status_button` fragt den Status des eigenen Funkmoduls ab (`D1`), `sid_map_button` die lokale SID-Map (`D3 03`).

### Manuelle Abfragen

Fünf Aktionen lösen lesende Abfragen aus. Jede Fernanfrage erzeugt Funkverkehr bei allen Knoten und kostet Batterie; deshalb laufen sie nur auf Abruf.

| Aktion | Befehl | Antwort |
| --- | --- | --- |
| `wisafe2.query_remote_status` mit `sid` | `D3 06 sid 00` | `D4 06` vom Ziel-RM: Geräte-ID, RM-Batterie, RSSI |
| `wisafe2.query_identity` mit `sid` | `D3 06 sid 01` | `C4` vom Host des Ziels: Klasse und Modell |
| `wisafe2.query_battery` | `C7 dev model 06` | `C8` mit der Batterie in mV |
| `wisafe2.query_fault_details` | `72 dev model 00` | `73` mit den Fehlerbits |
| `wisafe2.query_fault_events` | `C5 dev model 74` | `C6`, nur WST-630 und ST-630-DE |

Die letzten drei brauchen Geräte-ID und Modell des Ziels. Mit `sid` allein nimmt die Brücke beides aus den bisher gehörten Nachrichten dieser SID, etwa nach `query_remote_status` (Geräte-ID) und `query_identity` (Modell). Alternativ `device_id` und `model` direkt angeben. Bis Stufe 2 liegen diese Angaben nur im RAM.

Über `api: actions` stehen die Aktionen in Home Assistant bereit:

```yaml
api:
  actions:
    - action: wisafe2_query_remote_status
      variables:
        sid: int
      then:
        - wisafe2.query_remote_status:
            sid: !lambda "return sid;"
    - action: wisafe2_query_identity
      variables:
        sid: int
      then:
        - wisafe2.query_identity:
            sid: !lambda "return sid;"
    - action: wisafe2_query_battery
      variables:
        sid: int
      then:
        - wisafe2.query_battery:
            sid: !lambda "return sid;"
    - action: wisafe2_query_fault_details
      variables:
        sid: int
      then:
        - wisafe2.query_fault_details:
            sid: !lambda "return sid;"
    - action: wisafe2_query_fault_events
      variables:
        sid: int
      then:
        - wisafe2.query_fault_events:
            sid: !lambda "return sid;"
```

Mit fester Geräte-ID, etwa für einen Button:

```yaml
button:
  - platform: template
    name: "Batterie Hitzemelder"
    on_press:
      - wisafe2.query_battery:
          device_id: "132F08"
          model: 1041
```

Das Ergebnis steht im Log und in `last_reply`, etwa `Batterie Gerät 33F0DC, 2954 mV, SID 11, Folge 13` oder `Remote-Status SID 25: nicht erreichbar (D4 01)`.

`spi_button` richtet SPI-Slave und IRQ auf Kern 1 neu ein, zum Testen, sobald kein Befehl unterwegs ist. Danach steht im Log `SPI-Slave gestartet, CS …, SCK …, MOSI …` mit den gelesenen Pegeln. Im Ruhezustand sind CS High und SCK Low.

Verbindet sich der Log-Client erst später, `WiSafe2 Init senden` drücken. Ein Druck sendet den Befehl bis zu 50-mal und hört bei `46 7E` auf.

## Log

Rahmen erscheinen demaskiert und ohne `7E`. Geräte-IDs stehen in Anzeigereihenfolge, wie das Gateway sie zeigt: Paket `3D E4 2F` erscheint als `2FE43D`.

Bekannte Rahmen erscheinen als Text, danach die Bytes in eckigen Klammern:

| Rahmen | Text |
| --- | --- |
| `46`, `47` | Quittung, Abgelehnt |
| `41` | Identitätsanfrage |
| `D2` | RM-Status mit SID, Batterie in mV, RSSI, Firmware, gelernter Geräte-ID, kritischen Bits, Funkfehlern |
| `D4 01` | nicht erreichbare SIDs |
| `D4 03`, `D4 04` | lokale SID-Map, neue Netzkarte |
| `D4 06` | Remote-Status |
| `D4 09` | Präsenzmeldung |
| `D4 10` | Init-Antworten: Knoten, die nach dem Init geantwortet haben, und eigene SID |
| `D4 11` | Remote-SID-Map |
| `50`, `51`, `61` | Alarm, Alarm Ende, Stummschaltung, mit Klasse |
| `70` | Test mit Klasse, Ergebnis und Modell |
| `71` | Fehlerbericht mit Modell und Flags im Klartext, etwa `abgenommen, kalibriert` |
| `72`, `C5`, `C7` | Anfrage Fehlerdetails, Fehlerereignisse, Batterie an ein Gerät |
| `73` | Fehlerdetails mit den gesetzten Bits je Modell, etwa `FS 0004: Melder-Batterie (LOW)` |
| `C1`, `C2` | Diagnoseanfrage, Diagnoseergebnis |
| `C3`, `C4` | ID-Anfrage, ID-Information mit Klasse und Modell |
| `C6`, `C8` | Fehlerereignisse, Batterie in mV |
| `D5` | Geräte-ID, Modell und d6, Bedeutung offen |

Funknachrichten enden mit `SID n, Folge m`: SID des sendenden Funkmoduls und Sequenznummer. Modelle stehen dezimal wie im Gateway, etwa `Modell 1041 WHT-630`; bekannt sind alle 14 Modelle der Protokollanalyse. Die Geräte-ID `030201` ist als `(Gateway)` markiert.

Kommt dieselbe Funknachricht (SID, Typ, Sequenznummer) innerhalb von 60 s erneut, steht sie auf Stufe DEBUG als `Duplikat:` im Log und wird nicht weiter ausgewertet.

Alles andere erscheint als `Roh:` mit Hex-Bytes. Fehlerhafte Rahmen erscheinen als `Roh unvollständig:`, `Roh zu lang:` oder `Roh ungültige Maskierung:`. Gesendete Bytes stehen auf Stufe DEBUG als `Sende …` im Log, maskiert und mit `7E`. Nimmt das Modul kein Byte an, steht dort `Kern 1: Modul nimmt kein Byte an (…)` mit dem Grund: `CS belegt` (das Modul sendet gerade selbst) oder `Modul taktet nicht`.

Fehler auf Kern 1, etwa eine volle Warteschlange oder ein SPI-Überlauf, kommen über `core1_log` ins selbe Log.

## Tests

Rahmenschicht, Nachrichten, Modelltabellen, Duplikatfilter, Antworten des Hosts, Sendeplanung und Log-Text lassen sich ohne Hardware auf dem PC prüfen:

```sh
make -C tests/host
```

Die Tests nutzen die Beispielrahmen aus der Protokollanalyse und brauchen nur g++.

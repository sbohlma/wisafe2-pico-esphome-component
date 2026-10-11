# wisafe2-pico-esphome-component

**WIP.** ESPHome-External-Component für eine WiSafe2-Brücke auf dem Raspberry Pi Pico 2 W.

Die zertifizierten Melder bleiben die Sicherheitsanlage. Diese Brücke hört im 868-MHz-Netz mit und ist kein Ersatz für die Melder.

## Stand

Beim Start hält Kern 1 nur IRQ auf Low. Die Kommunikation mit dem Modul beginnt erst, wenn ESPHome hochgefahren und das WLAN verbunden ist. 5 s danach, damit sich Log-Clients verbinden können, fordert Kern 0 die Einrichtung des SPI-Slaves an und reiht unmittelbar danach den Init-Befehl `D3 19 50 00 7E` ein. Die erwartete Antwort `46 7E` erscheint im Log als `Antwort bereit 46 7E`. Ohne Antwort wiederholt Kern 0 den Befehl bis zu 50-mal im Abstand von etwa 0,7 s. Der Button `init_button` startet denselben Ablauf jederzeit neu.

Kern 1 ist SPI-Slave und quittiert jedes empfangene Byte mit einem IRQ-Puls von 8 µs. Auf MISO liegt im Empfang `0x00`. Die empfangenen Bytes gehen über eine Warteschlange nach Kern 0. Dort werden Rahmen bis `7E` erkannt und ins Log geschrieben.

Befehle an das Modul kommen von Kern 0 über eine zweite Warteschlange, bis zu 32 Bytes je Befehl. Kern 1 sendet die Bytes eines Befehls direkt hintereinander und meldet danach zurück, ob alle Bytes abgenommen wurden. Ein Byte zum Modul hält IRQ für die Dauer dieses Bytes auf High.

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
```

Der Ordner `components/wisafe2` wird darüber geladen. Der Status-Sensor meldet `bereit`. Alle Einträge sind optional.

`spi_button` richtet SPI-Slave und IRQ auf Kern 1 neu ein, zum Testen, sobald kein Befehl unterwegs ist. Danach steht im Log `SPI-Slave gestartet, CS …, SCK …, MOSI …` mit den gelesenen Pegeln. Im Ruhezustand sind CS High und SCK Low.

Verbindet sich der Log-Client erst später, `WiSafe2 Init senden` drücken. Ein Druck sendet den Befehl bis zu 50-mal und hört bei `46 7E` auf.

## Log

Bekannte Rahmen erscheinen als Text: `70` Test, `71` Sockel, `50` Alarm, `61` Stille, `D2` fehlendes Gerät, `41` und `46` Antwort, `D4` Kopplung. Alles andere erscheint als `Roh:` mit Hex-Bytes.

Fehler auf Kern 1, etwa eine volle Warteschlange oder ein SPI-Überlauf, kommen über `core1_log` ins selbe Log.

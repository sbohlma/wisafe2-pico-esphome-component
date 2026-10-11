# wisafe2-pico-esphome-component

**WIP.** ESPHome-External-Component für eine WiSafe2-Brücke auf dem Raspberry Pi Pico 2 W.

Die zertifizierten Melder bleiben die Sicherheitsanlage. Diese Brücke hört im 868-MHz-Netz mit und ist kein Ersatz für die Melder.

## Stand

Nach dem Start sendet Kern 1 den Init-Befehl `D3 19 50 00 7E`. Die erwartete Antwort `46 7E` erscheint im Log als `Antwort bereit 46 7E`. Danach bleibt die Komponente im Empfang. Der Button `init_button` startet denselben Ablauf jederzeit neu, ohne die Wartezeit von 5 s.

Kern 1 ist SPI-Slave, legt IRQ beim Start auf Low und quittiert jedes empfangene Byte mit einem IRQ-Puls von 8 µs. Auf MISO liegt im Empfang `0x00`. Ein Byte zum Modul hält IRQ für die Dauer dieses Bytes auf High. Die Bytes gehen über eine Warteschlange nach Kern 0. Dort werden Rahmen bis `7E` erkannt und ins Log geschrieben.

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
```

Der Ordner `components/wisafe2` wird darüber geladen. Der Status-Sensor meldet `bereit`. Beide Einträge sind optional.

Der Init-Ablauf beim Start ist nach etwa 30 s abgeschlossen. Über WLAN verbindet sich der Log-Client oft erst danach. Zum Prüfen deshalb das Log öffnen und dann `WiSafe2 Init senden` drücken. Ein Druck sendet den Befehl bis zu 50-mal im Abstand von etwa 0,5 s und hört bei `46 7E` auf.

## Log

Bekannte Rahmen erscheinen als Text: `70` Test, `71` Sockel, `50` Alarm, `61` Stille, `D2` fehlendes Gerät, `41` und `46` Antwort, `D4` Kopplung. Alles andere erscheint als `Roh:` mit Hex-Bytes.

Fehler auf Kern 1, etwa eine volle Warteschlange oder ein SPI-Überlauf, kommen über `core1_log` ins selbe Log.

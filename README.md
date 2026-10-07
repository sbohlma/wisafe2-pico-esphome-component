# wisafe2-pico-esphome-component

**WIP.** ESPHome-External-Component für eine WiSafe2-Brücke auf dem Raspberry Pi Pico 2 W.

Die zertifizierten Melder bleiben die Sicherheitsanlage. Diese Brücke hört im 868-MHz-Netz mit und ist kein Ersatz für die Melder.

## Stand

Die Komponente empfängt. Sie sendet noch keine Befehle an das Funkmodul.

Kern 1 ist SPI-Slave, legt IRQ beim Start auf Low und quittiert jedes empfangene Byte mit einem IRQ-Puls von 8 µs. Auf MISO liegt dabei `0x00`. Die Bytes gehen über eine Warteschlange nach Kern 0. Dort werden Rahmen bis `7E` erkannt und ins Log geschrieben.

Der spätere Init-Befehl ist `D3 19 50 00 7E`, die erwartete Antwort `46 7E`. Ohne diesen Befehl kann das Modul stumm bleiben. Der erste erwartete Verkehr ist eine Testtaste an einem bereits gekoppelten Melder.

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
```

Der Ordner `components/wisafe2` wird darüber geladen. Der Status-Sensor meldet `bereit`.

## Log

Bekannte Rahmen erscheinen als Text: `70` Test, `71` Sockel, `50` Alarm, `61` Stille, `D2` fehlendes Gerät, `41` und `46` Antwort, `D4` Kopplung. Alles andere erscheint als `Roh:` mit Hex-Bytes.

Fehler auf Kern 1, etwa eine volle Warteschlange oder ein SPI-Überlauf, kommen über `core1_log` ins selbe Log.

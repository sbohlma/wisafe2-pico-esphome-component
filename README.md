# wisafe2-pico-esphome-component

ESPHome-External-Component für die WiSafe2-Brücke auf einem Raspberry Pi Pico 2 W.

Der Ordner `components/wisafe2` wird vom ESPHome Device Builder per `external_components` geladen. Die Komponente meldet den Status `bereit`.

Kern 1 ist SPI-Slave am Funkmodul und hört zu. Das Modul ist SPI-Master: es taktet jedes Byte einzeln. Nach jedem Byte zieht Kern 1 IRQ für 8 µs hoch, sonst kommt das nächste Byte nicht. Auf MISO liegt dabei `0x00`. Es wird kein Befehl gesendet.

Die Bytes laufen über die Warteschlange nach Kern 0. Dort werden Rahmen bis `7E` erkannt. Bekannte Typen (`70` Test, `71` Sockel, `50` Alarm, `61` Stille, `D2` fehlendes Gerät, `41`/`46` Antwort, `D4` Kopplung) erscheinen als Text im Log. Alles andere erscheint als `Roh:` mit Hex-Bytes.

`core1_log` bleibt für Fehler auf Kern 1, zum Beispiel eine volle Warteschlange oder einen SPI-Überlauf.

```yaml
external_components:
  - source: github://sbohlma/wisafe2-pico-esphome-component@main
    components: [wisafe2]
    refresh: 5min

wisafe2:
  status:
    name: "WiSafe2 Status"
```

Ein Eintrag `blink:` gehört nicht mehr in die YAML.

## Anschlüsse

| Signal | Pico | Modulpad |
| --- | --- | --- |
| MOSI, Modul → Pico | GP16, Pin 21 | 5 |
| CS | GP17, Pin 22 | 1 |
| SCK | GP18, Pin 24 | 7 |
| MISO, Pico → Modul | GP19, Pin 25 | 10 |
| IRQ, Pico → Modul | GP20, Pin 26 | 3 |
| 3,3 V | Pin 36 | 2 |
| GND | Pin 38 und 23 | 9 |

Die Firmware legt IRQ als Ausgang auf low, sobald Kern 1 startet. 3,3 V zum Modul erst danach anlegen. JP1 bleibt offen, solange die Lithiumzelle im Modul steckt.

Ohne den späteren Init-Befehl `D3 19 50 00 7E` kann das Modul stumm bleiben. Eine Testtaste an einem bereits gekoppelten Melder ist der erste erwartete Verkehr.

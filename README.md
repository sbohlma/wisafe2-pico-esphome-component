# wisafe2-pico-esphome-component

ESPHome-External-Component für die WiSafe2-Brücke auf einem Raspberry Pi Pico 2 W.

Der Ordner `components/wisafe2` wird vom ESPHome Device Builder per `external_components` geladen. Die Komponente meldet den Status `bereit`. SPI folgt später.

Kern 1 läuft über `setup1` und `loop1` mit eigenem Stack. Die Onboard-LED hängt am WLAN-Chip, deshalb schreibt Kern 1 sie über `digitalWrite(PIN_LED)`.

## Schalter und Log

Der Schalter **LED Blinken** erscheint auf der lokalen Webseite. Nach dem Start steht er auf ein, die LED blinkt.

Kern 0 legt jeden Wechsel in eine Warteschlange. Kern 1 liest sie in `loop1`, schaltet das Blinken und ruft `core1_log` auf. Kern 0 holt die Zeile in `loop()` ab und schreibt sie mit `ESP_LOGI` in den ESPHome-Logger. Dort erscheint zum Beispiel:

```text
[wisafe2] Kern 1: Schalter umgelegt, Blinken aus
```

Die Warteschlange hat einen Schreiber und einen Leser und nimmt keine Sperre. Ein Kern wartet nicht auf den anderen.

```yaml
external_components:
  - source: github://sbohlma/wisafe2-pico-esphome-component@main
    components: [wisafe2]
    refresh: 5min

wisafe2:
  status:
    name: "WiSafe2 Status"
  blink:
    name: "LED Blinken"
```

`blink` ist optional. Ohne den Eintrag heißt der Schalter ebenfalls `LED Blinken`.

Die `output`- und `light`-Sektion für `pin: LED` bleibt aus der YAML draußen. Sonst schreibt ESPHome denselben Pin.

Das Funkmodul bleibt stromlos, solange die Komponente die SPI-Pins nicht treibt.

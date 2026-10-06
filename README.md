# wisafe2-pico-esphome-component

ESPHome-External-Component für die WiSafe2-Brücke auf einem Raspberry Pi Pico 2 W.

Der Ordner `components/wisafe2` wird vom ESPHome Device Builder per `external_components` geladen. Die Komponente meldet den Status `bereit`. SPI folgt später.

Kern 1 läuft über `setup1` und `loop1` mit eigenem Stack. `loop1` schaltet die Onboard-LED jede Sekunde um. Dafür gibt es keine ESPHome-Entität. Die LED hängt am WLAN-Chip, deshalb schreibt Kern 1 sie über `digitalWrite(PIN_LED)`.

```yaml
external_components:
  - source: github://sbohlma/wisafe2-pico-esphome-component@main
    components: [wisafe2]
    refresh: 5min

wisafe2:
  status:
    name: "WiSafe2 Status"
```

Die bisherige `output`- und `light`-Sektion für `pin: LED` muss aus der YAML weg. Sonst schreibt ESPHome denselben Pin.

Das Funkmodul bleibt stromlos, solange die Komponente die SPI-Pins nicht treibt.

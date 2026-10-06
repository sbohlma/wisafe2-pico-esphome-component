# wisafe2-pico-esphome-component

ESPHome-External-Component für die WiSafe2-Brücke auf einem Raspberry Pi Pico 2 W.

Der Ordner `components/wisafe2` wird vom ESPHome Device Builder per `external_components` geladen. Die erste Fassung meldet nur den Status `bereit`. SPI und der zweite Kern folgen später.

```yaml
external_components:
  - source: github://sbohlma/wisafe2-pico-esphome-component@main
    components: [wisafe2]
    refresh: 5min

wisafe2:
  status:
    name: "WiSafe2 Status"
```

Das Funkmodul bleibt stromlos, solange die Komponente die SPI-Pins nicht treibt.

# WiSafe2-Bridge für Home Assistant

Hardware-Unterlage · Raspberry Pi Pico 2 W und FireAngel-WiSafe2-Funkmodul · Revision 2 · 7. Oktober 2026

Dieselbe Unterlage zum Ausdrucken: [HTML](WiSafe2-Pico2W-Hardware.html). Im HTML liegt der Modulstecker im Maßstab 2:1. Der Maßbalken muss auf dem Ausdruck 20 mm lang sein.

**Diese Brücke ist kein Ersatz für die Rauchmelder.** Die Melder bleiben die Sicherheitsanlage und arbeiten ohne dieses Gerät. Die Brücke ist ein zusätzlicher Teilnehmer im 868-MHz-Netz und meldet Ereignisse an Home Assistant. Ein Ausfall der Brücke darf nicht der einzige Hinweis auf einen Alarm sein.

## 1. Umfang

Das Blatt beschreibt die Hardware zwischen dem Pico 2 W und einem originalen WiSafe2-Funkmodul. Die Firmware im Pico hört als SPI-Slave mit und sendet noch keine Befehle. IRQ wird ab dem Start als Ausgang auf Low gelegt. Die 3,3-V-Leitung zum Modul wird erst geschlossen, nachdem diese Firmware läuft.

Das Funkmodul ist SPI-Master. Es erzeugt Takt und Chip-Select. Der Pico ist SPI-Slave. Beide Seiten arbeiten mit 3,3 V. Ein Pegelwandler gehört nicht in diesen Aufbau. Der Pico verträgt an den GPIO keine 5 V.

## 2. Stückliste

| Teil | Wert / Typ | Bezug |
| --- | --- | --- |
| U1 | Raspberry Pi Pico 2 W | Aufdruck muss „Pico 2 W“ lauten. Ein Pico 2 ohne W hat kein WLAN. |
| U2 | WiSafe2-Funkmodul | Bevorzugt rote Platine aus einer W2-SVP-630 / CP-LED, ohne Lithiumzelle. |
| R1–R5 | 33 Ω, 5 Stück | Serie in CS, IRQ, MOSI, SCK, MISO. 22 Ω bis 47 Ω ist gleichwertig. |
| R6 | 10 kΩ | Pull-up von CS nach 3,3 V. Hält Select im Leerlauf auf High. |
| R7 | 10 kΩ | Pull-down von IRQ nach GND. Hält IRQ auf Low, solange die Firmware den Pin noch nicht treibt. |
| C1 | 100 nF keramisch | Direkt an Pad 2 gegen Pad 9. |
| C2 | 10 µF keramisch | Direkt an Pad 2 gegen Pad 9. |
| JP1 | Lötbrücke oder 2-polige Stiftleiste | Schaltet Pad 8 an 3,3 V. Stellung hängt von der Modulvariante ab, siehe Abschnitt 6. |
| AE1 | Drahtantenne | 86 mm Viertelwelle oder 173 mm Halbwelle, gemessen ab dem Antennenpad. |
| J1 | Stiftleiste 1×6, 2,54 mm | Messpunkte für den Logikanalysator. |
| — | Micro-USB-Kabel | Einzige Versorgung. 5 V nur über USB, nie an den Pin 3V3. |

Die Serienwiderstände dämpfen das Klingeln der schnellen SPI-Flanken. Ohne sie kann der Slave auf einer Leitung zusätzliche Taktimpulse sehen. Sie verschieben keinen Pegel und begrenzen im Ruhezustand keinen Strom.

## 3. Schaltplan

Die Siebdruckbezeichnung „MISO“ und „MOSI“ auf dem Pico gilt nur, wenn der Pico Master ist. Hier ist das Funkmodul Master. Maßgeblich sind die Pinnummern und die Richtung.

```text
U1 Pico 2 W                                              U2 WiSafe2
Pin 40 VBUS   nur Micro-USB
Pin 39 VSYS   offen
Pin 36 3V3  --+--------------------------- Pad 2  3V3
              |  C1 100 nF und C2 10 µF nach GND
              +-- R6 10 kΩ --------------- CS-Leitung
              +-- JP1 -------------------- Pad 8  Bat+
Pin 38 GND  --+
              +--------------------------- Pad 9  GND
Pin 23 GND  --+

Pin 26 GP20 IRQ aus  -- R2 33 Ω --+-------- Pad 3  IRQ ein
                                  +-- R7 10 kΩ nach GND
Pin 25 GP19 MISO aus -- R5 33 Ω ---------- Pad 10 MISO ein
Pin 24 GP18 SCK ein  -- R4 33 Ω ---------- Pad 7  SCK aus
Pin 22 GP17 CS ein   -- R1 33 Ω ---------- Pad 1  CS aus
Pin 21 GP16 MOSI ein -- R3 33 Ω ---------- Pad 5  MOSI aus
```

MOSI und SCK treibt das Modul. MISO und IRQ treibt der Pico. CS treibt das Modul, aktiv Low, ein Puls je Byte. J1 sitzt an der Modulseite von R1–R5: CS, IRQ, MOSI, SCK, MISO, GND.

GP0 und GP1 bleiben frei. GP23, GP24, GP25 und GP29 gehören intern dem WLAN-Chip.

## 4. Verdrahtung zum Abhaken

Alle GPIO-Nummern sind die des RP2350. Die Pin-Nummer ist die Position an der 40-poligen Stiftleiste, USB-Stecker nach oben, Bauteile zu dir.

| Von | Über | Nach | Richtung | Farbe |
| --- | --- | --- | --- | --- |
| Pico Pin 36, 3V3 | direkt | U2 Pad 2 | Versorgung | rot |
| Pico Pin 36, 3V3 | JP1 | U2 Pad 8 | Versorgung, siehe Abschnitt 6 | rot |
| Pad 2 | C1 100 nF und C2 10 µF | Pad 9 | Abblockung am Modul | — |
| Pico Pin 38, GND | direkt | U2 Pad 9 | Masse | schwarz |
| Pico Pin 23, GND | direkt | dieselbe Masse | kurze Signalmasse neben SPI | schwarz |
| Pico Pin 22, GP17 | R1 33 Ω | U2 Pad 1, CS | Modul → Pico | orange |
| CS-Leitung, Modulseite | R6 10 kΩ | 3,3 V | Pull-up, Leerlauf High | — |
| Pico Pin 26, GP20 | R2 33 Ω | U2 Pad 3, IRQ | Pico → Modul | weiß |
| IRQ-Leitung, Modulseite | R7 10 kΩ | GND | Pull-down, Leerlauf Low | — |
| Pico Pin 21, GP16 | R3 33 Ω | U2 Pad 5, MOSI | Modul → Pico | grün |
| Pico Pin 24, GP18 | R4 33 Ω | U2 Pad 7, SCK | Modul → Pico | gelb |
| Pico Pin 25, GP19 | R5 33 Ω | U2 Pad 10, MISO | Pico → Modul | blau |
| U2 Pad 12, ANT | Draht | AE1 | 868 MHz | blank |

Pad 4 und Pad 6 bleiben offen. Die ICSP-Pads `MCLR`, `VDD`, `GND`, `DAT` und `CLK` auf dem Modul sind die Programmierschnittstelle des PIC. Sie werden nicht mit dem Pico verbunden. `CLK` dort ist nicht SCK.

### Messleiste J1

J1 wird auf der Modulseite von R1 bis R5 angeschlossen, also zwischen Widerstand und Modul.

| Stift | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- |
| Signal | CS | IRQ | MOSI | SCK | MISO | GND |

## 5. Modulstecker

Die maßstäbliche Zeichnung steht im HTML. Pad 1 ist eckig. Obere Reihe von links: 1 CS, 3 IRQ, 5 MOSI, 7 SCK, 9 GND. Untere Reihe: 2 ist 3,3 V, 4 frei, 6 frei, 8 Batterieplus, 10 MISO. Weiter rechts: 11 nur bei Durchgang nach Masse, 12 Antenne. Das Raster ist 2,54 mm. Die Antennenpads liegen etwa 29 mm neben Pad 2.

Vor dem Löten die eckige Bohrung am eigenen Modul suchen und mit der Zeichnung zur Deckung bringen. Liegt die eckige Bohrung auf der anderen Reihe, die beiden Reihen tauschen und Pad 9 gegen die Modulmasse durchklingeln.

Quelle der Padfolge: Footprint `Tho85:W2-MM`.

## 6. JP1 und die zwei Modulvarianten

Pad 2 ist die Logikversorgung und liegt immer an 3,3 V des Pico. Pad 8 ist der Batteriepluspol. JP1 verbindet Pad 8 mit derselben 3,3-V-Schiene.

| Modul | Woran zu erkennen | JP1 |
| --- | --- | --- |
| Rot, aus Blitzleuchte W2-SVP-630 / CP-LED | Keine Lithiumzelle. Das Gerät hat das Modul mit 3,3 V versorgt. | geschlossen |
| Schwarz, Zelle ausgelötet | Zelle entfernt und ihr Pluspol mit Pad 8 verbunden. So von C19HOP geprüft. | geschlossen |
| Schwarz, Zelle bleibt eingebaut | Fest verbaute 3-V-Lithiumzelle bleibt die Versorgung des Moduls. | offen |

**JP1 bleibt offen, solange eine Lithiumzelle eingebaut ist.** Eine geschlossene Brücke legt die Zelle parallel an den 3,3-V-Regler des Pico. Das gilt auch für den ersten Probebetrieb.

Der Strom des Moduls liegt beim Senden um 30 mA, beim Empfang um 20 mA. Der 3V3-Pin des Pico darf extern bis 300 mA liefern. Die Versorgung aus Pin 36 ist dafür ausgelegt. Eine externe 5-V-Quelle, falls später nötig, kommt an Pin 39 (VSYS), niemals an Pin 36.

## 7. Antenne und Aufstellung

- Viertelwelle 86 mm oder Halbwelle 173 mm, gerechnet für 868 MHz. Die Länge zählt ab dem Antennenpad.
- Der Draht steht senkrecht und frei. Er liegt nicht über der Pico-Platine.
- Die 2,4-GHz-Antenne des Pico 2 W sitzt an der Kante gegenüber dem USB-Stecker. Die Aussparung dort ist 14 mm × 9 mm und bleibt frei von Metall, Massefläche und Draht.
- Das Gehäuse der Brücke ist kein geschlossener Metallkasten.
- Pad 11 nur dann an GND legen, wenn es am unversorgten Modul Durchgang zu Pad 9 hat. Sonst offen lassen.

## 8. Reihenfolge

1. Pico vom USB trennen. Modul nicht versorgen.
2. Pad 1 am Modul finden (eckiges Pad) und mit Abschnitt 5 vergleichen. Pad 9 muss Durchgang zur Modulmasse haben.
3. Verdrahten nach Abschnitt 4. JP1 in der Stellung aus Abschnitt 6 vorbereiten. Die 3,3-V-Leitung zum Modul bleibt bis nach dem nächsten Schritt offen.
4. Unversorgt auf Kurzschluss prüfen: 3,3 V gegen GND darf nicht durchklingeln. CS, IRQ, MOSI, SCK und MISO dürfen untereinander und gegen 3,3 V und GND nicht durchklingeln. Der 10-kΩ-Pull-up und der 10-kΩ-Pull-down zeigen dabei einen hohen Widerstand, keinen Kurzschluss.
5. Pico allein per USB starten und die aktuelle Firmware per OTA einspielen. Im Log steht `SPI-Slave gestartet`. Die Webseite `http://wisafe2-pico.local/` antwortet weiter. IRQ (GP20) ist dann Ausgang und Low.
6. Erst danach die 3,3-V-Leitung zum Modul schließen. Vor dieser Firmware kann ein freier MISO-Pin das Modul beim Hochlauf mit Zufallsdaten füttern.

## 9. Was die Firmware elektrisch tut

- SPI-Slave auf Hardware-SPI0, Modus 0, MSB zuerst. Die Schleife läuft auf Kern 1 und liegt im RAM.
- GP16 Empfang (MOSI des Moduls), GP19 Senden (MISO des Moduls), GP18 Takt, GP17 Chip-Select, GP20 IRQ.
- Chip-Select ist aktiv Low und rahmt jedes einzelne Byte. IRQ liegt in Ruhe auf Low. Ein empfangenes Byte wird mit einem High-Puls von 8 µs auf IRQ quittiert. MISO gibt dabei `0x00` aus.
- Befehle an das Modul werden noch nicht gesendet. Der Init-Befehl `D3 19 50 00 7E` und die erwartete Antwort `46 7E` folgen später. Bis dahin kann das Modul stumm bleiben.
- Die eingebaute ESPHome-Komponente `spi:` ist nur ein Master und wird nicht eingetragen.

## 10. Quellen der Padfolge und der Pegel

- Tho85/ws2mqtt, Footprint `Tho85:W2-MM` und Schaltplan: Padnummern, direkte 3,3-V-Verbindung ohne Pegelwandler, Lötbrücke „Module power“ an Pad 8.
- C19HOP/WiSafe2-to-HomeAssistant-Bridge: Modul ist SPI-Master, IRQ im Ruhezustand Low, 5-V-Nano nur mit Pegelwandler. Für diesen Aufbau entfällt der Wandler, weil der Pico 2 W auf 3,3 V liegt.
- NikNakk/fireangel-pro-connected-esphome: beide 3,3-V-Pads an dieselbe 3,3-V-Quelle, MOSI vom Modul zum Host, MISO vom Host zum Modul.
- pimvanoerle/fireangle: rote Spenderplatine W2-SVP-630L, Si4431 plus PIC16LF1936, Antennenlängen, Warnung vor den ICSP-Pads.
- Raspberry Pi Pico 2 W Datasheet: Pin 36 ist 3V3-Ausgang, externe Last unter 300 mA, GPIO23/24/25/29 gehören dem CYW43439, Antennenaussparung an der unteren Kante.

Revision 2 · 7. Oktober 2026. Zum Ausdruck die HTML-Fassung auf A4 mit Skalierung 100 % verwenden.

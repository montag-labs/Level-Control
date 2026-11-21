# Salzstand Laser: Trägerplatine und Gehäuse

Nov 21, 2026 · @Marc

Stand der Trägerplatine (bestellt bei JLCPCB, Produktionsdateien bestätigt) und des Gehäuses mit allem, was du für Code, Montage und Konstruktion brauchst.

## Pinbelegung (Software)

Der I2C-Bus liegt auf GPIO3 (SDA) und GPIO4 (SCL), INT auf GPIO2, XSHUT auf GPIO1.

| Sensor (VL53L1X) | ESP32-C3 | Hinweis |
| --- | --- | --- |
| SDA | GPIO3 | I2C-Daten, 10 kΩ Pull-up auf dem Sensormodul (gemessen) |
| SCL | GPIO4 | I2C-Takt, 10 kΩ Pull-up auf dem Sensormodul (gemessen) |
| INT (GPIO1 am Sensor) | GPIO2 | Strapping-Pin, mit dem Sensor-Pull-up unkritisch |
| XSHUT | GPIO1 | Sensor an/aus |
| VIN | 3V3 | Versorgung des Sensors |
| GND | G | Masse |

```cpp
#define PIN_SDA   3
#define PIN_SCL   4
#define PIN_INT   2
#define PIN_XSHUT 1

Wire.begin(PIN_SDA, PIN_SCL);
```

Vor der Sensor-Initialisierung muss XSHUT auf HIGH gesetzt werden. GPIO0 ist auf der Platine nicht beschaltet.

## Trägerplatine

Der ESP32-C3 wird flach auf die Platine gelötet, der Sensor von unten in 6 Pins gesteckt und das USB-Kabel an zwei Pads angelötet.

| Parameter | Wert |
| --- | --- |
| Platine | 32,5 × 20,2 mm, 1,0 mm dick, 2 Lagen, Eckenradius 0,5 mm |
| Oberfläche | LeadFree HASL |
| Bestellung | JLCPCB, Production File bestätigt, Versand EuroPacket (4,93 €) |
| Gerber-Dateien | Traegerplatine\_ESP\_USBKabel\_v2.zip (identisch mit dem Upload bei JLCPCB) |
| ESP-Pads | 2 Reihen à 8, Raster 2,54 mm, Reihenabstand 16 mm (Lochmitte), 18 mm außen; 5V-Loch 3 mm vom USB-Rand |
| Sensor-Pins | 6 Durchkontaktierungen Ø 1,0 mm, Raster 2,54 mm |
| Kabel-Pads | Ø 2,4 mm, Bohrung 1,2 mm; G bei x = 25,98 / y = 3,9 mm, 5V bei x = 25,98 / y = 7,4 mm |
| Kabelbinder | 2 Löcher Ø 3,2 mm bei x = 29,58 mm, y = 2,15 und 9,15 mm (Zugentlastung) |

Koordinaten gelten in der Draufsicht (Deckel- und ESP-Seite) mit der USB-Seite des ESP rechts und dem Ursprung links unten.

**Verbindungen auf der Platine**

- XSHUT zu GPIO1, INT zu GPIO2, SDA zu GPIO3, SCL zu GPIO4 laufen gerade auf der Oberseite.
- VIN zu 3V3 und GND zu G sind gekreuzt, weil die Reihenfolge am Sensor (GND, VIN) und am ESP (G, 3V3) vertauscht ist. GND geht über eine Via auf die Unterseite.
- Das ESP-Pad 5V und das ESP-Pad G sind mit den Kabel-Pads 5V und G verbunden. Darauf liegt die Versorgung des Systems.
- GPIO0 und die 8 Pads der freien Reihe sind nur mechanisch, ohne Verbindung.
- Die USB-Datenleitungen des ESP werden nicht auf die Platine geführt.

**Hinweise**

- Die Antenne des ESP sitzt am Ende gegenüber der USB-Buchse und braucht kupferfreien Raum. Die Platine endet dort 0,32 mm hinter der ESP-Kante.
- Die DFM-Meldung "negative soldermask expansion" an den 6 Sensorlöchern ist gewollt: Die Lackmaske öffnet dort nur 1,3 mm.

## Aufbau und Höhe im Gehäuse

Der Stapel wird um 1,96 mm niedriger als mit dem alten Abstandshalter. Deine 12,8 mm reichen also, ohne dass du die Gehäusegröße ändern musst.

**Stapel von oben nach unten**

1. Deckel mit TPU-Dichtung
2. ESP32-C3, Bauteile nach oben
3. Trägerplatine (1,0 mm)
4. Kunststoffkörper der Sensor-Stiftleiste (2,54 mm)
5. Sensor VL53L1X, Rückseite nach oben, Fenster nach unten

| Maß | Alt (Abstandshalter) | Neu (Trägerplatine) |
| --- | --- | --- |
| Abstand Sensorplatine zu ESP | 5,5 mm (3 mm Platte + 2,5 mm Zylinder) | 3,54 mm (1,0 + 2,54) |
| Differenz |  | 1,96 mm niedriger |

Prüfformel: Dicke der Sensorplatine + 3,54 mm + ESP-Gesamthöhe (ESP-Unterseite bis Oberkante USB-C) ≤ 12,8 mm. Die Dicke der Sensorplatine und die ESP-Höhe habe ich nicht, miss sie, wenn du die Reserve genau kennen willst.

**Gehäuse (aus deiner 3MF Salzstand\_Laser\_v2)**

| Teil | Maße |
| --- | --- |
| Wanne | außen 40 × 27 mm, Wand 3 mm, innen 34 × 21 mm, 14 mm hoch |
| Grundplatte | Oberkante bei z = 1,2 mm, daraus ergeben sich 12,8 mm Innenhöhe bis z = 14 mm |
| Deckel | 54 × 27 × 3 mm mit Ohren, Schraubenloch Ø 3,2 mm |
| Schraubenhalter | 7 × 7 mm, Loch Ø 4,4 mm (Einsatz) |
| Kabelkerbe | x = 37–40 mm, y = 3–9,5 mm, z = 8–14 mm, mit Nuten für den Stopfen |

**Was sich im Gehäuse ändert**

- Der durchgehende Abstandshalter entfällt. Du brauchst nur einen Ausschnitt für die Trägerplatine (etwa 33 × 21 mm) und eine Kabelführung rechts.
- Rechts über der Trägerplatine muss das Kabel samt Kabelbinder durchpassen. Die Platinenoberseite liegt bei Sensorplatinendicke + 3,54 mm über der Sensorunterseite.
- Optional ein leichter Niederhalter von oben (Anschlag an der Dichtung oder Schaumstoff), damit der ESP nicht wackelt.

## Dichtung (TPU 85A)

Die Dichtung passt geometrisch und reicht gegen Schwitzwasser. Gegen Druck oder Flutung ist sie nicht ausgelegt und nicht geprüft.

**Befund aus der 3MF**

- Die Dichtung ist 1,0 mm dick (z = 14–15 mm) und liegt als Ring mit 3 mm Breite auf der Wandoberkante. Außenkontur und Innenöffnung (34 × 21 mm) sind deckungsgleich mit Wand und Deckel.
- Der Kabelstopfen füllt die Kerbe ohne Überschneidung mit der Wand, aber mit 0 mm Spiel. Seine Bohrung hat Ø 4,0 mm und ist rundum geschlossen.
- Der Spalt zwischen Wandoberkante und Deckel (1,0 mm) entspricht der Dichtungsdicke. Es gibt keinen Anschlag, die Verpressung hängt allein vom Schraubenmoment ab.
- Die Schrauben sitzen nur mittig an den kurzen Enden. Der Deckel (3 mm) kann sich zwischen den Schrauben aufbiegen, auf den Langseiten kann dort ein Spalt entstehen. Das ist eine grobe Abschätzung, nicht getestet.

**Entscheidungen und Änderungen**

| Punkt | Entscheidung |
| --- | --- |
| Dichtung und Deckel | bleiben wie modelliert, für Schwitzwasser ausreichend |
| Überstand | Du hast den Deckel an den Langseiten um 0,5 mm verlängert und die Dichtung nach unten überstehen lassen. Die Fase an der Dichtungsunterkante lenkt das Wasser zur Fuge zurück, ein Tropfeffekt entsteht so nicht. Du lässt es erst einmal so. |
| Kabelbohrung | 0,3–0,5 mm kleiner als das Kabel, als Tropfenform drucken; Stopfen 0,1–0,2 mm größer als die Kerbe |
| Kabelführung | nach unten oder zur Seite, mit Schlaufe (Tropfen laufen unten ab) |
| Wenn es doch undicht wird | Überstand auf 1,0–1,5 mm vergrößern, Fase unten entfernen, ggf. Nut unter dem Überstand; mehr Schrauben an den Langseiten oder steiferer Deckel |

**Weitere Maßnahmen**

- Schutzlack (Conformal Coating) auf Trägerplatine, ESP und Sensorplatine. Sensorfenster und Pins ausnehmen. Das schützt gegen feuchte Luft im Gehäuse besser als die Dichtung.
- Sensorkappe: Sie sitzt stramm in der Öffnung und soll später von innen verklebt werden. Nimm 2K-Epoxid oder neutral vernetzendes Silikon, kein Cyanacrylat (kann einen weißen Schleier auf der Optik hinterlassen) und kein Essigsäure-Silikon. Verklebe die Kappe vor dem Einbau des Sensors und lass sie ganz aushärten.

## Offene Punkte

- [ ] Kabeldurchmesser messen, danach die Bohrung im Stopfen festlegen (Kabel minus 0,3–0,5 mm).
- [ ] Dicke der Sensorplatine und Gesamthöhe des ESP messen, damit die Reserve in den 12,8 mm bekannt ist.
- [ ] Das Kabel hat im Stopfen keinen Schlitz. Es muss mit dem abgeschnittenen Ende von außen eingefädelt werden, ein Stecker passt nicht durch.
- [ ] Das Sensorfenster in der Grundplatte ist noch nicht geprüft, Kondenswasser auf dem Fenster kann die Messung stören.
- [ ] Die Platinen kommen frühestens nach 3 Tagen Produktion plus 4–7 Tagen EuroPacket. Danach Pinbelegung testen (SDA 3, SCL 4, INT 2, XSHUT 1).

# Zusatzhardware: Sensoren und Haptik

Stand: 2026-10-07 · geplant, noch nicht angeschlossen. Bis dahin liefert
`app/cpp/sensors/SimulatedSensors` plausible Werte über MQTT an Home
Assistant; die echten Treiber implementieren später dieselbe Schnittstelle
(`app/cpp/sensors/SensorReadings.h`, `ISensorSource`).

| Baustein | Anschluss | Versorgung | Messwerte |
|---|---|---|---|
| HLK-LD6004 mmWave | UART: TX/RX auf GPIO2 / GPIO3; optional Presence-OUT auf GPIO4 | eigener 3,3-V-Regler aus 5 V | presence, target_count, target_1 x/y/z, distance, speed |
| SHT45 Temperatur/Feuchte | I²C: GPIO7 = SDA, GPIO8 = SCL | 3,3 V | temperature, humidity |
| DRV2605L Haptik-Treiber + 1× LRA | I²C (GPIO7/GPIO8), LRA direkt am DRV2605L | 5 V | – (Ausgang: Haptik-Effekte) |
| 4× Piezo-Drucksensor | über passive Schutz-/Clamp-/Envelope-Schaltung zusammengeführt, analog auf GPIO21 (ADC); **nie ohne Schutzschaltung** | – | press_force, Druck-Gesten |
| Umgebungslicht (optional VEML7700 oder BH1750) | I²C (GPIO7/GPIO8) | 3,3 V | ambient_lux |
| GPIO5 | frei / Reserve | | |

Für die Treiber zu beachten: GPIO7/8 sind ein gemeinsamer I²C-Bus für drei
Bausteine (Adressen prüfen: SHT45 0x44, DRV2605L 0x5A, VEML7700 0x10 bzw.
BH1750 0x23/0x5C). Die BSP nutzt bereits einen I²C-Bus (Touch, Audio-Codecs) —
klären, ob GPIO7/8 ein zweiter Bus sind.

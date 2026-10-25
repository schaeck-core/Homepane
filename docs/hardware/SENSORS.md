# Zusatzhardware: Sensoren, Haptik, Mikrofone

Stand: 2026-10-19 · geplant, noch nicht angeschlossen. Das Gerät prüft beim
Start, was angeschlossen ist (`platforms/esp32p4/main/EspSensors.cpp`), und
liest SHTC3 und LD2410 schon aus; der Simulator nutzt
`app/cpp/sensors/SimulatedSensors` (mit `HOMEPANE_HW=none` u. a. ohne
Hardware). Erkennung und Ausblenden: `docs/plans/HARDWARE_CAPABILITIES.md`.

| Baustein | Anschluss | Messwerte / Funktion |
|---|---|---|
| BOS1921 + 4 Piezo-Aktuatoren | I²C: GPIO7 = SDA, GPIO8 = SCL; optional IRQ auf GPIO21 | Force-/Druck-Erkennung, Haptik, Piezo-Signaltöne |
| SHTC3 Temperatur/Feuchte | I²C (GPIO7/GPIO8) | temperature, humidity |
| SHT4x Temperatur/Feuchte (zum Testen, statt SHTC3) | eigener I²C-Bus: GPIO46 = SCL, GPIO47 = SDA, 100 kHz; Adresse 0x44 oder 0x45 wird erkannt; Pins wechseln später | temperature, humidity |
| VOC/Luftqualität (optional, z. B. ENS160 oder SGP40) | I²C (GPIO7/GPIO8) | ENS160: AQI, TVOC, eCO₂ · SGP40: VOC-Index |
| mmWave: HLK-LD2410 (zum Testen) oder HLK-LD6004 (später) | UART: TX GPIO2 / RX GPIO3 | LD2410: presence, Distanz bewegt/ruhend · LD6004: zusätzlich target_count, Position |
| 2× PDM-MEMS-Frontmikrofone (optional) | GPIO4 = PDM CLK, GPIO5 = PDM DATA | besseres Far-Field-Audio; ohne sie die Board-Mikrofone (ES7210) |

Ein Umgebungslichtsensor ist vorerst zurückgestellt.

GPIO7/8 ist der I²C-Bus der BSP (Touch GT911, Codecs ES8311/ES7210, 400 kHz),
kein zweiter Bus. Adressen (7 Bit): GT911 0x5D (bzw. 0x14), ES8311 0x18,
ES7210 0x40, BOS1921 0x44 (laut Datenblatt bestätigen), ENS160 0x52/0x53,
SGP40 0x59, SHTC3 0x70. Keine Kollision; ein späterer Sensor auf 0x44
(SHT4x, OPT3001) bräuchte eine andere Adresse.

Für den Bus: IRQ des BOS1921 auf GPIO21 anschließen (kein Polling), Haptik über
den internen Waveform-Synthesizer, Töne in Blöcken über den 1024-Sample-FIFO
statt Sample für Sample, Kabel zur Zusatzplatine kurz halten, keine zweiten
starken Pull-ups, SDA/SCL weg von den Hochspannungsleitungen der Piezos.
Details: Abschnitt *Bus load and risks* im Plan.

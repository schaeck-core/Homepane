# Sprachassistent: Gerät als Mikrofon/Lautsprecher für Home Assistant Assist

Stand: 2026-10-08 · Status: V0–V4 umgesetzt und auf dem Gerät im Einsatz (Weg A, eigenes Wakeword); offen: Langzeittest, Timer/Wecker, V5/V6

## Ziel

Das Panel hört auf ein Wakeword, streamt danach die Sprache an Home Assistant
(Assist-Pipeline: Spracherkennung → Intent/LLM → Sprachausgabe) und spielt die
Antwort über den eingebauten Lautsprecher ab. Die gesamte Sprachverarbeitung
läuft in Home Assistant bzw. den dort angebundenen Diensten (Whisper, LLM,
Piper); das Gerät ist nur Audio-Ein-/Ausgabe plus Wakeword und UI.

## Ausgangslage (aus dem Code)

- **Hardware** (BSP `components/esp32_p4_wifi6_touch_lcd_4b`): ein I2S-Bus im
  Duplex-Betrieb (MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 11).
  - Wiedergabe: Codec **ES8311** mit Verstärker-Enable an **GPIO53**.
  - Aufnahme: ADC **ES7210** (Mehrkanal; wie viele Mikrofone bestückt sind und
    ob ein Kanal als Echo-Referenz verdrahtet ist, ist noch zu prüfen —
    entscheidet über Echo-Unterdrückung, s. V5).
  - BSP-Funktionen vorhanden: `bsp_audio_init()`,
    `bsp_audio_codec_speaker_init()`, `bsp_audio_codec_microphone_init()`
    (über `esp_codec_dev`). Standard-I2S-Konfiguration: Mono, 16 bit,
    22 050 Hz — für Sprache brauchen wir **16 kHz**; da Ein- und Ausgabe sich
    den Takt teilen, laufen beide mit derselben Rate.
  - Bisher wird Audio nirgends initialisiert.
- **HA-Anbindung**: Es gibt bereits eine authentifizierte WebSocket-Verbindung
  (`HaClient`). Sie kann bisher nur Text-Nachrichten senden; für Audio braucht
  sie Binär-Frames.
- **Speicher**: App ~3,7 MB von 6 MB pro Slot; PSRAM reichlich.
  microWakeWord-Modelle sind klein (Größenordnung 50–100 KB) und passen in
  die App bzw. den Storage — keine eigene Partition nötig (s. E3).

## Wie das Gerät mit Home Assistant spricht

Zwei mögliche Wege; Empfehlung: mit A experimentieren, für das Produkt B
bewerten.

**A. Assist-Pipeline über die bestehende WebSocket-Verbindung**
(`assist_pipeline/run`, so macht es auch das HA-Frontend):
1. `assist_pipeline/run` mit `start_stage: "stt"` (bzw. `"wake_word"`, s. V3),
   `end_stage: "tts"`, `input: {sample_rate: 16000}`.
2. Auf das Event `stt-start` warten, es liefert `stt_binary_handler_id`.
3. Audio als Binär-Frames senden: 1 Byte Handler-ID + PCM (16 kHz, 16 bit,
   mono). Ende des Audios: ein Frame nur mit dem Handler-ID-Byte. Das Ende
   der Sprache erkennt HA selbst (Events `stt-vad-start`/`stt-vad-end`).
4. Events `stt-end` (erkannter Text), `intent-progress`, `intent-end`
   (Antworttext), `tts-start`/`tts-end`.

**Gestreamte Antwort (geringere Latenz)** — so macht es HA selbst für
Satelliten (Quellcode `assist_pipeline/run.py`, `wyoming/assist_satellite.py`):
- Schon `run-start` enthält `tts_output` mit `token`, `url`, `mime_type` und
  `stream_response`.
- Meldet ein `intent-progress`-Event `tts_start_streaming: true`, läuft die
  Sprachausgabe bereits, während das LLM noch antwortet. Das Gerät öffnet dann
  sofort die URL (`/api/tts_proxy/<token>.<format>`); HA liefert das Audio
  stückweise, sobald es erzeugt wird, und das Gerät spielt ab dem ersten
  Stück. Ohne dieses Event: URL bei `tts-end` öffnen (auch dann wird die
  Datei gestreamt, nicht erst komplett geladen).
- Voraussetzung auf HA-Seite: ein Konversations-Agent, der seine Antwort
  streamt (die LLM-Integrationen tun das), und eine TTS-Engine mit Streaming
  (Wyoming-Piper ja; für Kokoro zu prüfen).
- Format: Die WebSocket-API lässt das Ausgabeformat nicht wählen (das
  Wyoming-Satellitenprotokoll schon: dort kommt immer PCM). Wyoming-TTS
  liefert WAV; das Gerät spielt WAV/PCM direkt und braucht nur für MP3/FLAC
  einen Decoder (`mime_type` aus `run-start` entscheidet).

Vorteil: keine zusätzliche Einrichtung in HA, nutzt Token und Verbindung, die
wir schon haben. Nachteil: Das Gerät erscheint in HA nicht als
„Assist-Satellit" (keine Ankündigungen/Timer von HA aus).

**B. Wyoming-Satellit**: Das Gerät stellt einen kleinen TCP-Server mit dem
Wyoming-Protokoll bereit, HA bindet ihn über die Wyoming-Integration ein.
Vorteil: echtes Satelliten-Gerät in HA (Ankündigungen, Timer, Auswahl der
Pipeline im HA-UI). Nachteil: zweiter Kommunikationsweg, Discovery
(Zeroconf), mehr Code.

## HA-Seite (für den Test aufzusetzen)

Umgebung (E1/E2): HA OS als VM auf Proxmox (Intel i5 14. Gen, keine GPU);
ein Gaming-Server mit RTX 4090 (Whisper + Kokoro vorhanden) ist meist aus und
nur Ausweichlösung. Ziel: alles auf der CPU, LLM aus der Cloud.

Vor jedem Geräte-Code einmal über den Browser (Assist im HA-Frontend)
lauffähig machen:
- **Spracherkennung**: Add-on „Whisper" (faster-whisper, Wyoming), Sprache
  Deutsch. Auf der CPU mit `small-int8` beginnen und die Zeit pro Befehl
  messen; `base` ist schneller, versteht Deutsch aber schlechter. Der
  VM genug Kerne geben (Whisper skaliert mit Threads).
- **Konversation**: Cloud-LLM über eine HA-Integration mit Streaming und
  Tool-Aufrufen (nötig, damit es Geräte steuern kann). Gemma (Wunsch des
  Nutzers) zuerst prüfen: Welche Version bei welchem Anbieter, und ob
  Tool-Aufrufe dort unterstützt werden; als Vergleich ein schnelles
  Gemini-Flash-Modell über die offizielle Google-Integration. Option
  „Gerätesteuerung bevorzugt lokal behandeln" einschalten: einfache Befehle
  („Licht an") beantwortet dann der lokale HA-Agent ohne LLM-Umweg.
- **Sprachausgabe**: Zuerst Add-on „Piper" (Wyoming, deutsche Stimme
  `thorsten`/`kerstin`, Streaming-fähig) als Referenz. Kokoro danach: Das
  offizielle Kokoro-82M hat **keine deutsche Stimme**; es gibt ein deutsches
  Fine-Tune auf Thorsten-Voice-Daten (Hugging Face `Thorsten-Voice/Kokoro`,
  laut Beschreibung auf CPU in Echtzeit). Kokoro ist kein HA-Add-on: als
  Container auf Proxmox (LXC/VM) mit Wyoming-Server betreiben und in HA über
  die Wyoming-Integration einbinden; prüfen, ob der Server Streaming kann
  (sonst verlieren wir den Latenzvorteil).
- Eine **Assist-Pipeline** aus diesen drei; das Gerät nutzt die
  Standard-Pipeline oder eine konfigurierte Pipeline-ID.

**Ressourcen der HA-VM** (bisher 2 Kerne / 4 GB):
- Empfehlung **4–6 vCPU, 8 GB RAM**. Whisper ist der einzige große
  Verbraucher (`small-int8` ~1 GB RAM, `medium-int8` ~2–3 GB) und wird mit
  mehr Kernen direkt schneller; Piper braucht wenige hundert MB.
- In Proxmox den **CPU-Typ der VM auf `host`** stellen: Der Standardtyp
  verbirgt AVX2/AVX-VNNI, auf die faster-whisper (CTranslate2) für Tempo
  angewiesen ist.
- Im Whisper-Add-on Sprache fest auf `de` (spart die Spracherkennung) und
  kleine Beam-Size.
- Kokoro läuft nicht in der HA-VM, sondern als eigener Container (LXC,
  ~2 Kerne / 2 GB).

## UI (Design-Guide, `docs/design/canvas/VoiceAssistant*.dc.html`)

Zwei Entwürfe, gleiche Zustände: Wakeword → Zuhören → Verarbeiten →
(Antworten) → Erledigt / Nicht verstanden.

- **Variante A „Kreis"**: nur Kreise, Ringe, ein rotierender Bogen und Text
  (`VoiceStates.dc.html` nennt die LVGL-Bausteine: `lv_anim` für Größe/
  Deckkraft, `lv_arc`, Mikrofonpegel → Kreisgröße). Problemlos auf dem P4;
  wird zuerst gebaut.
- **Variante B „Aurora"**: Wellen aus fünf halbtransparenten Flächen mit
  Farbverlauf und Glüh-Effekt (Weichzeichner), ~130 Partikel beim
  Verarbeiten, Ring mit Verlauf. In LVGL machbar über die bereits aktive
  Vektorgrafik (ThorVG), aber teuer: Weichzeichner gibt es in LVGL 9.2 nicht
  direkt (Ersatz: in halber Auflösung zeichnen und hochskalieren, oder
  vorgerenderte Glüh-Sprites), und Software-Rendering einer 660×260-Fläche
  pro Frame belastet die CPU. Vor einer Entscheidung ein **Performance-
  Spike**: nur die Wellenfläche, gemessen in FPS und CPU-Last, während
  gleichzeitig Audio läuft.
- Darum: `VoiceController` (Zustände, Pegel, Texte) strikt von der Ansicht
  trennen; die Ansicht bekommt nur Zustand + Pegel + Text, so ist B später
  ein Austausch der Ansicht.
- **Abweichungen vom Entwurf, die die Technik vorgibt**:
  - Die Entwürfe zeigen den gesprochenen Text **Wort für Wort** während des
    Zuhörens. Whisper über HA liefert aber nur das Endergebnis (`stt-end`);
    der Text erscheint also erst nach dem Sprechen. Während des Zuhörens
    reagiert nur die Animation auf den Pegel.
  - Variante A hat keinen eigenen Zustand „Antwortet" (B schon). Mit
    gestreamter Antwort spricht das Gerät schon, während der Text noch
    entsteht — A bräuchte dafür eine Darstellung (z. B. Kreis pulsiert mit
    dem Ausgabepegel).
  - Die Geräte-Kachel in der Antwort („Deckenlicht · Wohnzimmer · 50 %")
    lässt sich aus `intent-end` ableiten (HA meldet die betroffenen
    Entitäten).
  - Fehlerfälle über „Nicht verstanden" hinaus: HA nicht erreichbar,
    Zeitüberschreitung, Mikrofon stumm.

## Phasen

**V0 – Audio-Bring-up (nur Gerät, kein HA)**
- **Stand 2026-10-06**: `platforms/esp32p4/main/audio_hw.c` (I2S 16 kHz
  Stereo, ES8311 + ES7210 geöffnet) und ein Debug-Server
  (`audio_debug.c`, Port 8990, nur mit `SMARTBOARD_AUDIO_DEBUG=1 idf.py
  reconfigure build`): `/audio/tone?hz=&ms=&amp=`, `/audio/record?sec=`
  (liefert Stereo-WAV), `/audio/last.wav`, `/audio/play?ch=l|r|both`,
  `/audio/stats`, `/audio/level?vol=&gain=`, `/audio/loopback?hz=&ms=&amp=`.
  Gemessen: Ruhe ca. −59 dBFS (Gain 30 dB); Lautsprecher-Ton kommt auf beiden
  Kanälen gleich laut an (~60 dB über Rauschen) → MIC1 und MIC2 sind zwei
  echte, eng benachbarte Mikrofone (Korrelation 0,95–0,98), **keine
  Echo-Referenz** auf MIC1/MIC2; ob MIC3/MIC4 eine liefern, in V5 prüfen
  (TDM). Offen: Sprachaufnahme anhören, Pegel für Sprache einstellen.
- `bsp_audio_init()` mit 16 kHz Duplex, Lautsprecher- und Mikrofon-Codec
  öffnen, Verstärker schalten, Lautstärke/Mikrofon-Verstärkung setzen.
- Test 1: Sinuston/kurzer WAV aus dem Flash abspielen.
- Test 2: 5 s aufnehmen, Pegel anzeigen (Debug-Seite), wieder abspielen; die
  Aufnahme zusätzlich über den Webserver des Geräts als WAV abrufbar machen,
  um sie am Rechner anzuhören (Rauschen, Übersteuerung, Kanäle).
- Klären: Kanalbelegung des ES7210 (Mikrofone, Echo-Referenz).
- Architektur: portables `IAudioAdapter` (open/read/write/close) in
  `app/cpp/voice/`, ESP-Implementierung im Plattform-Ordner, Simulator mit SDL
  (Mikrofon + Lautsprecher am Mac) — so lässt sich alles ab V2 auch am
  Desktop testen.
- Fertig, wenn: Aufnahme klingt sauber, Wiedergabe ohne Knacken/Aussetzer,
  UI bleibt flüssig, kein Speicherleck über 100 Durchläufe.

**V1 – HA-Pipeline aufsetzen** (parallel zu V0, s. o.)
- Fertig, wenn: „Schalte das Licht im Wohnzimmer an" im Browser-Assist
  funktioniert und Piper antwortet.

**V2 – Push-to-talk**
- **Stand 2026-10-07: funktioniert Ende-zu-Ende** (Entwickler-Build,
  Mikrofon-Button + vereinfachtes Overlay Variante A): Audio als Binär-Frames
  über die Dashboard-Verbindung, Antwort gestreamt (HA liefert **MP3**, die
  WebSocket-API lässt kein WAV wählen → Dekodierung auf dem Gerät mit
  minimp3, CC0), AGC + 80-Hz-Hochpass auf dem Gerät (Sprache sonst nur
  ~−40 dBFS auf Armlänge, jetzt ~−19 dBFS bei ~30 dB Rauschabstand).
  Gemessen (Whisper-Add-on `small-int8`, Beam 3, Initial Prompt; LLM
  DeepSeek über OpenRouter): Sprachende → Text **1,0 s**, → Antworttext
  **1,6 s**, erster Ton wenige ms danach. `turbo` erkennt kaum besser, braucht
  aber ~6 s auf der CPU. Ohne Initial Prompt / mit `tiny`/`base` war die
  Erkennung deutlich schlechter („Wie viel Uhr" → „Wie viel du").
- Offen in V2: Variante A vollständig nach Design (Antwort-Kachel, Zustand
  „Antwortet" mit Ausgabepegel, Fehlerzustände), Host-Tests für
  `VoiceController`, Feature aus dem Entwickler-Build in die normale Firmware
  holen (Schalter in den Settings).
- Mikrofon-Taste in der UI → Weg A ohne Wakeword: Audio streamen, Antwort
  abspielen.
- `HaClient`/WebSocket: Binär-Frames senden; Pipeline-Events verarbeiten.
- Sprachausgabe gestreamt laden (s. „Gestreamte Antwort"): HTTP-GET auf die
  TTS-URL beim ersten `intent-progress` mit `tts_start_streaming`, Audio
  stückweise über einen Ringpuffer in PSRAM an den Lautsprecher; WAV-Header
  auswerten (Rate/Kanäle), bei anderer Rate als 16 kHz resamplen.
- UI nach Design-Guide: Zustände Zuhören / Verarbeiten / Antwort, erkannter
  Text und Antworttext auf dem Display, Abbrechen.
- `VoiceController` (portable Zustandsmaschine, Host-Tests wie beim
  `UpdateController`).
- Fertig, wenn: Ende-zu-Ende per Taste funktioniert, Latenz gemessen
  (Ende der Sprache → Beginn der Antwort).

**V3 – Wakeword auf dem Gerät** (entschieden, kein Wakeword in HA)
- **microWakeWord** mit den fertigen Modellen „Okay Nabu" / „Hey Jarvis"
  (TensorFlow Lite Micro + esp-nn; ESPHome unterstützt damit ausdrücklich
  den ESP32-P4, inkl. größerem Task-Stack für die P4-optimierten Kernel).
  Audio verlässt das Gerät erst nach dem Wakeword.
- Ablauf: Mikrofon läuft dauerhaft in einen Ringpuffer → Merkmalsextraktion
  (Spektrogramm-Frontend) → Modell; bei Treffer startet V2-Ablauf mit
  `start_stage: "stt"` und `wake_word_phrase`, inkl. der letzten ~0,5 s aus
  dem Ringpuffer, damit der Satzanfang nicht verloren geht.
- **Lizenz prüfen**, bevor Code übernommen wird: ESPHomes C++-Komponenten
  stehen unter GPLv3 — für eine kommerzielle Firmware selbst auf TFLite Micro
  (Apache 2.0) aufbauen, nicht kopieren; die Lizenz der Modelle
  (`esphome/micro-wake-word-models`) ebenfalls prüfen.
- Alternative bei Problemen: esp-sr WakeNet (offiziell für ESP32-P4; braucht
  eine Modell-Partition, s. E3).
- Fertig, wenn: Erkennungsrate/Fehlalarme über einen Tag im Raum akzeptabel,
  CPU-Last und Speicher gemessen, UI bleibt flüssig.

**V4 – Feinschliff**
- **Stand 2026-10-07**: Settings (Ein/Aus mit Mikrofon-Abhängigkeit,
  Wakeword, HA-Assistent, UI A/B, Ton, Lautstärke); synthetisierte
  Bestätigungstöne (`app/cpp/voice/earcons.c`, vier Stile, Mikrofon während des
  Tons stumm); Endpunkt-Erkennung auf dem Gerät und Abbruchwörter; lange Texte
  scrollen in festen Boxen; fehlende Schriftzeichen werden ersetzt
  (`app/views/text_fit.c`); Host-Tests für den `VoiceController`.
  **Variante B bleibt experimentell** (Standard: A). Der PPA-Hochskalierer
  wurde entfernt: sein blockierender Aufruf kam manchmal nicht zurück und
  fror den LVGL-Task mit gehaltener Display-Sperre ein (Overlay schloss sich
  nicht) — die CPU-Variante ist langsamer (~20–30 ms/Frame), aber stabil.
- Offen: Langzeittest mit dauerhaftem Wakeword (Fehlauslösungen pro Tag,
  Speicher), Empfindlichkeit einstellbar, Ton-Auswahl final.
- **Stand 2026-10-08**: Hinweistöne blockierten den UI-Task 5+ s (`sinf` mit
  riesigen Winkeln in `earcons.c`, behoben); Mikrofon-Taste jetzt im
  Dashboard-Header (nur bei aktivem Assistenten), Texte der Overlays über die
  Sprachdateien (`app/i18n`).
- **Raumbezug — umgesetzt 2026-10-07** (`DeviceLink`, Details in
  `app/cpp/home_assistant/DeviceLink.h`): `device_id` des MQTT-Geräts geht in jede
  Anfrage; der Bereich muss in HA am Gerät „Smartboard“ gesetzt werden (das
  Panel darf ihn ohne Admin-Rechte nicht setzen, übernimmt ihn aber).
  Nicht live geprüft: „Licht aus“ trifft den Raum.
- **Timer/Wecker** (Nutzerwunsch 2026-10-08, offen): Assist-Timer gibt es nur
  für Satelliten mit Timer-Handler — prüfen, ob das über die
  WebSocket-Pipeline geht; sonst Helfer in HA.
- Ursprüngliche Planung des Raumbezugs (2026-10-07): HA nutzt den Bereich des
  anfragenden Geräts als bevorzugten Bereich, im eingebauten Agenten
  (`default_agent.py`, `preferred_area_id`) wie in den LLM-Tools
  (`llm.py`, `async_get_match_preferences`). Dafür braucht das Panel ein
  eigenes HA-Gerät und schickt dessen `device_id` in `assist_pipeline/run`
  mit (der Parameter existiert). Das Gerät entsteht mit der MQTT-Discovery
  (Bereich über `suggested_area` bzw. später `config/device_registry/update`);
  kein separates Mobile-App-Gerät vorher. Gewünscht: Der Raum folgt dem
  geladenen Dashboard (Vorbelegung = HA-Bereich mit passendem Namen), in den
  Settings fest überschreibbar.
- Lautstärke/Stummschaltung des Mikrofons in den Settings (mit sichtbarem
  Status), Ton beim Erkennen des Wakewords, Display aus dem Energiesparmodus
  wecken, laufende Medien leiser stellen.
- Entscheidung Weg A vs. B (Wyoming) fürs Produkt. Für B spricht: HA
  liefert die Antwort dort immer als PCM-Stream, das Gerät ist ein echter
  Satellit (Pipeline-Auswahl, Ankündigungen, Timer in HA).

**V6 – Externe Mikrofone / Lautsprecher (später, Idee 2026-10-07)**
- *Antwort auf einem Smart-Speaker* (Music Assistant): Das Panel hört zu wie
  bisher, spielt die Antwort aber nicht selbst, sondern lässt HA die TTS-URL
  auf einem gewählten `media_player` abspielen (z. B. als Ansage über Music
  Assistant). Text und Animation bleiben auf dem Panel. Auswahl „Antwort über"
  in den Settings.
- *Fremder Satellit hört zu, das Panel zeigt an*: Ein separates
  Mikrofon/ein ESPHome-Satellit läuft in HA als `assist_satellite`-Entität.
  Das Panel abonniert deren Zustand (idle/listening/processing/responding)
  und zeigt das Overlay; erkannter Text und Antwort ließen sich aus den
  Pipeline-Läufen holen (`assist_pipeline/pipeline_debug/*`) — zu prüfen,
  ob das zeitnah genug ist oder ob es einen besseren Weg gibt.

**V5 – Echo-Unterdrückung / Unterbrechen**
- Nur wenn ein Referenzkanal vorhanden ist: AEC (esp-sr AFE), damit das
  Gerät während der eigenen Ausgabe zuhören kann.

## Entscheidungen (2026-10-06)

- **E1 HA-Installation**: HA OS (VM auf Proxmox) → Whisper/Piper als
  Add-ons; Kokoro als eigener Container auf Proxmox per Wyoming.
- **E2 Rechenleistung**: Whisper und TTS auf der CPU (i5 14. Gen), LLM aus der
  Cloud (Gemma zuerst prüfen); RTX-4090-Server nur als Ausweichlösung.
- **E3 Wakeword-Partition**: mit microWakeWord nicht nötig. Nur falls wir auf
  esp-sr ausweichen: `model`-Partition hinter `storage` (dort sind 8 MB des
  32-MB-Flashs frei; `storage` und Config bleiben unberührt), braucht aber
  einmal USB, weil OTA die Partitionstabelle nicht ändern kann.
- **E4 Wakeword**: „Okay Nabu" / „Hey Jarvis" sind in Ordnung; Wakeword auf
  dem Gerät, nicht in HA.
- **Antwort gestreamt**, nicht als fertige Datei (s. „Gestreamte Antwort").

## Risiken

- Duplex auf einem I2S-Bus: Ein- und Ausgabe teilen Takt und Rate.
- Speicher/CPU: Wakeword dauerhaft + LVGL + TLS; P4 hat zwei Kerne — Audio
  auf einen Kern pinnen, UI auf den anderen.
- Latenz hängt fast vollständig an Whisper/LLM auf HA-Seite; Whisper auf
  der CPU ist der wahrscheinlich langsamste Schritt.
- Kokoro: keine offizielle deutsche Stimme, Streaming-Fähigkeit des
  Wyoming-Servers offen.

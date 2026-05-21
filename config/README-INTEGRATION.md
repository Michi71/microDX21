# VelvetKeys Zero 2 WH Integration Guide

Dieses Template fügt Zero 2 WH Support mit OLED Display und Rotary Encoder zu deinem VelvetKeys Projekt hinzu.

## Dateien in diesem ZIP

```
velvetkeys-zero2-starter/
├── README. md                # Diese Datei
├── README-INTEGRATION.md    # Integrationsanleitung
├── configure.sh            # Circle Build Konfiguration
├── Makefile               # Zero 2 WH Build
├── Rules.mk               # Circle Make Rules
├── config/
│   ├── config.txt         # Boot Config
│   └── cmdline.txt        # Kernel Args
├── src/
│   ├── kernel.cpp         # Main Kernel
│   ├── kernel. h
│   ├── audio_task.cpp     # Audio Processing
│   ├── audio_task.h
│   ├── gui_manager.cpp    # OLED GUI
│   ├── gui_manager.h
│   ├── encoder.cpp        # Rotary Encoder
│   ├── encoder.h
│   ├── u8g2_circle.cpp    # u8g2 Backend
│   └── u8g2_circle.h
└── include/
    └── hw_config.h        # Pin Definitions
```

## Integration in bestehendes VelvetKeys Projekt

### Option 1: Separater Branch (empfohlen)

```bash
cd velvetkeys
git checkout -b zero2-oled
# Extrahiere ZIP Inhalt
# Commit
git add .
git commit -m "Add Zero 2 WH support with OLED"
```

### Option 2: Parallele Builds

```bash
# In velvetkeys/ Verzeichnis
mkdir -p variants/zero2
# Kopiere Dateien aus ZIP nach variants/zero2/

# Makefile anpassen für Multi-Target Build
```

## Benötigte Submodules

```bash
# librdpiano (falls noch nicht vorhanden)
git submodule add https://github.com/Michi71/librdpiano.git

# u8g2 für OLED
git submodule add https://github.com/olikraus/u8g2.git lib/u8g2
```

## Build-Prozess

```bash
# 1. Toolchain Setup
export PATH=/path/to/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin:$PATH

# 2. Configure Circle
./configure. sh

# 3. Build Circle
cd circle-stdlib
make -j$(nproc)
cd ..

# 4. Build VelvetKeys Zero 2
make -j$(nproc)

# Output:  src/kernel8.img
```

## Hardware-Anschluss

```
Pi Zero 2 WH        Component
─────────────────────────────────
GPIO 2  (SDA)   →   OLED SDA
GPIO 3  (SCL)   →   OLED SCL
3.3V            →   OLED VCC
GND             →   OLED GND

GPIO 20         →   Encoder CLK (A)
GPIO 16         →   Encoder DT (B)
GPIO 26         →   Encoder SW (Button)
3.3V            →   Encoder +
GND             →   Encoder GND

GPIO 18         →   PCM5102 BCK
GPIO 19         →   PCM5102 LRCK (WS)
GPIO 21         →   PCM5102 DIN (DATA)
3.3V            →   PCM5102 VIN
GND             →   PCM5102 GND
```

## Pin-Anpassungen

Falls du andere Pins nutzen möchtest: 

Editiere `include/hw_config.h`:

```cpp
#define PIN_OLED_SDA        2   // ← Deine Pin-Nummer
#define PIN_OLED_SCL        3
#define PIN_ENCODER_A       20
// ... 
```

## Unterschiede zu RPi 5 Version

| Feature | RPi 5 (CM5) | Zero 2 WH |
|---------|-------------|-----------|
| Display | 800x480 DSI (LVGL) | 128x64 OLED (u8g2) |
| Input | Touch | Rotary Encoder |
| Cores | 4× A76 @ 2.4 GHz | 4× A53 @ 1 GHz |
| Target | RASPPI=5 | RASPPI=3 |
| Kernel | kernel_2712.img | kernel8.img |

## Testing

Nach erfolgreichem Build:

```bash
# SD Karte vorbereiten (FAT32)
mkdir sdcard
cp src/kernel8.img sdcard/
cp config/*. txt sdcard/

# Raspberry Pi Firmware hinzufügen
cp /path/to/rpi-firmware/bootcode.bin sdcard/
cp /path/to/rpi-firmware/start.elf sdcard/
cp /path/to/rpi-firmware/fixup.dat sdcard/

# SD Karte in Pi Zero 2 WH stecken, booten! 
```

## Serielle Konsole (Debug)

```bash
# GPIO 14 (TX) mit USB-Serial-Adapter verbinden
screen /dev/ttyUSB0 115200

# Zeigt Circle Boot-Messages und Logs
```

## Performance-Optimierung

Falls CPU-Last zu hoch:

1. **Resampling deaktivieren:**
   ```cpp
   // In audio_task.cpp: 
   // Ändere rdPiano Konstruktor auf native Sample-Rate
   m_pRDPiano = new rdPiano(AUDIO_SAMPLE_RATE);  // Nutzt 48 kHz direkt
   ```

2. **NEON-Optimierung aktivieren:**
   ```makefile
   # In Makefile:
   CFLAGS += -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
   ```

3. **Effekte optional:**
   ```cpp
   // In rdpiano/Config.h:
   #define ENABLE_CHORUS    1
   #define ENABLE_PHASER    0  // Deaktivieren
   #define ENABLE_DELAY     1
   ```

## Bekannte Issues

- **I2S Knacken:** Falls Audio knackst → `AUDIO_CHUNK_SIZE` erhöhen (512 statt 256)
- **OLED flackert:** I2C Clock auf 100 kHz reduzieren in `hw_config.h`
- **Encoder springt:** Zusätzlichen Kondensator (100nF) zwischen CLK und GND

## Nächste Schritte

1. ✅ Basic Build testen
2. ✅ OLED Display prüfen
3. ✅ Encoder-Funktion testen
4. ✅ Audio-Output verifizieren
5. ✅ MIDI Input testen
6. ⬜ GUI erweitern (alle Parameter)
7. ⬜ NEON-Optimierung
8. ⬜ SD-Card Preset-Speicherung

## Support

Bei Problemen:
1. Check Serial Console Output
2. Prüfe Pin-Verbindungen
3. Teste einzelne Komponenten (OLED, Encoder, Audio)
4. GitHub Issues erstellen mit Logs

Viel Erfolg!  🎹
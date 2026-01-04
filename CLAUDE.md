# GPS AtomS3 Project

## Hardware
- **MCU**: M5Stack AtomS3 (ESP32-S3)
- **GPS**: Atomic GPS Base v2.0 (AT6558 chipset, multi-constellation)
- **Display**: Built-in 128x128 LCD

## Pin Configuration
- GPS RX: GPIO 5 (TX from GPS module)
- GPS TX: Not connected (read-only)
- GPS Baud: 115200

## Libraries
- M5AtomS3 / M5Unified - Hardware abstraction
- TinyGPSPlus with MultipleSatellite - GPS parsing (M5Stack fork)
- Preferences - NVS storage for settings

## Features
- **3 Display Modes** (tap to cycle):
  - Main: Coordinates, satellite count, fix accuracy, UTC/PST time
  - Satellites: HDOP, fix quality, accuracy, checksum stats
  - Speed/Alt: Speed (km/h, mph), altitude (m, ft), heading
- **Brightness Control**: Long press to cycle (high/medium/low)
- **NVS Persistence**: Last position and brightness saved across reboots
- **Last Position Display**: Shows grey coordinates while searching for fix

## GPS Commands
The AT6558 uses PCAS commands. The MultipleSatellite library supports:
- `setSystemBootMode()` - hot/warm/cold start
- `setSatelliteMode()` - GPS/BDS/GLONASS/Galileo/QZSS

## Build
```bash
pio run           # Build
pio run -t upload # Upload
pio device monitor # Serial monitor (115200 baud)
```

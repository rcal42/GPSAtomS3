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
- NimBLE-Arduino - BLE beacon functionality

## Features
- **3 Display Modes** (tap to cycle):
  - Main: Coordinates, satellite count, fix accuracy, UTC/PST time, BLE status
  - Satellites: HDOP, fix quality, accuracy, checksum stats
  - Speed/Alt: Speed (km/h, mph), altitude (m, ft), heading
- **Brightness Control**: Long press to cycle (high/medium/low)
- **NVS Persistence**: Last position and brightness saved across reboots
- **Last Position Display**: Shows grey coordinates while searching for fix
- **BLE Beacon**: Broadcasts GPS position via Nordic UART Service (see below)

## GPS Commands
The AT6558 uses PCAS commands. The MultipleSatellite library supports:
- `setSystemBootMode()` - hot/warm/cold start
- `setSatelliteMode()` - GPS/BDS/GLONASS/Galileo/QZSS

## BLE Beacon
Broadcasts GPS position for SpotDocs integration.

**Device Identification:**
- Name: `AtomS3-GPS-XXXX` (XXXX = last 4 hex digits of MAC address)
- Device ID is persistent and unique per hardware unit

**Nordic UART Service (NUS):**
- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` (notify)
- RX Characteristic: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` (write)

**NMEA Sentences Transmitted:**
- `$PATOM,<id>,<name>*XX` - Sent on connect (custom device info)
- `$GPGGA,...*XX` - Sent every 1 second (standard position/fix data)

**Display Indicator:**
- Top-right of main view shows `BLE` (blue) when connected, `ble` (grey) when advertising

## Build
```bash
pio run           # Build
pio run -t upload # Upload
pio device monitor # Serial monitor (115200 baud)
```

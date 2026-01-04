/**
 * @file GPS.ino
 * @brief M5AtomS3 Atomic GPS Base v2.0 with Display
 *
 * @Hardware: M5AtomS3 + Atomic GPS Base v2.0 (115200 baud)
 * Uses MultipleSatellite library for multi-constellation support
 */

#include "M5AtomS3.h"
#include "MultipleSatellite.h"
#include <Preferences.h>
#include <NimBLEDevice.h>

// GPS Base v2.0 pins and baud rate
static const int RXPin = 5, TXPin = -1;
static const uint32_t GPSBaud = 115200;

// Create MultipleSatellite instance using Serial2
MultipleSatellite gps(Serial2, GPSBaud, SERIAL_8N1, RXPin, TXPin);

// BLE Nordic UART Service (NUS) UUIDs
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// BLE objects
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
bool bleConnected = false;
uint32_t bleConnectedCount = 0;
unsigned long lastBleTransmit = 0;
const unsigned long BLE_TRANSMIT_INTERVAL = 1000;  // Send GGA every 1 second

// Device ID (derived from MAC address)
char deviceName[20] = "AtomS3-GPS";
char deviceId[5] = "";  // Last 4 hex digits of MAC

// NVS storage for last known position
Preferences prefs;
double lastLat = 0.0, lastLng = 0.0;
bool hasLastPosition = false;
unsigned long lastSaveTime = 0;
const unsigned long SAVE_INTERVAL = 60000;  // Save every 60 seconds

// Display modes
enum DisplayMode { MODE_MAIN = 0, MODE_SATELLITES, MODE_SPEED_ALT, MODE_COUNT };
DisplayMode currentMode = MODE_MAIN;

// Brightness levels (0-255)
const uint8_t BRIGHTNESS_LEVELS[] = { 255, 128, 40 };  // High, Medium, Low
const uint8_t BRIGHTNESS_COUNT = 3;
uint8_t brightnessIndex = 0;

// Forward declarations
void displayMainView();
void displaySatelliteView();
void displaySpeedAltView();

// Forward declaration for sending device info
void sendBleDeviceInfo();

// BLE Server Callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleConnected = true;
        bleConnectedCount++;
        Serial.println("BLE client connected");
        // Send device info after a short delay (let connection stabilize)
        delay(100);
        sendBleDeviceInfo();
    }

    void onDisconnect(NimBLEServer* pServer) {
        bleConnected = false;
        Serial.println("BLE client disconnected");
        // Restart advertising
        NimBLEDevice::startAdvertising();
    }
};

// Calculate NMEA checksum (XOR of all characters between $ and *)
uint8_t calcNmeaChecksum(const char* sentence) {
    uint8_t checksum = 0;
    // Skip the leading '$'
    for (int i = 1; sentence[i] != '*' && sentence[i] != '\0'; i++) {
        checksum ^= sentence[i];
    }
    return checksum;
}

// Generate NMEA GGA sentence from current GPS data
String generateGGA() {
    char sentence[100];
    char timeStr[12] = "000000.000";
    char latStr[15] = "";
    char latDir = 'N';
    char lonStr[15] = "";
    char lonDir = 'E';
    int fixQuality = 0;
    int numSats = 0;
    float hdop = 99.9;
    float altitude = 0.0;

    // Time
    if (gps.time.isValid()) {
        snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d.%03d",
            gps.time.hour(), gps.time.minute(), gps.time.second(),
            gps.time.centisecond() * 10);
    }

    // Position (convert decimal degrees to NMEA format: ddmm.mmmm)
    if (gps.location.isValid()) {
        double lat = gps.location.lat();
        double lng = gps.location.lng();

        latDir = (lat >= 0) ? 'N' : 'S';
        lat = fabs(lat);
        int latDeg = (int)lat;
        double latMin = (lat - latDeg) * 60.0;
        snprintf(latStr, sizeof(latStr), "%02d%09.6f", latDeg, latMin);

        lonDir = (lng >= 0) ? 'E' : 'W';
        lng = fabs(lng);
        int lonDeg = (int)lng;
        double lonMin = (lng - lonDeg) * 60.0;
        snprintf(lonStr, sizeof(lonStr), "%03d%09.6f", lonDeg, lonMin);

        fixQuality = 1;  // GPS fix
    }

    // Satellites
    if (gps.satellites.isValid()) {
        numSats = gps.satellites.value();
    }

    // HDOP
    if (gps.hdop.isValid()) {
        hdop = gps.hdop.hdop();
    }

    // Altitude
    if (gps.altitude.isValid()) {
        altitude = gps.altitude.meters();
    }

    // Build GGA sentence (without checksum)
    snprintf(sentence, sizeof(sentence),
        "$GPGGA,%s,%s,%c,%s,%c,%d,%02d,%.1f,%.1f,M,0.0,M,,*",
        timeStr, latStr, latDir, lonStr, lonDir,
        fixQuality, numSats, hdop, altitude);

    // Calculate and append checksum
    uint8_t cs = calcNmeaChecksum(sentence);
    char result[110];
    snprintf(result, sizeof(result), "%s%02X\r\n", sentence, cs);
    // Remove the extra * that was in the template
    String ggaStr = String(result);
    ggaStr.replace("**", "*");

    return ggaStr;
}

// Initialize BLE with Nordic UART Service
void initBLE() {
    // Get MAC address and create unique device ID (last 4 hex digits)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(deviceId, sizeof(deviceId), "%02X%02X", mac[4], mac[5]);
    snprintf(deviceName, sizeof(deviceName), "AtomS3-GPS-%s", deviceId);

    Serial.printf("Device ID: %s\n", deviceId);
    Serial.printf("BLE Name: %s\n", deviceName);

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power for range

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    // TX Characteristic - for sending data to client (notify)
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX Characteristic - for receiving data from client (write)
    pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );

    pService->start();

    // Start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.printf("BLE advertising started as '%s'\n", deviceName);
}

// Send GGA sentence over BLE
void sendBleGGA() {
    if (!bleConnected || !pTxCharacteristic) return;

    String gga = generateGGA();
    pTxCharacteristic->setValue((uint8_t*)gga.c_str(), gga.length());
    pTxCharacteristic->notify();

    Serial.print("BLE TX: ");
    Serial.print(gga);
}

// Send device info over BLE (called on connect)
void sendBleDeviceInfo() {
    if (!bleConnected || !pTxCharacteristic) return;

    // Send custom sentence with device ID: $PATOM,<deviceId>,<deviceName>*XX
    char sentence[60];
    snprintf(sentence, sizeof(sentence), "$PATOM,%s,%s*", deviceId, deviceName);
    uint8_t cs = calcNmeaChecksum(sentence);
    char msg[70];
    snprintf(msg, sizeof(msg), "%s%02X\r\n", sentence, cs);
    String infoStr = String(msg);
    infoStr.replace("**", "*");

    pTxCharacteristic->setValue((uint8_t*)infoStr.c_str(), infoStr.length());
    pTxCharacteristic->notify();

    Serial.print("BLE TX (info): ");
    Serial.print(infoStr);
}

void updateDisplay() {
    switch (currentMode) {
        case MODE_MAIN:
            displayMainView();
            break;
        case MODE_SATELLITES:
            displaySatelliteView();
            break;
        case MODE_SPEED_ALT:
            displaySpeedAltView();
            break;
        default:
            displayMainView();
            break;
    }
}

void displayMainView() {
    AtomS3.Display.fillScreen(BLACK);
    AtomS3.Display.setTextColor(WHITE);
    AtomS3.Display.setFont(&fonts::Font2);
    AtomS3.Display.setTextSize(1);

    int y = 5;

    // Show satellite count
    AtomS3.Display.setCursor(5, y);
    if (gps.satellites.isValid()) {
        AtomS3.Display.setTextColor(GREEN);
        AtomS3.Display.printf("Sats: %d", gps.satellites.value());
    } else {
        AtomS3.Display.setTextColor(YELLOW);
        AtomS3.Display.print("Sats: --");
    }

    // BLE status indicator (top right)
    AtomS3.Display.setCursor(100, y);
    if (bleConnected) {
        AtomS3.Display.setTextColor(BLUE);
        AtomS3.Display.print("BLE");
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("ble");
    }
    y += 18;

    // Show fix status with accuracy
    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.setTextColor(GREEN);
        if (gps.hdop.isValid()) {
            // Convert HDOP to approximate accuracy (HDOP * 2.5m base accuracy)
            float accuracyM = gps.hdop.hdop() * 2.5;
            AtomS3.Display.printf("FIX OK ~%.0fm", accuracyM);
        } else {
            AtomS3.Display.print("FIX OK");
        }
    } else {
        AtomS3.Display.setTextColor(ORANGE);
        AtomS3.Display.print(hasLastPosition ? "Searching (last)" : "Searching...");
    }
    y += 18;

    // Show coordinates (monospace, right-justified, 5 decimal places)
    AtomS3.Display.setFont(&fonts::FreeMono9pt7b);
    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("%10.5f", gps.location.lat());
    } else if (hasLastPosition) {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.printf("%10.5f", lastLat);
    } else {
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.print("   --    ");
    }
    y += 16;

    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("%10.5f", gps.location.lng());
    } else if (hasLastPosition) {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.printf("%10.5f", lastLng);
    } else {
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.print("   --    ");
    }
    y += 18;
    AtomS3.Display.setFont(&fonts::Font2);

    // Show time if valid
    AtomS3.Display.setCursor(5, y);
    if (gps.time.isValid()) {
        AtomS3.Display.setTextColor(CYAN);
        AtomS3.Display.printf("%02d:%02d:%02d UTC",
            gps.time.hour(), gps.time.minute(), gps.time.second());
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("Time: --:--:--");
    }
    y += 16;

    // Show PST time (UTC-8)
    AtomS3.Display.setCursor(5, y);
    if (gps.time.isValid()) {
        int pstHour = gps.time.hour() - 8;
        if (pstHour < 0) pstHour += 24;
        AtomS3.Display.setTextColor(YELLOW);
        AtomS3.Display.printf("%02d:%02d:%02d PST",
            pstHour, gps.time.minute(), gps.time.second());
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("PST: --:--:--");
    }
    y += 16;

    // Debug: show chars received from GPS module
    AtomS3.Display.setCursor(5, y);
    AtomS3.Display.setTextColor(DARKGREY);
    AtomS3.Display.printf("Rx: %lu", gps.charsProcessed());
}

void displaySatelliteView() {
    AtomS3.Display.fillScreen(BLACK);
    AtomS3.Display.setFont(&fonts::Font2);
    AtomS3.Display.setTextSize(1);

    int y = 5;

    // Header
    AtomS3.Display.setCursor(5, y);
    AtomS3.Display.setTextColor(CYAN);
    AtomS3.Display.print("SATELLITES");
    y += 20;

    // Total satellite count
    AtomS3.Display.setCursor(5, y);
    if (gps.satellites.isValid()) {
        AtomS3.Display.setTextColor(GREEN);
        AtomS3.Display.printf("Total: %d in use", gps.satellites.value());
    } else {
        AtomS3.Display.setTextColor(YELLOW);
        AtomS3.Display.print("Total: --");
    }
    y += 18;

    // HDOP (precision indicator)
    AtomS3.Display.setCursor(5, y);
    AtomS3.Display.setTextColor(WHITE);
    if (gps.hdop.isValid()) {
        float hdopVal = gps.hdop.hdop();
        if (hdopVal < 1.0) {
            AtomS3.Display.setTextColor(GREEN);
            AtomS3.Display.printf("HDOP: %.1f (ideal)", hdopVal);
        } else if (hdopVal < 2.0) {
            AtomS3.Display.setTextColor(GREEN);
            AtomS3.Display.printf("HDOP: %.1f (excellent)", hdopVal);
        } else if (hdopVal < 5.0) {
            AtomS3.Display.setTextColor(YELLOW);
            AtomS3.Display.printf("HDOP: %.1f (good)", hdopVal);
        } else {
            AtomS3.Display.setTextColor(ORANGE);
            AtomS3.Display.printf("HDOP: %.1f (moderate)", hdopVal);
        }
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("HDOP: --");
    }
    y += 18;

    // Fix quality
    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        auto quality = gps.location.FixQuality();
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.print("Fix: ");
        switch (quality) {
            case TinyGPSLocation::GPS:
                AtomS3.Display.setTextColor(GREEN);
                AtomS3.Display.print("GPS");
                break;
            case TinyGPSLocation::DGPS:
                AtomS3.Display.setTextColor(GREEN);
                AtomS3.Display.print("DGPS");
                break;
            case TinyGPSLocation::RTK:
                AtomS3.Display.setTextColor(GREEN);
                AtomS3.Display.print("RTK");
                break;
            case TinyGPSLocation::FloatRTK:
                AtomS3.Display.setTextColor(YELLOW);
                AtomS3.Display.print("Float RTK");
                break;
            default:
                AtomS3.Display.setTextColor(YELLOW);
                AtomS3.Display.print("Standard");
                break;
        }
    } else {
        AtomS3.Display.setTextColor(ORANGE);
        AtomS3.Display.print("Fix: None");
    }
    y += 18;

    // Accuracy estimate
    AtomS3.Display.setCursor(5, y);
    if (gps.hdop.isValid()) {
        float accuracy = gps.hdop.hdop() * 2.5;
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("Accuracy: ~%.0fm", accuracy);
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("Accuracy: --");
    }
    y += 18;

    // Sentences/checksums stats
    AtomS3.Display.setCursor(5, y);
    AtomS3.Display.setTextColor(DARKGREY);
    AtomS3.Display.printf("OK:%lu Fail:%lu",
        gps.passedChecksum(), gps.failedChecksum());
}

void displaySpeedAltView() {
    AtomS3.Display.fillScreen(BLACK);
    AtomS3.Display.setFont(&fonts::Font2);
    AtomS3.Display.setTextSize(1);

    int y = 5;

    // Header
    AtomS3.Display.setCursor(5, y);
    AtomS3.Display.setTextColor(CYAN);
    AtomS3.Display.print("SPEED / ALT");
    y += 20;

    // Speed
    AtomS3.Display.setCursor(5, y);
    if (gps.speed.isValid()) {
        float speedKmh = gps.speed.kmph();
        float speedMph = gps.speed.mph();
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("%.1f km/h", speedKmh);
        y += 16;
        AtomS3.Display.setCursor(5, y);
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.printf("%.1f mph", speedMph);
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("Speed: --");
        y += 16;
    }
    y += 18;

    // Altitude
    AtomS3.Display.setCursor(5, y);
    if (gps.altitude.isValid()) {
        float altM = gps.altitude.meters();
        float altFt = gps.altitude.feet();
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("%.0f m", altM);
        y += 16;
        AtomS3.Display.setCursor(5, y);
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.printf("%.0f ft", altFt);
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("Altitude: --");
        y += 16;
    }
    y += 18;

    // Course/Heading
    AtomS3.Display.setCursor(5, y);
    if (gps.course.isValid() && gps.speed.isValid() && gps.speed.kmph() > 1.0) {
        float heading = gps.course.deg();
        const char* cardinal = TinyGPSPlus::cardinal(heading);
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.printf("Heading: %.0f %s", heading, cardinal);
    } else {
        AtomS3.Display.setTextColor(DARKGREY);
        AtomS3.Display.print("Heading: --");
    }
}

void setup() {
    auto cfg = M5.config();
    AtomS3.begin(cfg);

    Serial.begin(115200);
    Serial.println("GPS Starting...");

    // Load settings from NVS
    prefs.begin("gps", true);  // read-only mode
    if (prefs.isKey("lat")) {
        lastLat = prefs.getDouble("lat", 0.0);
        lastLng = prefs.getDouble("lng", 0.0);
        hasLastPosition = (lastLat != 0.0 || lastLng != 0.0);
        if (hasLastPosition) {
            Serial.printf("Loaded last position: %.6f, %.6f\n", lastLat, lastLng);
        }
    }
    if (prefs.isKey("bright")) {
        brightnessIndex = prefs.getUChar("bright", 0);
        if (brightnessIndex >= BRIGHTNESS_COUNT) brightnessIndex = 0;
        AtomS3.Display.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
        Serial.printf("Loaded brightness: %d\n", BRIGHTNESS_LEVELS[brightnessIndex]);
    }
    prefs.end();

    // Show splash screen
    AtomS3.Display.fillScreen(BLACK);
    AtomS3.Display.setTextColor(GREEN);
    AtomS3.Display.setTextDatum(middle_center);
    AtomS3.Display.setFont(&fonts::FreeSansBold18pt7b);
    AtomS3.Display.drawString("GPS", AtomS3.Display.width() / 2,
                              AtomS3.Display.height() / 2);

    // Initialize GPS (keep it simple - advanced commands may block)
    gps.begin();
    Serial.println("GPS initialized");

    // Initialize BLE beacon
    initBLE();

    delay(1000);
}

void loop() {
    // Update M5 button state
    AtomS3.update();

    // Handle long press for brightness control
    if (AtomS3.BtnA.wasHold()) {
        brightnessIndex = (brightnessIndex + 1) % BRIGHTNESS_COUNT;
        AtomS3.Display.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
        Serial.printf("Brightness: %d\n", BRIGHTNESS_LEVELS[brightnessIndex]);

        // Save to NVS
        prefs.begin("gps", false);
        prefs.putUChar("bright", brightnessIndex);
        prefs.end();
    }
    // Handle short press to cycle display modes (only if not a hold)
    else if (AtomS3.BtnA.wasClicked()) {
        currentMode = (DisplayMode)((currentMode + 1) % MODE_COUNT);
        Serial.printf("Display mode: %d\n", currentMode);
    }

    // Update GPS data
    gps.updateGPS();

    // Update display
    updateDisplay();

    // Send BLE GGA periodically when connected
    if (bleConnected && (millis() - lastBleTransmit > BLE_TRANSMIT_INTERVAL)) {
        lastBleTransmit = millis();
        sendBleGGA();
    }

    // Also output to serial for debugging
    if (gps.location.isValid()) {
        Serial.printf("Lat: %.6f, Lon: %.6f, Sats: %d\n",
            gps.location.lat(), gps.location.lng(), gps.satellites.value());

        // Save position to NVS periodically
        if (millis() - lastSaveTime > SAVE_INTERVAL) {
            lastSaveTime = millis();
            lastLat = gps.location.lat();
            lastLng = gps.location.lng();
            hasLastPosition = true;

            prefs.begin("gps", false);  // read-write mode
            prefs.putDouble("lat", lastLat);
            prefs.putDouble("lng", lastLng);
            prefs.end();
            Serial.printf("Saved position to NVS: %.6f, %.6f\n", lastLat, lastLng);
        }
    } else {
        Serial.printf("Searching... Chars: %lu, Sats: %d\n",
            gps.charsProcessed(),
            gps.satellites.isValid() ? gps.satellites.value() : 0);
    }

    // Check if GPS module is responding
    if (millis() > 5000 && gps.charsProcessed() < 10) {
        AtomS3.Display.fillScreen(RED);
        AtomS3.Display.setTextColor(WHITE);
        AtomS3.Display.setTextDatum(middle_center);
        AtomS3.Display.setFont(&fonts::Font2);
        AtomS3.Display.drawString("No GPS", AtomS3.Display.width() / 2,
                                  AtomS3.Display.height() / 2 - 10);
        AtomS3.Display.drawString("Check wiring", AtomS3.Display.width() / 2,
                                  AtomS3.Display.height() / 2 + 10);
        Serial.println("No GPS data received: check wiring");
    }

    delay(100);
}

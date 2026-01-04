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

// GPS Base v2.0 pins and baud rate
static const int RXPin = 5, TXPin = -1;
static const uint32_t GPSBaud = 115200;

// Create MultipleSatellite instance using Serial2
MultipleSatellite gps(Serial2, GPSBaud, SERIAL_8N1, RXPin, TXPin);

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

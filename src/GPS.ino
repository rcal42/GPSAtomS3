/**
 * @file GPS.ino
 * @brief M5AtomS3 Atomic GPS Base v2.0 with Display
 *
 * @Hardware: M5AtomS3 + Atomic GPS Base v2.0 (115200 baud)
 */

#include "M5AtomS3.h"
#include <TinyGPSPlus.h>

TinyGPSPlus gps;

// Feed GPS data while waiting
static void smartDelay(unsigned long ms) {
    unsigned long start = millis();
    do {
        while (Serial2.available()) {
            gps.encode(Serial2.read());
        }
    } while (millis() - start < ms);
}

void updateDisplay() {
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

    // Show fix status
    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.setTextColor(GREEN);
        AtomS3.Display.print("FIX OK");
    } else {
        AtomS3.Display.setTextColor(ORANGE);
        AtomS3.Display.print("Searching...");
    }
    y += 18;

    // Show coordinates (monospace, right-justified, 5 decimal places)
    AtomS3.Display.setFont(&fonts::FreeMono9pt7b);
    AtomS3.Display.setTextColor(WHITE);
    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.printf("%10.5f", gps.location.lat());
    } else {
        AtomS3.Display.print("   --    ");
    }
    y += 16;

    AtomS3.Display.setCursor(5, y);
    if (gps.location.isValid()) {
        AtomS3.Display.printf("%10.5f", gps.location.lng());
    } else {
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

void setup() {
    auto cfg = M5.config();
    AtomS3.begin(cfg);

    Serial.begin(115200);
    Serial.println("GPS Starting...");

    // GPS Base v2.0 uses 115200 baud on GPIO5 (RX)
    Serial2.begin(115200, SERIAL_8N1, 5, -1);

    AtomS3.Display.fillScreen(BLACK);
    AtomS3.Display.setTextColor(GREEN);
    AtomS3.Display.setTextDatum(middle_center);
    AtomS3.Display.setFont(&fonts::FreeSansBold18pt7b);
    AtomS3.Display.drawString("GPS", AtomS3.Display.width() / 2,
                              AtomS3.Display.height() / 2);

    delay(1000);
}

void loop() {
    // Update display
    updateDisplay();

    // Also output to serial for debugging
    if (gps.location.isValid()) {
        Serial.printf("Lat: %.6f, Lon: %.6f, Sats: %d\n",
            gps.location.lat(), gps.location.lng(), gps.satellites.value());
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

    smartDelay(1000);
}

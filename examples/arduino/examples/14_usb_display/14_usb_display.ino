#include <Arduino.h>

#include "UsbDisplay.h"

UsbDisplay usbDisplay;

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("USB JPEG display receiver");
    if (!usbDisplay.begin()) {
        Serial.printf("USB display setup failed: %s\n", usbDisplay.lastError());
        return;
    }
    Serial.println("Ready for the Espressif Windows USB display driver.");
}

void loop()
{
    usbDisplay.tick();
}

#include <Arduino.h>
#include <KnobWireless.h>

void setup() {
    Serial.begin(115200);
    delay(500);

    const int beginResult = ws_wireless_begin();
    Serial.printf("wireless begin result=%d\n", beginResult);
    if (beginResult != 0) {
        return;
    }

    ws_wifi_record_t records[10] = {};
    size_t count = 0;
    const int scanResult = ws_wifi_scan(records, 10, &count);
    Serial.printf("wifi scan result=%d count=%u\n", scanResult, (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
        Serial.printf("%u: %s RSSI=%d CH=%u auth=%u\n", (unsigned)(i + 1),
                      records[i].ssid[0] ? records[i].ssid : "<hidden>",
                      records[i].rssi, records[i].channel, records[i].authmode);
    }
}

void loop() {
    delay(1000);
}

#include <Arduino.h>

extern "C" void idf_example_app_main(void);

namespace {

void runIdfExample(void *)
{
    idf_example_app_main();
    vTaskDelete(nullptr);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    xTaskCreate(runIdfExample, "ble_example", 8192, nullptr, 5, nullptr);
}

void loop()
{
    delay(1000);
}

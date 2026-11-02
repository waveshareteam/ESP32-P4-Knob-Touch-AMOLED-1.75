#include <Arduino.h>

namespace {

constexpr size_t kBytesPerMiB = 1024UL * 1024UL;

void printMemorySize(const char *label, size_t bytes)
{
    Serial.printf("%s: %u bytes (%u MiB)\n", label, static_cast<unsigned>(bytes),
                  static_cast<unsigned>(bytes / kBytesPerMiB));
}

void printSystemInfo()
{
    Serial.println("ESP32-P4 system information");
    Serial.printf("Chip: %s, revision %u\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU frequency: %u MHz\n", ESP.getCpuFreqMHz());
    printMemorySize("Flash", ESP.getFlashChipSize());
    printMemorySize("PSRAM", ESP.getPsramSize());
    printMemorySize("Free heap", ESP.getFreeHeap());
    printMemorySize("Free PSRAM", ESP.getFreePsram());
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    printSystemInfo();
}

void loop()
{
    delay(5000);
    printSystemInfo();
}

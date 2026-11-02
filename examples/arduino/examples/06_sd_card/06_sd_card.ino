#include <Arduino.h>
#include <SD_MMC.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <pin_config.h>

namespace {

constexpr char kMountPoint[] = "/sdcard";
constexpr char kPayload[] = "ESP32-P4 Arduino SD card write/read verification\n";
constexpr uint32_t kSdFrequencyKhz = 10000;

bool writeAll(int fd, const char *data, size_t length)
{
    size_t written = 0;
    while (written < length) {
        const ssize_t result = write(fd, data + written, length - written);
        if (result <= 0) {
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

bool verifyTemporaryFile(char *path, size_t pathSize)
{
    int fd = -1;
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        snprintf(path, pathSize, "%s/sd06_%08lx_%08lx.txt", kMountPoint,
                 static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(millis()));
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) {
            break;
        }
    }
    if (fd < 0) {
        Serial.printf("Temporary file creation failed: errno=%d\n", errno);
        return false;
    }

    const bool writeOk = writeAll(fd, kPayload, strlen(kPayload));
    const bool closeAfterWriteOk = close(fd) == 0;
    if (!writeOk || !closeAfterWriteOk) {
        Serial.printf("Temporary file write failed; no cleanup was attempted for %s. Stop and check the card.\n", path);
        return false;
    }

    fd = open(path, O_RDONLY);
    char readback[sizeof(kPayload)] = {};
    const ssize_t bytesRead = fd >= 0 ? read(fd, readback, sizeof(readback)) : -1;
    const bool contentMatches = bytesRead == static_cast<ssize_t>(strlen(kPayload)) &&
                                memcmp(readback, kPayload, strlen(kPayload)) == 0;
    const bool closeAfterReadOk = fd >= 0 && close(fd) == 0;
    if (!contentMatches || !closeAfterReadOk) {
        Serial.printf("Temporary file read-back verification failed; no cleanup was attempted for %s. Stop and check the card.\n", path);
        return false;
    }

    if (unlink(path) != 0) {
        Serial.printf("Temporary file verification passed but deletion failed: errno=%d path=%s\n", errno, path);
        return false;
    }
    return true;
}

void runSdCardTest()
{
    if (!SD_MMC.setPins(KNOB_PIN_SD_CLK, KNOB_PIN_SD_CMD, KNOB_PIN_SD_D0,
                        KNOB_PIN_SD_D1, KNOB_PIN_SD_D2, KNOB_PIN_SD_D3)) {
        Serial.println("SDMMC pin configuration failed.");
        return;
    }
    if (!SD_MMC.setPowerChannel(KNOB_SDMMC_LDO_CHANNEL)) {
        Serial.println("SDMMC LDO configuration failed.");
        return;
    }
    if (!SD_MMC.begin(kMountPoint, true, false, kSdFrequencyKhz)) {
        Serial.println("SD card mount failed.");
        return;
    }

    const uint64_t totalBytes = SD_MMC.totalBytes();
    const uint64_t usedBytes = SD_MMC.usedBytes();
    const uint64_t freeBytes = totalBytes >= usedBytes ? totalBytes - usedBytes : 0;
    Serial.printf("Mounted 1-bit SDMMC at %lu kHz: card=%llu MiB total=%llu MiB free=%llu MiB\n",
                  static_cast<unsigned long>(kSdFrequencyKhz),
                  static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(totalBytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(freeBytes / (1024ULL * 1024ULL)));

    char temporaryPath[80] = {};
    const bool verified = verifyTemporaryFile(temporaryPath, sizeof(temporaryPath));
    Serial.println(verified ? "Write/read/delete verification passed." : "Write/read/delete verification failed.");
    SD_MMC.end();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("SD card mount and verification example");
    runSdCardTest();
}

void loop()
{
    delay(1000);
}

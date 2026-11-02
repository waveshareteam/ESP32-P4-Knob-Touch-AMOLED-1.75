#include <Arduino.h>
#include <KnobBt.h>

struct DeviceRecord {
  ws_ble_result_t result;
  char name[31];
};

static QueueHandle_t resultQueue;
static DeviceRecord devices[32];
static uint8_t deviceCount;
static volatile uint32_t receivedCount;
static volatile uint32_t queueDropCount;
static uint32_t windowStart;
static bool reported;

static void resultCallback(const ws_ble_result_t *result) {
  if (!result) return;
  ++receivedCount;
  if (!resultQueue || xQueueSend(resultQueue, result, 0) != pdPASS) ++queueDropCount;
}

static void readName(const ws_ble_result_t &result, char *name, size_t nameSize) {
  name[0] = '\0';
  const uint8_t dataLength = result.data_len <= sizeof(result.data) ? result.data_len : sizeof(result.data);
  uint8_t offset = 0;
  while (offset < dataLength) {
    const uint8_t length = result.data[offset++];
    if (!length) break;
    if (length > dataLength - offset) break;
    const uint8_t type = result.data[offset];
    if (type == 0x08 || type == 0x09) {
      size_t count = length - 1;
      if (count >= nameSize) count = nameSize - 1;
      memcpy(name, result.data + offset + 1, count);
      name[count] = '\0';
      return;
    }
    offset += length;
  }
}

static int findDevice(const ws_ble_result_t &result) {
  for (uint8_t i = 0; i < deviceCount; ++i) {
    if (devices[i].result.addr_type == result.addr_type &&
        !memcmp(devices[i].result.addr, result.addr, sizeof(result.addr))) return i;
  }
  return -1;
}

static void saveResult(const ws_ble_result_t &result) {
  int index = findDevice(result);
  const bool isNew = index < 0;
  if (index < 0) {
    if (deviceCount == sizeof(devices) / sizeof(devices[0])) return;
    index = deviceCount++;
  }
  if (!isNew && devices[index].result.rssi > result.rssi) return;
  devices[index].result = result;
  readName(result, devices[index].name, sizeof(devices[index].name));
}

static void printAddress(const uint8_t *address) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (i) Serial.print(':');
    if (address[i] < 16) Serial.print('0');
    Serial.print(address[i], HEX);
  }
}

static void printSummary() {
  for (uint8_t i = 0; i < deviceCount; ++i) {
    for (uint8_t j = i + 1; j < deviceCount; ++j) {
      if (devices[j].result.rssi > devices[i].result.rssi) {
        DeviceRecord temp = devices[i]; devices[i] = devices[j]; devices[j] = temp;
      }
    }
  }
  Serial.printf("Received %lu advertisements, %u unique devices, %lu queue drops\n",
                receivedCount, deviceCount, queueDropCount);
  const uint8_t shown = deviceCount < 10 ? deviceCount : 10;
  for (uint8_t i = 0; i < shown; ++i) {
    Serial.printf("%2u: ", i + 1); printAddress(devices[i].result.addr);
    Serial.printf("  %d dBm  %s\n", devices[i].result.rssi,
                  devices[i].name[0] ? devices[i].name : "<unnamed>");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  resultQueue = xQueueCreate(32, sizeof(ws_ble_result_t));
  if (!resultQueue || ws_bt_begin() != 0) {
    Serial.println("Bluetooth initialization failed");
    return;
  }
  ws_ble_set_result_callback(resultCallback);
  if (ws_ble_start_scan(10) != 0) {
    Serial.println("BLE scan could not start");
    return;
  }
  windowStart = millis();
  Serial.println("Scanning for BLE advertisements for 10 seconds...");
}

void loop() {
  ws_ble_result_t result;
  while (resultQueue && xQueueReceive(resultQueue, &result, 0) == pdPASS) saveResult(result);
  if (!reported && windowStart && millis() - windowStart >= 10000) {
    printSummary();
    reported = true;
  }
}

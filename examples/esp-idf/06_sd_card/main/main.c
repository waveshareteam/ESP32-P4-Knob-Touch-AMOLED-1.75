#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "lvgl.h"
#include "sdmmc_cmd.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "sdkconfig.h"

#define MOUNT_POINT BSP_SD_MOUNT_POINT

static const char *TAG = "06_sd_card";

typedef struct {
    char status[24];
    char card_type[32];
    char capacity[40];
    char free_space[40];
    char bus[48];
    char verify[48];
    char error[72];
    char power[40];
} sd_ui_update_t;

typedef struct {
    lv_obj_t *status;
    lv_obj_t *card_type;
    lv_obj_t *capacity;
    lv_obj_t *free_space;
    lv_obj_t *bus;
    lv_obj_t *verify;
    lv_obj_t *error;
    lv_obj_t *power;
} sd_ui_labels_t;

static QueueHandle_t s_ui_queue;
static lv_display_t *s_disp;

static void format_mib(uint64_t bytes, char *out, size_t out_size)
{
    snprintf(out, out_size, "%llu MiB", (unsigned long long)(bytes / (1024ULL * 1024ULL)));
}

static const char *card_type_name(const sdmmc_card_t *card)
{
    if (!card) {
        return "Unknown";
    }
    if (card->is_sdio) {
        return "SDIO";
    }
    if (card->is_mmc) {
        return "MMC/eMMC";
    }
    return "SD memory";
}

static void post_update(const sd_ui_update_t *update)
{
    if (s_ui_queue && update) {
        (void)xQueueOverwrite(s_ui_queue, update);
    }
}

static void label_set(lv_obj_t *label, const char *caption, const char *value)
{
    lv_label_set_text_fmt(label, "%s: %s", caption, value && value[0] ? value : "-");
}

static lv_obj_t *new_line_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, BSP_LCD_H_RES - 32);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1F2933), 0);
    return label;
}

static void create_ui(sd_ui_labels_t *labels)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF7F8FA), 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_gap(scr, 7, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = new_line_label(scr);
    lv_label_set_text(title, "06 SD Card");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0B1720), 0);

    labels->status = new_line_label(scr);
    labels->card_type = new_line_label(scr);
    labels->capacity = new_line_label(scr);
    labels->free_space = new_line_label(scr);
    labels->bus = new_line_label(scr);
    labels->verify = new_line_label(scr);
    labels->power = new_line_label(scr);
    labels->error = new_line_label(scr);
    lv_obj_set_style_text_color(labels->error, lv_color_hex(0x9A3412), 0);
}

static void apply_ui_update(const sd_ui_labels_t *labels, const sd_ui_update_t *update)
{
    label_set(labels->status, "Status", update->status);
    label_set(labels->card_type, "Card", update->card_type);
    label_set(labels->capacity, "Total", update->capacity);
    label_set(labels->free_space, "Free", update->free_space);
    label_set(labels->bus, "Bus", update->bus);
    label_set(labels->verify, "Verify", update->verify);
    label_set(labels->power, "Power control", update->power);
    label_set(labels->error, "Last error", update->error);
}

static void ui_task(void *arg)
{
    (void)arg;
    sd_ui_labels_t labels = {0};

    s_disp = bsp_display_start();
    if (!s_disp) {
        ESP_LOGE(TAG, "LVGL init failed");
        vTaskDelete(NULL);
    }

    if (bsp_display_lock(-1)) {
        create_ui(&labels);
        sd_ui_update_t boot = {
            .status = "Starting",
            .card_type = "-",
            .capacity = "-",
            .free_space = "-",
            .bus = "4-bit primary",
            .verify = "-",
            .error = "-",
            .power = "external/unchanged",
        };
        apply_ui_update(&labels, &boot);
        bsp_display_unlock();
    }

    while (1) {
        sd_ui_update_t update;
        if (xQueueReceive(s_ui_queue, &update, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (bsp_display_lock(-1)) {
                apply_ui_update(&labels, &update);
                bsp_display_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t write_read_verify(const char *path)
{
    const char *payload = "ESP32-P4 06_sd_card write/read verification\n";
    char readback[96] = {0};
    FILE *file = fopen(path, "w");
    if (!file) {
        return ESP_FAIL;
    }
    if (fputs(payload, file) == EOF) {
        fclose(file);
        return ESP_FAIL;
    }
    if (fclose(file) != 0) {
        return ESP_FAIL;
    }

    file = fopen(path, "r");
    if (!file) {
        return ESP_FAIL;
    }
    char *line = fgets(readback, sizeof(readback), file);
    if (fclose(file) != 0 || !line) {
        return ESP_FAIL;
    }
    return strcmp(payload, readback) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static void sd_task(void *arg)
{
    (void)arg;
    sd_ui_update_t ui = {
        .status = "Mounting",
        .card_type = "-",
        .capacity = "-",
        .free_space = "-",
        .bus = "4-bit 40000 kHz",
        .verify = "-",
        .error = "-",
        .power = "external/unchanged",
    };
    post_update(&ui);

    ESP_LOGI(TAG, "START SDMMC test D0..D3=GPIO39..42 CLK=GPIO43 CMD=GPIO44");
    ESP_LOGI(TAG, "Power control: external/unchanged; GPIO45 is not driven");
    ESP_LOGI(TAG, "primary mount width=%d freq=%d kHz", 4, SDMMC_FREQ_HIGHSPEED);

    sdmmc_card_t *card = NULL;
    int active_width = 4;
    int active_freq = SDMMC_FREQ_HIGHSPEED;
    snprintf(ui.bus, sizeof(ui.bus), "%d-bit %d kHz", active_width, active_freq);
    esp_err_t ret = bsp_sdcard_mount();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL mount: %s", esp_err_to_name(ret));
        snprintf(ui.status, sizeof(ui.status), "Failed");
        snprintf(ui.error, sizeof(ui.error), "mount: %s", esp_err_to_name(ret));
        post_update(&ui);
        vTaskDelete(NULL);
    }

    card = bsp_sdcard;
    sdmmc_card_print_info(stdout, card);
    snprintf(ui.status, sizeof(ui.status), "Mounted");
    snprintf(ui.card_type, sizeof(ui.card_type), "%s", card_type_name(card));
    uint64_t card_bytes = (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    format_mib(card_bytes, ui.capacity, sizeof(ui.capacity));

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    ret = esp_vfs_fat_info(MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret == ESP_OK) {
        format_mib(free_bytes, ui.free_space, sizeof(ui.free_space));
    } else {
        snprintf(ui.free_space, sizeof(ui.free_space), "info failed");
        snprintf(ui.error, sizeof(ui.error), "fat info: %s", esp_err_to_name(ret));
    }
    snprintf(ui.bus, sizeof(ui.bus), "%d-bit req %d kHz real %d kHz",
             active_width, active_freq, card->real_freq_khz);
    snprintf(ui.verify, sizeof(ui.verify), "Running");
    post_update(&ui);

    char path[96] = {0};
    snprintf(path, sizeof(path), MOUNT_POINT "/sd06_%08lx_%08lx.txt",
             (unsigned long)esp_random(), (unsigned long)xTaskGetTickCount());
    ret = write_read_verify(path);
    bool verified = (ret == ESP_OK);
    if (verified) {
        ESP_LOGI(TAG, "write/read verified: %s", path);
        snprintf(ui.verify, sizeof(ui.verify), "Verified");
    } else {
        ESP_LOGE(TAG, "FAIL write/read verify: %s", esp_err_to_name(ret));
        snprintf(ui.verify, sizeof(ui.verify), "Failed");
        snprintf(ui.error, sizeof(ui.error), "verify: %s", esp_err_to_name(ret));
    }
    post_update(&ui);

    bool delete_ok = false;
    if (unlink(path) == 0) {
        delete_ok = true;
    } else {
        ESP_LOGW(TAG, "temporary file delete failed or file missing: %s", path);
        if (verified) {
            snprintf(ui.verify, sizeof(ui.verify), "Delete failed");
            snprintf(ui.error, sizeof(ui.error), "delete temp failed");
        }
    }

    ret = bsp_sdcard_unmount();
    if (ret == ESP_OK) {
        bool passed = verified && delete_ok;
        ESP_LOGI(TAG, "unmounted SD card");
        snprintf(ui.status, sizeof(ui.status), passed ? "Passed" : "Failed");
        if (passed) {
            ESP_LOGI(TAG, "PASS SD card mount/write/read/delete/unmount complete");
        }
    } else {
        ESP_LOGE(TAG, "FAIL unmount: %s", esp_err_to_name(ret));
        snprintf(ui.status, sizeof(ui.status), "Failed");
        snprintf(ui.error, sizeof(ui.error), "unmount: %s", esp_err_to_name(ret));
    }
    post_update(&ui);
    vTaskDelete(NULL);
}

void app_main(void)
{
    s_ui_queue = xQueueCreate(1, sizeof(sd_ui_update_t));
    if (!s_ui_queue) {
        ESP_LOGE(TAG, "FAIL create UI queue");
        return;
    }
    xTaskCreate(ui_task, "sd_ui", 8192, NULL, 4, NULL);
    xTaskCreate(sd_task, "sd_worker", 8192, NULL, 5, NULL);
}

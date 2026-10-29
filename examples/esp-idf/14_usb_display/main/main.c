/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "usb_descriptors.h"

#define SCREEN_WIDTH            BSP_LCD_H_RES
#define SCREEN_HEIGHT           BSP_LCD_V_RES
#define FRAME_LIMIT             (300 * 1024)
#define JPEG_MCU_SIZE           16
#define USB_RHPORT_HS           1
#define USB_TASK_STACK          4096
#define USB_TASK_PRIORITY       5
#if defined(CONFIG_CACHE_L2_CACHE_LINE_128B)
#define PPA_ALIGNMENT           128
#elif defined(CONFIG_CACHE_L2_CACHE_LINE_64B)
#define PPA_ALIGNMENT           64
#else
#define PPA_ALIGNMENT           64
#endif
#define CANVAS_BYTES            (SCREEN_WIDTH * SCREEN_HEIGHT * 2)
#define CANVAS_BUFFER_SIZE      ((CANVAS_BYTES + PPA_ALIGNMENT - 1) & ~(PPA_ALIGNMENT - 1))

typedef struct __attribute__((packed)) {
    uint16_t crc16;
    uint8_t type;
    uint8_t cmd;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t frame_id : 10;
    uint32_t payload_total : 22;
} udisp_frame_header_t;

typedef struct __attribute__((packed)) {
    uint8_t tip;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} touch_finger_t;

typedef struct __attribute__((packed)) {
    touch_finger_t fingers[USB_DISPLAY_TOUCH_POINTS];
    uint8_t count;
} touch_report_t;

static const char *TAG = "14_usb_display";
static SemaphoreHandle_t s_rx_lock;
static SemaphoreHandle_t s_frame_ready;
static SemaphoreHandle_t s_frame_available;
static SemaphoreHandle_t s_touch_lock;
static uint8_t *s_frame;
static uint8_t *s_jpeg_output;
static uint8_t *s_canvas_buffer;
static size_t s_frame_len;
static uint32_t s_payload_total;
static uint16_t s_frame_x;
static uint16_t s_frame_y;
static uint16_t s_frame_width;
static uint16_t s_frame_height;
static uint32_t s_drop_remaining;
static bool s_receiving;
/* A single JPEG buffer is deliberately kept busy until the decoder returns it. New frames are
 * dropped while rendering instead of racing the decoder and corrupting an in-flight picture. */
static bool s_frame_busy;
static uint32_t s_usb_session;
static jpeg_decoder_handle_t s_jpeg;
static ppa_client_handle_t s_ppa;
static lv_obj_t *s_canvas;
static touch_finger_t s_last_finger;
static bool s_touch_down;
static touch_report_t s_pending_touch;
static bool s_touch_pending;
static uint32_t s_touch_generation;

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/* The P4 JPEG engine requires dimensions aligned to the JPEG sampling MCU. The desktop sender
 * supplies the visible rectangle; only its SOF dimensions are enlarged before decoding, then PPA crops it back. */
static bool align_jpeg_size(uint8_t *data, size_t len, uint16_t width, uint16_t height)
{
    size_t pos = 2;
    while (pos + 9 <= len && data[pos] == 0xFF) {
        const uint8_t marker = data[pos + 1];
        if (marker == 0xFF) {
            pos++;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            return false;
        }
        const size_t segment_len = ((size_t)data[pos + 2] << 8) | data[pos + 3];
        if (segment_len < 2 || pos + 2 + segment_len > len) {
            return false;
        }
        if (marker >= 0xC0 && marker <= 0xC2) {
            data[pos + 5] = height >> 8;
            data[pos + 6] = height & 0xFF;
            data[pos + 7] = width >> 8;
            data[pos + 8] = width & 0xFF;
            return true;
        }
        pos += 2 + segment_len;
    }
    return false;
}

static bool frame_header_valid(const udisp_frame_header_t *header)
{
    if (header->type != USB_DISPLAY_FORMAT_JPEG || header->width == 0 || header->height == 0 ||
            header->x >= SCREEN_WIDTH || header->y >= SCREEN_HEIGHT ||
            header->width > SCREEN_WIDTH - header->x || header->height > SCREEN_HEIGHT - header->y ||
            header->payload_total == 0 || header->payload_total > FRAME_LIMIT) {
        ESP_LOGW(TAG, "Drop unsupported frame type=%u rect=%ux%u+%u+%u len=%" PRIu32,
                 header->type, header->width, header->height, header->x, header->y, header->payload_total);
        return false;
    }
    return true;
}

/* TinyUSB calls this from its device task. A frame starts at a packet boundary and is followed
 * by raw JPEG bytes. The Windows driver is configured by the vendor string in usb_descriptors.c. */
#if CFG_TUD_API_V0_19_COMPAT
void tud_vendor_rx_cb(uint8_t instance, const uint8_t *buffer, uint16_t size)
#else
void tud_vendor_rx_cb(uint8_t instance, const uint8_t *buffer, uint32_t size)
#endif
{
    (void)instance;
    if (!buffer || size == 0 || !s_rx_lock) {
        return;
    }

    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if (!s_receiving && s_drop_remaining == 0) {
        if (size < sizeof(udisp_frame_header_t)) {
            xSemaphoreGive(s_rx_lock);
            return;
        }
        const udisp_frame_header_t header = *(const udisp_frame_header_t *)buffer;
        if (!frame_header_valid(&header)) {
            s_drop_remaining = header.payload_total;
        } else {
            /* A rectangle may depend on every earlier rectangle. Apply USB bulk backpressure
             * until the renderer returns the one buffer, preserving the host's frame order. */
            const uint32_t session = s_usb_session;
            xSemaphoreGive(s_rx_lock);
            xSemaphoreTake(s_frame_available, portMAX_DELAY);
            xSemaphoreTake(s_rx_lock, portMAX_DELAY);
            if (session != s_usb_session) {
                xSemaphoreGive(s_frame_available);
                s_drop_remaining = header.payload_total;
            } else {
                s_frame_len = 0;
                s_payload_total = header.payload_total;
                s_frame_x = header.x;
                s_frame_y = header.y;
                s_frame_width = header.width;
                s_frame_height = header.height;
                s_receiving = true;
            }
        }
        buffer += sizeof(header);
        size -= sizeof(header);
    }

    if (s_drop_remaining) {
        const size_t consumed = size < s_drop_remaining ? size : s_drop_remaining;
        s_drop_remaining -= consumed;
    } else if (s_receiving) {
        const size_t remaining = s_payload_total - s_frame_len;
        const size_t copied = size < remaining ? size : remaining;
        memcpy(s_frame + s_frame_len, buffer, copied);
        s_frame_len += copied;
        if (s_frame_len == s_payload_total) {
            s_receiving = false;
            s_frame_busy = true;
            xSemaphoreGive(s_frame_ready);
        }
    }
    xSemaphoreGive(s_rx_lock);
}

static void queue_touch_report(bool down, uint16_t x, uint16_t y)
{
    touch_report_t report = {0};
    touch_finger_t finger = {
        .tip = down ? 1 : 0,
        .id = 0,
        .x = x < SCREEN_WIDTH ? x : SCREEN_WIDTH - 1,
        .y = y < SCREEN_HEIGHT ? y : SCREEN_HEIGHT - 1,
        .width = 8,
        .height = 8,
    };
    report.fingers[0] = finger;
    report.count = 1;
    xSemaphoreTake(s_touch_lock, portMAX_DELAY);
    s_pending_touch = report;
    s_touch_pending = true;
    s_touch_generation++;
    xSemaphoreGive(s_touch_lock);
}

static void touch_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED) {
        if (s_touch_down) {
            queue_touch_report(false, s_last_finger.x, s_last_finger.y);
            s_touch_down = false;
        }
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }
    lv_indev_t *input = lv_indev_active();
    lv_point_t point;
    if (input) {
        lv_indev_get_point(input, &point);
        s_last_finger.x = point.x;
        s_last_finger.y = point.y;
        s_touch_down = true;
        queue_touch_report(true, point.x, point.y);
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t request_len)
{
    (void)instance;
    (void)report_type;
    if (report_id == USB_DISPLAY_REPORT_ID_MAX_COUNT && request_len >= 1) {
        buffer[0] = USB_DISPLAY_TOUCH_POINTS;
        return 1;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, const uint8_t *buffer,
                           uint16_t buffer_size)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)buffer_size;
}

static esp_err_t render_frame(void)
{
    jpeg_decode_picture_info_t info = {0};
    ESP_RETURN_ON_ERROR(jpeg_decoder_get_info(s_frame, s_frame_len, &info), TAG, "Read JPEG header failed");
    ESP_RETURN_ON_FALSE(info.width == s_frame_width && info.height == s_frame_height, ESP_ERR_INVALID_SIZE, TAG,
                        "Unexpected JPEG size %ux%u", info.width, info.height);

    uint16_t mcu_width;
    uint16_t mcu_height;
    switch (info.sample_method) {
    case JPEG_DOWN_SAMPLING_YUV444:
        mcu_width = 8;
        mcu_height = 8;
        break;
    case JPEG_DOWN_SAMPLING_YUV422:
        mcu_width = 16;
        mcu_height = 8;
        break;
    case JPEG_DOWN_SAMPLING_YUV420:
        mcu_width = 16;
        mcu_height = 16;
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint16_t decoded_width = align_up(info.width, mcu_width);
    const uint16_t decoded_height = align_up(info.height, mcu_height);
    ESP_RETURN_ON_FALSE(align_jpeg_size(s_frame, s_frame_len, decoded_width, decoded_height), ESP_ERR_INVALID_RESPONSE,
                        TAG, "JPEG has no supported frame header");

    const jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t output_size = 0;
    ESP_RETURN_ON_ERROR(jpeg_decoder_process(s_jpeg, &decode_cfg, s_frame, s_frame_len, s_jpeg_output,
                                             decoded_width * decoded_height * 2, &output_size),
                        TAG, "Decode JPEG failed");

    const ppa_srm_oper_config_t copy = {
        .in = {
            .buffer = s_jpeg_output,
            .pic_w = decoded_width,
            .pic_h = decoded_height,
            .block_w = s_frame_width,
            .block_h = s_frame_height,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_canvas_buffer,
            .buffer_size = CANVAS_BUFFER_SIZE,
            .pic_w = SCREEN_WIDTH,
            .pic_h = SCREEN_HEIGHT,
            .block_offset_x = s_frame_x,
            .block_offset_y = s_frame_y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ESP_RETURN_ON_FALSE(bsp_display_lock(-1), ESP_ERR_TIMEOUT, TAG, "Display lock failed");
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa, &copy);
    if (ret == ESP_OK) {
        ret = esp_cache_msync(s_canvas_buffer, CANVAS_BUFFER_SIZE,
                              ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    if (ret == ESP_OK) {
        lv_obj_invalidate(s_canvas);
    }
    bsp_display_unlock();
    ESP_RETURN_ON_ERROR(ret, TAG, "Render JPEG frame failed");
    return ret;
}

static void frame_task(void *arg)
{
    (void)arg;
    while (true) {
        if (xSemaphoreTake(s_frame_ready, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = render_frame();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Frame dropped: %s", esp_err_to_name(ret));
            }
            xSemaphoreTake(s_rx_lock, portMAX_DELAY);
            s_frame_busy = false;
            xSemaphoreGive(s_rx_lock);
            xSemaphoreGive(s_frame_available);
        }
    }
}

static void usb_device_task(void *arg)
{
    (void)arg;
    while (true) {
        tud_task_ext(1, false);
        if (tud_mounted() && tud_hid_ready() && xSemaphoreTake(s_touch_lock, 0) == pdTRUE) {
            const touch_report_t report = s_pending_touch;
            const uint32_t generation = s_touch_generation;
            const bool pending = s_touch_pending;
            xSemaphoreGive(s_touch_lock);
            if (pending && tud_hid_report(USB_DISPLAY_REPORT_ID_TOUCH, &report, sizeof(report))) {
                xSemaphoreTake(s_touch_lock, portMAX_DELAY);
                if (s_touch_generation == generation) {
                    s_touch_pending = false;
                }
                xSemaphoreGive(s_touch_lock);
            }
        }
        vTaskDelay(1);
    }
}

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB host configured the display device");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB host disconnected");
    s_touch_down = false;
    /* The last packet can end in the middle of a JPEG. Forget that receive state so a new
     * host session always starts at a protocol header; keep a completed frame busy until its
     * rendering task returns it. */
    if (s_rx_lock) {
        xSemaphoreTake(s_rx_lock, portMAX_DELAY);
        const bool abandon_receiving = s_receiving;
        s_receiving = false;
        s_drop_remaining = 0;
        s_usb_session++;
        if (!s_frame_busy) {
            s_frame_len = 0;
            s_payload_total = 0;
            if (abandon_receiving) {
                xSemaphoreGive(s_frame_available);
            }
        }
        xSemaphoreGive(s_rx_lock);
    }
    if (s_touch_lock) {
        xSemaphoreTake(s_touch_lock, portMAX_DELAY);
        s_touch_pending = false;
        s_touch_generation++;
        xSemaphoreGive(s_touch_lock);
    }
}

static esp_err_t start_usb_device(void)
{
    usb_phy_handle_t phy = NULL;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_HIGH,
    };
    ESP_RETURN_ON_ERROR(usb_new_phy(&phy_config, &phy), TAG, "USB PHY init failed");
    const tusb_rhport_init_t init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_HIGH,
    };
    ESP_RETURN_ON_FALSE(tusb_init(USB_RHPORT_HS, &init), ESP_FAIL, TAG, "TinyUSB init failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(usb_device_task, "usb_device", USB_TASK_STACK, NULL, USB_TASK_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Create USB task failed");
    return ESP_OK;
}

void app_main(void)
{
    s_rx_lock = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    s_frame_available = xSemaphoreCreateBinary();
    s_touch_lock = xSemaphoreCreateMutex();
    s_frame = heap_caps_aligned_alloc(PPA_ALIGNMENT, FRAME_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_canvas_buffer = heap_caps_aligned_alloc(PPA_ALIGNMENT, CANVAS_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpeg_output = heap_caps_aligned_alloc(PPA_ALIGNMENT, align_up(SCREEN_WIDTH, JPEG_MCU_SIZE) * align_up(SCREEN_HEIGHT, JPEG_MCU_SIZE) * 2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK((s_rx_lock && s_frame_ready && s_frame_available && s_touch_lock && s_frame && s_canvas_buffer && s_jpeg_output) ? ESP_OK : ESP_ERR_NO_MEM);
    xSemaphoreGive(s_frame_available);
    memset(s_canvas_buffer, 0, CANVAS_BUFFER_SIZE);

    const jpeg_decode_engine_cfg_t jpeg_cfg = {.intr_priority = 0, .timeout_ms = 100};
    const ppa_client_config_t ppa_cfg = {.oper_type = PPA_OPERATION_SRM};
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&jpeg_cfg, &s_jpeg));
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &s_ppa));

    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Display lock failed");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    s_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buffer, SCREEN_WIDTH, SCREEN_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(s_canvas);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_canvas, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_canvas, touch_event_cb, LV_EVENT_RELEASED, NULL);
    bsp_display_unlock();

    ESP_ERROR_CHECK(xTaskCreate(frame_task, "usb_frame", 6144, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(start_usb_device());
    ESP_LOGI(TAG, "Ready for a Windows USB display host at %dx%d JPEG", SCREEN_WIDTH, SCREEN_HEIGHT);
}

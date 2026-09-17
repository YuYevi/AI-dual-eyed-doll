#include "dual_eye_display.h"

#include "config.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <miniz.h>

#define TAG "DualEyeDisplay"

#define EYE_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)
#define EYE_BYTES (EYE_PIXELS * sizeof(uint16_t))

extern const uint8_t cute_rgb565_z_start[] asm("_binary_cute_rgb565_z_start");
extern const uint8_t cute_rgb565_z_end[] asm("_binary_cute_rgb565_z_end");
extern const uint8_t happy_rgb565_z_start[] asm("_binary_happy_rgb565_z_start");
extern const uint8_t happy_rgb565_z_end[] asm("_binary_happy_rgb565_z_end");
extern const uint8_t surprised_rgb565_z_start[] asm("_binary_surprised_rgb565_z_start");
extern const uint8_t surprised_rgb565_z_end[] asm("_binary_surprised_rgb565_z_end");
extern const uint8_t angry_rgb565_z_start[] asm("_binary_angry_rgb565_z_start");
extern const uint8_t angry_rgb565_z_end[] asm("_binary_angry_rgb565_z_end");
extern const uint8_t sad_rgb565_z_start[] asm("_binary_sad_rgb565_z_start");
extern const uint8_t sad_rgb565_z_end[] asm("_binary_sad_rgb565_z_end");
extern const uint8_t sleepy_rgb565_z_start[] asm("_binary_sleepy_rgb565_z_start");
extern const uint8_t sleepy_rgb565_z_end[] asm("_binary_sleepy_rgb565_z_end");
extern const uint8_t shy_rgb565_z_start[] asm("_binary_shy_rgb565_z_start");
extern const uint8_t shy_rgb565_z_end[] asm("_binary_shy_rgb565_z_end");
extern const uint8_t shocked_rgb565_z_start[] asm("_binary_shocked_rgb565_z_start");
extern const uint8_t shocked_rgb565_z_end[] asm("_binary_shocked_rgb565_z_end");

struct EyeAsset {
    const char* name;
    const uint8_t* start;
    const uint8_t* end;
};

static const EyeAsset kEyeAssets[] = {
    {"cute", cute_rgb565_z_start, cute_rgb565_z_end},
    {"happy", happy_rgb565_z_start, happy_rgb565_z_end},
    {"surprised", surprised_rgb565_z_start, surprised_rgb565_z_end},
    {"angry", angry_rgb565_z_start, angry_rgb565_z_end},
    {"sad", sad_rgb565_z_start, sad_rgb565_z_end},
    {"sleepy", sleepy_rgb565_z_start, sleepy_rgb565_z_end},
    {"shy", shy_rgb565_z_start, shy_rgb565_z_end},
    {"shocked", shocked_rgb565_z_start, shocked_rgb565_z_end},
};

static bool DecompressEyeAsset(const EyeAsset& asset, uint16_t* dest) {
    // tinfl_decompress_mem_to_mem() keeps tinfl_decompressor on the caller's stack (~4KB+).
    // Board init runs on the 8KB main task, so that overflows and reboots with StoreProhibited.
    auto* decomp = static_cast<tinfl_decompressor*>(
        heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decomp == nullptr) {
        decomp = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
    }
    if (decomp == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate zlib decompressor");
        return false;
    }

    tinfl_init(decomp);
    size_t src_len = static_cast<size_t>(asset.end - asset.start);
    size_t dest_len = EYE_BYTES;
    tinfl_status status = tinfl_decompress(
        decomp, asset.start, &src_len, reinterpret_cast<mz_uint8*>(dest),
        reinterpret_cast<mz_uint8*>(dest), &dest_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(decomp);

    if (status != TINFL_STATUS_DONE || dest_len != EYE_BYTES) {
        ESP_LOGE(TAG, "Decompress %s failed: status=%d len=%u", asset.name, static_cast<int>(status),
                 static_cast<unsigned>(dest_len));
        return false;
    }

    // Packed RGB565 assets are little-endian. GC9D01 expects the high byte first
    // on SPI, so swap each pixel before handing the buffer to esp_lcd.
    auto* bytes = reinterpret_cast<uint8_t*>(dest);
    for (size_t i = 0; i < EYE_BYTES; i += sizeof(uint16_t)) {
        const uint8_t high = bytes[i];
        bytes[i] = bytes[i + 1];
        bytes[i + 1] = high;
    }
    return true;
}

DualEyeDisplay::DualEyeDisplay(esp_lcd_panel_io_handle_t left_io, esp_lcd_panel_handle_t left_panel,
                               esp_lcd_panel_io_handle_t right_io, esp_lcd_panel_handle_t right_panel)
    : left_panel_(left_panel), right_panel_(right_panel) {
    width_ = DISPLAY_WIDTH;
    height_ = DISPLAY_HEIGHT;
    mutex_ = xSemaphoreCreateMutex();
    flush_done_ = xSemaphoreCreateBinary();
    frame_ = static_cast<uint16_t*>(heap_caps_malloc(EYE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (frame_ == nullptr) {
        frame_ = static_cast<uint16_t*>(heap_caps_malloc(EYE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
    }
    if (frame_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate eye frame buffer");
        return;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushDone,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(left_io, &cbs, this));
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(right_io, &cbs, this));

    DrawEmotion("cute");
}

DualEyeDisplay::~DualEyeDisplay() {
    if (frame_ != nullptr) {
        heap_caps_free(frame_);
        frame_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
    if (flush_done_ != nullptr) {
        vSemaphoreDelete(flush_done_);
    }
}

bool DualEyeDisplay::Lock(int timeout_ms) {
    if (mutex_ == nullptr) {
        return true;
    }
    const TickType_t timeout_ticks = timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
    return xSemaphoreTake(mutex_, timeout_ticks) == pdTRUE;
}

void DualEyeDisplay::Unlock() {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

bool DualEyeDisplay::OnFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* self = static_cast<DualEyeDisplay*>(user_ctx);
    BaseType_t hp_task = pdFALSE;
    xSemaphoreGiveFromISR(self->flush_done_, &hp_task);
    return hp_task == pdTRUE;
}

bool DualEyeDisplay::WaitFlush() {
    return xSemaphoreTake(flush_done_, pdMS_TO_TICKS(200)) == pdTRUE;
}

const char* DualEyeDisplay::MapEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') {
        return "cute";
    }
    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0 ||
        strcmp(emotion, "funny") == 0 || strcmp(emotion, "confident") == 0 ||
        strcmp(emotion, "relaxed") == 0 || strcmp(emotion, "delicious") == 0 ||
        strcmp(emotion, "cool") == 0 || strcmp(emotion, "silly") == 0) {
        return "happy";
    }
    if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "crying") == 0) {
        return "sad";
    }
    if (strcmp(emotion, "angry") == 0) {
        return "angry";
    }
    if (strcmp(emotion, "surprised") == 0) {
        return "surprised";
    }
    if (strcmp(emotion, "shocked") == 0 || strcmp(emotion, "thinking") == 0 ||
        strcmp(emotion, "confused") == 0) {
        return "shocked";
    }
    if (strcmp(emotion, "sleepy") == 0) {
        return "sleepy";
    }
    if (strcmp(emotion, "shy") == 0 || strcmp(emotion, "embarrassed") == 0) {
        return "shy";
    }
    if (strcmp(emotion, "cute") == 0 || strcmp(emotion, "loving") == 0 ||
        strcmp(emotion, "kissy") == 0 || strcmp(emotion, "winking") == 0 ||
        strcmp(emotion, "neutral") == 0 || strcmp(emotion, "robot_2") == 0) {
        return "cute";
    }
    return "cute";
}

bool DualEyeDisplay::DrawEmotion(const char* emotion) {
    if (frame_ == nullptr || left_panel_ == nullptr || right_panel_ == nullptr) {
        return false;
    }

    const char* mapped = MapEmotion(emotion);
    const EyeAsset* asset = nullptr;
    for (const auto& item : kEyeAssets) {
        if (strcmp(item.name, mapped) == 0) {
            asset = &item;
            break;
        }
    }
    if (asset == nullptr) {
        return false;
    }

    if (!DecompressEyeAsset(*asset, frame_)) {
        return false;
    }

    xSemaphoreTake(flush_done_, 0);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(left_panel_, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, frame_));
    if (!WaitFlush()) {
        ESP_LOGW(TAG, "Left eye flush timeout");
    }
    xSemaphoreTake(flush_done_, 0);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(right_panel_, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, frame_));
    if (!WaitFlush()) {
        ESP_LOGW(TAG, "Right eye flush timeout");
    }

    current_emotion_ = mapped;
    ESP_LOGI(TAG, "Emotion %s -> %s", emotion, mapped);
    return true;
}

void DualEyeDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    const char* mapped = MapEmotion(emotion);
    if (current_emotion_ != nullptr && strcmp(current_emotion_, mapped) == 0) {
        return;
    }
    DrawEmotion(mapped);
}

void DualEyeDisplay::SetStatus(const char* status) { ESP_LOGD(TAG, "SetStatus: %s", status); }

void DualEyeDisplay::SetChatMessage(const char* role, const char* content) {
    ESP_LOGD(TAG, "Chat %s: %s", role, content);
}

void DualEyeDisplay::ShowNotification(const char* notification, int duration_ms) {
    ESP_LOGD(TAG, "Notify (%d ms): %s", duration_ms, notification);
}

void DualEyeDisplay::SetPowerSaveMode(bool on) {
    DisplayLockGuard lock(this);
    if (left_panel_ != nullptr) {
        esp_lcd_panel_disp_on_off(left_panel_, !on);
    }
    if (right_panel_ != nullptr) {
        esp_lcd_panel_disp_on_off(right_panel_, !on);
    }
}

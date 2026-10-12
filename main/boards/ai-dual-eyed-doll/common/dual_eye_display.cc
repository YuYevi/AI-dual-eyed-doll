#include "dual_eye_display.h"

#include "assets.h"
#include "config.h"

#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <miniz.h>

#define TAG "DualEyeDisplay"

#define EYE_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)
#define EYE_BYTES (EYE_PIXELS * sizeof(uint16_t))
#define ANIMATION_MAGIC "DEYEANIM"
#define ANIMATION_VERSION 1
#define ANIMATION_MAX_COUNT 8
#define ANIMATION_FRAME_INTERVAL_MS 40

// This is the on-flash table format written by scripts/pack_dual_eye_assets.py.
#pragma pack(push, 1)
struct AnimationFileHeader {
    char magic[8];
    uint16_t version;
    uint16_t count;
    uint32_t table_offset;
    uint32_t data_offset;
};

struct AnimationFileEntry {
    char name[12];
    uint16_t frame_count;
    uint16_t fps;
    uint32_t left_offset;
    uint32_t left_size;
    uint32_t right_offset;
    uint32_t right_size;
};
#pragma pack(pop)

static_assert(sizeof(AnimationFileHeader) == 20, "Unexpected animation header layout");
static_assert(sizeof(AnimationFileEntry) == 32, "Unexpected animation table layout");

static uint32_t ReadU32(const uint8_t* data) {
    uint32_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

static void* AllocateBuffer(size_t size, uint32_t caps) {
    void* buffer = heap_caps_malloc(size, caps | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        buffer = malloc(size);
    }
    return buffer;
}

DualEyeDisplay::DualEyeDisplay(esp_lcd_panel_io_handle_t left_io,
                               esp_lcd_panel_handle_t left_panel,
                               esp_lcd_panel_io_handle_t right_io,
                               esp_lcd_panel_handle_t right_panel)
    : left_panel_(left_panel), right_panel_(right_panel) {
    width_ = DISPLAY_WIDTH;
    height_ = DISPLAY_HEIGHT;
    mutex_ = xSemaphoreCreateMutex();
    flush_done_ = xSemaphoreCreateBinary();
    frame_ = static_cast<uint16_t*>(
        heap_caps_malloc(EYE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (frame_ == nullptr) {
        frame_ = static_cast<uint16_t*>(
            heap_caps_malloc(EYE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
    }
    left_state_ = static_cast<uint8_t*>(AllocateBuffer(EYE_BYTES, MALLOC_CAP_SPIRAM));
    right_state_ = static_cast<uint8_t*>(AllocateBuffer(EYE_BYTES, MALLOC_CAP_SPIRAM));
    decode_buffer_ = static_cast<uint8_t*>(AllocateBuffer(EYE_BYTES, MALLOC_CAP_SPIRAM));
    decompressor_ = AllocateBuffer(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM);
    if (frame_ == nullptr || left_state_ == nullptr || right_state_ == nullptr ||
        decode_buffer_ == nullptr || decompressor_ == nullptr || mutex_ == nullptr ||
        flush_done_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate animation buffers");
        return;
    }
    memset(left_state_, 0, EYE_BYTES);
    memset(right_state_, 0, EYE_BYTES);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushDone,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(left_io, &cbs, this));
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(right_io, &cbs, this));

    // The controller's RAM is undefined at power-on. Clear both eyes even if
    // an old or missing assets partition prevents animations from loading.
    DrawFrame(left_state_, left_panel_);
    DrawFrame(right_state_, right_panel_);
    if (!LoadAnimations() || !SelectAnimation("calm")) {
        ESP_LOGE(TAG, "Failed to load dual-eye animations; flash the board's assets partition");
    }

    if (xTaskCreate(AnimationTaskEntry, "dual_eye_anim", 4096, this, 4, &animation_task_) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create animation task");
        animation_task_ = nullptr;
    }
}

DualEyeDisplay::~DualEyeDisplay() {
    if (animation_task_ != nullptr) {
        vTaskDelete(animation_task_);
        animation_task_ = nullptr;
    }
    // Do not release the DMA buffer while a queued SPI transfer still refers to it.
    if (flush_pending_) {
        (void)WaitFlush();
        flush_pending_ = false;
    }
    if (decompressor_ != nullptr) {
        heap_caps_free(decompressor_);
        decompressor_ = nullptr;
    }
    if (decode_buffer_ != nullptr) {
        heap_caps_free(decode_buffer_);
        decode_buffer_ = nullptr;
    }
    if (right_state_ != nullptr) {
        heap_caps_free(right_state_);
        right_state_ = nullptr;
    }
    if (left_state_ != nullptr) {
        heap_caps_free(left_state_);
        left_state_ = nullptr;
    }
    if (frame_ != nullptr) {
        heap_caps_free(frame_);
        frame_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (flush_done_ != nullptr) {
        vSemaphoreDelete(flush_done_);
        flush_done_ = nullptr;
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

bool DualEyeDisplay::OnFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*,
                                 void* user_ctx) {
    auto* self = static_cast<DualEyeDisplay*>(user_ctx);
    if (self == nullptr || self->flush_done_ == nullptr) {
        return false;
    }
    BaseType_t hp_task = pdFALSE;
    xSemaphoreGiveFromISR(self->flush_done_, &hp_task);
    return hp_task == pdTRUE;
}

bool DualEyeDisplay::WaitFlush() {
    return flush_done_ != nullptr &&
           xSemaphoreTake(flush_done_, pdMS_TO_TICKS(200)) == pdTRUE;
}

bool DualEyeDisplay::LoadAnimations() {
    void* data = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData("dual_eye.anim", data, size)) {
        ESP_LOGE(TAG, "dual_eye.anim is missing from the assets partition");
        return false;
    }
    if (size < sizeof(AnimationFileHeader)) {
        ESP_LOGE(TAG, "dual_eye.anim is truncated");
        return false;
    }

    // Assets payloads follow a two-byte magic and are not necessarily word-aligned.
    AnimationFileHeader header;
    memcpy(&header, data, sizeof(header));
    if (memcmp(header.magic, ANIMATION_MAGIC, sizeof(header.magic)) != 0 ||
        header.version != ANIMATION_VERSION || header.count == 0 ||
        header.count > ANIMATION_MAX_COUNT || header.table_offset != sizeof(header)) {
        ESP_LOGE(TAG, "Invalid dual-eye animation header");
        return false;
    }
    const size_t table_end = header.table_offset +
                             static_cast<size_t>(header.count) * sizeof(AnimationFileEntry);
    if (table_end > size || header.data_offset < table_end || header.data_offset > size) {
        ESP_LOGE(TAG, "Invalid dual-eye animation table");
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    auto valid_stream = [bytes, size, &header](uint32_t offset, uint32_t length,
                                              uint16_t frame_count) {
        if (offset < header.data_offset || offset > size || length > size - offset) {
            return false;
        }
        const uint8_t* stream = bytes + offset;
        size_t cursor = 0;
        for (uint16_t frame = 0; frame < frame_count; ++frame) {
            if (length - cursor < sizeof(uint32_t)) {
                return false;
            }
            const size_t compressed_size = ReadU32(stream + cursor);
            cursor += sizeof(uint32_t);
            if (compressed_size == 0 || compressed_size > length - cursor) {
                return false;
            }
            cursor += compressed_size;
        }
        return cursor == length;
    };
    for (size_t i = 0; i < header.count; ++i) {
        AnimationFileEntry entry;
        memcpy(&entry, bytes + header.table_offset + i * sizeof(entry), sizeof(entry));
        if (entry.frame_count == 0 || entry.fps == 0 ||
            !valid_stream(entry.left_offset, entry.left_size, entry.frame_count) ||
            !valid_stream(entry.right_offset, entry.right_size, entry.frame_count)) {
            ESP_LOGE(TAG, "Invalid animation entry %u", static_cast<unsigned>(i));
            return false;
        }
        memcpy(animations_[i].name, entry.name, sizeof(entry.name));
        animations_[i].name[sizeof(entry.name)] = '\0';
        animations_[i].frame_count = entry.frame_count;
        animations_[i].fps = entry.fps;
        animations_[i].left_stream = bytes + entry.left_offset;
        animations_[i].left_size = entry.left_size;
        animations_[i].right_stream = bytes + entry.right_offset;
        animations_[i].right_size = entry.right_size;
    }
    animation_count_ = header.count;
    return true;
}

bool DualEyeDisplay::SelectAnimation(const char* emotion) {
    const char* mapped = MapEmotion(emotion);
    for (size_t i = 0; i < animation_count_; ++i) {
        if (strcmp(animations_[i].name, mapped) == 0) {
            animation_ = &animations_[i];
            animation_frame_ = 0;
            memset(left_state_, 0, EYE_BYTES);
            memset(right_state_, 0, EYE_BYTES);
            if (!RenderFrame(animation_frame_)) {
                animation_ = nullptr;
                return false;
            }
            animation_frame_ = 1;
            current_emotion_ = animation_->name;
            ESP_LOGI(TAG, "Emotion %s -> %s", emotion != nullptr ? emotion : "(null)", mapped);
            return true;
        }
    }
    return false;
}

bool DualEyeDisplay::DecompressFrame(const uint8_t* compressed, size_t compressed_size,
                                     uint8_t* dest) {
    auto* decomp = static_cast<tinfl_decompressor*>(decompressor_);
    if (decomp == nullptr || compressed == nullptr || dest == nullptr) {
        return false;
    }
    tinfl_init(decomp);
    size_t src_len = compressed_size;
    size_t dest_len = EYE_BYTES;
    const tinfl_status status = tinfl_decompress(
        decomp, compressed, &src_len, dest, dest, &dest_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return status == TINFL_STATUS_DONE && dest_len == EYE_BYTES;
}

bool DualEyeDisplay::RenderEyeFrame(const uint8_t* stream, size_t stream_size,
                                    uint16_t frame_index, uint8_t* state) {
    const uint8_t* cursor = stream;
    const uint8_t* end = stream + stream_size;
    for (uint16_t i = 0; i <= frame_index; ++i) {
        if (cursor + sizeof(uint32_t) > end) {
            return false;
        }
        const uint32_t compressed_size = ReadU32(cursor);
        cursor += sizeof(uint32_t);
        if (compressed_size > static_cast<size_t>(end - cursor)) {
            return false;
        }
        if (i == frame_index) {
            if (!DecompressFrame(cursor, compressed_size, decode_buffer_)) {
                ESP_LOGE(TAG, "Failed to decompress animation frame %u",
                         static_cast<unsigned>(frame_index));
                return false;
            }
            for (size_t byte = 0; byte < EYE_BYTES; ++byte) {
                state[byte] ^= decode_buffer_[byte];
            }
            return true;
        }
        cursor += compressed_size;
    }
    return false;
}

bool DualEyeDisplay::DrawFrame(const uint8_t* state, esp_lcd_panel_handle_t panel) {
    if (state == nullptr || panel == nullptr || frame_ == nullptr) {
        return false;
    }
    // A timed-out SPI transfer can still be using the shared DMA buffer.
    // Its completion must be observed before replacing any of the pixels.
    if (flush_pending_) {
        if (!WaitFlush()) {
            ESP_LOGW(TAG, "Previous eye flush is still pending");
            return false;
        }
        flush_pending_ = false;
    }
    // The animation file stores little-endian RGB565. The panel expects high byte first.
    for (size_t pixel = 0; pixel < EYE_PIXELS; ++pixel) {
        frame_[pixel] = static_cast<uint16_t>((state[pixel * 2] << 8) | state[pixel * 2 + 1]);
    }
    xSemaphoreTake(flush_done_, 0);
    const auto error =
        esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, frame_);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw eye frame: %s", esp_err_to_name(error));
        return false;
    }
    flush_pending_ = true;
    if (!WaitFlush()) {
        ESP_LOGW(TAG, "Eye flush timeout");
        return false;
    }
    flush_pending_ = false;
    return true;
}

bool DualEyeDisplay::RenderFrame(uint16_t frame_index) {
    if (animation_ == nullptr || frame_index >= animation_->frame_count) {
        return false;
    }
    if (!RenderEyeFrame(animation_->left_stream, animation_->left_size, frame_index, left_state_)) {
        return false;
    }
    if (animation_->right_stream == animation_->left_stream &&
        animation_->right_size == animation_->left_size) {
        memcpy(right_state_, left_state_, EYE_BYTES);
    } else if (!RenderEyeFrame(animation_->right_stream, animation_->right_size, frame_index,
                               right_state_)) {
        return false;
    }
    return DrawFrame(left_state_, left_panel_) && DrawFrame(right_state_, right_panel_);
}

void DualEyeDisplay::AnimationTaskEntry(void* arg) {
    static_cast<DualEyeDisplay*>(arg)->AnimationTask();
}

void DualEyeDisplay::AnimationTask() {
    TickType_t wake_time = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(ANIMATION_FRAME_INTERVAL_MS));
        if (!Lock(20)) {
            continue;
        }
        if (animation_ != nullptr) {
            if (animation_frame_ >= animation_->frame_count) {
                animation_frame_ = 0;
                memset(left_state_, 0, EYE_BYTES);
                memset(right_state_, 0, EYE_BYTES);
            }
            if (RenderFrame(animation_frame_)) {
                ++animation_frame_;
            } else {
                // The frame delta may already have been applied. Repeating it
                // would XOR the frame back and corrupt all following frames.
                ESP_LOGE(TAG, "Stopping animation after a failed frame");
                animation_ = nullptr;
                current_emotion_ = nullptr;
            }
        }
        Unlock();
    }
}

const char* DualEyeDisplay::MapEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') {
        return "calm";
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
    if (strcmp(emotion, "scared") == 0 || strcmp(emotion, "shocked") == 0 ||
        strcmp(emotion, "thinking") == 0 || strcmp(emotion, "confused") == 0) {
        return "scared";
    }
    if (strcmp(emotion, "sleepy") == 0) {
        return "sleepy";
    }
    if (strcmp(emotion, "shy") == 0 || strcmp(emotion, "embarrassed") == 0) {
        return "shy";
    }
    if (strcmp(emotion, "calm") == 0 || strcmp(emotion, "cute") == 0 ||
        strcmp(emotion, "loving") == 0 || strcmp(emotion, "kissy") == 0 ||
        strcmp(emotion, "winking") == 0 || strcmp(emotion, "neutral") == 0 ||
        strcmp(emotion, "robot_2") == 0) {
        return "calm";
    }
    return "calm";
}

void DualEyeDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (frame_ == nullptr || left_state_ == nullptr || right_state_ == nullptr ||
        decode_buffer_ == nullptr || decompressor_ == nullptr || flush_done_ == nullptr) {
        return;
    }
    // Assets may have been installed during activation after display startup.
    if (animation_count_ == 0 && !LoadAnimations()) {
        return;
    }
    const char* mapped = MapEmotion(emotion);
    if (animation_ != nullptr && current_emotion_ != nullptr &&
        strcmp(current_emotion_, mapped) == 0) {
        return;
    }
    SelectAnimation(mapped);
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

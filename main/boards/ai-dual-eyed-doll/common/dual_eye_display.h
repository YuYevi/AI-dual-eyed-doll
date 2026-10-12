#ifndef DUAL_EYE_DISPLAY_H
#define DUAL_EYE_DISPLAY_H

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>

struct DualEyeAnimation {
    char name[13] = {};
    uint16_t frame_count = 0;
    uint16_t fps = 0;
    const uint8_t* left_stream = nullptr;
    size_t left_size = 0;
    const uint8_t* right_stream = nullptr;
    size_t right_size = 0;
};

class DualEyeDisplay : public Display {
public:
    DualEyeDisplay(esp_lcd_panel_io_handle_t left_io, esp_lcd_panel_handle_t left_panel,
                   esp_lcd_panel_io_handle_t right_io, esp_lcd_panel_handle_t right_panel);
    ~DualEyeDisplay() override;

    void SetEmotion(const char* emotion) override;
    void ShowNextAnimation();
    bool IsReady() const { return ready_; }
    bool SetScreenEnabled(bool enabled);
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetPowerSaveMode(bool on) override;

private:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

    bool LoadAnimations();
    bool SelectAnimation(const char* emotion);
    bool RenderFrame(uint16_t frame_index);
    bool RenderEyeFrame(const uint8_t* stream, size_t stream_size, uint16_t frame_index,
                        uint8_t* state);
    bool DecompressFrame(const uint8_t* compressed, size_t compressed_size, uint8_t* dest);
    bool DrawFrame(const uint8_t* state, esp_lcd_panel_handle_t panel);
    void AnimationTask();
    static void AnimationTaskEntry(void* arg);
    static const char* MapEmotion(const char* emotion);
    static bool OnFlushDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx);
    bool WaitFlush();

    esp_lcd_panel_handle_t left_panel_ = nullptr;
    esp_lcd_panel_handle_t right_panel_ = nullptr;
    uint16_t* frame_ = nullptr;
    uint8_t* left_state_ = nullptr;
    uint8_t* right_state_ = nullptr;
    uint8_t* decode_buffer_ = nullptr;
    void* decompressor_ = nullptr;
    DualEyeAnimation animations_[8] = {};
    size_t animation_count_ = 0;
    const DualEyeAnimation* animation_ = nullptr;
    uint16_t animation_frame_ = 0;
    TaskHandle_t animation_task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t flush_done_ = nullptr;
    bool flush_pending_ = false;
    bool ready_ = false;
    bool screen_enabled_ = false;
    const char* current_emotion_ = nullptr;
};

#endif  // DUAL_EYE_DISPLAY_H

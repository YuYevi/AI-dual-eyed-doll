#ifndef DUAL_EYE_DISPLAY_H
#define DUAL_EYE_DISPLAY_H

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class DualEyeDisplay : public Display {
public:
    DualEyeDisplay(esp_lcd_panel_io_handle_t left_io, esp_lcd_panel_handle_t left_panel,
                   esp_lcd_panel_io_handle_t right_io, esp_lcd_panel_handle_t right_panel);
    ~DualEyeDisplay() override;

    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetPowerSaveMode(bool on) override;

private:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

    bool DrawEmotion(const char* emotion);
    static const char* MapEmotion(const char* emotion);
    static bool OnFlushDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata,
                            void* user_ctx);
    bool WaitFlush();

    esp_lcd_panel_handle_t left_panel_ = nullptr;
    esp_lcd_panel_handle_t right_panel_ = nullptr;
    uint16_t* frame_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t flush_done_ = nullptr;
    const char* current_emotion_ = nullptr;
};

#endif  // DUAL_EYE_DISPLAY_H

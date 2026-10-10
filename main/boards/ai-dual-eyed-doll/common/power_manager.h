#ifndef AI_DUAL_EYED_DOLL_POWER_MANAGER_H
#define AI_DUAL_EYED_DOLL_POWER_MANAGER_H

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Board-local ADC owner: battery and USB both have 47k/47k dividers.
class PowerManager {
public:
    PowerManager();
    ~PowerManager();
    bool GetStatus(int& level, bool& charging, bool& discharging);
private:
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    bool usb_present_ = false;
    int ReadMillivolts(adc_channel_t channel);
};
#endif

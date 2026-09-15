#ifndef AI_DUAL_EYED_DOLL_POWER_MANAGER_H
#define AI_DUAL_EYED_DOLL_POWER_MANAGER_H

#include "../config.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*
 * Power latch: PWR_CTRL (GPIO1) high keeps the board on through Q4.
 * KEY_ON (GPIO2) is pulled up 51k and goes low while the power button is held.
 * Deep-sleep wake is ext0 on KEY_ON = 0; a 3s hold is required to actually boot.
 */
class PowerManager {
public:
    static void ConfigureWakeAndPowerOff() {
        rtc_gpio_init(PWR_BUTTON_GPIO);
        rtc_gpio_set_direction(PWR_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(PWR_BUTTON_GPIO);
        rtc_gpio_pulldown_dis(PWR_BUTTON_GPIO);
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(PWR_BUTTON_GPIO, 0));

        rtc_gpio_init(PWR_CONTROL_PIN);
        rtc_gpio_set_direction(PWR_CONTROL_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_hold_dis(PWR_CONTROL_PIN);
        rtc_gpio_set_level(PWR_CONTROL_PIN, 0);
        esp_deep_sleep_start();
    }

    static bool QualifyPowerOn() {
        const auto reset_reason = esp_reset_reason();
        const auto wakeup_cause = esp_sleep_get_wakeup_cause();
        if (reset_reason != ESP_RST_POWERON && wakeup_cause != ESP_SLEEP_WAKEUP_EXT0) {
            return true;
        }

        ESP_LOGI("PowerManager", "Hold power button for %d ms to power on", POWER_BUTTON_HOLD_MS);
        const int64_t start_time = esp_timer_get_time();
        while (esp_timer_get_time() - start_time < POWER_BUTTON_HOLD_MS * 1000LL) {
            if (gpio_get_level(PWR_BUTTON_GPIO) != 0) {
                ESP_LOGI("PowerManager", "Power button released before startup qualification");
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
        }
        return gpio_get_level(PWR_BUTTON_GPIO) == 0;
    }

    static void HoldPower() {
        rtc_gpio_init(PWR_CONTROL_PIN);
        rtc_gpio_set_direction(PWR_CONTROL_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(PWR_CONTROL_PIN, 1);
    }
};

#endif  // AI_DUAL_EYED_DOLL_POWER_MANAGER_H

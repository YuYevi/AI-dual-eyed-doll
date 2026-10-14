#include "power_manager.h"
#include "config.h"

#include <algorithm>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

namespace {
constexpr char kTag[] = "DollBattery";
// 根据锂离子电池电压保守估算电量；负载和电芯化学体系会影响精度。
struct BatteryPoint { int millivolts; int percent; };
constexpr BatteryPoint kCurve[] = {
    {3200, 0}, {3400, 5}, {3500, 10}, {3600, 20}, {3700, 40},
    {3800, 60}, {3900, 75}, {4000, 85}, {4100, 95}, {4200, 100},
};
int EstimateLevel(int millivolts) {
    for (size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); ++i) {
        if (millivolts < kCurve[i].millivolts) {
            const auto& lo = kCurve[i - 1];
            const auto& hi = kCurve[i];
            return std::clamp(lo.percent + (millivolts - lo.millivolts) *
                              (hi.percent - lo.percent) / (hi.millivolts - lo.millivolts), 0, 100);
        }
    }
    return 100;
}
}

PowerManager::PowerManager() {
    mutex_ = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(mutex_ ? ESP_OK : ESP_ERR_NO_MEM);
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << FULL_STATUS_GPIO;
    cfg.mode = GPIO_MODE_INPUT;
    // FULL 为开漏输出，其 LED 上拉连接至 USB_5V。充电芯片未供电时，
    // 不要对该引脚施加上拉。USB 未连接时忽略其读数。
    ESP_ERROR_CHECK(gpio_config(&cfg));

    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = BATTERY_ADC_UNIT;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc_));
    adc_oneshot_chan_cfg_t channel = {};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, BATTERY_ADC_CHANNEL, &channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, USB_ADC_CHANNEL, &channel));
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = BATTERY_ADC_UNIT;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    const auto error = adc_cali_create_scheme_curve_fitting(&cal, &calibration_);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "ADC calibration unavailable: %s; battery status disabled", esp_err_to_name(error));
    }
#else
    ESP_LOGW(kTag, "ADC curve fitting unsupported; battery status disabled");
#endif
}

PowerManager::~PowerManager() {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (calibration_) adc_cali_delete_scheme_curve_fitting(calibration_);
#endif
    if (adc_) adc_oneshot_del_unit(adc_);
    if (mutex_) vSemaphoreDelete(mutex_);
}

int PowerManager::ReadMillivolts(adc_channel_t channel) {
    int total = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        int mv = 0;
        if (adc_oneshot_read(adc_, channel, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(calibration_, raw, &mv) != ESP_OK) return -1;
        total += mv;
    }
    return (total / 16) * 2; // 上臂 47k / 下臂 47k 分压电路。
}

bool PowerManager::GetStatus(int& level, bool& charging, bool& discharging) {
    if (!calibration_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const int battery_mv = ReadMillivolts(BATTERY_ADC_CHANNEL);
    const int usb_mv = ReadMillivolts(USB_ADC_CHANNEL);
    if (battery_mv < 0 || usb_mv < 0) {
        xSemaphoreGive(mutex_);
        return false;
    }
    // USB 检测的迟滞可避免拔出时的噪声和悬空 FULL 引脚导致充电状态误判。
    usb_present_ = usb_mv >= (usb_present_ ? 3800 : 4200);
    const bool full = usb_present_ && gpio_get_level(FULL_STATUS_GPIO) == FULL_STATUS_ACTIVE_LEVEL;
    charging = usb_present_ && !full;
    discharging = !usb_present_;
    level = EstimateLevel(battery_mv);
    xSemaphoreGive(mutex_);
    return true;
}

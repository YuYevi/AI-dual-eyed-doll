#include "dual_network_board.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <memory>
#include <atomic>
#include <driver/i2c_master.h>
#include <driver/rtc_io.h>
#include <driver/spi_common.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string_view>

#include "power_manager.h"
#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "board_sounds.h"
#include "button.h"
#include "doll_audio_codec.h"

#include "config.h"
#include "dual_eye_display.h"
#include "esp_lcd_gc9d01.h"

#define TAG "AiDualEyedDoll"

// GPIO43/44 供模组使用；将应用控制台的所有输入输出切换至 USB。
#if !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || !defined(CONFIG_ESP_CONSOLE_SECONDARY_NONE)
#error "AI dual-eyed doll requires USB Serial/JTAG primary console and no secondary console; GPIO43/44 are reserved for ML307C"
#endif

static const gc9d01_lcd_init_cmd_t kGc9d01InitCommands[] = {
    {0xFE, nullptr, 0, 0},
    {0xEF, nullptr, 0, 0},
    {0x80, (const uint8_t[]){0xFF}, 1, 0},
    {0x81, (const uint8_t[]){0xFF}, 1, 0},
    {0x82, (const uint8_t[]){0xFF}, 1, 0},
    {0x83, (const uint8_t[]){0xFF}, 1, 0},
    {0x84, (const uint8_t[]){0xFF}, 1, 0},
    {0x85, (const uint8_t[]){0xFF}, 1, 0},
    {0x86, (const uint8_t[]){0xFF}, 1, 0},
    {0x87, (const uint8_t[]){0xFF}, 1, 0},
    {0x88, (const uint8_t[]){0xFF}, 1, 0},
    {0x89, (const uint8_t[]){0xFF}, 1, 0},
    {0x8A, (const uint8_t[]){0xFF}, 1, 0},
    {0x8B, (const uint8_t[]){0xFF}, 1, 0},
    {0x8C, (const uint8_t[]){0xFF}, 1, 0},
    {0x8D, (const uint8_t[]){0xFF}, 1, 0},
    {0x8E, (const uint8_t[]){0xFF}, 1, 0},
    {0x8F, (const uint8_t[]){0xFF}, 1, 0},
    {0x3A, (const uint8_t[]){0x05}, 1, 0},
    {0xEC, (const uint8_t[]){0x01}, 1, 0},
    {0x74, (const uint8_t[]){0x02, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x98, (const uint8_t[]){0x3E}, 1, 0},
    {0x99, (const uint8_t[]){0x3E}, 1, 0},
    {0xB5, (const uint8_t[]){0x0D, 0x0D}, 2, 0},
    {0x60, (const uint8_t[]){0x38, 0x0F, 0x79, 0x67}, 4, 0},
    {0x61, (const uint8_t[]){0x38, 0x11, 0x79, 0x67}, 4, 0},
    {0x64, (const uint8_t[]){0x38, 0x17, 0x71, 0x5F, 0x79, 0x67}, 6, 0},
    {0x65, (const uint8_t[]){0x38, 0x13, 0x71, 0x5B, 0x79, 0x67}, 6, 0},
    {0x6A, (const uint8_t[]){0x00, 0x00}, 2, 0},
    {0x6C, (const uint8_t[]){0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50}, 7, 0},
    {0x6E, (const uint8_t[]){0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0x0F, 0x0F, 0x0D, 0x0D, 0x0B,
                             0x0B, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x0A, 0x0C, 0x0C,
                             0x0E, 0x0E, 0x10, 0x10, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04}, 32, 0},
    {0xBF, (const uint8_t[]){0x01}, 1, 0},
    {0xF9, (const uint8_t[]){0x40}, 1, 0},
    {0x9B, (const uint8_t[]){0x3B}, 1, 0},
    {0x93, (const uint8_t[]){0x33, 0x7F, 0x00}, 3, 0},
    {0x7E, (const uint8_t[]){0x30}, 1, 0},
    {0x70, (const uint8_t[]){0x0D, 0x02, 0x08, 0x0D, 0x02, 0x08}, 6, 0},
    {0x71, (const uint8_t[]){0x0D, 0x02, 0x08}, 3, 0},
    {0x91, (const uint8_t[]){0x0E, 0x09}, 2, 0},
    {0xC3, (const uint8_t[]){0x18}, 1, 0},
    {0xC4, (const uint8_t[]){0x18}, 1, 0},
    {0xC9, (const uint8_t[]){0x3C}, 1, 0},
    {0xF0, (const uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x38}, 6, 0},
    {0xF2, (const uint8_t[]){0x13, 0x15, 0x04, 0x05, 0x01, 0x34}, 6, 0},
    {0xF1, (const uint8_t[]){0x4B, 0xB8, 0x7B, 0x34, 0x35, 0xEF}, 6, 0},
    {0xF3, (const uint8_t[]){0x47, 0xB4, 0x72, 0x34, 0x35, 0xDA}, 6, 0},
    {0x36, (const uint8_t[]){0x00}, 1, 0},
    {0x11, nullptr, 0, 200},
    // 在双眼均写入完整首帧前，保持屏幕扫描关闭。
    {0x28, nullptr, 0, 0},
};

class AiDualEyedDollBoard : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    DualEyeDisplay* display_ = nullptr;
    PowerManager* battery_monitor_ = nullptr;
    std::unique_ptr<Button> key1_button_, key2_button_, pwr_button_, touch1_button_, touch2_button_;
    bool screen_on_ = false;
    bool power_button_released_ = false;
    bool power_press_accepted_ = false;
    std::atomic<bool> shutting_down_{false};
    AudioCodec* codec_ = nullptr;

    static void ConfigureOutput(gpio_num_t pin, int level) {
        // 先设置输出锁存值，再使能输出，避免产生有效电平脉冲。
        ESP_ERROR_CHECK(gpio_set_level(pin, level));
        ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
    }

    void InitializePowerOff() {
        gpio_deep_sleep_hold_dis();
        // 在解除 GPIO 输出电平保持前，先预设关闭电平。
        for (auto pin : {POWER_EN1_GPIO, POWER_EN2_GPIO, AUDIO_PA_ENABLE_GPIO,
                         DISPLAY_BACKLIGHT_PIN, RUN_LED_GPIO}) {
            gpio_set_level(pin, pin == POWER_EN1_GPIO ? 1 : 0);
            rtc_gpio_set_level(pin, pin == POWER_EN1_GPIO ? 1 : 0);
            gpio_hold_dis(pin);
            rtc_gpio_hold_dis(pin);
            rtc_gpio_deinit(pin);
        }
        ConfigureOutput(POWER_EN1_GPIO, 1);
        ConfigureOutput(POWER_EN2_GPIO, 0);
        ConfigureOutput(AUDIO_PA_ENABLE_GPIO, 0);
        ConfigureOutput(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OFF_LEVEL);
        ConfigureOutput(RUN_LED_GPIO, 0);
        rtc_gpio_hold_dis(PWR_BUTTON_GPIO);
        rtc_gpio_deinit(PWR_BUTTON_GPIO);
        gpio_config_t key = {};
        key.pin_bit_mask = 1ULL << PWR_BUTTON_GPIO;
        key.mode = GPIO_MODE_INPUT;
        // 按下时 Q3 输出高电平；松开时由外部 R53 提供低电平。
        key.pull_up_en = GPIO_PULLUP_DISABLE;
        key.pull_down_en = GPIO_PULLDOWN_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&key));
        // 在确认开机条件期间，将未供电外设的输入保持为低电平。
        for (auto pin : {DISPLAY_SPI_SCLK_GPIO, DISPLAY_SPI_MOSI_GPIO, DISPLAY_SPI_DC_GPIO,
                         DISPLAY_SPI_RST_GPIO, DISPLAY_1_SPI_CS_GPIO, DISPLAY_2_SPI_CS_GPIO,
                         AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                         AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, I2C_SDA_IO, I2C_SCL_IO}) {
            gpio_hold_dis(pin);
            if (rtc_gpio_is_valid_gpio(pin)) {
                rtc_gpio_hold_dis(pin);
                rtc_gpio_deinit(pin);
            }
            ESP_ERROR_CHECK(gpio_reset_pin(pin));
            ConfigureOutput(pin, 0);
        }
    }

    void ConfigureWakeAndPowerOff() {
        // ext0 为电平触发：启用有效电平唤醒前，先确认按键已稳定松开。
        int released_samples = 0;
        while (released_samples < 5) {
            released_samples = gpio_get_level(PWR_BUTTON_GPIO) != POWER_BUTTON_ACTIVE_LEVEL
                                   ? released_samples + 1 : 0;
            vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
        }
        ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
        ESP_ERROR_CHECK(rtc_gpio_init(PWR_BUTTON_GPIO));
        ESP_ERROR_CHECK(rtc_gpio_set_direction(PWR_BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY));
        // 禁用 RTC 上拉和下拉；松开时的电平由外部 R53 确定。
        ESP_ERROR_CHECK(rtc_gpio_pullup_dis(PWR_BUTTON_GPIO));
        ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(PWR_BUTTON_GPIO));
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(PWR_BUTTON_GPIO, POWER_BUTTON_ACTIVE_LEVEL));

        // 关闭任一外设电源前，先保持信号为低电平。
        // 电平保持可防止后台 SPI/I2S 活动改变引脚电平。
        // 保持调度器运行，直至 IDF 执行进入睡眠前的关闭流程。
        for (auto pin : {DISPLAY_SPI_SCLK_GPIO, DISPLAY_SPI_MOSI_GPIO, DISPLAY_SPI_DC_GPIO,
                         DISPLAY_SPI_RST_GPIO, DISPLAY_1_SPI_CS_GPIO, DISPLAY_2_SPI_CS_GPIO,
                         AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                         AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN}) {
            gpio_reset_pin(pin); // 断开 SPI/I2S 外设的输出矩阵连接。
            gpio_set_pull_mode(pin, GPIO_FLOATING);
            gpio_set_level(pin, 0);
            gpio_set_direction(pin, GPIO_MODE_OUTPUT);
            // 仅支持 RTC 功能的引脚可通过 RTC 保持电平（I2S 引脚不支持）。
            if (rtc_gpio_is_valid_gpio(pin)) {
                rtc_gpio_init(pin);
                rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
                rtc_gpio_pullup_dis(pin);
                rtc_gpio_pulldown_dis(pin);
                rtc_gpio_set_level(pin, 0);
                rtc_gpio_hold_en(pin);
            } else {
                gpio_hold_en(pin);
            }
        }
        // 共享 I2C 上拉仍连接至 MCU 的 3.3V 电源。将总线拉低，避免
        // 通过输入保护二极管向已关闭的编解码器电源反向供电。
        for (auto pin : {I2C_SDA_IO, I2C_SCL_IO}) {
            rtc_gpio_init(pin);
            rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
            rtc_gpio_pullup_dis(pin);
            rtc_gpio_pulldown_dis(pin);
            rtc_gpio_set_level(pin, 0);
            rtc_gpio_hold_en(pin);
        }
        for (auto pin : {AUDIO_PA_ENABLE_GPIO, DISPLAY_BACKLIGHT_PIN, RUN_LED_GPIO,
                         POWER_EN1_GPIO, POWER_EN2_GPIO}) {
            rtc_gpio_init(pin);
            rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
            rtc_gpio_set_level(pin, pin == POWER_EN1_GPIO ? 1 : 0);
            rtc_gpio_hold_en(pin);
        }
        gpio_deep_sleep_hold_en();
        ESP_LOGI(TAG, "Deep sleep: KEY_ON GPIO%d wake level=%d",
                 static_cast<int>(PWR_BUTTON_GPIO), POWER_BUTTON_ACTIVE_LEVEL);
        esp_deep_sleep_start();
    }

    bool QualifyPowerOn() {
        const auto reset_reason = esp_reset_reason();
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        const bool ext0_wakeup = (esp_sleep_get_wakeup_causes() & (1UL << ESP_SLEEP_WAKEUP_EXT0)) != 0;
#else
        const bool ext0_wakeup = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#endif
        // 软件复位、看门狗复位以及下载/复位后均可正常启动。
        if (reset_reason != ESP_RST_POWERON && !ext0_wakeup) return true;
        ESP_LOGI(TAG, "Hold power button for %d ms to power on", POWER_BUTTON_HOLD_MS);
        const int64_t start = esp_timer_get_time();
        while (esp_timer_get_time() - start < POWER_BUTTON_HOLD_MS * 1000LL) {
            if (gpio_get_level(PWR_BUTTON_GPIO) != POWER_BUTTON_ACTIVE_LEVEL) {
                ESP_LOGI(TAG, "Power-on cancelled: KEY_ON released before %d ms", POWER_BUTTON_HOLD_MS);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
        }
        const bool pressed = gpio_get_level(PWR_BUTTON_GPIO) == POWER_BUTTON_ACTIVE_LEVEL;
        if (!pressed) ESP_LOGI(TAG, "Power-on cancelled: KEY_ON released at hold completion");
        return pressed;
    }

    void EnablePeripheralPower() {
        ConfigureOutput(POWER_EN1_GPIO, 0);
        ConfigureOutput(POWER_EN2_GPIO, 1);
        ConfigureOutput(RUN_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        // 两个控制器共用 SCLK/MOSI/DC。在 RESET 仍为低电平时取消两个控制器的片选，
        // 确保此操作早于任一屏幕 IO 或 SPI 总线发送命令。
        ConfigureOutput(DISPLAY_1_SPI_CS_GPIO, 1);
        ConfigureOutput(DISPLAY_2_SPI_CS_GPIO, 1);
        ESP_LOGI(TAG, "Screen power ready: backlight off, reset low, both CS high");
    }

    void SetScreenOn(bool on) {
        auto backlight = GetBacklight();
        if (on) {
            if (display_ == nullptr || !display_->SetScreenEnabled(true)) {
                backlight->SetBrightness(0);
                screen_on_ = false;
                ESP_LOGE(TAG, "Screen startup incomplete; backlight stays off");
                return;
            }
            ESP_LOGI(TAG, "Both screens enabled with valid frames; restoring backlight");
            backlight->RestoreBrightness();
            screen_on_ = true;
        } else {
            backlight->SetBrightness(0);
            if (display_ != nullptr) {
                display_->SetScreenEnabled(false);
            }
            screen_on_ = false;
        }
    }

    void Shutdown() {
        ESP_LOGI(TAG, "Power off: release KEY_ON to enter deep sleep");
        SetScreenOn(false);
        Application::GetInstance().GetAudioService().Stop();
        if (codec_) {
            codec_->EnableOutput(false);
            codec_->EnableInput(false);
        }
        // 等待异步背光渐变完成，再断开 PWM。
        vTaskDelay(pdMS_TO_TICKS(550));
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, DISPLAY_BACKLIGHT_OFF_LEVEL);
        ConfigureOutput(AUDIO_PA_ENABLE_GPIO, 0);
        ConfigureWakeAndPowerOff();
    }

    void ToggleWifiConfig() {
        if (GetNetworkType() != NetworkType::WIFI) {
            ESP_LOGI(TAG, "KEY_1 long press: WiFi provisioning unavailable in 4G mode");
            return;
        }
        auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
        if (wifi_board.IsInWifiConfigMode()) {
            ESP_LOGI(TAG, "KEY_1 long press: exit WiFi config and reboot");
            Application::GetInstance().Reboot();
            return;
        }
        ESP_LOGI(TAG, "KEY_1 long press: enter WiFi config");
        wifi_board.EnterWifiConfigMode();
    }

    static void PlayTouchReaction(const char* emotion, const std::string_view& sound) {
        Application::GetInstance().Schedule([emotion, sound]() {
            Board::GetInstance().GetDisplay()->SetEmotion(emotion);
            Application::GetInstance().PlaySound(sound);
        });
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_SDA_IO,
            .scl_io_num = I2C_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeTca9554() {
        const auto probe = i2c_master_probe(i2c_bus_, TCA9554_I2C_ADDRESS, 100);
        if (probe != ESP_OK) {
            ESP_LOGW(TAG, "TCA9554 probe 0x20 failed: %s", esp_err_to_name(probe));
            return;
        }
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = TCA9554_I2C_ADDRESS;
        cfg.scl_speed_hz = 100000;
        i2c_master_dev_handle_t device = nullptr;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &cfg, &device));
        const uint8_t inputs[] = {0x03, 0xFF}; // 配置：全部八个引脚均设为输入。
        const auto result = i2c_master_transmit(device, inputs, sizeof(inputs), 100);
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(device));
        ESP_ERROR_CHECK(result);
        ESP_LOGI(TAG, "TCA9554 0x20: all ports input (IMU/SD not populated)");
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_GPIO;
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_GPIO;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void ResetEyes() {
        // 共用一次硬件复位。在两个 CS 均为高电平前，不可释放 RESET。
        ConfigureOutput(DISPLAY_SPI_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_SPI_RST_GPIO, 1));
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    void CreateEyePanel(gpio_num_t cs_pin, bool mirror_x, esp_lcd_panel_io_handle_t* io_out,
                        esp_lcd_panel_handle_t* panel_out) {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = cs_pin;
        io_config.dc_gpio_num = DISPLAY_SPI_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, io_out));

        gc9d01_vendor_config_t vendor_config = {
            .init_cmds = kGc9d01InitCommands,
            .init_cmds_size = static_cast<uint16_t>(sizeof(kGc9d01InitCommands) /
                                                    sizeof(kGc9d01InitCommands[0])),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = &vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(*io_out, &panel_config, panel_out));
        // ResetEyes 已同时复位两个控制器。驱动在此处
        // 不得再次操作共用复位线，也不得再次发送软件复位命令。
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_out, false));
        ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_out));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_out, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_out, mirror_x, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_out, false));
        ESP_LOGI(TAG, "Eye CS%d initialized with display off", static_cast<int>(cs_pin));
    }

    void InitializeEyes() {
        ESP_LOGI(TAG, "Init dual GC9D01N eyes");
        ResetEyes();

        esp_lcd_panel_io_handle_t left_io = nullptr;
        esp_lcd_panel_io_handle_t right_io = nullptr;
        esp_lcd_panel_handle_t left_panel = nullptr;
        esp_lcd_panel_handle_t right_panel = nullptr;
        CreateEyePanel(DISPLAY_1_SPI_CS_GPIO, false, &left_io, &left_panel);
        CreateEyePanel(DISPLAY_2_SPI_CS_GPIO, false, &right_io, &right_panel);
        display_ = new DualEyeDisplay(left_io, left_panel, right_io, right_panel);
    }

    void InitializeButtons() {
        key1_button_ = std::make_unique<Button>(KEY_1_GPIO, false, 2000);
        key2_button_ = std::make_unique<Button>(KEY_2_GPIO, false, 2000);
        pwr_button_ = std::make_unique<Button>(PWR_BUTTON_GPIO, POWER_BUTTON_ACTIVE_LEVEL == 1, POWER_BUTTON_HOLD_MS);
        // Button 默认启用内部上下拉；本板使用外部 R53。
        ESP_ERROR_CHECK(gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_FLOATING));
        touch1_button_ = std::make_unique<Button>(TOUCH_1_GPIO, TOUCH_ACTIVE_HIGH);
        touch2_button_ = std::make_unique<Button>(TOUCH_2_GPIO, TOUCH_ACTIVE_HIGH);
        // 实装 TOG 接地：松开时不得再次触发响应。
        touch1_button_->OnPressDown([this]() {
            if (!shutting_down_) PlayTouchReaction("happy", BoardSounds::OGG_SHORT_LAUGH);
        });
        touch2_button_->OnPressDown([this]() {
            if (!shutting_down_) PlayTouchReaction("angry", BoardSounds::OGG_TSUNDERE);
        });
        power_button_released_ = gpio_get_level(PWR_BUTTON_GPIO) != POWER_BUTTON_ACTIVE_LEVEL;
        pwr_button_->OnPressDown([this]() { power_press_accepted_ = power_button_released_; });
        pwr_button_->OnPressUp([this]() { power_button_released_ = true; });
        pwr_button_->OnClick([this]() {
            if (!power_press_accepted_ || shutting_down_) return;
            Application::GetInstance().Schedule([this]() {
                if (!shutting_down_) SetScreenOn(!screen_on_);
            });
        });
        pwr_button_->OnLongPress([this]() {
            // 忽略开机时的首次持续按压，直到松开并重新按下。
            if (!power_press_accepted_ || shutting_down_.exchange(true)) return;
            Application::GetInstance().Schedule([this]() { Shutdown(); });
        });
        key1_button_->OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                if (!shutting_down_) Application::GetInstance().ToggleChatState();
            });
        });
        key1_button_->OnLongPress([this]() {
            Application::GetInstance().Schedule([this]() { if (!shutting_down_) ToggleWifiConfig(); });
        });
        key2_button_->OnClick([this]() {
            if (shutting_down_) return;
            Application::GetInstance().Schedule([this]() {
                if (shutting_down_ || display_ == nullptr) return;
                ESP_LOGI(TAG, "KEY_2 click: next animation");
                display_->ShowNextAnimation();
            });
        });
        key2_button_->OnLongPress([this]() {
            if (shutting_down_) return;
            Application::GetInstance().Schedule([this]() {
                if (!shutting_down_) SwitchNetworkType();
            });
        });
    }

    void InitializeBattery() { battery_monitor_ = new PowerManager(); }

public:
    AiDualEyedDollBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN) {
        InitializePowerOff();
        if (!QualifyPowerOn()) ConfigureWakeAndPowerOff();
        EnablePeripheralPower();
        InitializeI2c();
        InitializeTca9554();
        InitializeSpi();
        InitializeEyes();
        InitializeButtons();
        InitializeBattery();
        SetScreenOn(true);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static DollAudioCodec audio_codec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_PA_ENABLE_GPIO);
        codec_ = &audio_codec;
        return codec_;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (shutting_down_ || !battery_monitor_) return false;
        return battery_monitor_->GetStatus(level, charging, discharging);
    }
};

DECLARE_BOARD(AiDualEyedDollBoard);

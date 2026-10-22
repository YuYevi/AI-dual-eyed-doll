// ==================== 依赖与板级配置 ====================
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
#include "codecs/box_audio_codec.h"
#include "settings.h"

#include "config.h"
#include "dual_eye_display.h"
#include "esp_lcd_panel_vendor.h"

#define TAG "AiDualEyedDoll"

// GPIO43/44 供模组使用；将应用控制台的所有输入输出切换至 USB。
#if !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || !defined(CONFIG_ESP_CONSOLE_SECONDARY_NONE)
#error "AI dual-eyed doll requires USB Serial/JTAG primary console and no secondary console; GPIO43/44 are reserved for ML307C"
#endif

// ==================== 音频适配：采集通道与功放控制 ====================
// 复用上游 SZPI 的驱动结构：一路人声麦克风和一路回声参考。
class CustomAudioCodec : public BoxAudioCodec {
public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus)
        : BoxAudioCodec(i2c_bus, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                        AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                        AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, GPIO_NUM_NC,
                        AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE,
                        28.0f,  // 物理 MIC1 的增益
                        2,      // 物理 MIC3 作为播放回声参考输入
                        0.0f) {}

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_PA_ENABLE_GPIO, 1));
        } else {
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_PA_ENABLE_GPIO, 0));
        }
    }
};

// ==================== 双眼屏幕：GC9D01N 初始化命令 ====================
// BOE 0.9-inch ST7735S power, frame-rate, VCOM, and gamma settings.
// ST7789 handles SLPOUT, MADCTL, and COLMOD; keep display off until both
// controllers have received their first complete frames.
struct PanelInitCmd {
    uint8_t cmd;
    const uint8_t* data;
    size_t data_bytes;
};

static const PanelInitCmd kBoePanelInit[] = {
    {0xB1, (const uint8_t[]){0x02, 0x35, 0x36}, 3},
    {0xB2, (const uint8_t[]){0x02, 0x35, 0x36}, 3},
    {0xB3, (const uint8_t[]){0x02, 0x35, 0x36, 0x02, 0x35, 0x36}, 6},
    {0xB4, (const uint8_t[]){0x00}, 1},
    {0xC0, (const uint8_t[]){0xA2, 0x02, 0x84}, 3},
    {0xC1, (const uint8_t[]){0xC5}, 1},
    {0xC2, (const uint8_t[]){0x0D, 0x00}, 2},
    {0xC3, (const uint8_t[]){0x8A, 0x2A}, 2},
    {0xC4, (const uint8_t[]){0x8D, 0xEE}, 2},
    {0xC5, (const uint8_t[]){0x02}, 1},
    {0xE0, (const uint8_t[]){0x12, 0x1C, 0x10, 0x18, 0x33, 0x2C, 0x25, 0x28, 0x28, 0x27, 0x2F, 0x3C, 0x00, 0x03, 0x03, 0x10}, 16},
    {0xE1, (const uint8_t[]){0x12, 0x1C, 0x10, 0x18, 0x2D, 0x28, 0x23, 0x28, 0x28, 0x26, 0x2F, 0x3B, 0x00, 0x03, 0x03, 0x10}, 16},
};

class AiDualEyedDollBoard : public DualNetworkBoard {
private:
    // ---------- 外设对象与按键句柄 ----------
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    DualEyeDisplay* display_ = nullptr;
    PowerManager* battery_monitor_ = nullptr;
    std::unique_ptr<Button> key1_button_, key2_button_, pwr_button_, touch1_button_, touch2_button_;
    // ---------- 显示、电源按键与关机状态 ----------
    bool screen_on_ = false;
    bool power_button_released_ = false;
    bool power_press_accepted_ = false;
    std::atomic<bool> shutting_down_{false};
    AudioCodec* codec_ = nullptr;

    // ---------- 触摸保护：启动等待、稳定松开与再次触发许可 ----------
    esp_timer_handle_t touch_guard_timer_ = nullptr;
    int64_t touch_ready_after_ = 0;
    uint8_t touch_release_ticks_[2] = {};
    std::atomic<bool> touch_armed_[2] = {};


    // ==================== 电源管理：安全上电与深度睡眠关机 ====================
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
                         AUDIO_I2S_GPIO_DOUT, I2C_SDA_IO, I2C_SCL_IO}) {
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

        // 关闭任一外设电源前，先将 MCU 驱动的信号保持为低电平。
        // 电平保持可防止后台 SPI/I2S 活动改变引脚电平。
        // 保持调度器运行，直至 IDF 执行进入睡眠前的关闭流程。
        for (auto pin : {DISPLAY_SPI_SCLK_GPIO, DISPLAY_SPI_MOSI_GPIO, DISPLAY_SPI_DC_GPIO,
                         DISPLAY_SPI_RST_GPIO, DISPLAY_1_SPI_CS_GPIO, DISPLAY_2_SPI_CS_GPIO,
                         AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                         AUDIO_I2S_GPIO_DOUT}) {
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

    // ---------- 屏幕显示与背光开关 ----------
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

    // ---------- 关机入口：停止音频、关闭背光并进入深度睡眠 ----------
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

    // ==================== 网络操作：进入 WiFi 配网 ====================
    void EnterBoardWifiConfig() {
        if (GetNetworkType() != NetworkType::WIFI) {
            // 先持久化配网请求，再切换网络，重启后进入 WiFi 配网。
            {
                Settings settings("doll", true);
                settings.SetInt("wifi_config", 1);
            }
            SwitchNetworkType();
            return;
        }
        auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
        if (!wifi_board.IsInWifiConfigMode()) {
            wifi_board.EnterWifiConfigMode();
        }
    }

    // ==================== 触摸反馈：仅在待机且无播放时执行 ====================
    static void PlayTouchReaction(const char* emotion, const std::string_view& sound) {
        Application::GetInstance().Schedule([emotion, sound]() {
            auto& app = Application::GetInstance();
            // 对话和其他播放优先，排队后的触摸事件也必须重新检查。
            if (app.GetDeviceState() != kDeviceStateIdle || !app.GetAudioService().IsPlaybackIdle()) return;
            Board::GetInstance().GetDisplay()->SetEmotion(emotion);
            Application::GetInstance().PlaySound(sound);
        });
    }

    // ==================== 外设总线：共享 I2C 与 TCA9554 扩展器 ====================
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_PORT,
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

    // ==================== 双眼显示：SPI、共享复位与面板创建 ====================
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

    void CreateEyePanel(gpio_num_t cs_pin, esp_lcd_panel_io_handle_t* io_out,
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

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = DISPLAY_BITS_PER_PIXEL;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(*io_out, &panel_config, panel_out));
        // ResetEyes already reset both controllers; never reset one while
        // the other is initialized on the shared reset line.
        ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_out));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_out, false));
        for (const auto& item : kBoePanelInit) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(*io_out, item.cmd, item.data, item.data_bytes));
        }
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel_out, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_out, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_out, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_out, false));
        ESP_LOGI(TAG, "Eye CS%d initialized with display off", static_cast<int>(cs_pin));
    }

    void InitializeEyes() {
        ESP_LOGI(TAG, "Init dual BOE ST7735S 128x128 eyes");
        ResetEyes();

        esp_lcd_panel_io_handle_t left_io = nullptr;
        esp_lcd_panel_io_handle_t right_io = nullptr;
        esp_lcd_panel_handle_t left_panel = nullptr;
        esp_lcd_panel_handle_t right_panel = nullptr;
        CreateEyePanel(DISPLAY_1_SPI_CS_GPIO, &left_io, &left_panel);
        CreateEyePanel(DISPLAY_2_SPI_CS_GPIO, &right_io, &right_panel);
        display_ = new DualEyeDisplay(left_io, left_panel, right_io, right_panel);
    }

    // ==================== 输入事件：触摸、电源键、KEY1 与 KEY2 ====================
    void InitializeButtons() {
        key1_button_ = std::make_unique<Button>(KEY_1_GPIO, false, 2000);
        key2_button_ = std::make_unique<Button>(KEY_2_GPIO, false, 2000);
        pwr_button_ = std::make_unique<Button>(PWR_BUTTON_GPIO, POWER_BUTTON_ACTIVE_LEVEL == 1, POWER_BUTTON_HOLD_MS);
        // Button 默认启用内部上下拉；本板使用外部 R53。
        ESP_ERROR_CHECK(gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_FLOATING));
        // ---------- 触摸：启动防误触与对话期间屏蔽 ----------
        touch1_button_ = std::make_unique<Button>(TOUCH_1_GPIO, TOUCH_ACTIVE_HIGH);
        touch2_button_ = std::make_unique<Button>(TOUCH_2_GPIO, TOUCH_ACTIVE_HIGH);
        // 上电稳定 2 秒后，连续松开 300 毫秒才允许下一次触摸，不阻塞音频任务。
        touch_ready_after_ = esp_timer_get_time() + 2000000;
        touch1_button_->OnPressDown([this]() {
            if (!touch_armed_[0].exchange(false) || shutting_down_) return;
            if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
                PlayTouchReaction("happy", BoardSounds::OGG_SHORT_LAUGH);
        });
        touch2_button_->OnPressDown([this]() {
            if (!touch_armed_[1].exchange(false) || shutting_down_) return;
            if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
                PlayTouchReaction("angry", BoardSounds::OGG_TSUNDERE);
        });
        esp_timer_create_args_t touch_timer_args = {};
        touch_timer_args.callback = [](void* arg) {
            auto* board = static_cast<AiDualEyedDollBoard*>(arg);
            if (board->shutting_down_ || esp_timer_get_time() < board->touch_ready_after_) return;
            const gpio_num_t pins[] = {TOUCH_1_GPIO, TOUCH_2_GPIO};
            for (int i = 0; i < 2; ++i) {
                if (gpio_get_level(pins[i]) == (TOUCH_ACTIVE_HIGH ? 0 : 1)) {
                    if (board->touch_release_ticks_[i] < 6) ++board->touch_release_ticks_[i];
                    if (board->touch_release_ticks_[i] == 6) board->touch_armed_[i] = true;
                } else {
                    board->touch_release_ticks_[i] = 0;
                }
            }
        };
        touch_timer_args.arg = this;
        touch_timer_args.name = "touch_guard";
        ESP_ERROR_CHECK(esp_timer_create(&touch_timer_args, &touch_guard_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touch_guard_timer_, 50000));
        // ---------- 电源键：短按切换对话，长按 3 秒关机 ----------
        power_button_released_ = gpio_get_level(PWR_BUTTON_GPIO) != POWER_BUTTON_ACTIVE_LEVEL;
        pwr_button_->OnPressDown([this]() { power_press_accepted_ = power_button_released_; });
        pwr_button_->OnPressUp([this]() { power_button_released_ = true; });
        pwr_button_->OnClick([this]() {
            if (!power_press_accepted_ || shutting_down_) return;
            Application::GetInstance().Schedule([this]() {
                if (!shutting_down_) Application::GetInstance().ToggleChatState();
            });
        });
        pwr_button_->OnLongPress([this]() {
            // 忽略开机时的首次持续按压，直到松开并重新按下。
            if (!power_press_accepted_ || shutting_down_.exchange(true)) return;
            Application::GetInstance().Schedule([this]() { Shutdown(); });
        });
        // ---------- KEY1：单击切换网络，三击进入 WiFi 配网 ----------
        key1_button_->OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                if (!shutting_down_) SwitchNetworkType();
            });
        });
        key1_button_->OnMultipleClick([this]() {
            Application::GetInstance().Schedule([this]() { if (!shutting_down_) EnterBoardWifiConfig(); });
        }, 3);
        // ---------- KEY2：单击手动切换表情 ----------
        key2_button_->OnClick([this]() {
            if (shutting_down_) return;
            Application::GetInstance().Schedule([this]() {
                if (shutting_down_ || display_ == nullptr) return;
                ESP_LOGI(TAG, "KEY_2 click: next animation");
                display_->ShowNextAnimation();
            });
        });
    }

    // ==================== 电池监测 ====================
    void InitializeBattery() { battery_monitor_ = new PowerManager(); }

public:
    // ==================== 板卡启动：按电源、总线、显示、输入的顺序初始化 ====================
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

    // ==================== 框架接口：音频、显示、网络启动、背光与电池状态 ====================
    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(i2c_bus_);
        codec_ = &audio_codec;
        return codec_;
    }

    virtual Display* GetDisplay() override { return display_; }

    void StartNetwork() override {
        bool enter_config = false;
        {
            Settings settings("doll", true);
            enter_config = settings.GetInt("wifi_config", 0) != 0;
            settings.SetInt("wifi_config", 0);
        }
        DualNetworkBoard::StartNetwork();
        if (enter_config && GetNetworkType() == NetworkType::WIFI) {
            // 此时 WiFi 管理器已初始化；已有配网热点时无需重复进入。
            auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
            if (!wifi_board.IsInWifiConfigMode()) {
                wifi_board.EnterWifiConfigMode();
            }
        }
    }

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

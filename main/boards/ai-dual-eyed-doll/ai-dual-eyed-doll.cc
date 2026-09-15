#include "wifi_board.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_gc9d01.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string_view>
#include <wifi_manager.h>

#include "adc_battery_monitor.h"
#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "button.h"
#include "codecs/es8389_audio_codec.h"
#include "common/dual_eye_display.h"
#include "common/power_manager.h"
#include "config.h"

#define TAG "AiDualEyedDoll"

extern const char short_laugh_ogg_start[] asm("_binary_Short_laugh_ogg_start");
extern const char short_laugh_ogg_end[] asm("_binary_Short_laugh_ogg_end");
extern const char tsundere_ogg_start[] asm("_binary_tsundere_ogg_start");
extern const char tsundere_ogg_end[] asm("_binary_tsundere_ogg_end");

static const std::string_view kOggShortLaugh{
    static_cast<const char*>(short_laugh_ogg_start),
    static_cast<size_t>(short_laugh_ogg_end - short_laugh_ogg_start)};
static const std::string_view kOggTsundere{
    static_cast<const char*>(tsundere_ogg_start),
    static_cast<size_t>(tsundere_ogg_end - tsundere_ogg_start)};

/* Vendor init from docs/原理图与外设手册/0.71寸初始化BOE+GC9D01.txt */
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
    {0x29, nullptr, 0, 0},
    {0x2C, nullptr, 0, 0},
};

class AiDualEyedDollBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    DualEyeDisplay* display_ = nullptr;
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    Button key1_button_;
    Button pwr_button_;
    Button touch1_button_;
    Button touch2_button_;
    bool screen_on_ = true;
    bool power_button_released_ = false;

    void SetScreenOn(bool on) {
        auto backlight = GetBacklight();
        auto display = GetDisplay();
        if (on) {
            if (display != nullptr) {
                display->SetPowerSaveMode(false);
            }
            backlight->RestoreBrightness();
            screen_on_ = true;
        } else {
            backlight->SetBrightness(0);
            if (display != nullptr) {
                display->SetPowerSaveMode(true);
            }
            screen_on_ = false;
        }
    }

    void Shutdown() {
        ESP_LOGI(TAG, "Power off");
        SetScreenOn(false);
        auto codec = GetAudioCodec();
        if (codec != nullptr) {
            codec->EnableOutput(false);
        }
        PowerManager::ConfigureWakeAndPowerOff();
    }

    void ToggleWifiConfig() {
        if (IsInWifiConfigMode()) {
            ESP_LOGI(TAG, "KEY_1 long press: exit WiFi config");
            WifiManager::GetInstance().StopConfigAp();
            in_config_mode_ = false;
            Application::GetInstance().DismissAlert();
            TryWifiConnect();
            return;
        }
        ESP_LOGI(TAG, "KEY_1 long press: enter WiFi config");
        EnterWifiConfigMode();
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
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
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

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void ResetEyes() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = 1ULL << DISPLAY_RST_PIN;
        io_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io_conf);
        gpio_set_level(DISPLAY_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(DISPLAY_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(DISPLAY_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    void CreateEyePanel(gpio_num_t cs_pin, bool mirror_x, esp_lcd_panel_io_handle_t* io_out,
                        esp_lcd_panel_handle_t* panel_out) {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = cs_pin;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
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
        ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_out));
        ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_out));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_out, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_out, mirror_x, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_out, true));
    }

    void InitializeEyes() {
        ESP_LOGI(TAG, "Init dual GC9D01N eyes");
        ResetEyes();

        esp_lcd_panel_io_handle_t left_io = nullptr;
        esp_lcd_panel_io_handle_t right_io = nullptr;
        esp_lcd_panel_handle_t left_panel = nullptr;
        esp_lcd_panel_handle_t right_panel = nullptr;
        CreateEyePanel(DISPLAY_LEFT_CS_PIN, false, &left_io, &left_panel);
        CreateEyePanel(DISPLAY_RIGHT_CS_PIN, true, &right_io, &right_panel);
        display_ = new DualEyeDisplay(left_io, left_panel, right_io, right_panel);
    }

    void InitializeButtons() {
        touch1_button_.OnClick([]() { PlayTouchReaction("happy", kOggShortLaugh); });
        touch2_button_.OnClick([]() { PlayTouchReaction("angry", kOggTsundere); });

        pwr_button_.OnPressUp([this]() { power_button_released_ = true; });
        pwr_button_.OnClick([this]() { SetScreenOn(!screen_on_); });
        pwr_button_.OnLongPress([this]() {
            if (!power_button_released_) {
                ESP_LOGI(TAG, "Ignore power long press held from boot");
                return;
            }
            Application::GetInstance().Schedule([this]() { Shutdown(); });
        });
        power_button_released_ = gpio_get_level(PWR_BUTTON_GPIO) != 0;

        key1_button_.OnClick([]() { Application::GetInstance().ToggleChatState(); });
        key1_button_.OnLongPress([this]() { ToggleWifiConfig(); });
    }

    void InitializeBattery() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = 1ULL << CHARGE_DETECT_PIN;
        io_conf.mode = GPIO_MODE_INPUT;
        gpio_config(&io_conf);
        battery_monitor_ = new AdcBatteryMonitor(POWER_BATTERY_ADC_UNIT, POWER_BATTERY_ADC_CHANNEL,
                                                 200000, 200000, GPIO_NUM_NC);
    }

public:
    AiDualEyedDollBoard()
        : key1_button_(KEY_1_BUTTON_GPIO, false, 2000),
          pwr_button_(PWR_BUTTON_GPIO, false, POWER_BUTTON_HOLD_MS),
          touch1_button_(TOUCH_1_GPIO, true),
          touch2_button_(TOUCH_2_GPIO, true) {
        if (!PowerManager::QualifyPowerOn()) {
            PowerManager::ConfigureWakeAndPowerOff();
        }
        PowerManager::HoldPower();
        InitializeI2c();
        InitializeSpi();
        InitializeEyes();
        InitializeButtons();
        InitializeBattery();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8389AudioCodec audio_codec(
            i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8389_ADDR, true,
            AUDIO_INPUT_CHANNELS);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        /* ETA6098 STAT is open-drain and active-low, with R3 51k pull-up. */
        charging = gpio_get_level(CHARGE_DETECT_PIN) == 0;
        discharging = !charging;
        level = battery_monitor_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(AiDualEyedDollBoard);

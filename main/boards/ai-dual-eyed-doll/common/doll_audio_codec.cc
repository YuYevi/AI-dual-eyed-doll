#include "doll_audio_codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <driver/gpio.h>
#include <driver/i2s_tdm.h>
#include <esp_log.h>
#include <freertos/task.h>

namespace {
constexpr const char* TAG = "DollAudioCodec";
constexpr int kFrames = AUDIO_CODEC_DMA_FRAME_NUM;
constexpr uint32_t kIoTimeoutMs = 1000;
bool CodecOk(int result, const char* operation) {
    if (result == ESP_CODEC_DEV_OK) return true;
    ESP_LOGE(TAG, "%s failed: %d", operation, result);
    return false;
}
}

DollAudioCodec::DollAudioCodec(void* bus, int sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
    gpio_num_t dout, gpio_num_t din, gpio_num_t pa_pin) : pa_pin_(pa_pin) {
    // 此处仅验证 ES7210 从机配置是否符合 V0.2 的 24 kHz 采样率约定。
    ESP_ERROR_CHECK(sample_rate == 24000 && bus != nullptr ? ESP_OK : ESP_ERR_INVALID_ARG);
    duplex_ = true;
    input_reference_ = true;
    input_channels_ = 3; // AFE MMR：MIC1、MIC2，以及参考通道 MIC3。
    output_channels_ = 1;
    input_sample_rate_ = output_sample_rate_ = sample_rate;
    input_gain_ = 30.0f;
    ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(pa_pin_) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(gpio_set_level(pa_pin_, 0));
    ESP_ERROR_CHECK(gpio_set_direction(pa_pin_, GPIO_MODE_OUTPUT));

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    channel.dma_frame_num = kFrames;
    channel.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &tx_handle_, &rx_handle_));

    // 物理帧格式固定：共 64 个 BCLK 周期，WS 低电平和高电平各占 32 个时钟周期。
    // TX 采用 Philips 立体声格式，在 32 位时隙中传输 16 位数据；RX 采用四个 16 位的
    // Philips TDM 时隙。因此两个方向使用完全相同的时钟。
    i2s_std_config_t tx = {};
    tx.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sample_rate));
    tx.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    tx.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    tx.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    tx.slot_cfg.ws_width = 32;
    tx.gpio_cfg.mclk = mclk;
    tx.gpio_cfg.bclk = bclk;
    tx.gpio_cfg.ws = ws;
    tx.gpio_cfg.dout = dout;
    tx.gpio_cfg.din = I2S_GPIO_UNUSED;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx));

    i2s_tdm_config_t rx = {};
    rx.clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sample_rate));
    rx.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    rx.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO, static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3));
    rx.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    rx.slot_cfg.total_slot = 4;
    rx.slot_cfg.ws_width = 32;
    rx.slot_cfg.left_align = true;
    rx.gpio_cfg.mclk = mclk;
    rx.gpio_cfg.bclk = bclk;
    rx.gpio_cfg.ws = ws;
    rx.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx.gpio_cfg.din = din;
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &rx));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));

    // codec_dev 的通用 I2S 数据接口会动态改变时隙宽度，
    // 且 esp_codec_dev_open 会忽略其 set_fmt/enable 返回值。因此直接使用
    // 编解码器控制接口，并自行管理这对固定配置的 I2S 通道。
    // 以下地址为 8 位地址：I2C 适配器会右移一位，以适配 IDF 的 7 位地址总线。
    audio_codec_i2c_cfg_t ctrl = {};
    ctrl.port = 0;
    ctrl.bus_handle = bus;
    ctrl.addr = 0x18 << 1;
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&ctrl);
    ctrl.addr = 0x41 << 1;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&ctrl);
    gpio_if_ = audio_codec_new_gpio();
    ESP_ERROR_CHECK(out_ctrl_if_ && in_ctrl_if_ && gpio_if_ ? ESP_OK : ESP_ERR_NO_MEM);
    es8311_codec_cfg_t dac = {};
    dac.ctrl_if = out_ctrl_if_;
    dac.gpio_if = gpio_if_;
    dac.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    dac.pa_pin = -1; // PA 由此处管理；驱动不得提前使能。
    dac.use_mclk = true;
    dac.mclk_div = 256;
    dac.hw_gain.pa_voltage = 3.3f;
    dac.hw_gain.codec_dac_voltage = 3.3f;
    out_codec_if_ = es8311_codec_new(&dac);
    es7210_codec_cfg_t adc = {};
    adc.ctrl_if = in_ctrl_if_;
    adc.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3;
    adc.master_mode = false;
    adc.mclk_div = 256;
    in_codec_if_ = es7210_codec_new(&adc);
    ESP_ERROR_CHECK(out_codec_if_ && in_codec_if_ ? ESP_OK : ESP_FAIL);
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.sample_rate = sample_rate;
    fs.mclk_multiple = 256;
    fs.channel = 4;
    fs.channel_mask = 0x0f;
    ESP_ERROR_CHECK(CodecOk(in_codec_if_->set_fs(in_codec_if_, &fs), "ADC format") ? ESP_OK : ESP_FAIL);
    fs.channel = 1;
    fs.channel_mask = 0;
    ESP_ERROR_CHECK(CodecOk(out_codec_if_->set_fs(out_codec_if_, &fs), "DAC format") ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ApplyInputGain(input_gain_) ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "24kHz MMR; MCLK=6144000 BCLK=1536000 WS=24000; RX MIC1,MIC3,MIC2,MIC4 -> MIC1,MIC2,MIC3; AU3V3=3.3V");
}

void DollAudioCodec::SetPa(bool enable) {
    ESP_ERROR_CHECK(gpio_set_level(pa_pin_, enable ? 1 : 0));
}

bool DollAudioCodec::ApplyInputGain(float gain) {
    // 增益掩码使用物理 MIC 编号，而非串行时隙编号。
    return CodecOk(in_codec_if_->set_mic_channel_gain(in_codec_if_, 0x03, gain), "MIC1/2 gain") &&
        CodecOk(in_codec_if_->set_mic_channel_gain(in_codec_if_, 0x04, 0.0f), "MIC3 reference gain");
}

bool DollAudioCodec::ApplyOutputVolume(int volume) {
    const float db = volume == 0 ? -96.0f : -50.0f + volume * 0.5f;
    return CodecOk(out_codec_if_->set_vol(out_codec_if_, db), "DAC volume");
}

void DollAudioCodec::SetInputGain(float gain) {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    if (!std::isfinite(gain)) return;
    gain = std::clamp(gain, 0.0f, 37.5f);
    if (ApplyInputGain(gain)) AudioCodec::SetInputGain(gain);
}

void DollAudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    volume = std::clamp(volume, 0, 100);
    if (ApplyOutputVolume(volume)) AudioCodec::SetOutputVolume(volume);
}

void DollAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    if (enable == input_enabled_) return;
    if (!CodecOk(in_codec_if_->enable(in_codec_if_, enable), "ADC enable")) return;
    // ES7210 的 start() 会恢复增益，因此需在使能后重新设置参考通道增益。
    if (enable && !ApplyInputGain(input_gain_)) {
        in_codec_if_->enable(in_codec_if_, false);
        return;
    }
    if (enable) {
        // 逻辑上禁用时 RX DMA 仍在运行：丢弃队列中的过期帧。
        int16_t discard[kFrames * 4];
        for (int i = 0; i < AUDIO_CODEC_DMA_DESC_NUM + 1; ++i) {
            size_t bytes = 0;
            if (i2s_channel_read(rx_handle_, discard, sizeof(discard), &bytes, kIoTimeoutMs) != ESP_OK || bytes != sizeof(discard)) {
                in_codec_if_->enable(in_codec_if_, false);
                ESP_LOGE(TAG, "ADC startup flush failed");
                return;
            }
        }
    }
    AudioCodec::EnableInput(enable);
}

void DollAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (enable == output_enabled_) return;
    SetPa(false);
    if (!CodecOk(out_codec_if_->enable(out_codec_if_, enable), "DAC enable")) return;
    if (enable) {
        if (!ApplyOutputVolume(output_volume_)) {
            out_codec_if_->enable(out_codec_if_, false);
            return;
        }
        // 在拉高高电平有效的 PA 使能信号前，将队列中的播放数据替换为静音。
        int16_t silence[kFrames * 2] = {};
        for (int i = 0; i < AUDIO_CODEC_DMA_DESC_NUM + 1; ++i) {
            size_t bytes = 0;
            if (i2s_channel_write(tx_handle_, silence, sizeof(silence), &bytes, kIoTimeoutMs) != ESP_OK || bytes != sizeof(silence)) {
                out_codec_if_->enable(out_codec_if_, false);
                ESP_LOGE(TAG, "DAC startup silence failed");
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        SetPa(true);
    }
    AudioCodec::EnableOutput(enable);
}

int DollAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    if (!input_enabled_ || !dest || samples <= 0 || samples % 3 != 0) return 0;
    int16_t raw[kFrames * 4];
    for (int frame = 0; frame < samples / 3;) {
        const int count = std::min(kFrames, samples / 3 - frame);
        const size_t wanted = count * 4 * sizeof(int16_t);
        size_t bytes = 0;
        const esp_err_t err = i2s_channel_read(rx_handle_, raw, wanted, &bytes, kIoTimeoutMs);
        if (err != ESP_OK || bytes != wanted) {
            std::memset(dest, 0, samples * sizeof(int16_t));
            ESP_LOGE(TAG, "RX failed: %s, bytes=%u/%u", esp_err_to_name(err), unsigned(bytes), unsigned(wanted));
            return 0; // 不可将不完整的帧作为有效音频返回。
        }
        for (int i = 0; i < count; ++i) {
            dest[(frame + i) * 3] = raw[i * 4];
            dest[(frame + i) * 3 + 1] = raw[i * 4 + 2];
            dest[(frame + i) * 3 + 2] = raw[i * 4 + 1];
        }
        frame += count;
    }
    return samples;
}

int DollAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!output_enabled_ || !data || samples <= 0) return 0;
    int16_t stereo[kFrames * 2];
    for (int frame = 0; frame < samples;) {
        const int count = std::min(kFrames, samples - frame);
        for (int i = 0; i < count; ++i) {
            stereo[i * 2] = stereo[i * 2 + 1] = data[frame + i];
        }
        const size_t wanted = count * 2 * sizeof(int16_t);
        size_t bytes = 0;
        const esp_err_t err = i2s_channel_write(tx_handle_, stereo, wanted, &bytes, kIoTimeoutMs);
        if (err != ESP_OK || bytes != wanted) {
            SetPa(false);
            out_codec_if_->enable(out_codec_if_, false);
            AudioCodec::EnableOutput(false);
            ESP_LOGE(TAG, "TX failed: %s, bytes=%u/%u", esp_err_to_name(err), unsigned(bytes), unsigned(wanted));
            return 0;
        }
        frame += count;
    }
    return samples;
}

DollAudioCodec::~DollAudioCodec() {
    // 调用方必须先停止音频任务，再销毁编解码器。
    std::scoped_lock lock(rx_mutex_, tx_mutex_);
    SetPa(false);
    CodecOk(out_codec_if_->enable(out_codec_if_, false), "DAC shutdown");
    CodecOk(in_codec_if_->enable(in_codec_if_, false), "ADC shutdown");
    input_enabled_ = output_enabled_ = false;
    // 编解码器接口借用 ctrl/gpio；先销毁借用方，再销毁其依赖对象。
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_codec_if(in_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    // 这些通道不由任何 codec_dev 数据接口管理。保持共享时钟运行，直到
    // 两个芯片均停止，再先禁用 RX，最后禁用产生时钟的 TX。
    ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    ESP_ERROR_CHECK(i2s_del_channel(rx_handle_));
    ESP_ERROR_CHECK(i2s_del_channel(tx_handle_));
}

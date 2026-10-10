#pragma once

#include "audio_codec.h"
#include <esp_codec_dev_defaults.h>
#include <mutex>

// V0.2: ES8311 DAC only; ES7210 MIC1/MIC2 + analog DAC reference on MIC3.
class DollAudioCodec : public AudioCodec {
public:
    DollAudioCodec(void* i2c_master_handle, int sample_rate,
                   gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                   gpio_num_t dout, gpio_num_t din, gpio_num_t pa_pin);
    ~DollAudioCodec() override;
    void SetOutputVolume(int volume) override;
    void SetInputGain(float gain) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;
    gpio_num_t pa_pin_;
    std::mutex rx_mutex_;
    std::mutex tx_mutex_;
    bool ApplyInputGain(float gain);
    bool ApplyOutputVolume(int volume);
    void SetPa(bool enable);
};

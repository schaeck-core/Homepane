#include "audio_hw.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "audio_hw";

static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;
static SemaphoreHandle_t s_init_lock;

esp_err_t audio_hw_init(void)
{
    // First caller creates the lock; app_main and the debug server never
    // race on it in practice, but init itself must not run twice.
    if (!s_init_lock) s_init_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_init_lock, portMAX_DELAY);
    if (s_spk && s_mic) {
        xSemaphoreGive(s_init_lock);
        return ESP_OK;
    }

    // The BSP default is mono at 22.05 kHz; speech needs 16 kHz, and stereo
    // keeps the ES7210's second channel (MIC2) instead of dropping it.
    const i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_HW_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    esp_err_t err = bsp_i2c_init();
    if (err == ESP_OK) err = bsp_audio_init(&i2s_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_init_lock);
        return err;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_spk || !s_mic) {
        ESP_LOGE(TAG, "codec init failed (speaker %p, mic %p)", s_spk, s_mic);
        xSemaphoreGive(s_init_lock);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = AUDIO_HW_CHANNELS,
        .sample_rate = AUDIO_HW_SAMPLE_RATE,
    };
    int ret = esp_codec_dev_open(s_spk, &fs);
    if (ret == ESP_CODEC_DEV_OK) ret = esp_codec_dev_open(s_mic, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed: %d", ret);
        xSemaphoreGive(s_init_lock);
        return ESP_FAIL;
    }
    // Volume 90 ≈ 0 dB DAC gain (the driver maps 100 to +3.6 dB, which clips
    // full-scale speech); 37 dB is the ES7210's maximum analog gain — the
    // microphones are quiet at room distance.
    esp_codec_dev_set_out_vol(s_spk, 90);
    esp_codec_dev_set_in_gain(s_mic, 37.0f);
    ESP_LOGI(TAG, "audio ready: %d Hz, %d ch, 16 bit", AUDIO_HW_SAMPLE_RATE, AUDIO_HW_CHANNELS);
    xSemaphoreGive(s_init_lock);
    return ESP_OK;
}

esp_err_t audio_hw_read(int16_t *frames_lr, size_t frames)
{
    if (!s_mic) return ESP_ERR_INVALID_STATE;
    int ret = esp_codec_dev_read(s_mic, frames_lr, (int)(frames * AUDIO_HW_CHANNELS * sizeof(int16_t)));
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_hw_write(const int16_t *frames_lr, size_t frames)
{
    if (!s_spk) return ESP_ERR_INVALID_STATE;
    int ret = esp_codec_dev_write(s_spk, (void *)frames_lr, (int)(frames * AUDIO_HW_CHANNELS * sizeof(int16_t)));
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_hw_set_volume(int percent)
{
    if (!s_spk) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_set_out_vol(s_spk, percent) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_hw_set_mic_gain(float db)
{
    if (!s_mic) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_set_in_gain(s_mic, db) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}


#include <freertos/FreeRTOS.h>

#include <stdlib.h>
#include <stdint.h>

#include <esp_log.h>
#include <esp_err.h>

#include <driver/i2s.h>

#include "webrtc_vad.h"

#define I2S_DMA_BUFFER_COUNT (4U)     // The total amount of DMA buffers count
#define I2S_DMA_BUFFER_SIZE  (1024U)  // The size of each DMA buffer in samples
#define I2S_SAMPLE_COUNT     (16384U) // The total amount of samples per data transfer
#define I2S_SAMPLE_RATE      (16000U) // The total amount of samples per second

#define I2S_INMP441_PORT (I2S_NUM_0)
#define I2S_INMP441_SCK  (GPIO_NUM_26)
#define I2S_INMP441_WS   (GPIO_NUM_22)
#define I2S_INMP441_SD   (GPIO_NUM_21)

#define VAD_FRAME_LENGTH  (20) // ms
#define VAD_FRAME_SIZE    (I2S_SAMPLE_RATE / 1000 * VAD_FRAME_LENGTH)

static const char* TAG = "ESP32 I2S Mic VAD";
static const TickType_t xDelay = 3000 / portTICK_PERIOD_MS;

void mic_init()
{
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_BUFFER_COUNT,
        .dma_buf_len = I2S_DMA_BUFFER_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    const i2s_pin_config_t i2s_pin_config = {
        .bck_io_num = I2S_INMP441_SCK,
        .ws_io_num = I2S_INMP441_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_INMP441_SD
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_INMP441_PORT, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_INMP441_PORT, &i2s_pin_config));
    ESP_ERROR_CHECK(i2s_set_clk(I2S_INMP441_PORT, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO));
}

size_t mic_read(int16_t *samples, int count)
{
    static int32_t buffer[I2S_DMA_BUFFER_SIZE / sizeof(int32_t)];
    
    size_t sample_index = 0;
    while (count > 0) {
        size_t bytes_need = count * sizeof(int32_t);
        if (bytes_need > sizeof(buffer)) {
            bytes_need = sizeof(buffer);
        }

        size_t bytes_read = 0;
        ESP_ERROR_CHECK(i2s_read(I2S_INMP441_PORT, buffer, bytes_need, &bytes_read, portMAX_DELAY));

        size_t samples_read = bytes_read / sizeof(int32_t);
        for (int i = 0; i < samples_read; ++i) {
            samples[sample_index] = buffer[i] >> 12;
            sample_index++;
            count--;
        }
    }
    return sample_index;
}

void mic_loop()
{
    int16_t* samples = (int16_t*) malloc(sizeof(uint16_t) * I2S_SAMPLE_RATE);
    if (!samples) {
        ESP_LOGE(TAG, "Failed to allocate memory for samples");
        return;
    }

    VadInst* handle = WebRtcVad_Create();
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to create VAD instance");
        vTaskDelay(xDelay);
        esp_restart();
    }
    if (WebRtcVad_Init(handle) != 0) {
        ESP_LOGE(TAG, "Failed to initialize VAD");
        vTaskDelay(xDelay);
        esp_restart();
    }
    if (WebRtcVad_set_mode(handle, 3) != 0) {
        ESP_LOGE(TAG, "Failed to set mode for VAD");
        vTaskDelay(xDelay);
        esp_restart();
    }

    while(true) {
        int samples_read = mic_read(samples, I2S_SAMPLE_RATE);
        if (samples_read < VAD_FRAME_SIZE) {
            ESP_LOGE(TAG, "Too few data");
            continue;
        }

        size_t offset = 0;
        while (offset + VAD_FRAME_SIZE <= samples_read) {
            int rv = WebRtcVad_Process(handle, I2S_SAMPLE_RATE, samples + offset, 320);
            if (rv == +1) {
                ESP_LOGI(TAG, "Voice detected");
                break;
            }
            if (rv == -1) {
                ESP_LOGI(TAG, "Error detected");
                break;
            }
            offset += VAD_FRAME_SIZE;
        }
    }
}

void app_main()
{
    ESP_LOGI(TAG, "ESP32 Mic VAD Example Start");
    mic_init();
    mic_loop();
}
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>

#include <driver/i2s_std.h>

#include <webrtc_vad.h>

/* Pins configuration */
#define I2S_INMP441_SCK (GPIO_NUM_26)
#define I2S_INMP441_WS (GPIO_NUM_22)
#define I2S_INMP441_SD (GPIO_NUM_21)

/* Audio configuration */
#define I2S_SAMPLE_RATE (16000U) // samples per second
#define I2S_SAMPLE_BYTES (4U)    // the amount of bytes in one sample

/* VAD configuration (supported 10, 20 and 30 ms frames and the rates 8000, 16000 and 32000 Hz) */
#define VAD_FRAME_LENGTH (20)                                      // ms
#define VAD_FRAME_SIZE (I2S_SAMPLE_RATE / 1000 * VAD_FRAME_LENGTH) // samples per ms * frame length

static const char* TAG = "ESP32 I2S Mic VAD";

static i2s_chan_handle_t s_rx_handle;

static void
mic_init()
{
    /* Get the default channel configuration */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

    /* Allocate a new RX channel and get the handle of this channel */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle));

    /* Setting the configurations */
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
      /* Each sample has 32bits and only one channel (Mono) */
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                  I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_INMP441_SCK,
              .ws = I2S_INMP441_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_INMP441_SD,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

    /* Initialize the channel */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));

    /* Before reading data, start the RX channel first */
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));
}

static size_t
mic_read(int16_t* samples)
{
    static const size_t s_buffer_size = 512U;
    static int32_t s_buffer[512U];

    int32_t sample_index = 0;
    int32_t need_to_read = I2S_SAMPLE_RATE / 2;
    while (need_to_read > 0) {
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx_handle,
                             (char*) s_buffer,
                             s_buffer_size * I2S_SAMPLE_BYTES,
                             &bytes_read,
                             portMAX_DELAY)
            != ESP_OK) {
            ESP_LOGE(TAG, "Unable to read from audio channel");
            return 0;
        }

        const size_t samples_read = bytes_read / I2S_SAMPLE_BYTES;
        for (size_t i = 0; i < samples_read && need_to_read > 0; ++i) {
            /* Get the highest 16 bits (the smaller, the louder it will be) */
            samples[sample_index++] = s_buffer[i] >> 12;
            /* Decrement needed to read samples  */
            need_to_read--;
        }
    }
    return sample_index;
}

_Noreturn void
mic_loop()
{
    static const TickType_t xDelay = 3000 / portTICK_PERIOD_MS;

    int16_t* samples = (int16_t*) malloc(sizeof(uint16_t) * I2S_SAMPLE_RATE);
    assert(samples != NULL);

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

    while (true) {
        size_t samples_read = mic_read(samples);
        if (samples_read < VAD_FRAME_SIZE) {
            ESP_LOGE(TAG, "Too few data for one VAD frame");
            continue;
        }

        size_t offset = 0;
        while (offset + VAD_FRAME_SIZE <= samples_read) {
            int rv = WebRtcVad_Process(handle, I2S_SAMPLE_RATE, samples + offset, VAD_FRAME_SIZE);
            if (rv == 1) {
                ESP_LOGI(TAG, "Voice detected");
            }
            offset += VAD_FRAME_SIZE;
        }
    }
}

void
app_main()
{
    ESP_LOGI(TAG, "ESP32 Mic VAD Example Start");
    mic_init();
    mic_loop();
}
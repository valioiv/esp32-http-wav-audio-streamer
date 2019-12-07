/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "esp_log.h"
#include "esp_camera.h"
#include "app_audio.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s.h"

typedef struct {
    QueueHandle_t *queue;
    int item_size; //in bytes
} src_cfg_t;

static const char *TAG = "app_audio";
static src_cfg_t srcif;
static int16_t *buffer = NULL;
static QueueHandle_t sndQueue;

static void i2s_init(void)
{
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,//the mode must be set according to DSP configuration
        .sample_rate = 16000,                           //must be the same as DSP configuration
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,   //must be the same as DSP configuration
        .bits_per_sample = 32,                          //must be the same as DSP configuration
        .communication_format = I2S_COMM_FORMAT_I2S,
        .dma_buf_count = 3,
        .dma_buf_len = 300,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = 26,  // IIS_SCLK
        .ws_io_num = 32,   // IIS_LCLK
        .data_out_num = -1,// IIS_DSIN
        .data_in_num = 33  // IIS_DOUT
    };
    i2s_driver_install(1, &i2s_config, 0, NULL);
    i2s_set_pin(1, &pin_config);
    i2s_zero_dma_buffer(1);
}

static void recsrcTask(void *arg)
{
    i2s_init();

    src_cfg_t *cfg=(src_cfg_t*)arg;
    size_t samp_len = cfg->item_size*2*sizeof(int)/sizeof(int16_t);

    int *samp=malloc(samp_len);

    size_t read_len = 0;

    while(1) {
        i2s_read(1, samp, samp_len, &read_len, portMAX_DELAY);
        for (int x=0; x<cfg->item_size/4; x++) {
            int s1 = ((samp[x * 4] + samp[x * 4 + 1]) >> 13) & 0x0000FFFF;
            int s2 = ((samp[x * 4 + 2] + samp[x * 4 + 3]) << 3) & 0xFFFF0000;
            samp[x] = s1 | s2;
        }

        xQueueSend(*cfg->queue, samp, portMAX_DELAY);
        ESP_LOGI(TAG, "Audio update done");
    }

    free(buffer);
    vTaskDelete(NULL);
}

void* app_audio_get_input()
{
    xQueueReceive(sndQueue, buffer, portMAX_DELAY);
    return ((void*)buffer);
}

void app_audio_main ()
{
    int audio_chunksize=1024;

    //Initialize sound source
    sndQueue=xQueueCreate(2, (audio_chunksize*sizeof(int16_t)));
    srcif.queue=&sndQueue;
    srcif.item_size=audio_chunksize*sizeof(int16_t);
    
    buffer=malloc(audio_chunksize*sizeof(int16_t));
    assert(buffer);
    
    ESP_LOGI(TAG, "Audio queue and buffer sucessfully created");

    xTaskCreatePinnedToCore(&recsrcTask, "rec", 3*1024, (void*)&srcif, 5, NULL, 1);
}

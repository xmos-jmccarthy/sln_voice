// Copyright (c) 2022 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

/* System headers */
#include <platform.h>
#include <xs1.h>
#include <xcore/channel.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "queue.h"
#include "event_groups.h"

/* Library headers */
#include "rtos_printf.h"

/* App headers */
#include "app_conf.h"
#include "platform/platform_init.h"
#include "platform/driver_instances.h"
#include "audio_pipeline/audio_pipeline.h"
#include "inference_engine.h"
#include "fs_support.h"
#include "gpio_ctrl/gpi_ctrl.h"
#include "gpio_ctrl/leds.h"
#include "rtos_swmem.h"
#include "xcore_device_memory.h"
#include "ssd1306_rtos_support.h"
#include "intent_handler/intent_handler.h"
#include "xcore_clock_control.h"
extern void startup_task(void *arg);
extern void tile_common_init(chanend_t c);

//void uart_write(char data) {} //API for Wanson's Debug

static uint32_t in_last = 0;
static uint32_t out_last = 0;
__attribute__((weak))
void audio_pipeline_input(void *input_app_data,
                          int32_t **input_audio_frames,
                          size_t ch_count,
                          size_t frame_count)
{
    (void) input_app_data;

    static int flushed;
    while (!flushed) {
        size_t received;
        received = rtos_mic_array_rx(mic_array_ctx,
                                     input_audio_frames,
                                     frame_count,
                                     0);
        if (received == 0) {
            rtos_mic_array_rx(mic_array_ctx,
                              input_audio_frames,
                              frame_count,
                              portMAX_DELAY);
            flushed = 1;
        }
    }


    rtos_mic_array_rx(mic_array_ctx,
                      input_audio_frames,
                      frame_count,
                      portMAX_DELAY);



    // rtos_printf("in times diff:%d\n", get_reference_time() - in_last);
    // in_last = get_reference_time();

}

__attribute__((weak))
int audio_pipeline_output(void *output_app_data,
                          int32_t **output_audio_frames,
                          size_t ch_count,
                          size_t frame_count)
{
    (void) output_app_data;

#if appconfINFERENCE_ENABLED
    inference_engine_sample_push((int32_t *)output_audio_frames, frame_count);
#endif

    // rtos_printf("out times diff:%d\n", get_reference_time() - out_last);
    // out_last = get_reference_time();

    return AUDIO_PIPELINE_FREE_FRAME;
}

void vApplicationMallocFailedHook(void)
{
    rtos_printf("Malloc Failed on tile %d!\n", THIS_XCORE_TILE);
    xassert(0);
    for(;;);
}

__attribute__((weak))
void startup_task(void *arg)
{
    rtos_printf("Startup task running from tile %d on core %d\n", THIS_XCORE_TILE, portGET_CORE_ID());

    platform_start();

#if ON_TILE(1)
    gpio_gpi_init(gpio_ctx_t0);
#endif

#if ON_TILE(FS_TILE_NO)
    rtos_fatfs_init(qspi_flash_ctx);
#endif

#if appconfINFERENCE_ENABLED && ON_TILE(INFERENCE_TILE_NO)
#if appconfSSD1306_DISPLAY_ENABLED
    ssd1306_display_create(appconfSSD1306_TASK_PRIORITY);
#endif
    QueueHandle_t q_intent = xQueueCreate(appconfINTENT_QUEUE_LEN, sizeof(int32_t));
    intent_handler_create(appconfINFERENCE_MODEL_RUNNER_TASK_PRIORITY, q_intent);
    inference_engine_create(appconfINFERENCE_MODEL_RUNNER_TASK_PRIORITY, q_intent);
#endif

#if ON_TILE(AUDIO_PIPELINE_TILE_NO)
#if appconfINFERENCE_ENABLED
    // Wait until the Wanson engine is initialized before we start the
    // audio pipeline.
    {
        int ret = 0;
        rtos_intertile_rx_len(intertile_ctx, appconfWANSON_READY_SYNC_PORT, RTOS_OSAL_WAIT_FOREVER);
        rtos_intertile_rx_data(intertile_ctx, &ret, sizeof(ret));
    }
#endif
    audio_pipeline_init(NULL, NULL);
#endif

#if ON_TILE(1)
    led_heartbeat_create(appconfLED_HEARTBEAT_TASK_PRIORITY, NULL);
#endif
#if ON_TILE(0)
// vTaskDelay(pdMS_TO_TICKS(2000));    /* some time before we kill this tile for other things to get set up */
    // set_tile_processor_clk_div(get_local_tile_id(), 600);
    rtos_printf("tile[%d] clock rate %d\n", THIS_XCORE_TILE, get_local_tile_processor_clock());
    // rtos_printf("ref is %d\n", get_local_ref_clock());
#endif

#if ON_TILE(1)
    // set_tile_processor_clk_div(get_local_tile_id(), 2); // 600/2 = 300
    // set_tile_processor_clk_div(get_local_tile_id(), 3); // 600/3 = 200 // lowest i2s functions
    // set_tile_processor_clk_div(get_local_tile_id(), 4); // 600/4 = 150
    set_tile_processor_clk_div(get_local_tile_id(), 5); // 600/5 = 120
    // set_tile_processor_clk_div(get_local_tile_id(), 6); // 600/6 = 100
    // set_tile_processor_clk_div(get_local_tile_id(), 12); // 600/12 = 50
    // set_tile_processor_clk_div(get_local_tile_id(), 24); // 600/24 = 25
    // set_tile_processor_clk_div(get_local_tile_id(), 600); // 600/600 = 1
    rtos_printf("tile[%d] clock rate %d\n", THIS_XCORE_TILE, get_local_tile_processor_clock());
#endif

    vTaskSuspend(NULL);
    while(1){;} /* Trap */
}

void vApplicationMinimalIdleHook(void)
{
    rtos_printf("idle hook on tile %d core %d\n", THIS_XCORE_TILE, rtos_core_id_get());
    asm volatile("waiteu");
}

__attribute__((weak))
void tile_common_init(chanend_t c)
{
    platform_init(c);
    chanend_free(c);

    enable_local_tile_processor_clock_divider();
    set_tile_processor_clk_div(get_local_tile_id(), 1);

    xTaskCreate((TaskFunction_t) startup_task,
                "startup_task",
                RTOS_THREAD_STACK_SIZE(startup_task),
                NULL,
                appconfSTARTUP_TASK_PRIORITY,
                NULL);

    rtos_printf("start scheduler on tile %d\n", THIS_XCORE_TILE);
    vTaskStartScheduler();
}

#if ON_TILE(0)
void main_tile0(chanend_t c0, chanend_t c1, chanend_t c2, chanend_t c3)
{
    (void) c0;
    (void) c2;
    (void) c3;

    tile_common_init(c1);
}
#endif

#if ON_TILE(1)
void main_tile1(chanend_t c0, chanend_t c1, chanend_t c2, chanend_t c3)
{
    (void) c1;
    (void) c2;
    (void) c3;

    tile_common_init(c0);
}
#endif

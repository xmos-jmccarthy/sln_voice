// Copyright 2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

#include "usb_descriptors.h"
#include "tusb.h"
#include "app_conf.h"
#include "usb_video.h"

#if appconfUSB_VIDEO_DISPLAY_ENABLED
//--------------------------------------------------------------------+
// USB Video
//--------------------------------------------------------------------+
static unsigned interval_ms = 1000 / FRAME_RATE;

/* YUY2 frame buffer */
// #include "images.h"
static uint8_t frame_buffer[FRAME_WIDTH * FRAME_HEIGHT * 16 / 8] = {0};

static TaskHandle_t video_out_task_handle;


// YUYV format
const uint8_t yuy2_blk_blk[4] = {  16, 128,  16, 128};
const uint8_t yuy2_blk_wht[4] = {  16, 128, 235, 128};
const uint8_t yuy2_wht_wht[4] = { 235, 128, 235, 128};
const uint8_t yuy2_wht_blk[4] = { 235, 128,  16, 128};

#include "ssd1306_rtos_support.h"

/* Draw on main buffer at pixel row y and column x+ndx */
#define LOCATION_BASE (frame_buffer + (FRAME_WIDTH * 2 * y) + ((x * 8 + ndx) * 2))

void video_class_write(uint8_t *bitmap)
{
    for(int y=0; y<MAX_ROWS; y++) {
        for(int x=0; x<MAX_COLS; x++) {
            uint8_t tmpchar = *(bitmap + (MAX_COLS * y) + x);
            for(int ndx=6; ndx>=0; ndx-=2) {
                uint8_t two_pixels = (tmpchar>>ndx) & 0x03;
                switch (two_pixels) {
                    default:
                    case 0x03:
                        memcpy(LOCATION_BASE, yuy2_blk_blk, 4);
                        break;
                    case 0x02:
                        memcpy(LOCATION_BASE, yuy2_blk_wht, 4);
                        break;
                    case 0x01:
                        memcpy(LOCATION_BASE, yuy2_wht_blk, 4);
                        // memcpy(LOCATION_BASE, yuy2_blk_wht, 4);
                        break;
                    case 0x00:
                        memcpy(LOCATION_BASE, yuy2_wht_wht, 4);
                        break;
                }
            }
        }
    }
}

void video_task(void)
{
    if (!tud_video_n_streaming(0, 0)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }

    tud_video_n_frame_xfer(0, 0,
        (void*)(uintptr_t) frame_buffer,
        FRAME_WIDTH * FRAME_HEIGHT * 16/8);

    (void) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
}

void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
    (void)ctl_idx; (void)stm_idx;
    xTaskNotifyGive(video_out_task_handle);
}

int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
			video_probe_and_commit_control_t const *parameters)
{
    (void)ctl_idx; (void)stm_idx;
    /* convert unit to ms from 100 ns */
    interval_ms = parameters->dwFrameInterval / 10000;
    return VIDEO_ERROR_NONE;
}

static void video_task_wrapper(void *arg) {
    while(1) {
        video_task();
    }
}

void usb_video_init(unsigned priority)
{
    xTaskCreate((TaskFunction_t) video_task_wrapper,
            "video_task",
            portTASK_STACK_DEPTH(video_task_wrapper),
            NULL,
            priority,
            &video_out_task_handle);
}

#endif /* appconfUSB_VIDEO_DISPLAY_ENABLED */

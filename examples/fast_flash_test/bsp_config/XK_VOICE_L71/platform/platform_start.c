// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

/* System headers */
#include <platform.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"

/* Library headers */

/* App headers */
#include "platform_conf.h"
#include "platform/driver_instances.h"

static void flash_start(void)
{
    rtos_qspi_flash_start(qspi_flash_ctx, appconfQSPI_FLASH_TASK_PRIORITY);
}

void platform_start(void)
{
    flash_start();
}

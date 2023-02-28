// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#ifndef DRIVER_INSTANCES_H_
#define DRIVER_INSTANCES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "rtos_qspi_flash.h"

#ifdef __cplusplus
};
#endif

/* Tile specifiers */

/** TILE 0 Clock Blocks */
#define FLASH_CLKBLK  XS1_CLKBLK_1

/** TILE 1 Clock Blocks */

/* Port definitions */
#define PORT_SQI_CS         PORT_SQI_CS_0
#define PORT_SQI_SCLK       PORT_SQI_SCLK_0
#define PORT_SQI_SIO        PORT_SQI_SIO_0

extern rtos_qspi_flash_t *qspi_flash_ctx;

#endif /* DRIVER_INSTANCES_H_ */

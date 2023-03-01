// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#ifndef QSPI_FAST_FLASH_READ_H_
#define QSPI_FAST_FLASH_READ_H_

/* Allow user to override pattern header */
#if defined(QSPI_FLASH_FAST_READ_USER_CONFIG_FILE)
/*
 * User provided header must include:
 * - QSPI_FLASH_FAST_READ_PATTERN_ADDRESS
 *      starting flash address of expected pattern
 * - QSPI_FLASH_FAST_READ_PATTERN_WORDS
 *      number of words in the calibration pattern
 * - qspi_flash_fast_read_pattern
 *      a static array of QSPI_FLASH_FAST_READ_PATTERN_WORDS
 *      words containing the calibration pattern
 */
#include QSPI_FLASH_FAST_READ_USER_CONFIG_FILE
#else
#include "qspi_flash_fast_read_default_pattern.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	qspi_fast_flash_read_transfer_raw = 0, /**< Do not bit reverse port in */
	qspi_fast_flash_read_transfer_bitrev   /**< Bitreverse port ins */
} qspi_fast_flash_read_transfer_mode_t;

typedef struct {
	xclock_t clock_block;
	port_t cs_port;
	port_t sclk_port;
	port_t sio_port;
    qspi_fast_flash_read_transfer_mode_t mode;
    unsigned char divide;
} qspi_fast_flash_read_ctx_t;

// Define the clock source divide - 600MHz/800MHz core clock divided by (2*CLK_DIVIDE)
// CORE CLOCK   600MHz    800MHz
// CLK_DIVIDE      SPI_CLK
// 3            100MHz    133MHz
// 4            75MHz     100MHz
// 5            60MHz     80MHz
// 6            50MHz     66MHz

void qspi_flash_fast_read_init(
    qspi_fast_flash_read_ctx_t *ctx,
	xclock_t clock_block,
	port_t cs_port,
	port_t sclk_port,
	port_t sio_port,
    qspi_fast_flash_read_transfer_mode_t mode,
    unsigned char divide);

void qspi_flash_fast_read_setup_resources(
    qspi_fast_flash_read_ctx_t *ctx);

void qspi_flash_fast_read_shutdown(
    qspi_fast_flash_read_ctx_t *ctx);

#ifdef __cplusplus
};
#endif

#endif /* QSPI_FAST_FLASH_READ_H_ */


// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include <xs1.h>
#include <xcore/clock.h>
#include <xcore/port.h>
#include <xcore/assert.h>

#include "qspi_flash_fast_read.h"

// Defines for SETPADCTRL

#define DR_STR_2mA     0
#define DR_STR_4mA     1
#define DR_STR_8mA     2
#define DR_STR_12mA    3

#define PORT_PAD_CTL_SMT    0           // Schmitt off
#define PORT_PAD_CTL_SR     1           // Fast slew
#define PORT_PAD_CTL_DR_STR DR_STR_8mA  // 8mA drive
#define PORT_PAD_CTL_REN    1           // Receiver enabled
#define PORT_PAD_CTL_MODE   0x0006

#define PORT_PAD_CTL ((PORT_PAD_CTL_SMT     << 23) | \
                      (PORT_PAD_CTL_SR      << 22) | \
                      (PORT_PAD_CTL_DR_STR  << 20) | \
                      (PORT_PAD_CTL_REN     << 17) | \
                      (PORT_PAD_CTL_MODE    << 0))
                      
// #define HIGH_DRIVE_8MA


void qspi_flash_fast_read_setup_resources(
    qspi_fast_flash_read_ctx_t *ctx)
{
    clock_enable(ctx->clock_block);
    port_enable(ctx->sclk_port);
    port_enable(ctx->cs_port);
    port_enable(ctx->sio_port);

    clock_set_source_clk_xcore(ctx->clock_block);
    clock_set_divide(ctx->clock_block, ctx->divide);
    port_set_clock(ctx->sclk_port, ctx->clock_block);
    port_set_out_clock(ctx->sclk_port);

    /* Port initial setup */
    port_out(ctx->cs_port, 1);
    port_set_clock(ctx->sio_port, XS1_CLKBLK_REF);
    port_out(ctx->sio_port, 0xF);
	port_sync(ctx->sio_port);
    port_set_clock(ctx->sio_port, ctx->clock_block);
    port_set_buffered(ctx->sio_port);
    port_set_transfer_width(ctx->sio_port, 32);

    // Set the drive strength to 8mA on all ports.
    // This is optional. If used it may change timing tuning so must tune with the settings we are using.
    // This will reduce rise time uncertainty and ensure ports will fully switch at 100MHz rate over PVT. (Remember our corner silicon was only skewed for core transistors not IO).
    // Setting all ports to the same drive to ensure we don't introduce any extra skew between outputs.
    // Downside is extra EMI.
#ifdef HIGH_DRIVE_8MA
    asm volatile ("setc res[%0], %1" :: "r" (ctx->sclk_port), "r" (PORT_PAD_CTL));
    asm volatile ("setc res[%0], %1" :: "r" (ctx->sio_port), "r" (PORT_PAD_CTL));
    asm volatile ("setc res[%0], %1" :: "r" (ctx->cs_port), "r" (PORT_PAD_CTL));
#endif
}

void qspi_flash_fast_read_init(
    qspi_fast_flash_read_ctx_t *ctx,
	xclock_t clock_block,
	port_t cs_port,
	port_t sclk_port,
	port_t sio_port,
    qspi_fast_flash_read_transfer_mode_t mode,
    unsigned char divide)
{
    ctx->clock_block = clock_block;
    ctx->cs_port = cs_port;
    ctx->sclk_port = sclk_port;
    ctx->sio_port = sio_port;
    ctx->mode = mode;
    ctx->divide = divide;

    xassert(divide >= 3); /* Clock divider must be greater than 2 */
}

void qspi_flash_fast_read_shutdown(
    qspi_fast_flash_read_ctx_t *ctx)
{
    clock_stop(ctx->clock_block);
    clock_disable(ctx->clock_block);
    port_reset(ctx->sclk_port);
    port_disable(ctx->sclk_port);
    port_reset(ctx->cs_port);
    port_disable(ctx->cs_port);
    port_reset(ctx->sio_port);
    port_disable(ctx->sio_port);
}
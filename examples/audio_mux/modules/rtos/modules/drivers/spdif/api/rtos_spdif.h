// Copyright 2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#ifndef RTOS_SPDIF_H_
#define RTOS_SPDIF_H_

/**
 * \addtogroup rtos_spdif_driver rtos_spdif_driver
 *
 * The public API for using the RTOS spdif driver.
 * @{
 */

#include <xcore/channel.h>
#include <xcore/clock.h>
#include <xcore/port.h>
#include "spdif.h"

#include "rtos_osal.h"
#include "rtos_driver_rpc.h"

/**
 * Typedef to the RTOS spdif driver instance struct.
 */
typedef struct rtos_spdif_struct rtos_spdif_t;

/**
 * Struct representing an RTOS spdif driver instance.
 *
 * The members in this struct should not be accessed directly.
 */
struct rtos_spdif_struct {
    rtos_driver_rpc_t *rpc_config;

    __attribute__((fptrgroup("rtos_spdif_rx_fptr_grp")))
    size_t (*rx)(rtos_spdif_t *, int32_t **sample_buf, size_t, unsigned);

    streaming_channel_t c_spdif;
    port_t p_spdif;
    xclock_t clk_blk_spdif;
    unsigned freq;

    rtos_osal_thread_t hil_thread;
    rtos_osal_semaphore_t recv_sem;
    int recv_blocked;

    struct {
        int32_t *buf;
        size_t buf_size;
        size_t write_index;
        size_t read_index;
        volatile size_t total_written;
        volatile size_t total_read;
        volatile size_t required_available_count;
    } recv_buffer;
};

#include "rtos_spdif_rpc.h"

/**
 * \addtogroup rtos_spdif_driver_core rtos_spdif_driver_core
 *
 * The core functions for using an RTOS spdif driver instance after
 * it has been initialized and started. These functions may be used
 * by both the host and any client tiles that RPC has been enabled for.
 * @{
 */

/**
 * Receives sample frames from the PDM spdif interface.
 *
 * This function will block until new frames are available.
 *
 * \param ctx            A pointer to the spdif driver instance to use.
 * \param sample_buf     A buffer to copy the received sample frames into.
 * \param frame_count    The number of frames to receive from the buffer.
 *                       This must be less than or equal to the size of the
 *                       buffer specified to rtos_spdif_start() if in
 *                       RTOS_SPDIF_SAMPLE_CHANNEL mode.  This must be equal
 *                       to SPDIF_CONFIG_SAMPLES_PER_FRAME if in
 *                       RTOS_SPDIF_CHANNEL_SAMPLE mode.
 * \param timeout        The amount of time to wait before the requested number
 *                       of frames becomes available.
 *
 * \returns              The number of frames actually received into \p sample_buf.
 */
inline size_t rtos_spdif_rx(
        rtos_spdif_t *ctx,
        int32_t **sample_buf,
        size_t frame_count,
        unsigned timeout)
{
    return ctx->rx(ctx, sample_buf, frame_count, timeout);
}

/**@}*/

/**
 * Starts an RTOS spdif driver instance. This must only be called by the tile that
 * owns the driver instance. It must be called after starting the RTOS from an RTOS thread,
 * and must be called before any of the core spdif driver functions are called with this
 * instance.
 *
 * rtos_spdif_init() must be called on this spdif driver instance prior to calling this.
 *
 * \param spdif_ctx         A pointer to the spdif driver instance to start.
 * \param buffer_size           The size in frames of the input buffer. Each frame is two samples
 *                              (one for each microphone) plus one sample per reference channel.
 *                              This must be at least SPDIF_CONFIG_SAMPLES_PER_FRAME. Samples are pulled out
 *                              of this buffer by the application by calling rtos_spdif_rx().
 * \param interrupt_core_id     The ID of the core on which to enable the spdif interrupt.
 */
void rtos_spdif_start(
        rtos_spdif_t *spdif_ctx,
        size_t buffer_size,
        unsigned interrupt_core_id);

/**
 * Initializes an RTOS spdif driver instance.
 * This must only be called by the tile that owns the driver instance. It should be
 * called before starting the RTOS, and must be called before calling rtos_spdif_start()
 * or any of the core spdif driver functions with this instance.
 *
 * \param spdif_ctx     A pointer to the spdif driver instance to initialize.
 * \param io_core_mask      A bitmask representing the cores on which the low level spdif
 *                          I/O thread created by the driver is allowed to run. Bit 0 is core 0,
 *                          bit 1 is core 1, etc.
 * \param format            Format of the output data
 */
void rtos_spdif_init(
        rtos_spdif_t *spdif_ctx,
        uint32_t io_core_mask,
        port_t p_spdif,
        xclock_t clk_blk,
        uint32_t sample_freq_est);

/**@}*/

#endif /* RTOS_SPDIF_H_ */

// Copyright 2022 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#define DEBUG_UNIT RTOS_SPDIF

#include <string.h>

#include <xcore/assert.h>
#include <xcore/triggerable.h>

#include "rtos_interrupt.h"
#include "rtos_spdif.h"

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define CLRSR(c) asm volatile("clrsr %0" : : "n"(c));

void spdif_rx(chanend_t c, port_t p_spdif, xclock_t clk, unsigned sample_freq_estimate);

static void spdif_thread(rtos_spdif_t *ctx)
{
    (void) s_chan_in_byte(ctx->c_spdif.end_a);

    rtos_printf("SPDIF on tile %d core %d\n", THIS_XCORE_TILE, rtos_core_id_get());

    rtos_interrupt_mask_all();

    /*
     * ma_basic_task() itself uses interrupts, and does re-enable them. However,
     * it appears to assume that KEDI is not set, therefore it is cleared here.
     */
    CLRSR(XS1_SR_KEDI_MASK);

    spdif_rx(ctx->c_spdif.end_a, ctx->p_spdif, ctx->clk_blk_spdif, ctx->freq);
}

DEFINE_RTOS_INTERRUPT_CALLBACK(rtos_spdif_isr, arg)
{
    rtos_spdif_t *ctx = arg;
    size_t words_remaining = 1;
    size_t words_available = ctx->recv_buffer.total_written - ctx->recv_buffer.total_read;
    size_t words_free = ctx->recv_buffer.buf_size - words_available;

    int32_t sample = 0;
    size_t index = 0;

    sample = s_chan_in_word(ctx->c_spdif.end_a);
    index = (sample & 0xF) == SPDIF_FRAME_Y ? 1 : 0;
    sample = (sample & ~0xF) << 4;

    if (words_remaining <= words_free) {
        ctx->recv_buffer.buf[ctx->recv_buffer.write_index] = sample;
        ctx->recv_buffer.write_index += 1;

        if (ctx->recv_buffer.write_index >= ctx->recv_buffer.buf_size) {
            ctx->recv_buffer.write_index = 0;
        }

        RTOS_MEMORY_BARRIER();
        ctx->recv_buffer.total_written += 1;
    } else {
        rtos_printf("spdif rx overrun\n");
    }

    if (ctx->recv_buffer.required_available_count > 0) {
        words_available = ctx->recv_buffer.total_written - ctx->recv_buffer.total_read;

        if (words_available >= ctx->recv_buffer.required_available_count) {
            ctx->recv_buffer.required_available_count = 0;
            rtos_osal_semaphore_put(&ctx->recv_sem);
        }
    }
}

__attribute__((fptrgroup("rtos_spdif_rx_fptr_grp")))
static size_t spdif_local_rx(
        rtos_spdif_t *ctx,
        int32_t **sample_buf,
        size_t frame_count,
        unsigned timeout)
{
    size_t frames_recvd = 0;
    size_t words_remaining = frame_count;
    int32_t *sample_buf_ptr = (int32_t *) sample_buf;

    xassert(words_remaining <= ctx->recv_buffer.buf_size);
    if (words_remaining > ctx->recv_buffer.buf_size) {
        return frames_recvd;
    }

    if (!ctx->recv_blocked) {
        size_t words_available = ctx->recv_buffer.total_written - ctx->recv_buffer.total_read;
        if (words_remaining > words_available) {
            ctx->recv_buffer.required_available_count = words_remaining;
            ctx->recv_blocked = 1;
        }
    }

    if (ctx->recv_blocked) {
        if (rtos_osal_semaphore_get(&ctx->recv_sem, timeout) == RTOS_OSAL_SUCCESS) {
            ctx->recv_blocked = 0;
        }
    }

    if (!ctx->recv_blocked) {
        while (words_remaining) {
            size_t words_to_copy = MIN(words_remaining, ctx->recv_buffer.buf_size - ctx->recv_buffer.read_index);
            memcpy(sample_buf_ptr, &ctx->recv_buffer.buf[ctx->recv_buffer.read_index], words_to_copy * sizeof(int32_t));
            ctx->recv_buffer.read_index += words_to_copy;

            sample_buf_ptr += words_to_copy;
            words_remaining -= words_to_copy;

            if (ctx->recv_buffer.read_index >= ctx->recv_buffer.buf_size) {
                ctx->recv_buffer.read_index = 0;
            }
        }

        RTOS_MEMORY_BARRIER();
        ctx->recv_buffer.total_read += frame_count;

        frames_recvd = frame_count;
    }

    return frames_recvd;
}

void rtos_spdif_start(
        rtos_spdif_t *spdif_ctx,
        size_t buffer_size,
        unsigned interrupt_core_id)
{
    uint32_t core_exclude_map;

    memset(&spdif_ctx->recv_buffer, 0, sizeof(spdif_ctx->recv_buffer));
    spdif_ctx->recv_buffer.buf_size = buffer_size;
    spdif_ctx->recv_buffer.buf = rtos_osal_malloc(spdif_ctx->recv_buffer.buf_size * sizeof(int32_t));
    rtos_osal_semaphore_create(&spdif_ctx->recv_sem, "spdif_recv_sem", 1, 0);

    /* Ensure that the mic array interrupt is enabled on the requested core */
    rtos_osal_thread_core_exclusion_get(NULL, &core_exclude_map);
    rtos_osal_thread_core_exclusion_set(NULL, ~(1 << interrupt_core_id));

    triggerable_enable_trigger(spdif_ctx->c_spdif.end_b);

    /* Tells the task running the decimator to start */
    s_chan_out_byte(spdif_ctx->c_spdif.end_b, 0);

    /* Restore the core exclusion map for the calling thread */
    rtos_osal_thread_core_exclusion_set(NULL, core_exclude_map);

    if (spdif_ctx->rpc_config != NULL && spdif_ctx->rpc_config->rpc_host_start != NULL) {
        spdif_ctx->rpc_config->rpc_host_start(spdif_ctx->rpc_config);
    }
}

void rtos_spdif_init(
        rtos_spdif_t *spdif_ctx,
        uint32_t io_core_mask,
        port_t p_spdif,
        xclock_t clk_blk,
        uint32_t sample_freq_est)
{
    spdif_ctx->c_spdif = s_chan_alloc();
    spdif_ctx->p_spdif = p_spdif;
    spdif_ctx->clk_blk_spdif = clk_blk;
    spdif_ctx->freq = sample_freq_est;
    spdif_ctx->rpc_config = NULL;
    spdif_ctx->rx = spdif_local_rx;

    triggerable_setup_interrupt_callback(spdif_ctx->c_spdif.end_b, spdif_ctx, RTOS_INTERRUPT_CALLBACK(rtos_spdif_isr));

    rtos_osal_thread_create(
            &spdif_ctx->hil_thread,
            "spdif_thread",
            (rtos_osal_entry_function_t) spdif_thread,
            spdif_ctx,
            RTOS_THREAD_STACK_SIZE(spdif_thread),
            RTOS_OSAL_HIGHEST_PRIORITY);

    /* Ensure the mic array thread is never preempted */
    rtos_osal_thread_preemption_disable(&spdif_ctx->hil_thread);
    /* And ensure it only runs on one of the specified cores */
    rtos_osal_thread_core_exclusion_set(&spdif_ctx->hil_thread, ~io_core_mask);
}

#undef MIN

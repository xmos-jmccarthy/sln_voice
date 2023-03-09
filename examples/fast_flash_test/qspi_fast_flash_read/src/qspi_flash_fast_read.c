// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcore/assert.h>

#include "qspi_flash_fast_read.h"
#include "qspi_flash_fast_read_priv.h"

#ifndef READ_ADJUST_MIN_START_CYCLE
/* This is the min clock cycles for valid data to be read
 * 8 clocks for instruction (0xEB)
 * 8 clocks for output addr and mode (currently implicitly 0)
 * @18 port turns around to begin shift register fill
 * 8 clocks for 32bit shift register to be filled 4 bits/clk
 * Meaning on clock 27 would be the first cycle the full
 * transfer is available in the transfer register is available
 * Data will be overwritten in clock 27+8
 * 
 * Allow to be user overridden for advanced users
 */
#define READ_ADJUST_MIN_START_CYCLE     27
#endif

#define MAX_CLK_DIVIDE 6

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

    QSPI_FF_SETC(ctx->sclk_port, QSPI_FF_PORT_PAD_CTL);
    QSPI_FF_SETC(ctx->sio_port, QSPI_FF_PORT_PAD_CTL);
    QSPI_FF_SETC(ctx->cs_port, QSPI_FF_PORT_PAD_CTL);
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
    memset(ctx, 0x00, sizeof(qspi_fast_flash_read_ctx_t));
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
    clock_disable(ctx->clock_block);
    port_disable(ctx->sclk_port);
    port_disable(ctx->cs_port);
    port_disable(ctx->sio_port);
}

void qspi_flash_fast_read_apply_best_setting(
    qspi_fast_flash_read_ctx_t *ctx,
    char best_setting)
{
    printf("Best settings: read adj %d, sdelay = %d, pad_delay = %d\n", (best_setting & 0x06) >> 1, (best_setting & 0x01), (best_setting & 0x38) >> 3);

    int sdelay = (best_setting & 0x01);
    int pad_delay = (best_setting & 0x38) >> 3;
    int read_adj  = (best_setting & 0x06) >> 1;
    ctx->read_start_pt = READ_ADJUST_MIN_START_CYCLE + read_adj;
    
    if (sdelay == 1) {
        port_set_sample_falling_edge(ctx->sio_port);
    } else {
        port_set_sample_rising_edge(ctx->sio_port);
    }
    QSPI_FF_SETC(ctx->sio_port, QSPI_FF_SETC_PAD_DELAY(pad_delay));
}

int qspi_flash_fast_read_calibrate(
    qspi_fast_flash_read_ctx_t *ctx)
{
    int retval = 0;
    unsigned read_data_tmp[QSPI_FLASH_FAST_READ_PATTERN_WORDS];

    int pass_start = -1;
    int pass_count = 0;
    int passing_words = 0;
    char time_index = 0;
    /* Save mode to restore.  Calibration is always done raw */
    qspi_fast_flash_read_transfer_mode_t start_mode = ctx->mode;
    ctx->mode = qspi_fast_flash_read_transfer_raw;

    xassert(ctx->divide <= MAX_CLK_DIVIDE);
    // Bit 0 sdelay, bits 1-2 read adj, bits 3-5 pad delay
    // the index into the array becomes the nominal time.
    char results[6 * (MAX_CLK_DIVIDE)];
        
    // This loops over the settings in such a way that the result is sequentially increasing in time in units of core clocks.
    for(int read_adj = 0; read_adj < 3; read_adj++) // Data read port time adjust
    {
        ctx->read_start_pt = READ_ADJUST_MIN_START_CYCLE + read_adj;
        for(int sdelay = 0; sdelay < 2; sdelay++) // Sample delays
        {
            if (sdelay == 1) {
                port_set_sample_falling_edge(ctx->sio_port);
            } else {
                port_set_sample_rising_edge(ctx->sio_port);
            }
            for(int pad_delay = (ctx->divide - 1); pad_delay >= 0; pad_delay--) // Pad delays (only loop over useful pad delays)
            {
                // Set input pad delay in units of core clocks
                QSPI_FF_SETC(ctx->sio_port, QSPI_FF_SETC_PAD_DELAY(pad_delay));

                // Read the data with the current settings
                qspi_flash_fast_read(ctx, 0x100000, (uint8_t*)&read_data_tmp[0], QSPI_FLASH_FAST_READ_PATTERN_WORDS * sizeof(uint32_t));
                
                // Check if the data is correct
                passing_words = 0;
                for (int m = 0; m < QSPI_FLASH_FAST_READ_PATTERN_WORDS; m++)
                {
                    if (read_data_tmp[m] == qspi_flash_fast_read_pattern[m]) {
                        passing_words++;
                    }
                }

                // Store the settings
                if (passing_words == QSPI_FLASH_FAST_READ_PATTERN_WORDS) {
                    results[time_index] = sdelay | (read_adj << 1) | (pad_delay << 3);
                    pass_count++;
                    if (pass_start == -1) {
                        pass_start = time_index;
                    }
                }
                time_index++;
            }
        }
    }

    if (pass_count >= 5) {
        char best_setting = pass_start + (pass_count >> 1); // Pick the middle setting
        qspi_flash_fast_read_apply_best_setting(ctx, results[best_setting]);
    } else {
        // printf("Failed to find valid settings.  Pass count: %d\n", pass_count);
        retval = -1;
    }
    
    /* Restore settings */
    ctx->mode = start_mode;
    
    return retval;
}

void qspi_flash_fast_read(
    qspi_fast_flash_read_ctx_t *ctx,
	unsigned int addr,
	unsigned char *buf,
	size_t len)
{
    uint32_t *read_data = (uint32_t *)buf;
	size_t word_len = len / sizeof(uint32_t);

    unsigned addr_mode_wr;
    int i=0;

    // We shift the address left by 8 bits to align the MSB of address with MSB of the int. We or in the mode bits.
    // Buffered ports remember always shift right.
    // So LSB first
    // We need to nibble swap the address and mode word as it needs to be output MS nibble first and ports do the opposite
    addr_mode_wr = nibble_swap(byterev((addr << 8)));
    
    // Need to set the first data output bit (MS bit of flash command) before we start the clock.
    port_out_part_word(ctx->sio_port, 0x1, 4);
    clock_start(ctx->clock_block);
    port_sync(ctx->sio_port);
    clock_stop(ctx->clock_block);
    
    port_out(ctx->cs_port, 0); // Set CS_N low
    
    // Pre load the transfer register in the port with data. This will not go out yet because clock has not been started.
    // This data needs to be shifted as when starting the clock we will clock the first bit of data to flash before this data goes out.
    // We also need to Or in the first nibble of addr_mode_wr that we want to output.
    // This is the 7 LSB of the 0xEB instruction on dat[0], dat[3:1] = 0
    port_out(ctx->sio_port, 0x01101011 | ((addr_mode_wr & 0x0000000F) << 28));

    // Start the clock block running. This starts output of data and resets the port timer to 0 on the clk port.
    clock_start(ctx->clock_block);

    port_out_part_word(ctx->sio_port, (addr_mode_wr >> 4), 28); // Immediately follow up with the remaining address and mode bits
    
    // Now we want to turn the port around at the right point.
    // At specified value of port timer we read the transfer reg word and discard, data will be junk. Exact timing of port going high-z would need simulation but it will be in the cycle specified.
    (void) port_in_at_time(ctx->sio_port, 18);
    
    // Now we need to read the transfer register at the correct port time so that the initial data from flash will be in there
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    port_set_trigger_time(ctx->sio_port, ctx->read_start_pt);
    for (i = 0; i < word_len; i++) {
        read_data[i] = port_in(ctx->sio_port);
        if (ctx->mode == qspi_fast_flash_read_transfer_nibble_swap) {
            read_data[i] = nibble_swap(read_data[i]);
        }
    }

    // Remainder bytes
    len %= 4;
    if (len > 0) {
        /* Note: This will shift in a full word but we will drop
         * unwanted bits */
        uint32_t tmp = port_in(ctx->sio_port);

		if (ctx->mode == qspi_fast_flash_read_transfer_nibble_swap) {
			tmp = nibble_swap(tmp);
		}

        buf = (uint8_t *) &read_data[i];
		for (i = 0; i < len; i++) {
			*buf++ = (uint8_t)(tmp & 0xFF);
			tmp >>= 8;
		}
    }

    // Stop the clock
    clock_stop(ctx->clock_block);
    
    // Put chip select back high
    port_out(ctx->cs_port, 1);
}

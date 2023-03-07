// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xs1.h>
#include <xclib.h>
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



/* The SETC constant for pad delay is missing from xs2a_user.h */
#define QSPI_IO_SETC_PAD_DELAY(n) (0x7007 | ((n) << 3))

/* These appear to be missing from the public API of lib_xcore */
#define QSPI_IO_RESOURCE_SETCI(res, c) asm volatile( "setc res[%0], %1" :: "r" (res), "n" (c))
#define QSPI_IO_RESOURCE_SETC(res, r) asm volatile( "setc res[%0], %1" :: "r" (res), "r" (r))


void fast_read_loop(qspi_fast_flash_read_ctx_t *ctx, unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[]);

__attribute__((always_inline))
inline uint32_t nibble_swap(uint32_t word)
{
	// uint32_t tmp;
    // asm volatile (
    //     ".issue_mode dual\n"
    //     "dualentsp 0\n"
	// 	"{and %0,%0,%2 ; and  %1,%0,%3}\n"
	// 	"{shl %0,%0,4  ; shr  %1,%1,4}\n"
    //     "entsp 0\n"
    //     ".issue_mode single\n"
	// 	: "+r"(word), "=r"(tmp)
	// 	: "r"(0x0F0F0F0F), "r"(0xF0F0F0F0)
	// );

	// return word | tmp;
    return ((word & 0x0F0F0F0F) << 4) | ((word & 0xF0F0F0F0) >> 4);
}


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

#define MAX_CLK_DIVIDE 5

void find_best_setting(qspi_fast_flash_read_ctx_t *ctx,
    char results[6*MAX_CLK_DIVIDE])
{
    char pass_start = 0;
    char pass_count = 0;
    char read_adj, sdelay, pad_delay, pass;
    
    // Loop over the results looking for the window of settings where we pass.
    for(int time_index = 0; time_index < (6*ctx->divide); time_index++) // Loop over time
    {
        // extract individual settings/result from the settings/results array.
        read_adj  = (results[time_index] & 0x06) >> 1;
        sdelay    = (results[time_index] & 0x01);
        pad_delay = (results[time_index] & 0x38) >> 3;
        pass      = (results[time_index] & 0x80) >> 7;
        
        printf("time_index %d, read adj %d, sdelay %d, pad_delay %d :  ",time_index,read_adj,sdelay,pad_delay);
        if (pass == 1) // PASS
        {
            printf("PASS\n");
            if (pass_count == 0) // This is first PASS we've seen
            {
                pass_start = time_index; // Record the setting index
            }
            pass_count++;
        }
        else
        {
            printf("FAIL\n");
        }
    }
    
    char best_setting = pass_start + (pass_count >> 1); // Pick the middle setting
    
    printf("pass_start %d, pass_count %d, best_setting %d\n",pass_start,pass_count,best_setting);
    
    if (pass_count < 5)
    {
        printf("WARNING: Data valid window size is only %d core clocks, minimum should be 5.\n", pass_count);
    }
    
    read_adj  = (results[best_setting] & 0x06) >> 1;
    sdelay    = (results[best_setting] & 0x01);
    pad_delay = (results[best_setting] & 0x38) >> 3;
    printf("Best settings: read adj %d, sdelay = %d, pad_delay = %d\n",read_adj,sdelay,pad_delay);
    ctx->best_setting = results[best_setting];
}

void qspi_flash_fast_read_apply_best_setting(
    qspi_fast_flash_read_ctx_t *ctx)
{
    int sdelay = (ctx->best_setting & 0x01);
    int pad_delay = (ctx->best_setting & 0x38) >> 3;

    ctx->read_adj  = (ctx->best_setting & 0x06) >> 1;
    if (sdelay == 1) {
        port_set_sample_falling_edge(ctx->sio_port);
    } else {
        port_set_sample_rising_edge(ctx->sio_port);
    }
    QSPI_IO_RESOURCE_SETC(ctx->sio_port, QSPI_IO_SETC_PAD_DELAY(pad_delay));
}

void qspi_flash_fast_read_calibrate(
    qspi_fast_flash_read_ctx_t *ctx)
{
    unsigned read_data_tmp[QSPI_FLASH_FAST_READ_PATTERN_WORDS];

    int passing_words;
    xassert(ctx->divide < MAX_CLK_DIVIDE);
    // Declare the results as an array of 36 (max) chars.
    // Bit 0 sdelay, bits 1-2 read adj, bits 3-5 pad delay, bit 7 is pass/fail.
    // the index into the array becomes the nominal time.
    char results[6*MAX_CLK_DIVIDE];
    
    // So, lets run the testing and collect pass/fail results.
    // Keep an index of the time we are looping across.
    unsigned char time_index = 0;

    // This loops over the settings in such a way that the result is sequentially increasing in time in units of core clocks.
    for(int read_adj = 0; read_adj < 3; read_adj++) // Data read port time adjust
    {
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
                // set_pad_delay(ctx->sio_port, pad_delay);
                QSPI_IO_RESOURCE_SETC(ctx->sio_port, QSPI_IO_SETC_PAD_DELAY(pad_delay));

                // Read the data with the current settings
                fast_read_loop(ctx, QSPI_FLASH_FAST_READ_PATTERN_ADDRESS, 0, read_adj, QSPI_FLASH_FAST_READ_PATTERN_WORDS, read_data_tmp);
                
                // Check if the data is correct
                passing_words = 0;
                for (int m = 0; m < QSPI_FLASH_FAST_READ_PATTERN_WORDS; m++)
                {
                    printf("0x%x\n",read_data_tmp[m]);
                    if (read_data_tmp[m] == qspi_flash_fast_read_pattern[m]) {
                        passing_words++;
                    }
                }
                char setting = sdelay | (read_adj << 1) | (pad_delay << 3);
                // Store the settings and pass/fail in the results
                if (passing_words == QSPI_FLASH_FAST_READ_PATTERN_WORDS) {
                    results[time_index] = setting | 1 << 7;
                } else {
                    results[time_index] = setting;
                }
                time_index++;
            }
        }
    }
    
    find_best_setting(ctx, results);
}


// This function has a bit longer latency but meets the timing specs for CS_N active setup time to rising edge of clock for faster clock speeds.
void fast_read_loop(
    qspi_fast_flash_read_ctx_t *ctx, unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[])
{
    unsigned addr_mode_wr;
    unsigned int1, int2;
    unsigned read_start_pt;
    
    // Starting data read port timer value
    read_start_pt = 27 + read_adj;

    // We shift the address left by 8 bits to align the MSB of address with MSB of the int. We or in the mode bits.
    // Buffered ports remember always shift right.
    // So LSB first
    // We need to nibble swap the address and mode word as it needs to be output MS nibble first and ports do the opposite
    // {int1, int2} = unzip(byterev((addr << 8) | mode), 2);
    // addr_mode_wr = zip(int2, int1, 2);
    
    addr_mode_wr = nibble_swap(byterev((addr << 8) | mode));
    
    //printf("addr_mode_wr 0x%08x\n", addr_mode_wr);
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
    read_data[0] = port_in_at_time(ctx->sio_port, read_start_pt);
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    for (int i = 1; i < read_count; i++)
    {
        read_data[i] = port_in(ctx->sio_port);
    }
    
    // Stop the clock
    clock_stop(ctx->clock_block);
    
    // Put chip select back high
    port_out(ctx->cs_port, 1);
}

// This function has a bit longer latency but meets the timing specs for CS_N active setup time to rising edge of clock for faster clock speeds.
void fast_read_loop_bitrev(
    qspi_fast_flash_read_ctx_t *ctx, unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[])
{
    unsigned addr_mode_wr;
    unsigned int1, int2;
    unsigned read_start_pt;
    
    // Starting data read port timer value
    read_start_pt = 27 + read_adj;

    // We shift the address left by 8 bits to align the MSB of address with MSB of the int. We or in the mode bits.
    // Buffered ports remember always shift right.
    // So LSB first
    // We need to nibble swap the address and mode word as it needs to be output MS nibble first and ports do the opposite
    // {int1, int2} = unzip(byterev((addr << 8) | mode), 2);
    // addr_mode_wr = zip(int2, int1, 2);
    
    addr_mode_wr = nibble_swap(byterev((addr << 8) | mode));
    
    //printf("addr_mode_wr 0x%08x\n", addr_mode_wr);
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
    read_data[0] = port_in_at_time(ctx->sio_port, read_start_pt);
    read_data[0] = bitrev(read_data[0]);
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    for (int i = 1; i < read_count; i++)
    {
        read_data[i] = port_in(ctx->sio_port);
        read_data[i] = bitrev(read_data[i]);
    }
    
    // Stop the clock
    clock_stop(ctx->clock_block);
    
    // Put chip select back high
    port_out(ctx->cs_port, 1);
}

void qspi_flash_fast_read(
    qspi_fast_flash_read_ctx_t *ctx,
	unsigned int addr,
	unsigned char *buf,
	size_t len)
{
    fast_read_loop(ctx, addr, 0, ctx->read_adj, len, buf);
}
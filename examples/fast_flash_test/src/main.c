// Copyright (c) 2023 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public License: Version 1

/* System headers */
#include <platform.h>
#include <xs1.h>
#include <xcore/clock.h>
#include <xcore/port.h>
#include <xcore/hwtimer.h>
#include <xcore/parallel.h>
#include <xclib.h>
#include "qspi_flash_fast_read.h"

DECLARE_JOB(test, (void));
DECLARE_JOB(dummy, (void));

xclock_t clk_spi = XS1_CLKBLK_1;
port_t p_spi_clk = XS1_PORT_1C;
port_t p_spi_csn = XS1_PORT_1B;
port_t p_spi_dat = XS1_PORT_4B;


// Define the clock source divide - 600MHz/800MHz core clock divided by (2*CLK_DIVIDE)
// CORE CLOCK   600MHz    800MHz
// CLK_DIVIDE      SPI_CLK
// 3            100MHz    133MHz
// 4            75MHz     100MHz
// 5            60MHz     80MHz
// 6            50MHz     66MHz

#define CLK_DIVIDE 3



/* The SETC constant for pad delay is missing from xs2a_user.h */
#define QSPI_IO_SETC_PAD_DELAY(n) (0x7007 | ((n) << 3))

/* These appear to be missing from the public API of lib_xcore */
#define QSPI_IO_RESOURCE_SETCI(res, c) asm volatile( "setc res[%0], %1" :: "r" (res), "n" (c))
#define QSPI_IO_RESOURCE_SETC(res, r) asm volatile( "setc res[%0], %1" :: "r" (res), "r" (r))


qspi_fast_flash_read_ctx_t qspi_fast_flash_read_ctx;
qspi_fast_flash_read_ctx_t *ctx = &qspi_fast_flash_read_ctx;


__attribute__((always_inline))
inline uint32_t nibble_swap(uint32_t word)
{
	uint32_t tmp;
    asm volatile (
        ".issue_mode dual\n"
        "dualentsp 0\n"
		"{and %0,%0,%2 ; and  %1,%0,%3}\n"
		"{shl %0,%0,4  ; shr  %1,%1,4}\n"
        "entsp 0\n"
        ".issue_mode single\n"
		: "+r"(word), "=r"(tmp)
		: "r"(0x0F0F0F0F), "r"(0xF0F0F0F0)
	);

	return word | tmp;
    // return ((word & 0x0F0F0F0F) << 4) | ((word & 0xF0F0F0F0) >> 4);
}

// This function has a bit longer latency but meets the timing specs for CS_N active setup time to rising edge of clock for faster clock speeds.
void fast_read_loop(unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[])
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
    port_out_part_word(p_spi_dat, 0x1, 4);
    clock_start(clk_spi);
    port_sync(p_spi_dat);
    clock_stop(clk_spi);
    
    port_out(p_spi_csn, 0); // Set CS_N low
    
    // Pre load the transfer register in the port with data. This will not go out yet because clock has not been started.
    // This data needs to be shifted as when starting the clock we will clock the first bit of data to flash before this data goes out.
    // We also need to Or in the first nibble of addr_mode_wr that we want to output.
    // This is the 7 LSB of the 0xEB instruction on dat[0], dat[3:1] = 0
    port_out(p_spi_dat, 0x01101011 | ((addr_mode_wr & 0x0000000F) << 28));
    
    // Start the clock block running. This starts output of data and resets the port timer to 0 on the clk port.
    clock_start(clk_spi);

    port_out_part_word(p_spi_dat, (addr_mode_wr >> 4), 28); // Immediately follow up with the remaining address and mode bits
    
    // Now we want to turn the port around at the right point.
    // At specified value of port timer we read the transfer reg word and discard, data will be junk. Exact timing of port going high-z would need simulation but it will be in the cycle specified.

    (void) port_in_at_time(p_spi_dat, 18);
    // Now we need to read the transfer register at the correct port time so that the initial data from flash will be in there
    read_data[0] = port_in_at_time(p_spi_dat, read_start_pt);
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    for (int i = 1; i < read_count; i++)
    {
        read_data[i] = port_in(p_spi_dat);
    }
    
    // Stop the clock
    clock_stop(clk_spi);
    
    // Put chip select back high
    port_out(p_spi_csn, 1);
}

// This function has a bit longer latency but meets the timing specs for CS_N active setup time to rising edge of clock for faster clock speeds.
void fast_read_loop_bitrev(unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[])
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
    port_out_part_word(p_spi_dat, 0x1, 4);
    clock_start(clk_spi);
    port_sync(p_spi_dat);
    clock_stop(clk_spi);
    
    port_out(p_spi_csn, 0); // Set CS_N low
    
    // Pre load the transfer register in the port with data. This will not go out yet because clock has not been started.
    // This data needs to be shifted as when starting the clock we will clock the first bit of data to flash before this data goes out.
    // We also need to Or in the first nibble of addr_mode_wr that we want to output.
    // This is the 7 LSB of the 0xEB instruction on dat[0], dat[3:1] = 0
    port_out(p_spi_dat, 0x01101011 | ((addr_mode_wr & 0x0000000F) << 28));
    
    // Start the clock block running. This starts output of data and resets the port timer to 0 on the clk port.
    clock_start(clk_spi);

    port_out_part_word(p_spi_dat, (addr_mode_wr >> 4), 28); // Immediately follow up with the remaining address and mode bits
    
    // Now we want to turn the port around at the right point.
    // At specified value of port timer we read the transfer reg word and discard, data will be junk. Exact timing of port going high-z would need simulation but it will be in the cycle specified.

    (void) port_in_at_time(p_spi_dat, 18);
    // Now we need to read the transfer register at the correct port time so that the initial data from flash will be in there
    read_data[0] = port_in_at_time(p_spi_dat, read_start_pt);
    read_data[0] = bitrev(read_data[0]);
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    for (int i = 1; i < read_count; i++)
    {
        read_data[i] = port_in(p_spi_dat);
        read_data[i] = bitrev(read_data[i]);
    }
    
    // Stop the clock
    clock_stop(clk_spi);
    
    // Put chip select back high
    port_out(p_spi_csn, 1);
}


// This function has a bit longer latency but meets the timing specs for CS_N active setup time to rising edge of clock for faster clock speeds.
void fast_read_loop_bitrev2(unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[])
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
    port_out_part_word(p_spi_dat, 0x1, 4);
    clock_start(clk_spi);
    port_sync(p_spi_dat);
    clock_stop(clk_spi);
    
    port_out(p_spi_csn, 0); // Set CS_N low
    
    // Pre load the transfer register in the port with data. This will not go out yet because clock has not been started.
    // This data needs to be shifted as when starting the clock we will clock the first bit of data to flash before this data goes out.
    // We also need to Or in the first nibble of addr_mode_wr that we want to output.
    // This is the 7 LSB of the 0xEB instruction on dat[0], dat[3:1] = 0
    port_out(p_spi_dat, 0x01101011 | ((addr_mode_wr & 0x0000000F) << 28));
    
    // Start the clock block running. This starts output of data and resets the port timer to 0 on the clk port.
    clock_start(clk_spi);

    port_out_part_word(p_spi_dat, (addr_mode_wr >> 4), 28); // Immediately follow up with the remaining address and mode bits
    
    // Now we want to turn the port around at the right point.
    // At specified value of port timer we read the transfer reg word and discard, data will be junk. Exact timing of port going high-z would need simulation but it will be in the cycle specified.

    (void) port_in_at_time(p_spi_dat, 18);
    // Now we need to read the transfer register at the correct port time so that the initial data from flash will be in there
    read_data[0] = port_in_at_time(p_spi_dat, read_start_pt);
    // All following reads will happen directly after this read with no gaps so do not need to be timed.
    for (int i = 1; i < read_count; i++)
    {
        read_data[i] = port_in(p_spi_dat);
    }
    for (int i = 0; i < read_count; i++)
    {
        read_data[i] = bitrev(read_data[i]);
    }
    // Stop the clock
    clock_stop(clk_spi);
    
    // Put chip select back high
    port_out(p_spi_csn, 1);
}


char find_best_setting(char results[6*CLK_DIVIDE])
{
    char pass_start = 0;
    char pass_count = 0;
    char read_adj, sdelay, pad_delay, pass;
    
    // Loop over the results looking for the window of settings where we pass.
    for(int time_index = 0; time_index < (6*CLK_DIVIDE); time_index++) // Loop over time
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
    return best_setting;
}


void test(void)
{
    printf("CLK_DIVIDE %D\n",CLK_DIVIDE);

    // Setup the clock block and ports
    qspi_flash_fast_read_init(ctx,
        XS1_CLKBLK_1,
        XS1_PORT_1B,
        XS1_PORT_1C,
        XS1_PORT_4B,
        qspi_fast_flash_read_transfer_raw,
        CLK_DIVIDE
    );
    qspi_flash_fast_read_setup_resources(ctx);

    unsigned read_data_tmp[QSPI_FLASH_FAST_READ_PATTERN_WORDS];

    int passing_words;
    
    // Declare the results as an array of 36 (max) chars.
    // Bit 0 sdelay, bits 1-2 read adj, bits 3-5 pad delay, bit 7 is pass/fail.
    // the index into the array becomes the nominal time.
    char results[6*CLK_DIVIDE];
    
    // So, lets run the testing and collect pass/fail results.
    // Keep an index of the time we are looping across.
    unsigned char time_index = 0;

    // This loops over the settings in such a way that the result is sequentially increasing in time in units of core clocks.
    for(int read_adj = 0; read_adj < 3; read_adj++) // Data read port time adjust
    {
        for(int sdelay = 0; sdelay < 2; sdelay++) // Sample delays
        {
            if (sdelay == 1) {
                port_set_sample_falling_edge(p_spi_dat);
            } else {
                port_set_sample_rising_edge(p_spi_dat);
            }
            for(int pad_delay = (CLK_DIVIDE - 1); pad_delay >= 0; pad_delay--) // Pad delays (only loop over useful pad delays)
            {
                // Set input pad delay in units of core clocks
                // set_pad_delay(p_spi_dat, pad_delay);
                QSPI_IO_RESOURCE_SETC(p_spi_dat, QSPI_IO_SETC_PAD_DELAY(pad_delay));

                // Read the data with the current settings
                fast_read_loop(QSPI_FLASH_FAST_READ_PATTERN_ADDRESS, 0, read_adj, QSPI_FLASH_FAST_READ_PATTERN_WORDS, read_data_tmp);
                // fast_read_loop_bitrev(QSPI_FLASH_FAST_READ_PATTERN_ADDRESS, 0, read_adj, QSPI_FLASH_FAST_READ_PATTERN_WORDS, read_data_tmp);
                
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
                    /* for (int m = 0; m < QSPI_FLASH_FAST_READ_PATTERN_WORDS; m++)
                    {
                        printf("read_data_tmp 0x%08x, xor 0x%08x\n", read_data_tmp[m], (read_data_tmp[m] ^ read_data_check[m]));
                    } */
                }
                time_index++;
            }
        }
    }
    
    char settings = find_best_setting(results);

    delay_microseconds(1);

    {
        unsigned read_data[1024];
        int read_adj  = (results[settings] & 0x06) >> 1;
        int sdelay    = (results[settings] & 0x01);
        int pad_delay = (results[settings] & 0x38) >> 3;
        if (sdelay == 1) {
            port_set_sample_falling_edge(p_spi_dat);
        } else {
            port_set_sample_rising_edge(p_spi_dat);
        }
        QSPI_IO_RESOURCE_SETC(p_spi_dat, QSPI_IO_SETC_PAD_DELAY(pad_delay));

        uint32_t start = get_reference_time();
        fast_read_loop(0, 0, read_adj, 1024, read_data);
        uint32_t end = get_reference_time();
        printf("duration: %d bytes: %d start: %d end: %d\n", end-start, 1024*4, start, end);
    }

    delay_microseconds(1);

    {
        unsigned read_data[1024];
        int read_adj  = (results[settings] & 0x06) >> 1;
        int sdelay    = (results[settings] & 0x01);
        int pad_delay = (results[settings] & 0x38) >> 3;
        if (sdelay == 1) {
            port_set_sample_falling_edge(p_spi_dat);
        } else {
            port_set_sample_rising_edge(p_spi_dat);
        }
        QSPI_IO_RESOURCE_SETC(p_spi_dat, QSPI_IO_SETC_PAD_DELAY(pad_delay));

        uint32_t start = get_reference_time();
        fast_read_loop_bitrev(0, 0, read_adj, 1024, read_data);
        uint32_t end = get_reference_time();
        printf("bitrev duration: %d bytes: %d start: %d end: %d\n", end-start, 1024*4, start, end);
    }

    delay_microseconds(1);

    {
        unsigned read_data[1024];
        int read_adj  = (results[settings] & 0x06) >> 1;
        int sdelay    = (results[settings] & 0x01);
        int pad_delay = (results[settings] & 0x38) >> 3;
        if (sdelay == 1) {
            port_set_sample_falling_edge(p_spi_dat);
        } else {
            port_set_sample_rising_edge(p_spi_dat);
        }
        QSPI_IO_RESOURCE_SETC(p_spi_dat, QSPI_IO_SETC_PAD_DELAY(pad_delay));

        uint32_t start = get_reference_time();
        fast_read_loop_bitrev2(0, 0, read_adj, 1024, read_data);
        uint32_t end = get_reference_time();
        printf("bitrev duration: %d bytes: %d start: %d end: %d\n", end-start, 1024*4, start, end);
    }
        

    while(1) {
        ; // trap
    }
}

// Dummy thread to make sure we're running 8 threads.
void dummy(void)
{
    int i,j;

    while(1)
    {
      i++;
      if (i == 0) {
        j = i;
      }
    }
}


int main(void)
{
    PAR_JOBS (
        PJOB(test, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ()),
        PJOB(dummy, ())
    );
    return 0;
}

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

#define CLK_DIVIDE 6
// #define CLK_DIVIDE 3


qspi_fast_flash_read_ctx_t qspi_fast_flash_read_ctx;
qspi_fast_flash_read_ctx_t *ctx = &qspi_fast_flash_read_ctx;

void test(void)
{
    // Setup the clock block and ports
    qspi_flash_fast_read_init(ctx,
        XS1_CLKBLK_1,
        XS1_PORT_1B,
        XS1_PORT_1C,
        XS1_PORT_4B,
        // qspi_fast_flash_read_transfer_raw,
        qspi_fast_flash_read_transfer_nibble_swap,
        CLK_DIVIDE
    );
    qspi_flash_fast_read_setup_resources(ctx);
    if (qspi_flash_fast_read_calibrate(ctx) != 0) {
        printf("Fast flash calibration failed\n");
        _Exit(0);
    }

    {
        unsigned read_data[10240];
        uint32_t start = get_reference_time();
        qspi_flash_fast_read(ctx, 0x100000, read_data, 10240);
        uint32_t end = get_reference_time();
        printf("duration: %d bytes: %d start: %d end: %d\n", end-start, 10240*4, start, end);
        unsigned read_data2[10240];
        for(int i=0; i<10240; i++) {
            read_data2[i] = (read_data[i]);
        }

        char *ptr = &read_data2;
        for(int i=0; i<1024; i++) {
            printf("%02x ", (char)*(ptr+i));
        }
        printf("\n");
    }

    qspi_flash_fast_read_shutdown(ctx);
    qspi_flash_fast_read_setup_resources(ctx);

    {
        printf("Test 2\n");
        unsigned read_data[10240];
        uint32_t start = get_reference_time();
        qspi_flash_fast_read(ctx, 0x100000, read_data, 10);
        uint32_t end = get_reference_time();
        printf("duration: %d bytes: %d start: %d end: %d\n", end-start, 10, start, end);
        unsigned read_data2[10];
        for(int i=0; i<10; i++) {
            read_data2[i] = (read_data[i]);
        }

        char *ptr = &read_data2;
        for(int i=0; i<10; i++) {
            printf("%02x ", (char)*(ptr+i));
        }
        printf("\n");
    }

    qspi_flash_fast_read_shutdown(ctx);
    qspi_flash_fast_read_setup_resources(ctx);

    {
        printf("Test 3\n");
        unsigned read_data[1];
        uint32_t start = get_reference_time();
        qspi_flash_fast_read(ctx, 0x100000, read_data, 1);
        uint32_t end = get_reference_time();
        printf("duration: %d bytes: %d start: %d end: %d\n", end-start, 1, start, end);
        unsigned read_data2[1];
        for(int i=0; i<1; i++) {
            read_data2[i] = (read_data[i]);
        }

        char *ptr = &read_data2;
        for(int i=0; i<1; i++) {
            printf("%02x ", (char)*(ptr+i));
        }
        printf("\n");
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

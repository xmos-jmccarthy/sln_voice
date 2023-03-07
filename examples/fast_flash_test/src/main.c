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
        qspi_fast_flash_read_transfer_raw,
        CLK_DIVIDE
    );
    qspi_flash_fast_read_setup_resources(ctx);
    qspi_flash_fast_read_calibrate(ctx);

    {
        unsigned read_data[10240];
        uint32_t start = get_reference_time();
        // fast_read_loop(0, 0, read_adj, 10240, read_data);
        qspi_flash_fast_read(ctx, 0, read_data, 10240);
        uint32_t end = get_reference_time();
        printf("duration: %d bytes: %d start: %d end: %d\n", end-start, 10240*4, start, end);
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
        // PJOB(dummy, ()),
        // PJOB(dummy, ()),
        // PJOB(dummy, ()),
        // PJOB(dummy, ()),
        // PJOB(dummy, ()),
        PJOB(dummy, ())
    );
    return 0;
}

// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#ifndef QSPI_FAST_FLASH_READ_PATTERN_H_
#define QSPI_FAST_FLASH_READ_PATTERN_H_

#ifdef __cplusplus
extern "C" {
#endif

// Address in Flash where the training pattern starts
#define QSPI_FLASH_FAST_READ_PATTERN_ADDRESS 0x100000
// How many 32 bit words are in the pattern
#define QSPI_FLASH_FAST_READ_PATTERN_WORDS 1

static unsigned qspi_flash_fast_read_pattern[QSPI_FLASH_FAST_READ_PATTERN_WORDS] = {
    // 0xD409EFBE
    0x7DF7902B
};

#ifdef __cplusplus
};
#endif

#endif /* QSPI_FAST_FLASH_READ_PATTERN_H_ */


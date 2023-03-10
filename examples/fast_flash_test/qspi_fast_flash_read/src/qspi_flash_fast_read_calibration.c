// Copyright (c) 2023 XMOS LIMITED. This Software is subject to the terms of the
// XMOS Public License: Version 1

#include "qspi_flash_fast_read_priv.h"

__attribute__((section(".SwMem_data")))
unsigned int qspi_flash_fast_read_pattern[QSPI_FLASH_FAST_READ_PATTERN_WORDS] = {
    0xf0f000ff,
    0xf0f0f0f0,
    0x00ff00ff,
    0x00ff00ff,
    0x80ec7f13,
    0x80ec7f13,
    0x36c936c9,
    0x36c936c9
};

// unsigned int qspi_flash_fast_read_pattern[QSPI_FLASH_FAST_READ_PATTERN_WORDS] = {
//     0x0f0f00ff,
//     0x0f0f0f0f,
//     0x00ff00ff,
//     0x00ff00ff,
//     0x08cef731,
//     0x08cef731,
//     0x639c639c,
//     0x639c639c
// };

unsigned int qspi_flash_fast_read_pattern_expect[QSPI_FLASH_FAST_READ_PATTERN_WORDS] = {
    0x0f0f00ff,
    0x0f0f0f0f,
    0x00ff00ff,
    0x00ff00ff,
    0x08cef731,
    0x08cef731,
    0x639c639c,
    0x639c639c
};

#ifndef UNIT_TEST

#include "system/firmware_header.h"
#include "config/device_config.h"

const firmware_image_header_t _firmware_image_header
    __attribute__((section(".firmware_header"), used)) = {
    .magic_0    = FW_HEADER_MAGIC_0,
    .magic_1    = FW_HEADER_MAGIC_1,
    .fw_version = ((uint32_t)FIRMWARE_VERSION_MAJOR << 16) |
                  ((uint32_t)FIRMWARE_VERSION_MINOR <<  8) |
                  ((uint32_t)FIRMWARE_VERSION_PATCH),
    .text_crc32  = 0x00000000,
    .text_length = 0x00000000,
    .reserved    = {0, 0, 0},
};

#endif /* UNIT_TEST */

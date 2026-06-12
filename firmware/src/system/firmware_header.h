#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h>

#define FW_HEADER_MAGIC_0  0xC63D0001u
#define FW_HEADER_MAGIC_1  0x3FC2FFFFu  /* bitwise complement of MAGIC_0 */

typedef struct __attribute__((packed)) {
    uint32_t magic_0;
    uint32_t magic_1;
    uint32_t fw_version;   /* MAJOR<<16 | MINOR<<8 | PATCH */
    uint32_t text_crc32;   /* stamped by crc_stamp.py post-build */
    uint32_t text_length;  /* stamped by crc_stamp.py post-build */
    uint32_t reserved[3];
} firmware_image_header_t;

#ifndef UNIT_TEST
extern uint8_t _image_text_start[];
extern uint8_t _image_rodata_start[];
extern uint8_t _image_rodata_end[];
extern const firmware_image_header_t _firmware_image_header;
#endif

#endif /* FIRMWARE_HEADER_H */

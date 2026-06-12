#ifndef POST_H
#define POST_H

#include "config/error_codes.h"
#include "system/firmware_header.h"
#include <stdint.h>

/* Testable core: verify header magic, stamped length, and CRC over region. */
cgm_error_t post_flash_verify(const firmware_image_header_t *hdr,
                               const void *region, uint32_t len);

/* Production entry point: calls post_flash_verify with linker-resolved symbols. */
cgm_error_t post_flash_crc_check(void);

#endif /* POST_H */

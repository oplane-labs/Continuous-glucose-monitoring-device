#include "system/post.h"
#include "system/firmware_header.h"
#include "system/crc_utils.h"

#ifndef UNIT_TEST

cgm_error_t post_flash_crc_check(void)
{
    const firmware_image_header_t *hdr = &_firmware_image_header;

    if (hdr->magic_0 != FW_HEADER_MAGIC_0 ||
        hdr->magic_1 != FW_HEADER_MAGIC_1) {
        return CGM_ERR_POST_FLASH;
    }

    if (hdr->text_length == 0) {
        return CGM_ERR_POST_FLASH;
    }

    uint32_t computed = crc32_calculate(_image_text_start, hdr->text_length);
    if (computed != hdr->text_crc32) {
        return CGM_ERR_POST_FLASH;
    }

    return CGM_OK;
}

#else /* UNIT_TEST */

cgm_error_t test_stub_post_flash_result = CGM_OK;

cgm_error_t post_flash_crc_check(void)
{
    return test_stub_post_flash_result;
}

#endif /* UNIT_TEST */

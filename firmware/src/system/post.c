#include "system/post.h"
#include "system/crc_utils.h"

cgm_error_t post_flash_verify(const firmware_image_header_t *hdr,
                               const void *region, uint32_t len)
{
    if (hdr->magic_0 != FW_HEADER_MAGIC_0 ||
        hdr->magic_1 != FW_HEADER_MAGIC_1) {
        return CGM_ERR_POST_FLASH;
    }

    if (hdr->text_length == 0) {
        return CGM_ERR_POST_FLASH;
    }

    uint32_t computed = crc32_calculate(region, len);
    if (computed != hdr->text_crc32) {
        return CGM_ERR_POST_FLASH;
    }

    return CGM_OK;
}

#ifndef UNIT_TEST

cgm_error_t post_flash_crc_check(void)
{
    return post_flash_verify(&_firmware_image_header,
                             _image_text_start,
                             _firmware_image_header.text_length);
}

#endif /* UNIT_TEST */

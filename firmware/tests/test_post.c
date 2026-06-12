/**
 * @file test_post.c
 * @brief Tests for firmware integrity verification (OPLANE_REQ-00092310)
 *
 * Tests call post_flash_verify() directly with synthetic headers and regions
 * so the actual CRC logic, magic checks, and error paths are exercised —
 * not a stub.
 */

#include "unity.h"
#include "system/post.h"
#include "system/firmware_header.h"
#include "system/crc_utils.h"
#include "config/error_codes.h"

static const uint8_t s_region[] = {0x01, 0x02, 0x03, 0x04, 0xDE, 0xAD, 0xBE, 0xEF};

static firmware_image_header_t make_valid_header(void)
{
    firmware_image_header_t hdr = {0};
    hdr.magic_0    = FW_HEADER_MAGIC_0;
    hdr.magic_1    = FW_HEADER_MAGIC_1;
    hdr.text_length = sizeof(s_region);
    hdr.text_crc32  = crc32_calculate(s_region, sizeof(s_region));
    return hdr;
}

void setUp(void) {}
void tearDown(void) {}

void test_valid_header_and_matching_crc_returns_ok(void)
{
    firmware_image_header_t hdr = make_valid_header();
    TEST_ASSERT_EQUAL(CGM_OK, post_flash_verify(&hdr, s_region, sizeof(s_region)));
}

void test_crc_mismatch_returns_post_flash_error(void)
{
    firmware_image_header_t hdr = make_valid_header();
    hdr.text_crc32 ^= 0x1; /* corrupt stored CRC */
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH,
                      post_flash_verify(&hdr, s_region, sizeof(s_region)));
}

void test_corrupted_region_returns_post_flash_error(void)
{
    firmware_image_header_t hdr = make_valid_header();
    uint8_t bad_region[sizeof(s_region)];
    memcpy(bad_region, s_region, sizeof(s_region));
    bad_region[3] ^= 0xFF; /* flip a byte */
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH,
                      post_flash_verify(&hdr, bad_region, sizeof(bad_region)));
}

void test_bad_magic_0_returns_post_flash_error(void)
{
    firmware_image_header_t hdr = make_valid_header();
    hdr.magic_0 ^= 0x1;
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH,
                      post_flash_verify(&hdr, s_region, sizeof(s_region)));
}

void test_bad_magic_1_returns_post_flash_error(void)
{
    firmware_image_header_t hdr = make_valid_header();
    hdr.magic_1 ^= 0x1;
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH,
                      post_flash_verify(&hdr, s_region, sizeof(s_region)));
}

void test_zero_length_returns_post_flash_error(void)
{
    firmware_image_header_t hdr = make_valid_header();
    hdr.text_length = 0; /* simulates unstamped binary */
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH,
                      post_flash_verify(&hdr, s_region, sizeof(s_region)));
}

void test_error_code_is_0x0701(void)
{
    firmware_image_header_t hdr = make_valid_header();
    hdr.text_crc32 ^= 0x1;
    cgm_error_t err = post_flash_verify(&hdr, s_region, sizeof(s_region));
    TEST_ASSERT_EQUAL_HEX16(0x0701, (uint16_t)err);
}

void test_post_flash_failure_propagates_to_main(void)
{
    /* Verify the boot halt path is reachable when POST returns non-OK.
     * In UNIT_TEST mode firmware_main() returns -1 instead of looping. */
    extern cgm_error_t test_stub_post_flash_result;
    test_stub_post_flash_result = CGM_ERR_POST_FLASH;
    extern int firmware_main(void);
    TEST_ASSERT_EQUAL(-1, firmware_main());
    test_stub_post_flash_result = CGM_OK;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_header_and_matching_crc_returns_ok);
    RUN_TEST(test_crc_mismatch_returns_post_flash_error);
    RUN_TEST(test_corrupted_region_returns_post_flash_error);
    RUN_TEST(test_bad_magic_0_returns_post_flash_error);
    RUN_TEST(test_bad_magic_1_returns_post_flash_error);
    RUN_TEST(test_zero_length_returns_post_flash_error);
    RUN_TEST(test_error_code_is_0x0701);
    RUN_TEST(test_post_flash_failure_propagates_to_main);
    return UNITY_END();
}

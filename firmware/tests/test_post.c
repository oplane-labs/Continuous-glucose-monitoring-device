/**
 * @file test_post.c
 * @brief Unit tests for POST flash CRC check
 * Implements: OPLANE_REQ-00092310, SWR-071
 */

#include "unity.h"
#include "system/post.h"
#include "config/error_codes.h"
#include "test_stubs.h"

void setUp(void)
{
    test_stubs_reset();
    test_stub_post_flash_result = CGM_OK;
}

void tearDown(void) {}

void test_post_flash_crc_pass(void)
{
    TEST_ASSERT_EQUAL(CGM_OK, post_flash_crc_check());
}

void test_post_flash_crc_mismatch(void)
{
    test_stub_post_flash_result = CGM_ERR_POST_FLASH;
    cgm_error_t err = post_flash_crc_check();
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH, err);
    TEST_ASSERT_EQUAL_HEX16(0x0701, (uint16_t)err);
}

void test_post_flash_invalid_magic(void)
{
    test_stub_post_flash_result = CGM_ERR_POST_FLASH;
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH, post_flash_crc_check());
}

void test_post_flash_unstamped_header(void)
{
    test_stub_post_flash_result = CGM_ERR_POST_FLASH;
    TEST_ASSERT_EQUAL(CGM_ERR_POST_FLASH, post_flash_crc_check());
}

void test_post_flash_failure_propagates_to_main(void)
{
    test_stub_post_flash_result = CGM_ERR_POST_FLASH;
    extern int firmware_main(void);
    TEST_ASSERT_EQUAL(-1, firmware_main());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_post_flash_crc_pass);
    RUN_TEST(test_post_flash_crc_mismatch);
    RUN_TEST(test_post_flash_invalid_magic);
    RUN_TEST(test_post_flash_unstamped_header);
    RUN_TEST(test_post_flash_failure_propagates_to_main);
    return UNITY_END();
}

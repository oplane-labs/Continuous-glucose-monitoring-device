#ifndef POST_H
#define POST_H

#include "config/error_codes.h"

cgm_error_t post_flash_crc_check(void);

#ifdef UNIT_TEST
extern cgm_error_t test_stub_post_flash_result;
#endif

#endif /* POST_H */

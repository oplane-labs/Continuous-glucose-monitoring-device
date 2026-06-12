#ifndef CRC_UTILS_H
#define CRC_UTILS_H

#include <stdint.h>
#include <stddef.h>

uint32_t crc32_calculate(const void *data, size_t len);

#endif /* CRC_UTILS_H */

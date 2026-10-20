#pragma once
#include <stddef.h>
/* Capture the real rendered snapshot at the encoding boundary, never replace it. */
int mbedtls_base64_encode(unsigned char *dst,size_t capacity,size_t *used,const unsigned char *src,size_t n);

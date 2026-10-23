#pragma once
#include "pet_reader.h"
#include "cJSON.h"
#include <stddef.h>
typedef struct {pet_reader_page_t page;bool catalog,available;} pet_reader_metadata_t;
bool pet_reader_parse(cJSON *json,pet_reader_metadata_t *result);
size_t pet_reader_encode(const pet_reader_request_t *request,char *out,size_t capacity);

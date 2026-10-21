#pragma once
#include "pet_caption.h"
#include "pet_face.h"

/* Keep the first three values stable for the USB display bench. */
typedef enum {
    PET_PAGE_HOME, PET_PAGE_SETTINGS, PET_PAGE_CONNECTION, PET_PAGE_READING,
    PET_PAGE_VOLUME, PET_PAGE_BRIGHTNESS, PET_PAGE_CAPTIONS, PET_PAGE_MORE,
    PET_PAGE_PROVISION, PET_PAGE_DETAIL, PET_PAGE_COUNT
} pet_page_t;
typedef struct {
    pet_pose_t pose;
    pet_dialogue_t dialogue;
    pet_connection_t connection;
    pet_caption_mode_t caption_mode;
    bool waiting_long;
    const char *caption;
    pet_notice_t notice;
} pet_readability_input_t;
typedef struct {
    char summary[PET_CAPTION_BYTES+1];
    const char *hint;
    bool can_read, local;
} pet_readability_text_t;

/* Pure presentation policy; input caption remains owned by the caller. */
pet_readability_text_t pet_readability_text(const pet_readability_input_t *input);
pet_page_t pet_readability_page(pet_page_t page,const pet_readability_input_t *input);
pet_face_t pet_readability_face(pet_pose_t pose);

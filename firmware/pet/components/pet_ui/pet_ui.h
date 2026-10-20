#pragma once
#include "pet_core.h"
#include "esp_err.h"
typedef enum {PET_UI_PET,PET_UI_WAKE,PET_UI_VOLUME,PET_UI_BRIGHTNESS,PET_UI_QUIET,
              PET_UI_CAPTIONS,PET_UI_SPEAKER,PET_UI_SETUP_OPEN,PET_UI_SETUP_CLOSE,PET_UI_TOUCH_TAP} pet_ui_action_kind_t;
typedef struct {pet_ui_action_kind_t kind;int value;} pet_ui_action_t;
typedef struct {
    pet_pose_t pose;
    pet_dialogue_t dialogue;
    pet_connection_t connection;
    char caption[321],wifi[80],mac[80],voice[80],setup[96];
    int volume,brightness,battery,passkey;
    bool quiet,captions,speaker,task_channel,waiting_long;
} pet_ui_view_t;
typedef void (*pet_ui_callback_t)(pet_ui_action_t action,void *context);
esp_err_t pet_ui_start(pet_ui_callback_t callback,void *context);
void pet_ui_update(const pet_ui_view_t *view);
void pet_ui_home(void);
void pet_ui_request_snapshot(void);
void pet_ui_bench_page(int page);

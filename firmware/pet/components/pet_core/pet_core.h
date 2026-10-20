#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PET_EXPRESSION_INPUT_VERSION 1

typedef enum { PET_LIFE_AWAKE, PET_LIFE_DROWSY, PET_LIFE_SLEEP } pet_life_t;
typedef enum { PET_DIALOGUE_IDLE, PET_DIALOGUE_LISTEN, PET_DIALOGUE_WAIT, PET_DIALOGUE_SPEAK } pet_dialogue_t;
typedef enum { PET_CONN_OFFLINE, PET_CONN_CONNECTING, PET_CONN_READY, PET_CONN_UNAVAILABLE } pet_connection_t;
typedef enum { PET_EXPR_IDLE, PET_EXPR_HAPPY, PET_EXPR_GREET, PET_EXPR_LISTEN, PET_EXPR_THINK,
               PET_EXPR_SPEAK, PET_EXPR_SLEEP, PET_EXPR_CURIOUS, PET_EXPR_CONFUSED } pet_expression_t;
typedef enum { PET_EV_TICK, PET_EV_PET, PET_EV_PRESS, PET_EV_RELEASE, PET_EV_CANCEL,
               PET_EV_LINK, PET_EV_BUFFERED, PET_EV_PLAYING, PET_EV_RESPONSE_DONE, PET_EV_DRAIN,
               PET_EV_CLOCK, PET_EV_USB, PET_EV_QUIET, PET_EV_MUTED, PET_EV_SETUP,
               PET_EV_TASK_CHANNEL, PET_EV_TOUCH_TAP } pet_event_kind_t;
enum { PET_FX_NONE=0, PET_FX_START=1, PET_FX_STOP=2, PET_FX_SUBMIT=4, PET_FX_CANCEL=8,
       PET_FX_CONNECT=16, PET_FX_WAKE=32, PET_FX_SAVE_QUOTA=64 };
typedef struct {
    pet_event_kind_t kind;
    uint64_t now_ms;
    uint32_t generation, turn, day;
    int value;
} pet_event_t;
typedef struct {
    pet_life_t life;
    pet_dialogue_t dialogue;
    pet_connection_t connection;
    uint64_t now_ms, last_input_ms, started_ms, pet_until_ms, greet_until_ms;
    uint64_t curious_until_ms, confused_until_ms;
    uint64_t next_greet_ms, last_greet_ms, entered_ms, clock_ms;
    uint32_t generation, turn, seed, day;
    unsigned greetings_today;
    int minute, level;
    bool usb, quiet, muted, setup, held, expired, clock_valid, response_done, playback_empty;
    bool task_channel, connection_problem_seen, response_cancelled;
} pet_state_t;
typedef struct {
    pet_expression_t expression;
    float offset_y, scale_x, scale_y, rotation, eye_open, look_x, level;
    float eye_asymmetry, eye_tilt;
    bool screen_on, dim, invitation;
} pet_pose_t;

void pet_init(pet_state_t *state, uint64_t now_ms);
/* Caller serializes state; local activity preserves expression and dialogue. */
void pet_note_activity(pet_state_t *state, uint64_t now_ms);
uint32_t pet_step(pet_state_t *state, pet_event_t event);
pet_pose_t pet_pose(const pet_state_t *state, uint64_t now_ms);
typedef enum { PET_GESTURE_NONE, PET_GESTURE_TAP, PET_GESTURE_STROKE, PET_GESTURE_SETTINGS } pet_gesture_t;
pet_gesture_t pet_gesture(int x0,int y0,int x1,int y1);
bool pet_event_urgent(pet_event_kind_t kind);
bool pet_response_audio_allowed(const pet_state_t *state,uint32_t generation,uint32_t turn);

bool pet_network_should_sleep(const pet_state_t *state);

uint32_t pet_delay_ticks(uint32_t milliseconds,uint32_t tick_rate);

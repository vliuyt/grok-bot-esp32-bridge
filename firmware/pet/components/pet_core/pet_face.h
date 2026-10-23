#pragma once
#include "pet_core.h"

typedef struct {
    int x,y,height,rotation,arc_start,arc_end;
    bool arc;
} pet_eye_t;
typedef struct {
    int x,y,width,height,rotation;
    pet_eye_t eyes[2];
} pet_face_t;

/* Logical 466x466 coordinates shared by LVGL and the exported preview. */
pet_face_t pet_face_geometry(pet_pose_t pose);

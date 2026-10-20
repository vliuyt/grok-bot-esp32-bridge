#include "pet_core.h"
#include "pet_face.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    pet_state_t s;pet_init(&s,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_TOUCH_TAP,.now_ms=100});
    pet_face_t curious=pet_face_geometry(pet_pose(&s,100));
    assert(curious.eyes[0].height>curious.eyes[1].height);
    assert(curious.eyes[0].rotation!=curious.eyes[1].rotation);
    assert(curious.width==224&&curious.height==224&&!curious.eyes[0].arc);
    pet_step(&s,(pet_event_t){.kind=PET_EV_PET,.now_ms=200});
    pet_face_t happy=pet_face_geometry(pet_pose(&s,200));
    assert(happy.eyes[0].arc&&happy.eyes[0].arc_start==200);
    pet_step(&s,(pet_event_t){.kind=PET_EV_TICK,.now_ms=91000});
    pet_face_t sleepy=pet_face_geometry(pet_pose(&s,91000));
    assert(sleepy.eyes[0].arc&&sleepy.eyes[0].arc_start==20);
    for(unsigned expression=0;expression<=PET_EXPR_CONFUSED;expression++) {
        pet_pose_t p={.expression=(pet_expression_t)expression,.scale_x=1,.scale_y=1,.eye_open=1};
        pet_face_t face=pet_face_geometry(p);
        assert(face.x>=0&&face.y>=0&&face.x+face.width<=466&&face.y+face.height<334);
        for(unsigned eye=0;eye<2;eye++) {
            assert(face.eyes[eye].height>=5);
            assert(face.eyes[eye].x>=0&&face.eyes[eye].x+24<=face.width);
            assert(face.eyes[eye].y>=0&&face.eyes[eye].y+face.eyes[eye].height<=face.height);
        }
    }
    puts("PET_FACE_OK");
}

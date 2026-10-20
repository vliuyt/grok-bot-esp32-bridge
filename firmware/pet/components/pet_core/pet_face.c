#include "pet_face.h"
#include <math.h>

pet_face_t pet_face_geometry(pet_pose_t p) {
    pet_face_t f={.width=(int)(224*p.scale_x),.height=(int)(224*p.scale_y),
                  .rotation=(int)(p.rotation*10)};
    f.x=(466-f.width)/2;f.y=210-f.height/2+(int)p.offset_y;
    for(unsigned i=0;i<2;i++) {
        f.eyes[i]=(pet_eye_t){
            .x=(int)(f.width*(i?.5223f:.2411f))+(int)p.look_x,
            .y=(int)(f.height*.29f),
            .height=(int)fmaxf(5,60*p.eye_open*(1+(i?-1:1)*p.eye_asymmetry)),
            .rotation=50+(int)((i?-1:1)*p.eye_tilt*10),
            .arc=p.expression==PET_EXPR_HAPPY||p.expression==PET_EXPR_SLEEP,
            .arc_start=p.expression==PET_EXPR_SLEEP?20:200,
            .arc_end=p.expression==PET_EXPR_SLEEP?160:340
        };
    }
    return f;
}

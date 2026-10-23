#include "pet_readability.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv){
    if(argc!=2)return 2;
    char *end;unsigned long cycle=strtoul(argv[1],&end,10);
    if(*end||cycle<2500||cycle>10000)return 2;
    pet_state_t s;pet_init(&s,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
    pet_step(&s,(pet_event_t){.kind=PET_EV_PRESS,.now_ms=1000});
    pet_motion_t motion={0};
    for(uint64_t age=0;age<cycle*2;age+=33)pet_motion_step(&motion,pet_pose(&s,1000+age),1000+age);
    putchar('[');
    for(uint64_t age=0;age<cycle*2;age+=33){
        uint64_t now=1000+cycle*2+age;
        pet_face_t f=pet_readability_face(pet_motion_step(&motion,pet_pose(&s,now),now));
        printf("%s{\"at\":%llu,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"rotation\":%d,\"eyes\":[",age?",":"",(unsigned long long)age,f.x,f.y,f.width,f.height,f.rotation);
        for(unsigned i=0;i<2;i++)printf("%s{\"x\":%d,\"y\":%d,\"height\":%d,\"rotation\":%d}",i?",":"",f.eyes[i].x,f.eyes[i].y,f.eyes[i].height,f.eyes[i].rotation);
        printf("]}");
    }
    puts("]");return 0;
}

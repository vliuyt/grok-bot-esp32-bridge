#include "pet_readability.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void event(pet_state_t *s,pet_event_kind_t kind,uint64_t now,int value){
    pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,.generation=s->generation,.turn=s->turn});
}
static void ready(pet_state_t *s){
    pet_init(s,0);pet_step(s,(pet_event_t){.kind=PET_EV_LINK,.now_ms=1,.generation=1,.value=PET_CONN_READY});
}
static pet_pose_t resting(void){
    return (pet_pose_t){.expression=PET_EXPR_IDLE,.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true};
}
static int center_y(pet_face_t f){return f.y+f.height/2;}
static void safe_face(pet_face_t f){
    assert(f.y>=76); /* The title occupies y45..71. */
    double theta=f.rotation*3.141592653589793/1800;
    double radius=fmin(f.width,f.height)/2;
    double extent=radius+fabs((double)f.width-f.height)/2*(f.width>=f.height?fabs(sin(theta)):fabs(cos(theta)));
    assert(center_y(f)+extent<307); /* Keep the two subtitle lines clear. */
    assert(hypot(f.x+f.width/2-233,center_y(f)-233)+fmax(f.width,f.height)/2<233);
}
static pet_face_t frame(const pet_state_t *s,pet_motion_t *motion,uint64_t now){
    pet_face_t face=pet_readability_face(pet_motion_step(motion,pet_pose(s,now),now));
    safe_face(face);return face;
}
static void listening_distinct(void){
    pet_state_t s;ready(&s);event(&s,PET_EV_PRESS,100,0);
    pet_motion_t motion={0};pet_motion_step(&motion,resting(),99);
    pet_face_t listening={0};
    for(uint64_t now=100;now<700;now+=33)listening=frame(&s,&motion,now);
    pet_face_t idle=pet_readability_face(resting());
    printf("listen eye_growth=%d lift=%d\n",listening.eyes[0].height-idle.eyes[0].height,center_y(idle)-center_y(listening));fflush(stdout);
    assert(listening.eyes[0].height-idle.eyes[0].height>=0&&listening.eyes[0].height-idle.eyes[0].height<=5);
    assert(center_y(idle)-center_y(listening)>=5&&center_y(idle)-center_y(listening)<=8);
}
static void listening_engaged(void){
    pet_state_t s,other;ready(&s);ready(&other);other.turn=1;
    event(&s,PET_EV_PRESS,100,0);event(&other,PET_EV_PRESS,100,0);
    pet_state_t before;memcpy(&before,&s,sizeof(s));
    pet_motion_t motion={0};pet_motion_step(&motion,resting(),99);
    int min_x=1000,max_x=-1000,min_r=1000,max_r=-1000,min_y=1000,max_y=-1000,min_eye=1000,blinks=0;bool closed=false;
    for(uint64_t age=0;age<=12600;age+=33){
        pet_face_t f=frame(&s,&motion,100+age);
        assert(f.eyes[0].height<=65&&f.width>=195&&f.width<=205);
        if(!closed&&f.eyes[0].height<45){blinks++;closed=true;}
        if(f.eyes[0].height>55)closed=false;
        if(age>=150&&age<1150){if(f.eyes[0].x<min_x)min_x=f.eyes[0].x;if(f.eyes[0].x>max_x)max_x=f.eyes[0].x;}
        if(age>=650&&age<1900){if(f.rotation<min_r)min_r=f.rotation;if(f.rotation>max_r)max_r=f.rotation;}
        if(age>=1850&&age<2900){if(center_y(f)<min_y)min_y=center_y(f);if(center_y(f)>max_y)max_y=center_y(f);}
        if(age>=3000&&age<3500&&f.eyes[0].height<min_eye)min_eye=f.eyes[0].height;
    }
    printf("listen engagement gaze=%d tilt=%d nod=%d blink_eye=%d\n",max_x-min_x,max_r-min_r,max_y-min_y,min_eye);fflush(stdout);
    assert(max_x-min_x>=3&&max_x-min_x<=6);
    assert(max_r-min_r>=15&&max_r-min_r<=35);
    assert(max_y-min_y>=2&&max_y-min_y<=4);
    assert(min_eye>=15&&min_eye<=45);
    assert(blinks==3); /* One brief blink in each selected cadence cycle. */
    assert(!memcmp(&s,&before,sizeof(s))); /* Local presentation cannot record, submit or change the turn. */
    pet_pose_t a=pet_pose(&s,750),b=pet_pose(&other,750);
    assert(a.look_x>=3&&b.look_x<=-3);
    a=pet_pose(&s,4875);assert(a.look_x<=-3); /* A long hold alternates the next quiet cycle. */
    event(&s,PET_EV_RELEASE,7100,0);event(&s,PET_EV_CANCEL,7150,0);event(&s,PET_EV_PRESS,7200,0);
    a=pet_pose(&s,7200);assert(fabsf(a.look_x)<.001f&&fabsf(a.look_y)<.001f);
}
static void warmth_visible(void){
    pet_state_t warm,neutral;ready(&warm);ready(&neutral);
    event(&warm,PET_EV_PET,10,0);
    event(&warm,PET_EV_PRESS,100,0);event(&neutral,PET_EV_PRESS,100,0);
    assert(warm.turn_warmth>34&&warm.turn_warmth<35);
    pet_motion_t a={0},b={0};
    pet_motion_step(&a,resting(),99);pet_motion_step(&b,resting(),99);
    for(int stage=0;stage<3;stage++){
        uint64_t start=100+(uint64_t)stage*700;
        if(stage==1){event(&warm,PET_EV_RELEASE,start,0);event(&neutral,PET_EV_RELEASE,start,0);}
        pet_face_t hot={0},cold={0};
        for(uint64_t now=start;now<start+600;now+=33){
            if(stage==2){event(&warm,PET_EV_PLAYING,now,90);event(&neutral,PET_EV_PLAYING,now,90);}
            hot=frame(&warm,&a,now);cold=frame(&neutral,&b,now);
        }
        printf("warmth stage=%d body_height_delta=%d center_y_delta=%d eye_height_delta=%d\n",
            stage,hot.height-cold.height,center_y(cold)-center_y(hot),cold.eyes[0].height-hot.eyes[0].height);
        fflush(stdout);
        assert(hot.height-cold.height>=3);
        assert(center_y(cold)-center_y(hot)>=2);
        assert(cold.eyes[0].height-hot.eyes[0].height>=5);
        assert(warm.turn_warmth>34&&warm.turn_warmth<35);
    }
}
static void early_wait_visible(void){
    pet_state_t s;ready(&s);event(&s,PET_EV_PRESS,100,0);
    pet_motion_t motion={0};pet_face_t listening={0};
    for(uint64_t now=100;now<700;now+=33)listening=frame(&s,&motion,now);
    event(&s,PET_EV_RELEASE,700,0);
    int minimum=1000,maximum=-1000;pet_face_t early={0};
    for(uint64_t age=0;age<=990;age+=33){
        pet_face_t f=frame(&s,&motion,700+age);
        if(age>=297){if(f.rotation<minimum)minimum=f.rotation;if(f.rotation>maximum)maximum=f.rotation;early=f;}
    }
    printf("wait first_second rotation_range=%d eye_closure=%d listening_rotation_delta=%d\n",
        maximum-minimum,listening.eyes[0].height-early.eyes[0].height,abs(listening.rotation-early.rotation));
    fflush(stdout);
    assert(maximum-minimum>=25&&maximum-minimum<=55);
    assert(abs(early.eyes[0].height-early.eyes[1].height)>=10&&abs(early.eyes[0].height-early.eyes[1].height)<=20);
    assert(listening.eyes[0].height-early.eyes[0].height>=14&&listening.eyes[0].height-early.eyes[0].height<=26);
    assert(abs(listening.rotation-early.rotation)>=35);
    assert(s.wait_started_ms==700); /* The presentation does not restart the deadline. */
}
static void round_speech_levels(void){
    const int warmth[]={0,35,100},levels[]={0,90,500,1000};
    for(unsigned w=0;w<3;w++){
        pet_state_t s;ready(&s);s.turn_warmth=warmth[w];s.dialogue=PET_DIALOGUE_WAIT;
        pet_motion_t motion={0};
        for(uint64_t now=100;now<700;now+=33)frame(&s,&motion,now);
        int diameter=0;
        for(unsigned level=0;level<4;level++){
            uint64_t start=700+level*600;pet_face_t f={0};
            for(uint64_t now=start;now<start+600;now+=33){
                event(&s,PET_EV_PLAYING,now,levels[level]);
                pet_pose_t target=pet_pose(&s,now);
                assert(target.scale_x==target.scale_y);
                f=frame(&s,&motion,now);
                if(now-start>=330)assert(abs(f.width-f.height)<=1);
            }
            if(!level)diameter=f.width;
            assert(abs(f.width-diameter)<=1); /* Loudness cannot elongate the circle. */
        }
    }
}
static void quiet_speech_visible(void){
    round_speech_levels();
    pet_state_t speaking,silent;ready(&speaking);ready(&silent);
    speaking.dialogue=silent.dialogue=PET_DIALOGUE_SPEAK;
    pet_motion_t a={0},b={0};
    pet_motion_step(&a,resting(),999);pet_motion_step(&b,resting(),999);
    pet_face_t hot={0},cold={0};
    for(uint64_t now=1000;now<=1330;now+=33){
        event(&speaking,PET_EV_PLAYING,now,90);event(&silent,PET_EV_PLAYING,now,0);
        assert(fabsf(pet_pose(&speaking,now).level-.09f)<.000001f);
        hot=frame(&speaking,&a,now);cold=frame(&silent,&b,now);
    }
    printf("speech level90 body_height_delta=%d body_width_delta=%d center_y_delta=%d\n",
        hot.height-cold.height,cold.width-hot.width,center_y(cold)-center_y(hot));
    fflush(stdout);
    assert(hot.height==hot.width&&cold.height==cold.width);
    assert(hot.height==cold.height); /* Speech changes the eyes, not the round silhouette. */
    assert(cold.eyes[0].height-hot.eyes[0].height>=4&&cold.eyes[0].height-hot.eyes[0].height<=9);
    for(uint64_t now=1363;now<2500;now+=33){hot=frame(&speaking,&a,now);cold=frame(&silent,&b,now);}
    assert(abs(hot.height-cold.height)<=1&&abs(hot.width-cold.width)<=1);
    assert(abs(center_y(hot)-center_y(cold))<=1);
    assert(speaking.level==90&&silent.level==0); /* Visual gain never changes the audio level. */
    assert(pet_pose(&speaking,2500).level<.0001f);
}
static void spoken_finish_visible(void){
    pet_state_t s;ready(&s);event(&s,PET_EV_PRESS,100,0);event(&s,PET_EV_RELEASE,300,0);
    event(&s,PET_EV_PLAYING,800,90);event(&s,PET_EV_RESPONSE_DONE,900,0);event(&s,PET_EV_DRAIN,1000,0);
    assert(s.dialogue==PET_DIALOGUE_IDLE&&s.settle_until_ms==1900);
    pet_motion_t motion={0};pet_motion_step(&motion,resting(),999);
    int minimum_y=1000,maximum_y=-1000,minimum_eye=1000,maximum_eye=-1000,minimum_r=1000,maximum_r=-1000;
    for(uint64_t age=0;age<900;age+=33){
        pet_pose_t p=pet_pose(&s,1000+age);assert(p.expression==PET_EXPR_IDLE);
        pet_face_t f=frame(&s,&motion,1000+age);
        if(center_y(f)<minimum_y)minimum_y=center_y(f);
        if(center_y(f)>maximum_y)maximum_y=center_y(f);
        if(f.eyes[0].height<minimum_eye)minimum_eye=f.eyes[0].height;
        if(f.eyes[0].height>maximum_eye)maximum_eye=f.eyes[0].height;
        if(f.rotation<minimum_r)minimum_r=f.rotation;
        if(f.rotation>maximum_r)maximum_r=f.rotation;
        pet_readability_input_t in={.pose=p,.dialogue=s.dialogue,.connection=PET_CONN_READY,
            .caption_mode=PET_CAPTION_FULL,.caption="刚才的回答。"};
        pet_readability_text_t text=pet_readability_text(&in);
        assert(text.can_read&&!text.local&&!strcmp(text.summary,"刚才的回答。"));
    }
    printf("finish center_y_range=%d eye_height_range=%d rotation_range=%d\n",
        maximum_y-minimum_y,maximum_eye-minimum_eye,maximum_r-minimum_r);fflush(stdout);
    assert(maximum_y-minimum_y>=6&&maximum_y-minimum_y<=9);
    assert(maximum_eye-minimum_eye>=25&&maximum_eye-minimum_eye<=50);
    assert(maximum_r-minimum_r>=35&&maximum_r-minimum_r<=60);
    pet_pose_t end=pet_pose(&s,1899);
    assert(fabsf(end.offset_y)<.1f&&fabsf(end.rotation)<.1f&&fabsf(end.eye_open-1)<.01f);
    event(&s,PET_EV_PRESS,1500,0);
    assert(s.dialogue==PET_DIALOGUE_LISTEN&&s.settle_until_ms==0);
}
static void extremes_safe(void){
    pet_state_t s;ready(&s);s.turn_warmth=100;
    for(int stage=PET_DIALOGUE_LISTEN;stage<=PET_DIALOGUE_SPEAK;stage++){
        s.dialogue=(pet_dialogue_t)stage;s.wait_started_ms=100;
        pet_motion_t motion={0};
        for(uint64_t now=100;now<=12100;now+=33){
            if(stage==PET_DIALOGUE_SPEAK){s.level=1000;s.entered_ms=now;}
            pet_face_t f=frame(&s,&motion,now);
            assert(f.width>=180&&f.height<=222&&(double)f.height/f.width<=1.25);
            if(stage==PET_DIALOGUE_SPEAK)assert(f.width==f.height);
        }
    }
    s.dialogue=PET_DIALOGUE_IDLE;s.settle_until_ms=13000;s.level=0;
    pet_motion_t motion={0};
    for(uint64_t now=12100;now<13000;now+=33)frame(&s,&motion,now);
}
int main(int argc,char **argv){
    if(argc>1&&!strcmp(argv[1],"listen")){listening_distinct();listening_engaged();}
    else if(argc>1&&!strcmp(argv[1],"warmth"))warmth_visible();
    else if(argc>1&&!strcmp(argv[1],"wait"))early_wait_visible();
    else if(argc>1&&!strcmp(argv[1],"speech"))quiet_speech_visible();
    else if(argc>1&&!strcmp(argv[1],"finish"))spoken_finish_visible();
    else if(argc>1&&!strcmp(argv[1],"bounds"))extremes_safe();
    else return 2;
    puts("PET_VISIBLE_MOTION_OK");return 0;
}

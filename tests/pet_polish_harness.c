#include "pet_core.h"
#include "pet_face.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#ifndef PET_CONTACT_INPUT_VERSION
#define PET_EV_CONTACT ((pet_event_kind_t)(PET_EV_TURN_STATUS+1))
#define PET_CONTACT_DOWN 1
#define PET_CONTACT_MOVE 2
#define PET_CONTACT_UP 3
#define PET_CONTACT_CANCEL 4
#endif
static uint32_t contact(pet_state_t *s,int phase,int x,int y,uint64_t now){
    return pet_step(s,(pet_event_t){.kind=PET_EV_CONTACT,.now_ms=now,.value=(phase<<18)|(x<<9)|y});
}
static void event(pet_state_t *s,pet_event_kind_t kind,uint64_t now,int value){
    pet_step(s,(pet_event_t){.kind=kind,.now_ms=now,.value=value,.generation=s->generation,.turn=s->turn});
}
static void touch_reactions(void){
    pet_state_t s;pet_init(&s,0);
    uint32_t fx=contact(&s,PET_CONTACT_DOWN,150,150,100);
    pet_pose_t p=pet_pose(&s,100);assert(p.expression==PET_EXPR_CURIOUS&&p.look_x<0);
    assert((fx&PET_FX_WAKE)&&!(fx&(PET_FX_START|PET_FX_SUBMIT|PET_FX_SAVE_QUOTA)));
    pet_face_t face=pet_face_geometry(p);
    assert(face.eyes[0].x>=0&&face.eyes[0].y>=0);
    contact(&s,PET_CONTACT_MOVE,330,150,160);p=pet_pose(&s,160);
    assert(p.look_x>0&&p.expression==PET_EXPR_HAPPY&&p.eye_open<.5f);
    contact(&s,PET_CONTACT_UP,330,150,200);assert(pet_pose(&s,350).look_x>0);
    contact(&s,PET_CONTACT_CANCEL,0,0,360);assert(fabsf(pet_pose(&s,360).look_x)<.1f);
    contact(&s,PET_CONTACT_MOVE,400,200,370);assert(fabsf(pet_pose(&s,370).look_x)<.1f);

    pet_init(&s,0);
    for(unsigned i=0;i<3;i++){
        uint64_t now=100+i*200;
        contact(&s,PET_CONTACT_DOWN,300,180,now);contact(&s,PET_CONTACT_UP,300,180,now+20);
        event(&s,PET_EV_TOUCH_TAP,now+20,0);
    }
    assert(pet_pose(&s,700).scale_x<.98f); /* A gentle dodge after rapid pokes. */
    assert(pet_pose(&s,3000).scale_x>.98f);
    contact(&s,PET_CONTACT_DOWN,150,160,3100);event(&s,PET_EV_TICK,4200,0);
    assert(pet_pose(&s,4200).expression==PET_EXPR_IDLE); /* Missing UP cannot stick. */

    pet_init(&s,0);event(&s,PET_EV_TICK,120000,0);assert(s.life==PET_LIFE_SLEEP);
    assert(!(contact(&s,PET_CONTACT_DOWN,511,200,120001)&PET_FX_WAKE));assert(s.life==PET_LIFE_SLEEP);
    contact(&s,PET_CONTACT_DOWN,200,180,120002);assert(s.life==PET_LIFE_AWAKE);
    assert(pet_pose(&s,120202).scale_y>1.015f);
    for(uint64_t at=120002;at<121000;at+=17){
        p=pet_pose(&s,at);face=pet_face_geometry(p);
        assert(isfinite(p.scale_y)&&face.y>=0&&face.y+face.height<334);
        for(unsigned i=0;i<2;i++)assert(face.eyes[i].x>=0&&face.eyes[i].y>=0&&face.eyes[i].x+24<=face.width);
    }
}
static void voice_priority(void){
    pet_state_t s;pet_init(&s,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.generation=1,.value=PET_CONN_READY,.now_ms=1});
    event(&s,PET_EV_PRESS,2,0);contact(&s,PET_CONTACT_DOWN,140,140,3);
    contact(&s,PET_CONTACT_MOVE,330,140,70);assert(pet_pose(&s,70).expression==PET_EXPR_LISTEN);
    event(&s,PET_EV_RELEASE,202,0);assert(pet_pose(&s,202).expression==PET_EXPR_THINK);
    event(&s,PET_EV_PLAYING,300,700);assert(pet_pose(&s,300).expression==PET_EXPR_SPEAK);
    event(&s,PET_EV_TURN_STATUS,400,PET_NOTICE_NO_REPLY);
    contact(&s,PET_CONTACT_DOWN,100,100,401);assert(pet_pose(&s,401).expression==PET_EXPR_CONFUSED);
}
static void playing_pause(void){
    pet_state_t s;pet_init(&s,0);
    pet_step(&s,(pet_event_t){.kind=PET_EV_LINK,.generation=1,.value=PET_CONN_READY,.now_ms=1});
    event(&s,PET_EV_PRESS,2,0);event(&s,PET_EV_RELEASE,202,0);event(&s,PET_EV_PLAYING,300,800);
    assert(pet_pose(&s,300).level>.79f);
    assert(pet_pose(&s,800).level<.05f); /* A real playback pause must settle, not keep bouncing. */
    event(&s,PET_EV_PLAYING,900,600);assert(pet_pose(&s,900).level>.59f);
}
static void smooth_motion(void){
    pet_pose_t idle={.scale_x=1,.scale_y=1,.eye_open=1,.screen_on=true};
    pet_pose_t target=idle;target.rotation=20;target.look_x=8;target.look_y=-5;
    pet_motion_t one={0},many={0};
    pet_motion_step(&one,idle,0);pet_motion_step(&many,idle,0);
    pet_pose_t a=pet_motion_step(&one,target,90),b=idle;
    for(uint64_t at=10;at<=90;at+=10)b=pet_motion_step(&many,target,at);
    assert(fabsf(a.rotation-20*(1-expf(-1)))<.001f);
    assert(fabsf(a.rotation-b.rotation)<.001f&&fabsf(a.look_x-b.look_x)<.001f&&fabsf(a.look_y-b.look_y)<.001f);
    target.expression=PET_EXPR_LISTEN;target.dim=true;
    a=pet_motion_step(&one,target,90);assert(a.expression==PET_EXPR_LISTEN&&a.dim&&a.rotation<20);
    a=pet_motion_step(&one,idle,1000);assert(a.rotation==0&&a.eye_open==1);
    a=pet_motion_step(&one,target,10);assert(a.rotation==20); /* Clock rollback resets. */
    target.screen_on=false;a=pet_motion_step(&one,target,11);assert(!a.screen_on&&!one.initialized);
    a=pet_motion_step(&one,idle,12);assert(a.rotation==0&&a.screen_on);
    target=idle;target.expression=PET_EXPR_HAPPY;
    assert(!pet_face_geometry(target).eyes[0].arc);target.eye_open=.2f;assert(pet_face_geometry(target).eyes[0].arc);
    assert(pet_contact_value(PET_CONTACT_DOWN,-1,0)==-1&&pet_contact_value(PET_CONTACT_UP,466,0)==-1);
    assert(pet_contact_value((pet_contact_phase_t)0,0,0)==-1&&pet_contact_value(PET_CONTACT_MOVE,0,466)==-1);
}
int main(void){touch_reactions();voice_priority();playing_pause();smooth_motion();puts("PET_POLISH_OK");return 0;}

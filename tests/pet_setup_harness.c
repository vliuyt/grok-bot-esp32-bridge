#include "pet_setup_guard.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 pet_setup_guard_t s={0};pet_setup_open(&s,100,300000);
 assert(pet_setup_chunk(&s,101,false,"{}\n",3)==PET_SETUP_DENIED);
 assert(pet_setup_chunk(&s,102,true,"abc",3)==PET_SETUP_MORE);
 assert(pet_setup_chunk(&s,102,true,"\n",1)==PET_SETUP_READY);assert(!strcmp(s.bytes,"abc"));
 pet_setup_reset(&s);char large[2049]={0};assert(pet_setup_chunk(&s,200,true,large,sizeof(large))==PET_SETUP_INVALID);
 assert(pet_setup_chunk(&s,300,true,"x",1)==PET_SETUP_MORE);
 assert(pet_setup_chunk(&s,11000,true,"\n",1)==PET_SETUP_INVALID);
 assert(pet_setup_request(&s,"same"));pet_setup_result(&s,"same",false);
 assert(!pet_setup_request(&s,"same"));assert(s.result==-1);
 assert(pet_setup_request(&s,"other"));assert(!pet_setup_request(&s,"busy"));
 pet_setup_result(&s,"other",true);assert(s.result==1);
 assert(pet_setup_chunk(&s,300101,true,"{}\n",3)==PET_SETUP_DENIED);
 puts("PET_SETUP_OK");
}

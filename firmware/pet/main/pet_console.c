#include "pet_app.h"
#include "pet_ui.h"
#include "pet_provision.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
static TaskHandle_t console_task;
unsigned pet_console_stack_free(void){return console_task?(unsigned)uxTaskGetStackHighWaterMark(console_task):0;}

static void command(const char *line){
    char status[1100];
    if(!strcmp(line,"status")||!strcmp(line,"pet.status")){
        pet_app_status(status,sizeof(status),false);printf("@status %s\n",status);
    }else if(!strcmp(line,"grok.status")){
        pet_app_status(status,sizeof(status),true);printf("@grok %s\n",status);
    }else if(!strcmp(line,"grok.ptt down")||!strcmp(line,"grok.ptt up")){
        bool down=!strcmp(line,"grok.ptt down");
        bool ok=pet_app_post((pet_event_t){.kind=down?PET_EV_PRESS:PET_EV_RELEASE});
        printf("@grok.ptt %s\n",ok?"ok":"error");
    }else if(!strcmp(line,"pet.snapshot")){pet_ui_request_snapshot();}
    else if(!strcmp(line,"pet.page home")){pet_ui_bench_page(0);}
    else if(!strcmp(line,"pet.page settings")){pet_ui_bench_page(1);}
    else if(!strcmp(line,"pet.page connection")){pet_ui_bench_page(2);}
    else if(!strcmp(line,"pet.pet")){pet_app_post((pet_event_t){.kind=PET_EV_PET});printf("@pet.pet ok\n");}
    else if(!strcmp(line,"pet.touch down")||!strcmp(line,"pet.touch up")||!strcmp(line,"pet.touch lost")){
        int edge=!strcmp(line,"pet.touch down")?0:!strcmp(line,"pet.touch up")?1:2;
        printf("@pet.touch %s\n",pet_ui_bench_talk(edge)?"queued":"busy");
    }
    else if(!strcmp(line,"pet.setup open")){pet_provision_open(true);printf("@pet.setup ok\n");}
    else if(!strcmp(line,"pet.setup close")){pet_provision_open(false);printf("@pet.setup ok\n");}
    else if(!strcmp(line,"pet.setup status")){pet_provision_status(status,sizeof(status));printf("@pet.setup %s\n",status);}
    else if(!strncmp(line,"pet.configure ",14)){printf("@pet.configure %s\n",pet_provision_usb(line+14)?"accepted":"rejected");}
    else {printf("@pet.command error\n");}
    fflush(stdout);
}
static void serial_task(void *unused){
    (void)unused;char line[2048];size_t length=0;bool reading=false,overflow=false;
    for(;;){
        uint8_t ch;
        if(usb_serial_jtag_read_bytes(&ch,1,portMAX_DELAY)!=1)continue;
        if(ch=='\n'||ch=='\r'){
            if(reading&&!overflow){line[length]=0;command(line);memset(line,0,sizeof(line));}
            length=0;reading=false;overflow=false;
        }else if(ch=='>'&&!reading){reading=true;length=0;overflow=false;}
        else if(reading){if(ch==0){overflow=true;}else if(length+1<sizeof(line)){line[length++]=(char)ch;}else {overflow=true;}}
        else if(ch=='p'){pet_ui_request_snapshot();}
        else if(ch=='w'){pet_app_post((pet_event_t){.kind=PET_EV_PET});}
        else if(ch=='d'||ch=='u'){pet_app_post((pet_event_t){.kind=ch=='d'?PET_EV_PRESS:PET_EV_RELEASE});}
    }
}
void pet_console_start(void){
    usb_serial_jtag_driver_config_t config=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();config.rx_buffer_size=2048;
    /* Measured USB parsing chain exceeds 7 KiB before parser/RTOS headroom. */
    if(usb_serial_jtag_driver_install(&config)==ESP_OK){xTaskCreate(serial_task,"pet_console",12288,NULL,3,&console_task);}
}

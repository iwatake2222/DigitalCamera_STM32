/*
 * playbackCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"
#include "../hal/display.h"
#include "../service/file.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[PB_CTRL] " str, ##__VA_ARGS__);
#define PLAY_SIZE_WIDTH  320
#define PLAY_SIZE_HEIGHT 240

typedef enum {
  INACTIVE,
  ACTIVE,
} STATUS;


/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;
static uint16_t s_lineBuff[PLAY_SIZE_WIDTH];

/*** Internal Function Declarations ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void playbackCtrl_procInactive(MSG_STRUCT *p_msg);
static void playbackCtrl_procActive(MSG_STRUCT *p_msg);
static RET playbackCtrl_init();
static RET playbackCtrl_exit();
static RET playbackCtrl_playNext();
static RET playbackCtrl_isFileJPEG(char *filename);
static RET playbackCtrl_isFileRGB565(char *filename);
static RET playbackCtrl_playRGB565(filename);
static RET playbackCtrl_playJPEG(filename);

/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(PLAYBACK_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(s_status) {
      case INACTIVE:
        playbackCtrl_procInactive(p_recvMsg);
        break;
      case ACTIVE:
        playbackCtrl_procActive(p_recvMsg);
        break;
      }
      freeMemoryPoolMessage(p_recvMsg);
    }
  }
}

/*** Internal Function Defines ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_recvMmsg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_recvMmsg->sender), (uint32_t)p_sendMsg, osWaitForever);
}

static void playbackCtrl_procInactive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    s_status = ACTIVE;
    ret = playbackCtrl_init();
    playbackCtrl_sendComp(p_msg, ret);
    break;
  case CMD_STOP:
    LOG("status error\n");
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("status error\n");
    break;
  }
}

static void playbackCtrl_procActive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_STOP:
    s_status = INACTIVE;
    ret = playbackCtrl_exit();
    playbackCtrl_sendComp(p_msg, ret);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.status);
    if(p_msg->param.input.type == INPUT_TYPE_DIAL0) {
      playbackCtrl_playNext();
    }
    break;
  }
}

static RET playbackCtrl_init()
{
  /*** register input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** init display ***/
  display_init();
  display_drawRect(0, 0, 100, 100, DISPLAY_COLOR_RED);

  /*** init file ***/
  file_seekStart("/");

  /*** display the first image ***/
  playbackCtrl_playNext();

  return RET_OK;
}

static RET playbackCtrl_exit()
{
  /*** unregister input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** exit file ***/
  file_seekStop();

  return RET_OK;
}

static RET playbackCtrl_playNext()
{
  RET ret;
  char filename[16];
  ret = file_seekFileNext(filename);
  if(ret == RET_OK) {
    printf("play %s\n", filename);
    if(playbackCtrl_isFileRGB565(filename) == RET_OK) playbackCtrl_playRGB565(filename);
    if(playbackCtrl_isFileJPEG(filename) == RET_OK) playbackCtrl_playJPEG(filename);
  } else {
    /* reached the end of files, or just error occured */
    file_seekStop();
    file_seekStart("/");
  }

  return RET_OK;
}

static RET playbackCtrl_isFileRGB565(char *filename)
{
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'R') && (filename[i+2] == 'G') && (filename[i+3] == 'B') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_isFileJPEG(char *filename)
{
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'J') && (filename[i+2] == 'P') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_playRGB565(char* filename)
{
  uint32_t num;
  RET ret;
  ret = file_loadStart(filename);
  display_setArea(0, 0, PLAY_SIZE_WIDTH - 1, PLAY_SIZE_HEIGHT - 1);

  for(uint32_t i = 0; i < PLAY_SIZE_HEIGHT; i++){
    ret |= file_load(s_lineBuff, PLAY_SIZE_WIDTH*2, &num);
    // cannot use memcpy as it copies byte by byte (not 16bit)
    // todo: use DMA copy from mem to FSMC, while reading from SD card
    for(uint32_t x = 0; x < PLAY_SIZE_WIDTH; x++) {
      *((uint16_t*)display_getCanvasHandle()) = s_lineBuff[x];
    }
    if( ret != RET_OK || num != PLAY_SIZE_WIDTH*2) break;
  }
  ret |= file_loadStop();

  if(ret != RET_OK) LOG("Error\n");

  return ret;
}

static RET playbackCtrl_playJPEG(char* filename)
{
  printf("todo\n");
}

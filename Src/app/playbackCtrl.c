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
#define LOG(str, ...) printf("[PB_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[PB_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);
#define PLAY_SIZE_WIDTH  320
#define PLAY_SIZE_HEIGHT 240

typedef enum {
  INACTIVE,
  ACTIVE,
} STATUS;


/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;

/*** Internal Function Declarations ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void playbackCtrl_procInactive(MSG_STRUCT *p_msg);
static void playbackCtrl_procActive(MSG_STRUCT *p_msg);
static RET playbackCtrl_init();
static RET playbackCtrl_exit();
static RET playbackCtrl_playNext();
static RET playbackCtrl_isFileJPEG(char *filename);
static RET playbackCtrl_isFileRGB565(char *filename);
static RET playbackCtrl_playRGB565(char* filename);
static RET playbackCtrl_playJPEG(char* filename);

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
    ret = playbackCtrl_init();
    if(ret == RET_OK) s_status = ACTIVE;
    playbackCtrl_sendComp(p_msg, ret);
    break;
  case CMD_STOP:
    LOG_E("status error\n");
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_NOTIFY_INPUT:
    LOG_E("status error\n");
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
    LOG_E("status error\n");
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_STOP:
    ret = playbackCtrl_exit();
    if(ret == RET_OK) s_status = INACTIVE;
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
  RET ret;
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
  ret = display_init();
//  ret |= display_drawRect(0, 0, 100, 100, DISPLAY_COLOR_RED);
  if(ret != RET_OK) LOG_E("\n");

  /*** init file ***/
  ret = file_seekStart("/");
  if(ret != RET_OK) LOG_E("\n");

  /*** display the first image ***/
  ret = playbackCtrl_playNext();
  if(ret != RET_OK) LOG_E("\n");

  LOG("init %d\n", ret);

  return ret;
}

static RET playbackCtrl_exit()
{
  RET ret;
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
  ret = file_seekStop();

  LOG("exit %d\n", ret);

  return ret;
}

static RET playbackCtrl_playNext()
{
  RET ret;
  char filename[16];
  ret = file_seekFileNext(filename);
  if(ret == RET_OK) {
    LOG("play %s\n", filename);
    if(playbackCtrl_isFileRGB565(filename) == RET_OK) playbackCtrl_playRGB565(filename);
    if(playbackCtrl_isFileJPEG(filename) == RET_OK) playbackCtrl_playJPEG(filename);
  } else {
    /* reached the end of files, or just error occured */
    LOG("dir end\n");
    ret =file_seekStop();
    ret |= file_seekStart("/");
    if(ret != RET_OK)LOG_E("\n");
  }

  return ret;
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

  uint16_t* pLineBuff = pvPortMalloc(PLAY_SIZE_WIDTH*2);
  if(pLineBuff == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret = file_loadStart(filename);
  ret |= display_setArea(0, 0, PLAY_SIZE_WIDTH - 1, PLAY_SIZE_HEIGHT - 1);

  for(uint32_t i = 0; i < PLAY_SIZE_HEIGHT; i++){
    ret |= file_load(pLineBuff, PLAY_SIZE_WIDTH * 2, &num);
    display_drawBuffer(pLineBuff, num / 2);
    if( ret != RET_OK || num != PLAY_SIZE_WIDTH*2) break;
  }
  ret |= file_loadStop();

  if(ret != RET_OK) LOG_E("%d\n", ret);

  vPortFree(pLineBuff);

  return ret;
}

static RET playbackCtrl_playJPEG(char* filename)
{
  printf("todo\n");
}

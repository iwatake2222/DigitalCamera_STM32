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

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[PB_CTRL] " str, ##__VA_ARGS__);
typedef enum {
  INACTIVE,
  ACTIVE,
} STATUS;

/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;

/*** Internal Function Declarations ***/
static RET playbackCtrl_procInactive(MSG_STRUCT *p_msg);
static RET playbackCtrl_procActive(MSG_STRUCT *p_msg);

/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(PLAYBACK_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
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
static RET playbackCtrl_procInactive(MSG_STRUCT *p_msg)
{
  RET ret;
  if (p_msg->command == CMD_START) {
    s_status = ACTIVE;
    LOG("start\n");
    // todo: init process comes here
    ret = RET_OK;
  } else {
    LOG("status error\n");
    ret = RET_ERR_STATUS;
  }

  /* return comp */
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_msg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_msg->sender), (uint32_t)p_sendMsg, osWaitForever);
  return ret;
}

static RET playbackCtrl_procActive(MSG_STRUCT *p_msg)
{
  RET ret;
  if (p_msg->command == CMD_STOP) {
    s_status = INACTIVE;
    LOG("stop\n");
    // todo: init process comes here
    ret = RET_OK;
  } else {
    LOG("status error\n");
    ret = RET_ERR_STATUS;
  }

  /* return comp */
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_msg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_msg->sender), (uint32_t)p_sendMsg, osWaitForever);
  return ret;
}

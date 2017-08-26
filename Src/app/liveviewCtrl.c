/*
 * liveviewCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[LV_CTRL] " str, ##__VA_ARGS__);
typedef enum {
  INACTIVE,
  ACTIVE,
} STATUS;

/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;

/*** Internal Function Declarations ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void liveviewCtrl_procInactive(MSG_STRUCT *p_msg);
static void liveviewCtrl_procActive(MSG_STRUCT *p_msg);
static RET liveviewCtrl_init();
static RET liveviewCtrl_exit();

/*** External Function Defines ***/
void liveviewCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(LIVEVIEW_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(s_status) {
      case INACTIVE:
        liveviewCtrl_procInactive(p_recvMsg);
        break;
      case ACTIVE:
        liveviewCtrl_procActive(p_recvMsg);
        break;
      }
      freeMemoryPoolMessage(p_recvMsg);
    }
  }
}

/*** Internal Function Defines ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_recvMmsg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_recvMmsg->sender), (uint32_t)p_sendMsg, osWaitForever);
}

static void liveviewCtrl_procInactive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    s_status = ACTIVE;
    ret = liveviewCtrl_init();
    liveviewCtrl_sendComp(p_msg, ret);
    break;
  case CMD_STOP:
    LOG("status error\n");
    liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("status error\n");
    break;
  }
}

static void liveviewCtrl_procActive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_STOP:
    s_status = INACTIVE;
    ret = liveviewCtrl_exit();
    liveviewCtrl_sendComp(p_msg, ret);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.status);
    break;
  }
}

static RET liveviewCtrl_init()
{
  /*** register input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  return RET_OK;
}

static RET liveviewCtrl_exit()
{
  /*** unregister input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  return RET_OK;
}



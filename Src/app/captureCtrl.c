/*
 * captureCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[CAP_CTRL] " str, ##__VA_ARGS__);

/*** Internal Static Variables ***/

/*** Internal Function Declarations ***/
static RET captureCtrl_capture(MSG_STRUCT *p_msg);

/*** External Function Defines ***/
void captureCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(CAPTURE_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      captureCtrl_capture(p_recvMsg);
      freeMemoryPoolMessage(p_recvMsg);
    }
  }

}

/*** Internal Function Defines ***/
static RET captureCtrl_capture(MSG_STRUCT *p_msg)
{
  // todo capture

  LOG("CAPTURE DONE\n");
  /* return comp */
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = CAPTURE_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_msg->command);
  p_sendMsg->param.val = RET_OK;
  osMessagePut(getQueueId(p_msg->sender), (uint32_t)p_sendMsg, osWaitForever);
  return RET_OK;

}

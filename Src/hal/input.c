/*
 * input.c
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"

/*** Internal Const Values, Macros ***/
#define INPUT_TASK_INTERVAL 50
#define INPUT_MAX_REGISTER_NUM 2

#define LOG(str, ...) printf("[INPUT] " str, ##__VA_ARGS__);

/*** Internal Static Variables ***/
// initialized by 0 (dummy module id)
MODULE_ID s_registeredIdMode[INPUT_MAX_REGISTER_NUM];
MODULE_ID s_registeredIdCap[INPUT_MAX_REGISTER_NUM];
MODULE_ID s_registeredIdOther0[INPUT_MAX_REGISTER_NUM];
MODULE_ID s_registeredIdDial0[INPUT_MAX_REGISTER_NUM];

/*** Internal Function Declarations ***/
static void input_init();
static RET input_regist(MSG_STRUCT *p_msg);
static RET input_unregist(MSG_STRUCT *p_msg);
static void input_checkStatus();


/*** External Function Defines ***/
void input_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(INPUT);

  while(1) {
    RET ret;
    osEvent event = osMessageGet(myQueueId, INPUT_TASK_INTERVAL);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      if(p_recvMsg->command == CMD_REGISTER) {
        ret = input_regist(p_recvMsg);
      } else if(p_recvMsg->command == CMD_UNREGISTER) {
        ret = input_unregist(p_recvMsg);
      } else {
        ret = RET_ERR;
      }

      /* return comp */
      MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
      p_sendMsg->sender  = INPUT;
      p_sendMsg->command = COMMAND_COMP(p_recvMsg->command);
      p_sendMsg->param.val = ret;
      osMessagePut(getQueueId(p_recvMsg->sender), (uint32_t)p_sendMsg, osWaitForever);

      freeMemoryPoolMessage(p_recvMsg); // free received message
    } else if (event.status == osEventTimeout){
      input_checkStatus();
    }
  }

}

/*** Internal Function Defines ***/
static RET input_regist(MSG_STRUCT *p_msg)
{
  if (p_msg->param.input.type == INPUT_TYPE_KEY_MODE) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdMode[i] == 0) {
        s_registeredIdMode[i] = p_msg->sender;
        return RET_OK;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_KEY_CAP) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdCap[i] == 0) {
        s_registeredIdCap[i] = p_msg->sender;
        return RET_OK;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdOther0[i] == 0) {
        s_registeredIdOther0[i] = p_msg->sender;
        return RET_OK;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_DIAL0) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdDial0[i] == 0) {
        s_registeredIdDial0[i] = p_msg->sender;
        return RET_OK;
      }
    }
  }
  return RET_ERR;
}

static RET input_unregist(MSG_STRUCT *p_msg)
{
  RET ret = RET_ERR;
  if (p_msg->param.input.type == INPUT_TYPE_KEY_MODE) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdMode[i] == p_msg->sender) {
        ret = RET_OK;
        s_registeredIdMode[i] = 0;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_KEY_CAP) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdCap[i] == p_msg->sender) {
        ret = RET_OK;
        s_registeredIdCap[i] = 0;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdOther0[i] == p_msg->sender) {
        ret = RET_OK;
        s_registeredIdOther0[i] = 0;
      }
    }
  }
  if (p_msg->param.input.type == INPUT_TYPE_DIAL0) {
    for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
      if (s_registeredIdDial0[i] == p_msg->sender) {
        ret = RET_OK;
        s_registeredIdDial0[i] = 0;
      }
    }
  }
  return ret;
}


static void input_checkStatus()
{

}

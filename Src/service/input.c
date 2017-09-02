/*
 * input.c
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "main.h"
#include "common.h"
#include "commonMsg.h"
#include "stm32f4xx_hal.h"


extern TIM_HandleTypeDef htim5;

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[INPUT:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[INPUT_ERR:%d] " str, __LINE__, ##__VA_ARGS__);

#define INPUT_TASK_INTERVAL 50
#define INPUT_MAX_REGISTER_NUM 2
#define INPUT_DIAL0_TICK 2

/*** Internal Static Variables ***/
MODULE_ID s_registeredId[INPUT_TYPE_NUM][INPUT_MAX_REGISTER_NUM]; // initialized by 0 (dummy module id)
uint8_t  s_needNotify[INPUT_TYPE_NUM];
int16_t  s_dial0Sensitivity = 1;

/*** Internal Function Declarations ***/
static void input_init();
static RET input_regist(MSG_STRUCT *p_msg);
static RET input_unregist(MSG_STRUCT *p_msg);
static void input_checkStatus();
static void input_notify(INPUT_TYPE type, int16_t status);

/*** External Function Defines ***/
void input_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(INPUT);

  input_init();

  while(1) {
    RET ret;
    osEvent event = osMessageGet(myQueueId, INPUT_TASK_INTERVAL);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      if(p_recvMsg->command == CMD_REGISTER) {
        ret = input_regist(p_recvMsg);
      } else if(p_recvMsg->command == CMD_UNREGISTER) {
        ret = input_unregist(p_recvMsg);
      } else {
        ret = RET_ERR;
      }

      /* return comp */
      MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // receiver must free
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
  if (p_msg->param.input.type >= INPUT_TYPE_NUM) return RET_ERR_PARAM;

  for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
    if (s_registeredId[p_msg->param.input.type][i] == 0) {
      s_registeredId[p_msg->param.input.type][i] = p_msg->sender;
      if(p_msg->param.input.type == INPUT_TYPE_DIAL0) {
        s_dial0Sensitivity = (p_msg->param.input.param != 0) ? p_msg->param.input.param : 1;
      }
      return RET_OK;
    }
  }
  return RET_ERR;
}

static RET input_unregist(MSG_STRUCT *p_msg)
{
  RET ret = RET_ERR;
  if (p_msg->param.input.type >= INPUT_TYPE_NUM) return RET_ERR_PARAM;

  for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
    if (s_registeredId[p_msg->param.input.type][i] == p_msg->sender) {
      s_registeredId[p_msg->param.input.type][i] = 0;
      ret = RET_OK;
    }
  }
  return ret;
}

static void input_init()
{
  /* start timer to decode dial0 (rotary encoer) */
  HAL_TIM_Encoder_Start(&htim5, TIM_CHANNEL_ALL);
}

static void input_checkStatus()
{
  /* state history to cancel chattering. [0] = state(n-1), [1] = state(n-2) */
  static GPIO_PinState s_btnMode[2]   = {GPIO_PIN_SET, GPIO_PIN_SET};
  static GPIO_PinState s_btnCap[2]    = {GPIO_PIN_SET, GPIO_PIN_SET};
  static GPIO_PinState s_btnOther0[2] = {GPIO_PIN_SET, GPIO_PIN_SET};
  static uint32_t      s_dial0 = 0;
  GPIO_PinState btn;


  /* check mode button */
  btn = HAL_GPIO_ReadPin(BTN_MODE_GPIO_Port, BTN_MODE_Pin);
  if( (btn != s_btnMode[1]) && (btn == s_btnMode[0]) ){
    s_btnMode[1] = s_btnMode[0];
    if (btn == 0) input_notify(INPUT_TYPE_KEY_MODE, btn);
  }
  if(btn != s_btnMode[0]){
    s_btnMode[1] = s_btnMode[0];
    s_btnMode[0] = btn;
  }

  /* check cap button */
  btn = HAL_GPIO_ReadPin(BTN_CAP_GPIO_Port, BTN_CAP_Pin);
  if( (btn != s_btnCap[1]) && (btn == s_btnCap[0]) ){
    s_btnCap[1] = s_btnCap[0];
    if (btn == 0) input_notify(INPUT_TYPE_KEY_CAP, btn);
  }
  if(btn != s_btnCap[0]){
    s_btnCap[1] = s_btnCap[0];
    s_btnCap[0] = btn;
  }

  /* check other0 button */
  btn = HAL_GPIO_ReadPin(BTN_OTHER0_GPIO_Port, BTN_OTHER0_Pin);
  if( (btn != s_btnOther0[1]) && (btn == s_btnOther0[0]) ){
    s_btnOther0[1] = s_btnOther0[0];
    if (btn == 0) input_notify(INPUT_TYPE_KEY_OTHER0, btn);
  }
  if(btn != s_btnOther0[0]){
    s_btnOther0[1] = s_btnOther0[0];
    s_btnOther0[0] = btn;
  }

  /* check dial 0(rotary encoder) */
  uint32_t cnt = htim5.Instance->CNT;
  int16_t delta = (int16_t)(cnt - s_dial0) / (INPUT_DIAL0_TICK * s_dial0Sensitivity);
  if(delta != 0){
    input_notify(INPUT_TYPE_DIAL0, delta);
    s_dial0 = cnt;
  }

}

static void input_notify(INPUT_TYPE type, int16_t status)
{
  for(uint32_t i = 0; i < INPUT_MAX_REGISTER_NUM; i++) {
    if (s_registeredId[type][i] != 0) {
      MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // receiver must free
      p_sendMsg->sender  = INPUT;
      p_sendMsg->command = CMD_NOTIFY_INPUT;
      p_sendMsg->param.input.type   = type;
      p_sendMsg->param.input.param = status;
      osMessagePut(getQueueId(s_registeredId[type][i]), (uint32_t)p_sendMsg, osWaitForever);
    }
  }
}

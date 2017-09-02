/*
 * modeMgr.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonMsg.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[MODE:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[MODE_ERR:%d] " str, __LINE__, ##__VA_ARGS__);

typedef enum {
  MODE_BOOT = 0,
  MODE_LIVEVIEW,
  MODE_PLAYBACK,
  MODE_NUM,
} MODE;

typedef enum {
  ACT_START_LIVEVIEW = 0,
  ACT_STOP_LIVEVIEW,
  ACT_START_PLAYBACK,
  ACT_STOP_PLAYBACK,
  ACT_ENTER_LIVEVIEW_MODE,
  ACT_ENTER_PLAYBACK_MODE,
  ACT_END = -1,
} ACTION;

#define SEQUENCE_MAX 5

/*** Internal Static Variables ***/
// sequences when startup
static ACTION s_sequenceStart[SEQUENCE_MAX]= {
  ACT_START_LIVEVIEW, ACT_ENTER_LIVEVIEW_MODE, ACT_END, ACT_END, ACT_END, // on boot status
};

// sequences when mode button pressed
static ACTION s_sequenceMode[MODE_NUM][SEQUENCE_MAX] = {
  {ACT_END, ACT_END, ACT_END, ACT_END, ACT_END}, // on boot status
  {ACT_STOP_LIVEVIEW, ACT_START_PLAYBACK, ACT_ENTER_PLAYBACK_MODE, ACT_END, ACT_END}, // on liveview status
  {ACT_STOP_PLAYBACK, ACT_START_LIVEVIEW, ACT_ENTER_LIVEVIEW_MODE, ACT_END, ACT_END}, // on playback status
};

static ACTION   *sp_currentAction;      // current sequence (e.g. pointer to s_sequenceMode[1])
static uint32_t s_currentActionIndex;
static MODE s_currentMode = MODE_BOOT;

/*** Internal Function Declarations ***/
static RET modeMgr_registInput();
static RET modeMgr_setNewSequence(MSG_STRUCT* p_recvMsg);
static RET modeMgr_actSequence();
static RET modeMgr_recvComp(MSG_STRUCT *p_recvMsg);

/*** External Function Defines ***/
void modeMgr_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(MODE_MGR);

  modeMgr_registInput();

  /* change mode by default (boot -> liveview) */
  s_currentActionIndex = 0;
  sp_currentAction = &s_sequenceStart[s_currentActionIndex];
  modeMgr_actSequence();

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(p_recvMsg->command) {
      case CMD_NOTIFY_INPUT:
        if (modeMgr_setNewSequence(p_recvMsg) == RET_OK) {
          modeMgr_actSequence();
        }
        break;
      case COMMAND_COMP(CMD_STOP):
      case COMMAND_COMP(CMD_START):
        modeMgr_recvComp(p_recvMsg);
        break;
      default:
        // do nothing (comp from input may come here)
        break;
      }
      freeMemoryPoolMessage(p_recvMsg);
    }
  }
}

/*** Internal Function Defines ***/
static RET modeMgr_registInput()
{
  MSG_STRUCT *p_sendMsg;

  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // receiver must free
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = MODE_MGR;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_MODE;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  return RET_OK;
}


static RET modeMgr_setNewSequence(MSG_STRUCT* p_recvMsg)
{
  if (sp_currentAction != 0) {
    // do not change mode during another mode changing
    return RET_DO_NOTHING;
  }

  /* start new sequence to change mode */
  switch(p_recvMsg->param.input.type) {
  case INPUT_TYPE_KEY_MODE:
    sp_currentAction = &s_sequenceMode[s_currentMode][0];
    LOG("sequence start by mode button\n");
    break;
  default:
    return RET_DO_NOTHING;
  }

  s_currentActionIndex = 0;

  return RET_OK;
}

static RET modeMgr_actSequence()
{
  COMMAND command;
  osMessageQId destQueue;

  LOG("sequence %d start\n", s_currentActionIndex);

  switch(sp_currentAction[s_currentActionIndex]) {
  case ACT_START_LIVEVIEW:
    command = CMD_START;
    destQueue = getQueueId(LIVEVIEW_CTRL);
    break;
  case ACT_STOP_LIVEVIEW:
    command = CMD_STOP;
    destQueue = getQueueId(LIVEVIEW_CTRL);
    break;
  case ACT_START_PLAYBACK:
    command = CMD_START;
    destQueue = getQueueId(PLAYBACK_CTRL);
    break;
  case ACT_STOP_PLAYBACK:
    command = CMD_STOP;
    destQueue = getQueueId(PLAYBACK_CTRL);
    break;
  case ACT_ENTER_LIVEVIEW_MODE:
    s_currentMode = MODE_LIVEVIEW;
    sp_currentAction = 0;
    s_currentActionIndex = 0;
    LOG("all sequence done, new mode: LiveView\n");
    return RET_OK;  // sequence done
  case ACT_ENTER_PLAYBACK_MODE:
    s_currentMode = MODE_PLAYBACK;
    sp_currentAction = 0;
    s_currentActionIndex = 0;
    LOG("all sequence done, new mode: Playback\n");
    return RET_OK;  // sequence done
  case ACT_END:
    sp_currentAction = 0;
    s_currentActionIndex = 0;
    LOG("all sequence done\n");
    return RET_DO_NOTHING;   // sequence done
  }

  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // receiver must free
  p_sendMsg->command = command;
  p_sendMsg->sender  = MODE_MGR;
  p_sendMsg->param.val = 0;
  osMessagePut(destQueue, (uint32_t)p_sendMsg, osWaitForever);
  return RET_OK;
}

static RET modeMgr_recvComp(MSG_STRUCT *p_recvMsg)
{
  if( !IS_COMMAND_COMP(p_recvMsg->command) ) {
    /* ignore if unexpected message comes */
    return RET_DO_NOTHING;
  }

  if( p_recvMsg->param.val != RET_OK ) {
    // cancel sequence if error
    LOG_E("sequence stop due to error %d\n", p_recvMsg->param.val);
    sp_currentAction = 0;
    s_currentActionIndex = 0;
    return RET_ERR;
  }

  // keep on sequence
  LOG("sequence %d done\n", s_currentActionIndex);
  s_currentActionIndex++;
  modeMgr_actSequence();
  return RET_OK;
}

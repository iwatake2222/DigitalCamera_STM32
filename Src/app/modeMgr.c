/*
 * modeMgr.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[MODE] " str, ##__VA_ARGS__);

typedef enum {
  MODE_BOOT = 0,
  MODE_LIVEVIEW,
  MODE_PLAYBACK,
  MODE_CAPTURING,
  MODE_NUM,
} MODE;

typedef enum {
  ACT_START_LIVEVIEW = 0,
  ACT_STOP_LIVEVIEW,
  ACT_START_PLAYBACK,
  ACT_STOP_PLAYBACK,
  ACT_DO_CAPTURE,    // capture automatically stops and return comp when it's done
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
  {ACT_END, ACT_END, ACT_END, ACT_END, ACT_END}, // on capturing status
};

// sequences when capture button pressed
static ACTION s_sequenceCapture[MODE_NUM][SEQUENCE_MAX] = {
  {ACT_END, ACT_END, ACT_END, ACT_END, ACT_END}, // on boot status
  {ACT_STOP_LIVEVIEW, ACT_DO_CAPTURE, ACT_START_LIVEVIEW, ACT_ENTER_LIVEVIEW_MODE, ACT_END}, // on liveview status
  {ACT_END, ACT_END, ACT_END, ACT_END, ACT_END}, // on playback status
  {ACT_END, ACT_END, ACT_END, ACT_END, ACT_END}, // on capturing status
};

static ACTION *sp_currentAction;
static MODE s_currentMode = MODE_BOOT;

/*** Internal Function Declarations ***/
static RET modeMgr_registInput();
static RET modeMgr_setNewSequence(MSG_STRUCT* p_recvMsg);
static RET modeMgr_doSequence();
static RET modeMgr_doSequenceComp(MSG_STRUCT *p_recvMsg);

/*** External Function Defines ***/
void modeMgr_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(MODE_MGR);

  modeMgr_registInput();

  /* change mode by default (boot -> liveview) */
  sp_currentAction = &s_sequenceStart[0];
  modeMgr_doSequence();

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(p_recvMsg->command) {
      case CMD_NOTIFY_INPUT:
        if (modeMgr_setNewSequence(p_recvMsg) == RET_OK) {
          modeMgr_doSequence();
        }
        break;
      case COMMAND_COMP(CMD_STOP):
      case COMMAND_COMP(CMD_START):
      case COMMAND_COMP(CMD_CAPTURE):
        modeMgr_doSequenceComp(p_recvMsg);
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
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = MODE_MGR;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_MODE;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = MODE_MGR;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_CAP;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  return RET_OK;
}


static RET modeMgr_setNewSequence(MSG_STRUCT* p_recvMsg)
{
  if (sp_currentAction != 0) {
    // do not change mode during another mode changing
    return RET_DO_NOTHING;
  }


  switch(p_recvMsg->param.input.type) {
  case INPUT_TYPE_KEY_MODE:
    sp_currentAction = &s_sequenceMode[s_currentMode][0];
    break;
  case INPUT_TYPE_KEY_CAP:
    sp_currentAction = &s_sequenceCapture[s_currentMode][0];
    break;
  default:
    return RET_DO_NOTHING;
  }


  return RET_OK;
}

static RET modeMgr_doSequence()
{
  COMMAND command;
  osMessageQId destQueue;

  switch(*sp_currentAction) {
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
  case ACT_DO_CAPTURE:
    command = CMD_CAPTURE;
    destQueue = getQueueId(CAPTURE_CTRL);
    break;
  case ACT_ENTER_LIVEVIEW_MODE:
    s_currentMode = MODE_LIVEVIEW;
    sp_currentAction = 0;
    LOG("new mode: LiveView\n");
    return RET_OK;  // sequence done
  case ACT_ENTER_PLAYBACK_MODE:
    s_currentMode = MODE_PLAYBACK;
    sp_currentAction = 0;
    LOG("new mode: Playback\n");
    return RET_OK;  // sequence done
  case ACT_END:
    sp_currentAction = 0;
    return RET_DO_NOTHING;   // sequence done
  }

  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = command;
  p_sendMsg->sender  = MODE_MGR;
  p_sendMsg->param.val = 0;
  osMessagePut(destQueue, (uint32_t)p_sendMsg, osWaitForever);
  return RET_OK;
}

static RET modeMgr_doSequenceComp(MSG_STRUCT *p_recvMsg)
{
  if( !IS_COMMAND_COMP(p_recvMsg->command) ) {
    /* ignore if unexpected message comes */
    return RET_DO_NOTHING;
  }

  if( p_recvMsg->param.val != RET_OK ) {
    // cancel sequence if error
    sp_currentAction = 0;
    return RET_ERR;
  }

  // keep on sequence
  sp_currentAction++;
  modeMgr_doSequence();
  return RET_OK;
}

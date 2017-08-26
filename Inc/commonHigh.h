/*
 * commonHigh.h
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */

#ifndef COMMONHIGH_H_
#define COMMONHIGH_H_

typedef enum {
  MODE_MGR,
  LIVEVIEW_CTRL,
  PLAYBACK_CTRL,
  CAPTURE_CTRL,
  INPUT,
} MODULE_ID;


typedef enum {
  CMD_START,
  CMD_STOP,
  CMD_CAPTURE,
  CMD_REGIST,
  CMD_UNREGIST,
  CMD_NOTIFY_INPUT,
} COMMAND;

#define COMMAND_COMP(cmd) (cmd | 0x80000000)
#define IS_COMMAND_COMP(cmd) ((cmd & 0x80000000) == 0x80000000)

typedef enum {
  INPUT_TYPE_KEY_MODE = 0,
  INPUT_TYPE_KEY_CAP,
  INPUT_TYPE_KEY_OTHER0,
  INPUT_TYPE_DIAL_0,
} INPUT_TYPE;


typedef struct {
  uint32_t  command;
  uint32_t  sender;
  union {
    uint32_t  val;
    struct {
      int16_t type;
      int16_t status;
    }input;
  }param;
} MSG_STRUCT;


osMessageQId getQueueId(MODULE_ID moduleId);
MSG_STRUCT *allocMemoryPoolMessage();
void freeMemoryPoolMessage(MSG_STRUCT *p_message);

#endif /* COMMONHIGH_H_ */

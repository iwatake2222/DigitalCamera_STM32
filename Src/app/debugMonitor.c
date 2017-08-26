/*
 * debugMonitor.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */

/*** Internal Const Values, Macros ***/
#define DEBUG_MONITOR_BUFFER_SIZE 32
#define DEBUG_MONITOR_ARGC_SIZE 4

#include "main.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "commonHigh.h"
#include "ff.h"
#include "../driver/ov7670/ov7670.h"



typedef struct {
  char* cmd;
  RET (*func)(char *argv[], uint32_t argc);
} DEBUG_MON_COMMAND;


static RET fatfs(char *argv[], uint32_t argc)
{
  FATFS FatFs;  // should allocate heap, but use stack as it is debug code
  FIL Fil;      // should allocate heap, but use stack as it is debug code
  FRESULT ret;
  uint32_t n;
  uint8_t buff[4];

  ret = f_mount(&FatFs, "", 0);
  printf("f_mount: %d\n", ret);

  ret = f_mkdir("aaa");
  printf("f_mkdir: %d\n", ret);

  ret = f_open(&Fil, "test1.txt", FA_WRITE);
  printf("f_open: %d\n", ret);

  ret = f_write(&Fil, "abc", 4, &n);
  printf("f_write: %d\n", ret);

  ret = f_close(&Fil);
  printf("f_close: %d\n", ret);

  ret = f_open(&Fil, "test1.txt", FA_READ);
  printf("f_open: %d\n", ret);

  ret = f_read(&Fil, buff, 4, &n);
  printf("f_read: %d, %s\n", ret, buff);

  ret = f_mount(0, "", 0);
  printf("f_mount: %d\n", ret);

  return RET_OK;
}

static RET led(char *argv[], uint32_t argc)
{
  uint32_t onoff = atoi(argv[0]);
  if(onoff == 0) {
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, GPIO_PIN_RESET);
  } else {
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, GPIO_PIN_SET);
  }
  return RET_OK;
}

static RET cap(char *argv[], uint32_t argc)
{
  uint32_t start = atoi(argv[0]);
  if(start == 1) {
    ov7670_startCap(OV7670_CAP_CONTINUOUS, lcdIli9341_getDrawAddress());
  } else if(start == 2) {
    ov7670_startCap(OV7670_CAP_SINGLE_FRAME, lcdIli9341_getDrawAddress());
  } else {
    ov7670_stopCap();
  }
  return RET_OK;
}

static RET mode(char *argv[], uint32_t argc)
{
  uint32_t key = atoi(argv[0]);

  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = INPUT;
  p_sendMsg->command = CMD_NOTIFY_INPUT;
  if(key == 0) {
    p_sendMsg->param.input.type   = (int16_t)INPUT_TYPE_KEY_MODE;
  } else if(key == 1) {
    p_sendMsg->param.input.type   = (int16_t)INPUT_TYPE_KEY_CAP;
  }
  p_sendMsg->param.input.status   = 1;

  osMessagePut(getQueueId(MODE_MGR), (uint32_t)p_sendMsg, 0);
  return RET_OK;
}

static RET test1(char *argv[], uint32_t argc)
{
  printf("test1\n");
  printf("argc = %d\n", argc);
  for (uint32_t i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  printf("a %d\n", atoi(argv[0]));
  return RET_OK;
}

static RET test2(char *argv[], uint32_t argc)
{
  printf("test2\n");
  printf("argc = %d\n", argc);
  for (uint32_t i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  return RET_OK;
}

DEBUG_MON_COMMAND s_debugCommands[] = {
  {"fatfs", fatfs},
  {"led",   led},
  {"cap",   cap},
  {"mode",  mode},
  {"test1", test1},
  {"test2", test2},
  {(void*)0, (void*)0},
};

void debugMonitorDo()
{
  static char s_storedCommand[DEBUG_MONITOR_BUFFER_SIZE];
  static uint32_t s_storedCommandIndex = 0;
  if (uartTerminal_recvTry(&s_storedCommand[s_storedCommandIndex]) == RET_OK) {
    /* echo back */
    putchar(s_storedCommand[s_storedCommandIndex]);

    /* check if one line is done */
    if (s_storedCommand[s_storedCommandIndex] == '\n' || s_storedCommand[s_storedCommandIndex] == '\r' || s_storedCommandIndex == DEBUG_MONITOR_BUFFER_SIZE-1) {
      /* split input command */
      char *argv[DEBUG_MONITOR_ARGC_SIZE] = {0};
      uint32_t argc = 0;
      argv[argc++] = &s_storedCommand[0];
      for (uint32_t i = 2; i <= s_storedCommandIndex; i++) {
        if (s_storedCommand[i] == '\r' || s_storedCommand[i] == '\n') {
          s_storedCommand[i] = '\0';
          break;
        }
        if (s_storedCommand[i] == ' ') {
          s_storedCommand[i] = '\0';
          argv[argc++] = &s_storedCommand[i+1];
          if (argc == DEBUG_MONITOR_ARGC_SIZE) break;
        }
      }

      /* call corresponding debug command */
      RET ret = RET_ERR;
      for (uint32_t i = 0; s_debugCommands[i].cmd != (void*)0; i++) {
        if (strcmp(s_debugCommands[i].cmd, argv[0]) == 0) {
          ret = s_debugCommands[i].func(&argv[1], argc-1);
          printf(">");
        }
      }
      if (ret != RET_OK) debugMonitorShow();
      s_storedCommandIndex = 0;
    } else {
      s_storedCommandIndex++;
    }
  }
}

void debugMonitorShow()
{
  printf("\nCommand List:\n");
  for (uint32_t i = 0; s_debugCommands[i].cmd != (void*)0; i++) {
    printf("%s\n", s_debugCommands[i].cmd);
  }
  printf(">");
}

void debugMonitor_task(void const * argument)
{
  while(1) {
    debugMonitorDo();
    osDelay(1);
  }
}



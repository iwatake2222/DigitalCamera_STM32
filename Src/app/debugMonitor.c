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
#include "../driver/ov7670/ov7670.h"

typedef struct {
  char* cmd;
  RET (*func)(char *argv[], uint32_t argc);
} DEBUG_MON_COMMAND;


static RET led(char *argv[], uint32_t argc)
{
  uint32_t onoff = atoi(argv[0]);
  if(onoff == 0) {
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, GPIO_PIN_RESET);
  } else {
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, GPIO_PIN_SET);
  }
  return OK;
}

static RET cap(char *argv[], uint32_t argc)
{
  uint32_t start = atoi(argv[0]);
  if(start == 1) {
    ov7670_startCap(OV7670_CAP_CONTINUOUS);
  } else if(start == 2) {
    ov7670_startCap(OV7670_CAP_SINGLE_FRAME);
  } else {
    ov7670_stopCap();
  }
  return OK;
}

static RET test1(char *argv[], uint32_t argc)
{
  printf("test1\n");
  printf("argc = %d\n", argc);
  for (uint32_t i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  printf("a %d\n", atoi(argv[0]));
  return OK;
}

static RET test2(char *argv[], uint32_t argc)
{
  printf("test2\n");
  printf("argc = %d\n", argc);
  for (uint32_t i = 0; i < argc; i++) {
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  return OK;
}

DEBUG_MON_COMMAND s_debugCommands[] = {
  {"led",   led},
  {"cap",   cap},
  {"test1", test1},
  {"test2", test2},
  {(void*)0, (void*)0},
};

void debugMonitorDo()
{
  static char s_storedCommand[DEBUG_MONITOR_BUFFER_SIZE];
  static uint32_t s_storedCommandIndex = 0;
  if (uartTerminal_recvTry(&s_storedCommand[s_storedCommandIndex]) == OK) {
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
      RET ret = ERR;
      for (uint32_t i = 0; s_debugCommands[i].cmd != (void*)0; i++) {
        if (strcmp(s_debugCommands[i].cmd, argv[0]) == 0) {
          ret = s_debugCommands[i].func(&argv[1], argc-1);
          printf(">");
        }
      }
      if (ret != OK) debugMonitorShow();
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


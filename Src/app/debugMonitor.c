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
#include "commonMsg.h"
#include "ff.h"
#include "jpeglib.h"
#include "../driver/ov7670/ov7670.h"



typedef struct {
  char* cmd;
  RET (*func)(char *argv[], uint32_t argc);
} DEBUG_MON_COMMAND;

static RET enc(char *argv[], uint32_t argc)
{
  FATFS *p_fatFs = pvPortMalloc(sizeof(FATFS));
  FIL *p_fil = pvPortMalloc(sizeof(FIL));
  struct jpeg_compress_struct* p_cinfo = pvPortMalloc(sizeof(struct jpeg_compress_struct));
  struct jpeg_error_mgr* p_jerr = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  uint8_t *p_LineBuffRGB888 = pvPortMalloc(320*3);
  JSAMPROW buffer[2] = {0};

  buffer[0] = p_LineBuffRGB888;

  for(uint32_t x = 0; x < 320; x++){
    p_LineBuffRGB888[x*3+0] = 0x00;
    p_LineBuffRGB888[x*3+1] = 0x00;
    p_LineBuffRGB888[x*3+2] = 0xff;
  }

  p_cinfo->err = jpeg_std_error(p_jerr);
  jpeg_create_compress(p_cinfo);

  f_mount(p_fatFs, "", 0);
  f_open(p_fil, "test.jpg", FA_WRITE | FA_CREATE_ALWAYS);
  jpeg_stdio_dest(p_cinfo, p_fil);

  p_cinfo->image_width = 320;
  p_cinfo->image_height = 240;
  p_cinfo->input_components = 3;
  p_cinfo->in_color_space = JCS_RGB;
  jpeg_set_defaults(p_cinfo);
  jpeg_set_quality(p_cinfo, 10, TRUE);
  jpeg_start_compress(p_cinfo, TRUE);
  for ( uint32_t y = 0; y < 240; y++ ) {
    jpeg_write_scanlines(p_cinfo, buffer, 1);
  }

  jpeg_finish_compress(p_cinfo);
  jpeg_destroy_compress(p_cinfo);
  f_close(p_fil);

  vPortFree(p_cinfo);
  vPortFree(p_jerr);
  vPortFree(p_LineBuffRGB888);
  vPortFree(p_fil);
  vPortFree(p_fatFs);

  f_mount(0, "", 0);

  return RET_OK;
}

static RET ls(char *argv[], uint32_t argc)
{
  FATFS *p_fatFs = pvPortMalloc(sizeof(FATFS));
  DIR *p_dir = pvPortMalloc(sizeof(DIR));
  FRESULT ret;
  FILINFO fileinfo;

  ret = f_mount(p_fatFs, "", 0);
  if(argc > 0) {
    ret = f_opendir(p_dir, argv[0]);
  } else {
    ret = f_opendir(p_dir, "/");
  }

  while(1){
    ret |= f_readdir(p_dir, &fileinfo);
    if (ret != FR_OK || fileinfo.fname[0] == 0) break;
    if (fileinfo.fname[0] == '.') continue;
    if ( (fileinfo.fattrib & AM_SYS) == AM_SYS ) continue;

    printf("%s", fileinfo.fname);
    if(fileinfo.fattrib & AM_DIR) printf("/");
    printf("\n");
  }

  ret |= f_closedir(p_dir);
  ret |= f_mount(0, "", 0);

  if(ret != RET_OK) printf("err: %d\n", ret);

  vPortFree(p_dir);
  vPortFree(p_fatFs);

  f_mount(0, "", 0);

  return RET_OK;
}

static RET fatfs(char *argv[], uint32_t argc)
{
  FATFS *p_fatFs = pvPortMalloc(sizeof(FATFS));
  FIL *p_fil = pvPortMalloc(sizeof(FIL));
  FRESULT ret;
  uint32_t n;
  uint8_t buff[4];

  ret = f_mount(p_fatFs, "", 0);
  printf("f_mount: %d\n", ret);

  ret = f_mkdir("aaa");
  printf("f_mkdir: %d\n", ret);

  ret = f_open(p_fil, "test1.txt", FA_WRITE | FA_CREATE_ALWAYS);
  printf("f_open: %d\n", ret);

  ret = f_write(p_fil, "abc", 4, &n);
  printf("f_write: %d\n", ret);

  ret = f_close(p_fil);
  printf("f_close: %d\n", ret);

  ret = f_open(p_fil, "test1.txt", FA_READ);
  printf("f_open: %d\n", ret);

  ret = f_read(p_fil, buff, 4, &n);
  printf("f_read: %d, %s\n", ret, buff);

  ret = f_mount(0, "", 0);
  printf("f_mount: %d\n", ret);

  vPortFree(p_fil);
  vPortFree(p_fatFs);

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
  p_sendMsg->param.input.param   = 1;

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
  {"enc", enc},
  {"ls", ls},
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



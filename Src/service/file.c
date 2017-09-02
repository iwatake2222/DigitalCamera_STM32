/*
 * file.c
 *
 *  Created on: 2017/08/27
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "main.h"
#include "common.h"
#include "commonMsg.h"
#include "stm32f4xx_hal.h"
#include "ff.h"

/*** Internal Const Values, Macros ***/

/*** Internal Static Variables ***/
static FATFS s_fatFs;
static DIR s_dir;
static FIL s_fil;
static uint8_t s_isInitDone = 0;

/*** Internal Function Declarations ***/

/*** External Function Defines ***/
RET file_init()
{
  FRESULT ret;
  f_mount(0, "", 0);
  ret = f_mount(&s_fatFs, "", 0);
  if(ret != FR_OK) return RET_ERR_FILE;
  s_isInitDone = 1;
  return RET_OK;
}
RET file_deinit()
{
  FRESULT ret;
  ret = f_mount(0, "", 0);
  if(ret != FR_OK) return RET_ERR_FILE;
  s_isInitDone = 0;
  return RET_OK;
}

RET file_seekStart(const char* path)
{
  FRESULT ret = 0;
  if(s_isInitDone == 0) ret = file_init();
  if(path == 0 || path[0] == 0) {
    ret |= f_opendir(&s_dir, "/");
  } else {
    ret |= f_opendir(&s_dir, path);
  }
  if(ret != FR_OK) return RET_ERR_FILE;
  return RET_OK;
}

RET file_seekStop()
{
  FRESULT ret;
  ret = f_closedir(&s_dir);
  if(ret != FR_OK) return RET_ERR_FILE;
  return RET_OK;
}

RET file_seekFileNext(char* filename)
{
  FRESULT ret;
  FILINFO fileinfo;
  while(1){
    ret = f_readdir(&s_dir, &fileinfo);
    if (ret != FR_OK) return RET_ERR_FILE;
    if (fileinfo.fname[0] == 0) return RET_NO_DATA;
    if (fileinfo.fname[0] == '.') continue;
    if ( (fileinfo.fattrib & AM_SYS) == AM_SYS ) continue;
    if (fileinfo.fattrib & AM_DIR) continue;

    strcpy(filename, fileinfo.fname);
    break;
  }
  return RET_OK;
}

RET file_loadStart(char* filename)
{
  FRESULT ret = 0;
  if(s_isInitDone == 0)ret = file_init();
  ret |= f_open(&s_fil, filename, FA_READ);

  if(ret != FR_OK) return RET_ERR_FILE;
  return RET_OK;
}

RET file_loadStop()
{
  FRESULT ret;
  ret = f_close(&s_fil);
  if(ret != FR_OK) return RET_ERR_FILE;
  return RET_OK;
}

// numByte must be <1024
RET file_load(void* destAddress, uint32_t numByte, uint32_t* p_numByte)
{
  FRESULT ret;
  ret = f_read(&s_fil, destAddress, numByte, (UINT*)p_numByte);
  if(ret != FR_OK) return RET_ERR_FILE;
  return RET_OK;
}

FIL* file_loadGetCurrentFil()
{
  return &s_fil;
}

/*** Internal Function Defines ***/

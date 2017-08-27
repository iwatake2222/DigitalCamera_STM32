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
#include "commonHigh.h"
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
  s_isInitDone = 1;
  return f_mount(&s_fatFs, "", 0);
}
RET file_deinit()
{
  s_isInitDone = 0;
  return f_mount(0, "", 0);
}

RET file_seekStart(const char* path)
{
  FRESULT ret;
  if(s_isInitDone == 0)file_init();
  if(path == 0 || path[0] == 0) {
    ret = f_opendir(&s_dir, "/");
  } else {
    ret = f_opendir(&s_dir, path);
  }
  return ret;
}

RET file_seekStop()
{
  FRESULT ret;
  ret |= f_closedir(&s_dir);
  return ret;
}

RET file_seekFileNext(char* filename)
{
  FRESULT ret;
  FILINFO fileinfo;
  while(1){
    FILINFO fileinfo;
    ret |= f_readdir(&s_dir, &fileinfo);
    if (ret != FR_OK) return RET_ERR;
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
  FRESULT ret;
  if(s_isInitDone == 0)file_init();
  uint32_t actualNum;
  ret = f_open(&s_fil, filename, FA_READ);

  return ret;
}

RET file_loadStop()
{
  return f_close(&s_fil);
}

RET file_load(void* destAddress, uint32_t numByte, uint32_t* p_numByte)
{
  FRESULT ret;
  uint32_t actualNum;
  ret = f_read(&s_fil, destAddress, numByte, &p_numByte);
  return ret;
}


/*** Internal Function Defines ***/

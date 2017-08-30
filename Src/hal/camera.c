/*
 * camera.c
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "main.h"
#include "common.h"
#include "stm32f4xx_hal.h"
#include "../driver/ov7670/ov7670.h"
#include "camera.h"

extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef hdma_dcmi;
extern I2C_HandleTypeDef hi2c2;

/*** Internal Const Values, Macros ***/
/*** Internal Static Variables ***/
/*** Internal Function Declarations ***/

/*** External Function Defines ***/
RET camera_init()
{
  return ov7670_init(&hdcmi, &hdma_dcmi, &hi2c2);
}

RET camera_config(uint32_t mode)
{
  uint32_t ov7670Mode;
  switch (mode){
  case CAMERA_MODE_QVGA_RGB565:
    ov7670Mode = OV7670_MODE_QVGA_RGB565;
    break;
  case CAMERA_MODE_QVGA_YUV:
    ov7670Mode = OV7670_MODE_QVGA_YUV;
    break;
  default:
    printf("camera mode %d is not supported\n", mode);
    return RET_ERR;
  }
  return ov7670_config(ov7670Mode);
}

RET camera_startCap(uint32_t capMode, void* destHandle)
{
  uint32_t ov7670CapMode;

  switch (capMode){
  case CAMERA_CAP_CONTINUOUS:
    ov7670CapMode = OV7670_CAP_CONTINUOUS;
    break;
  case CAMERA_CAP_SINGLE_FRAME:
    ov7670CapMode = OV7670_CAP_SINGLE_FRAME;
    break;
  default:
    printf("cap mode %d is not supported\n", capMode);
    return RET_ERR;
  }
  return ov7670_startCap(ov7670CapMode, (uint32_t)destHandle);
}

RET camera_stopCap()
{
  return ov7670_stopCap();
}

void camera_registerCallback(void (*cbHsync)(uint32_t h), void (*cbVsync)(uint32_t v))
{
  ov7670_registerCallback(cbHsync, cbVsync);
}

/*** Internal Function Defines ***/

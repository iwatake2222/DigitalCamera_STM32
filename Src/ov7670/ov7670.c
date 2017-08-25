/*
 * ov7670.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "main.h"
#include "stm32f4xx_hal.h"
#include "common.h"
#include "ov7670.h"
#include "ov7670Config.h"
#include "ov7670Reg.h"

/*** Internal Const Values, Macros ***/
#define DCMI_DR_ADDRESS       0x50050028

#define I2C_WAIT_EVENT(EVENT) {\
    for(uint32_t WAIT_NUM = 0; (WAIT_NUM <= I2C_TIMEOUT) && !I2C_CheckEvent(I2Cx, EVENT); WAIT_NUM++){\
      if(WAIT_NUM == I2C_TIMEOUT) {printf("err\n");return ERR_TIMEOUT;}\
    }\
}

/*** Internal Static Variables ***/
DCMI_HandleTypeDef *sp_hdcmi;
DMA_HandleTypeDef  *sp_hdma_dcmi;
I2C_HandleTypeDef *sp_hi2c;

/*** Internal Function Declarations ***/
static RET ov7670_write(uint8_t regAddr, uint8_t data);
static RET ov7670_read(uint8_t regAddr, uint8_t *data);
static RET ov7670_i2cRead(uint8_t slaveAddr, int8_t dataNum, uint8_t data[]);

/*** External Function Defines ***/
RET ov7670_init(DCMI_HandleTypeDef *p_hdcmi, DMA_HandleTypeDef *p_hdma_dcmi, I2C_HandleTypeDef *p_hi2c)
{
  sp_hdcmi = p_hdcmi;
  sp_hdma_dcmi = p_hdma_dcmi;
  sp_hi2c = p_hi2c;
  ov7670_write(0x12, 0x80);  // RESET
  HAL_Delay(30);

  uint8_t buffer[4];
  ov7670_read(0x0b, buffer);
  printf("[OV7670] dev id = %02X\n", buffer[0]);


  return OK;
}

RET ov7670_config(uint32_t mode)
{
  ov7670_stopCap();
  ov7670_write(0x12, 0x80);  // RESET
  HAL_Delay(30);
  for(int i = 0; OV7670_reg[i][0] != REG_BATT; i++) {
    ov7670_write(OV7670_reg[i][0], OV7670_reg[i][1]);
    HAL_Delay(1);
  }
  return OK;
}

RET ov7670_startCap(uint32_t capMode)
{
  ov7670_stopCap();
  if (capMode == OV7670_CAP_CONTINUOUS) {
    HAL_DCMI_Start_DMA(sp_hdcmi, DCMI_MODE_CONTINUOUS, lcdIli9341_getDrawAddress(), 320*120);
  } else if (capMode == OV7670_CAP_SINGLE_FRAME) {
    HAL_DCMI_Start_DMA(sp_hdcmi, DCMI_MODE_SNAPSHOT, lcdIli9341_getDrawAddress(), 320*120);
  }

  return OK;
}

RET ov7670_stopCap()
{
  HAL_DCMI_Stop(sp_hdcmi);
  HAL_Delay(30);
  return OK;
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  printf("FRAME %d\n", HAL_GetTick());
}

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  printf("VSYNC %d\n", HAL_GetTick());
  HAL_DMA_Start_IT(hdcmi->DMA_Handle, (uint32_t)&hdcmi->Instance->DR, (uint32_t)lcdIli9341_getDrawAddress(), 320*120);
}

/*** Internal Function Defines ***/
static RET ov7670_write(uint8_t regAddr, uint8_t data)
{
  HAL_StatusTypeDef ret;
  do {
    ret = HAL_I2C_Mem_Write(sp_hi2c, SLAVE_ADDR, regAddr, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
  } while (ret != HAL_OK);
  return OK;
}

static RET ov7670_read(uint8_t regAddr, uint8_t *data)
{
  HAL_StatusTypeDef ret;
  do {
    // HAL_I2C_Mem_Read doesn't work (because of SCCB protocol(doesn't have ack))? */
//    ret = HAL_I2C_Mem_Read(sp_hi2c, SLAVE_ADDR, regAddr, I2C_MEMADD_SIZE_8BIT, data, 1, 1000);
    ret = HAL_I2C_Master_Transmit(sp_hi2c, SLAVE_ADDR, &regAddr, 1, 100);
    ret = HAL_I2C_Master_Receive(sp_hi2c, SLAVE_ADDR, data, 1, 100);
  } while (ret != HAL_OK);
  return OK;
}



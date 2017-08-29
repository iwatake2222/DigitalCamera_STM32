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
#define OV7670_QVGA_WIDTH  320
#define OV7670_QVGA_HEIGHT 240

/*** Internal Static Variables ***/
static DCMI_HandleTypeDef *sp_hdcmi;
static DMA_HandleTypeDef  *sp_hdma_dcmi;
static I2C_HandleTypeDef  *sp_hi2c;
static uint16_t           *sp_destAddress;
static void (* s_cbHsync)(uint32_t h);
static void (* s_cbVsync)(uint32_t v);
static uint32_t s_currentH;
static uint32_t s_currentV;

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
  sp_destAddress = 0;

  HAL_GPIO_WritePin(CAMERA_RESET_GPIO_Port, CAMERA_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(CAMERA_RESET_GPIO_Port, CAMERA_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(50);

  ov7670_write(0x12, 0x80);  // RESET
  HAL_Delay(30);

  uint8_t buffer[4];
  ov7670_read(0x0b, buffer);
  printf("[OV7670] dev id = %02X\n", buffer[0]);

  s_cbHsync = 0;
  s_cbVsync = 0;
  s_currentH = 0;
  s_currentV = 0;

  return RET_OK;
}

RET ov7670_config(uint32_t mode)
{
  ov7670_capStop();
  ov7670_write(0x12, 0x80);  // RESET
  HAL_Delay(30);
  for(int i = 0; OV7670_reg[i][0] != REG_BATT; i++) {
    ov7670_write(OV7670_reg[i][0], OV7670_reg[i][1]);
    HAL_Delay(1);
  }
  return RET_OK;
}

RET ov7670_capStart(uint32_t capMode, uint16_t* destAddress)
{
  ov7670_capStop();
  if (capMode == OV7670_CAP_CONTINUOUS) {
    sp_destAddress = destAddress;
    HAL_DCMI_Start_DMA(sp_hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)destAddress, OV7670_QVGA_WIDTH*OV7670_QVGA_HEIGHT/2);
  } else if (capMode == OV7670_CAP_SINGLE_FRAME) {
    HAL_DCMI_Start_DMA(sp_hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)destAddress, OV7670_QVGA_WIDTH*OV7670_QVGA_HEIGHT/2);
  }
  return RET_OK;
}

void ov7670_writeExt(uint8_t reg, uint8_t data)
{
  ov7670_write(reg, data);
}

RET ov7670_capLine2MemStart(uint16_t* destAddress)
{
  ov7670_capStop();

  sp_hdma_dcmi->Instance->CR = (sp_hdma_dcmi->Instance->CR & 0xFFFFFAFF) | DMA_MINC_ENABLE | DMA_NORMAL;

//  ov7670_write(0x11, 0x08); // pre-scalar = 1/2
//  ov7670_write(0x13, 0x00);
//  ov7670_write(0x07, 0x0f);
//  ov7670_write(0x10, 0x00);
//  ov7670_write(0x04, 0x00);

//  ov7670_write(0x55, 0xff);

  HAL_DCMI_Start_DMA(sp_hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)destAddress, OV7670_QVGA_WIDTH/2);

  return RET_OK;
}

RET ov7670_capLine2MemNext(uint16_t* destAddress)
{
  /* start only DMA, because DCMI keeps running in one frame */
  HAL_DMA_Start_IT(sp_hdcmi->DMA_Handle, (uint32_t)&sp_hdcmi->Instance->DR, (uint32_t)destAddress, OV7670_QVGA_WIDTH/2);
  return RET_OK;
}

RET ov7670_capStop()
{
  HAL_DCMI_Stop(sp_hdcmi);
  HAL_Delay(30);
  return RET_OK;
}

void ov7670_registerCallback(void (*cbHsync)(uint32_t h), void (*cbVsync)(uint32_t v))
{
  s_cbHsync = cbHsync;
  s_cbVsync = cbVsync;
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
//  printf("FRAME %d\n", HAL_GetTick());
}

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  printf("VSYNC %d\n", HAL_GetTick());
  if(s_cbVsync)s_cbVsync(s_currentV);
  HAL_DMA_Start_IT(hdcmi->DMA_Handle, (uint32_t)&hdcmi->Instance->DR, (uint32_t)sp_destAddress, OV7670_QVGA_WIDTH*OV7670_QVGA_HEIGHT/2);
  s_currentV++;
  s_currentH = 0;
}

void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *hdcmi)
{
//  printf("HSYNC %d\n", HAL_GetTick());
  if(s_cbHsync)s_cbHsync(s_currentH);
  s_currentH++;
}


/*** Internal Function Defines ***/
static RET ov7670_write(uint8_t regAddr, uint8_t data)
{
  HAL_StatusTypeDef ret;
  do {
    ret = HAL_I2C_Mem_Write(sp_hi2c, SLAVE_ADDR, regAddr, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
  } while (ret != HAL_OK && 0);
  return ret;
}

static RET ov7670_read(uint8_t regAddr, uint8_t *data)
{
  HAL_StatusTypeDef ret;
  do {
    // HAL_I2C_Mem_Read doesn't work (because of SCCB protocol(doesn't have ack))? */
//    ret = HAL_I2C_Mem_Read(sp_hi2c, SLAVE_ADDR, regAddr, I2C_MEMADD_SIZE_8BIT, data, 1, 1000);
    ret = HAL_I2C_Master_Transmit(sp_hi2c, SLAVE_ADDR, &regAddr, 1, 100);
    ret |= HAL_I2C_Master_Receive(sp_hi2c, SLAVE_ADDR, data, 1, 100);
  } while (ret != HAL_OK && 0);
  return ret;
}



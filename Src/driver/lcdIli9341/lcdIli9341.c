/*
 * lcdIli9341.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "main.h"
#include "stm32f4xx_hal.h"
#include "common.h"
#include "lcdIli9341.h"
#include "lcdIli9341Config.h"

/*** Internal Const Values, Macros ***/
#define FSMC_ADDRESS  (0x60000000 + ((FSMC_NEx-1) << 26))
#define LCD_CMD_ADDR  (FSMC_ADDRESS)

#ifdef BIT_WIDTH_16
#define LCD_DATA_ADDR (FSMC_ADDRESS | 1 << (FSMC_Ax + 1))
#define LCD_CMD       (*((volatile uint16_t*) LCD_CMD_ADDR))
#define LCD_DATA      (*((volatile uint16_t*) LCD_DATA_ADDR))
#else
#define LCD_DATA_ADDR (FSMC_ADDRESS | 1 << (FSMC_Ax + 0))
#define LCD_CMD       (*((volatile uint8_t*) LCD_CMD_ADDR))
#define LCD_DATA      (*((volatile uint8_t*) LCD_DATA_ADDR))
#endif

/*** Internal Static Variables ***/

/*** Internal Function Declarations ***/
#ifdef BIT_WIDTH_16
static void lcdIli9341_writeData(uint16_t data);
static void lcdIli9341_writeCmd(uint16_t cmd);
#else
static void lcdIli9341_writeData(uint8_t data);
static void lcdIli9341_writeCmd(uint8_t cmd);
#endif
static void lcdIli9341_readData();

/*** External Function Defines ***/
void lcdIli9341_setArea(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  lcdIli9341_writeCmd(0x2a);
  lcdIli9341_writeData(xStart >> 8);
  lcdIli9341_writeData(xStart & 0xff);
  lcdIli9341_writeData(xEnd >> 8);
  lcdIli9341_writeData(xEnd & 0xff);

  lcdIli9341_writeCmd(0x2b);
  lcdIli9341_writeData(yStart >> 8);
  lcdIli9341_writeData(yStart & 0xff);
  lcdIli9341_writeData(yEnd >> 8);
  lcdIli9341_writeData(yEnd & 0xff);

  lcdIli9341_writeCmd(0x2c);
}

void lcdIli9341_setAreaRead(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  lcdIli9341_writeCmd(0x2a);
  lcdIli9341_writeData(xStart >> 8);
  lcdIli9341_writeData(xStart & 0xff);
  lcdIli9341_writeData(xEnd >> 8);
  lcdIli9341_writeData(xEnd & 0xff);

  lcdIli9341_writeCmd(0x2b);
  lcdIli9341_writeData(yStart >> 8);
  lcdIli9341_writeData(yStart & 0xff);
  lcdIli9341_writeData(yEnd >> 8);
  lcdIli9341_writeData(yEnd & 0xff);

  lcdIli9341_writeCmd(0x2e);

  // the first read is invalid
  lcdIli9341_readData();
}

void lcdIli9341_drawRect(uint16_t xStart, uint16_t yStart, uint16_t width, uint16_t height, uint16_t color)
{
  lcdIli9341_setArea(xStart, yStart, xStart + width - 1, yStart + height - 1);
  for( uint16_t y = 0; y < height; y++ ){
    for( uint16_t x = 0; x < width; x++ ){
//      lcdIli9341_writeData(color >> 8);
//      lcdIli9341_writeData(color);
      LCD_DATA = color;
    }
  }
}

inline uint16_t* lcdIli9341_getDrawAddress()
{
  return (uint16_t*)LCD_DATA_ADDR;
}

RET lcdIli9341_init()
{
  //  GPIO_SetBits(GPIO_RESET_PORT, GPIO_RESET_PIN);  delay(10);
  //  GPIO_ResetBits(GPIO_RESET_PORT, GPIO_RESET_PIN);  delay(10);
  //  GPIO_SetBits(GPIO_RESET_PORT, GPIO_RESET_PIN);  delay(10);

  lcdIli9341_writeCmd(0x01); //software reset
  HAL_Delay(50);
  lcdIli9341_writeCmd(0x11); //exit sleep
  HAL_Delay(50);

  lcdIli9341_writeCmd(0xB6);
  lcdIli9341_writeData(0x0A);
  lcdIli9341_writeData(0xC2);

  lcdIli9341_writeCmd(0x36);   // memory access control
  lcdIli9341_writeData(0x68);     // BGR -> seems RGB
//  lcdIli9341_writeData(0x60);     // RGB -> seems BGR

  lcdIli9341_writeCmd(0x3A); // pixel format
  lcdIli9341_writeData(0x55); //RGB565 (16bit)

  lcdIli9341_writeCmd(0xE0); //gamma
  lcdIli9341_writeData(0x10);
  lcdIli9341_writeData(0x10);
  lcdIli9341_writeData(0x10);
  lcdIli9341_writeData(0x08);
  lcdIli9341_writeData(0x0E);
  lcdIli9341_writeData(0x06);
  lcdIli9341_writeData(0x42);
  lcdIli9341_writeData(0x28);
  lcdIli9341_writeData(0x36);
  lcdIli9341_writeData(0x03);
  lcdIli9341_writeData(0x0E);
  lcdIli9341_writeData(0x04);
  lcdIli9341_writeData(0x13);
  lcdIli9341_writeData(0x0E);
  lcdIli9341_writeData(0x0C);

  lcdIli9341_writeCmd(0XE1); //gamma
  lcdIli9341_writeData(0x0C);
  lcdIli9341_writeData(0x23);
  lcdIli9341_writeData(0x26);
  lcdIli9341_writeData(0x04);
  lcdIli9341_writeData(0x0C);
  lcdIli9341_writeData(0x04);
  lcdIli9341_writeData(0x39);
  lcdIli9341_writeData(0x24);
  lcdIli9341_writeData(0x4B);
  lcdIli9341_writeData(0x03);
  lcdIli9341_writeData(0x0B);
  lcdIli9341_writeData(0x0B);
  lcdIli9341_writeData(0x33);
  lcdIli9341_writeData(0x37);
  lcdIli9341_writeData(0x0F);

  lcdIli9341_writeCmd(0x2a);//
  lcdIli9341_writeData(0x00);
  lcdIli9341_writeData(0x00);
  lcdIli9341_writeData(0x00);
  lcdIli9341_writeData(0xef);

  lcdIli9341_writeCmd(0x2b); //
  lcdIli9341_writeData(0x00);
  lcdIli9341_writeData(0x00);
  lcdIli9341_writeData(0x01);
  lcdIli9341_writeData(0x3f);

  lcdIli9341_writeCmd(0x29);
  HAL_Delay(10);
  lcdIli9341_writeCmd(0x2C);


//  lcdIli9341_drawRect(0, 0, LCD_ILI9342_WIDTH, LCD_ILI9342_HEIGHT, LCD_ILI9342_COLOR_RED);
//  lcdIli9341_drawRect(0, 0, LCD_ILI9342_WIDTH, LCD_ILI9342_HEIGHT, LCD_ILI9342_COLOR_GREEN);
//  lcdIli9341_drawRect(0, 0, LCD_ILI9342_WIDTH, LCD_ILI9342_HEIGHT, LCD_ILI9342_COLOR_BLUE);
//  lcdIli9341_drawRect(0, 0, 100, 100, LCD_ILI9342_COLOR_RED);
//  lcdIli9341_drawRect(100, 100, 50, 50, LCD_ILI9342_COLOR_GREEN);
//  lcdIli9341_drawRect(200, 100, 120, 140, LCD_ILI9342_COLOR_BLUE);
//
//  lcdIli9341_writeCmd(0xd3);
//  lcdIli9341_readData();
//  lcdIli9341_readData();
//  lcdIli9341_readData();
//  lcdIli9341_readData();

//  lcdIli9341_drawRect(0, 0, LCD_ILI9342_WIDTH, LCD_ILI9342_HEIGHT, 0xffff);
  lcdIli9341_drawRect(0, 0, LCD_ILI9342_WIDTH, LCD_ILI9342_HEIGHT, 0x0000);
  lcdIli9341_setArea(0, 0, LCD_ILI9342_WIDTH - 1, LCD_ILI9342_HEIGHT - 1);

  return RET_OK;
}

/*** Internal Function Defines ***/



#ifdef BIT_WIDTH_16
inline static void lcdIli9341_writeCmd(uint16_t cmd)
#else
inline static void lcdIli9341_writeCmd(uint8_t cmd)
#endif
{
  LCD_CMD = cmd;
}

#ifdef BIT_WIDTH_16
inline static void lcdIli9341_writeData(uint16_t data)
#else
inline static void lcdIli9341_writeData(uint8_t data)
#endif
{
  LCD_DATA = data;
}

inline static void lcdIli9341_readData()
{
#ifdef BIT_WIDTH_16
  uint16_t data = LCD_DATA;
//  printf("%04X\n", data);
#else
  uint8_t data = LCD_DATA;
  printf("%02X\n", data);
#endif
}


uint16_t convRGB565(uint8_t r, uint8_t g, uint8_t b)
{
  r >>= 3;
  g >>= 2;
  b >>= 3;
  uint8_t dataHigh = r << 3 | g >> 3;
  uint8_t dataLow = (g & 0x07) << 5 | b;
//  return dataHigh<< 8 | dataLow;
  return dataLow<< 8 | dataHigh;
}



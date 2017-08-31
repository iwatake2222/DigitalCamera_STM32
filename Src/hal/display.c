/*
 * display.c
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "main.h"
#include "common.h"
#include "stm32f4xx_hal.h"
#include "../driver/lcdIli9341/lcdIli9341.h"
#include "display.h"

/*** Internal Const Values, Macros ***/

/*** Internal Static Variables ***/
static uint16_t s_xStart, s_yStart, s_xEnd, s_yEnd;

/*** Internal Function Declarations ***/
static RET display_checkArea(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);

/*** External Function Defines ***/
RET display_init()
{
  return lcdIli9341_init();
}

RET display_setArea(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  if(display_checkArea(xStart, yStart, xEnd, yEnd) == RET_OK){
    lcdIli9341_setArea(xStart, yStart, xEnd, yEnd);
    s_xStart = xStart;  s_yStart = yStart;  s_xEnd = xEnd;  s_yEnd = yEnd;
    return RET_OK;
  }
  return RET_ERR_PARAM;
}

RET display_setAreaRead(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  if(display_checkArea(xStart, yStart, xEnd, yEnd) == RET_OK){
    lcdIli9341_setAreaRead(xStart, yStart, xEnd, yEnd);
    return RET_OK;
  }
  return RET_ERR_PARAM;
}

RET display_drawRect(uint16_t xStart, uint16_t yStart, uint16_t width, uint16_t height, uint16_t color)
{
  if(display_checkArea(xStart, yStart, xStart + width - 1, yStart + height - 1) == RET_OK){
    lcdIli9341_drawRect(xStart, yStart, width, height, color);
    return RET_OK;
  }
  return RET_ERR_PARAM;
}

void* display_getDisplayHandle()
{
  (void*)lcdIli9341_getDrawAddress();
}

uint32_t display_getPixelFormat()
{
  return DISPLAY_PIXEL_FORMAT_RGB565;
}


void display_writeImage(void* srcHandle, uint32_t pixelNum)
{
  uint16_t *p_srcBuff = srcHandle;
  volatile uint16_t *p_dstBuff = lcdIli9341_getDrawAddress();
  for(uint32_t x = 0; x < pixelNum; x++) {
    *p_dstBuff = *p_srcBuff;
    p_srcBuff++;
  }
}

inline void display_putPixelRGB565(uint16_t rgb565)
{
  volatile uint16_t *p_dstBuff = lcdIli9341_getDrawAddress();
  *p_dstBuff = rgb565;
}


void display_readImageRGB888(uint8_t *p_buff, uint32_t pixelNum)
{
  /* can I use DMA for this? */
  volatile uint16_t* p_lcdAddr = (volatile uint16_t* )(lcdIli9341_getDrawAddress());
  for(uint32_t x = 0; x < pixelNum / 2; x++){
    uint16_t data0 = *p_lcdAddr;
    uint16_t data1 = *p_lcdAddr;
    uint16_t data2 = *p_lcdAddr;
    p_buff[x*6 + 0] = data0 >> 8;
    p_buff[x*6 + 1] = data0 & 0x00FF;
    p_buff[x*6 + 2] = data1 >> 8;
    p_buff[x*6 + 3] = data1 & 0x00FF;
    p_buff[x*6 + 4] = data2 >> 8;
    p_buff[x*6 + 5] = data2 & 0x00FF;
  }
}

void display_osdMark(uint32_t osdType)
{
  switch(osdType) {
  case DISPLAY_OSD_TYPE_PLAY:
    // not yet
    break;
  case DISPLAY_OSD_TYPE_PAUSE:
    display_drawRect(LCD_ILI9342_WIDTH / 2 - 45, 35, 40, LCD_ILI9342_HEIGHT - 35 * 2, DISPLAY_COLOR_BLACK);
    display_drawRect(LCD_ILI9342_WIDTH / 2 + 35, 35, 40, LCD_ILI9342_HEIGHT - 35 * 2, DISPLAY_COLOR_BLACK);
    display_drawRect(LCD_ILI9342_WIDTH / 2 - 40, 40, 30, LCD_ILI9342_HEIGHT - 40 * 2, DISPLAY_COLOR_WHITE);
    display_drawRect(LCD_ILI9342_WIDTH / 2 + 40, 40, 30, LCD_ILI9342_HEIGHT - 40 * 2, DISPLAY_COLOR_WHITE);
    break;
  case DISPLAY_OSD_TYPE_STOP:
    display_drawRect(LCD_ILI9342_WIDTH / 2 - 45, LCD_ILI9342_HEIGHT / 2 - 45, 90, 90, DISPLAY_COLOR_BLACK);
    display_drawRect(LCD_ILI9342_WIDTH / 2 - 40, LCD_ILI9342_HEIGHT / 2 - 40, 80, 80, DISPLAY_COLOR_WHITE);
    break;
  case DISPLAY_OSD_TYPE_END:
    // not yet
    break;
  }
  display_setArea(s_xStart, s_yStart, s_xEnd, s_yEnd);
}

void display_osdBar(uint32_t level)
{
  const uint32_t MARGINE = 20;
  const uint32_t HEIGHT  = 40;
  display_drawRect(MARGINE,                         LCD_ILI9342_HEIGHT - MARGINE - HEIGHT,
                   LCD_ILI9342_WIDTH - 2 * MARGINE, HEIGHT,
                   DISPLAY_COLOR_BLACK);
  display_drawRect(MARGINE,                         LCD_ILI9342_HEIGHT - MARGINE - HEIGHT,
                   ((LCD_ILI9342_WIDTH - 2 * MARGINE) * level) / 100, HEIGHT,
                   DISPLAY_COLOR_BLUE);

  display_setArea(s_xStart, s_yStart, s_xEnd, s_yEnd);
}

/*** Internal Function Defines ***/
static RET display_checkArea(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  if( (xEnd < LCD_ILI9342_WIDTH) && (yEnd < LCD_ILI9342_HEIGHT) ){
    return RET_OK;
  }
  return RET_ERR_PARAM;
}


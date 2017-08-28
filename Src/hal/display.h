/*
 * display.h
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */

#ifndef HAL_DISPLAY_H_
#define HAL_DISPLAY_H_

#define DISPLAY_COLOR_RED    0xf800
#define DISPLAY_COLOR_GREEN  0x07e0
#define DISPLAY_COLOR_BLUE   0x001f

#define DISPLAY_PIXEL_FORMAT_RGB565 0

RET display_init();
RET display_setArea(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
RET display_drawRect(uint16_t xStart, uint16_t yStart, uint16_t width, uint16_t height, uint16_t color);
void* display_getDisplayHandle();
uint32_t display_getPixelFormat();
void display_drawBuffer(void* canvasHandle, uint32_t pixelNum);


#endif /* HAL_DISPLAY_H_ */

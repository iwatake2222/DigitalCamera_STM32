/*
 * camera.h
 *
 *  Created on: 2017/08/26
 *      Author: take-iwiw
 */

#ifndef HAL_CAMERA_H_
#define HAL_CAMERA_H_

#define CAMERA_MODE_QVGA_RGB565 0
#define CAMERA_MODE_QVGA_YUV    1

#define CAMERA_CAP_CONTINUOUS   0
#define CAMERA_CAP_SINGLE_FRAME 1

RET camera_init();
RET camera_config(uint32_t mode);
RET camera_startCap(uint32_t capMode, void* destHandle);
RET camera_stopCap();
void camera_registerCallback(void (*cbHsync)(uint32_t h), void (*cbVsync)(uint32_t v));

#endif /* HAL_CAMERA_H_ */

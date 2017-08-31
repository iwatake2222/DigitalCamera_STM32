/*
 * applicationSettings.h
 *
 *  Created on: 2017/08/30
 *      Author: take-iwiw
 */

#ifndef APPLICATIONSETTINGS_H_
#define APPLICATIONSETTINGS_H_

#define IMAGE_SIZE_WIDTH  320
#define IMAGE_SIZE_HEIGHT 240

#define MOTION_JPEG_FPS_MSEC     200   // target is 5fps
#define MOTION_JPEG_FPS_MSEC_EX  100   // play motion jpeg(not recorded by this device) as 10fps

#define JPEG_QUALITY 60   // 1 - 100

#define FILENAME_JPEG      "IMG000.JPG"
#define FILENAME_MOVIE     "IMG000.AVI"
#define FILENAME_NUM_POS  3       // index number start at 3 (e.g. filename = IMG + 000)

#define BLACK_CURTAIN_TIME 200

#endif /* APPLICATIONSETTINGS_H_ */

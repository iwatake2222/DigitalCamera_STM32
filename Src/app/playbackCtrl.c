/*
 * playbackCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"

/*** Internal Const Values, Macros ***/
#define LOG(str) {printf("[PB_CTRL] " str);}

/*** Internal Static Variables ***/

/*** Internal Function Declarations ***/

/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  while(1) {
    osDelay(1);
  }
}

/*** Internal Function Defines ***/

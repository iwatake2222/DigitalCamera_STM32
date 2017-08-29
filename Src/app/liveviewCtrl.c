/*
 * liveviewCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"
#include "../hal/display.h"
#include "../hal/camera.h"
#include "ff.h"
#include "jpeglib.h"

// todo delete
#include "stm32f4xx_hal.h"


/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[LV_CTRL] " str, ##__VA_ARGS__);
typedef enum {
  INACTIVE,
  ACTIVE,
  SINGLE_ENCODING,
  MOVIE_ENCODING,
  MOVIE_BUFFERING,

} STATUS;

/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;
static uint8_t s_requestStopMovie = 0;  // movie record will stop at next frame

static FATFS *sp_fatFs;
static FIL *sp_fil;
static struct jpeg_compress_struct* sp_cinfo;
static struct jpeg_error_mgr* sp_jerr;
static JSAMPROW s_jsamprow[2] = {0};
static uint8_t *sp_lineBuffRGB888;

/*** Internal Function Declarations ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void liveviewCtrl_procInactive(MSG_STRUCT *p_msg);
static void liveviewCtrl_procActive(MSG_STRUCT *p_msg);
static RET liveviewCtrl_init();
static RET liveviewCtrl_exit();
static RET liveviewCtrl_singleEncode();

static RET liveviewCtrl_jpegStart();
static RET liveviewCtrl_jpegFinish();

/*** External Function Defines ***/
void liveviewCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(LIVEVIEW_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(s_status) {
      case INACTIVE:
        liveviewCtrl_procInactive(p_recvMsg);
        break;
      case ACTIVE:
        liveviewCtrl_procActive(p_recvMsg);
        break;
      case SINGLE_ENCODING:
        // ignore any message during encoding
        break;
      case MOVIE_ENCODING:
      case MOVIE_BUFFERING:
        if( (p_recvMsg->command == CMD_NOTIFY_INPUT) && (p_recvMsg->param.input.type = INPUT_TYPE_KEY_OTHER0) ){
          // movie record will stop at next frame
          s_requestStopMovie = 1;
        }
      }
      freeMemoryPoolMessage(p_recvMsg);
    }
  }
}

/*** Internal Function Defines ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_recvMmsg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_recvMmsg->sender), (uint32_t)p_sendMsg, osWaitForever);
}

static void liveviewCtrl_procInactive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    s_status = ACTIVE;
    ret = liveviewCtrl_init();
    liveviewCtrl_sendComp(p_msg, ret);
    break;
  case CMD_STOP:
    LOG("status error\n");
    liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("status error\n");
    break;
  }
}

static void liveviewCtrl_procActive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_STOP:
    s_status = INACTIVE;
    ret = liveviewCtrl_exit();
    liveviewCtrl_sendComp(p_msg, ret);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.status);
    if(p_msg->param.input.type == INPUT_TYPE_KEY_CAP) {
      s_status = SINGLE_ENCODING;
      ret = liveviewCtrl_singleEncode();
      s_status = ACTIVE;
    } else if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
      s_status = MOVIE_ENCODING;
    }
    break;
  }
}

static RET liveviewCtrl_init()
{
  /*** register input ***/
  MSG_STRUCT *p_sendMsg;

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_CAP;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when dial0 is rotated */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** init display ***/
  display_init();
  uint32_t pixelFormat = display_getPixelFormat();
//  uint32_t size = display_getDisplaySize();
  display_setArea(0, 0, 320-1, 240-1);

  void* canvasHandle = display_getDisplayHandle();

  /*** init camera ***/
  camera_init();
  if (pixelFormat == DISPLAY_PIXEL_FORMAT_RGB565 ){
    camera_config(CAMERA_MODE_QVGA_RGB565);
  } else {
    LOG("not supported\n");
    return RET_ERR;
  }
  camera_startCap(CAMERA_CAP_CONTINUOUS, canvasHandle);
  return RET_OK;
}

static RET liveviewCtrl_exit()
{
  /*** unregister input ***/
  MSG_STRUCT *p_sendMsg;

  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_CAP;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when dial0 is rotated */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = LIVEVIEW_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** stop camera ***/
  camera_stopCap();

  return RET_OK;
}

static RET liveviewCtrl_singleEncode()
{
  LOG("Single Encode Start\n");
  uint16_t dummy;
  camera_stopCap();

//  lcdIli9341_setAreaRead(0, 4, 320-1, 240-1);
//  dummy = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
//  extern DMA_HandleTypeDef hdma_memtomem_dma2_stream0;
//  HAL_DMA_Start(&hdma_memtomem_dma2_stream0, lcdIli9341_getDrawAddress(), sp_lineBuffRGB888, 100);
//  HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, 100);

  uint32_t start = HAL_GetTick();

  liveviewCtrl_jpegStart();
  lcdIli9341_setAreaRead(0, 0, 320-1, 240-1);
  dummy = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
  for(uint32_t y = 0; y < 240; y++) {
    for(uint32_t x = 0; x < 320/2; x++){
      uint16_t data0 = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
      uint16_t data1 = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
      uint16_t data2 = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
      sp_lineBuffRGB888[x*6 + 0] = data0 >> 8;
      sp_lineBuffRGB888[x*6 + 1] = data0 & 0x00FF;
      sp_lineBuffRGB888[x*6 + 2] = data1 >> 8;
      sp_lineBuffRGB888[x*6 + 3] = data1 & 0x00FF;
      sp_lineBuffRGB888[x*6 + 4] = data2 >> 8;
      sp_lineBuffRGB888[x*6 + 5] = data2 & 0x00FF;
    }
    jpeg_write_scanlines(sp_cinfo, s_jsamprow, 1);
  }
  liveviewCtrl_jpegFinish();
  printf("time = %d\n", HAL_GetTick() - start);

  LOG("Single Encode End\n");
  return RET_OK;
}

static RET liveviewCtrl_jpegStart()
{
  sp_fatFs = pvPortMalloc(sizeof(FATFS));
  sp_fil = pvPortMalloc(sizeof(FIL));
  sp_cinfo = pvPortMalloc(sizeof(struct jpeg_compress_struct));
  sp_jerr = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  sp_lineBuffRGB888 = pvPortMalloc(320*3);

  s_jsamprow[0] = sp_lineBuffRGB888;

  sp_cinfo->err = jpeg_std_error(sp_jerr);
  jpeg_create_compress(sp_cinfo);

  f_mount(sp_fatFs, "", 0);
  f_open(sp_fil, "ghj.jpg", FA_WRITE | FA_CREATE_ALWAYS);
  jpeg_stdio_dest(sp_cinfo, sp_fil);

  sp_cinfo->image_width = 320;
  sp_cinfo->image_height = 240;
  sp_cinfo->input_components = 3;
  sp_cinfo->in_color_space = JCS_RGB;
  jpeg_set_defaults(sp_cinfo);
  jpeg_set_quality(sp_cinfo, 70, TRUE);
  jpeg_start_compress(sp_cinfo, TRUE);

}

static RET liveviewCtrl_jpegFinish()
{
  jpeg_finish_compress(sp_cinfo);
  jpeg_destroy_compress(sp_cinfo);
  f_close(sp_fil);

  vPortFree(sp_cinfo);
  vPortFree(sp_jerr);
  vPortFree(sp_lineBuffRGB888);
  vPortFree(sp_fil);
  vPortFree(sp_fatFs);

  f_mount(0, "", 0);
}

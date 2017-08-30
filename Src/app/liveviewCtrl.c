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
#define LOG(str, ...) printf("[LV_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[LV_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);
#define CAP_SIZE_WIDTH  320
#define CAP_SIZE_HEIGHT 240
#define CAP_MOTION_JPEG_FPS_MSEC  100   //(10fps)
#define CAP_NAME_JPEG      "IMG000.JPG"
#define CAP_NAME_MOVIE     "IMG000.AVI"
#define CAP_NAME_INDEX_POS  3       // index number start at 3 (e.g. filename = IMG + 000)

// need to modify jdatasrc.c
#define INPUT_BUF_SIZE  512  /* choose an efficiently fread'able size */

typedef enum {
  INACTIVE,
  ACTIVE,
  SINGLE_ENCODING,
  MOVIE_ENCODING,
} STATUS;

/*** Internal Static Variables ***/
/* for status control */
static STATUS s_status = INACTIVE;
static uint8_t s_requestStopMovie = 0;  // movie record will stop at next frame

/* foe encode */
static uint8_t *sp_lineBuffRGB888;
static FATFS *sp_fatFs;
static FIL    *sp_fil;
static struct jpeg_compress_struct *sp_cinfo;
static struct jpeg_error_mgr       *sp_jerr;
static JSAMPROW s_jsamprow[2] = {0};


/*** Internal Function Declarations ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void liveviewCtrl_processMsg(MSG_STRUCT *p_msg);
static void liveviewCtrl_processFrame();
static RET liveviewCtrl_init();
static RET liveviewCtrl_exit();
static RET liveviewCtrl_startLiveView();
static RET liveviewCtrl_stopLiveView();

static RET liveviewCtrl_singleRecord();

static RET liveviewCtrl_movieRecordStart();
static RET liveviewCtrl_movieRecordFinish();
static RET liveviewCtrl_movieRecordFrame();


static RET liveviewCtrl_writeFileStart(char* filename);
static RET liveviewCtrl_writeFileFinish();
static RET liveviewCtrl_encodeJpegStart();
static RET liveviewCtrl_encodeJpegFinish();

static RET liveviewCtrl_generateFilename(char* filename, uint8_t posIndex);

/*** External Function Defines ***/
void liveviewCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(LIVEVIEW_CTRL);

  while(1) {
    osEvent event;
    event = osMessageGet(myQueueId, 200);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      liveviewCtrl_processMsg(p_recvMsg);
      freeMemoryPoolMessage(p_recvMsg);
    } else if (event.status == osEventTimeout) {
      liveviewCtrl_processFrame();
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

static void liveviewCtrl_processMsg(MSG_STRUCT *p_msg)
{
  RET ret;
  switch(s_status) {
  case INACTIVE:
    if (IS_COMMAND_COMP(p_msg->command)) {
      // do nothing when comp (may be comp from input)
      return;
    }
    switch(p_msg->command){
    case CMD_START:
      s_status = ACTIVE;
      ret = liveviewCtrl_init();
      liveviewCtrl_sendComp(p_msg, ret);
      break;
    case CMD_STOP:
      LOG_E("status error\n");
      liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
      break;
    default:
      LOG_E("status error\n");
      break;
    }
    break;
  case ACTIVE:
    if (IS_COMMAND_COMP(p_msg->command)) {
      // do nothing when comp (may be comp from input)
      return;
    }
    switch(p_msg->command){
    case CMD_START:
      LOG_E("status error\n");
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
        liveviewCtrl_singleRecord();
        s_status = ACTIVE;
      } else if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
        s_status = MOVIE_ENCODING;
        liveviewCtrl_movieRecordStart();
      }
      break;
    default:
      LOG_E("status error\n");
      break;
    }
    break;
  case SINGLE_ENCODING:
    // there shouldn't be any message during this status
    break;
  case MOVIE_ENCODING:
    switch(p_msg->command){
    case CMD_START:
    case CMD_STOP:
      LOG_E("status error\n");
      liveviewCtrl_sendComp(p_msg, RET_ERR_STATUS);
      break;
    case CMD_NOTIFY_INPUT:
      if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0){
        // stop movie record at next frame
        s_requestStopMovie = 1;
      }
      break;
    default:
      LOG_E("status error\n");
      break;
    }
    break;
  }
}

static void liveviewCtrl_processFrame()
{
  if( s_status == MOVIE_ENCODING ){
    if(s_requestStopMovie) {
      liveviewCtrl_movieRecordFinish();
    } else {
      liveviewCtrl_movieRecordFrame();
    }
  }
}


static RET liveviewCtrl_init()
{
  RET ret = RET_OK;

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
  ret |= display_init();
  uint32_t pixelFormat = display_getPixelFormat();

  /*** init camera ***/
  ret |= camera_init();
  if (pixelFormat == DISPLAY_PIXEL_FORMAT_RGB565 ){
    camera_config(CAMERA_MODE_QVGA_RGB565);
  } else {
    LOG("not supported\n");
    return RET_ERR;
  }

  /*** start liveview ***/
  ret |= liveviewCtrl_startLiveView();

  return ret;
}

static RET liveviewCtrl_exit()
{
  RET ret = RET_OK;

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
  ret |= liveviewCtrl_stopLiveView();

  return ret;
}

static RET liveviewCtrl_startLiveView()
{
  RET ret = RET_OK;
  void* displayHandle = display_getDisplayHandle();
  display_setArea(0, 0, CAP_SIZE_WIDTH - 1, CAP_SIZE_HEIGHT - 1);
  ret |= camera_startCap(CAMERA_CAP_CONTINUOUS, displayHandle);
  return ret;
}

static RET liveviewCtrl_stopLiveView()
{
  RET ret = RET_OK;
  ret |= camera_stopCap();
  return ret;
}

static RET liveviewCtrl_singleRecord()
{
  LOG("Single Encode Start\n");
  RET ret = RET_OK;
  uint16_t dummy;
  char filename[14] = CAP_NAME_JPEG;

  ret |= liveviewCtrl_stopLiveView();

//  lcdIli9341_setAreaRead(0, 4, 320-1, 240-1);
//  dummy = *((volatile uint16_t* )(lcdIli9341_getDrawAddress()));
//  extern DMA_HandleTypeDef hdma_memtomem_dma2_stream0;
//  HAL_DMA_Start(&hdma_memtomem_dma2_stream0, lcdIli9341_getDrawAddress(), sp_lineBuffRGB888, 100);
//  HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, 100);

  uint32_t start = HAL_GetTick();

  ret |= liveviewCtrl_generateFilename(filename, CAP_NAME_INDEX_POS);
  LOG("create %s\n", filename);
  ret |= liveviewCtrl_writeFileStart(filename);
  ret |= liveviewCtrl_encodeJpegStart();
  if(ret != RET_OK) {
    ret |= liveviewCtrl_encodeJpegFinish();
    ret |= liveviewCtrl_writeFileFinish();
    LOG_E("Single Encode End by error: %08X\n", ret);
    return ret;
  }
  lcdIli9341_setAreaRead(0, 0, CAP_SIZE_WIDTH - 1, CAP_SIZE_HEIGHT - 1);
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
    if(jpeg_write_scanlines(sp_cinfo, s_jsamprow, 1) != 1) {
      LOG_E("Single Encode Stop at line %d\n", y);
      ret |= liveviewCtrl_encodeJpegFinish();
      ret |= liveviewCtrl_writeFileFinish();
      return ret;
    }
  }
  ret |= liveviewCtrl_encodeJpegFinish();
  ret |= liveviewCtrl_writeFileFinish();
  LOG("encode time = %d\n", HAL_GetTick() - start);

  LOG("Single Encode Finish\n");

  /*** start liveview ***/
  ret |= liveviewCtrl_startLiveView();
  return RET_OK;
}

static RET liveviewCtrl_writeFileStart(char* filename)
{
  RET ret = RET_OK;
  sp_fatFs = pvPortMalloc(sizeof(FATFS));
  sp_fil   = pvPortMalloc(sizeof(FIL));
  if( (sp_fatFs == 0) || (sp_fil == 0) ) {
    return RET_ERR;
  }

  ret |= f_mount(sp_fatFs, "", 0);
  ret |= f_open(sp_fil, filename, FA_WRITE | FA_CREATE_NEW);

  return ret;
}

static RET liveviewCtrl_writeFileFinish()
{
  RET ret = RET_OK;
  ret |= f_close(sp_fil);
  ret |= f_mount(0, "", 0);
  vPortFree(sp_fil);
  vPortFree(sp_fatFs);
  return ret;
}

static RET liveviewCtrl_encodeJpegStart()
{
  RET ret = RET_OK;

  sp_cinfo = pvPortMalloc(sizeof(struct jpeg_compress_struct));
  sp_jerr  = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  sp_lineBuffRGB888 = pvPortMalloc(CAP_SIZE_WIDTH * 3);

  if( (sp_cinfo == 0) || (sp_jerr == 0) || (sp_lineBuffRGB888 == 0) ){
    LOG_E("not enough memory\n");
    vPortFree(sp_cinfo);
    vPortFree(sp_jerr);
    vPortFree(sp_lineBuffRGB888);
    return RET_ERR;
  }

  s_jsamprow[0] = sp_lineBuffRGB888;

  sp_cinfo->err = jpeg_std_error(sp_jerr);
  jpeg_create_compress(sp_cinfo);

  jpeg_stdio_dest(sp_cinfo, sp_fil);

  sp_cinfo->image_width  = CAP_SIZE_WIDTH;
  sp_cinfo->image_height = CAP_SIZE_HEIGHT;
  sp_cinfo->input_components = 3;
  sp_cinfo->in_color_space = JCS_RGB;
  jpeg_set_defaults(sp_cinfo);
  jpeg_set_quality(sp_cinfo, 70, TRUE);
  jpeg_start_compress(sp_cinfo, TRUE);

  return ret;
}

static RET liveviewCtrl_encodeJpegFinish()
{
  RET ret = RET_OK;

  jpeg_finish_compress(sp_cinfo);
  jpeg_destroy_compress(sp_cinfo);

  vPortFree(sp_cinfo);
  vPortFree(sp_jerr);
  vPortFree(sp_lineBuffRGB888);

  return ret;
}


static RET liveviewCtrl_movieRecordStart()
{
  LOG("Movie Encode Start\n");
  RET ret = RET_OK;
  char filename[14] = CAP_NAME_MOVIE;
  ret |= liveviewCtrl_stopLiveView();

  ret |= liveviewCtrl_generateFilename(filename, CAP_NAME_INDEX_POS);
  LOG("create %s\n", filename);
  ret |= liveviewCtrl_writeFileStart(filename);
  if(ret != RET_OK) {
    ret |= liveviewCtrl_writeFileFinish();
    LOG_E("Movie Encode End by error: %08X\n", ret);
    return ret;
  }

  return ret;
}

static RET liveviewCtrl_movieRecordFinish()
{
  RET ret = RET_OK;
  ret |= liveviewCtrl_writeFileFinish();

  liveviewCtrl_stopLiveView();

  /*** start liveview ***/
  ret |= liveviewCtrl_startLiveView();


  s_requestStopMovie = 0;
  s_status = ACTIVE;
  LOG("Movie Encode Finish\n");
  return ret;
}

static RET liveviewCtrl_movieRecordFrame()
{
  LOG("Movie One Frame Encode Start\n");
  RET ret = RET_OK;
  uint16_t dummy;

  // todo wait until dcmi dma done. callbackŽg‚¤

  ret |= liveviewCtrl_encodeJpegStart();
  if(ret != RET_OK) {
    ret |= liveviewCtrl_encodeJpegFinish();
    ret |= liveviewCtrl_writeFileFinish();
    LOG_E("Single Encode End by error: %08X\n", ret);
    return ret;
  }
  lcdIli9341_setAreaRead(0, 0, CAP_SIZE_WIDTH - 1, CAP_SIZE_HEIGHT - 1);
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
    if(jpeg_write_scanlines(sp_cinfo, s_jsamprow, 1) != 1) {
      LOG_E("Single Encode Stop at line %d\n", y);
      ret |= liveviewCtrl_encodeJpegFinish();
      ret |= liveviewCtrl_writeFileFinish();
      return ret;
    }
  }
  ret |= liveviewCtrl_encodeJpegFinish();

  void* displayHandle = display_getDisplayHandle();
  display_setArea(0, 0, CAP_SIZE_WIDTH - 1, CAP_SIZE_HEIGHT - 1);
  ret |= camera_startCap(CAMERA_CAP_SINGLE_FRAME, displayHandle);

  return ret;
}



static RET liveviewCtrl_generateFilename(char* filename, uint8_t posIndex)
{
  static uint8_t s_index = 0;

  FRESULT ret;
  FATFS *p_fatFs = pvPortMalloc(sizeof(FATFS));
  FIL   *p_fil   = pvPortMalloc(sizeof(FIL));
  if( (p_fatFs == 0) || (p_fil == 0) ) {
    vPortFree(p_fil);
    vPortFree(p_fatFs);
    return RET_ERR;
  }

  f_mount(p_fatFs, "", 0);
  do {
    uint8_t num100 = (s_index/100) % 10;
    uint8_t num10  = (s_index/10) % 10;
    uint8_t num1   = (s_index/1) % 10;
    filename[posIndex + 0] = '0' + num100;
    filename[posIndex + 1] = '0' + num10;
    filename[posIndex + 2] = '0' + num1;
    s_index++;
    ret = f_open(p_fil, filename, FA_OPEN_EXISTING);
    f_close(p_fil);
  } while(ret == FR_OK);

  f_mount(0, "", 0);
  vPortFree(p_fil);
  vPortFree(p_fatFs);

  if(ret == FR_NO_FILE) {
      return RET_OK;
  }
  return RET_ERR;
}
